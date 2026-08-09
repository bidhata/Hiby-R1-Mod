#include "gb.h"
#include "apu.h"
#include <string.h>

/* Duty patterns, MSB first: 12.5%, 25%, 50%, 75%. */
static const u8 duty_table[4] = { 0x01, 0x81, 0x87, 0x7E };

/* Noise channel divisors selected by the low 3 bits of NR43. */
static const int noise_divisors[8] = { 8, 16, 32, 48, 64, 80, 96, 112 };

#define FRAME_SEQ_CYCLES 8192 /* 512 Hz frame sequencer */

void gb_apu_init(gb_apu_t *apu) {
    memset(apu, 0, sizeof(*apu));
    apu->enabled = true;
    apu->nr50 = 0x77;
    apu->nr51 = 0xF3;
    apu->master_volume_l = 7;
    apu->master_volume_r = 7;

    apu->ch1.duty = 2;
    apu->ch2.duty = 2;
    apu->ch3.freq_counter = 1;
    apu->ch4.lfsr = 0x7FFF;
    apu->ch4.period = noise_divisors[0];
}

static void square_trigger(gb_apu_square_t *ch, int max_length, bool sweep_channel);

/* ---------- Register writes ---------- */

static void square_write(gb_apu_square_t *ch, u8 base_reg, u8 reg, u8 val,
                         bool sweep_channel) {
    switch (reg - base_reg) {
    case 0: /* NR10: sweep (channel 1 only) */
        if (sweep_channel) {
            ch->sweep_period = (val >> 4) & 0x07;
            ch->sweep_dir = (val >> 3) & 1;
            ch->sweep_shift = val & 0x07;
        }
        break;

    case 1: /* NRx1: duty and length load */
        ch->duty = (val >> 6) & 3;
        ch->length_counter = 64 - (val & 0x3F);
        break;

    case 2: /* NRx2: volume envelope */
        ch->envelope_start = (val >> 4) & 0x0F;
        ch->envelope_dir = (val >> 3) & 1;
        ch->envelope_period = val & 0x07;
        ch->dac_enabled = (val & 0xF8) != 0;
        if (!ch->dac_enabled) ch->enabled = false;
        break;

    case 3: /* NRx3: frequency low bits */
        ch->freq = (u16)((ch->freq & 0x0700) | val);
        break;

    case 4: /* NRx4: frequency high bits, length enable, trigger */
        ch->freq = (u16)((ch->freq & 0x00FF) | ((val & 0x07) << 8));
        ch->length_enabled = (val & 0x40) != 0;
        if (val & 0x80) square_trigger(ch, 64, sweep_channel);
        break;

    default:
        break;
    }
}

static u16 sweep_next_freq(gb_apu_square_t *ch) {
    u16 delta = (u16)(ch->sweep_shadow >> ch->sweep_shift);
    if (ch->sweep_dir) {
        if (delta > ch->sweep_shadow) return 0;
        return (u16)(ch->sweep_shadow - delta);
    }
    return (u16)(ch->sweep_shadow + delta);
}

static void square_trigger(gb_apu_square_t *ch, int max_length, bool sweep_channel) {
    ch->enabled = ch->dac_enabled;
    if (ch->length_counter == 0) ch->length_counter = max_length;
    ch->freq_counter = (2048 - ch->freq) * 4;
    ch->envelope_counter = ch->envelope_period;
    ch->volume = ch->envelope_start;

    if (sweep_channel) {
        ch->sweep_shadow = ch->freq;
        ch->sweep_counter = ch->sweep_period ? ch->sweep_period : 8;
        ch->sweep_enabled = (ch->sweep_period != 0 || ch->sweep_shift != 0);
        if (ch->sweep_shift != 0 && sweep_next_freq(ch) > 2047) {
            ch->enabled = false;
        }
    }
}

static void wave_write(gb_apu_wave_t *ch, u8 reg, u8 val) {
    switch (reg) {
    case 0x1A: /* NR30: DAC power */
        ch->dac_enabled = (val & 0x80) != 0;
        if (!ch->dac_enabled) ch->enabled = false;
        break;
    case 0x1B: /* NR31: length load */
        ch->length_counter = 256 - val;
        break;
    case 0x1C: /* NR32: output level */
        ch->volume_shift = (val >> 5) & 0x03;
        break;
    case 0x1D: /* NR33: frequency low */
        ch->freq = (u16)((ch->freq & 0x0700) | val);
        break;
    case 0x1E: /* NR34: frequency high, length enable, trigger */
        ch->freq = (u16)((ch->freq & 0x00FF) | ((val & 0x07) << 8));
        ch->length_enabled = (val & 0x40) != 0;
        if (val & 0x80) {
            ch->enabled = ch->dac_enabled;
            if (ch->length_counter == 0) ch->length_counter = 256;
            ch->freq_counter = (2048 - ch->freq) * 2;
            ch->sample_index = 0;
        }
        break;
    default:
        break;
    }
}

