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

/* Exit code 10: "please start the boring music player." Reserved for the
 * "player" sentinel (PARAM=player). The boot script does the actual exec --
 * we just politely ask, like asking your RA to turn down the music. */
#define BIDHATA_EXIT_RUN_PLAYER 10

/* Exit code 42: the answer to life, the universe, and "run something else."
 * Used for every RUN item that isn't the player sentinel (Rockbox, etc.).
 * We write the real command to EXEC_TARGET_FILE and exit 42; the launcher
 * reads the file and execs it -- same baton-pass as EXIT_RUN_PLAYER. */
#define BIDHATA_EXIT_RUN_TARGET 42
#define EXEC_TARGET_FILE "/usr/data/bidhata_exec_target"

static volatile sig_atomic_t running = 1;

static void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

/* system() really wants you to check its return value. We do, we just
 * don't care all that much -- we're about to reboot/poweroff anyway, so a
 * failure here is like worrying about your GPA during the apocalypse. */
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
        fprintf(stderr, "Failed to initialize platform -- no screen, no party\n");
        /* No framebuffer = no menu. Hand back to hiby_player so the device
         * doesn't just stare blankly into the void like a philosophy major. */
        return BIDHATA_EXIT_RUN_PLAYER;
    }

    /* Zero buttons = zero hope of answering the menu. Politely bow out and
     * let hiby_player take over instead of waiting forever like a lonely
     * undergrad outside office hours. */
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
        bidhata_menu_result_t choice = bidhata_menu_run(&platform, &cursor, &cfg, "");

        if (choice.action == BIDHATA_MENU_QUIT) {
            break;
        }

        /* ITEM_SELECTED only arrives for RUN and the three built-ins.
         * EXEC/submenu never escape menu.c -- they run inline and loop back,
         * like a grad student who keeps "just fixing one more thing" at 2 AM. */
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
            printf("Shutting down... stay groovy, R1.\n");
            sync();
            run_cmd("poweroff");
            /* poweroff is async -- we park here forever so nothing trickles
             * past us. Like leaving the party early and guarding the door. */
            for (;;) pause();

        case BIDHATA_ACTION_FW_UPDATE:
            bidhata_platform_destroy(&platform);
            printf("Rebooting into the firmware updater...\n");
            sync();
            run_cmd("/usr/bin/bootmode.sh Recovery");
            run_cmd("echo clear > /proc/jz/reset/reset");
            run_cmd("reboot");
            for (;;) pause(); /* hold the line until silicon resets */

        case BIDHATA_ACTION_FACTORY_RESET:
            bidhata_platform_destroy(&platform);
            printf("Factory reset requested, rebooting... yeet everything.\n");
            run_cmd("echo recovery_all > /data/recovery_all");
            sync();
            run_cmd("echo clear > /proc/jz/reset/reset");
            run_cmd("reboot");
            for (;;) pause(); /* Thanos would be proud. */

        case BIDHATA_ACTION_EXEC:
        case BIDHATA_ACTION_SUBMENU:
            /* Unreachable -- menu.c handles both inline and never bubbles
             * them here. If this fires, someone broke the contract. */
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
