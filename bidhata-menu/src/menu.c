/* Boot launcher: a config-driven list of device utilities (see
 * menu_config.h/.c). Drawn straight to the framebuffer. */
#include "menu.h"
#include "font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

/* Palette, matching the boot menu's dark blue scheme. */
#define COL_BG          0xFF1A1A2E
#define COL_PANEL       0xFF16213E
#define COL_HILIGHT     0xFF0F3460
#define COL_ACCENT      0xFFE94560
#define COL_TEXT        0xFFEAEAEA
#define COL_DIM         0xFF8888A0

/* Auto-boots to the "player" sentinel item after this many idle
 * milliseconds, so a device left at the menu (or one that rebooted for a
 * reason other than a person standing in front of it) doesn't just sit
 * there. */
#define IDLE_TIMEOUT_MS 5000

#define ROW_H       56
#define LIST_TOP    150
#define MARGIN      24

/* Truncates text with a trailing ".." so a long title cannot overflow its row. */
static void fit_text(const char *in, char *out, size_t out_size,
                     int max_pixels, int scale) {
    snprintf(out, out_size, "%s", in);
    if (bidhata_font_width(out, scale) <= max_pixels) return;

    size_t len = strlen(out);
    while (len > 2 && bidhata_font_width(out, scale) > max_pixels) {
        len--;
        out[len] = '\0';
        if (len >= 2) {
            out[len - 1] = '.';
            out[len - 2] = '.';
        }
    }
}

static void draw_row(bidhata_platform_t *platform, int y, bool selected,
                     const char *label, u32 label_color, int width) {
    u32 bg = selected ? COL_HILIGHT : COL_PANEL;
    bidhata_platform_fill_rect(platform, MARGIN, y, width, ROW_H - 6, bg);

    /* Accent stripe marks the cursor row. */
    if (selected) {
        bidhata_platform_fill_rect(platform, MARGIN, y, 5, ROW_H - 6, COL_ACCENT);
    }

    char shown[BIDHATA_MENU_LABEL_MAX];
    fit_text(label, shown, sizeof(shown), width - 40, 2);
    bidhata_font_draw(platform, MARGIN + 20, y + (ROW_H - 6 - BIDHATA_FONT_H * 2) / 2,
                 shown, label_color, 2);
}

/* Index of the reserved "run player" sentinel item, or -1 if the config
 * doesn't have one (a deliberately customized config might not). Used for
 * the idle-timeout auto-boot -- searched rather than assumed to be row 0,
 * since a custom config can reorder rows freely. */
static int find_player_index(const bidhata_menu_config_t *cfg) {
    for (int i = 0; i < cfg->count; i++)
        if (cfg->items[i].action == BIDHATA_ACTION_RUN &&
            strcmp(cfg->items[i].param, "player") == 0)
            return i;
    return -1;
}

static void draw_menu(bidhata_platform_t *platform, const bidhata_menu_config_t *cfg,
                      int selected, int scroll, int visible_rows, int idle_ms) {
    int width = platform->fb_width - MARGIN * 2;

    bidhata_platform_clear(platform, COL_BG);

    /* Header. */
    bidhata_font_draw(platform, MARGIN, 50, "HIBY R1", COL_DIM, 2);
    bidhata_font_draw(platform, MARGIN, 80, "BIDHATA MENU", COL_ACCENT, 3);
    bidhata_platform_fill_rect(platform, MARGIN, 125, width, 3, COL_ACCENT);

    for (int row = 0; row < visible_rows; row++) {
        int index = scroll + row;
        if (index >= cfg->count) break;

        int y = LIST_TOP + row * ROW_H;
        draw_row(platform, y, index == selected,
                cfg->items[index].label, cfg->items[index].color, width);
    }

    /* Footer. */
    int footer_y = platform->fb_height - 70;
    bidhata_font_draw(platform, MARGIN, footer_y,
                 "VOL +/- MOVE   NEXT PICKS", COL_DIM, 2);

    /* Idle countdown to the auto-boot -- always visible so the timeout is
     * never a surprise, and cancels the instant any key or tap arrives. */
    int player_index = find_player_index(cfg);
    int secs_left = (IDLE_TIMEOUT_MS - idle_ms + 999) / 1000;
    if (secs_left > 0 && player_index >= 0) {
        char msg[64];
        snprintf(msg, sizeof(msg), "STARTING %s IN %ds...",
                cfg->items[player_index].label, secs_left);
        bidhata_font_draw(platform, MARGIN, footer_y + 25, msg, COL_ACCENT, 2);
    }

    /* Scroll position, when the list is longer than the screen. */
    if (cfg->count > visible_rows) {
        char pos[32];
        snprintf(pos, sizeof(pos), "%d/%d", selected + 1, cfg->count);
        bidhata_font_draw(platform, platform->fb_width - MARGIN - bidhata_font_width(pos, 2),
                     footer_y + 25, pos, COL_DIM, 2);
    }
}

