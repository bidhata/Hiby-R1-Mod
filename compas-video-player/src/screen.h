#ifndef SCREEN_H
#define SCREEN_H

#include <stdbool.h>
#include <stdint.h>

// The panel: /dev/fb0 in RGB565, two pages, swapped by panning. A frame is
// drawn into the page that is off screen and then shown whole.

bool screen_open(void);
void screen_close(void);

int screen_width(void);
int screen_height(void);

// The page that is not on screen, to draw into. With a framebuffer that has
// only one page it is the one on screen, and drawing shows as it happens.
uint16_t *screen_back(void);

// Shows the page just drawn; screen_back() is then the other one.
void screen_flip(void);

// The same picture on both pages: copies the one on screen into the other.
// For something drawn once and left up, so that a later flip does not bring
// back whatever the other page held.
void screen_sync(void);

// The panel's light and scan-out, off and on.
void screen_blank(bool blank);

// Puts the backlight at the brightness the user has set in CompasOS, if it is
// below that. For calling when a film opens and every few seconds while one
// plays: see screen.c for why it has to be the app that does it.
void screen_hold_brightness(void);

#endif /* SCREEN_H */
