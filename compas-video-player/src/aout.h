#ifndef AOUT_H
#define AOUT_H

#include <stdbool.h>

// The sound output, for one writer: the video thread.
//
// Two ways out, as in CompasOS itself. The sound card is opened with tinyalsa,
// straight on the kernel's PCM. Bluetooth headphones are reached the only way
// a static program can reach them: raw PCM piped into the device's own
// `aplay -D bluealsa`, which is linked against the real ALSA library and its
// BlueALSA plugin.
//
// Which of the two is decided when the output is opened: Bluetooth when
// BlueALSA has a playback PCM (headphones connected), the card otherwise.
// Over Bluetooth the PCM is one pinned to the rate the link already runs at;
// see BT_PCM_NAME in aout.c for why that matters.

// Opens the output for 16-bit interleaved samples. `channels` and `bits` are
// what the writer will hand over; only 2 and 16 are taken.
bool audio_external_begin(int sample_rate, int channels, int bits);

// Writes `count` frames. Blocks while the output has no room. Returns the
// frames written.
int audio_external_write(const void *frames, int count);

// What has been written and not yet played, in milliseconds; -1 when not
// known. Over Bluetooth this is an estimate: what was written less the time
// gone by, plus aplay's wait before it starts and the delay BlueALSA reports
// for the link.
int audio_external_delay_ms(void);

// What is waiting to be taken by the output and can be seen to be: for
// Bluetooth, the sound in the pipe to aplay, in milliseconds. -1 for the card,
// whose delay above is already exact. The writer keeps this to a set amount,
// which no amount of waiting on aplay's side -- starting up, a pause -- can
// then add to.
int audio_external_queued_ms(void);

// Starts the card on what has been written so far, for a writer that keeps
// less waiting than the card's start threshold. Harmless when it is running.
void audio_external_start(void);

void audio_external_end(void);

// True when the output that is open is Bluetooth.
bool audio_external_is_bluetooth(void);

// One press of a volume key: +1 louder, -1 quieter, on whichever output is
// open. Returns the new level, 0 to 100, or -1 when it could not be set.
int audio_volume_step(int direction);

// Puts the sound card's volume back to what it was before the first step.
// Call once, when the app closes.
void audio_volume_restore(void);

#endif /* AOUT_H */
