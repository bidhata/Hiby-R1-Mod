/* Boot launcher: a config-driven list of device utilities (see
 * menu_config.h/.c). Drawn straight to the framebuffer -- raw pixels,
 * no X11, no Wayland, just us and /dev/fb0. Like graphics programming
 * in 1987, but with more MIPS. */
#include "menu.h"
#include "font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>

/* Palette -- dark academia for audiophiles. If Wes Anderson designed firmware. */
#define COL_BG          0xFF1A1A2E
#define COL_PANEL       0xFF16213E
#define COL_HILIGHT     0xFF0F3460
#define COL_ACCENT      0xFFE94560
#define COL_TEXT        0xFFEAEAEA
#define COL_DIM         0xFF8888A0

/* After 5s of you staring at the menu doing nothing, we take the hint
 * and boot the music player. Like a waiter who notices you keep reading
 * the menu but never ordering. */
#define IDLE_TIMEOUT_MS 5000

static bool g_idle_frozen = false;

#define ROW_H       56
#define LIST_TOP    150
#define MARGIN      24

/* Long label? Snip it and add ".." -- typographic mercy, not malware. */
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

    /* Accent stripe -- the "you are here" dot on the mall directory. */
    if (selected) {
        bidhata_platform_fill_rect(platform, MARGIN, y, 5, ROW_H - 6, COL_ACCENT);
    }

    char shown[BIDHATA_MENU_LABEL_MAX];
    fit_text(label, shown, sizeof(shown), width - 40, 2);
    bidhata_font_draw(platform, MARGIN + 20, y + (ROW_H - 6 - BIDHATA_FONT_H * 2) / 2,
                 shown, label_color, 2);
}

/* Where's the "player" sentinel? Could be anywhere -- configs reorder freely.
 * We scan the whole thing. Timeout needs this to know where to auto-launch. */
static int find_player_index(const bidhata_menu_config_t *cfg) {
    for (int i = 0; i < cfg->count; i++)
        if (cfg->items[i].action == BIDHATA_ACTION_RUN &&
            strcmp(cfg->items[i].param, "player") == 0)
            return i;
    return -1;
}

/* Collect rows for `group` ("" = main screen). Submenus aren't a separate
 * data structure -- just rows sharing a GROUP tag. Cheaper than a tree,
 * harder to desync. Big brain flat-file design. */
static int filter_items(const bidhata_menu_config_t *cfg, const char *group, int *out_idx) {
    int n = 0;
    for (int i = 0; i < cfg->count; i++)
        if (strcmp(cfg->items[i].group, group) == 0)
            out_idx[n++] = i;
    return n;
}

static void draw_menu(bidhata_platform_t *platform, const bidhata_menu_config_t *cfg,
                      const int *idx, int count, const char *group,
                      int selected, int scroll, int visible_rows,
                      int idle_ms, bool idle_frozen) {
    int width = platform->fb_width - MARGIN * 2;

    bidhata_platform_clear(platform, COL_BG);

    /* Header -- shows submenu name when nested, otherwise BIDHATA MENU. */
    bidhata_font_draw(platform, MARGIN, 50, "HIBY R1", COL_DIM, 2);
    bidhata_font_draw(platform, MARGIN, 80, group[0] ? group : "BIDHATA MENU", COL_ACCENT, 3);
    bidhata_platform_fill_rect(platform, MARGIN, 125, width, 3, COL_ACCENT);

    for (int row = 0; row < visible_rows; row++) {
        int pos = scroll + row;
        if (pos >= count) break;

        const bidhata_menu_item_t *item = &cfg->items[idx[pos]];
        int y = LIST_TOP + row * ROW_H;
        draw_row(platform, y, pos == selected, item->label, item->color, width);
    }

    /* Footer: controls + sneaky idle countdown (when ticking). */
    int footer_y = platform->fb_height - 70;
    bidhata_font_draw(platform, MARGIN, footer_y,
                 group[0] ? "VOL +/- MOVE   NEXT PICKS   POWER BACK"
                          : "VOL +/- MOVE   NEXT PICKS", COL_DIM, 2);

    /* Countdown to "you didn't pick, so we pick the player for you."
     * Hidden when frozen (Up/Down was touched -- user is browsing, don't rush). */
    int player_index = find_player_index(cfg);
    int secs_left = (IDLE_TIMEOUT_MS - idle_ms + 999) / 1000;
    if (!idle_frozen && secs_left > 0 && player_index >= 0) {
        char msg[64];
        snprintf(msg, sizeof(msg), "STARTING %s IN %ds...",
                cfg->items[player_index].label, secs_left);
        bidhata_font_draw(platform, MARGIN, footer_y + 25, msg, COL_ACCENT, 2);
    }

    /* Scroll position, when the list is longer than the screen. */
    if (count > visible_rows) {
        char pos[32];
        snprintf(pos, sizeof(pos), "%d/%d", selected + 1, count);
        bidhata_font_draw(platform, platform->fb_width - MARGIN - bidhata_font_width(pos, 2),
                     footer_y + 25, pos, COL_DIM, 2);
    }
}

