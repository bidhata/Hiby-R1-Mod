#ifndef DRAW_H
#define DRAW_H

#include <stdbool.h>
#include <stdint.h>

// What little drawing the app does itself: rectangles, discs, triangles and
// one typeface, into an RGB565 page.
//
// Everything is given in the viewer's coordinates. A film wider than tall is
// shown turned a quarter turn, top along the panel's right edge, and whoever
// watches holds the player sideways: the controls have to be drawn turned the
// same way to read upright. draw_target() says which way round the viewer is,
// and every call below goes through that.

#define RGB565(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

void draw_target(uint16_t *page, int panel_w, int panel_h, bool turned);

// The viewer's width and height: the panel's, swapped when turned.
int draw_width(void);
int draw_height(void);

// A point on the panel, as the viewer sees it. For the touches.
void draw_from_panel(int px, int py, int *vx, int *vy);

void draw_rect(int x, int y, int w, int h, uint16_t color);
// Halves the brightness of what is there: the dark glass behind a control.
void draw_dim_disc(int cx, int cy, int r);
void draw_dim_rect(int x, int y, int w, int h);
void draw_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color);
void draw_line(int x0, int y0, int x1, int y1, int thickness, uint16_t color);

// UTF-8 in, ASCII and Latin-1 drawn; anything else comes out as '?'. `y` is
// the top of the line. No wider than `max_w`: the text is cut, with "..." at
// the cut. Returns the width drawn.
int draw_text(int x, int y, const char *text, uint16_t color, int max_w);
int draw_text_width(const char *text);
int draw_line_height(void);

#endif /* DRAW_H */
