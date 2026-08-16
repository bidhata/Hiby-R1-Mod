/* Config loader: reads one line per row, "LABEL|COLOR|ACTION|PARAM|TEXT|GROUP".
 * Yes, it's a pipe-delimited CSV's spiritual cousin. We tried JSON, we tried
 * YAML, then we remembered this runs on an Ingenic MIPS SoC and simplicity won.
 * Tries /usr/data/*.conf (writable), then /usr/bin/*.conf (baked-in), then a
 * hardcoded fallback -- the menu can never be empty, like a fridge with backup
 * instant noodles. */
#include "menu_config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>

struct color_name { const char *name; u32 value; };
/* Palette names -- like CSS but make it embedded. Case-insensitive, because users will type "player" not "PLAYER". */
static const struct color_name PALETTE[] = {
    { "PLAYER",    0xFF53C483 },
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
    /* Not a name? Assume 0xRRGGBB hex. Unknown strings get black with full alpha -- the "you typed nonsense" fallback. */
    return (u32)strtoul(s, NULL, 0) | 0xFF000000;
}

static bidhata_action_t parse_action(const char *s)
{
    if (strcmp(s, "run") == 0)           return BIDHATA_ACTION_RUN;
    if (strcmp(s, "exec") == 0)          return BIDHATA_ACTION_EXEC;
    if (strcmp(s, "shutdown") == 0)      return BIDHATA_ACTION_SHUTDOWN;
    if (strcmp(s, "factory_reset") == 0) return BIDHATA_ACTION_FACTORY_RESET;
    if (strcmp(s, "fw_update") == 0)     return BIDHATA_ACTION_FW_UPDATE;
    if (strcmp(s, "submenu") == 0)       return BIDHATA_ACTION_SUBMENU;
    return BIDHATA_ACTION_RUN; /* typo? Treat as RUN. Better wonky than crashy. */
}

/* Split "a|b|c|d|e|f" on '|' in place. Null-terminates each. Returns field count. */
static int split_fields(char *line, char *fields[6])
{
    int n = 0;
    char *p = line;
    fields[n++] = p;
    while (n < 6 && (p = strchr(p, '|')) != NULL)
    {
        *p++ = '\0';
        fields[n++] = p;
    }
    return n;
}

static bool parse_line(char *line, bidhata_menu_item_t *item)
{
    line[strcspn(line, "\r\n")] = '\0';
    if (line[0] == '\0' || line[0] == '#') return false; /* comment/blank -- yeet */

    char *fields[6] = { "", "", "", "", "", "" };
    int n = split_fields(line, fields);
    if (n < 3) return false; /* need at least LABEL|COLOR|ACTION or we can't work */

    memset(item, 0, sizeof(*item));
    snprintf(item->label, sizeof(item->label), "%s", fields[0]);
    item->color = parse_color(fields[1]);
    item->action = parse_action(fields[2]);
    if (n > 3) snprintf(item->param, sizeof(item->param), "%s", fields[3]);
    if (n > 4) snprintf(item->confirm_text, sizeof(item->confirm_text), "%s", fields[4]);
    if (n > 5) snprintf(item->group, sizeof(item->group), "%s", fields[5]);
    return true;
}

/* Fallback: same content as config/bidhata-menu.conf.default but baked into
 * the binary. Parsed via parse_line() so it takes the exact same code path
 * as a file on disk. No secret codepaths, no drift. */
static void load_defaults(bidhata_menu_config_t *cfg)
{
    static const char *const DEFAULT_LINES[] = {
        "HIBY PLAYER|PLAYER|run|player|",
        "UTILITIES|STRIP|submenu|UTILITIES|",
        "DANGER ZONE|DANGER|submenu|DANGER|",
        "SHUTDOWN|SHUTDOWN|shutdown||Power off the device.|UTILITIES",
        "FIRMWARE UPDATE (SD)|FW_UPDATE|fw_update||Reboots into the updater. Needs a .upt file|UTILITIES",
        "ENABLE ADB|FW_UPDATE|exec|adbon|Turns on ADB until reboot. USB stops acting as a card reader while it is on.|UTILITIES",
        "FACTORY RESET|DANGER|factory_reset||Erases ALL data on the device. Cannot be undone.|DANGER",
        "COMPRESS FILE ART|STRIP|exec|compress_art_all.sh|Shrinks oversized embedded art on every FLAC/MP3 on SD to fit the screen.|UTILITIES",
        "COMPRESS ALBUM ART|STRIP|exec|compress_folder_art.sh -f|Shrinks oversized folder.jpg/cover.png etc. on SD to fit the screen.|UTILITIES",
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