/* Full-screen "are you sure" gate for any item with non-empty confirm_text.
 * Vol Up/Down move between NO and YES, Next Track picks, Power always
 * cancels. Starts on NO so a stray tap or an accidental Next Track on the
 * row behind it can't complete a destructive action by itself -- confirming
 * takes a deliberate second selection. */
static bool confirm_dangerous_action(bidhata_platform_t *platform, const char *title,
                                     const char *detail, u32 accent) {
    int width = platform->fb_width - MARGIN * 2;
    bool yes_selected = false;
    bool dirty = true;

    for (;;) {
        if (dirty) {
            bidhata_platform_clear(platform, COL_BG);
            bidhata_font_draw(platform, MARGIN, 60, title, accent, 3);
            bidhata_font_draw(platform, MARGIN, 110, detail, COL_DIM, 2);

            int y = 200;
            draw_row(platform, y, !yes_selected, "CANCEL", COL_TEXT, width);
            draw_row(platform, y + ROW_H, yes_selected, "YES, CONTINUE", accent, width);

            bidhata_font_draw(platform, MARGIN, platform->fb_height - 70,
                         "VOL +/- MOVE   NEXT PICKS   POWER CANCELS", COL_DIM, 2);
            dirty = false;
        }

        bool tapped = false;
        int tap_x = 0, tap_y = 0;
        bidhata_key_t key = bidhata_platform_poll_menu(platform, &tapped, &tap_x, &tap_y);

        if (tapped) {
            if (tap_y >= 200 && tap_y < 200 + ROW_H) { yes_selected = false; key = BIDHATA_KEY_SELECT; }
            else if (tap_y >= 200 + ROW_H && tap_y < 200 + ROW_H * 2) { yes_selected = true; key = BIDHATA_KEY_SELECT; }
        }

        switch (key) {
        case BIDHATA_KEY_UP:
        case BIDHATA_KEY_DOWN:
            yes_selected = !yes_selected;
            dirty = true;
            break;
        case BIDHATA_KEY_SELECT:
            return yes_selected;
        case BIDHATA_KEY_BACK:
            return false;
        case BIDHATA_KEY_NONE:
            break;
        }

        usleep(50000);
    }
}

/* Runs a shell command that touches the SD card and returns to the menu
 * afterward (EXEC-action items only -- everything else leaves the menu for
 * good). Blocks on system() -- these can take a while over a big library --
 * with a plain "working" screen up first so the device doesn't look hung,
 * then a "done" screen that waits for a keypress before handing back to the
 * menu, so a summary printed on a fast run isn't missed. */
static void run_maintenance_action(bidhata_platform_t *platform, const char *working_label,
                                   const char *done_label, const char *cmd, u32 accent) {
    bidhata_platform_clear(platform, COL_BG);
    bidhata_font_draw(platform, MARGIN, 60, working_label, accent, 3);
    bidhata_font_draw(platform, MARGIN, 110, "This can take a while on a big library.",
                 COL_DIM, 2);

    if (system(cmd) != 0) {
        fprintf(stderr, "run_maintenance_action: command failed: %s\n", cmd);
    }

    bidhata_platform_clear(platform, COL_BG);
    bidhata_font_draw(platform, MARGIN, 60, done_label, accent, 3);
    bidhata_font_draw(platform, MARGIN, platform->fb_height - 70,
                 "NEXT TRACK CONTINUES", COL_DIM, 2);

    /* Swallow input until a real key arrives, so the tap/press that started
     * this action can't also dismiss the done screen. */
    for (;;) {
        bidhata_key_t key = bidhata_platform_poll_menu(platform, NULL, NULL, NULL);
        if (key == BIDHATA_KEY_SELECT || key == BIDHATA_KEY_BACK) break;
        usleep(50000);
    }
}