/* "Are you sure?" gate. Defaults to NO -- we assume you butt-dialed it.
 * Vol Up/Down wiggles between NO/YES, Next confirms, Power nopes out. */
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

/* Working screen: where "please wait, crunching your JPEGs" lives. */
#define STATUS_Y  170
#define STATUS_H  (BIDHATA_FONT_H * 2 + 8)

static void draw_working_screen(bidhata_platform_t *platform, const char *working_label,
                                u32 accent) {
    bidhata_platform_clear(platform, COL_BG);
    bidhata_font_draw(platform, MARGIN, 60, working_label, accent, 3);
    bidhata_font_draw(platform, MARGIN, 110, "This can take a while on a big library.",
                 COL_DIM, 2);
}

static void update_working_status(bidhata_platform_t *platform, const char *status) {
    int width = platform->fb_width - MARGIN * 2;
    bidhata_platform_fill_rect(platform, MARGIN, STATUS_Y, width, STATUS_H, COL_BG);

    char shown[BIDHATA_MENU_LABEL_MAX];
    fit_text(status, shown, sizeof(shown), width, 2);
    bidhata_font_draw(platform, MARGIN, STATUS_Y, shown, COL_TEXT, 2);
}

/* Run `cmd` and stream its output line by line. NOT popen() -- popen waits
 * for EOF, and `adbon` spawns adbd which inherits the pipe and holds it
 * open forever. That used to wedge the menu on "starting adb..." until
 * the heat death of the universe. We waitpid() instead: when the child
 * exits, we bail, daemon or no daemon. Big brain fix, tiny diff. */
static int run_command_streaming(const char *cmd, void (*on_line)(void *, const char *),
                                 void *ctx) {
    int fds[2];
    if (pipe(fds) != 0) return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return -1;
    }
    if (pid == 0) {
        close(fds[0]);
        if (dup2(fds[1], STDOUT_FILENO) < 0) _exit(127);
        close(fds[1]);
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }

    close(fds[1]);
    fcntl(fds[0], F_SETFL, O_NONBLOCK);

    char line[256];
    size_t len = 0;
    bool child_done = false;
    int status = 0;

    for (;;) {
        struct pollfd pfd = { .fd = fds[0], .events = POLLIN, .revents = 0 };
        /* Poll with 200ms heartbeat so we notice child death quickly. */
        int pr = poll(&pfd, 1, child_done ? 0 : 200);

        bool got_data = false;
        if (pr > 0 && (pfd.revents & (POLLIN | POLLHUP))) {
            char buf[256];
            ssize_t n = read(fds[0], buf, sizeof(buf));
            if (n > 0) {
                got_data = true;
                for (ssize_t i = 0; i < n; i++) {
                    if (buf[i] == '\n' || buf[i] == '\r' || len + 1 >= sizeof(line)) {
                        line[len] = '\0';
                        if (len > 0 && on_line) on_line(ctx, line);
                        len = 0;
                        if (buf[i] != '\n' && buf[i] != '\r') line[len++] = buf[i];
                    } else {
                        line[len++] = buf[i];
                    }
                }
            } else if (n == 0) {
                break; /* real EOF: nothing holds the write end */
            }
        }

        if (!child_done && waitpid(pid, &status, WNOHANG) == pid) child_done = true;
        if (child_done && !got_data) break; /* child dead + no output = we're done */
    }

    if (len > 0 && on_line) {
        line[len] = '\0';
        on_line(ctx, line);
    }

    close(fds[0]);
    if (!child_done) waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* One-shot status updater: each line the child prints becomes the on-screen ticker.
 * Watching "47/200 flac_with_a 4MB wall art" crawl by is the only proof
 * your slow MIPS isn't dead. */
