#include "gb.h"
#include "ppu.h"
#include "palette.h"
#include <string.h>

#define DMG_COLORS 4

/* Scanline timings in T-cycles; a full line is 456. */
#define MODE2_CYCLES 80
#define MODE3_CYCLES 172
#define MODE0_CYCLES 204
#define LINE_CYCLES  456
#define FRAME_CYCLES 70224

void gb_ppu_set_dmg_colors(gb_ppu_t *ppu, const u32 *colors) {
    for (int i = 0; i < DMG_COLORS; i++) ppu->dmg_colors[i] = colors[i];
}

void gb_ppu_init(gb_ppu_t *ppu) {
    memset(ppu, 0, sizeof(*ppu));
    ppu->mode = 2;
    ppu->mode_clock = 0;
    ppu->ly = 0;
    ppu->lyc = 0;
    ppu->lcdc = 0x91;
    ppu->stat = 0x02;
    ppu->window_line = 0;
    ppu->frame_ready = false;

    /* Post-boot palettes: BGP = 0xFC, OBP0 = OBP1 = 0xFF. */
    ppu->bg_palette[0] = 0;
    ppu->bg_palette[1] = 3;
    ppu->bg_palette[2] = 3;
    ppu->bg_palette[3] = 3;
    for (int i = 0; i < 4; i++) {
        ppu->ob_palette[0][i] = 3;
        ppu->ob_palette[1][i] = 3;
    }

    gb_ppu_set_dmg_colors(ppu, gb_palette_colors(GB_PALETTE_GREEN));

    /* CGB palette memory powers on filled with ones, which reads as white. */
    memset(ppu->cgb_bg_palette, 0xFF, sizeof(ppu->cgb_bg_palette));
    memset(ppu->cgb_ob_palette, 0xFF, sizeof(ppu->cgb_ob_palette));

    for (int i = 0; i < GB_WIDTH * GB_HEIGHT; i++)
        ppu->framebuffer[i] = ppu->dmg_colors[0];
}

/* Expands an RGB555 pair from CGB palette memory into an ARGB pixel.
 *
 * Each 5-bit channel is scaled straight up to 8 bits, repeating the high bits
 * in the low ones so 31 maps to 255. The alternative is to model the console's
 * dim, washed-out screen, which is faithful to the original but looks muddy on
 * a bright modern panel: a pure yellow comes out orange. Vivid wins here, and
 * it matches the colours the cartridge actually asked for. */
