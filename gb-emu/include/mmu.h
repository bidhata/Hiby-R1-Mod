#ifndef GB_MMU_H
#define GB_MMU_H

#include "types.h"

#define ROM_BANK_SIZE 0x4000
#define RAM_BANK_SIZE 0x2000

typedef enum {
    MBC_NONE,
    MBC1,
    MBC2,
    MBC3,
    MBC5
} mbc_type_t;

struct gb;

typedef struct {
    u8 *rom_data;
    size_t rom_size;
    int rom_banks;

    u8 *ram_data;
    size_t ram_size;
    int ram_banks;

    int rom_bank;
    int ram_bank;
    bool ram_enabled;
    bool battery;
    bool has_rtc;
    mbc_type_t mbc;
    u8 rom_mode;

    u8 rtc_registers[5];
    u8 rtc_latched[5];
    bool rtc_latch;

    /* Path of the battery-backed save file; empty when the cart has none. */
    char save_path[512];
} gb_mmu_t;

u8 gb_mmu_read(gb_mmu_t *mmu, struct gb *gb, u16 addr);
void gb_mmu_write(gb_mmu_t *mmu, struct gb *gb, u16 addr, u8 val);
int gb_mmu_load_rom(gb_mmu_t *mmu, const char *path);
void gb_mmu_unload(gb_mmu_t *mmu);

/* Writes external RAM back to the .sav file for battery-backed carts. */
int gb_mmu_save_ram(gb_mmu_t *mmu);

/* Advances DIV and TIMA by the given number of T-cycles. */
void gb_mmu_timer_step(struct gb *gb, int cycles);

/* Copies the LCD-related I/O registers into the PPU state. */
void gb_mmu_sync_ppu(struct gb *gb);

/* Moves one block of an HBlank-mode HDMA transfer. Called by the PPU as each
 * HBlank begins; does nothing unless such a transfer is running. */
void gb_mmu_hdma_hblank(struct gb *gb);

#endif
