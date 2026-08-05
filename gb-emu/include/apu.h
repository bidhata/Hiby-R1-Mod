#ifndef GB_APU_H
#define GB_APU_H

#include "types.h"

typedef struct {
    bool enabled;
    u8 volume;
    u8 duty;
    u16 freq;
    u8 length;
    bool length_enabled;
    u8 envelope_vol;
    u8 envelope_dir;
    u8 envelope_period;
    int envelope_counter;
    int freq_counter;
    int length_counter;
    int duty_counter;
} gb_apu_channel_t;

typedef struct {
    gb_apu_channel_t ch1;
    gb_apu_channel_t ch2;
    bool enabled;
    u8 nr50;
    u8 nr51;
    u8 master_volume_l;
    u8 master_volume_r;
} gb_apu_t;

struct gb;

void gb_apu_init(gb_apu_t *apu);
void gb_apu_step(gb_apu_t *apu, struct gb *gb, int cycles);
s16 gb_apu_sample(gb_apu_t *apu);

#endif
