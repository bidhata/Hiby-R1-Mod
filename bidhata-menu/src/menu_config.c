/* Config-driven menu item loading. Format documented in
 * docs/superpowers/plans/2026-08-13-bidhata-menu-rename-and-config.md and
 * config/bidhata-menu.conf.default: one pipe-delimited line per item,
 *   LABEL|COLOR|ACTION|PARAM|CONFIRM_TEXT
 * '#'-prefixed and blank lines ignored. Tries /usr/data/bidhata-menu.conf,
 * then /usr/bin/bidhata-menu.conf, then falls back to a compiled-in
 * default so a missing or malformed config can never leave the menu
 * empty. */
#include "menu_config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>

struct color_name { const char *name; u32 value; };
/* Matches menu.c's COL_* palette. */
static const struct color_name PALETTE[] = {
    { "PLAYER",    0xFF53C483 },
    { "ROCKBOX",   0xFF4CAF50 },
    { "SHUTDOWN",  0xFFE9A441 },
    { "FW_UPDATE", 0xFF5A9EE9 },
    { "DANGER",    0xFFE94560 },
    { "STRIP",     0xFF9B7FD4 },
    { "TEXT",      0xFFEAEAEA },
};

static u32 parse_color(const char *s)
{
    for (size_t i = 0; i < sizeof(PALETTE) / sizeof(PALETTE[0]); i++)
        if (strcasecmp(s, PALETTE[i].name) == 0)
            return PALETTE[i].value;
    /* Escape hatch: a bare 0xRRGGBB for anything the palette doesn't cover. */
    return (u32)strtoul(s, NULL, 0) | 0xFF000000;
}

static bidhata_action_t parse_action(const char *s)
{
    if (strcmp(s, "run") == 0)           return BIDHATA_ACTION_RUN;
    if (strcmp(s, "exec") == 0)          return BIDHATA_ACTION_EXEC;
    if (strcmp(s, "shutdown") == 0)      return BIDHATA_ACTION_SHUTDOWN;
    if (strcmp(s, "factory_reset") == 0) return BIDHATA_ACTION_FACTORY_RESET;
    if (strcmp(s, "fw_update") == 0)     return BIDHATA_ACTION_FW_UPDATE;
    return BIDHATA_ACTION_RUN; /* unknown keyword: safest fallback, not a crash */
}

/* Splits "a|b|c|d|e" into up to 5 fields in place, trailing fields may be
 * empty/absent. Returns the number of fields found. */
static int split_fields(char *line, char *fields[5])
{
    int n = 0;
    char *p = line;
    fields[n++] = p;
    while (n < 5 && (p = strchr(p, '|')) != NULL)
    {
        *p++ = '\0';
        fields[n++] = p;
    }
    return n;
}

static bool parse_line(char *line, bidhata_menu_item_t *item)
{
    /* strip trailing newline/CR */
    line[strcspn(line, "\r\n")] = '\0';
    if (line[0] == '\0' || line[0] == '#')
        return false;

    char *fields[5] = { "", "", "", "", "" };
    int n = split_fields(line, fields);
    if (n < 3) /* need at least LABEL|COLOR|ACTION */
        return false;

    memset(item, 0, sizeof(*item));
    snprintf(item->label, sizeof(item->label), "%s", fields[0]);
    item->color = parse_color(fields[1]);
    item->action = parse_action(fields[2]);
    if (n > 3) snprintf(item->param, sizeof(item->param), "%s", fields[3]);
    if (n > 4) snprintf(item->confirm_text, sizeof(item->confirm_text), "%s", fields[4]);
    return true;
}

/* Compiled-in fallback, kept byte-identical to
 * config/bidhata-menu.conf.default -- see that file for the human-edited
 * source of truth. Reusing parse_line() here (rather than filling the
 * struct fields directly) guarantees the fallback is parsed by the exact
 * same code path as a real file, so the two can never silently drift
 * apart in behavior even if the text itself is duplicated. */
static void load_defaults(bidhata_menu_config_t *cfg)
{
    static const char *const DEFAULT_LINES[] = {
        "HIBY PLAYER|PLAYER|run|player|",
        "ROCKBOX (BETA)|ROCKBOX|run|/usr/bin/rockbox.r1|",
        "SHUTDOWN|SHUTDOWN|shutdown||Power off the device.",
        "FIRMWARE UPDATE (SD)|FW_UPDATE|fw_update||Reboots into the updater. Needs a .upt file",
        "FACTORY RESET|DANGER|factory_reset||Erases ALL data on the device. Cannot be undone.",
        "STRIP FILE ART|STRIP|exec|strip_art_all.sh|Removes embedded art from every FLAC/MP3 on SD.",
        "STRIP ALBUM ART|STRIP|exec|remove_folder_art.sh -f|Deletes folder.jpg/cover.png etc. from SD. Cannot be undone.",
    };
    cfg->count = 0;
    for (size_t i = 0; i < sizeof(DEFAULT_LINES) / sizeof(DEFAULT_LINES[0]); i++)
    {
        char line[256];
        snprintf(line, sizeof(line), "%s", DEFAULT_LINES[i]);
        if (cfg->count < BIDHATA_MENU_MAX_ITEMS && parse_line(line, &cfg->items[cfg->count]))
            cfg->count++;
    }
}

static bool load_file(const char *path, bidhata_menu_config_t *cfg)
{
    FILE *f = fopen(path, "r");
    if (!f) return false;

    cfg->count = 0;
    char line[256];
    while (cfg->count < BIDHATA_MENU_MAX_ITEMS && fgets(line, sizeof(line), f))
    {
        if (parse_line(line, &cfg->items[cfg->count]))
            cfg->count++;
    }
    fclose(f);
    return cfg->count > 0;
}

void bidhata_menu_config_load(bidhata_menu_config_t *cfg)
{
    if (load_file("/usr/data/bidhata-menu.conf", cfg))
        return;
    if (load_file("/usr/bin/bidhata-menu.conf", cfg))
        return;
    load_defaults(cfg);
}
