#ifndef GB_PPU_H
#define GB_PPU_H

#include "types.h"

typedef struct {
    u32 framebuffer[GB_WIDTH * GB_HEIGHT];
    int mode;
    int mode_clock;
    u8 bg_palette[4];
    u8 ob_palette[2][4];
    u8 scx, scy;
    u8 wx, wy;
    u8 ly;
    u8 lyc;
    u8 stat;
    u8 lcdc;

    /* Window line counter: only advances on lines where the window is drawn. */
    int window_line;
    /* Previous state of the STAT interrupt sources, for edge detection. */
    bool stat_irq_line;
    /* Cycle budget used to pace frames while the LCD is switched off. */
    int blank_clock;
    /* Palette index (0-3) of the background pixel drawn on the current line. */
    u8 bg_color[GB_WIDTH];
    /* Set where the background tile asked to sit above sprites (CGB attribute
     * bit 7). Sprites consult this along with their own priority flag. */
    bool bg_priority[GB_WIDTH];

    /* Game Boy Color palette memory: eight background and eight sprite
     * palettes of four RGB555 colours each, addressed a byte at a time through
     * BCPS/BCPD and OCPS/OCPD. */
    u8 cgb_bg_palette[64];
    u8 cgb_ob_palette[64];
    u8 bcps;    /* background palette index, bit 7 auto-increments */
    u8 ocps;    /* sprite palette index */

    /* The four DMG shades, chosen by the user. Ignored in CGB mode, where the
     * cartridge programs its own colours. */
    u32 dmg_colors[4];

    bool frame_ready;
} gb_ppu_t;

struct gb;

void gb_ppu_init(gb_ppu_t *ppu);
void gb_ppu_step(gb_ppu_t *ppu, struct gb *gb, int cycles);

/* Installs the four DMG shades, lightest first. */
void gb_ppu_set_dmg_colors(gb_ppu_t *ppu, const u32 *colors);

/* CGB palette register access, used by the MMU for BCPS/BCPD and OCPS/OCPD. */
void gb_ppu_write_cgb_palette(gb_ppu_t *ppu, bool sprite, u8 value);
u8 gb_ppu_read_cgb_palette(const gb_ppu_t *ppu, bool sprite);

#endif
