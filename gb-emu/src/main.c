#include "gb.h"
#include "types.h"
#include "cpu.h"
#include "ppu.h"
#include "mmu.h"
#include "apu.h"
#include "platform.h"
#include "menu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

/* Exit status telling the boot script to start the HiBy music player. Anything
 * that starts gb-emu at boot is responsible for acting on this; the emulator
 * deliberately does not exec the player itself, so the player keeps being
 * started the same way the stock firmware starts it. */
#define GB_EXIT_RUN_PLAYER 10

static volatile sig_atomic_t running = 1;

static void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

/* Runs one ROM until the user quits it. Returns 0 normally, -1 if the ROM
 * could not be loaded. */
static int run_rom(gb_platform_t *platform, const char *rom_path) {
    static gb_t gb;

    gb_init(&gb);
    if (gb_load_rom(&gb, rom_path) != 0) {
        fprintf(stderr, "Failed to load ROM: %s\n", rom_path);
        return -1;
    }

    char title[17];
    memcpy(title, &gb.mmu.rom_data[0x134], 16);
    title[16] = '\0';
    for (int i = 0; i < 16; i++) {
        if (title[i] != '\0' && (title[i] < 0x20 || title[i] > 0x7E)) title[i] = ' ';
    }

    printf("ROM loaded: %s\n", rom_path);
    printf("Title: %s\n", title);
    printf("MBC type: %d, ROM %zu KB, RAM %zu KB%s\n",
           gb.mmu.mbc, gb.mmu.rom_size / 1024, gb.mmu.ram_size / 1024,
           gb.mmu.battery ? ", battery" : "");

    /* The emulator borrows the platform the menu already set up. Work through
     * the caller's copy rather than gb.platform: input state lives in there,
     * and a struct copy would strand every button and touch update in a
     * duplicate that nothing else reads. */
    gb.platform = platform;

    gb_platform_clear(platform, 0xFF000000);
    gb_platform_draw_touch_overlay(platform);

    gb.state = GB_STATE_RUNNING;

    while (gb.state == GB_STATE_RUNNING && running) {
        gb_run_frame(&gb);
        gb_platform_update_video(platform, &gb.ppu);
        gb_platform_update_audio(platform, &gb.apu);
        if (gb_platform_poll_input(platform)) {
            gb.state = GB_STATE_STOPPED;
        }
        gb_platform_wait_frame(platform);
    }

    /* Writes out the battery save, if the cart has one. */
    gb_destroy(&gb);
    return 0;
}

static void print_usage(const char *argv0) {
    fprintf(stderr, "Usage: %s [rom.gb]\n", argv0);
    fprintf(stderr, "  With no ROM, shows the launcher: pick a game from the\n");
    fprintf(stderr, "  SD card games/ folder, or hand back to the music player.\n");
}

int main(int argc, char *argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (argc >= 2 && (strcmp(argv[1], "-h") == 0 ||
                      strcmp(argv[1], "--help") == 0)) {
        print_usage(argv[0]);
        return 0;
    }

    static gb_platform_t platform;
    if (gb_platform_init(&platform) != 0) {
        fprintf(stderr, "Failed to initialize platform\n");
        /* Without a screen there is nothing to pick from, so ask for the music
         * player rather than leaving the device showing nothing. */
        return argc >= 2 ? 1 : GB_EXIT_RUN_PLAYER;
    }

    /* An explicit ROM path skips the launcher entirely. */
    if (argc >= 2) {
        int result = run_rom(&platform, argv[1]);
        gb_platform_destroy(&platform);
        printf("Goodbye!\n");
        return result == 0 ? 0 : 1;
    }

    /* No usable buttons would leave the menu impossible to answer, so hand
     * straight back to the player instead of blocking on input forever. */
    if (platform.input_count == 0) {
        fprintf(stderr, "No input devices; handing back to the music player\n");
        gb_platform_destroy(&platform);
        return GB_EXIT_RUN_PLAYER;
    }

    /* Launcher: keep returning here when a game exits, so the user can pick
     * another one without rebooting. */
    int cursor = 0;
    int exit_code = 0;
    while (running) {
        gb_menu_result_t choice = gb_menu_run(&platform, &cursor);

        if (choice.action == GB_MENU_PLAYER) {
            exit_code = GB_EXIT_RUN_PLAYER;
            break;
        }
        if (choice.action == GB_MENU_QUIT) {
            break;
        }

        run_rom(&platform, choice.rom_path);
    }

    gb_platform_destroy(&platform);
    printf(exit_code == GB_EXIT_RUN_PLAYER ? "Switching to music player...\n"
                                           : "Goodbye!\n");
    return exit_code;
}
