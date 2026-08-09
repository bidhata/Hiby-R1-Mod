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
    /* Points at the platform owned by main(), not a copy: the joypad is read
     * straight out of it, so it has to be the same struct the input polling
     * writes to. NULL in the headless runner, which has no input at all. */
    gb_platform_t *platform;

    /* Two VRAM banks and eight WRAM banks on CGB hardware; a DMG uses only the
     * first of each, so the extra space simply goes unused. */
    u8 vram[0x4000];
    u8 wram[0x8000];
    u8 oam[0xA0];
    u8 hram[0x7F];
    u8 io[0x80];
    u8 ie;

    /* Game Boy Color state. cgb_mode is set when the cartridge asks for CGB
     * features; everything below is meaningless without it. */
    bool cgb_mode;
    u8 vram_bank;       /* VBK, 0 or 1 */
    u8 wram_bank;       /* SVBK, 1-7 (0 reads as 1) */

    /* CPU double-speed mode: KEY1 bit 7 is the current speed, bit 0 an armed
     * switch that STOP carries out. */
    bool double_speed;
    bool speed_switch_armed;
    /* Odd CPU cycle held back when halving the clock for the PPU and APU. */
    int tick_carry;

    /* HDMA/GDMA: a block copy from ROM or RAM into VRAM, either immediate or
     * one 16-byte block per HBlank. */
    u16 hdma_src;
    u16 hdma_dst;
    u8 hdma_length;     /* blocks remaining, minus one */
    bool hdma_active;   /* an HBlank transfer is in progress */

    u8 joypad_state;
    bool cart_loaded;

    /* Echo bytes written to the serial port to stdout. Test ROMs report their
     * results over the link cable, so this makes them readable as text. */
    bool serial_log;

    int turbo_mode;
    int frame_count;
} gb_t;

int gb_init(gb_t *gb);
void gb_destroy(gb_t *gb);
int gb_load_rom(gb_t *gb, const char *path);
void gb_run_frame(gb_t *gb);
void gb_reset(gb_t *gb);

/* Advances the timer, PPU and APU by the given number of T-cycles. The CPU
 * calls this as each memory access happens, so hardware state stays in step
 * with the instruction rather than jumping forward once it has finished. */
void gb_tick(gb_t *gb, int cycles);

#endif
