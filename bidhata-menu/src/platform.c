#include "platform.h"
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

/* Once mmap'd, we never un-mmap until destroy. Like a tattoo, but for pixels. */
static size_t fb_map_size;

int bidhata_platform_init(bidhata_platform_t *platform) {
    memset(platform, 0, sizeof(bidhata_platform_t));
    platform->fb_fd = -1;
    for (int i = 0; i < BIDHATA_MAX_INPUT_DEVICES; i++) platform->input_fds[i] = -1;
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

    /* Wipe the screen -- stale pixels from last boot are like leftover food in the fridge. Gone. */
    memset(platform->fb_mem, 0, fb_map_size);

    /* Open ALL event nodes. R1 scatters keys across GPIO, touchscreen, ADC --
     * only opening event0 would miss Vol Up/Down like missing half the exam questions. */
    platform->input_count = 0;
    for (int i = 0; i < BIDHATA_MAX_INPUT_DEVICES; i++) {
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

    return 0;
}

void bidhata_platform_destroy(bidhata_platform_t *platform) {
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
}

static inline u16 argb_to_rgb565(u32 c) {
    return (u16)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
}

void bidhata_platform_fill_rect(bidhata_platform_t *platform, int x, int y, int w, int h,
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

void bidhata_platform_clear(bidhata_platform_t *platform, u32 color) {
    bidhata_platform_fill_rect(platform, 0, 0, platform->fb_width,
                          platform->fb_height, color);
}

/* Button mapping: R1 has Vol Up, Vol Down, Next Track, Power. No Play/Pause.
 * Next Track = confirm (you have to make do), Power = back. We also accept
 * the usual USB keyboard keys so you can test on your laptop without crying. */
#define KEY_QUEUE_LEN \
    ((int)(sizeof(((bidhata_platform_t *)0)->key_queue) / sizeof(bidhata_key_t)))

static void key_queue_push(bidhata_platform_t *platform, bidhata_key_t key) {
    int next = (platform->key_queue_tail + 1) % KEY_QUEUE_LEN;
    /* Queue full? Drop the newest -- like ignoring your 5th "are we there yet?" */
    if (next == platform->key_queue_head) return;
    platform->key_queue[platform->key_queue_tail] = key;
    platform->key_queue_tail = next;
}

static bidhata_key_t key_queue_pop(bidhata_platform_t *platform) {
    if (platform->key_queue_head == platform->key_queue_tail) return BIDHATA_KEY_NONE;
    bidhata_key_t key = platform->key_queue[platform->key_queue_head];
    platform->key_queue_head = (platform->key_queue_head + 1) % KEY_QUEUE_LEN;
    return key;
}

bidhata_key_t bidhata_platform_poll_menu(bidhata_platform_t *platform, bool *tapped,
                               int *tap_x, int *tap_y) {
    struct input_event ev;
    bidhata_key_t key = BIDHATA_KEY_NONE;

    if (tapped) *tapped = false;

    for (int i = 0; i < platform->input_count; i++) {
        /* Drain buttons + touch together: two readers on same fds would eat each other's events like roommates. */
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
                /* Fire on release -- dragging across rows shouldn't select everything like a wild shopping spree. */
                if (ev.value == 0 && platform->touch_active && tapped) {
                    *tapped = true;
                }
                platform->touch_active = (ev.value != 0);
                continue;
            }

            /* 1 = press, 2 = auto-repeat (hold to scroll -- feels nice), 0 = release (ignored). */
            if (ev.value == 0) continue;

            bidhata_key_t k = BIDHATA_KEY_NONE;
            switch (ev.code) {
                case KEY_UP:
                case KEY_VOLUMEUP:      k = BIDHATA_KEY_UP; break;
                case KEY_DOWN:
                case KEY_VOLUMEDOWN:    k = BIDHATA_KEY_DOWN; break;
                /* Next Track is the confirm button on the R1. */
                case KEY_NEXTSONG:
                case KEY_ENTER:
                case KEY_PLAYPAUSE:
                case KEY_SPACE:         k = BIDHATA_KEY_SELECT; break;
                case KEY_ESC:
                case KEY_POWER:
                case KEY_BACKSPACE:
                case KEY_STOP:          k = BIDHATA_KEY_BACK; break;
                default: break;
            }
            if (k != BIDHATA_KEY_NONE) key_queue_push(platform, k);
        }
    }

    key = key_queue_pop(platform);

    if (tapped && *tapped) {
        if (tap_x) *tap_x = platform->touch_x;
        if (tap_y) *tap_y = platform->touch_y;
    }
    return key;
}

bidhata_key_t bidhata_platform_poll_key(bidhata_platform_t *platform) {
    return bidhata_platform_poll_menu(platform, NULL, NULL, NULL);
}
