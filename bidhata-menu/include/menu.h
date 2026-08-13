#ifndef BIDHATA_MENU_H
#define BIDHATA_MENU_H

#include "types.h"
#include "platform.h"
#include "menu_config.h"

typedef enum {
    BIDHATA_MENU_QUIT = 0,      /* shut the launcher down without starting anything */
    BIDHATA_MENU_ITEM_SELECTED, /* result.item_index names which config row */
} bidhata_menu_action_t;

/* EXEC-action items (strip_art_all.sh / remove_folder_art.sh and anything
 * else config-defined) are run inline by the menu itself and never
 * produce this result -- the user stays in the menu until they pick
 * something that leaves it. */

typedef struct {
    bidhata_menu_action_t action;
    int item_index; /* valid when action == BIDHATA_MENU_ITEM_SELECTED */
} bidhata_menu_result_t;

/* Draws the launcher and blocks until the user picks something that leaves
 * it. start_index is the row to place the cursor on, and is updated to
 * wherever the cursor ended so a return trip lands on the same entry. cfg
 * is loaded once by the caller (main.c), not reloaded every frame. */
bidhata_menu_result_t bidhata_menu_run(bidhata_platform_t *platform, int *start_index,
                                       const bidhata_menu_config_t *cfg);

#endif
