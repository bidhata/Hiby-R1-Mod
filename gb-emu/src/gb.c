#include "gb.h"
#include "cpu.h"
#include "ppu.h"
#include "mmu.h"
#include "apu.h"

#include <string.h>

/* Register and I/O state left behind by the DMG boot ROM. */
static void gb_apply_post_boot_state(gb_t *gb) {
    gb->cpu.reg.af = 0x01B0;
    gb->cpu.reg.bc = 0x0013;
    gb->cpu.reg.de = 0x00D8;
    gb->cpu.reg.hl = 0x014D;
    gb->cpu.reg.sp = 0xFFFE;
    gb->cpu.reg.pc = 0x0100;

    memset(gb->io, 0, sizeof(gb->io));
    gb->io[0x00] = 0xCF;
    gb->io[0x05] = 0x00;
    gb->io[0x06] = 0x00;
    gb->io[0x07] = 0x00;
    gb->io[0x0F] = 0xE1;
    gb->io[0x10] = 0x80;
    gb->io[0x11] = 0xBF;
    gb->io[0x12] = 0xF3;
    gb->io[0x14] = 0xBF;
    gb->io[0x16] = 0x3F;
    gb->io[0x17] = 0x00;
    gb->io[0x19] = 0xBF;
    gb->io[0x1A] = 0x7F;
    gb->io[0x1B] = 0xFF;
    gb->io[0x1C] = 0x9F;
    gb->io[0x1E] = 0xBF;
    gb->io[0x20] = 0xFF;
    gb->io[0x21] = 0x00;
    gb->io[0x22] = 0x00;
    gb->io[0x23] = 0xBF;
    gb->io[0x24] = 0x77;
    gb->io[0x25] = 0xF3;
    gb->io[0x26] = 0xF1;
    gb->io[0x40] = 0x91;
    gb->io[0x41] = 0x85;
    gb->io[0x42] = 0x00;
    gb->io[0x43] = 0x00;
    gb->io[0x45] = 0x00;
    gb->io[0x47] = 0xFC;
    gb->io[0x48] = 0xFF;
    gb->io[0x49] = 0xFF;
    gb->io[0x4A] = 0x00;
    gb->io[0x4B] = 0x00;
    gb->io[0x50] = 0xFF;

    gb->ie = 0x00;
    gb->joypad_state = 0xFF;

    /* Push the LCD registers into the PPU so both start from the same state. */
    gb_mmu_sync_ppu(gb);
}

int gb_init(gb_t *gb) {
    memset(gb, 0, sizeof(gb_t));

    gb_cpu_init(&gb->cpu);
    gb_ppu_init(&gb->ppu);
    gb_apu_init(&gb->apu);

    gb_apply_post_boot_state(gb);

    gb->cart_loaded = false;
    gb->state = GB_STATE_STOPPED;
    gb->frame_count = 0;

    return 0;
}

void gb_destroy(gb_t *gb) {
    gb_mmu_unload(&gb->mmu);
    gb->cart_loaded = false;
}

int gb_load_rom(gb_t *gb, const char *path) {
    int result = gb_mmu_load_rom(&gb->mmu, path);
    if (result == 0) {
        gb->cart_loaded = true;
    }
    return result;
}

void gb_tick(gb_t *gb, int cycles) {
    if (cycles <= 0) return;

    gb_mmu_timer_step(gb, cycles);
    gb_ppu_step(&gb->ppu, gb, cycles);
    gb_apu_step(&gb->apu, gb, cycles);
}

void gb_run_frame(gb_t *gb) {
    gb->ppu.frame_ready = false;

    /* Upper bound on work per frame so a stuck ROM cannot hang the loop. */
    int budget = CYCLES_PER_FRAME * 4;

    /* gb_cpu_step ticks the hardware itself, one memory access at a time, so
     * there is nothing to advance out here. */
    while (!gb->ppu.frame_ready && budget > 0) {
        int cycles = gb_cpu_step(&gb->cpu, gb);
        if (cycles <= 0) cycles = 4;
        budget -= cycles;
    }

    gb->frame_count++;
}

void gb_reset(gb_t *gb) {
    gb_cpu_init(&gb->cpu);
    gb_ppu_init(&gb->ppu);
    gb_apu_init(&gb->apu);

    memset(gb->vram, 0, sizeof(gb->vram));
    memset(gb->wram, 0, sizeof(gb->wram));
    memset(gb->oam, 0, sizeof(gb->oam));
    memset(gb->hram, 0, sizeof(gb->hram));

    /* Cartridge mapper state returns to bank 1 with RAM disabled. */
    gb->mmu.rom_bank = 1;
    gb->mmu.ram_bank = 0;
    gb->mmu.ram_enabled = false;
    gb->mmu.rom_mode = 0;

    gb_apply_post_boot_state(gb);
    gb->frame_count = 0;
}
