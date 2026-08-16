#ifndef BIDHATA_MENU_CONFIG_H
#define BIDHATA_MENU_CONFIG_H
/* menu_config.h -- where a humble pipe-delimited text file becomes menu rows.
 * LABEL|COLOR|ACTION|PARAM|CONFIRM|GROUP. Simple enough to edit on your
 * phone. No JSON was harmed in the making of this parser. */
#include "types.h"

#define BIDHATA_MENU_MAX_ITEMS 32
#define BIDHATA_MENU_LABEL_MAX 64
#define BIDHATA_MENU_PARAM_MAX 128
#define BIDHATA_MENU_CONFIRM_MAX 128
#define BIDHATA_MENU_GROUP_MAX 32

typedef enum {
    BIDHATA_ACTION_RUN,           /* PARAM=player (sentinel) or a binary path */
    BIDHATA_ACTION_EXEC,          /* PARAM=shell command, runs and returns to menu */
    BIDHATA_ACTION_SHUTDOWN,      /* yeet to off (PARAM ignored) */
    BIDHATA_ACTION_FACTORY_RESET, /* yeet everything (PARAM ignored, has confirm gate!) */
    BIDHATA_ACTION_FW_UPDATE,     /* yeet to updater (PARAM ignored) */
    BIDHATA_ACTION_SUBMENU        /* PARAM=group name to open */
} bidhata_action_t;

typedef struct {
    char label[BIDHATA_MENU_LABEL_MAX];
    u32 color;
    bidhata_action_t action;
    char param[BIDHATA_MENU_PARAM_MAX];
    char confirm_text[BIDHATA_MENU_CONFIRM_MAX]; /* empty = no confirm gate */
    char group[BIDHATA_MENU_GROUP_MAX]; /* empty = shown on the main screen */
} bidhata_menu_item_t;

typedef struct {
    bidhata_menu_item_t items[BIDHATA_MENU_MAX_ITEMS];
    int count;
} bidhata_menu_config_t;

/* Load from /usr/data/*.conf, else /usr/bin/*.conf, else hardcoded fallback.
 * Never fails -- worst case you get default menu. Like fallback fonts, but for choices. */
void bidhata_menu_config_load(bidhata_menu_config_t *cfg);

#endif /* BIDHATA_MENU_CONFIG_H */
