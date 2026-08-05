#include "gb.h"
#include "mmu.h"
#include "cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int gb_mmu_load_rom(gb_mmu_t *mmu, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    mmu->rom_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    mmu->rom_data = (u8 *)malloc(mmu->rom_size);
    if (!mmu->rom_data) {
        fclose(f);
        return -1;
    }

    size_t read = fread(mmu->rom_data, 1, mmu->rom_size, f);
    fclose(f);

    if (read != mmu->rom_size) {
        free(mmu->rom_data);
        mmu->rom_data = NULL;
        return -1;
    }

    /* Detect MBC type from header byte 0x147 */
    mmu->mbc = MBC_NONE;
    if (mmu->rom_size > 0x147) {
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
            default: mmu->mbc = MBC_NONE; break;
        }

        /* Check for battery */
        mmu->battery = (cart_type == 0x03 || cart_type == 0x06 ||
                        cart_type == 0x10 || cart_type == 0x13 ||
                        cart_type == 0x1B || cart_type == 0x1E);
    }

    mmu->rom_bank = 1;
    mmu->ram_bank = 0;
    mmu->ram_enabled = false;
    mmu->rom_mode = 0;
    memset(mmu->rtcRegisters, 0, sizeof(mmu->rtcRegisters));
    memset(mmu->rtc_latched, 0, sizeof(mmu->rtc_latched));
    mmu->rtc_latch = false;

    return 0;
}

/* Joypad handling */
static u8 joypad_read(struct gb *gb) {
    u8 row = gb->io[0x00]; /* 0xFF00 - 0xFF00 offset */
    u8 result = 0x3F; /* bits 5,6 unused = 1; bits 0-3 = 1 */

    if (!(row & 0x10)) {
        /* Direction keys selected */
        if (gb->platform.button_up)    result &= ~0x01;
        if (gb->platform.button_down)  result &= ~0x02;
        if (gb->platform.button_left)  result &= ~0x04;
        if (gb->platform.button_right) result &= ~0x08;
    }
    if (!(row & 0x20)) {
        /* Button keys selected */
        if (gb->platform.button_a)      result &= ~0x01;
        if (gb->platform.button_b)      result &= ~0x02;
        if (gb->platform.button_select) result &= ~0x04;
        if (gb->platform.button_start)  result &= ~0x08;
    }

    return (row & 0x30) | result;
}

static void joypad_write(struct gb *gb, u8 val) {
    gb->io[0x00] = val;
}

/* Timer handling */
static void timer_div_update(struct gb *gb) {
    gb->cpu.div_counter++;
    if (gb->cpu.div_counter >= 256) {
        gb->cpu.div_counter = 0;
        gb->io[0x04]++; /* DIV */
    }
}

static void timer_tima_update(struct gb *gb) {
    u8 tac = gb->io[0x07]; /* TAC */
    if (!(tac & 0x04)) return; /* Timer not enabled */

    int freq_table[4] = {1024, 16, 64, 256};
    int freq = freq_table[tac & 0x03];

    gb->cpu.tima_counter++;
    if (gb->cpu.tima_counter >= freq) {
        gb->cpu.tima_counter = 0;
        gb->io[0x05]++; /* TIMA */
        if (gb->io[0x05] == 0) {
            /* TIMA overflow, reload from TMA */
            gb->io[0x05] = gb->io[0x06]; /* TMA */
            gb->io[0x0F] |= 0x04; /* Timer interrupt flag */
        }
    }
}