static void status_line_cb(void *ctx, const char *line) {
    update_working_status((bidhata_platform_t *)ctx, line);
}

static void run_maintenance_action(bidhata_platform_t *platform, const char *working_label,
                                   const char *done_label, const char *cmd, u32 accent) {
    draw_working_screen(platform, working_label, accent);

    int rc = run_command_streaming(cmd, status_line_cb, platform);
    if (rc < 0) {
        fprintf(stderr, "run_maintenance_action: cannot start: %s\n", cmd);
        update_working_status(platform, "FAILED TO START");
    } else if (rc != 0) {
        fprintf(stderr, "run_maintenance_action: command failed (%d): %s\n", rc, cmd);
    }

    bidhata_platform_clear(platform, COL_BG);
    bidhata_font_draw(platform, MARGIN, 60, done_label, accent, 3);
    bidhata_font_draw(platform, MARGIN, platform->fb_height - 70,
                 "NEXT TRACK CONTINUES", COL_DIM, 2);

    /* Wait for an actual key -- swallow the phantom tap that launched us. */
    for (;;) {
        bidhata_key_t key = bidhata_platform_poll_menu(platform, NULL, NULL, NULL);
        if (key == BIDHATA_KEY_SELECT || key == BIDHATA_KEY_BACK) break;
        usleep(50000);
    }
}

