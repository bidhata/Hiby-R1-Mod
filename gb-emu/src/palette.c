/* Colour schemes for the four DMG shades, and their persistence. */
#include "palette.h"

#include <stdio.h>
#include <string.h>

/* Each row is lightest to darkest, matching the shade numbering the PPU uses. */
static const u32 palettes[GB_PALETTE_COUNT][4] = {
    /* The original DMG's green LCD. */
    [GB_PALETTE_GREEN]  = { 0xFF9BBC0F, 0xFF8BAC0F, 0xFF306230, 0xFF0F380F },
    /* Plain greyscale. */
    [GB_PALETTE_GREY]   = { 0xFFFFFFFF, 0xFFAAAAAA, 0xFF555555, 0xFF000000 },
    /* Game Boy Pocket: near-neutral, a touch warm. */
    [GB_PALETTE_POCKET] = { 0xFFE3E6C9, 0xFFC3C4A5, 0xFF8E8B61, 0xFF1F1F1F },
    /* Warm amber, in the spirit of an old handheld screen. */
    [GB_PALETTE_AMBER]  = { 0xFFFFEFCE, 0xFFE0A56B, 0xFFA9603A, 0xFF3B1F0B },
};

static const char *names[GB_PALETTE_COUNT] = {
    [GB_PALETTE_GREEN]  = "GREEN",
    [GB_PALETTE_GREY]   = "GREY",
    [GB_PALETTE_POCKET] = "POCKET",
    [GB_PALETTE_AMBER]  = "AMBER",
};

/* Lives on the writable data partition: the rootfs is read-only squashfs. */
#define PALETTE_FILE "/usr/data/gb_palette"

const u32 *gb_palette_colors(gb_palette_id_t id) {
    if (id < 0 || id >= GB_PALETTE_COUNT) id = GB_PALETTE_GREEN;
    return palettes[id];
}

const char *gb_palette_name(gb_palette_id_t id) {
    if (id < 0 || id >= GB_PALETTE_COUNT) id = GB_PALETTE_GREEN;
    return names[id];
}

gb_palette_id_t gb_palette_load(void) {
    FILE *f = fopen(PALETTE_FILE, "r");
    if (!f) return GB_PALETTE_GREEN;

    char buf[32] = { 0 };
    if (!fgets(buf, sizeof(buf), f)) {
        fclose(f);
        return GB_PALETTE_GREEN;
    }
    fclose(f);

    /* Stored by name so the file stays readable and survives reordering. */
    for (int i = 0; i < GB_PALETTE_COUNT; i++) {
        size_t len = strlen(names[i]);
        if (strncmp(buf, names[i], len) == 0) return (gb_palette_id_t)i;
    }
    return GB_PALETTE_GREEN;
}

void gb_palette_save(gb_palette_id_t id) {
    FILE *f = fopen(PALETTE_FILE, "w");
    if (!f) return;
    fprintf(f, "%s\n", gb_palette_name(id));
    fclose(f);
}