/* Sync PPU state from I/O registers */
static void ppu_sync_from_io(struct gb *gb) {
    gb->ppu.lcdc = gb->io[0x40];
    gb->ppu.stat = (gb->ppu.stat & 0x07) | (gb->io[0x41] & 0xF8);
    gb->ppu.scy = gb->io[0x42];
    gb->ppu.scx = gb->io[0x43];
    gb->ppu.ly = gb->io[0x44];
    gb->ppu.lyc = gb->io[0x45];
    gb->ppu.wy = gb->io[0x4A];
    gb->ppu.wx = gb->io[0x4B];

    /* Palette */
    u8 bgp = gb->io[0x47];
    gb->ppu.bg_palette[0] = bgp & 0x03;
    gb->ppu.bg_palette[1] = (bgp >> 2) & 0x03;
    gb->ppu.bg_palette[2] = (bgp >> 4) & 0x03;
    gb->ppu.bg_palette[3] = (bgp >> 6) & 0x03;

    u8 obp0 = gb->io[0x48];
    gb->ppu.ob_palette[0][0] = obp0 & 0x03;
    gb->ppu.ob_palette[0][1] = (obp0 >> 2) & 0x03;
    gb->ppu.ob_palette[0][2] = (obp0 >> 4) & 0x03;
    gb->ppu.ob_palette[0][3] = (obp0 >> 6) & 0x03;

    u8 obp1 = gb->io[0x49];
    gb->ppu.ob_palette[1][0] = obp1 & 0x03;
    gb->ppu.ob_palette[1][1] = (obp1 >> 2) & 0x03;
    gb->ppu.ob_palette[1][2] = (obp1 >> 4) & 0x03;
    gb->ppu.ob_palette[1][3] = (obp1 >> 6) & 0x03;
}

/* Sync I/O registers from PPU state */
static void ppu_sync_to_io(struct gb *gb) {
    gb->io[0x44] = gb->ppu.ly;

    u8 stat = gb->io[0x41];
    stat = (stat & 0xF8) | (gb->ppu.stat & 0x07);
    gb->io[0x41] = stat;
}

u8 gb_mmu_read(gb_mmu_t *mmu, struct gb *gb, u16 addr) {
    /* Boot ROM area (first 256 bytes, before boot ROM disable) */
    if (addr < 0x100 && !(gb->io[0x50] & 1)) {
        return 0xFF; /* Boot ROM disabled or not loaded */
    }

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
        u32 bank_addr = bank * ROM_BANK_SIZE + (addr - 0x4000);

        if (mmu->mbc == MBC5) {
            bank = (mmu->rom_bank & 0x1FF);
            bank_addr = bank * ROM_BANK_SIZE + (addr - 0x4000);
        } else {
            /* MBC1: mask to lower 5 bits if in ROM mode */
            if (mmu->mbc == MBC1 && mmu->rom_mode == 0) {
                bank = bank & 0x1F;
            }
            if (bank == 0) bank = 1;
            bank_addr = bank * ROM_BANK_SIZE + (addr - 0x4000);
        }

        if (bank_addr < mmu->rom_size)
            return mmu->rom_data[bank_addr];
        return 0xFF;
    }

    /* VRAM: 8000-9FFF */
    if (addr < 0xA000) {
        return gb->vram[addr - 0x8000];
    }

    /* External RAM: A000-BFFF */
    if (addr < 0xC000) {
        if (!mmu->ram_enabled) return 0xFF;
        /* TODO: implement external RAM read for MBC */
        return 0xFF;
    }

    /* Work RAM: C000-DFFF */
    if (addr < 0xE000) {
        return gb->wram[addr - 0xC000];
    }

    /* Echo RAM: E000-FDFF */
    if (addr < 0xFE00) {
        return gb->wram[addr - 0xE000];
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
        u8 reg = addr & 0xFF;

        /* Joypad */
        if (reg == 0x00) return joypad_read(gb);

        /* Timer */
        if (reg == 0x04) return gb->io[0x04]; /* DIV */
        if (reg == 0x05) return gb->io[0x05]; /* TIMA */
        if (reg == 0x06) return gb->io[0x06]; /* TMA */
        if (reg == 0x07) return gb->io[0x07]; /* TAC */

        /* Interrupt Flag */
        if (reg == 0x0F) return gb->io[0x0F]; /* IF */

        /* Audio registers 0x10-0x3F */
        if (reg >= 0x10 && reg <= 0x3F) {
            return gb->io[reg];
        }

        /* LCD registers */
        if (reg == 0x40) return gb->ppu.lcdc;
        if (reg == 0x41) return gb->io[0x41];
        if (reg == 0x42) return gb->ppu.scy;
        if (reg == 0x43) return gb->ppu.scx;
        if (reg == 0x44) {
            ppu_sync_to_io(gb);
            return gb->ppu.ly;
        }
        if (reg == 0x45) return gb->ppu.lyc;
        if (reg == 0x47) return gb->io[0x47]; /* BGP */
        if (reg == 0x48) return gb->io[0x48]; /* OBP0 */
        if (reg == 0x49) return gb->io[0x49]; /* OBP1 */
        if (reg == 0x4A) return gb->ppu.wy;
        if (reg == 0x4B) return gb->ppu.wx;

        /* Boot ROM disable */
        if (reg == 0x50) return gb->io[0x50];

        /* All other I/O */
        return gb->io[reg];
    }

    /* High RAM: FF80-FFFE */
    if (addr < 0xFFFF) {
        return gb->hram[addr - 0xFF80];
    }

    /* Interrupt Enable: FFFF */
    if (addr == 0xFFFF) {
        return gb->ie;
    }

    return 0xFF;
}

