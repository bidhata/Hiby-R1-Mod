#ifndef BIDHATA_PLATFORM_H
#define BIDHATA_PLATFORM_H
/* platform.h -- where raw Linux meets our menu. Framebuffer goes brr, inputs go click. */
#include "types.h"

/* How many /dev/input/eventN nodes we bother opening. 8 is overkill, but R1 is extra. */
#define BIDHATA_MAX_INPUT_DEVICES 8

/* One press = one move. No cheating, no turbo-fire. */
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

    /* Every /dev/input/eventN -- R1 scatters keys across GPIO/touch/ADC/earpods like confetti. */
    int input_fds[BIDHATA_MAX_INPUT_DEVICES];
    int input_count;

    /* Touch goo: tap-to-select needs finger tracking. */
    bool touch_active;
    int touch_x;
    int touch_y;

    /* Tiny key queue: polls drain all fds, we park extras here so second taps don't vanish like lost socks. */
    bidhata_key_t key_queue[16];
    int key_queue_head;
    int key_queue_tail;
} bidhata_platform_t;

int bidhata_platform_init(bidhata_platform_t *platform);
void bidhata_platform_destroy(bidhata_platform_t *platform);

/* Draw on the framebuffer. Handles 16bpp and 32bpp, clips to panel bounds so we don't paint off-screen like a toddler. */
void bidhata_platform_fill_rect(bidhata_platform_t *platform, int x, int y, int w, int h,
                           u32 color);
void bidhata_platform_clear(bidhata_platform_t *platform, u32 color);

bidhata_key_t bidhata_platform_poll_key(bidhata_platform_t *platform);

/* One poll to handle them all: buttons + touch. Reports first key + tap location. NULL-safe. */
bidhata_key_t bidhata_platform_poll_menu(bidhata_platform_t *platform, bool *tapped,
                               int *tap_x, int *tap_y);

#endif
