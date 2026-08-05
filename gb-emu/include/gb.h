#ifndef GB_H
#define GB_H

#include "types.h"
#include "cpu.h"
#include "ppu.h"
#include "mmu.h"
#include "apu.h"
#include "platform.h"

#define GB_IO_IF  0xFF0F
#define GB_IO_IE  0xFFFF
#define GB_IO_LCDC 0xFF40
#define GB_IO_STAT 0xFF41
#define GB_IO_SCY  0xFF42
#define GB_IO_SCX  0xFF43
#define GB_IO_LY   0xFF44
#define GB_IO_LYC  0xFF45
#define GB_IO_BGP  0xFF47
#define GB_IO_OBP0 0xFF48
#define GB_IO_OBP1 0xFF49
#define GB_IO_WY   0xFF4A
#define GB_IO_WX   0xFF4B
#define GB_IO_DIV  0xFF04
#define GB_IO_TIMA 0xFF05
#define GB_IO_TMA  0xFF06
#define GB_IO_TAC  0xFF07
#define GB_IO_JOYP 0xFF00

typedef enum {
    GB_STATE_STOPPED,
    GB_STATE_RUNNING,
    GB_STATE_PAUSED
} gb_state_t;

typedef struct gb {
    gb_state_t state;
    gb_cpu_t cpu;
    gb_ppu_t ppu;
    gb_mmu_t mmu;
    gb_apu_t apu;
    gb_platform_t platform;

    u8 vram[0x8000];
    u8 wram[0x2000];
    u8 oam[0xA0];
    u8 hram[0x7F];
    u8 io[0x80];
    u8 ie;

    u8 joypad_state;
    bool cart_loaded;

    int turbo_mode;
    int frame_count;
} gb_t;

int gb_init(gb_t *gb);
void gb_destroy(gb_t *gb);
int gb_load_rom(gb_t *gb, const char *path);
void gb_run_frame(gb_t *gb);
void gb_reset(gb_t *gb);

#endif
