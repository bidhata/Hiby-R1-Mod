#include "gb.h"
#include "ppu.h"
#include <string.h>

#define DMG_COLORS 4
static const u32 dmg_palette[DMG_COLORS] = {
    0xFF9BBC0F, /* lightest */
    0xFF8BAC0F,
    0xFF306230,
    0xFF0F380F  /* darkest */
};

/* Scanline timings in T-cycles; a full line is 456. */
#define MODE2_CYCLES 80
#define MODE3_CYCLES 172
#define MODE0_CYCLES 204
#define LINE_CYCLES  456
#define FRAME_CYCLES 70224

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

    for (int i = 0; i < GB_WIDTH * GB_HEIGHT; i++)
        ppu->framebuffer[i] = dmg_palette[0];
}

static inline bool ppu_lcdc_bit(u8 lcdc, int bit) {
    return (lcdc >> bit) & 1;
}

static inline u8 ppu_fetch_vram(struct gb *gb, u16 addr) {
    return gb->vram[addr & 0x1FFF];
}

static inline u8 ppu_fetch_oam(struct gb *gb, int idx) {
    if (idx < 0 || idx >= 0xA0) return 0;
    return gb->oam[idx];
}

/* Reads one pixel out of a tile in the given tile data area. */
static u8 ppu_get_tile_pixel(struct gb *gb, u16 tile_data_base, u8 tile_idx,
                             int pixel_x, int pixel_y) {
    u16 tile_addr;
    if (tile_data_base == 0x8000) {
        tile_addr = (u16)(0x8000 + tile_idx * 16);
    } else {
        /* 0x8800 area uses signed tile numbers relative to 0x9000. */
        tile_addr = (u16)(0x9000 + (s8)tile_idx * 16);
    }

    u16 row_addr = (u16)(tile_addr + (pixel_y & 7) * 2);
    u8 lo = ppu_fetch_vram(gb, row_addr);
    u8 hi = ppu_fetch_vram(gb, (u16)(row_addr + 1));

    int bit = 7 - (pixel_x & 7);
    return (u8)((((hi >> bit) & 1) << 1) | ((lo >> bit) & 1));
}

/* Renders the background and window for one scanline. */
static void ppu_render_bg_line(struct gb *gb, gb_ppu_t *ppu, int line) {
    u32 *row = &ppu->framebuffer[line * GB_WIDTH];

    if (!ppu_lcdc_bit(ppu->lcdc, 0)) {
        /* Background off: the line is blank and nothing shades sprites. */
        for (int pixel = 0; pixel < GB_WIDTH; pixel++) {
            ppu->bg_color[pixel] = 0;
            row[pixel] = dmg_palette[0];
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

        u8 color_idx = ppu_get_tile_pixel(gb, tile_data_base, tile_idx, src_x, src_y);
        ppu->bg_color[pixel] = color_idx;
        row[pixel] = dmg_palette[ppu->bg_palette[color_idx & 3] & 3];
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

    /* On DMG the leftmost sprite wins, ties broken by OAM index. Sort so the
     * lowest priority is drawn first and higher priorities overwrite it. */
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

        if (sprite_height == 16) tile_idx &= 0xFE;

        int tile_y = line - sprite_y;
        if (flip_y) tile_y = sprite_height - 1 - tile_y;

        u16 tile_addr = (u16)(0x8000 + tile_idx * 16 + tile_y * 2);
        u8 lo = ppu_fetch_vram(gb, tile_addr);
        u8 hi = ppu_fetch_vram(gb, (u16)(tile_addr + 1));

        for (int px = 0; px < 8; px++) {
            int screen_x = sprite_x + px;
            if (screen_x < 0 || screen_x >= GB_WIDTH) continue;

            int bit = flip_x ? px : (7 - px);
            u8 color_idx = (u8)((((hi >> bit) & 1) << 1) | ((lo >> bit) & 1));
            if (color_idx == 0) continue; /* Colour 0 is transparent */

            /* With the priority flag set, non-zero background pixels win. */
            if (bg_priority && ppu->bg_color[screen_x] != 0) continue;

            row[screen_x] = dmg_palette[ppu->ob_palette[palette_num][color_idx] & 3];
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
                ppu->framebuffer[i] = dmg_palette[0];
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
