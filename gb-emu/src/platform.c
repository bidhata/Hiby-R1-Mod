#include "platform.h"
#include "ppu.h"
#include "apu.h"
#include "types.h"
#include "font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <errno.h>

#ifdef GB_USE_ALSA
#include <alsa/asoundlib.h>
#endif

#define FRAME_NS 16742706L /* 59.7275 Hz, the DMG refresh rate */
#define AUDIO_CHUNK 2048

static size_t fb_map_size;
static struct timespec next_frame;
static bool pacing_started;

int gb_platform_init(gb_platform_t *platform) {
    memset(platform, 0, sizeof(gb_platform_t));
    platform->fb_fd = -1;
    for (int i = 0; i < GB_MAX_INPUT_DEVICES; i++) platform->input_fds[i] = -1;
    platform->input_count = 0;

    platform->fb_fd = open("/dev/fb0", O_RDWR);
    if (platform->fb_fd < 0) {
        perror("Failed to open /dev/fb0");
        return -1;
    }

    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    if (ioctl(platform->fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0 ||
        ioctl(platform->fb_fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        perror("Failed to get screen info");
        close(platform->fb_fd);
        platform->fb_fd = -1;
        return -1;
    }

    platform->fb_width = vinfo.xres;
    platform->fb_height = vinfo.yres;
    platform->fb_bpp = vinfo.bits_per_pixel;
    platform->fb_stride = finfo.line_length;
    if (platform->fb_stride <= 0) {
        platform->fb_stride = platform->fb_width * (platform->fb_bpp / 8);
    }

    if (platform->fb_bpp != 16 && platform->fb_bpp != 32) {
        fprintf(stderr, "Unsupported framebuffer depth: %d bpp\n", platform->fb_bpp);
        close(platform->fb_fd);
        platform->fb_fd = -1;
        return -1;
    }

    fb_map_size = finfo.smem_len;
    if (fb_map_size == 0) {
        fb_map_size = (size_t)platform->fb_stride * platform->fb_height;
    }

    platform->fb_mem = mmap(NULL, fb_map_size, PROT_READ | PROT_WRITE,
                            MAP_SHARED, platform->fb_fd, 0);
    if (platform->fb_mem == MAP_FAILED) {
        perror("Failed to mmap framebuffer");
        platform->fb_mem = NULL;
        close(platform->fb_fd);
        platform->fb_fd = -1;
        return -1;
    }
    platform->fb_data = (u32 *)platform->fb_mem;

    int scale_x = platform->fb_width / GB_WIDTH;
    int scale_y = platform->fb_height / GB_HEIGHT;
    platform->scale = (scale_x < scale_y) ? scale_x : scale_y;
    if (platform->scale < 1) platform->scale = 1;

    platform->offset_x = (platform->fb_width - GB_WIDTH * platform->scale) / 2;

    /* Centre horizontally, but sit against the top edge rather than the middle:
     * the lower part of a tall panel belongs to the on-screen pad, and a
     * centred picture would cover it. On a wide screen where the picture
     * already fills the height this is the same as centring. */
    int used = GB_HEIGHT * platform->scale;
    int spare = platform->fb_height - used;
    /* The pad needs roughly the bottom 45% of a portrait panel. */
    platform->offset_y = (spare > platform->fb_height / 2) ? spare / 4 : 0;

    if (platform->offset_x < 0) platform->offset_x = 0;
    if (platform->offset_y < 0) platform->offset_y = 0;

    /* Clear the screen once so the borders are not left with stale content. */
    memset(platform->fb_mem, 0, fb_map_size);

    /* Open every event node. On the R1 the buttons are split across drivers
     * loaded in sequence (GPIO keys, then the touchscreen, then the ADC keys
     * carrying volume up/down), so listening to only the first two would miss
     * the volume keys entirely. */
    platform->input_count = 0;
    for (int i = 0; i < GB_MAX_INPUT_DEVICES; i++) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            platform->input_fds[platform->input_count++] = fd;
        }
    }
    if (platform->input_count == 0) {
        fprintf(stderr, "Warning: No input devices found\n");
    }

    platform->audio_buf_size = AUDIO_CHUNK;
    platform->audio_buffer = (s16 *)malloc((size_t)platform->audio_buf_size * sizeof(s16));
    if (!platform->audio_buffer) {
        fprintf(stderr, "Warning: audio buffer allocation failed, sound disabled\n");
        platform->audio_buf_size = 0;
    }
    platform->audio_pos = 0;

#ifdef GB_USE_ALSA
    snd_pcm_t *pcm = NULL;
    int err = snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        err = snd_pcm_open(&pcm, "hw:0,0", SND_PCM_STREAM_PLAYBACK, 0);
    }
    if (err >= 0 && pcm) {
        err = snd_pcm_set_params(pcm,
            SND_PCM_FORMAT_S16_LE,
            SND_PCM_ACCESS_RW_INTERLEAVED,
            1,                     /* mono */
            GB_APU_SAMPLE_RATE,
            1,                     /* allow resampling */
            50000);                /* 50 ms latency */
        if (err < 0) {
            fprintf(stderr, "Warning: ALSA setup failed: %s\n", snd_strerror(err));
            snd_pcm_close(pcm);
        } else {
            platform->audio_handle = pcm;
        }
    } else {
        fprintf(stderr, "Warning: ALSA init failed: %s\n", snd_strerror(err));
    }
