#include "draw.h"

#include <stdlib.h>

#include "font_data.h"

static uint16_t *page;
static int pw, ph;
static bool turned;

void draw_target(uint16_t *target, int panel_w, int panel_h, bool is_turned) {
	page = target;
	pw = panel_w;
	ph = panel_h;
	turned = is_turned;
}

int draw_width(void) { return turned ? ph : pw; }
int draw_height(void) { return turned ? pw : ph; }

// The viewer's right is down the panel and the viewer's down is the panel's
// left, as the picture itself is laid (convert_turned in video.c).
static inline uint16_t *at(int vx, int vy) {
	int x = turned ? pw - 1 - vy : vx;
	int y = turned ? vx : vy;
	if (x < 0 || y < 0 || x >= pw || y >= ph) {
		return NULL;
	}
	return page + (size_t)y * pw + x;
}

void draw_from_panel(int px, int py, int *vx, int *vy) {
	*vx = turned ? py : px;
	*vy = turned ? pw - 1 - px : py;
}

void draw_rect(int x, int y, int w, int h, uint16_t color) {
	for (int j = y; j < y + h; j++) {
		for (int i = x; i < x + w; i++) {
			uint16_t *p = at(i, j);
			if (p) {
				*p = color;
			}
		}
	}
}

static inline void dim(uint16_t *p) { *p = (uint16_t)((*p >> 1) & 0x7BEF); }

void draw_dim_rect(int x, int y, int w, int h) {
	for (int j = y; j < y + h; j++) {
		for (int i = x; i < x + w; i++) {
			uint16_t *p = at(i, j);
			if (p) {
				dim(p);
			}
		}
	}
}

void draw_dim_disc(int cx, int cy, int r) {
	for (int j = -r; j <= r; j++) {
		for (int i = -r; i <= r; i++) {
			if (i * i + j * j <= r * r) {
				uint16_t *p = at(cx + i, cy + j);
				if (p) {
					dim(p);
				}
			}
		}
	}
}

static int edge(int ax, int ay, int bx, int by, int px, int py) { return (bx - ax) * (py - ay) - (by - ay) * (px - ax); }

void draw_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color) {
	int min_x = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
	int max_x = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
	int min_y = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
	int max_y = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
	for (int y = min_y; y <= max_y; y++) {
		for (int x = min_x; x <= max_x; x++) {
			int a = edge(x0, y0, x1, y1, x, y), b = edge(x1, y1, x2, y2, x, y), c = edge(x2, y2, x0, y0, x, y);
			if ((a >= 0 && b >= 0 && c >= 0) || (a <= 0 && b <= 0 && c <= 0)) {
				uint16_t *p = at(x, y);
				if (p) {
					*p = color;
				}
			}
		}
	}
}

void draw_line(int x0, int y0, int x1, int y1, int thickness, uint16_t color) {
	int steps = abs(x1 - x0) > abs(y1 - y0) ? abs(x1 - x0) : abs(y1 - y0);
	if (steps == 0) {
		steps = 1;
	}
	for (int s = 0; s <= steps; s++) {
		int x = x0 + (x1 - x0) * s / steps, y = y0 + (y1 - y0) * s / steps;
		draw_rect(x - thickness / 2, y - thickness / 2, thickness, thickness, color);
	}
}

// ---------------------------------------------------------------------------
// text
// ---------------------------------------------------------------------------

static const font_glyph_t *glyph_for(unsigned code) {
	// The table is in code order with one gap, between 126 and 160.
	if (code >= 32 && code <= 126) {
		return &font_glyphs[code - 32];
	}
	if (code >= 160 && code <= 255) {
		return &font_glyphs[95 + code - 160];
	}
	return &font_glyphs['?' - 32];
}

// One character of UTF-8. A byte that does not start a character counts as
// one of its own, so a name in some other encoding still comes out, as '?'.
static unsigned next_code(const char **text) {
	const unsigned char *s = (const unsigned char *)*text;
	unsigned c = *s++;
	int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
	if (more) {
		c &= 0x3F >> more;
		while (more-- && (*s & 0xC0) == 0x80) {
			c = (c << 6) | (*s++ & 0x3F);
		}
	}
	*text = (const char *)s;
	return c;
}

int draw_line_height(void) { return FONT_LINE_HEIGHT; }

int draw_text_width(const char *text) {
	int w = 0;
	while (*text) {
		w += glyph_for(next_code(&text))->advance;
	}
	return w;
}

static void draw_glyph(int x, int y, const font_glyph_t *g, uint16_t color) {
	const uint8_t *cov = font_coverage + g->offset;
	int r = color >> 11, gr = (color >> 5) & 0x3F, b = color & 0x1F;
	for (int j = 0; j < g->h; j++) {
		for (int i = 0; i < g->w; i++) {
			int a = cov[j * g->w + i];
			if (a < 16) {
				continue;
			}
			uint16_t *p = at(x + g->left + i, y + g->top + j);
			if (!p) {
				continue;
			}
			if (a > 239) {
				*p = color;
				continue;
			}
			int dr = *p >> 11, dg = (*p >> 5) & 0x3F, db = *p & 0x1F;
			*p = (uint16_t)(((dr + (r - dr) * a / 255) << 11) | ((dg + (gr - dg) * a / 255) << 5) | (db + (b - db) * a / 255));
		}
	}
}

int draw_text(int x, int y, const char *text, uint16_t color, int max_w) {
	static const char dots[] = "...";
	int dots_w = draw_text_width(dots);
	bool fits = draw_text_width(text) <= max_w;
	int w = 0;
	while (*text) {
		const font_glyph_t *g = glyph_for(next_code(&text));
		if (!fits && w + g->advance + dots_w > max_w) {
			for (const char *d = dots; *d; d++) {
				const font_glyph_t *dg = glyph_for((unsigned)*d);
				draw_glyph(x + w, y, dg, color);
				w += dg->advance;
			}
			break;
		}
		draw_glyph(x + w, y, g, color);
		w += g->advance;
	}
	return w;
}
