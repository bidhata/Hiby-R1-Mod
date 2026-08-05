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

static const u8 duty_cycles[4] = {0x01, 0x81, 0x7E, 0x82};

void gb_ppu_init(gb_ppu_t *ppu) {
    memset(ppu, 0, sizeof(*ppu));
    ppu->mode = 2;
    ppu->mode_clock = 0;
    ppu->line = 0;
    ppu->ly = 0;
    ppu->lyc = 0;
    ppu->lcdc = 0x91;
    ppu->stat = 0;
    ppu->scx = 0;
    ppu->scy = 0;
    ppu->wx = 0;
    ppu->wy = 0;
    ppu->frame_ready = false;

    /* DMG default palette */
    ppu->bg_palette[0] = 0;
    ppu->bg_palette[1] = 1;
    ppu->bg_palette[2] = 2;
    ppu->bg_palette[3] = 3;

    ppu->ob_palette[0][0] = 0;
    ppu->ob_palette[0][1] = 1;
    ppu->ob_palette[1][0] = 2;
    ppu->ob_palette[1][1] = 3;

    /* Clear framebuffer to white (color 0) */
    for (int i = 0; i < GB_WIDTH * GB_HEIGHT; i++)
        ppu->framebuffer[i] = dmg_palette[0];
}

static inline bool ppu_lcdc_bit(u8 lcdc, int bit) {
    return (lcdc >> bit) & 1;
}

static inline u32 ppu_resolve_color(u8 palette_idx) {
    if (palette_idx >= DMG_COLORS) return dmg_palette[0];
    return dmg_palette[palette_idx];
}

static u8 ppu_fetch_vram(struct gb *gb, u16 addr) {
    return gb->vram[addr & 0x1FFF];
}

static u8 ppu_fetch_oam(gb_ppu_t *ppu, int idx) {
    return ppu->oam[idx & 0xFF];
}

/* Fetch tile pixel from VRAM tile data area */
static u8 ppu_get_tile_pixel(struct gb *gb, u16 tile_data_base, int tile_idx,
                             int pixel_x, int pixel_y) {
    /* Tile data can be at 0x8000 or 0x8800 (signed indexing) */
    u16 tile_addr;
    if (tile_data_base == 0x8000) {
        tile_addr = 0x8000 + (tile_idx & 0xFF) * 16;
    } else {
        /* Signed: 0x9000 + (s8)tile_idx * 16 */
        s8 stile = (s8)tile_idx;
        tile_addr = 0x9000 + stile * 16;
    }

    /* Each row is 2 bytes: low byte first, high byte second */
    u16 row_addr = tile_addr + (pixel_y & 7) * 2;
    u8 lo = ppu_fetch_vram(gb, row_addr);
    u8 hi = ppu_fetch_vram(gb, row_addr + 1);

    /* Pixel 7-bit is leftmost, 0 is rightmost */
    int bit = 7 - (pixel_x & 7);
    return ((hi >> bit) & 1) << 1 | ((lo >> bit) & 1);
}

