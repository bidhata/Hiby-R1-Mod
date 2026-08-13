#include "types.h"
#include "platform.h"
#include "menu.h"
#include "menu_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>

/* Exit status telling the boot script to start the HiBy music player.
 * Reserved for the "player" sentinel PARAM (see menu_config.h) -- anything
 * that starts bidhata-menu at boot is responsible for acting on this; the
 * launcher deliberately does not exec the player itself, so the player
 * keeps being started the same way the stock firmware starts it. */
#define BIDHATA_EXIT_RUN_PLAYER 10

/* Exit status telling the boot script to exec whatever command line this
 * process wrote to EXEC_TARGET_FILE. Used for every RUN-action item other
 * than the player sentinel (Rockbox, or anything else a config adds) --
 * the launcher never execs the target itself, the boot script does, same
 * hand-off pattern as BIDHATA_EXIT_RUN_PLAYER. */
#define BIDHATA_EXIT_RUN_TARGET 42
#define EXEC_TARGET_FILE "/usr/data/bidhata_exec_target"

static volatile sig_atomic_t running = 1;

static void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

/* system() is marked warn_unused_result; these calls are followed by a hang
 * waiting for the reboot/poweroff to land, so there's nothing to do with a
 * failure but note it. */
static void run_cmd(const char *cmd) {
    if (system(cmd) != 0) {
        fprintf(stderr, "command failed: %s\n", cmd);
    }
}

static void write_exec_target(const char *cmdline) {
    FILE *f = fopen(EXEC_TARGET_FILE, "w");
    if (!f) {
        fprintf(stderr, "cannot write exec target %s: %s\n",
                EXEC_TARGET_FILE, strerror(errno));
        return;
    }
    fprintf(f, "%s\n", cmdline);
    fclose(f);
}

static void print_usage(const char *argv0) {
    fprintf(stderr, "Usage: %s\n", argv0);
    fprintf(stderr, "  Shows the boot launcher: pick a utility, or hand back\n");
    fprintf(stderr, "  to the music player.\n");
}

int main(int argc, char *argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (argc >= 2 && (strcmp(argv[1], "-h") == 0 ||
                      strcmp(argv[1], "--help") == 0)) {
        print_usage(argv[0]);
        return 0;
    }

    static bidhata_platform_t platform;
    if (bidhata_platform_init(&platform) != 0) {
        fprintf(stderr, "Failed to initialize platform\n");
        /* Without a screen there is nothing to pick from, so ask for the music
         * player rather than leaving the device showing nothing. */
        return BIDHATA_EXIT_RUN_PLAYER;
    }

    /* No usable buttons would leave the menu impossible to answer, so hand
     * straight back to the player instead of blocking on input forever. */
    if (platform.input_count == 0) {
        fprintf(stderr, "No input devices; handing back to the music player\n");
        bidhata_platform_destroy(&platform);
        return BIDHATA_EXIT_RUN_PLAYER;
    }

    static bidhata_menu_config_t cfg;
    bidhata_menu_config_load(&cfg);

    int cursor = 0;
    int exit_code = 0;
    while (running) {
        bidhata_menu_result_t choice = bidhata_menu_run(&platform, &cursor, &cfg);

        if (choice.action == BIDHATA_MENU_QUIT) {
            break;
        }

        /* BIDHATA_MENU_ITEM_SELECTED: menu.c only returns this for RUN and
         * the three built-ins -- EXEC items are run in-process by menu.c
         * itself and never reach here. */
        const bidhata_menu_item_t *item = &cfg.items[choice.item_index];
        switch (item->action) {
        case BIDHATA_ACTION_RUN:
            if (strcmp(item->param, "player") == 0) {
                exit_code = BIDHATA_EXIT_RUN_PLAYER;
            } else {
                write_exec_target(item->param);
                exit_code = BIDHATA_EXIT_RUN_TARGET;
            }
            goto done;

        case BIDHATA_ACTION_SHUTDOWN:
            bidhata_platform_destroy(&platform);
            printf("Shutting down...\n");
            sync();
            run_cmd("poweroff");
            /* poweroff is asynchronous; wait here so the launcher doesn't
             * fall through to anything else while it happens. */
            for (;;) pause();

        case BIDHATA_ACTION_FW_UPDATE:
            bidhata_platform_destroy(&platform);
            printf("Rebooting into the firmware updater...\n");
            sync();
            run_cmd("/usr/bin/bootmode.sh Recovery");
            run_cmd("echo clear > /proc/jz/reset/reset");
            run_cmd("reboot");
            for (;;) pause();

        case BIDHATA_ACTION_FACTORY_RESET:
            bidhata_platform_destroy(&platform);
            printf("Factory reset requested, rebooting...\n");
            run_cmd("echo recovery_all > /data/recovery_all");
            sync();
            run_cmd("echo clear > /proc/jz/reset/reset");
            run_cmd("reboot");
            for (;;) pause();

        case BIDHATA_ACTION_EXEC:
            /* Unreachable: menu.c handles EXEC entirely itself and loops
             * back into bidhata_menu_run() rather than returning. */
            break;
        }
    }
done:

    bidhata_platform_destroy(&platform);
    if (exit_code == BIDHATA_EXIT_RUN_PLAYER) {
        printf("Switching to music player...\n");
    } else if (exit_code == BIDHATA_EXIT_RUN_TARGET) {
        printf("Switching to another target...\n");
    } else {
        printf("Goodbye!\n");
    }
    return exit_code;
}