static void noise_write(gb_apu_noise_t *ch, u8 reg, u8 val) {
    switch (reg) {
    case 0x20: /* NR41: length load */
        ch->length_counter = 64 - (val & 0x3F);
        break;
    case 0x21: /* NR42: volume envelope */
        ch->envelope_start = (val >> 4) & 0x0F;
        ch->envelope_dir = (val >> 3) & 1;
        ch->envelope_period = val & 0x07;
        ch->dac_enabled = (val & 0xF8) != 0;
        if (!ch->dac_enabled) ch->enabled = false;
        break;
    case 0x22: /* NR43: divisor and width */
        ch->period = noise_divisors[val & 0x07] << ((val >> 4) & 0x0F);
        ch->width_mode = (val & 0x08) != 0;
        break;
    case 0x23: /* NR44: length enable, trigger */
        ch->length_enabled = (val & 0x40) != 0;
        if (val & 0x80) {
            ch->enabled = ch->dac_enabled;
            if (ch->length_counter == 0) ch->length_counter = 64;
            ch->envelope_counter = ch->envelope_period;
            ch->volume = ch->envelope_start;
            ch->freq_counter = ch->period > 0 ? ch->period : 8;
            ch->lfsr = 0x7FFF;
        }
        break;
    default:
        break;
    }
}

void gb_apu_write_reg(gb_apu_t *apu, u8 reg, u8 val) {
    if (reg >= 0x30 && reg <= 0x3F) {
        apu->wave_ram[reg - 0x30] = val;
        return;
    }

    if (reg == 0x26) { /* NR52: master enable */
        bool on = (val & 0x80) != 0;
        if (!on && apu->enabled) {
            /* Powering down clears every channel register. */
            memset(&apu->ch1, 0, sizeof(apu->ch1));
            memset(&apu->ch2, 0, sizeof(apu->ch2));
            memset(&apu->ch3, 0, sizeof(apu->ch3));
            memset(&apu->ch4, 0, sizeof(apu->ch4));
            apu->ch4.lfsr = 0x7FFF;
            apu->frame_seq_step = 0;
            apu->frame_seq_counter = 0;
        }
        apu->enabled = on;
        return;
    }

    /* While powered down only NR52 and wave RAM accept writes. */
    if (!apu->enabled) return;

    switch (reg) {
    case 0x10: case 0x11: case 0x12: case 0x13: case 0x14:
        square_write(&apu->ch1, 0x10, reg, val, true);
        break;
    /* Channel 2 has no sweep register, so NR21 sits at 0x16. */
    case 0x16: case 0x17: case 0x18: case 0x19:
        square_write(&apu->ch2, 0x15, reg, val, false);
        break;
    case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E:
        wave_write(&apu->ch3, reg, val);
        break;
    case 0x20: case 0x21: case 0x22: case 0x23:
        noise_write(&apu->ch4, reg, val);
        break;
    case 0x24:
        apu->nr50 = val;
        apu->master_volume_l = (val >> 4) & 0x07;
        apu->master_volume_r = val & 0x07;
        break;
    case 0x25:
        apu->nr51 = val;
        break;
    default:
        break;
    }
}

