#ifndef BIDHATA_MENU_H
#define BIDHATA_MENU_H
/* menu.h -- the actual boot menu. Where pixels become choices and choices become shell commands. */
#include "types.h"
#include "platform.h"
#include "menu_config.h"

typedef enum {
    BIDHATA_MENU_QUIT = 0,      /* "i'm out, no pick" -- caller decides what's next */
    BIDHATA_MENU_ITEM_SELECTED, /* you picked a thing -- see item_index */
    BIDHATA_MENU_BACK,          /* back from submenu -- no drama */
} bidhata_menu_action_t;

/* EXEC items run inline and loop back; SUBMENU recurses and returns BACK.
 * Everything else (real pick, quit, timeout->player) bubbles straight up
 * like a well-behaved exception -- no swallowing, no drama. */

typedef struct {
    bidhata_menu_action_t action;
    int item_index; /* valid when action == BIDHATA_MENU_ITEM_SELECTED */
} bidhata_menu_result_t;

/* Draw one screen (main if group=="", else that submenu), block until pick/quit/timeout.
 * start_index = where cursor lands + where it ends (NULL = fresh top). cfg loaded once. */
bidhata_menu_result_t bidhata_menu_run(bidhata_platform_t *platform, int *start_index,
                                       const bidhata_menu_config_t *cfg, const char *group);

#endif
