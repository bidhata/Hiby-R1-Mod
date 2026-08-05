#include "gb.h"
#include "apu.h"
#include <string.h>

/* Duty cycle waveforms (8-bit patterns) */
/* 12.5%: 0 0 0 0 0 0 0 1 */
/* 25.0%: 1 0 0 0 0 0 0 1 */
/* 50.0%: 1 0 0 0 0 1 1 1 */
/* 75.0%: 0 1 1 1 1 1 1 0 */
static const u8 duty_table[4] = {
    0x01, /* 00000001 */
    0x81, /* 10000001 */
    0x7E, /* 01111110 */
    0x82  /* 10000010 */
};

static const u8 duty_lengths[4] = {8, 4, 2, 1};

void gb_apu_init(gb_apu_t *apu) {
    memset(apu, 0, sizeof(*apu));
    apu->enabled = true;
    apu->nr50 = 0x77;
    apu->nr51 = 0xF3;
    apu->master_volume_l = 7;
    apu->master_volume_r = 7;

    /* Channel 1 */
    apu->ch1.enabled = false;
    apu->ch1.duty = 2; /* 50% */
    apu->ch1.length = 0;
    apu->ch1.freq = 0;
    apu->ch1.length_enabled = false;
    apu->ch1.volume = 0;
    apu->ch1.envelope_vol = 0;
    apu->ch1.envelope_dir = 0;
    apu->ch1.envelope_period = 0;
    apu->ch1.envelope_counter = 0;
    apu->ch1.freq_counter = 0;
    apu->ch1.length_counter = 64;
    apu->ch1.duty_counter = 0;

    /* Channel 2 */
    apu->ch2.enabled = false;
    apu->ch2.duty = 2;
    apu->ch2.length = 0;
    apu->ch2.freq = 0;
    apu->ch2.length_enabled = false;
    apu->ch2.volume = 0;
    apu->ch2.envelope_vol = 0;
    apu->ch2.envelope_dir = 0;
    apu->ch2.envelope_period = 0;
    apu->ch2.envelope_counter = 0;
    apu->ch2.freq_counter = 0;
    apu->ch2.length_counter = 64;
    apu->ch2.duty_counter = 0;
}

static inline void step_channel(gb_apu_channel_t *ch) {
    /* Frequency counter: generate waveform */
    ch->freq_counter++;
    if (ch->freq_counter >= (2048 - ch->freq) * 4) {
        ch->freq_counter = 0;
        ch->duty_counter = (ch->duty_counter + 1) & 7;
    }
}

static inline void step_length(gb_apu_channel_t *ch) {
    if (ch->length_enabled) {
        ch->length_counter--;
        if (ch->length_counter <= 0) {
            ch->enabled = false;
            ch->length_counter = 0;
        }
    }
}

static inline void step_envelope(gb_apu_channel_t *ch) {
    if (ch->envelope_period == 0) return;
    ch->envelope_counter--;
    if (ch->envelope_counter <= 0) {
        ch->envelope_counter = ch->envelope_period;
        if (ch->envelope_dir) {
            if (ch->volume < 15) ch->volume++;
        } else {
            if (ch->volume > 0) ch->volume--;
        }
    }
}