bidhata_menu_result_t bidhata_menu_run(bidhata_platform_t *platform, int *start_index,
                                       const bidhata_menu_config_t *cfg) {
    bidhata_menu_result_t result;
    memset(&result, 0, sizeof(result));

    int selected = (start_index && *start_index < cfg->count) ? *start_index : 0;
    if (selected < 0) selected = 0;

    int visible_rows = (platform->fb_height - LIST_TOP - 110) / ROW_H;
    if (visible_rows < 1) visible_rows = 1;

    int scroll = 0;
    bool dirty = true;
    int idle_ms = 0;

    for (;;) {
        if (selected < scroll) scroll = selected;
        if (selected >= scroll + visible_rows) scroll = selected - visible_rows + 1;

        if (dirty) {
            draw_menu(platform, cfg, selected, scroll, visible_rows, idle_ms);
            dirty = false;
        }

        bool tapped = false;
        int tap_x = 0, tap_y = 0;
        bidhata_key_t key = bidhata_platform_poll_menu(platform, &tapped, &tap_x, &tap_y);

        /* A tap straight on a row picks it, so the menu is usable without
         * learning which button confirms. */
        if (tapped) {
            int row = (tap_y - LIST_TOP) / ROW_H;
            int index = scroll + row;
            if (tap_y >= LIST_TOP && row >= 0 && row < visible_rows &&
                index >= 0 && index < cfg->count) {
                selected = index;
                key = BIDHATA_KEY_SELECT;
            }
        }

        if (key != BIDHATA_KEY_NONE) {
            idle_ms = 0;
        } else {
            idle_ms += 50;
            if (idle_ms >= IDLE_TIMEOUT_MS) {
                int player_index = find_player_index(cfg);
                if (player_index >= 0) {
                    if (start_index) *start_index = player_index;
                    result.action = BIDHATA_MENU_ITEM_SELECTED;
                    result.item_index = player_index;
                    return result;
                }
                /* No "player" sentinel in this config: fall back to
                 * quitting rather than guessing which row is "safe" to
                 * auto-select -- a custom config with no player row has
                 * opted out of the auto-boot's implicit assumption. */
                result.action = BIDHATA_MENU_QUIT;
                return result;
            }
            /* Countdown only needs to repaint once a second, not every
             * poll. */
            if (idle_ms % 1000 < 50) dirty = true;
        }

        switch (key) {
        case BIDHATA_KEY_UP:
            selected = (selected - 1 + cfg->count) % cfg->count;
            dirty = true;
            break;

        case BIDHATA_KEY_DOWN:
            selected = (selected + 1) % cfg->count;
            dirty = true;
            break;

        case BIDHATA_KEY_SELECT: {
            const bidhata_menu_item_t *item = &cfg->items[selected];
            bool needs_confirm = item->confirm_text[0] != '\0';

            if (!needs_confirm ||
                confirm_dangerous_action(platform, item->label,
                                         item->confirm_text, item->color)) {
                if (item->action == BIDHATA_ACTION_EXEC) {
                    char working[96], done[96];
                    snprintf(working, sizeof(working), "%s...", item->label);
                    snprintf(done, sizeof(done), "%s DONE", item->label);
                    run_maintenance_action(platform, working, done, item->param, item->color);
                    dirty = true;
                    break;
                }

                /* RUN and the three built-ins all leave the menu -- hand
                 * the selected config row back to main(), which owns the
                 * actual system()/exec sequencing. */
                if (start_index) *start_index = selected;
                result.action = BIDHATA_MENU_ITEM_SELECTED;
                result.item_index = selected;
                return result;
            }
            dirty = true;
            break;
        }

        case BIDHATA_KEY_BACK:
            if (start_index) *start_index = selected;
            result.action = BIDHATA_MENU_QUIT;
            return result;

        case BIDHATA_KEY_NONE:
            break;
        }

        /* 50 ms between polls keeps the menu responsive without spinning. */
        usleep(50000);
    }
}