/* Render one scanline of background + window */
static void ppu_render_bg_line(struct gb *gb, gb_ppu_t *ppu, int line) {
    if (!ppu_lcdc_bit(ppu->lcdc, 0)) return; /* BG enabled? */

    u16 tile_map_base = ppu_lcdc_bit(ppu->lcdc, 3) ? 0x9C00 : 0x9800;
    u16 tile_data_base = ppu_lcdc_bit(ppu->lcdc, 4) ? 0x8000 : 0x8800;

    u8 scroll_y = (u8)(ppu->scy + (u8)line);
    u8 win_y = (u8)(line - ppu->wy);

    bool win_rendered = false;

    for (int pixel = 0; pixel < GB_WIDTH; pixel++) {
        u8 scroll_x = (u8)(ppu->scx + pixel);
        u8 tile_x = scroll_x >> 3;
        u8 tile_y = scroll_y >> 3;

        /* Check window for this pixel */
        bool use_window = false;
        if (ppu_lcdc_bit(ppu->lcdc, 5) && ppu->window_enabled) {
            if (ppu->wy <= (u8)line && ppu->wx <= (u8)(pixel + 7)) {
                u8 win_pixel = (u8)(pixel + 7 - ppu->wx);
                u8 win_scroll_y = (u8)line - ppu->wy;
                u8 win_tile_x = win_pixel >> 3;
                u8 win_tile_y = win_scroll_y >> 3;
                tile_x = win_tile_x;
                tile_y = win_tile_y;
                use_window = true;
                win_rendered = true;
            }
        }

        u16 map_addr = tile_map_base + tile_y * 32 + tile_x;
        u8 tile_idx = ppu_fetch_vram(gb, map_addr);

        int pixel_y = tile_y * 8 + (scroll_y & 7);
        if (use_window) {
            pixel_y = (u8)line - ppu->wy;
        }

        u8 color_idx = ppu_get_tile_pixel(gb, tile_data_base, tile_idx,
                                           tile_x * 8 + (scroll_x & 7),
                                           pixel_y & 7);

        u8 palette_entry = ppu->bg_palette[color_idx & 3];
        ppu->framebuffer[line * GB_WIDTH + pixel] = dmg_palette[palette_entry & 3];
    }
}

/* Render sprites for current scanline */
static void ppu_render_sprite_line(struct gb *gb, gb_ppu_t *ppu, int line) {
    if (!ppu_lcdc_bit(ppu->lcdc, 1)) return; /* Sprites enabled? */
    if (!ppu->sprites_enabled) return;

    int sprite_height = ppu_lcdc_bit(ppu->lcdc, 2) ? 16 : 8;
    int sprites_on_line[10];
    int sprite_count = 0;

    /* Find up to 10 sprites on this line (first 10 in OAM) */
    for (int i = 0; i < 40 && sprite_count < 10; i++) {
        u8 y = ppu_fetch_oam(ppu, i * 4) - 16;
        if ((u8)line >= y && (u8)line < y + sprite_height) {
            sprites_on_line[sprite_count++] = i;
        }
    }

    /* Render in reverse order (lower index = higher priority) */
    for (int s = sprite_count - 1; s >= 0; s--) {
        int i = sprites_on_line[s];
        u8 sprite_y = ppu_fetch_oam(ppu, i * 4) - 16;
        u8 sprite_x = ppu_fetch_oam(ppu, i * 4 + 1) - 8;
        u8 tile_idx = ppu_fetch_oam(ppu, i * 4 + 2);
        u8 flags = ppu_fetch_oam(ppu, i * 4 + 3);

        bool flip_y = (flags >> 6) & 1;
        bool flip_x = (flags >> 5) & 1;
        bool bg_over = (flags >> 7) & 1;
        u8 palette_num = (flags >> 4) & 1;

        if (sprite_height == 16)
            tile_idx &= 0xFE;

        int tile_y = line - sprite_y;
        if (flip_y) tile_y = sprite_height - 1 - tile_y;

        u16 tile_addr = 0x8000 + tile_idx * 16 + tile_y * 2;
        u8 lo = ppu_fetch_vram(gb, tile_addr);
        u8 hi = ppu_fetch_vram(gb, tile_addr + 1);

        for (int px = 0; px < 8; px++) {
            int screen_x = sprite_x + px;
            if (screen_x < 0 || screen_x >= GB_WIDTH) continue;

            int bit = flip_x ? px : (7 - px);
            u8 color_idx = ((hi >> bit) & 1) << 1 | ((lo >> bit) & 1);

            if (color_idx == 0) continue; /* Transparent */

            u32 *dst = &ppu->framebuffer[line * GB_WIDTH + screen_x];
            u32 bg_color = *dst;

            /* BG-over-sprite: if bg palette entry 0, sprite is visible */
            if (bg_over) {
                /* Check if background pixel was palette index 0 */
                /* We don't store that info, so compare to white */
                if (bg_color == dmg_palette[ppu->bg_palette[0]]) {
                    /* BG pixel is color 0, sprite shows through */
                } else {
                    continue;
                }
            }

            u8 palette_entry = ppu->ob_palette[palette_num][color_idx - 1];
            *dst = dmg_palette[palette_entry & 3];
        }
    }
}

