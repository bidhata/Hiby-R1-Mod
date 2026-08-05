#include "platform.h"
#include "ppu.h"
#include "apu.h"
#include "types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <errno.h>

#ifdef GB_USE_ALSA
#include <alsa/asoundlib.h>
#endif

static const u32 dmg_palette[4] = {
    0xFF9BBC0F,
    0xFF8BAC0F,
    0xFF306230,
    0xFF0F380F
};

int gb_platform_init(gb_platform_t *platform) {
    memset(platform, 0, sizeof(gb_platform_t));

    platform->fb_fd = open("/dev/fb0", O_RDWR);
    if (platform->fb_fd < 0) {
        perror("Failed to open /dev/fb0");
        return -1;
    }

    struct fb_var_screeninfo vinfo;
    if (ioctl(platform->fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        perror("Failed to get screen info");
        close(platform->fb_fd);
        return -1;
    }

    platform->fb_width = vinfo.xres;
    platform->fb_height = vinfo.yres;
    platform->fb_bpp = vinfo.bits_per_pixel;
    platform->fb_stride = vinfo.xres * (platform->fb_bpp / 8);

    size_t fb_size = platform->fb_stride * platform->fb_height;
    platform->fb_mem = mmap(NULL, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, platform->fb_fd, 0);
    if (platform->fb_mem == MAP_FAILED) {
        perror("Failed to mmap framebuffer");
        close(platform->fb_fd);
        return -1;
    }
    platform->fb_data = (u32 *)platform->fb_mem;

    int scale_x = platform->fb_width / GB_WIDTH;
    int scale_y = platform->fb_height / GB_HEIGHT;
    platform->scale = (scale_x < scale_y) ? scale_x : scale_y;
    if (platform->scale < 1) platform->scale = 1;

    platform->offset_x = (platform->fb_width - GB_WIDTH * platform->scale) / 2;
    platform->offset_y = (platform->fb_height - GB_HEIGHT * platform->scale) / 2;

    platform->input_fd = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
    platform->input_fd2 = open("/dev/input/event1", O_RDONLY | O_NONBLOCK);
    if (platform->input_fd < 0 && platform->input_fd2 < 0) {
        fprintf(stderr, "Warning: No input devices found\n");
    }

    platform->audio_buf_size = 4096;
    platform->audio_buffer = (s16 *)malloc(platform->audio_buf_size * sizeof(s16));
    platform->audio_pos = 0;

#ifdef GB_USE_ALSA
    snd_pcm_t *pcm = NULL;
    int err = snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        err = snd_pcm_open(&pcm, "hw:0,0", SND_PCM_STREAM_PLAYBACK, 0);
    }
    if (err >= 0 && pcm) {
        snd_pcm_set_params(pcm,
            SND_PCM_FORMAT_S16_LE,
            SND_PCM_ACCESS_RW_INTERLEAVED,
            1,
            44100,
            1,
            100000);
        platform->audio_handle = pcm;
    } else {
        fprintf(stderr, "Warning: ALSA init failed: %s\n", snd_strerror(err));
    }
#else
    fprintf(stderr, "Audio disabled (no ALSA)\n");
#endif

    return 0;
}

void gb_platform_destroy(gb_platform_t *platform) {
    if (platform->fb_mem && platform->fb_mem != MAP_FAILED) {
        munmap(platform->fb_mem, platform->fb_stride * platform->fb_height);
    }
    if (platform->fb_fd >= 0) {
        close(platform->fb_fd);
    }
    if (platform->input_fd >= 0) {
        close(platform->input_fd);
    }
    if (platform->input_fd2 >= 0) {
        close(platform->input_fd2);
    }
    if (platform->audio_handle) {
#ifdef GB_USE_ALSA
        snd_pcm_close((snd_pcm_t *)platform->audio_handle);
#endif
    }
    free(platform->audio_buffer);
}

void gb_platform_update_video(gb_platform_t *platform, gb_ppu_t *ppu) {
    if (!platform->fb_data) return;

    for (int y = 0; y < GB_HEIGHT; y++) {
        for (int x = 0; x < GB_WIDTH; x++) {
            u32 color = ppu->framebuffer[y * GB_WIDTH + x];
            u8 index = (color >> 4) & 0x03;
            u32 pixel = dmg_palette[index];

            for (int sy = 0; sy < platform->scale; sy++) {
                for (int sx = 0; sx < platform->scale; sx++) {
                    int fb_x = platform->offset_x + x * platform->scale + sx;
                    int fb_y = platform->offset_y + y * platform->scale + sy;
                    if (fb_x < platform->fb_width && fb_y < platform->fb_height) {
                        int offset = fb_y * (platform->fb_stride / 4) + fb_x;
                        platform->fb_data[offset] = pixel;
                    }
                }
            }
        }
    }
}

void gb_platform_update_audio(gb_platform_t *platform, gb_apu_t *apu) {
#ifdef GB_USE_ALSA
    if (!platform->audio_handle || !apu->enabled) return;

    s16 sample = gb_apu_sample(apu);
    platform->audio_buffer[platform->audio_pos++] = sample;

    if (platform->audio_pos >= platform->audio_buf_size) {
        snd_pcm_sframes_t frames = snd_pcm_writei(
            (snd_pcm_t *)platform->audio_handle,
            platform->audio_buffer,
            platform->audio_pos
        );
        if (frames < 0) {
            frames = snd_pcm_recover((snd_pcm_t *)platform->audio_handle, frames, 0);
        }
        platform->audio_pos = 0;
    }
#else
    (void)platform; (void)apu;
#endif
}

int gb_platform_poll_input(gb_platform_t *platform) {
    struct input_event ev;
    int quit = 0;

    int fds[2] = { platform->input_fd, platform->input_fd2 };

    for (int i = 0; i < 2; i++) {
        if (fds[i] < 0) continue;

        while (read(fds[i], &ev, sizeof(ev)) == sizeof(ev)) {
            if (ev.type == EV_KEY) {
                int pressed = (ev.value != 0);
                switch (ev.code) {
                    case KEY_UP:    platform->button_up = pressed; break;
                    case KEY_DOWN:  platform->button_down = pressed; break;
                    case KEY_LEFT:  platform->button_left = pressed; break;
                    case KEY_RIGHT: platform->button_right = pressed; break;
                    case KEY_ENTER:    platform->button_a = pressed; break;
                    case KEY_BACKSPACE: platform->button_b = pressed; break;
                    case KEY_SPACE: platform->button_start = pressed; break;
                    case KEY_TAB:   platform->button_select = pressed; break;
                    case KEY_ESC:   quit = 1; break;
                }
            }
        }
    }

    return quit;
}

void gb_platform_wait_frame(gb_platform_t *platform) {
    (void)platform;
    usleep(16666);
}