#else
    fprintf(stderr, "Audio disabled (no ALSA)\n");
#endif

    pacing_started = false;
    return 0;
}

void gb_platform_destroy(gb_platform_t *platform) {
    if (platform->fb_mem && platform->fb_mem != MAP_FAILED) {
        munmap(platform->fb_mem, fb_map_size);
        platform->fb_mem = NULL;
        platform->fb_data = NULL;
    }
    if (platform->fb_fd >= 0) {
        close(platform->fb_fd);
        platform->fb_fd = -1;
    }
    for (int i = 0; i < platform->input_count; i++) {
        if (platform->input_fds[i] >= 0) close(platform->input_fds[i]);
        platform->input_fds[i] = -1;
    }
    platform->input_count = 0;
    if (platform->audio_handle) {
#ifdef GB_USE_ALSA
        snd_pcm_drain((snd_pcm_t *)platform->audio_handle);
        snd_pcm_close((snd_pcm_t *)platform->audio_handle);
#endif
        platform->audio_handle = NULL;
    }
    free(platform->audio_buffer);
    platform->audio_buffer = NULL;
}

static inline u16 argb_to_rgb565(u32 c) {
    return (u16)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
}

void gb_platform_update_video(gb_platform_t *platform, gb_ppu_t *ppu) {
    if (!platform->fb_mem) return;

    const int scale = platform->scale;
    u8 *base = (u8 *)platform->fb_mem;

    for (int y = 0; y < GB_HEIGHT; y++) {
        for (int sy = 0; sy < scale; sy++) {
            int fb_y = platform->offset_y + y * scale + sy;
            if (fb_y < 0 || fb_y >= platform->fb_height) continue;

            u8 *line = base + (size_t)fb_y * platform->fb_stride;

            for (int x = 0; x < GB_WIDTH; x++) {
                u32 color = ppu->framebuffer[y * GB_WIDTH + x];
                int fb_x = platform->offset_x + x * scale;

                for (int sx = 0; sx < scale; sx++, fb_x++) {
                    if (fb_x < 0 || fb_x >= platform->fb_width) continue;
                    if (platform->fb_bpp == 32) {
                        *((u32 *)(line + (size_t)fb_x * 4)) = color;
                    } else {
                        *((u16 *)(line + (size_t)fb_x * 2)) = argb_to_rgb565(color);
                    }
                }
            }
        }
    }
}

void gb_platform_update_audio(gb_platform_t *platform, gb_apu_t *apu) {
#ifdef GB_USE_ALSA
    if (!platform->audio_handle || !platform->audio_buffer) {
        /* Keep the queue from filling up when playback is unavailable. */
        s16 discard[256];
        while (gb_apu_read_samples(apu, discard, 256) > 0) { }
        return;
    }

    snd_pcm_t *pcm = (snd_pcm_t *)platform->audio_handle;
    int count;
    while ((count = gb_apu_read_samples(apu, platform->audio_buffer,
                                       platform->audio_buf_size)) > 0) {
        s16 *cursor = platform->audio_buffer;
        int remaining = count;
        while (remaining > 0) {
            snd_pcm_sframes_t frames = snd_pcm_writei(pcm, cursor, remaining);
            if (frames < 0) {
                if (snd_pcm_recover(pcm, (int)frames, 1) < 0) return;
                continue;
            }
            cursor += frames;
            remaining -= (int)frames;
        }
        if (count < platform->audio_buf_size) break;
    }
#else
    (void)platform;
    /* No output device: drain the queue so the APU never stalls on a full buffer. */
    s16 discard[256];
    while (gb_apu_read_samples(apu, discard, 256) > 0) { }
#endif
}

