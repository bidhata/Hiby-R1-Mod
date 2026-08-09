#include "gb.h"
#include "mmu.h"
#include "cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* RAM size codes from cartridge header byte 0x149. */
static size_t ram_size_from_code(u8 code) {
    switch (code) {
        case 0x00: return 0;
        case 0x01: return 2 * 1024;
        case 0x02: return 8 * 1024;
        case 0x03: return 32 * 1024;
        case 0x04: return 128 * 1024;
        case 0x05: return 64 * 1024;
        default:   return 0;
    }
}

/* Builds "<rom path minus extension>.sav" in mmu->save_path. */
static void build_save_path(gb_mmu_t *mmu, const char *rom_path) {
    size_t len = strlen(rom_path);
    const char *dot = strrchr(rom_path, '.');
    const char *slash = strrchr(rom_path, '/');
    if (dot && (!slash || dot > slash)) {
        len = (size_t)(dot - rom_path);
    }
    if (len > sizeof(mmu->save_path) - 5) {
        len = sizeof(mmu->save_path) - 5;
    }
    memcpy(mmu->save_path, rom_path, len);
    memcpy(mmu->save_path + len, ".sav", 5);
}

static void load_save_file(gb_mmu_t *mmu) {
    if (!mmu->battery || !mmu->ram_data || mmu->save_path[0] == '\0') return;

    FILE *f = fopen(mmu->save_path, "rb");
    if (!f) return;

    size_t got = fread(mmu->ram_data, 1, mmu->ram_size, f);
    fclose(f);
    if (got > 0) {
        printf("Loaded save: %s (%zu bytes)\n", mmu->save_path, got);
    }
}

int gb_mmu_save_ram(gb_mmu_t *mmu) {
    if (!mmu->battery || !mmu->ram_data || mmu->ram_size == 0) return 0;
    if (mmu->save_path[0] == '\0') return 0;

    FILE *f = fopen(mmu->save_path, "wb");
    if (!f) return -1;

    size_t written = fwrite(mmu->ram_data, 1, mmu->ram_size, f);
    fclose(f);
    return (written == mmu->ram_size) ? 0 : -1;
}

void gb_mmu_unload(gb_mmu_t *mmu) {
    if (mmu->battery) {
        gb_mmu_save_ram(mmu);
    }
    free(mmu->rom_data);
    free(mmu->ram_data);
    mmu->rom_data = NULL;
    mmu->ram_data = NULL;
    mmu->rom_size = 0;
    mmu->ram_size = 0;
}

