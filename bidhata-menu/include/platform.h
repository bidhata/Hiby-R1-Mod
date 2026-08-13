#ifndef BIDHATA_PLATFORM_H
#define BIDHATA_PLATFORM_H

#include "types.h"

/* Upper bound on the /dev/input/eventN nodes scanned at startup. */
#define BIDHATA_MAX_INPUT_DEVICES 8

/* One key press at a time, for menu navigation -- each press reported once so
 * a single tap moves the cursor a single row. */
typedef enum {
    BIDHATA_KEY_NONE = 0,
    BIDHATA_KEY_UP,
    BIDHATA_KEY_DOWN,
    BIDHATA_KEY_SELECT,
    BIDHATA_KEY_BACK
} bidhata_key_t;

typedef struct {
    int fb_fd;
    void *fb_mem;
    u32 *fb_data;
    int fb_width;
    int fb_height;
    int fb_bpp;
    int fb_stride;

    /* Every /dev/input/eventN is opened: the R1 spreads its controls over
     * several nodes (GPIO keys, touchscreen, ADC keys, earpod remote) and their
     * numbering follows driver load order, so which one carries the volume keys
     * is not fixed. */
    int input_fds[BIDHATA_MAX_INPUT_DEVICES];
    int input_count;

    /* Touchscreen state, for tap-to-select in the menu. The R1's own
     * physical buttons are just volume up/down, next track, and power, so
     * touch is the only way to reach most menu rows directly. */
    bool touch_active;
    int touch_x;
    int touch_y;

    /* Menu key presses seen while draining the input devices but not yet
     * returned. A poll has to consume everything queued on each descriptor, so
     * without somewhere to park the extras, a second press arriving in the same
     * interval would be thrown away. */
    bidhata_key_t key_queue[16];
    int key_queue_head;
    int key_queue_tail;
} bidhata_platform_t;

int bidhata_platform_init(bidhata_platform_t *platform);
void bidhata_platform_destroy(bidhata_platform_t *platform);

/* Direct framebuffer drawing, used by the menu. Both handle 16 and 32 bpp and
 * clip to the panel. */
void bidhata_platform_fill_rect(bidhata_platform_t *platform, int x, int y, int w, int h,
                           u32 color);
void bidhata_platform_clear(bidhata_platform_t *platform, u32 color);

bidhata_key_t bidhata_platform_poll_key(bidhata_platform_t *platform);

/* Drains buttons and the touch panel in one pass, so the menu can be driven by
 * either. Returns the first key pressed, and sets *tapped once per completed
 * finger-down/finger-up with the release point in *tap_x and *tap_y. Any of the
 * out-parameters may be NULL. */
bidhata_key_t bidhata_platform_poll_menu(bidhata_platform_t *platform, bool *tapped,
                               int *tap_x, int *tap_y);

#endif
