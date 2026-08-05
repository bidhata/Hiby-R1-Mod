#ifndef GB_PLATFORM_H
#define GB_PLATFORM_H

#include "types.h"
#include "ppu.h"
#include "apu.h"

typedef struct {
    int fb_fd;
    void *fb_mem;
    u32 *fb_data;
    int fb_width;
    int fb_height;
    int fb_bpp;
    int fb_stride;

    int input_fd;
    int input_fd2;

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

#endif