u8 gb_apu_read_reg(gb_apu_t *apu, u8 reg, u8 raw) {
    if (reg == 0x26) {
        u8 status = apu->enabled ? 0x80 : 0x00;
        if (apu->ch1.enabled) status |= 0x01;
        if (apu->ch2.enabled) status |= 0x02;
        if (apu->ch3.enabled) status |= 0x04;
        if (apu->ch4.enabled) status |= 0x08;
        return status | 0x70;
    }
    if (reg >= 0x30 && reg <= 0x3F) {
        return apu->wave_ram[reg - 0x30];
    }

    /* Unreadable bits return 1 on real hardware. */
    static const u8 read_masks[0x30] = {
        [0x10 - 0x10] = 0x80, [0x11 - 0x10] = 0x3F, [0x12 - 0x10] = 0x00,
        [0x13 - 0x10] = 0xFF, [0x14 - 0x10] = 0xBF,
        [0x15 - 0x10] = 0xFF, [0x16 - 0x10] = 0x3F, [0x17 - 0x10] = 0x00,
        [0x18 - 0x10] = 0xFF, [0x19 - 0x10] = 0xBF,
        [0x1A - 0x10] = 0x7F, [0x1B - 0x10] = 0xFF, [0x1C - 0x10] = 0x9F,
        [0x1D - 0x10] = 0xFF, [0x1E - 0x10] = 0xBF,
        [0x20 - 0x10] = 0xFF, [0x21 - 0x10] = 0x00, [0x22 - 0x10] = 0x00,
        [0x23 - 0x10] = 0xBF,
        [0x24 - 0x10] = 0x00, [0x25 - 0x10] = 0x00,
    };

    if (reg >= 0x10 && reg < 0x40) {
        return raw | read_masks[reg - 0x10];
    }
    return raw;
}

/* ---------- Timing units ---------- */

static void square_step_timer(gb_apu_square_t *ch, int cycles) {
    if (!ch->enabled) return;
    int period = (2048 - ch->freq) * 4;
    if (period <= 0) period = 4;

    ch->freq_counter -= cycles;
    while (ch->freq_counter <= 0) {
        ch->freq_counter += period;
        ch->duty_counter = (ch->duty_counter + 1) & 7;
    }
}

static void wave_step_timer(gb_apu_wave_t *ch, const u8 *wave_ram, int cycles) {
    if (!ch->enabled) return;
    int period = (2048 - ch->freq) * 2;
    if (period <= 0) period = 2;

    ch->freq_counter -= cycles;
    while (ch->freq_counter <= 0) {
        ch->freq_counter += period;
        ch->sample_index = (ch->sample_index + 1) & 31;
        u8 byte = wave_ram[ch->sample_index >> 1];
        ch->sample_buffer = (ch->sample_index & 1) ? (byte & 0x0F) : (byte >> 4);
    }
}

static void noise_step_timer(gb_apu_noise_t *ch, int cycles) {
    if (!ch->enabled) return;
    int period = ch->period > 0 ? ch->period : 8;

    ch->freq_counter -= cycles;
    while (ch->freq_counter <= 0) {
        ch->freq_counter += period;
        u16 bit = (u16)((ch->lfsr ^ (ch->lfsr >> 1)) & 1);
        ch->lfsr = (u16)((ch->lfsr >> 1) | (bit << 14));
        if (ch->width_mode) {
            ch->lfsr = (u16)((ch->lfsr & ~0x40) | (bit << 6));
        }
    }
}

static void step_length_square(gb_apu_square_t *ch) {
    if (ch->length_enabled && ch->length_counter > 0) {
        if (--ch->length_counter == 0) ch->enabled = false;
    }
}

static void step_envelope_square(gb_apu_square_t *ch) {
    if (ch->envelope_period == 0) return;
    if (--ch->envelope_counter > 0) return;

    ch->envelope_counter = ch->envelope_period;
    if (ch->envelope_dir) {
        if (ch->volume < 15) ch->volume++;
    } else {
        if (ch->volume > 0) ch->volume--;
    }
}

static void step_sweep(gb_apu_square_t *ch) {
    if (--ch->sweep_counter > 0) return;

    ch->sweep_counter = ch->sweep_period ? ch->sweep_period : 8;
    if (!ch->sweep_enabled || ch->sweep_period == 0) return;

    u16 next = sweep_next_freq(ch);
    if (next > 2047) {
        ch->enabled = false;
        return;
    }
    if (ch->sweep_shift != 0) {
        ch->sweep_shadow = next;
        ch->freq = next;
        if (sweep_next_freq(ch) > 2047) ch->enabled = false;
    }
}

