#ifndef GB_PALETTE_H
#define GB_PALETTE_H

#include "types.h"

/* Colour schemes for the four DMG shades. The Game Boy's own screen only ever
 * showed shades of green, so anything else is a preference rather than
 * accuracy; "green" reproduces the original panel. Game Boy Color titles ignore
 * this entirely and use the palettes the cartridge programs. */
typedef enum {
    GB_PALETTE_GREEN = 0,   /* the original DMG panel */
    GB_PALETTE_GREY,
    GB_PALETTE_POCKET,      /* Game Boy Pocket: neutral, slightly warm */
    GB_PALETTE_AMBER,
    GB_PALETTE_COUNT
} gb_palette_id_t;

/* Four ARGB colours, lightest first. */
const u32 *gb_palette_colors(gb_palette_id_t id);

/* Short name for the launcher, e.g. "GREEN". */
const char *gb_palette_name(gb_palette_id_t id);

/* The palette persists across reboots in a small file on the writable data
 * partition, so a choice made in the launcher sticks. */
gb_palette_id_t gb_palette_load(void);
void gb_palette_save(gb_palette_id_t id);

#endif
