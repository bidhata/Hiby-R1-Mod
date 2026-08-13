#ifndef BIDHATA_MENU_CONFIG_H
#define BIDHATA_MENU_CONFIG_H
#include "types.h"

#define BIDHATA_MENU_MAX_ITEMS 32
#define BIDHATA_MENU_LABEL_MAX 64
#define BIDHATA_MENU_PARAM_MAX 128
#define BIDHATA_MENU_CONFIRM_MAX 128

typedef enum {
    BIDHATA_ACTION_RUN,           /* PARAM="player" (sentinel) or a real path+args */
    BIDHATA_ACTION_EXEC,          /* PARAM=shell command, runs in-process, returns */
    BIDHATA_ACTION_SHUTDOWN,      /* built-in, PARAM ignored */
    BIDHATA_ACTION_FACTORY_RESET, /* built-in, PARAM ignored */
    BIDHATA_ACTION_FW_UPDATE      /* built-in, PARAM ignored */
} bidhata_action_t;

typedef struct {
    char label[BIDHATA_MENU_LABEL_MAX];
    u32 color;
    bidhata_action_t action;
    char param[BIDHATA_MENU_PARAM_MAX];
    char confirm_text[BIDHATA_MENU_CONFIRM_MAX]; /* empty = no confirm gate */
} bidhata_menu_item_t;

typedef struct {
    bidhata_menu_item_t items[BIDHATA_MENU_MAX_ITEMS];
    int count;
} bidhata_menu_config_t;

/* Loads /usr/data/bidhata-menu.conf, falling back to /usr/bin/bidhata-menu.conf,
 * falling back to a compiled-in default identical to the shipped
 * config/bidhata-menu.conf.default. Always succeeds -- a missing or
 * malformed config can never leave the menu empty. */
void bidhata_menu_config_load(bidhata_menu_config_t *cfg);

#endif /* BIDHATA_MENU_CONFIG_H */
