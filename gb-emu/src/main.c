#include "gb.h"
#include "types.h"
#include "cpu.h"
#include "ppu.h"
#include "mmu.h"
#include "apu.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

static volatile int running = 1;

/* Candidate mount points for the SD card, in priority order. */
static const char *sd_mounts[] = {
    "/mnt/sd_0",
    "/mnt/sd_1",
    "/data/mnt/sd_0",
    "/data/mnt/sd_1",
    NULL
};

/* ROM filename extensions, in priority order. */
static const char *rom_exts[] = { ".gb", ".gbc", ".GBC", ".GB", NULL };

static int has_suffix(const char *name, const char *suffix) {
    size_t nlen = strlen(name);
    size_t slen = strlen(suffix);
    if (slen > nlen) return 0;
    return strcmp(name + nlen - slen, suffix) == 0;
}

/* Find the first ROM inside <mount>/games/. Returns 0 with a
 * malloc'd path on success, -1 if none found. */
static int find_sd_rom(char *out, size_t out_size) {
    for (int m = 0; sd_mounts[m]; m++) {
        char games[256];
        snprintf(games, sizeof(games), "%s/games", sd_mounts[m]);

        struct stat st;
        if (stat(games, &st) != 0 || !S_ISDIR(st.st_mode)) {
            continue;
        }

        DIR *dir = opendir(games);
        if (!dir) continue;

        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (ent->d_name[0] == '.') continue;
            for (int e = 0; rom_exts[e]; e++) {
                if (has_suffix(ent->d_name, rom_exts[e])) {
                    char path[512];
                    snprintf(path, sizeof(path), "%s/%s", games, ent->d_name);
                    struct stat fst;
                    if (stat(path, &fst) == 0 && S_ISREG(fst.st_mode)) {
                        snprintf(out, out_size, "%s", path);
                        closedir(dir);
                        return 0;
                    }
                }
            }
        }
        closedir(dir);
    }
    return -1;
}

static void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

int gb_init(gb_t *gb) {
    memset(gb, 0, sizeof(gb_t));

    gb_cpu_init(&gb->cpu);
    gb_ppu_init(&gb->ppu);
    gb_apu_init(&gb->apu);

    gb->cpu.reg.af = 0x01B0;
    gb->cpu.reg.bc = 0x0013;
    gb->cpu.reg.de = 0x00D8;
    gb->cpu.reg.hl = 0x014D;
    gb->cpu.reg.sp = 0xFFFE;
    gb->cpu.reg.pc = 0x0100;

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
    gb->io[0x42] = 0x00;
    gb->io[0x43] = 0x00;
    gb->io[0x45] = 0x00;
    gb->io[0x47] = 0xFC;
    gb->io[0x48] = 0xFF;
    gb->io[0x49] = 0xFF;
    gb->io[0x4A] = 0x00;
    gb->io[0x4B] = 0x00;
    gb->io[0x50] = 0xFF;

    gb->joypad_state = 0xFF;
    gb->cart_loaded = false;
    gb->state = GB_STATE_STOPPED;
    gb->frame_count = 0;

    return 0;
}

void gb_destroy(gb_t *gb) {
    if (gb->mmu.rom_data) {
        free(gb->mmu.rom_data);
        gb->mmu.rom_data = NULL;
    }
    gb->cart_loaded = false;
}

int gb_load_rom(gb_t *gb, const char *path) {
    return gb_mmu_load_rom(&gb->mmu, path);
}

void gb_run_frame(gb_t *gb) {
    gb->ppu.frame_ready = false;

    while (!gb->ppu.frame_ready) {
        int cycles = gb_cpu_step(&gb->cpu, gb);
        gb_ppu_step(&gb->ppu, gb, cycles);
        gb_apu_step(&gb->apu, gb, cycles);

        gb->io[0x04]++;

        if (gb->io[0x07] & 0x04) {
            gb->io[0x05]++;
            if (gb->io[0x05] == 0) {
                gb->io[0x05] = gb->io[0x06];
                gb->io[0x0F] |= 0x04;
            }
        }
    }

    gb->frame_count++;
}

void gb_reset(gb_t *gb) {
    gb_cpu_init(&gb->cpu);
    gb_ppu_init(&gb->ppu);
    gb_apu_init(&gb->apu);

    gb->cpu.reg.af = 0x01B0;
    gb->cpu.reg.bc = 0x0013;
    gb->cpu.reg.de = 0x00D8;
    gb->cpu.reg.hl = 0x014D;
    gb->cpu.reg.sp = 0xFFFE;
    gb->cpu.reg.pc = 0x0100;

    memset(gb->io, 0, sizeof(gb->io));
    gb->io[0x50] = 0xFF;
}

int main(int argc, char *argv[]) {
    char rom_path[512];
    const char *rom_arg = NULL;

    if (argc >= 2) {
        rom_arg = argv[1];
    } else {
        /* No ROM given: auto-detect from SD card games/ folder. */
        if (find_sd_rom(rom_path, sizeof(rom_path)) != 0) {
            fprintf(stderr, "No ROM found. Put .gb/.gbc files in SD card games/ folder.\n");
            fprintf(stderr, "Usage: gb-emu <rom.gb>\n");
            return 1;
        }
        printf("Auto-detected ROM: %s\n", rom_path);
        rom_arg = rom_path;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    gb_t gb;
    gb_init(&gb);

    if (gb_load_rom(&gb, rom_arg) != 0) {
        fprintf(stderr, "Failed to load ROM: %s\n", rom_arg);
        return 1;
    }

    printf("ROM loaded: %s\n", rom_arg);
    printf("Title: %.16s\n", &gb.mmu.rom_data[0x134]);
    printf("MBC type: %d\n", gb.mmu.mbc);

    if (gb_platform_init(&gb.platform) != 0) {
        fprintf(stderr, "Failed to initialize platform\n");
        gb_destroy(&gb);
        return 1;
    }

    printf("Platform initialized. Running...\n");

    gb.state = GB_STATE_RUNNING;

    while (gb.state == GB_STATE_RUNNING && running) {
        gb_run_frame(&gb);
        gb_platform_update_video(&gb.platform, &gb.ppu);
        gb_platform_update_audio(&gb.platform, &gb.apu);
        int quit = gb_platform_poll_input(&gb.platform);
        if (quit) gb.state = GB_STATE_STOPPED;
        gb_platform_wait_frame(&gb.platform);
    }

    gb_platform_destroy(&gb.platform);
    gb_destroy(&gb);
    printf("Goodbye!\n");
    return 0;
}