bidhata_menu_result_t bidhata_menu_run(bidhata_platform_t *platform, int *start_index,
                                       const bidhata_menu_config_t *cfg, const char *group) {
    bidhata_menu_result_t result;
    memset(&result, 0, sizeof(result));

    int idx[BIDHATA_MENU_MAX_ITEMS];
    int count = filter_items(cfg, group, idx);
    if (count < 1) {
        /* Submenu points to a group nobody belongs to? Ghost menu. Treat as Back. */
        result.action = group[0] ? BIDHATA_MENU_BACK : BIDHATA_MENU_QUIT;
        return result;
    }

    /* Restore cursor to where you left it (top-level only). Submenus always
     * start fresh -- like reopening the fridge hoping food appeared. */
    int selected = 0;
    if (start_index) {
        for (int i = 0; i < count; i++) {
            if (idx[i] == *start_index) { selected = i; break; }
        }
    }

    int visible_rows = (platform->fb_height - LIST_TOP - 110) / ROW_H;
    if (visible_rows < 1) visible_rows = 1;

    int scroll = 0;
    bool dirty = true;
    int idle_ms = 0;
    if (group[0] == '\0') g_idle_frozen = false;
    bool idle_frozen = g_idle_frozen;

    for (;;) {
        if (selected < scroll) scroll = selected;
        if (selected >= scroll + visible_rows) scroll = selected - visible_rows + 1;

        if (dirty) {
            draw_menu(platform, cfg, idx, count, group, selected, scroll, visible_rows,
                     idle_ms, idle_frozen);
            dirty = false;
        }

        bool tapped = false;
        int tap_x = 0, tap_y = 0;
        bidhata_key_t key = bidhata_platform_poll_menu(platform, &tapped, &tap_x, &tap_y);

        /* Tap a row directly -- because remembering button combos is for exams. */
        if (tapped) {
            int row = (tap_y - LIST_TOP) / ROW_H;
            int pos = scroll + row;
            if (tap_y >= LIST_TOP && row >= 0 && row < visible_rows &&
                pos >= 0 && pos < count) {
                selected = pos;
                key = BIDHATA_KEY_SELECT;
            }
        }

        if (key == BIDHATA_KEY_UP || key == BIDHATA_KEY_DOWN) {
            /* Scrolling = browsing. Freeze the countdown -- no one likes being
             * rushed while they're still deciding. Sticky across submenus. */
            idle_frozen = true;
            g_idle_frozen = true;
        }

        if (key != BIDHATA_KEY_NONE) {
            idle_ms = 0;
        } else if (!idle_frozen) {
            idle_ms += 50;
            if (idle_ms >= IDLE_TIMEOUT_MS) {
                int player_index = find_player_index(cfg);
                if (player_index >= 0) {
                    /* Timeout! Auto-pick the player. Works even deep in a submenu
                     * -- bubbles up through every recursive call. */
                    if (start_index) *start_index = player_index;
                    result.action = BIDHATA_MENU_ITEM_SELECTED;
                    result.item_index = player_index;
                    return result;
                }
                /* No player row? We quit instead of guessing. You removed
                 * the escape hatch, you own the consequences. */
                result.action = BIDHATA_MENU_QUIT;
                return result;
            }
            /* Repaint countdown once a second -- not every 50ms. We're thrifty with pixels. */
            if (idle_ms % 1000 < 50) dirty = true;
        }

        switch (key) {
        case BIDHATA_KEY_UP:
            selected = (selected - 1 + count) % count;
            dirty = true;
            break;

        case BIDHATA_KEY_DOWN:
            selected = (selected + 1) % count;
            dirty = true;
            break;

        case BIDHATA_KEY_SELECT: {
            const bidhata_menu_item_t *item = &cfg->items[idx[selected]];
            bool needs_confirm = item->confirm_text[0] != '\0';

            if (!needs_confirm ||
                confirm_dangerous_action(platform, item->label,
                                         item->confirm_text, item->color)) {
                if (item->action == BIDHATA_ACTION_SUBMENU) {
                    bidhata_menu_result_t sub = bidhata_menu_run(platform, NULL, cfg, item->param);
                    if (sub.action == BIDHATA_MENU_BACK) {
                        /* Back from submenu -- reset timer; keep freeze if scrolling already happened. */
                        idle_ms = 0;
                        idle_frozen = g_idle_frozen;
                        dirty = true;
                        break;
                    }
                    /* Real selection / quit / timeout: propagate straight up. */
                    return sub;
                }

                if (item->action == BIDHATA_ACTION_EXEC) {
                    char working[96], done[96];
                    snprintf(working, sizeof(working), "%s...", item->label);
                    snprintf(done, sizeof(done), "%s DONE", item->label);
                    run_maintenance_action(platform, working, done, item->param, item->color);
                    idle_ms = 0;
                    idle_frozen = g_idle_frozen;
                    dirty = true;
                    break;
                }

                /* RUN + built-ins leave the menu. Tell main() to do the dirty work. */
                if (start_index) *start_index = idx[selected];
                result.action = BIDHATA_MENU_ITEM_SELECTED;
                result.item_index = idx[selected];
                return result;
            }
            dirty = true;
            break;
        }

        case BIDHATA_KEY_BACK:
            if (group[0]) {
                /* Inside a submenu: pop one level, don't quit the menu. */
                result.action = BIDHATA_MENU_BACK;
                return result;
            }
            if (start_index) *start_index = idx[selected];
            result.action = BIDHATA_MENU_QUIT;
            return result;

        case BIDHATA_KEY_NONE:
            break;
        }

        /* 50 ms between polls keeps the menu responsive without spinning. */
        usleep(50000);
    }
}
