#ifndef GB_PLATFORM_H
#define GB_PLATFORM_H

#include "types.h"
#include "ppu.h"
#include "apu.h"

/* Upper bound on the /dev/input/eventN nodes scanned at startup. */
#define GB_MAX_INPUT_DEVICES 8

/* One key press at a time, for menu navigation. Unlike gb_platform_poll_input,
 * which tracks buttons as held or released, these report each press once so a
 * single tap moves the cursor a single row. */
typedef enum {
    GB_KEY_NONE = 0,
    GB_KEY_UP,
    GB_KEY_DOWN,
    GB_KEY_SELECT,
    GB_KEY_BACK
} gb_key_t;

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
    int input_fds[GB_MAX_INPUT_DEVICES];
    int input_count;

    int scale;
    int offset_x;
    int offset_y;

    bool button_up;
    bool button_down;
    bool button_left;
    bool button_right;
    bool button_a;
    bool button_b;
    bool button_start;
    bool button_select;

    /* Touchscreen state. The panel is the only way to reach a full Game Boy
     * pad: the R1 itself has just volume up/down, next track and power.
     *
     * The physical keys and the on-screen pad are tracked separately and then
     * combined into the button_* fields above, so releasing a finger cannot
     * clear a key the user is still physically holding, and vice versa. */
    bool touch_active;
    int touch_x;
    int touch_y;
    bool key_held[8];
    bool touch_held[8];

    /* Menu key presses seen while draining the input devices but not yet
     * returned. A poll has to consume everything queued on each descriptor, so
     * without somewhere to park the extras, a second press arriving in the same
     * interval would be thrown away. */
    gb_key_t key_queue[16];
    int key_queue_head;
    int key_queue_tail;

    void *audio_handle;
    void *audio_params;
    s16 *audio_buffer;
    int audio_buf_size;
    int audio_pos;
} gb_platform_t;

int gb_platform_init(gb_platform_t *platform);
void gb_platform_destroy(gb_platform_t *platform);
void gb_platform_update_video(gb_platform_t *platform, gb_ppu_t *ppu);
void gb_platform_update_audio(gb_platform_t *platform, gb_apu_t *apu);
int gb_platform_poll_input(gb_platform_t *platform);
void gb_platform_wait_frame(gb_platform_t *platform);

/* Direct framebuffer drawing, used by the menu. Both handle 16 and 32 bpp and
 * clip to the panel. */
void gb_platform_fill_rect(gb_platform_t *platform, int x, int y, int w, int h,
                           u32 color);
void gb_platform_clear(gb_platform_t *platform, u32 color);

gb_key_t gb_platform_poll_key(gb_platform_t *platform);

/* Drains buttons and the touch panel in one pass, so the menu can be driven by
 * either. Returns the first key pressed, and sets *tapped once per completed
 * finger-down/finger-up with the release point in *tap_x and *tap_y. Any of the
 * out-parameters may be NULL. */
gb_key_t gb_platform_poll_menu(gb_platform_t *platform, bool *tapped,
                               int *tap_x, int *tap_y);

/* Draws the on-screen pad below the emulated screen. Called once after the
 * framebuffer is set up; the emulator only repaints the screen area, so the
 * pad stays put. Pressed keys are not highlighted, which keeps this off the
 * per-frame path entirely. */
void gb_platform_draw_touch_overlay(gb_platform_t *platform);

#endif
