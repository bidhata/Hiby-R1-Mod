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

    bool frame_ready;
} gb_ppu_t;

struct gb;

void gb_ppu_init(gb_ppu_t *ppu);
void gb_ppu_step(gb_ppu_t *ppu, struct gb *gb, int cycles);

#endif
