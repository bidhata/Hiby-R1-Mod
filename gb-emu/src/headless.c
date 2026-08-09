/* Headless test harness: runs a ROM without a framebuffer or audio device and
 * prints the resulting screen state. Useful for checking that the emulator
 * boots on a host machine, and for eyeballing test-ROM output over a terminal. */
#include "gb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The four DMG shades, darkest last, as produced by the PPU. */
static const u32 dmg_palette[4] = {
    0xFF9BBC0F, 0xFF8BAC0F, 0xFF306230, 0xFF0F380F
};

static int shade_of(u32 color) {
    for (int i = 0; i < 4; i++) {
        if (dmg_palette[i] == color) return i;
    }
    /* Colour output (CGB, or a non-default DMG palette) has no fixed shade, so
     * fall back to brightness. */
    int r = (color >> 16) & 0xFF, g = (color >> 8) & 0xFF, b = color & 0xFF;
    int luma = (r * 30 + g * 59 + b * 11) / 100;
    return 3 - (luma * 4) / 256;
}

/* Writes the framebuffer as a PPM, the simplest way to check colour output. */
static int write_ppm(const gb_t *gb, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    fprintf(f, "P6\n%d %d\n255\n", GB_WIDTH, GB_HEIGHT);
    for (int i = 0; i < GB_WIDTH * GB_HEIGHT; i++) {
        u32 c = gb->ppu.framebuffer[i];
        unsigned char rgb[3] = {
            (unsigned char)((c >> 16) & 0xFF),
            (unsigned char)((c >> 8) & 0xFF),
            (unsigned char)(c & 0xFF)
        };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return 0;
}

static void print_screen(const gb_t *gb) {
    static const char ramp[4] = { ' ', '.', '+', '#' };

    /* Two scanlines per text row keeps the aspect ratio close to square. */
    for (int y = 0; y < GB_HEIGHT; y += 2) {
        char line[GB_WIDTH + 1];
        for (int x = 0; x < GB_WIDTH; x++) {
            line[x] = ramp[shade_of(gb->ppu.framebuffer[y * GB_WIDTH + x])];
        }
        line[GB_WIDTH] = '\0';
        printf("%s\n", line);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <rom.gb> [frames] [--screen] [--ppm out.ppm]\n", argv[0]);
        return 1;
    }

    int frames = (argc >= 3) ? atoi(argv[2]) : 600;
    if (frames <= 0) frames = 600;

    bool show_screen = false;
    const char *ppm_path = NULL;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--screen") == 0) show_screen = true;
        else if (strcmp(argv[i], "--ppm") == 0 && i + 1 < argc) ppm_path = argv[++i];
    }

    static gb_t gb;
    gb_init(&gb);
    /* Test ROMs report pass/fail over the link cable. */
    gb.serial_log = true;

    if (gb_load_rom(&gb, argv[1]) != 0) {
        fprintf(stderr, "Failed to load ROM: %s\n", argv[1]);
        return 1;
    }

    printf("ROM: %s (MBC %d, ROM %zu KB, RAM %zu KB, %s)\n",
           argv[1], gb.mmu.mbc, gb.mmu.rom_size / 1024, gb.mmu.ram_size / 1024,
           gb.cgb_mode ? "CGB" : "DMG");

    for (int i = 0; i < frames; i++) {
        gb_run_frame(&gb);
    }

    /* Count non-blank pixels: a game that never draws leaves the screen empty. */
    int drawn = 0;
    for (int i = 0; i < GB_WIDTH * GB_HEIGHT; i++) {
        if (gb.ppu.framebuffer[i] != dmg_palette[0]) drawn++;
    }

    printf("Frames run: %d\n", gb.frame_count);
    printf("PC=%04X SP=%04X AF=%04X BC=%04X DE=%04X HL=%04X\n",
           gb.cpu.reg.pc, gb.cpu.reg.sp, gb.cpu.reg.af,
           gb.cpu.reg.bc, gb.cpu.reg.de, gb.cpu.reg.hl);
    printf("LCDC=%02X STAT=%02X LY=%d IE=%02X IF=%02X halted=%d ime=%d\n",
           gb.ppu.lcdc, gb.ppu.stat, gb.ppu.ly, gb.ie, gb.io[0x0F],
           (int)gb.cpu.halted, (int)gb.cpu.ime);
    printf("Non-background pixels: %d / %d\n", drawn, GB_WIDTH * GB_HEIGHT);

    if (show_screen) print_screen(&gb);
    if (ppm_path && write_ppm(&gb, ppm_path) == 0) {
        printf("Wrote %s\n", ppm_path);
    }

    gb_destroy(&gb);
    return drawn > 0 ? 0 : 2;
}