void gb_platform_fill_rect(gb_platform_t *platform, int x, int y, int w, int h,
                           u32 color) {
    if (!platform->fb_mem) return;

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > platform->fb_width)  w = platform->fb_width - x;
    if (y + h > platform->fb_height) h = platform->fb_height - y;
    if (w <= 0 || h <= 0) return;

    u16 color565 = argb_to_rgb565(color);
    u8 *base = (u8 *)platform->fb_mem;

    for (int j = 0; j < h; j++) {
        u8 *line = base + (size_t)(y + j) * platform->fb_stride;
        if (platform->fb_bpp == 32) {
            u32 *px = (u32 *)(line + (size_t)x * 4);
            for (int i = 0; i < w; i++) px[i] = color;
        } else {
            u16 *px = (u16 *)(line + (size_t)x * 2);
            for (int i = 0; i < w; i++) px[i] = color565;
        }
    }
}

void gb_platform_clear(gb_platform_t *platform, u32 color) {
    gb_platform_fill_rect(platform, 0, 0, platform->fb_width,
                          platform->fb_height, color);
}

/* Menu navigation for a player with three buttons and no Play key.
 *
 * The R1 has Volume Up, Volume Down, Next Track and Power - the drivers report
 * KEY_VOLUMEUP, KEY_VOLUMEDOWN, KEY_NEXTSONG and KEY_POWER (see
 * module_driver/keyboard_*.sh). There is no KEY_PLAYPAUSE on this hardware, so
 * Next Track confirms and Power backs out. The other codes are accepted too so
 * the same build stays usable on a desk with a USB keyboard.
 */
#define KEY_QUEUE_LEN \
    ((int)(sizeof(((gb_platform_t *)0)->key_queue) / sizeof(gb_key_t)))

static void key_queue_push(gb_platform_t *platform, gb_key_t key) {
    int next = (platform->key_queue_tail + 1) % KEY_QUEUE_LEN;
    /* Full queue: drop the newest rather than overwrite one not yet handled.
     * Only reachable if a poll is skipped for a long time. */
    if (next == platform->key_queue_head) return;
    platform->key_queue[platform->key_queue_tail] = key;
    platform->key_queue_tail = next;
}

static gb_key_t key_queue_pop(gb_platform_t *platform) {
    if (platform->key_queue_head == platform->key_queue_tail) return GB_KEY_NONE;
    gb_key_t key = platform->key_queue[platform->key_queue_head];
    platform->key_queue_head = (platform->key_queue_head + 1) % KEY_QUEUE_LEN;
    return key;
}

gb_key_t gb_platform_poll_menu(gb_platform_t *platform, bool *tapped,
                               int *tap_x, int *tap_y) {
    struct input_event ev;
    gb_key_t key = GB_KEY_NONE;

    if (tapped) *tapped = false;

    /* Buttons and the panel are drained together: two separate readers on the
     * same descriptors would each consume events the other needed. */
    for (int i = 0; i < platform->input_count; i++) {
        int fd = platform->input_fds[i];
        if (fd < 0) continue;

        while (read(fd, &ev, sizeof(ev)) == sizeof(ev)) {
            if (ev.type == EV_ABS) {
                switch (ev.code) {
                    case ABS_X:
                    case ABS_MT_POSITION_X: platform->touch_x = ev.value; break;
                    case ABS_Y:
                    case ABS_MT_POSITION_Y: platform->touch_y = ev.value; break;
                    case ABS_MT_TRACKING_ID:
                        if (ev.value < 0 && platform->touch_active && tapped) {
                            *tapped = true;
                        }
                        platform->touch_active = (ev.value >= 0);
                        break;
                    default: break;
                }
                continue;
            }

            if (ev.type != EV_KEY) continue;

            if (ev.code == BTN_TOUCH) {
                /* Acting on release keeps a drag across the screen from firing
                 * every row it passes over. */
                if (ev.value == 0 && platform->touch_active && tapped) {
                    *tapped = true;
                }
                platform->touch_active = (ev.value != 0);
                continue;
            }

            /* Value 1 is a fresh press; 2 is auto-repeat, which is welcome here
             * so holding the key keeps scrolling. Releases are ignored. */
            if (ev.value == 0) continue;

            gb_key_t k = GB_KEY_NONE;
            switch (ev.code) {
                case KEY_UP:
                case KEY_VOLUMEUP:      k = GB_KEY_UP; break;
                case KEY_DOWN:
                case KEY_VOLUMEDOWN:    k = GB_KEY_DOWN; break;
                /* Next Track is the confirm button on the R1. */
                case KEY_NEXTSONG:
                case KEY_ENTER:
                case KEY_PLAYPAUSE:
                case KEY_SPACE:         k = GB_KEY_SELECT; break;
                case KEY_ESC:
                case KEY_POWER:
                case KEY_BACKSPACE:
                case KEY_STOP:          k = GB_KEY_BACK; break;
                default: break;
            }
            if (k != GB_KEY_NONE) key_queue_push(platform, k);
        }
    }

    key = key_queue_pop(platform);

    if (tapped && *tapped) {
        if (tap_x) *tap_x = platform->touch_x;
        if (tap_y) *tap_y = platform->touch_y;
    }
    return key;
}