static u32 cgb_color(const u8 *palette, int index) {
    u16 raw = (u16)(palette[index * 2] | (palette[index * 2 + 1] << 8));

    u32 r = raw & 0x1F;
    u32 g = (raw >> 5) & 0x1F;
    u32 b = (raw >> 10) & 0x1F;

    r = (r << 3) | (r >> 2);
    g = (g << 3) | (g >> 2);
    b = (b << 3) | (b >> 2);

    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

void gb_ppu_write_cgb_palette(gb_ppu_t *ppu, bool sprite, u8 value) {
    u8 *spec = sprite ? &ppu->ocps : &ppu->bcps;
    u8 *data = sprite ? ppu->cgb_ob_palette : ppu->cgb_bg_palette;

    data[*spec & 0x3F] = value;

    /* Bit 7 makes the index step on after every write. */
    if (*spec & 0x80) {
        *spec = (u8)(0x80 | ((*spec + 1) & 0x3F));
    }
}

u8 gb_ppu_read_cgb_palette(const gb_ppu_t *ppu, bool sprite) {
    const u8 *data = sprite ? ppu->cgb_ob_palette : ppu->cgb_bg_palette;
    u8 spec = sprite ? ppu->ocps : ppu->bcps;
    return data[spec & 0x3F];
}

static inline bool ppu_lcdc_bit(u8 lcdc, int bit) {
    return (lcdc >> bit) & 1;
}

/* VRAM bank 0 holds tile data and tile maps; on CGB, bank 1 holds the matching
 * per-tile attributes and a second set of tile patterns. */
static inline u8 ppu_fetch_vram(struct gb *gb, u16 addr) {
    return gb->vram[addr & 0x1FFF];
}

static inline u8 ppu_fetch_vram_bank(struct gb *gb, u16 addr, int bank) {
    return gb->vram[(bank ? 0x2000 : 0) + (addr & 0x1FFF)];
}

static inline u8 ppu_fetch_oam(struct gb *gb, int idx) {
    if (idx < 0 || idx >= 0xA0) return 0;
    return gb->oam[idx];
}

/* Reads one pixel out of a tile in the given tile data area. On CGB a tile can
 * live in either VRAM bank and be mirrored on either axis, all of which the
 * tile's attribute byte selects. */
static u8 ppu_get_tile_pixel(struct gb *gb, u16 tile_data_base, u8 tile_idx,
                             int pixel_x, int pixel_y, int bank,
                             bool flip_x, bool flip_y) {
    u16 tile_addr;
    if (tile_data_base == 0x8000) {
        tile_addr = (u16)(0x8000 + tile_idx * 16);
    } else {
        /* 0x8800 area uses signed tile numbers relative to 0x9000. */
        tile_addr = (u16)(0x9000 + (s8)tile_idx * 16);
    }

    int row_in_tile = pixel_y & 7;
    if (flip_y) row_in_tile = 7 - row_in_tile;

    u16 row_addr = (u16)(tile_addr + row_in_tile * 2);
    u8 lo = ppu_fetch_vram_bank(gb, row_addr, bank);
    u8 hi = ppu_fetch_vram_bank(gb, (u16)(row_addr + 1), bank);

    int bit = flip_x ? (pixel_x & 7) : 7 - (pixel_x & 7);
    return (u8)((((hi >> bit) & 1) << 1) | ((lo >> bit) & 1));
}

/* Renders the background and window for one scanline. */
static void ppu_render_bg_line(struct gb *gb, gb_ppu_t *ppu, int line) {
    u32 *row = &ppu->framebuffer[line * GB_WIDTH];

    /* On CGB, LCDC bit 0 no longer blanks the background: it only drops the
     * background's claim to priority over sprites. */
    if (!ppu_lcdc_bit(ppu->lcdc, 0) && !gb->cgb_mode) {
        /* Background off: the line is blank and nothing shades sprites. */
        for (int pixel = 0; pixel < GB_WIDTH; pixel++) {
            ppu->bg_color[pixel] = 0;
            ppu->bg_priority[pixel] = false;
            row[pixel] = ppu->dmg_colors[0];
        }
        return;
    }

    u16 bg_map_base  = ppu_lcdc_bit(ppu->lcdc, 3) ? 0x9C00 : 0x9800;
    u16 win_map_base = ppu_lcdc_bit(ppu->lcdc, 6) ? 0x9C00 : 0x9800;
    u16 tile_data_base = ppu_lcdc_bit(ppu->lcdc, 4) ? 0x8000 : 0x8800;

    bool window_on_line = ppu_lcdc_bit(ppu->lcdc, 5) && line >= (int)ppu->wy;
    bool window_used = false;

    for (int pixel = 0; pixel < GB_WIDTH; pixel++) {
        u16 map_base;
        int src_x, src_y;

        if (window_on_line && pixel + 7 >= (int)ppu->wx) {
            map_base = win_map_base;
            src_x = pixel + 7 - (int)ppu->wx;
            src_y = ppu->window_line;
            window_used = true;
        } else {
            map_base = bg_map_base;
            src_x = (int)((u8)(ppu->scx + pixel));
            src_y = (int)((u8)(ppu->scy + line));
        }

        u16 map_addr = (u16)(map_base + (src_y / 8) * 32 + (src_x / 8));
        u8 tile_idx = ppu_fetch_vram(gb, map_addr);

        /* The attribute for a tile sits at the same map address in bank 1. */
        u8 attr = gb->cgb_mode ? ppu_fetch_vram_bank(gb, map_addr, 1) : 0;
        int tile_bank = (attr >> 3) & 1;
        bool flip_x = (attr >> 5) & 1;
        bool flip_y = (attr >> 6) & 1;

        u8 color_idx = ppu_get_tile_pixel(gb, tile_data_base, tile_idx,
                                          src_x, src_y, tile_bank,
                                          flip_x, flip_y);
        ppu->bg_color[pixel] = color_idx;

        if (gb->cgb_mode) {
            /* Attribute bit 7 puts this tile in front of sprites, but only
             * while LCDC bit 0 grants the background that priority at all. */
            ppu->bg_priority[pixel] =
                ((attr >> 7) & 1) && ppu_lcdc_bit(ppu->lcdc, 0);
            row[pixel] = cgb_color(ppu->cgb_bg_palette,
                                   (attr & 0x07) * 4 + color_idx);
        } else {
            ppu->bg_priority[pixel] = false;
            row[pixel] = ppu->dmg_colors[ppu->bg_palette[color_idx & 3] & 3];
        }
    }

    if (window_used) ppu->window_line++;
}

/* Renders the sprites that overlap one scanline. */
static void ppu_render_sprite_line(struct gb *gb, gb_ppu_t *ppu, int line) {
    if (!ppu_lcdc_bit(ppu->lcdc, 1)) return;

    int sprite_height = ppu_lcdc_bit(ppu->lcdc, 2) ? 16 : 8;
    int selected[10];
    int count = 0;

    /* Hardware scans OAM in order and keeps the first ten hits. */
    for (int i = 0; i < 40 && count < 10; i++) {
        int sprite_y = ppu_fetch_oam(gb, i * 4) - 16;
        if (line >= sprite_y && line < sprite_y + sprite_height) {
            selected[count++] = i;
        }
    }

    /* On DMG the leftmost sprite wins, ties broken by OAM index; on CGB the OAM
     * index alone decides, so the list is already in priority order. Sort so
     * the lowest priority is drawn first and higher priorities overwrite it. */
    if (!gb->cgb_mode) {
        for (int i = 1; i < count; i++) {
            int cur = selected[i];
            int cur_x = ppu_fetch_oam(gb, cur * 4 + 1);
            int j = i - 1;
            while (j >= 0) {
                int other_x = ppu_fetch_oam(gb, selected[j] * 4 + 1);
                if (other_x < cur_x || (other_x == cur_x && selected[j] < cur)) break;
                selected[j + 1] = selected[j];
                j--;
            }
            selected[j + 1] = cur;
        }
    }

    u32 *row = &ppu->framebuffer[line * GB_WIDTH];

    for (int s = count - 1; s >= 0; s--) {
        int i = selected[s];
        int sprite_y = ppu_fetch_oam(gb, i * 4) - 16;
        int sprite_x = ppu_fetch_oam(gb, i * 4 + 1) - 8;
        u8 tile_idx = ppu_fetch_oam(gb, i * 4 + 2);
        u8 flags = ppu_fetch_oam(gb, i * 4 + 3);

        bool bg_priority = (flags >> 7) & 1;
        bool flip_y = (flags >> 6) & 1;
        bool flip_x = (flags >> 5) & 1;
        u8 palette_num = (flags >> 4) & 1;
        /* CGB sprites choose one of eight palettes and either VRAM bank. */
        int tile_bank = gb->cgb_mode ? ((flags >> 3) & 1) : 0;
        int cgb_palette = flags & 0x07;

        if (sprite_height == 16) tile_idx &= 0xFE;

        int tile_y = line - sprite_y;
        if (flip_y) tile_y = sprite_height - 1 - tile_y;

        u16 tile_addr = (u16)(0x8000 + tile_idx * 16 + tile_y * 2);
        u8 lo = ppu_fetch_vram_bank(gb, tile_addr, tile_bank);
        u8 hi = ppu_fetch_vram_bank(gb, (u16)(tile_addr + 1), tile_bank);

        for (int px = 0; px < 8; px++) {
            int screen_x = sprite_x + px;
            if (screen_x < 0 || screen_x >= GB_WIDTH) continue;

            int bit = flip_x ? px : (7 - px);
            u8 color_idx = (u8)((((hi >> bit) & 1) << 1) | ((lo >> bit) & 1));
            if (color_idx == 0) continue; /* Colour 0 is transparent */

            /* Either the sprite yields to the background, or the background
             * tile claimed priority for itself; a background pixel of colour 0
             * never wins either way. */
            if (ppu->bg_color[screen_x] != 0 &&
                (bg_priority || ppu->bg_priority[screen_x])) {
                continue;
            }

            row[screen_x] = gb->cgb_mode
                ? cgb_color(ppu->cgb_ob_palette, cgb_palette * 4 + color_idx)
                : ppu->dmg_colors[ppu->ob_palette[palette_num][color_idx] & 3];
        }
    }
}

static void ppu_render_line(struct gb *gb, gb_ppu_t *ppu) {
    int line = ppu->ly;
    if (line < 0 || line >= GB_HEIGHT) return;

    ppu_render_bg_line(gb, ppu, line);
    ppu_render_sprite_line(gb, ppu, line);
}

/* Recomputes the STAT interrupt line and fires on a rising edge only. */
static void ppu_update_stat(struct gb *gb, gb_ppu_t *ppu) {
    bool lyc_match = (ppu->ly == ppu->lyc);

    if (lyc_match) ppu->stat |= 0x04;
    else ppu->stat &= ~0x04;

    bool line = false;
    if ((ppu->stat & 0x40) && lyc_match) line = true;
    if ((ppu->stat & 0x20) && ppu->mode == 2) line = true;
    if ((ppu->stat & 0x10) && ppu->mode == 1) line = true;
    if ((ppu->stat & 0x08) && ppu->mode == 0) line = true;

    if (line && !ppu->stat_irq_line) {
        gb_cpu_trigger_interrupt(gb, 1); /* LCD STAT interrupt */
    }
    ppu->stat_irq_line = line;

    gb->io[0x41] = (ppu->stat & 0x7F) | 0x80;
    gb->io[0x44] = ppu->ly;
}

static void ppu_set_mode(struct gb *gb, gb_ppu_t *ppu, int mode) {
    ppu->mode = mode;
    ppu->stat = (u8)((ppu->stat & ~0x03) | (mode & 0x03));
    ppu_update_stat(gb, ppu);
}

void gb_ppu_step(gb_ppu_t *ppu, struct gb *gb, int cycles) {
    /* LCD off: the panel stays blank, but frames must still be paced so the
     * main loop makes progress and input keeps being polled. */
    if (!ppu_lcdc_bit(ppu->lcdc, 7)) {
        ppu->blank_clock += cycles;
        if (ppu->blank_clock >= FRAME_CYCLES) {
            ppu->blank_clock -= FRAME_CYCLES;
            for (int i = 0; i < GB_WIDTH * GB_HEIGHT; i++)
                ppu->framebuffer[i] = ppu->dmg_colors[0];
            ppu->frame_ready = true;
        }
        return;
    }
    ppu->blank_clock = 0;

    ppu->mode_clock += cycles;

    switch (ppu->mode) {
    case 2: /* OAM scan */
        if (ppu->mode_clock >= MODE2_CYCLES) {
            ppu->mode_clock -= MODE2_CYCLES;
            ppu_set_mode(gb, ppu, 3);
        }
        break;

    case 3: /* Drawing */
        if (ppu->mode_clock >= MODE3_CYCLES) {
            ppu->mode_clock -= MODE3_CYCLES;
            ppu_render_line(gb, ppu);
            ppu_set_mode(gb, ppu, 0);
            /* HBlank has just begun: an HDMA in HBlank mode moves one block
             * here, which is what lets a game stream tiles mid-frame. */
            gb_mmu_hdma_hblank(gb);
        }
        break;

    case 0: /* HBlank */
        if (ppu->mode_clock >= MODE0_CYCLES) {
            ppu->mode_clock -= MODE0_CYCLES;
            ppu->ly++;

            if (ppu->ly == GB_HEIGHT) {
                ppu_set_mode(gb, ppu, 1);
                gb_cpu_trigger_interrupt(gb, 0); /* VBlank interrupt */
                ppu->frame_ready = true;
            } else {
                ppu_set_mode(gb, ppu, 2);
            }
        }
        break;

    case 1: /* VBlank */
        if (ppu->mode_clock >= LINE_CYCLES) {
            ppu->mode_clock -= LINE_CYCLES;
            ppu->ly++;

            if (ppu->ly > 153) {
                ppu->ly = 0;
                ppu->window_line = 0;
                ppu_set_mode(gb, ppu, 2);
            } else {
                ppu_update_stat(gb, ppu);
            }
        }
        break;

    default:
        ppu_set_mode(gb, ppu, 2);
        break;
    }
}
