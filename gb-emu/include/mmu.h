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
    int rom_bank;
    int ram_bank;
    bool ram_enabled;
    bool battery;
    mbc_type_t mbc;
    u8 rom_mode;
    u8 rtcRegisters[5];
    u8 rtc_latched[5];
    bool rtc_latch;
} gb_mmu_t;

u8 gb_mmu_read(gb_mmu_t *mmu, struct gb *gb, u16 addr);
void gb_mmu_write(gb_mmu_t *mmu, struct gb *gb, u16 addr, u8 val);
int gb_mmu_load_rom(gb_mmu_t *mmu, const char *path);

#endif