gb_key_t gb_platform_poll_key(gb_platform_t *platform) {
    return gb_platform_poll_menu(platform, NULL, NULL, NULL);
}

/* Screen regions making up the on-screen pad, as fractions of the panel.
 * The emulated screen is centred in the upper part, so the controls live in the
 * band below it: a D-pad on the left, A/B on the right, Start/Select along the
 * bottom. gb_platform_draw_touch_overlay paints these same rectangles. */
typedef struct {
    float x0, y0, x1, y1;
    int button;             /* index into the button table below */
} touch_zone_t;

enum {
    TB_UP, TB_DOWN, TB_LEFT, TB_RIGHT,
    TB_A, TB_B, TB_START, TB_SELECT, TB_COUNT
};

static const touch_zone_t touch_zones[] = {
    /* D-pad: a plus shape in the lower left. */
    { 0.11f, 0.62f, 0.27f, 0.72f, TB_UP    },
    { 0.11f, 0.82f, 0.27f, 0.92f, TB_DOWN  },
    { 0.02f, 0.72f, 0.12f, 0.82f, TB_LEFT  },
    { 0.26f, 0.72f, 0.36f, 0.82f, TB_RIGHT },
    /* A and B, lower right, A above and right of B as on the console. */
    { 0.76f, 0.64f, 0.96f, 0.76f, TB_A     },
    { 0.56f, 0.76f, 0.76f, 0.88f, TB_B     },
    /* Start and Select across the bottom. */
    { 0.30f, 0.93f, 0.50f, 1.00f, TB_SELECT },
    { 0.52f, 0.93f, 0.72f, 1.00f, TB_START  },
};

#define TOUCH_ZONE_COUNT ((int)(sizeof(touch_zones) / sizeof(touch_zones[0])))

/* Recomputes which pad buttons the finger is currently over. */
static void update_touch_held(gb_platform_t *platform) {
    for (int b = 0; b < TB_COUNT; b++) platform->touch_held[b] = false;

    if (!platform->touch_active) return;

    for (int i = 0; i < TOUCH_ZONE_COUNT; i++) {
        const touch_zone_t *z = &touch_zones[i];
        int x0 = (int)(z->x0 * platform->fb_width);
        int x1 = (int)(z->x1 * platform->fb_width);
        int y0 = (int)(z->y0 * platform->fb_height);
        int y1 = (int)(z->y1 * platform->fb_height);

        if (platform->touch_x >= x0 && platform->touch_x < x1 &&
            platform->touch_y >= y0 && platform->touch_y < y1) {
            platform->touch_held[z->button] = true;
        }
    }
}

/* A button is down when either input source says so. */
static void merge_buttons(gb_platform_t *platform) {
    bool down[TB_COUNT];
    for (int b = 0; b < TB_COUNT; b++) {
        down[b] = platform->key_held[b] || platform->touch_held[b];
    }

    platform->button_up     = down[TB_UP];
    platform->button_down   = down[TB_DOWN];
    platform->button_left   = down[TB_LEFT];
    platform->button_right  = down[TB_RIGHT];
    platform->button_a      = down[TB_A];
    platform->button_b      = down[TB_B];
    platform->button_start  = down[TB_START];
    platform->button_select = down[TB_SELECT];
}

/* Labels drawn on each pad key, indexed by the TB_* button ids. */
static const char *touch_labels[TB_COUNT] = {
    [TB_UP] = "^", [TB_DOWN] = "v", [TB_LEFT] = "<", [TB_RIGHT] = ">",
    [TB_A] = "A", [TB_B] = "B", [TB_START] = "START", [TB_SELECT] = "SELECT"
};

