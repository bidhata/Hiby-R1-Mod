/* Boot launcher: lists the ROMs on the SD card plus an entry that returns the
 * device to the HiBy music player. Drawn straight to the framebuffer with the
 * same platform layer the emulator uses. */
#include "menu.h"
#include "font.h"
#include "palette.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* Candidate SD mount points, in priority order.
 *
 * /data/mnt/sd_0 is where the stock firmware puts the card: sys_server does the
 * mounting, but only when hiby_player asks it to, and the launcher runs in the
 * player's place. gb-launcher.sh mounts the card before starting the emulator;
 * the rest of these are checked so a card mounted by anything else is still
 * found. */
static const char *sd_mounts[] = {
    "/data/mnt/sd_0",
    "/data/mnt/sd_1",
    "/mnt/sd_0",
    "/mnt/sd_1",
    NULL
};

static const char *rom_exts[] = { ".gb", ".gbc", NULL };

/* Palette, matching the boot menu's dark blue scheme. */
#define COL_BG        0xFF1A1A2E
#define COL_PANEL     0xFF16213E
#define COL_HILIGHT   0xFF0F3460
#define COL_ACCENT    0xFFE94560
#define COL_TEXT      0xFFEAEAEA
#define COL_DIM       0xFF8888A0
#define COL_PLAYER    0xFF53C483
#define COL_PALETTE   0xFFD9A441

/* Fixed rows at the head of the list, before the ROMs. */
#define MENU_ROW_PLAYER    0
#define MENU_ROW_PALETTE   1
#define MENU_ROW_FIRST_ROM 2

#define ROW_H       56
#define LIST_TOP    150
#define MARGIN      24

static bool has_rom_ext(const char *name) {
    size_t nlen = strlen(name);
    for (int i = 0; rom_exts[i]; i++) {
        size_t slen = strlen(rom_exts[i]);
        if (nlen <= slen) continue;
        /* Case-insensitive so .GB and .GBC are picked up too. */
        if (strcasecmp(name + nlen - slen, rom_exts[i]) == 0) return true;
    }
    return false;
}

/* Copies the filename minus its extension into out, for display. */
static void pretty_name(const char *filename, char *out, size_t out_size) {
    snprintf(out, out_size, "%s", filename);
    char *dot = strrchr(out, '.');
    if (dot) *dot = '\0';
}

static int compare_roms(const void *a, const void *b) {
    const gb_menu_rom_t *ra = (const gb_menu_rom_t *)a;
    const gb_menu_rom_t *rb = (const gb_menu_rom_t *)b;
    return strcasecmp(ra->name, rb->name);
}

int gb_menu_scan_roms(gb_menu_rom_t *roms, int max_roms) {
    int count = 0;

    for (int m = 0; sd_mounts[m] && count < max_roms; m++) {
        char games[GB_MENU_PATH_MAX];
        snprintf(games, sizeof(games), "%s/games", sd_mounts[m]);

        DIR *dir = opendir(games);
        if (!dir) continue;

        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL && count < max_roms) {
            if (ent->d_name[0] == '.') continue;
            if (!has_rom_ext(ent->d_name)) continue;

            char path[GB_MENU_PATH_MAX];
            if (snprintf(path, sizeof(path), "%s/%s", games, ent->d_name)
                    >= (int)sizeof(path)) {
                continue; /* Path too long to store; skip it. */
            }

            struct stat st;
            if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;

            snprintf(roms[count].path, sizeof(roms[count].path), "%s", path);
            pretty_name(ent->d_name, roms[count].name, sizeof(roms[count].name));
            count++;
        }
        closedir(dir);
    }

    qsort(roms, (size_t)count, sizeof(roms[0]), compare_roms);
    return count;
}

/* Truncates text with a trailing ".." so a long title cannot overflow its row. */
static void fit_text(const char *in, char *out, size_t out_size,
                     int max_pixels, int scale) {
    snprintf(out, out_size, "%s", in);
    if (gb_font_width(out, scale) <= max_pixels) return;

    size_t len = strlen(out);
    while (len > 2 && gb_font_width(out, scale) > max_pixels) {
        len--;
        out[len] = '\0';
        if (len >= 2) {
            out[len - 1] = '.';
            out[len - 2] = '.';
        }
    }
}

static void draw_row(gb_platform_t *platform, int y, bool selected,
                     const char *label, u32 label_color, int width) {
    u32 bg = selected ? COL_HILIGHT : COL_PANEL;
    gb_platform_fill_rect(platform, MARGIN, y, width, ROW_H - 6, bg);

    /* Accent stripe marks the cursor row. */
    if (selected) {
        gb_platform_fill_rect(platform, MARGIN, y, 5, ROW_H - 6, COL_ACCENT);
    }

    char shown[GB_MENU_NAME_MAX];
    fit_text(label, shown, sizeof(shown), width - 40, 2);
    gb_font_draw(platform, MARGIN + 20, y + (ROW_H - 6 - GB_FONT_H * 2) / 2,
                 shown, label_color, 2);
}

