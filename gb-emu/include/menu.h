#ifndef GB_MENU_H
#define GB_MENU_H

#include "types.h"
#include "platform.h"

#define GB_MENU_MAX_ROMS 128
#define GB_MENU_PATH_MAX 512
#define GB_MENU_NAME_MAX 128

typedef enum {
    GB_MENU_QUIT = 0,   /* shut the launcher down without starting anything */
    GB_MENU_PLAYER,     /* hand the device back to the HiBy music player */
    GB_MENU_ROM         /* run rom_path */
} gb_menu_action_t;

typedef struct {
    gb_menu_action_t action;
    char rom_path[GB_MENU_PATH_MAX];
} gb_menu_result_t;

typedef struct {
    char name[GB_MENU_NAME_MAX];
    char path[GB_MENU_PATH_MAX];
} gb_menu_rom_t;

/* Collects ROMs from every SD mount's games/ folder, sorted by name. Returns
 * how many were stored, capped at GB_MENU_MAX_ROMS. */
int gb_menu_scan_roms(gb_menu_rom_t *roms, int max_roms);

/* Draws the launcher and blocks until the user picks something. start_index is
 * the row to place the cursor on, and is updated to wherever the cursor ended
 * so a return trip from a game lands on the same entry. */
gb_menu_result_t gb_menu_run(gb_platform_t *platform, int *start_index);

#endif