int gb_mmu_load_rom(gb_mmu_t *mmu, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    long size = ftell(f);
    if (size <= 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    /* A cart without a complete header cannot be mapped. */
    if (size < 0x150) {
        fprintf(stderr, "ROM too small (%ld bytes)\n", size);
        fclose(f);
        return -1;
    }

    memset(mmu, 0, sizeof(*mmu));
    mmu->rom_size = (size_t)size;
    mmu->rom_data = (u8 *)malloc(mmu->rom_size);
    if (!mmu->rom_data) {
        fclose(f);
        return -1;
    }

    size_t got = fread(mmu->rom_data, 1, mmu->rom_size, f);
    fclose(f);

    if (got != mmu->rom_size) {
        free(mmu->rom_data);
        mmu->rom_data = NULL;
        mmu->rom_size = 0;
        return -1;
    }

    /* Detect MBC type from header byte 0x147. */
    u8 cart_type = mmu->rom_data[0x147];
    switch (cart_type) {
        case 0x00: mmu->mbc = MBC_NONE; break;
        case 0x01: case 0x02: case 0x03:
            mmu->mbc = MBC1; break;
        case 0x05: case 0x06:
            mmu->mbc = MBC2; break;
        case 0x0F: case 0x10: case 0x11: case 0x12: case 0x13:
            mmu->mbc = MBC3; break;
        case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E:
            mmu->mbc = MBC5; break;
        case 0x08: case 0x09:
            mmu->mbc = MBC_NONE; break;
        default:
            fprintf(stderr, "Unknown cart type 0x%02X, assuming MBC1\n", cart_type);
            mmu->mbc = MBC1;
            break;
    }

    mmu->battery = (cart_type == 0x03 || cart_type == 0x06 || cart_type == 0x09 ||
                    cart_type == 0x0F || cart_type == 0x10 || cart_type == 0x13 ||
                    cart_type == 0x1B || cart_type == 0x1E);
    mmu->has_rtc = (cart_type == 0x0F || cart_type == 0x10);

    mmu->rom_banks = (int)(mmu->rom_size / ROM_BANK_SIZE);
    if (mmu->rom_banks < 2) mmu->rom_banks = 2;

    /* MBC2 has 512 half-bytes of built-in RAM and no header RAM size. */
    if (mmu->mbc == MBC2) {
        mmu->ram_size = 512;
    } else {
        mmu->ram_size = ram_size_from_code(mmu->rom_data[0x149]);
    }
    /* Some carts declare a battery but no RAM size; give them one bank. */
    if (mmu->ram_size == 0 && mmu->battery) {
        mmu->ram_size = RAM_BANK_SIZE;
    }

    if (mmu->ram_size > 0) {
        mmu->ram_data = (u8 *)calloc(1, mmu->ram_size);
        if (!mmu->ram_data) {
            free(mmu->rom_data);
            mmu->rom_data = NULL;
            mmu->rom_size = 0;
            return -1;
        }
        mmu->ram_banks = (int)(mmu->ram_size / RAM_BANK_SIZE);
        if (mmu->ram_banks < 1) mmu->ram_banks = 1;
    }

    mmu->rom_bank = 1;
    mmu->ram_bank = 0;
    mmu->ram_enabled = false;
    mmu->rom_mode = 0;
    mmu->rtc_latch = false;

    if (mmu->battery) {
        build_save_path(mmu, path);
        load_save_file(mmu);
    }

    return 0;
}

/* VRAM is two 8 KB banks on CGB hardware, selected by VBK. */
static inline size_t vram_offset(struct gb *gb, u16 addr) {
    return (size_t)(gb->vram_bank ? 0x2000 : 0) + (addr - 0x8000);
}

/* WRAM is eight 4 KB banks on CGB: C000-CFFF is always bank 0, and D000-DFFF
 * is whichever bank SVBK selects. Bank 0 there is treated as bank 1. */
static inline size_t wram_offset(struct gb *gb, u16 addr) {
    if (addr < 0xD000) return (size_t)(addr - 0xC000);

    int bank = gb->cgb_mode ? (gb->wram_bank & 0x07) : 1;
    if (bank == 0) bank = 1;
    return (size_t)bank * 0x1000 + (addr - 0xD000);
}

/* Copies one 16-byte block from the HDMA source into VRAM and steps both
 * pointers on. */
static void hdma_copy_block(struct gb *gb) {
    for (int i = 0; i < 16; i++) {
        u16 src = (u16)(gb->hdma_src + i);
        u16 dst = (u16)(0x8000 + ((gb->hdma_dst + i) & 0x1FFF));
        /* Reading through the MMU keeps cartridge banking in play; writing
         * direct avoids re-entering the write path for something that can only
         * ever land in VRAM. */
        gb->vram[vram_offset(gb, dst)] = gb_mmu_read(&gb->mmu, gb, src);
    }
    gb->hdma_src = (u16)(gb->hdma_src + 16);
    gb->hdma_dst = (u16)(gb->hdma_dst + 16);
}

/* HDMA5: bit 7 picks the mode, the low bits give the length in 16-byte blocks
 * minus one. */
static void hdma_write_control(struct gb *gb, u8 val) {
    if (gb->hdma_active && !(val & 0x80)) {
        /* Writing with bit 7 clear during an HBlank transfer cancels it. */
        gb->hdma_active = false;
        return;
    }

    gb->hdma_length = val & 0x7F;

    if (val & 0x80) {
        /* HBlank mode: one block per HBlank, handled by gb_mmu_hdma_hblank. */
        gb->hdma_active = true;
        return;
    }

    /* General purpose mode: the whole block moves at once, with the CPU
     * stopped. The cycle cost is charged by the caller of gb_tick. */
    int blocks = gb->hdma_length + 1;
    for (int i = 0; i < blocks; i++) hdma_copy_block(gb);
    gb->hdma_active = false;
    gb->hdma_length = 0xFF;
}

void gb_mmu_hdma_hblank(struct gb *gb) {
    if (!gb->cgb_mode || !gb->hdma_active) return;

    hdma_copy_block(gb);

    if (gb->hdma_length == 0) {
        gb->hdma_active = false;
        gb->hdma_length = 0xFF;
    } else {
        gb->hdma_length--;
    }
}

/* Joypad handling: bits 4 and 5 select a row, bits 0-3 read it, all active low. */
static u8 joypad_read(struct gb *gb) {
    u8 row = gb->io[0x00];
    u8 result = 0x0F;

    /* No platform means no buttons: the headless runner reports everything
     * released rather than reaching through a null pointer. */
    const gb_platform_t *p = gb->platform;
    if (!p) return 0xC0 | (row & 0x30) | result;

    if (!(row & 0x10)) {
        if (p->button_right) result &= ~0x01;
        if (p->button_left)  result &= ~0x02;
        if (p->button_up)    result &= ~0x04;
        if (p->button_down)  result &= ~0x08;
    }
    if (!(row & 0x20)) {
        if (p->button_a)      result &= ~0x01;
        if (p->button_b)      result &= ~0x02;
        if (p->button_select) result &= ~0x04;
        if (p->button_start)  result &= ~0x08;
    }

    /* Bits 6 and 7 are unused and always read as 1. */
    return 0xC0 | (row & 0x30) | result;
}

static void joypad_write(struct gb *gb, u8 val) {
    /* Only the two select bits are writable. */
    gb->io[0x00] = (gb->io[0x00] & 0xCF) | (val & 0x30);
}

/* Timer: DIV ticks every 256 T-cycles, TIMA at the rate selected by TAC. */
static const int tima_freq_table[4] = {1024, 16, 64, 256};

void gb_mmu_timer_step(struct gb *gb, int cycles) {
    gb->cpu.div_counter += cycles;
    while (gb->cpu.div_counter >= 256) {
        gb->cpu.div_counter -= 256;
        gb->io[0x04]++;
    }

    u8 tac = gb->io[0x07];
    if (!(tac & 0x04)) return;

    int freq = tima_freq_table[tac & 0x03];
    gb->cpu.tima_counter += cycles;
    while (gb->cpu.tima_counter >= freq) {
        gb->cpu.tima_counter -= freq;
        gb->io[0x05]++;
        if (gb->io[0x05] == 0) {
            gb->io[0x05] = gb->io[0x06]; /* Reload from TMA */
            gb->io[0x0F] |= 0x04;        /* Request timer interrupt */
            if (gb->cpu.halted) gb->cpu.halted = false;
        }
    }
}

static void unpack_palette(u8 value, u8 *out) {
    out[0] = value & 0x03;
    out[1] = (value >> 2) & 0x03;
    out[2] = (value >> 4) & 0x03;
    out[3] = (value >> 6) & 0x03;
}

void gb_mmu_sync_ppu(struct gb *gb) {
    gb->ppu.lcdc = gb->io[0x40];
    gb->ppu.stat = (gb->io[0x41] & 0x78) | (gb->ppu.stat & 0x07);
    gb->ppu.scy = gb->io[0x42];
    gb->ppu.scx = gb->io[0x43];
    gb->ppu.ly = gb->io[0x44];
    gb->ppu.lyc = gb->io[0x45];
    gb->ppu.wy = gb->io[0x4A];
    gb->ppu.wx = gb->io[0x4B];

    unpack_palette(gb->io[0x47], gb->ppu.bg_palette);
    unpack_palette(gb->io[0x48], gb->ppu.ob_palette[0]);
    unpack_palette(gb->io[0x49], gb->ppu.ob_palette[1]);
}

/* Latches the host clock into the RTC registers (MBC3 only). */
static void rtc_latch_now(gb_mmu_t *mmu) {
    time_t now = time(NULL);
    struct tm tm_now;
#ifdef _WIN32
    tm_now = *localtime(&now);
#else
    localtime_r(&now, &tm_now);
#endif
    mmu->rtc_registers[0] = (u8)tm_now.tm_sec;
    mmu->rtc_registers[1] = (u8)tm_now.tm_min;
    mmu->rtc_registers[2] = (u8)tm_now.tm_hour;
    mmu->rtc_registers[3] = (u8)(tm_now.tm_yday & 0xFF);
    mmu->rtc_registers[4] = (u8)((tm_now.tm_yday >> 8) & 0x01);
    memcpy(mmu->rtc_latched, mmu->rtc_registers, sizeof(mmu->rtc_latched));
}

/* Maps an address in A000-BFFF to an offset in the cart RAM allocation.
 * Returns -1 when the access falls outside the cart's RAM. */
static long ext_ram_offset(gb_mmu_t *mmu, u16 addr) {
    if (!mmu->ram_data || mmu->ram_size == 0) return -1;

    long offset;
    if (mmu->mbc == MBC2) {
        /* MBC2 RAM is 512 half-bytes, mirrored through the whole window. */
        offset = (addr - 0xA000) & 0x01FF;
    } else if (mmu->mbc == MBC1 && mmu->rom_mode == 0) {
        /* In ROM banking mode only RAM bank 0 is reachable. */
        offset = addr - 0xA000;
    } else {
        int bank = mmu->ram_bank;
        if (mmu->ram_banks > 0) bank %= mmu->ram_banks;
        offset = (long)bank * RAM_BANK_SIZE + (addr - 0xA000);
    }

    if (offset < 0 || (size_t)offset >= mmu->ram_size) return -1;
    return offset;
}

u8 gb_mmu_read(gb_mmu_t *mmu, struct gb *gb, u16 addr) {
    /* ROM bank 0: 0000-3FFF */
    if (addr < 0x4000) {
        if (mmu->rom_data && addr < mmu->rom_size)
            return mmu->rom_data[addr];
        return 0xFF;
    }

    /* ROM bank N: 4000-7FFF */
    if (addr < 0x8000) {
        if (!mmu->rom_data) return 0xFF;

        int bank = mmu->rom_bank;
        switch (mmu->mbc) {
        case MBC5:
            bank &= 0x1FF;
            break;
        case MBC1:
            /* In RAM banking mode the upper two bits address RAM, not ROM. */
            if (mmu->rom_mode == 1) bank &= 0x1F;
            else bank &= 0x7F;
            if ((bank & 0x1F) == 0) bank++;
            break;
        case MBC2:
            bank &= 0x0F;
            if (bank == 0) bank = 1;
            break;
        case MBC3:
            bank &= 0x7F;
            if (bank == 0) bank = 1;
            break;
        case MBC_NONE:
            bank = 1;
            break;
        }
        if (mmu->rom_banks > 0) bank %= mmu->rom_banks;

        u32 bank_addr = (u32)bank * ROM_BANK_SIZE + (addr - 0x4000);
        if (bank_addr < mmu->rom_size)
            return mmu->rom_data[bank_addr];
        return 0xFF;
    }

    /* VRAM: 8000-9FFF */
    if (addr < 0xA000) {
        return gb->vram[vram_offset(gb, addr)];
    }

    /* External RAM / RTC: A000-BFFF */
    if (addr < 0xC000) {
        if (!mmu->ram_enabled) return 0xFF;

        if (mmu->mbc == MBC3 && mmu->ram_bank >= 0x08 && mmu->ram_bank <= 0x0C) {
            return mmu->rtc_latched[mmu->ram_bank - 0x08];
        }

        long offset = ext_ram_offset(mmu, addr);
        if (offset < 0) return 0xFF;
        /* MBC2 stores 4-bit values; the upper nibble reads back as 1. */
        if (mmu->mbc == MBC2) return mmu->ram_data[offset] | 0xF0;
        return mmu->ram_data[offset];
    }

    /* Work RAM: C000-DFFF */
    if (addr < 0xE000) {
        return gb->wram[wram_offset(gb, addr)];
    }

    /* Echo RAM: E000-FDFF mirrors C000-DDFF */
    if (addr < 0xFE00) {
        /* Echo RAM mirrors C000-DDFF, banking included. */
        return gb->wram[wram_offset(gb, (u16)(addr - 0x2000))];
    }

    /* OAM: FE00-FE9F */
    if (addr < 0xFEA0) {
        return gb->oam[addr - 0xFE00];
    }

    /* Unusable: FEA0-FEFF */
    if (addr < 0xFF00) {
        return 0xFF;
    }

    /* I/O Registers: FF00-FF7F */
    if (addr < 0xFF80) {
        u8 reg = addr & 0x7F;

        if (reg == 0x00) return joypad_read(gb);

        /* Serial: no link cable, so transfers never complete. */
        if (reg == 0x01) return gb->io[0x01];
        if (reg == 0x02) return gb->io[0x02] | 0x7E;

        if (reg == 0x0F) return gb->io[0x0F] | 0xE0;

        if (reg >= 0x10 && reg <= 0x3F) {
            return gb_apu_read_reg(&gb->apu, reg, gb->io[reg]);
        }

        if (reg == 0x40) return gb->ppu.lcdc;
        if (reg == 0x41) return (gb->ppu.stat & 0x7F) | 0x80;
        if (reg == 0x42) return gb->ppu.scy;
        if (reg == 0x43) return gb->ppu.scx;
        if (reg == 0x44) return gb->ppu.ly;
        if (reg == 0x45) return gb->ppu.lyc;
        if (reg == 0x4A) return gb->ppu.wy;
        if (reg == 0x4B) return gb->ppu.wx;

        /* Game Boy Color registers. On a DMG cart these are absent and read
         * back as 0xFF, which is what software uses to tell the two apart. */
        if (reg >= 0x4D && reg <= 0x70) {
            if (!gb->cgb_mode) return 0xFF;

            switch (reg) {
                case 0x4D: /* KEY1: current speed and the armed switch */
                    return (u8)((gb->double_speed ? 0x80 : 0x00) |
                                (gb->speed_switch_armed ? 0x01 : 0x00) | 0x7E);
                case 0x4F: return (u8)(gb->vram_bank | 0xFE);
                case 0x51: return (u8)(gb->hdma_src >> 8);
                case 0x52: return (u8)(gb->hdma_src & 0xFF);
                case 0x53: return (u8)(gb->hdma_dst >> 8);
                case 0x54: return (u8)(gb->hdma_dst & 0xFF);
                case 0x55:
                    /* Bit 7 clear means a transfer is still running. */
                    return (u8)(gb->hdma_active ? (gb->hdma_length & 0x7F)
                                                : 0xFF);
                case 0x68: return gb->ppu.bcps;
                case 0x69: return gb_ppu_read_cgb_palette(&gb->ppu, false);
                case 0x6A: return gb->ppu.ocps;
                case 0x6B: return gb_ppu_read_cgb_palette(&gb->ppu, true);
                case 0x70: return (u8)(gb->wram_bank | 0xF8);
                default:   return 0xFF;
            }
        }

        return gb->io[reg];
    }

    /* High RAM: FF80-FFFE */
    if (addr < 0xFFFF) {
        return gb->hram[addr - 0xFF80];
    }

    /* Interrupt Enable: FFFF */
    return gb->ie;
}

void gb_mmu_write(gb_mmu_t *mmu, struct gb *gb, u16 addr, u8 val) {
    /* ROM area: MBC control registers */
    if (addr < 0x8000) {
        if (!mmu->rom_data) return;

        switch (mmu->mbc) {
        case MBC_NONE:
            break;

        case MBC1:
            if (addr < 0x2000) {
                mmu->ram_enabled = ((val & 0x0F) == 0x0A);
            } else if (addr < 0x4000) {
                /* Low 5 bits of the ROM bank; bank 0 is remapped to 1. */
                int bank = val & 0x1F;
                if (bank == 0) bank = 1;
                mmu->rom_bank = (mmu->rom_bank & 0x60) | bank;
            } else if (addr < 0x6000) {
                /* Two extra bits: ROM bank high bits, or the RAM bank. */
                if (mmu->rom_mode == 1) {
                    mmu->ram_bank = val & 0x03;
                } else {
                    mmu->rom_bank = (mmu->rom_bank & 0x1F) | ((val & 0x03) << 5);
                }
            } else {
                mmu->rom_mode = val & 1;
                if (mmu->rom_mode == 0) mmu->ram_bank = 0;
            }
            break;

        case MBC2:
            /* Bit 8 of the address picks between RAM enable and bank select. */
            if (addr < 0x4000) {
                if (addr & 0x0100) {
                    mmu->rom_bank = val & 0x0F;
                    if (mmu->rom_bank == 0) mmu->rom_bank = 1;
                } else {
                    mmu->ram_enabled = ((val & 0x0F) == 0x0A);
                }
            }
            break;

        case MBC3:
            if (addr < 0x2000) {
                mmu->ram_enabled = ((val & 0x0F) == 0x0A);
            } else if (addr < 0x4000) {
                int bank = val & 0x7F;
                if (bank == 0) bank = 1;
                mmu->rom_bank = bank;
            } else if (addr < 0x6000) {
                /* 0x00-0x03 select a RAM bank, 0x08-0x0C an RTC register. */
                if (val <= 0x03 || (val >= 0x08 && val <= 0x0C)) {
                    mmu->ram_bank = val;
                }
            } else {
                /* Writing 0 then 1 latches the current time. */
                if (val == 0x00) {
                    mmu->rtc_latch = true;
                } else if (val == 0x01 && mmu->rtc_latch) {
                    mmu->rtc_latch = false;
                    if (mmu->has_rtc) rtc_latch_now(mmu);
                }
            }
            break;

        case MBC5:
            if (addr < 0x2000) {
                mmu->ram_enabled = ((val & 0x0F) == 0x0A);
            } else if (addr < 0x3000) {
                mmu->rom_bank = (mmu->rom_bank & 0x100) | val;
            } else if (addr < 0x4000) {
                mmu->rom_bank = (mmu->rom_bank & 0xFF) | ((val & 1) << 8);
            } else if (addr < 0x6000) {
                mmu->ram_bank = val & 0x0F;
            }
            break;
        }
        return;
    }

    /* VRAM: 8000-9FFF */
    if (addr < 0xA000) {
        gb->vram[vram_offset(gb, addr)] = val;
        return;
    }

    /* External RAM / RTC: A000-BFFF */
    if (addr < 0xC000) {
        if (!mmu->ram_enabled) return;

        if (mmu->mbc == MBC3 && mmu->ram_bank >= 0x08 && mmu->ram_bank <= 0x0C) {
            mmu->rtc_registers[mmu->ram_bank - 0x08] = val;
            return;
        }

        long offset = ext_ram_offset(mmu, addr);
        if (offset < 0) return;
        mmu->ram_data[offset] = (mmu->mbc == MBC2) ? (val & 0x0F) : val;
        return;
    }

    /* Work RAM: C000-DFFF */
    if (addr < 0xE000) {
        gb->wram[wram_offset(gb, addr)] = val;
        return;
    }

    /* Echo RAM: E000-FDFF */
    if (addr < 0xFE00) {
        gb->wram[wram_offset(gb, (u16)(addr - 0x2000))] = val;
        return;
    }

    /* OAM: FE00-FE9F */
    if (addr < 0xFEA0) {
        gb->oam[addr - 0xFE00] = val;
        return;
    }

    /* Unusable: FEA0-FEFF */
    if (addr < 0xFF00) {
        return;
    }

    /* I/O Registers: FF00-FF7F */
    if (addr < 0xFF80) {
        u8 reg = addr & 0x7F;

        switch (reg) {
        case 0x00:
            joypad_write(gb, val);
            return;

        case 0x04:
            /* Any write to DIV clears it and the internal divider. */
            gb->io[0x04] = 0;
            gb->cpu.div_counter = 0;
            gb->cpu.tima_counter = 0;
            return;

        case 0x02:
            /* Bit 0 selects the internal clock. Driving the transfer itself,
             * the console completes it even with nothing on the other end and
             * shifts in 0xFF; on the external clock no pulses ever arrive, so
             * the transfer stays pending. Test ROMs report their results this
             * way, so echo the byte when logging is on. */
            gb->io[0x02] = val;
            if ((val & 0x81) == 0x81) {
                if (gb->serial_log) {
                    fputc(gb->io[0x01], stdout);
                    fflush(stdout);
                }
                gb->io[0x01] = 0xFF;
                gb->io[0x02] = val & 0x7F;
                gb->io[0x0F] |= 0x08; /* Serial transfer complete */
            }
            return;

        case 0x0F:
            gb->io[0x0F] = val & 0x1F;
            return;

        case 0x40: {
            u8 old = gb->ppu.lcdc;
            gb->ppu.lcdc = val;
            gb->io[0x40] = val;
            /* Turning the LCD off resets the scanline counter and mode. */
            if ((old & 0x80) && !(val & 0x80)) {
                gb->ppu.ly = 0;
                gb->io[0x44] = 0;
                gb->ppu.mode_clock = 0;
                gb->ppu.window_line = 0;
                gb->ppu.mode = 0;
                gb->ppu.stat &= ~0x03;
            } else if (!(old & 0x80) && (val & 0x80)) {
                gb->ppu.mode_clock = 0;
                gb->ppu.mode = 2;
                gb->ppu.stat = (gb->ppu.stat & ~0x03) | 2;
            }
            return;
        }

        case 0x41:
            /* Bits 0-2 are read-only status bits. */
            gb->ppu.stat = (val & 0x78) | (gb->ppu.stat & 0x07);
            gb->io[0x41] = gb->ppu.stat;
            return;

        case 0x42: gb->ppu.scy = val; gb->io[0x42] = val; return;
        case 0x43: gb->ppu.scx = val; gb->io[0x43] = val; return;
        case 0x44: /* LY is read-only */ return;
        case 0x45: gb->ppu.lyc = val; gb->io[0x45] = val; return;

        case 0x46: {
            /* OAM DMA: copy 160 bytes from val*0x100 into OAM. */
            u16 src = (u16)val << 8;
            for (int i = 0; i < 0xA0; i++) {
                gb->oam[i] = gb_mmu_read(mmu, gb, (u16)(src + i));
            }
            gb->io[0x46] = val;
            return;
        }

        case 0x47:
            gb->io[0x47] = val;
            unpack_palette(val, gb->ppu.bg_palette);
            return;
        case 0x48:
            gb->io[0x48] = val;
            unpack_palette(val, gb->ppu.ob_palette[0]);
            return;
        case 0x49:
            gb->io[0x49] = val;
            unpack_palette(val, gb->ppu.ob_palette[1]);
            return;

        case 0x4A: gb->ppu.wy = val; gb->io[0x4A] = val; return;
        case 0x4B: gb->ppu.wx = val; gb->io[0x4B] = val; return;

        /* Game Boy Color registers; inert on a DMG cartridge. */
        case 0x4D:
            if (gb->cgb_mode) gb->speed_switch_armed = (val & 0x01) != 0;
            return;

        case 0x4F:
            if (gb->cgb_mode) gb->vram_bank = val & 0x01;
            return;

        case 0x51: if (gb->cgb_mode) gb->hdma_src = (u16)((gb->hdma_src & 0x00FF) | (val << 8)); return;
        case 0x52: if (gb->cgb_mode) gb->hdma_src = (u16)((gb->hdma_src & 0xFF00) | (val & 0xF0)); return;
        case 0x53: if (gb->cgb_mode) gb->hdma_dst = (u16)((gb->hdma_dst & 0x00FF) | ((val & 0x1F) << 8)); return;
        case 0x54: if (gb->cgb_mode) gb->hdma_dst = (u16)((gb->hdma_dst & 0xFF00) | (val & 0xF0)); return;

        case 0x55:
            if (gb->cgb_mode) hdma_write_control(gb, val);
            return;

        case 0x68:
            if (gb->cgb_mode) gb->ppu.bcps = val;
            return;
        case 0x69:
            if (gb->cgb_mode) gb_ppu_write_cgb_palette(&gb->ppu, false, val);
            return;
        case 0x6A:
            if (gb->cgb_mode) gb->ppu.ocps = val;
            return;
        case 0x6B:
            if (gb->cgb_mode) gb_ppu_write_cgb_palette(&gb->ppu, true, val);
            return;

        case 0x70:
            if (gb->cgb_mode) {
                gb->wram_bank = val & 0x07;
                if (gb->wram_bank == 0) gb->wram_bank = 1;
            }
            return;

        default:
            break;
        }

        if (reg >= 0x10 && reg <= 0x3F) {
            gb_apu_write_reg(&gb->apu, reg, val);
            gb->io[reg] = val;
            return;
        }

        gb->io[reg] = val;
        return;
    }

    /* High RAM: FF80-FFFE */
    if (addr < 0xFFFF) {
        gb->hram[addr - 0xFF80] = val;
        return;
    }

    /* Interrupt Enable: FFFF */
    gb->ie = val;
}
