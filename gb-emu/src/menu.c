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
#define COL_BG          0xFF1A1A2E
#define COL_PANEL       0xFF16213E
#define COL_HILIGHT     0xFF0F3460
#define COL_ACCENT      0xFFE94560
#define COL_TEXT        0xFFEAEAEA
#define COL_DIM         0xFF8888A0
#define COL_PLAYER      0xFF53C483
#define COL_PALETTE     0xFFD9A441
#define COL_SHUTDOWN    0xFFE9A441
#define COL_FW_UPDATE   0xFF5A9EE9
#define COL_DANGER      0xFFE94560
#define COL_STRIP       0xFF9B7FD4

/* Layout: PLAYER, a non-selectable "GAMES" divider, PALETTE, then every ROM
 * -- the maintenance entries (SHUTDOWN and everything after) come after the
 * ROM list, so their row indices depend on rom_count and are computed once
 * per gb_menu_run() call rather than being compile-time constants. */
#define MENU_ROW_PLAYER       0
#define MENU_ROW_GAMES_HEADER 1
#define MENU_ROW_PALETTE      2
#define MENU_ROW_FIRST_ROM    3

/* Fixed offsets from util_base (= MENU_ROW_FIRST_ROM + rom_count). */
#define UTIL_SHUTDOWN        0
#define UTIL_FW_UPDATE       1
#define UTIL_FACTORY_RESET   2
#define UTIL_STRIP_FILE_ART  3
#define UTIL_STRIP_ALBUM_ART 4
#define UTIL_COUNT           5

/* Auto-boots to the music player after this many idle milliseconds, so a
 * device left at the menu (or one that rebooted for a reason other than a
 * person standing in front of it) doesn't just sit there. */
#define IDLE_TIMEOUT_MS 5000

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

/* Plain label, no background/highlight/stripe -- marks a group of rows
 * below it rather than being a row itself. Never the cursor target. */
static void draw_section_header(gb_platform_t *platform, int y, const char *label) {
    gb_font_draw(platform, MARGIN + 4, y + (ROW_H - 6 - GB_FONT_H) / 2,
                 label, COL_DIM, 1);
}

/* Row label/color for every entry that isn't the player, the header, the
 * palette, or a ROM -- i.e. everything at or after util_base. Centralized
 * here so draw_menu and the input switch can't drift out of sync on what
 * index maps to what action. */
static void util_row_info(int offset, const char **label, u32 *color) {
    switch (offset) {
    case UTIL_SHUTDOWN:        *label = "SHUTDOWN";               *color = COL_SHUTDOWN; break;
    case UTIL_FW_UPDATE:       *label = "FIRMWARE UPDATE (SD)";   *color = COL_FW_UPDATE; break;
    case UTIL_FACTORY_RESET:   *label = "FACTORY RESET";          *color = COL_DANGER; break;
    case UTIL_STRIP_FILE_ART:  *label = "STRIP FILE ART";         *color = COL_STRIP; break;
    case UTIL_STRIP_ALBUM_ART: *label = "STRIP ALBUM ART";        *color = COL_STRIP; break;
    default:                   *label = "?";                      *color = COL_TEXT; break;
    }
}

