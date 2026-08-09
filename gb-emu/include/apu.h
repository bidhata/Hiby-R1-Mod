#ifndef GB_APU_H
#define GB_APU_H

#include "types.h"

#define GB_APU_SAMPLE_RATE 44100
/* Ring buffer holding roughly a quarter second of mono samples. */
#define GB_APU_BUFFER_SIZE 16384

/* Square wave channels (NR10-NR14 and NR21-NR24). */
typedef struct {
    bool enabled;
    bool dac_enabled;
    u8 duty;
    u16 freq;
    bool length_enabled;
    int length_counter;

    u8 volume;
    u8 envelope_start;
    u8 envelope_dir;
    u8 envelope_period;
    int envelope_counter;

    int freq_counter;
    int duty_counter;

    /* Sweep unit, channel 1 only. */
    u8 sweep_period;
    u8 sweep_dir;
    u8 sweep_shift;
    int sweep_counter;
    bool sweep_enabled;
    u16 sweep_shadow;
} gb_apu_square_t;

/* Wave channel (NR30-NR34 plus wave RAM at FF30-FF3F). */
typedef struct {
    bool enabled;
    bool dac_enabled;
    bool length_enabled;
    int length_counter;
    u8 volume_shift;
    u16 freq;
    int freq_counter;
    int sample_index;
    u8 sample_buffer;
} gb_apu_wave_t;

/* Noise channel (NR41-NR44). */
typedef struct {
    bool enabled;
    bool dac_enabled;
    bool length_enabled;
    int length_counter;

    u8 volume;
    u8 envelope_start;
    u8 envelope_dir;
    u8 envelope_period;
    int envelope_counter;

    int freq_counter;
    int period;
    u16 lfsr;
    bool width_mode;
} gb_apu_noise_t;

typedef struct {
    gb_apu_square_t ch1;
    gb_apu_square_t ch2;
    gb_apu_wave_t ch3;
    gb_apu_noise_t ch4;

    bool enabled;
    u8 nr50;
    u8 nr51;
    u8 master_volume_l;
    u8 master_volume_r;

    u8 wave_ram[16];

    int frame_seq_counter;
    int frame_seq_step;
    /* Fractional accumulator used to emit samples at GB_APU_SAMPLE_RATE. */
    int sample_counter;

    s16 buffer[GB_APU_BUFFER_SIZE];
    int buffer_read;
    int buffer_write;
} gb_apu_t;

struct gb;

void gb_apu_init(gb_apu_t *apu);
void gb_apu_step(gb_apu_t *apu, struct gb *gb, int cycles);

/* Handles a write to an audio register (0xFF10-0xFF3F, passed as low byte). */
void gb_apu_write_reg(gb_apu_t *apu, u8 reg, u8 val);

/* Returns the value an audio register reads back as. */
u8 gb_apu_read_reg(gb_apu_t *apu, u8 reg, u8 raw);

/* Copies up to max queued samples into out; returns how many were copied. */
int gb_apu_read_samples(gb_apu_t *apu, s16 *out, int max);

#endif
