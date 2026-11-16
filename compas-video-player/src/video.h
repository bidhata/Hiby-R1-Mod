#ifndef VIDEO_H
#define VIDEO_H

#include <stdbool.h>
#include <stdint.h>

// Video playback: a thread that reads a file, decodes its picture and its
// sound, and a page-sized RGB565 image for the interface to show.
//
// What it can play is set by the hardware, and was measured on it before any
// of this was written: one 1 GHz MIPS core with no SIMD
// decodes MPEG-4 at 800x450 and 30 frames a second with room to spare, and
// H.264 only up to about 480x270. So the files are expected to have been made
// for this screen (tools/video-convert.sh), no larger than the panel, and a
// file that is larger is refused rather than played at ten frames a second.
// Nothing is scaled: a picture is shown pixel for pixel, centred, turned a
// quarter turn when it is wider than tall so that it fills the panel held
// sideways.
//
// The clock is the sound card: the thread writes the sound, keeps a fifth of a second of it waiting in the
// card, and a frame is shown when the sample it belongs to is the one leaving
// the DAC. A file with no sound is paced by the monotonic clock.
//
// The picture is drawn by the interface thread, not by this one: the decoder
// queues finished frames in YUV, and video_present() converts the one that is
// due just before it is shown. One writer and one reader of the image, both
// the same thread, so a frame is never shown half converted.

// The size of the image: the panel's. Set once, before the first video_start().
void video_set_output(int width, int height);

// Starts playing `path` from `start_ms` (0 for the beginning; like any seek it
// lands on the key frame before). False when the thread does not start; a file
// that cannot be played is reported later, through video_failed(), since it is
// opened on the thread.
bool video_start(const char *path, int64_t start_ms);

// Stops and frees everything. Returns once the thread has ended. Anyone
// showing video_screen() must let go of it first.
void video_stop(void);

bool video_running(void);

// The file did not play. video_error() is why, as a language key.
bool video_failed(void);
const char *video_error(void);

// The last frame has been shown and the sound has run out.
bool video_ended(void);

void video_set_paused(bool paused);
bool video_paused(void);

// Moves by `seconds` from where it is, either way. Lands on the key frame at
// or before the place asked for, which is where a decoder can start.
void video_seek_relative(int seconds);

// Moves to `ms` from the start of the file, in the same way.
void video_seek_to(int64_t ms);

// Holds the picture back by `ms` against the sound (or, negative, shows it
// sooner): a correction for an output whose delay is not exactly known. Takes
// effect at once and stays until changed.
void video_set_sync_offset(int ms);

// Where playback is and how long the file is, in milliseconds. Zero when not
// known.
int64_t video_position_ms(void);
int64_t video_duration_ms(void);

// The image, video_set_output()'s size in RGB565. NULL until a file is open.
// Written only inside video_present() and video_repaint().
const uint16_t *video_screen(void);

// Converts the frame that is due. True when there was one, and what was drawn
// into should be shown. Interface thread only.
//
// `into` is where: NULL for the image above, or a buffer of the same size and
// layout -- a framebuffer page, when the page draws without LVGL in between.
// Only the picture itself is written: what lies around a picture smaller than
// the panel is the caller's to have made black.
bool video_present(uint16_t *into);

// Converts the frame last presented again, into the image or into `into`: for
// showing in one place what was last drawn in the other. False before the
// first frame.
bool video_repaint(uint16_t *into);

// Whether the picture is shown turned a quarter turn (wider than tall), top
// along the panel's right edge. Known from the first frame on.
bool video_turned(void);

// True when `name` ends in an extension this player opens as video.
bool video_is_video_file(const char *name);

#endif /* VIDEO_H */