/* Check LYC=LY coincidence and STAT interrupt */
static void ppu_check_stat(struct gb *gb, gb_ppu_t *ppu) {
    bool lyc_match = (ppu->ly == ppu->lyc);
    if (lyc_match) {
        ppu->stat |= 0x04; /* LYC=LY flag */
    } else {
        ppu->stat &= ~0x04;
    }

    /* STAT interrupt: mode or LYC coincidence */
    if ((ppu->stat & 0x40) && lyc_match) {
        gb_cpu_trigger_interrupt(gb, 2); /* STAT interrupt */
    }
}

/* Render the current scanline */
static void ppu_render_line(struct gb *gb, gb_ppu_t *ppu) {
    /* Render background/window */
    ppu_render_bg_line(gb, ppu, ppu->line);
    /* Render sprites */
    ppu_render_sprite_line(gb, ppu, ppu->line);
}

/* Set mode and fire appropriate interrupt */
static void ppu_set_mode(struct gb *gb, gb_ppu_t *ppu, int mode) {
    ppu->mode = mode;

    /* Update STAT mode bits */
    ppu->stat = (ppu->stat & ~0x03) | (mode & 0x03);

    /* Mode 0 (HBlank) interrupt */
    if (mode == 0 && (ppu->stat & 0x08))
        gb_cpu_trigger_interrupt(gb, 2);
    /* Mode 1 (VBlank) interrupt */
    if (mode == 1 && (ppu->stat & 0x10))
        gb_cpu_trigger_interrupt(gb, 2);
    /* Mode 2 (OAM) interrupt */
    if (mode == 2 && (ppu->stat & 0x20))
        gb_cpu_trigger_interrupt(gb, 2);
}

void gb_ppu_step(gb_ppu_t *ppu, struct gb *gb, int cycles) {
    /* If LCD disabled, do nothing */
    if (!ppu_lcdc_bit(ppu->lcdc, 7)) return;

    ppu->mode_clock += cycles;

    switch (ppu->mode) {
    case 2: /* OAM Scan: 80 cycles */
        if (ppu->mode_clock >= 80) {
            ppu->mode_clock -= 80;
            ppu_set_mode(gb, ppu, 3);
        }
        break;

    case 3: /* Drawing: ~172 cycles */
        if (ppu->mode_clock >= 172) {
            ppu->mode_clock -= 172;
            ppu_set_mode(gb, ppu, 0);

            /* Render the scanline */
            ppu_render_line(gb, ppu);
        }
        break;

    case 0: /* HBlank: ~204 cycles */
        if (ppu->mode_clock >= 204) {
            ppu->mode_clock -= 204;
            ppu->ly++;

            ppu_check_stat(gb, ppu);

            if (ppu->ly == 144) {
                /* Enter VBlank */
                ppu_set_mode(gb, ppu, 1);
                gb_cpu_trigger_interrupt(gb, 4); /* VBlank interrupt */
            } else {
                /* Next scanline: OAM scan */
                ppu_set_mode(gb, ppu, 2);
            }
        }
        break;

    case 1: /* VBlank: 4560 cycles total (10 lines * 456) */
        if (ppu->mode_clock >= 456) {
            ppu->mode_clock -= 456;
            ppu->ly++;

            ppu_check_stat(gb, ppu);

            if (ppu->ly > 153) {
                /* End of VBlank, start new frame */
                ppu->ly = 0;
                ppu->frame_ready = true;
                ppu_set_mode(gb, ppu, 2);
                ppu_check_stat(gb, ppu);
            }
        }
        break;
    }
}