void gb_mmu_write(gb_mmu_t *mmu, struct gb *gb, u16 addr, u8 val) {
    /* ROM area: write protection / MBC control */
    if (addr < 0x8000) {
        if (!mmu->rom_data) return;

        switch (mmu->mbc) {
        case MBC_NONE:
            /* No MBC, write ignored */
            break;

        case MBC1:
            if (addr < 0x2000) {
                /* RAM enable: write 0x0A to enable */
                mmu->ram_enabled = ((val & 0x0F) == 0x0A);
            } else if (addr < 0x4000) {
                /* ROM bank select: lower 5 bits */
                int bank = val & 0x1F;
                if (bank == 0) bank = 1;
                mmu->rom_bank = (mmu->rom_bank & 0x60) | bank;
            } else if (addr < 0x6000) {
                /* Upper 2 bits of bank number (or RAM bank) */
                mmu->rom_bank = (mmu->rom_bank & 0x1F) | ((val & 0x03) << 5);
            } else {
                /* Mode select: 0=ROM, 1=RAM */
                mmu->rom_mode = val & 1;
            }
            break;

        case MBC2:
            if (addr < 0x2000) {
                /* RAM enable: bit 0 of upper byte must be 0 */
                if (!(addr & 0x0100)) {
                    mmu->ram_enabled = ((val & 0x0F) == 0x0A);
                }
            } else if (addr < 0x4000) {
                if (!(addr & 0x0100)) {
                    mmu->rom_bank = val & 0x0F;
                    if (mmu->rom_bank == 0) mmu->rom_bank = 1;
                }
            }
            break;

        case MBC3:
            if (addr < 0x2000) {
                mmu->ram_enabled = ((val & 0x0F) == 0x0A);
            } else if (addr < 0x4000) {
                /* ROM bank select (7 bits) */
                int bank = val & 0x7F;
                if (bank == 0) bank = 1;
                mmu->rom_bank = bank;
            } else if (addr < 0x6000) {
                /* RAM bank or RTC register select */
                mmu->ram_bank = val & 0x03;
                if (val >= 0x08 && val <= 0x0C) {
                    /* RTC register select */
                    mmu->ram_bank = val;
                }
            } else {
                /* Latch clock data */
                if (val == 0x00 && !mmu->rtc_latch) {
                    mmu->rtc_latch = true;
                    /* TODO: latch actual RTC time */
                } else if (val == 0x01) {
                    mmu->rtc_latch = false;
                }
            }
            break;

        case MBC5:
            if (addr < 0x2000) {
                mmu->ram_enabled = ((val & 0x0F) == 0x0A);
            } else if (addr < 0x3000) {
                /* ROM bank low 8 bits */
                mmu->rom_bank = (mmu->rom_bank & 0x100) | val;
            } else if (addr < 0x4000) {
                /* ROM bank bit 8 */
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
        gb->vram[addr - 0x8000] = val;
        return;
    }

    /* External RAM: A000-BFFF */
    if (addr < 0xC000) {
        if (!mmu->ram_enabled) return;
        /* TODO: implement external RAM write for MBC */
        return;
    }

    /* Work RAM: C000-DFFF */
    if (addr < 0xE000) {
        gb->wram[addr - 0xC000] = val;
        return;
    }

    /* Echo RAM: E000-FDFF */
    if (addr < 0xFE00) {
        gb->wram[addr - 0xE000] = val;
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
        u8 reg = addr & 0xFF;

        /* Joypad */
        if (reg == 0x00) {
            joypad_write(gb, val);
            return;
        }

        /* Timer registers */
        if (reg == 0x04) {
            /* Writing to DIV resets it to 0 */
            gb->io[0x04] = 0;
            gb->cpu.div_counter = 0;
            return;
        }
        if (reg == 0x05) { gb->io[0x05] = val; return; } /* TIMA */
        if (reg == 0x06) { gb->io[0x06] = val; return; } /* TMA */
        if (reg == 0x07) { gb->io[0x07] = val; return; } /* TAC */

        /* Interrupt Flag */
        if (reg == 0x0F) {
            gb->io[0x0F] = val;
            return;
        }

        /* Audio registers: pass through */
        if (reg >= 0x10 && reg <= 0x3F) {
            gb->io[reg] = val;
            return;
        }

        /* LCD registers */
        if (reg == 0x40) {
            gb->ppu.lcdc = val;
            gb->io[0x40] = val;
            return;
        }
        if (reg == 0x41) {
            /* STAT: lower 3 bits are read-only */
            gb->io[0x41] = (gb->io[0x41] & 0x07) | (val & 0xF8);
            gb->ppu.stat = gb->io[0x41];
            return;
        }
        if (reg == 0x42) { gb->ppu.scy = val; gb->io[0x42] = val; return; }
        if (reg == 0x43) { gb->ppu.scx = val; gb->io[0x43] = val; return; }
        if (reg == 0x44) { /* LY is read-only */ return; }
        if (reg == 0x45) {
            gb->ppu.lyc = val;
            gb->io[0x45] = val;
            return;
        }
        if (reg == 0x47) {
            gb->io[0x47] = val;
            /* Update PPU palette */
            gb->ppu.bg_palette[0] = val & 0x03;
            gb->ppu.bg_palette[1] = (val >> 2) & 0x03;
            gb->ppu.bg_palette[2] = (val >> 4) & 0x03;
            gb->ppu.bg_palette[3] = (val >> 6) & 0x03;
            return;
        }
        if (reg == 0x48) {
            gb->io[0x48] = val;
            gb->ppu.ob_palette[0][0] = val & 0x03;
            gb->ppu.ob_palette[0][1] = (val >> 2) & 0x03;
            gb->ppu.ob_palette[0][2] = (val >> 4) & 0x03;
            gb->ppu.ob_palette[0][3] = (val >> 6) & 0x03;
            return;
        }
        if (reg == 0x49) {
            gb->io[0x49] = val;
            gb->ppu.ob_palette[1][0] = val & 0x03;
            gb->ppu.ob_palette[1][1] = (val >> 2) & 0x03;
            gb->ppu.ob_palette[1][2] = (val >> 4) & 0x03;
            gb->ppu.ob_palette[1][3] = (val >> 6) & 0x03;
            return;
        }
        if (reg == 0x4A) { gb->ppu.wy = val; gb->io[0x4A] = val; return; }
        if (reg == 0x4B) { gb->ppu.wx = val; gb->io[0x4B] = val; return; }

        /* Boot ROM disable */
        if (reg == 0x50) {
            gb->io[0x50] = val;
            return;
        }

        /* DMA transfer: FF46 */
        if (reg == 0x46) {
            u16 src = val << 8;
            for (int i = 0; i < 0xA0; i++) {
                u8 data = gb_mmu_read(mmu, gb, src + i);
                gb->oam[i] = data;
                gb->ppu.oam[i] = data;
            }
            gb->io[0x46] = val;
            return;
        }

        /* All other I/O */
        gb->io[reg] = val;
        return;
    }

    /* High RAM: FF80-FFFE */
    if (addr < 0xFFFF) {
        gb->hram[addr - 0xFF80] = val;
        return;
    }

    /* Interrupt Enable: FFFF */
    if (addr == 0xFFFF) {
        gb->ie = val;
        return;
    }
}