static void frame_sequencer_step(gb_apu_t *apu) {
    switch (apu->frame_seq_step) {
    case 0: case 4:
        step_length_square(&apu->ch1);
        step_length_square(&apu->ch2);
        if (apu->ch3.length_enabled && apu->ch3.length_counter > 0) {
            if (--apu->ch3.length_counter == 0) apu->ch3.enabled = false;
        }
        if (apu->ch4.length_enabled && apu->ch4.length_counter > 0) {
            if (--apu->ch4.length_counter == 0) apu->ch4.enabled = false;
        }
        break;

    case 2: case 6:
        step_length_square(&apu->ch1);
        step_length_square(&apu->ch2);
        if (apu->ch3.length_enabled && apu->ch3.length_counter > 0) {
            if (--apu->ch3.length_counter == 0) apu->ch3.enabled = false;
        }
        if (apu->ch4.length_enabled && apu->ch4.length_counter > 0) {
            if (--apu->ch4.length_counter == 0) apu->ch4.enabled = false;
        }
        step_sweep(&apu->ch1);
        break;

    case 7:
        step_envelope_square(&apu->ch1);
        step_envelope_square(&apu->ch2);
        if (apu->ch4.envelope_period != 0) {
            if (--apu->ch4.envelope_counter <= 0) {
                apu->ch4.envelope_counter = apu->ch4.envelope_period;
                if (apu->ch4.envelope_dir) {
                    if (apu->ch4.volume < 15) apu->ch4.volume++;
                } else {
                    if (apu->ch4.volume > 0) apu->ch4.volume--;
                }
            }
        }
        break;

    default:
        break;
    }

    apu->frame_seq_step = (apu->frame_seq_step + 1) & 7;
}

/* ---------- Mixing ---------- */

static int square_output(const gb_apu_square_t *ch) {
    if (!ch->enabled || !ch->dac_enabled) return 0;
    u8 pattern = duty_table[ch->duty & 3];
    int high = (pattern >> (7 - ch->duty_counter)) & 1;
    return high ? ch->volume : 0;
}

static int wave_output(const gb_apu_wave_t *ch) {
    if (!ch->enabled || !ch->dac_enabled) return 0;
    static const int shifts[4] = { 4, 0, 1, 2 };
    return ch->sample_buffer >> shifts[ch->volume_shift & 3];
}

static int noise_output(const gb_apu_noise_t *ch) {
    if (!ch->enabled || !ch->dac_enabled) return 0;
    return (~ch->lfsr & 1) ? ch->volume : 0;
}

static void apu_emit_sample(gb_apu_t *apu) {
    int outputs[4] = {
        square_output(&apu->ch1),
        square_output(&apu->ch2),
        wave_output(&apu->ch3),
        noise_output(&apu->ch4)
    };

    int left = 0, right = 0;
    for (int i = 0; i < 4; i++) {
        if (apu->nr51 & (0x10 << i)) left += outputs[i];
        if (apu->nr51 & (0x01 << i)) right += outputs[i];
    }

    left *= (apu->master_volume_l + 1);
    right *= (apu->master_volume_r + 1);

    /* Four channels of 0-15 scaled by a volume of 1-8 per side. */
    int mono = (left + right) / 2;
    int sample = (mono * 32767) / (4 * 15 * 8);
    if (sample > 32767) sample = 32767;
    if (sample < -32768) sample = -32768;

    int next = (apu->buffer_write + 1) % GB_APU_BUFFER_SIZE;
    if (next == apu->buffer_read) return; /* Buffer full: drop the sample */
    apu->buffer[apu->buffer_write] = (s16)sample;
    apu->buffer_write = next;
}

void gb_apu_step(gb_apu_t *apu, struct gb *gb, int cycles) {
    (void)gb;
    if (cycles <= 0) return;

    if (apu->enabled) {
        apu->frame_seq_counter += cycles;
        while (apu->frame_seq_counter >= FRAME_SEQ_CYCLES) {
            apu->frame_seq_counter -= FRAME_SEQ_CYCLES;
            frame_sequencer_step(apu);
        }

        square_step_timer(&apu->ch1, cycles);
        square_step_timer(&apu->ch2, cycles);
        wave_step_timer(&apu->ch3, apu->wave_ram, cycles);
        noise_step_timer(&apu->ch4, cycles);
    }

    /* Emit samples at exactly GB_APU_SAMPLE_RATE using a fraction counter. */
    apu->sample_counter += GB_APU_SAMPLE_RATE * cycles;
    while (apu->sample_counter >= GB_CPU_FREQ) {
        apu->sample_counter -= GB_CPU_FREQ;
        apu_emit_sample(apu);
    }
}

int gb_apu_read_samples(gb_apu_t *apu, s16 *out, int max) {
    int count = 0;
    while (count < max && apu->buffer_read != apu->buffer_write) {
        out[count++] = apu->buffer[apu->buffer_read];
        apu->buffer_read = (apu->buffer_read + 1) % GB_APU_BUFFER_SIZE;
    }
    return count;
}
