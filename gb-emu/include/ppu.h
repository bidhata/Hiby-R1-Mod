#ifndef GB_PPU_H
#define GB_PPU_H

#include "types.h"

typedef struct {
    u32 framebuffer[GB_WIDTH * GB_HEIGHT];
    int mode;
    int mode_clock;
    int line;
    u8 bg_palette[4];
    u8 ob_palette[4][4];
    bool window_enabled;
    bool sprites_enabled;
    bool bg_enabled;
    u8 scx, scy;
    u8 wx, wy;
    u8 ly;
    u8 lyc;
    u8 stat;
    u8 lcdc;
    u8 oam[160];
    bool frame_ready;
} gb_ppu_t;

struct gb;

void gb_ppu_init(gb_ppu_t *ppu);
void gb_ppu_step(gb_ppu_t *ppu, struct gb *gb, int cycles);

#endif