void gb_apu_step(gb_apu_t *apu, struct gb *gb, int cycles) {
    if (!apu->enabled) return;

    /* Handle APU I/O writes from gb->io */
    /* NR50 (0xFF24) */
    apu->nr50 = gb->io[0x24];
    apu->master_volume_l = (apu->nr50 >> 4) & 7;
    apu->master_volume_r = apu->nr50 & 7;

    /* NR51 (0xFF25) */
    apu->nr51 = gb->io[0x25];

    /* NR52 (0xFF26): master enable */
    if (!(gb->io[0x26] & 0x80)) {
        apu->ch1.enabled = false;
        apu->ch2.enabled = false;
        return;
    }

    /* Channel 1 (0xFF10-0xFF14) */
    /* NR10 (0xFF10): sweep - TODO: implement sweep */
    /* NR11 (0xFF11): duty + length */
    apu->ch1.length = gb->io[0x11] & 0x3F;
    apu->ch1.length_counter = 64 - apu->ch1.length;
    if (gb->io[0x14] & 0x40) {
        apu->ch1.length_enabled = true;
    }

    /* NR12 (0xFF12): volume envelope */
    apu->ch1.volume = (gb->io[0x12] >> 4) & 0x0F;
    apu->ch1.envelope_dir = (gb->io[0x12] >> 3) & 1;
    apu->ch1.envelope_period = gb->io[0x12] & 0x07;

    /* NR13 (0xFF13) + NR14 (0xFF14): frequency */
    apu->ch1.freq = ((gb->io[0x14] & 0x07) << 8) | gb->io[0x13];

    /* Trigger */
    if (gb->io[0x14] & 0x80) {
        gb->io[0x14] &= ~0x80; /* Clear trigger */
        apu->ch1.enabled = true;
        apu->ch1.duty = (gb->io[0x11] >> 6) & 3;
        apu->ch1.freq_counter = 0;
        apu->ch1.duty_counter = 0;
        apu->ch1.envelope_counter = apu->ch1.envelope_period;
        if (apu->ch1.length_counter == 0)
            apu->ch1.length_counter = 64;
        /* Sweep trigger TODO */
    }

    /* Channel 2 (0xFF16-0xFF19) */
    /* NR21 (0xFF16): duty + length */
    apu->ch2.length = gb->io[0x16] & 0x3F;
    apu->ch2.length_counter = 64 - apu->ch2.length;
    if (gb->io[0x19] & 0x40) {
        apu->ch2.length_enabled = true;
    }

    /* NR22 (0xFF17): volume envelope */
    apu->ch2.volume = (gb->io[0x17] >> 4) & 0x0F;
    apu->ch2.envelope_dir = (gb->io[0x17] >> 3) & 1;
    apu->ch2.envelope_period = gb->io[0x17] & 0x07;

    /* NR23 (0xFF18) + NR24 (0xFF19): frequency */
    apu->ch2.freq = ((gb->io[0x19] & 0x07) << 8) | gb->io[0x18];

    /* Trigger */
    if (gb->io[0x19] & 0x80) {
        gb->io[0x19] &= ~0x80;
        apu->ch2.enabled = true;
        apu->ch2.duty = (gb->io[0x16] >> 6) & 3;
        apu->ch2.freq_counter = 0;
        apu->ch2.duty_counter = 0;
        apu->ch2.envelope_counter = apu->ch2.envelope_period;
        if (apu->ch2.length_counter == 0)
            apu->ch2.length_counter = 64;
    }

    /* Step channels: advance frequency */
    if (apu->ch1.enabled) step_channel(&apu->ch1);
    if (apu->ch2.enabled) step_channel(&apu->ch2);

    /* Step length counters every 256 T-cycles (4 MHz / 256 = ~15.6 kHz) */
    static int length_div = 0;
    length_div += cycles;
    while (length_div >= 256) {
        length_div -= 256;
        if (apu->ch1.enabled) step_length(&apu->ch1);
        if (apu->ch2.enabled) step_length(&apu->ch2);
    }

    /* Step envelope every ~64ms (envelope period * 1/64s) */
    static int env_div = 0;
    env_div += cycles;
    while (env_div >= 65536) { /* 4096 Hz * 16 = ~65536 cycles at 4 MHz */
        env_div -= 65536;
        if (apu->ch1.enabled) step_envelope(&apu->ch1);
        if (apu->ch2.enabled) step_envelope(&apu->ch2);
    }
}

s16 gb_apu_sample(gb_apu_t *apu) {
    s16 sample = 0;
    s32 mix = 0;

    /* Channel 1: square wave */
    if (apu->ch1.enabled) {
        u8 duty = duty_table[apu->ch1.duty & 3];
        u8 bit = (duty >> (7 - apu->ch1.duty_counter)) & 1;
        s32 amp = bit ? apu->ch1.volume : -apu->ch1.volume;
        mix += amp;
    }

    /* Channel 2: square wave */
    if (apu->ch2.enabled) {
        u8 duty = duty_table[apu->ch2.duty & 3];
        u8 bit = (duty >> (7 - apu->ch2.duty_counter)) & 1;
        s32 amp = bit ? apu->ch2.volume : -apu->ch2.volume;
        mix += amp;
    }

    /* Scale to 16-bit range */
    /* Max possible: 2 channels * 15 volume = 30 */
    /* Scale: mix / 30 * 32767 */
    if (mix > 0) {
        sample = (s16)((mix * 32767) / 30);
    } else {
        sample = (s16)((mix * 32768) / 30);
    }

    /* Apply master volume */
    int master = (apu->master_volume_l + apu->master_volume_r) / 2;
    if (master == 0) master = 1;
    sample = (s16)(sample * master / 14);

    return sample;
}