void gb_platform_draw_touch_overlay(gb_platform_t *platform) {
    if (!platform->fb_mem) return;

    /* Muted greys: the pad should be readable without competing with the game. */
    const u32 face  = 0xFF2A2A38;
    const u32 edge  = 0xFF4A4A60;
    const u32 text  = 0xFFBFBFD0;

    for (int i = 0; i < TOUCH_ZONE_COUNT; i++) {
        const touch_zone_t *z = &touch_zones[i];
        int x0 = (int)(z->x0 * platform->fb_width);
        int x1 = (int)(z->x1 * platform->fb_width);
        int y0 = (int)(z->y0 * platform->fb_height);
        int y1 = (int)(z->y1 * platform->fb_height);
        int w = x1 - x0, h = y1 - y0;

        gb_platform_fill_rect(platform, x0, y0, w, h, edge);
        gb_platform_fill_rect(platform, x0 + 2, y0 + 2, w - 4, h - 4, face);

        const char *label = touch_labels[z->button];
        if (!label) continue;

        /* Shrink the label until it fits, so START and SELECT stay inside
         * their keys on a narrow panel. */
        int scale = 3;
        while (scale > 1 && gb_font_width(label, scale) > w - 8) scale--;

        gb_font_draw(platform,
                     x0 + (w - gb_font_width(label, scale)) / 2,
                     y0 + (h - GB_FONT_H * scale) / 2,
                     label, text, scale);
    }
}

int gb_platform_poll_input(gb_platform_t *platform) {
    struct input_event ev;
    int quit = 0;
    bool touch_changed = false;

    for (int i = 0; i < platform->input_count; i++) {
        int fd = platform->input_fds[i];
        if (fd < 0) continue;

        while (read(fd, &ev, sizeof(ev)) == sizeof(ev)) {
            if (ev.type == EV_ABS) {
                /* The panel is single-touch (cst_max_touch_number=1), and
                 * reports either the plain or the MT flavour of each axis. */
                switch (ev.code) {
                    case ABS_X:
                    case ABS_MT_POSITION_X:
                        platform->touch_x = ev.value;
                        touch_changed = true;
                        break;
                    case ABS_Y:
                    case ABS_MT_POSITION_Y:
                        platform->touch_y = ev.value;
                        touch_changed = true;
                        break;
                    case ABS_MT_TRACKING_ID:
                        /* -1 marks the finger leaving the panel. */
                        platform->touch_active = (ev.value >= 0);
                        touch_changed = true;
                        break;
                    default: break;
                }
                continue;
            }

            if (ev.type != EV_KEY) continue;
            /* Value 2 is auto-repeat, which counts as still held. */
            int pressed = (ev.value != 0);

            if (ev.code == BTN_TOUCH) {
                platform->touch_active = pressed;
                touch_changed = true;
                continue;
            }

            /* The R1 itself only has the volume pair, next track and power.
             * Next Track doubles as A so simple games are playable without
             * touching the screen; everything else comes from the panel. The
             * remaining codes keep a USB keyboard usable for desk testing. */
            switch (ev.code) {
                case KEY_UP:
                case KEY_VOLUMEUP:      platform->key_held[TB_UP] = pressed; break;
                case KEY_DOWN:
                case KEY_VOLUMEDOWN:    platform->key_held[TB_DOWN] = pressed; break;
                case KEY_LEFT:
                case KEY_PREVIOUSSONG:  platform->key_held[TB_LEFT] = pressed; break;
                case KEY_RIGHT:         platform->key_held[TB_RIGHT] = pressed; break;
                case KEY_NEXTSONG:
                case KEY_ENTER:
                case KEY_PLAYPAUSE:     platform->key_held[TB_A] = pressed; break;
                case KEY_BACKSPACE:
                case KEY_STOP:          platform->key_held[TB_B] = pressed; break;
                case KEY_SPACE:         platform->key_held[TB_START] = pressed; break;
                case KEY_TAB:           platform->key_held[TB_SELECT] = pressed; break;
                case KEY_ESC:
                case KEY_POWER:         if (pressed) quit = 1; break;
                default: break;
            }
        }
    }

    if (touch_changed) update_touch_held(platform);
    merge_buttons(platform);

    return quit;
}

void gb_platform_wait_frame(gb_platform_t *platform) {
    (void)platform;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    if (!pacing_started) {
        next_frame = now;
        pacing_started = true;
    }

    next_frame.tv_nsec += FRAME_NS;
    while (next_frame.tv_nsec >= 1000000000L) {
        next_frame.tv_nsec -= 1000000000L;
        next_frame.tv_sec++;
    }

    long delta_ns = (next_frame.tv_sec - now.tv_sec) * 1000000000L +
                    (next_frame.tv_nsec - now.tv_nsec);

    if (delta_ns <= 0) {
        /* Running behind: resync so the deficit does not accumulate. */
        if (delta_ns < -FRAME_NS * 4) next_frame = now;
        return;
    }

    struct timespec sleep_time = { delta_ns / 1000000000L, delta_ns % 1000000000L };
    while (nanosleep(&sleep_time, &sleep_time) == -1 && errno == EINTR) { }
}
