#ifndef GB_FONT_H_INCLUDED
#define GB_FONT_H_INCLUDED

#include "types.h"
#include "platform.h"

/* Width and height of one glyph cell at scale 1, including the spacing column
 * that draw_text advances by. */
#define GB_FONT_W 5
#define GB_FONT_H 7
#define GB_FONT_ADVANCE 6

/* Draws ASCII 0x20..0x7E; anything else prints as a space. */
void gb_font_draw(gb_platform_t *platform, int x, int y, const char *text,
                  u32 color, int scale);

/* Pixel width the given string occupies at the given scale. */
int gb_font_width(const char *text, int scale);

#endif