static void draw_menu(gb_platform_t *platform, const gb_menu_rom_t *roms,
                      int rom_count, int selected, int scroll, int visible_rows,
                      int util_base, int total, int idle_ms) {
    int width = platform->fb_width - MARGIN * 2;

    gb_platform_clear(platform, COL_BG);

    /* Header. */
    gb_font_draw(platform, MARGIN, 50, "HIBY R1", COL_DIM, 2);
    gb_font_draw(platform, MARGIN, 80, "GAME BOY", COL_ACCENT, 3);
    gb_platform_fill_rect(platform, MARGIN, 125, width, 3, COL_ACCENT);

    for (int row = 0; row < visible_rows; row++) {
        int index = scroll + row;
        if (index >= total) break;

        int y = LIST_TOP + row * ROW_H;
        bool is_selected = (index == selected);

        if (index == MENU_ROW_PLAYER) {
            draw_row(platform, y, is_selected, "HIBY PLAYER", COL_PLAYER, width);
        } else if (index == MENU_ROW_GAMES_HEADER) {
            draw_section_header(platform, y, "GAMES");
        } else if (index == MENU_ROW_PALETTE) {
            /* Selecting this row cycles the shade set used by DMG games. */
            char label[GB_MENU_NAME_MAX];
            snprintf(label, sizeof(label), "PALETTE: %s",
                     gb_palette_name(gb_palette_load()));
            draw_row(platform, y, is_selected, label, COL_PALETTE, width);
        } else if (index < util_base) {
            draw_row(platform, y, is_selected, roms[index - MENU_ROW_FIRST_ROM].name,
                     COL_TEXT, width);
        } else {
            const char *label;
            u32 color;
            util_row_info(index - util_base, &label, &color);
            draw_row(platform, y, is_selected, label, color, width);
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

    /* Idle countdown to the auto-boot -- always visible so the timeout is
     * never a surprise, and cancels the instant any key or tap arrives. */
    int secs_left = (IDLE_TIMEOUT_MS - idle_ms + 999) / 1000;
    if (secs_left > 0) {
        char msg[48];
        snprintf(msg, sizeof(msg), "STARTING PLAYER IN %ds...", secs_left);
        gb_font_draw(platform, MARGIN, footer_y + 25, msg, COL_ACCENT, 2);
    }

    /* Scroll position, when the list is longer than the screen. */
    if (total > visible_rows) {
        char pos[32];
        snprintf(pos, sizeof(pos), "%d/%d", selected + 1, total);
        gb_font_draw(platform, platform->fb_width - MARGIN - gb_font_width(pos, 2),
                     footer_y + 25, pos, COL_DIM, 2);
    }
}

/* Full-screen "are you sure" gate for the three actions that leave the menu
 * for good (power off, reflash, wipe /data). Vol Up/Down move between NO and
 * YES, Next Track picks, Power always cancels. Starts on NO so a stray tap
 * or an accidental Next Track on the row behind it can't complete a wipe by
 * itself -- confirming takes a deliberate second selection. */
static bool confirm_dangerous_action(gb_platform_t *platform, const char *title,
                                     const char *detail, u32 accent) {
    int width = platform->fb_width - MARGIN * 2;
    bool yes_selected = false;
    bool dirty = true;

    for (;;) {
        if (dirty) {
            gb_platform_clear(platform, COL_BG);
            gb_font_draw(platform, MARGIN, 60, title, accent, 3);
            gb_font_draw(platform, MARGIN, 110, detail, COL_DIM, 2);

            int y = 200;
            draw_row(platform, y, !yes_selected, "CANCEL", COL_TEXT, width);
            draw_row(platform, y + ROW_H, yes_selected, "YES, CONTINUE", accent, width);

            gb_font_draw(platform, MARGIN, platform->fb_height - 70,
                         "VOL +/- MOVE   NEXT PICKS   POWER CANCELS", COL_DIM, 2);
            dirty = false;
        }

        bool tapped = false;
        int tap_x = 0, tap_y = 0;
        gb_key_t key = gb_platform_poll_menu(platform, &tapped, &tap_x, &tap_y);

        if (tapped) {
            if (tap_y >= 200 && tap_y < 200 + ROW_H) { yes_selected = false; key = GB_KEY_SELECT; }
            else if (tap_y >= 200 + ROW_H && tap_y < 200 + ROW_H * 2) { yes_selected = true; key = GB_KEY_SELECT; }
        }

        switch (key) {
        case GB_KEY_UP:
        case GB_KEY_DOWN:
            yes_selected = !yes_selected;
            dirty = true;
            break;
        case GB_KEY_SELECT:
            return yes_selected;
        case GB_KEY_BACK:
            return false;
        case GB_KEY_NONE:
            break;
        }

        usleep(50000);
    }
}

/* Runs a shell command that touches the SD card and returns to the menu
 * afterward (unlike SHUTDOWN/FIRMWARE UPDATE/FACTORY RESET, which never
 * come back). Blocks on system() -- these can take a while over a big
 * library -- with a plain "working" screen up first so the device doesn't
 * look hung, then a "done" screen that waits for a keypress before handing
 * back to the menu, so a summary printed on a fast run isn't missed. */
static void run_maintenance_action(gb_platform_t *platform, const char *working_label,
                                   const char *done_label, const char *cmd) {
    gb_platform_clear(platform, COL_BG);
    gb_font_draw(platform, MARGIN, 60, working_label, COL_STRIP, 3);
    gb_font_draw(platform, MARGIN, 110, "This can take a while on a big library.",
                 COL_DIM, 2);

    if (system(cmd) != 0) {
        fprintf(stderr, "run_maintenance_action: command failed: %s\n", cmd);
    }

    gb_platform_clear(platform, COL_BG);
    gb_font_draw(platform, MARGIN, 60, done_label, COL_STRIP, 3);
    gb_font_draw(platform, MARGIN, platform->fb_height - 70,
                 "NEXT TRACK CONTINUES", COL_DIM, 2);

    /* Swallow input until a real key arrives, so the tap/press that started
     * this action can't also dismiss the done screen. */
    for (;;) {
        gb_key_t key = gb_platform_poll_menu(platform, NULL, NULL, NULL);
        if (key == GB_KEY_SELECT || key == GB_KEY_BACK) break;
        usleep(50000);
    }
}

/* Moves off the non-selectable GAMES divider in whichever direction the
 * cursor was already heading, so Up/Down never lands the cursor on a row
 * that can't be picked. */
static int skip_header(int index, int dir, int total) {
    if (index == MENU_ROW_GAMES_HEADER) {
        index = (index + dir + total) % total;
    }
    return index;
}

gb_menu_result_t gb_menu_run(gb_platform_t *platform, int *start_index) {
    static gb_menu_rom_t roms[GB_MENU_MAX_ROMS];
    gb_menu_result_t result;
    memset(&result, 0, sizeof(result));

    int rom_count = gb_menu_scan_roms(roms, GB_MENU_MAX_ROMS);

    /* Entries: player, GAMES divider, palette, every ROM, then the
     * maintenance rows. util_base is where the ROM list ends. */
    int util_base = MENU_ROW_FIRST_ROM + rom_count;
    int total = util_base + UTIL_COUNT;
    int selected = (start_index && *start_index < total) ? *start_index : 0;
    if (selected < 0 || selected == MENU_ROW_GAMES_HEADER) selected = 0;

    int visible_rows = (platform->fb_height - LIST_TOP - 110) / ROW_H;
    if (visible_rows < 1) visible_rows = 1;

    int scroll = 0;
    bool dirty = true;
    int idle_ms = 0;

    for (;;) {
        if (selected < scroll) scroll = selected;
        if (selected >= scroll + visible_rows) scroll = selected - visible_rows + 1;

        if (dirty) {
            draw_menu(platform, roms, rom_count, selected, scroll, visible_rows,
                     util_base, total, idle_ms);
            dirty = false;
        }

        bool tapped = false;
        int tap_x = 0, tap_y = 0;
        gb_key_t key = gb_platform_poll_menu(platform, &tapped, &tap_x, &tap_y);

        /* A tap straight on a row picks it, so the menu is usable without
         * learning which button confirms. Taps on the divider do nothing. */
        if (tapped) {
            int row = (tap_y - LIST_TOP) / ROW_H;
            int index = scroll + row;
            if (tap_y >= LIST_TOP && row >= 0 && row < visible_rows &&
                index >= 0 && index < total && index != MENU_ROW_GAMES_HEADER) {
                selected = index;
                key = GB_KEY_SELECT;
            }
        }

        if (key != GB_KEY_NONE) {
            idle_ms = 0;
        } else {
            idle_ms += 50;
            if (idle_ms >= IDLE_TIMEOUT_MS) {
                if (start_index) *start_index = MENU_ROW_PLAYER;
                result.action = GB_MENU_PLAYER;
                return result;
            }
            /* Countdown only needs to repaint once a second, not every
             * poll. */
            if (idle_ms % 1000 < 50) dirty = true;
        }

        switch (key) {
        case GB_KEY_UP:
            selected = skip_header((selected - 1 + total) % total, -1, total);
            dirty = true;
            break;

        case GB_KEY_DOWN:
            selected = skip_header((selected + 1) % total, 1, total);
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

            if (selected >= util_base) {
                int offset = selected - util_base;
                switch (offset) {
                case UTIL_SHUTDOWN:
                    if (confirm_dangerous_action(platform, "SHUTDOWN",
                            "Power off the device.", COL_SHUTDOWN)) {
                        if (start_index) *start_index = selected;
                        result.action = GB_MENU_SHUTDOWN;
                        return result;
                    }
                    break;
                case UTIL_FW_UPDATE:
                    if (confirm_dangerous_action(platform, "FIRMWARE UPDATE",
                            "Reboots into the updater. Needs a .upt file",
                            COL_FW_UPDATE)) {
                        if (start_index) *start_index = selected;
                        result.action = GB_MENU_FIRMWARE_UPDATE;
                        return result;
                    }
                    break;
                case UTIL_FACTORY_RESET:
                    if (confirm_dangerous_action(platform, "FACTORY RESET",
                            "Erases ALL data on the device. Cannot be undone.",
                            COL_DANGER)) {
                        if (start_index) *start_index = selected;
                        result.action = GB_MENU_FACTORY_RESET;
                        return result;
                    }
                    break;
                case UTIL_STRIP_FILE_ART:
                    if (confirm_dangerous_action(platform, "STRIP FILE ART",
                            "Removes embedded art from every FLAC/MP3 on SD.",
                            COL_STRIP)) {
                        run_maintenance_action(platform, "STRIPPING FILE ART...",
                            "FILE ART STRIPPED", "strip_art_all.sh");
                    }
                    break;
                case UTIL_STRIP_ALBUM_ART:
                    if (confirm_dangerous_action(platform, "STRIP ALBUM ART",
                            "Deletes folder.jpg/cover.png etc. from SD. Cannot be undone.",
                            COL_STRIP)) {
                        run_maintenance_action(platform, "STRIPPING ALBUM ART...",
                            "ALBUM ART STRIPPED", "remove_folder_art.sh -f");
                    }
                    break;
                }
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