static void draw_menu(gb_platform_t *platform, const gb_menu_rom_t *roms,
                      int rom_count, int selected, int scroll, int visible_rows) {
    int width = platform->fb_width - MARGIN * 2;

    gb_platform_clear(platform, COL_BG);

    /* Header. */
    gb_font_draw(platform, MARGIN, 50, "HIBY R1", COL_DIM, 2);
    gb_font_draw(platform, MARGIN, 80, "GAME BOY", COL_ACCENT, 3);
    gb_platform_fill_rect(platform, MARGIN, 125, width, 3, COL_ACCENT);

    /* Row 0 is the music player, row 1 the palette, then the ROMs. */
    for (int row = 0; row < visible_rows; row++) {
        int index = scroll + row;
        if (index >= rom_count + MENU_ROW_FIRST_ROM) break;

        int y = LIST_TOP + row * ROW_H;
        bool is_selected = (index == selected);

        if (index == MENU_ROW_PLAYER) {
            draw_row(platform, y, is_selected, "MUSIC PLAYER", COL_PLAYER, width);
        } else if (index == MENU_ROW_PALETTE) {
            /* Selecting this row cycles the shade set used by DMG games. */
            char label[GB_MENU_NAME_MAX];
            snprintf(label, sizeof(label), "PALETTE: %s",
                     gb_palette_name(gb_palette_load()));
            draw_row(platform, y, is_selected, label, COL_PALETTE, width);
        } else {
            draw_row(platform, y, is_selected, roms[index - MENU_ROW_FIRST_ROM].name,
                     COL_TEXT, width);
        }
    }

    /* Footer: either the controls or a note that no ROMs were found. */
    int footer_y = platform->fb_height - 70;
    if (rom_count == 0) {
        gb_font_draw(platform, MARGIN, footer_y - 30,
                     "NO ROMS IN SD games/", COL_ACCENT, 2);
    }
    gb_font_draw(platform, MARGIN, footer_y,
                 "VOL +/- MOVE   NEXT PICKS", COL_DIM, 2);

    /* Scroll position, when the list is longer than the screen. */
    int total_rows = rom_count + MENU_ROW_FIRST_ROM;
    if (total_rows > visible_rows) {
        char pos[32];
        snprintf(pos, sizeof(pos), "%d/%d", selected + 1, total_rows);
        gb_font_draw(platform, platform->fb_width - MARGIN - gb_font_width(pos, 2),
                     footer_y + 25, pos, COL_DIM, 2);
    }
}

gb_menu_result_t gb_menu_run(gb_platform_t *platform, int *start_index) {
    static gb_menu_rom_t roms[GB_MENU_MAX_ROMS];
    gb_menu_result_t result;
    memset(&result, 0, sizeof(result));

    int rom_count = gb_menu_scan_roms(roms, GB_MENU_MAX_ROMS);

    /* Entries are the music player, the palette, then every ROM. */
    int total = rom_count + MENU_ROW_FIRST_ROM;
    int selected = (start_index && *start_index < total) ? *start_index : 0;
    if (selected < 0) selected = 0;

    int visible_rows = (platform->fb_height - LIST_TOP - 110) / ROW_H;
    if (visible_rows < 1) visible_rows = 1;

    int scroll = 0;
    bool dirty = true;

    for (;;) {
        if (selected < scroll) scroll = selected;
        if (selected >= scroll + visible_rows) scroll = selected - visible_rows + 1;

        if (dirty) {
            draw_menu(platform, roms, rom_count, selected, scroll, visible_rows);
            dirty = false;
        }

        bool tapped = false;
        int tap_x = 0, tap_y = 0;
        gb_key_t key = gb_platform_poll_menu(platform, &tapped, &tap_x, &tap_y);

        /* A tap straight on a row picks it, so the menu is usable without
         * learning which button confirms. */
        if (tapped) {
            int row = (tap_y - LIST_TOP) / ROW_H;
            int index = scroll + row;
            if (tap_y >= LIST_TOP && row >= 0 && row < visible_rows &&
                index >= 0 && index < total) {
                selected = index;
                key = GB_KEY_SELECT;
            }
        }

        switch (key) {
        case GB_KEY_UP:
            selected = (selected - 1 + total) % total;
            dirty = true;
            break;

        case GB_KEY_DOWN:
            selected = (selected + 1) % total;
            dirty = true;
            break;

        case GB_KEY_SELECT:
            if (selected == MENU_ROW_PALETTE) {
                /* Cycle to the next shade set and stay in the menu, so the
                 * effect can be seen on the row itself. */
                gb_palette_id_t next =
                    (gb_palette_id_t)((gb_palette_load() + 1) % GB_PALETTE_COUNT);
                gb_palette_save(next);
                dirty = true;
                break;
            }

            if (start_index) *start_index = selected;
            if (selected == MENU_ROW_PLAYER) {
                result.action = GB_MENU_PLAYER;
            } else {
                result.action = GB_MENU_ROM;
                snprintf(result.rom_path, sizeof(result.rom_path), "%s",
                         roms[selected - MENU_ROW_FIRST_ROM].path);
            }
            return result;

        case GB_KEY_BACK:
            if (start_index) *start_index = selected;
            result.action = GB_MENU_QUIT;
            return result;

        case GB_KEY_NONE:
            break;
        }

        /* 50 ms between polls keeps the menu responsive without spinning. */
        usleep(50000);
    }
}
