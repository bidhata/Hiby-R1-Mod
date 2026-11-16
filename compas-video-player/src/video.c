#include "video.h"

#include <string.h>
#include <strings.h>

// The extensions the file list shows.
bool video_is_video_file(const char *name) {
	const char *dot = name ? strrchr(name, '.') : NULL;
	if (!dot) {
		return false;
	}
	static const char *const exts[] = {".mp4", ".m4v", ".mkv", ".avi", ".mov"};
	for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
		if (strcasecmp(dot, exts[i]) == 0) {
			return true;
		}
	}
	return false;
}

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <sys/resource.h>
#include <sys/syscall.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>

#include "aout.h"

// How much sound is kept waiting in the card, which is how far the decoder
// runs ahead of the picture: by the time this much is written, the frames that
// go with it have been decoded too. Deep enough to ride out a slow frame (the
// worst measured was 45 ms), shallow enough that the frames waiting to be
// shown stay few -- each is half a megabyte at 800x450.
//
// It is kept by sleeping, not by the size of the ALSA buffer (see "pacing"
// below for how much there is taken to be). The output is
// opened at its usual depth (three quarters of a second): the R1's driver
// refuses a 200 ms buffer outright ("Cannot apply HW parameters"), and a
// Bluetooth PCM has a depth of its own whatever is asked for. The thread
// writes, works out how much is waiting, and sleeps off whatever is over this.
#define AUDIO_LEAD_MS 200

// The same for an output paced by count (Bluetooth: see "pacing" in the
// worker). More, not less: what is in hand there is all the cushion there is
// between a decoder held up for a fifth of a second -- two heavy frames, the
// display thread taking the core in between -- and aplay running dry, which
// is a click in the headphones and B-frames dropped to catch up. At 100 ms
// that happened every half minute; at 250, measured as what is really in the
// pipe, the pipe was still seen down to 17 ms and frames went in one window
// in three. It costs nothing in memory: what waits longer for its sound is
// compressed packets. It does cost that much more old sound after a seek.
#define AUDIO_LEAD_COUNTED_MS 400

// Paused over such an output, silence is written while what the output says
// is waiting is under the lead plus this: the figure includes the link's own
// delay and aplay's buffer, a quarter of a second between them.
#define PAUSE_SILENCE_OVER_MS 250

// Frames decoded and not yet due. Few: a frame is decoded only when it is
// about to be wanted (DECODE_AHEAD_MS), however long it then has to wait for
// its sound -- over Bluetooth, with the viewer's correction on top, that can
// be most of a second. What waits that long is the compressed packet, a few
// kilobytes, and not the decoded frame, half a megabyte: with the player still
// in memory beside the app this device has under ten megabytes to spare and
// its swap is full.
#define QUEUE_MAX 8

// How long before a frame is due its packet is given to the decoder. Enough
// for the decoder's reordering (two B-frames), a heavy frame's 50 ms, and the
// display thread taking the core for its own 13 ms now and then.
#define DECODE_AHEAD_MS 240

// Compressed video waiting for its time. At 25 frames a second this is nearly
// four seconds, several times the longest wait there can be.
#define PACKETS_MAX 96

// A packet reached the decoder this long after its time: the decoder is not
// keeping up, and B-frames go until it has caught up; this long, and
// everything but key frames goes.
#define LATE_SKIP_B_MS 120
#define LATE_KEY_ONLY_MS 700

// Below this much sound in hand the decoder is losing the race, and the picture
// gives way before the sound does: first the frames nothing else is predicted
// from (B-frames), then everything but key frames. Above the second figure it
// has caught up.
#define SHED_BELOW_MS 70
#define SHED_ABOVE_MS 140
#define SHED_HOLD_MS 1000
// The same three for an output paced by count (Bluetooth). There what is "in
// hand" is only how far the writer is ahead of the clock, half as much to
// begin with, and behind it aplay holds another fifth of a second that the
// figure does not show. Judged by the figures above, one heavy frame was
// enough to trip it and B-frames went for a whole second each time: a fifth
// of the frames of a film that the device was otherwise keeping up with. So
// here it takes being level with the clock -- nothing ahead at all -- and it
// lets go sooner.
//
// And briefly. Skipping two or three B-frames wins back a tenth of a second,
// which is all a heavy scene costs; held for 400 ms it took ten to twenty
// frames each time, every quarter of a minute.
#define SHED_COUNTED_BELOW_MS 10
#define SHED_COUNTED_ABOVE_MS 40
#define SHED_COUNTED_HOLD_MS 80
// How long after a start, a seek or a resume the level in the buffer means
// anything: until then it is low because it is still filling.
#define SHED_WARMUP_MS 1500

// How often the thread says how playback is going.
#define STATS_MS 10000

static pthread_t worker;
static bool thread_live;
static volatile bool stop_flag;
static volatile bool running_flag;
static volatile bool failed_flag;
static volatile bool ended_flag;
static volatile bool paused_flag;
static const char *volatile error_key = "video_cannot_open";

static char file_path[1024];

static int out_w = 480, out_h = 800;
static uint16_t *screen;

// A seek asked for by the page and not yet done. Relative, and summed: three
// taps on +10 before the thread looks are one seek of thirty seconds.
static pthread_mutex_t seek_lock = PTHREAD_MUTEX_INITIALIZER;
static int seek_pending_s;
static int64_t seek_to_ms = -1; // a place asked for outright, which the steps above then count from
static volatile bool seek_wanted;

static volatile int64_t duration_ms;
static volatile bool turned_flag;

// ---------------------------------------------------------------------------
// the queue and the clock
// ---------------------------------------------------------------------------
//
// Both under q_lock, and both touched by the two threads: the decoder adds
// frames and moves the clock's anchor, the interface takes frames and reads
// the clock.

typedef struct {
	AVFrame *frame;
	int64_t pts_ms;
} queued_t;

static pthread_mutex_t q_lock = PTHREAD_MUTEX_INITIALIZER;
static queued_t queue[QUEUE_MAX];
static int q_head, q_count;

// The clock is "this position was playing at this moment": between two
// anchors it runs with the monotonic clock. With sound the anchor is renewed
// at every write, from what was written less what the card still holds, and
// the clock never runs past the last sample written -- if the decoder stalls,
// the sound stops and so must the picture.
static bool have_audio;
static bool clock_valid;
static bool clock_uncapped; // end of file: nothing more will be written
static int64_t anchor_pts_ms;
static int64_t anchor_mono_ms;
static int64_t clock_limit_ms;
static int64_t clock_last_ms;

// Compressed video packets, in the order they were read, waiting to be
// decoded. The worker thread's alone.
static AVPacket *packets[PACKETS_MAX];
static int p_head, p_count;

static void packets_clear(void) {
	while (p_count > 0) {
		av_packet_free(&packets[p_head]);
		p_head = (p_head + 1) % PACKETS_MAX;
		p_count--;
	}
	p_head = 0;
}

// What happened to the frames, for the line the thread logs every few seconds:
// shown, thrown away because a later one was already due, and pushed out of a
// full queue. Under q_lock.
static unsigned stat_shown, stat_late, stat_overflow;
// Packets that reached the decoder late enough to have frames dropped for it.
// The worker's alone.
static unsigned stat_skipping;

// Show the next frame whatever the clock says: the first one after a seek, so
// that seeking while paused moves the picture.
static bool show_next;

static int64_t mono_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// The viewer's correction of sound against picture (video_set_sync_offset):
// the clock the picture is shown by runs this much behind the one above.
static volatile int sync_offset_ms;

void video_set_sync_offset(int ms) { sync_offset_ms = ms; }

static int64_t clock_now_locked(void) {
	if (!clock_valid || paused_flag) {
		return clock_last_ms;
	}
	int64_t now = anchor_pts_ms + (mono_ms() - anchor_mono_ms);
	if (have_audio && !clock_uncapped && now > clock_limit_ms) {
		now = clock_limit_ms;
	}
	if (now > clock_last_ms) {
		clock_last_ms = now; // never backwards between two anchors
	}
	return clock_last_ms;
}

static void clock_anchor(int64_t pts_ms, int64_t limit_ms) {
	pthread_mutex_lock(&q_lock);
	anchor_pts_ms = pts_ms;
	anchor_mono_ms = mono_ms();
	clock_limit_ms = limit_ms;
	if (!clock_valid) {
		clock_last_ms = pts_ms; // a fresh start: it may be earlier than where it was
		clock_valid = true;
	}
	pthread_mutex_unlock(&q_lock);
}

// Stops the clock where it is, until the next anchor.
static void clock_hold(void) {
	pthread_mutex_lock(&q_lock);
	clock_now_locked();
	clock_valid = false;
	pthread_mutex_unlock(&q_lock);
}

static void queue_clear(void) {
	pthread_mutex_lock(&q_lock);
	while (q_count > 0) {
		av_frame_free(&queue[q_head].frame);
		q_head = (q_head + 1) % QUEUE_MAX;
		q_count--;
	}
	q_head = 0;
	pthread_mutex_unlock(&q_lock);
}

// Takes ownership of `frame`. A full queue loses its oldest frame: it is full
// because the interface is not keeping up, and the oldest is the one it would
// have thrown away next.
static void queue_push(AVFrame *frame, int64_t pts_ms) {
	pthread_mutex_lock(&q_lock);
	if (q_count == QUEUE_MAX) {
		av_frame_free(&queue[q_head].frame);
		q_head = (q_head + 1) % QUEUE_MAX;
		q_count--;
		stat_overflow++;
	}
	int tail = (q_head + q_count) % QUEUE_MAX;
	queue[tail].frame = frame;
	queue[tail].pts_ms = pts_ms;
	q_count++;
	pthread_mutex_unlock(&q_lock);
}

static int queue_count(void) {
	pthread_mutex_lock(&q_lock);
	int n = q_count;
	pthread_mutex_unlock(&q_lock);
	return n;
}

// ---------------------------------------------------------------------------
// YUV 4:2:0 to RGB565
// ---------------------------------------------------------------------------
//
// BT.601 by table: one lookup per term, one more to clamp and pack each
// channel. No SIMD on this core, and a multiply per pixel is slower than the
// load. Measured at 14 ms for 800x450 turned, 11 ms upright.

static int32_t tab_y[256], tab_rv[256], tab_gu[256], tab_gv[256], tab_bu[256];
#define CLAMP_OFF 512
static uint16_t pack_r[CLAMP_OFF * 2 + 256], pack_g[CLAMP_OFF * 2 + 256], pack_b[CLAMP_OFF * 2 + 256];
static bool tables_ready;

// The matrix is the stream's own: BT.709 for a film that says so (anything
// that came from HD keeps the tag through a conversion), BT.601 otherwise.
// Taking one for the other shifts reds and greens visibly.
static void tables_init(bool full_range, bool bt709) {
	double ky = full_range ? 1.0 : 1.164;
	double krv = bt709 ? (full_range ? 1.5748 : 1.793) : (full_range ? 1.402 : 1.596);
	double kgu = bt709 ? (full_range ? -0.1873 : -0.213) : (full_range ? -0.344 : -0.392);
	double kgv = bt709 ? (full_range ? -0.4681 : -0.533) : (full_range ? -0.714 : -0.813);
	double kbu = bt709 ? (full_range ? 1.8556 : 2.112) : (full_range ? 1.772 : 2.017);
	for (int i = 0; i < 256; i++) {
		tab_y[i] = (int32_t)(ky * (i - (full_range ? 0 : 16)));
		tab_rv[i] = (int32_t)(krv * (i - 128));
		tab_gu[i] = (int32_t)(kgu * (i - 128));
		tab_gv[i] = (int32_t)(kgv * (i - 128));
		tab_bu[i] = (int32_t)(kbu * (i - 128));
	}
	for (int i = 0; i < CLAMP_OFF * 2 + 256; i++) {
		int v = i - CLAMP_OFF;
		if (v < 0) {
			v = 0;
		}
		if (v > 255) {
			v = 255;
		}
		pack_r[i] = (uint16_t)((v >> 3) << 11);
		pack_g[i] = (uint16_t)((v >> 2) << 5);
		pack_b[i] = (uint16_t)(v >> 3);
	}
}

// The panel has five bits of red and blue and six of green, and a film is
// full of slow gradients -- a dark wall, a sky -- that come out as bands when
// eight bits are simply cut down. So each pixel is nudged before the cut by a
// small amount that depends on where it sits in its 2x2 block (an ordered
// dither): `d` is that amount for red and blue, whose steps are 8, and green,
// whose steps are 4, takes half. The four values average to nothing, so the
// picture is no lighter or darker, and neighbouring pixels land on either
// side of a step in proportion to how far between two steps the colour is.
#define DITHER_A (-3)
#define DITHER_B 1
#define DITHER_C 3
#define DITHER_D (-1)

#define PIXEL(yv, rr, gg, bb, d) \
	(uint16_t)(pack_r[CLAMP_OFF + (yv) + (rr) + (d)] | pack_g[CLAMP_OFF + (yv) + (gg) + (d) / 2] | pack_b[CLAMP_OFF + (yv) + (bb) + (d)])

// Whether a picture of this size is shown turned, and whether it fits at all.
static bool fits_upright(int w, int h) { return w <= out_w && h <= out_h; }
static bool fits_turned(int w, int h) { return w <= out_h && h <= out_w; }
static bool shown_turned(int w, int h) {
	if (w > h) {
		return fits_turned(w, h);
	}
	return !fits_upright(w, h);
}

// Upright: row to row, centred.
static void convert_upright(const AVFrame *f, uint16_t *dst) {
	int w = f->width & ~1, h = f->height & ~1;
	int off_x = ((out_w - w) / 2) & ~1;
	int off_y = ((out_h - h) / 2) & ~1;

	for (int y = 0; y < h; y += 2) {
		const uint8_t *y0 = f->data[0] + (ptrdiff_t)y * f->linesize[0];
		const uint8_t *y1 = y0 + f->linesize[0];
		const uint8_t *u = f->data[1] + (ptrdiff_t)(y / 2) * f->linesize[1];
		const uint8_t *v = f->data[2] + (ptrdiff_t)(y / 2) * f->linesize[2];
		uint16_t *d0 = dst + (size_t)(off_y + y) * out_w + off_x;
		uint16_t *d1 = d0 + out_w;

		for (int x = 0; x < w; x += 2) {
			int rr = tab_rv[*v], gg = tab_gu[*u] + tab_gv[*v], bb = tab_bu[*u];
			int a = tab_y[y0[0]], b = tab_y[y0[1]], c = tab_y[y1[0]], e = tab_y[y1[1]];
			d0[0] = PIXEL(a, rr, gg, bb, DITHER_A);
			d0[1] = PIXEL(b, rr, gg, bb, DITHER_B);
			d1[0] = PIXEL(c, rr, gg, bb, DITHER_C);
			d1[1] = PIXEL(e, rr, gg, bb, DITHER_D);
			y0 += 2, y1 += 2, d0 += 2, d1 += 2, u++, v++;
		}
	}
}

// A quarter turn, centred: source column sx becomes image row sx, read from
// the bottom of the picture up, so the top of the picture lies along the
// panel's right edge.
//
// In bands of BAND source rows. Going down a whole column at once touches one
// cache line per row in each plane, more than the 32 KB data cache holds, and
// every read misses (25 ms a frame at 800x450). Within a band the same few
// dozen lines serve 32 columns in a row.
static void convert_turned(const AVFrame *f, uint16_t *dst) {
	int w = f->width & ~1, h = f->height & ~1;
	int off_x = ((out_w - h) / 2) & ~1;
	int off_y = ((out_h - w) / 2) & ~1;
	ptrdiff_t ls = f->linesize[0], lsu = f->linesize[1], lsv = f->linesize[2];

	enum { BAND = 32 };
	for (int i0 = 0; i0 < h; i0 += BAND) {
		int i1 = i0 + BAND < h ? i0 + BAND : h;
		for (int sx = 0; sx < w; sx += 2) {
			uint16_t *d0 = dst + (size_t)(off_y + sx) * out_w + off_x + i0;
			uint16_t *d1 = d0 + out_w;
			const uint8_t *yp = f->data[0] + (ptrdiff_t)(h - 1 - i0) * ls + sx;
			const uint8_t *up = f->data[1] + (ptrdiff_t)(h / 2 - 1 - i0 / 2) * lsu + sx / 2;
			const uint8_t *vp = f->data[2] + (ptrdiff_t)(h / 2 - 1 - i0 / 2) * lsv + sx / 2;

			for (int i = i0; i < i1; i += 2) {
				int rr = tab_rv[*vp], gg = tab_gu[*up] + tab_gv[*vp], bb = tab_bu[*up];
				const uint8_t *yq = yp - ls; // the row above
				int a = tab_y[yp[0]], b = tab_y[yp[1]], c = tab_y[yq[0]], e = tab_y[yq[1]];
				d0[0] = PIXEL(a, rr, gg, bb, DITHER_A);
				d1[0] = PIXEL(b, rr, gg, bb, DITHER_C);
				d0[1] = PIXEL(c, rr, gg, bb, DITHER_B);
				d1[1] = PIXEL(e, rr, gg, bb, DITHER_D);
				d0 += 2, d1 += 2;
				yp -= 2 * ls, up -= lsu, vp -= lsv;
			}
		}
	}
}

const uint16_t *video_screen(void) { return screen; }

// The frame last presented, kept so that it can be drawn again somewhere else
// (video_repaint). Interface thread only, like the image.
static AVFrame *held;

static void convert(const AVFrame *f, uint16_t *dst) {
	if (!tables_ready) {
		tables_init(f->color_range == AVCOL_RANGE_JPEG || f->format == AV_PIX_FMT_YUVJ420P, f->colorspace == AVCOL_SPC_BT709);
		tables_ready = true;
	}
	if (shown_turned(f->width, f->height)) {
		convert_turned(f, dst);
	} else {
		convert_upright(f, dst);
	}
}

bool video_repaint(uint16_t *into) {
	if (!screen || !held) {
		return false;
	}
	convert(held, into ? into : screen);
	return true;
}

bool video_present(uint16_t *into) {
	if (!screen) {
		return false;
	}

	AVFrame *take = NULL;

	pthread_mutex_lock(&q_lock);
	int64_t now = clock_now_locked() - sync_offset_ms;
	if (show_next) {
		if (q_count > 0) {
			show_next = false;
			take = queue[q_head].frame;
		}
	} else if (clock_valid && !paused_flag) {
		// A frame with a later one also due is already late: converting it
		// would cost 14 ms to show something for none.
		while (q_count >= 2 && queue[(q_head + 1) % QUEUE_MAX].pts_ms <= now) {
			av_frame_free(&queue[q_head].frame);
			q_head = (q_head + 1) % QUEUE_MAX;
			q_count--;
			stat_late++;
		}
		if (q_count > 0 && queue[q_head].pts_ms <= now + 4) {
			take = queue[q_head].frame;
		}
	}
	if (take) {
		queue[q_head].frame = NULL;
		q_head = (q_head + 1) % QUEUE_MAX;
		q_count--;
		stat_shown++;
	}
	pthread_mutex_unlock(&q_lock);

	if (!take) {
		return false;
	}

	convert(take, into ? into : screen);
	av_frame_free(&held);
	held = take;
	return true;
}

// ---------------------------------------------------------------------------
// sound
// ---------------------------------------------------------------------------

static int16_t *pcm_buf;
static int pcm_cap; // in frames

// Whatever the decoder produced, as interleaved 16-bit stereo: the one format
// the output is opened in. More than two channels keeps the first two, which
// in every layout FFmpeg produces are front left and front right; files made
// for this player are stereo already.
static int to_s16_stereo(const AVFrame *f) {
	int n = f->nb_samples;
	int ch = f->ch_layout.nb_channels;
	if (n <= 0 || ch <= 0) {
		return 0;
	}
	if (n > pcm_cap) {
		int16_t *grown = realloc(pcm_buf, (size_t)n * 2 * sizeof(int16_t));
		if (!grown) {
			return 0;
		}
		pcm_buf = grown;
		pcm_cap = n;
	}

	int right = ch > 1 ? 1 : 0;
	int16_t *out = pcm_buf;

	switch (f->format) {
	case AV_SAMPLE_FMT_S16P: {
		const int16_t *l = (const int16_t *)f->data[0], *r = (const int16_t *)f->data[right];
		for (int i = 0; i < n; i++) {
			*out++ = l[i];
			*out++ = r[i];
		}
		break;
	}
	case AV_SAMPLE_FMT_S16: {
		const int16_t *s = (const int16_t *)f->data[0];
		for (int i = 0; i < n; i++) {
			*out++ = s[i * ch];
			*out++ = s[i * ch + right];
		}
		break;
	}
	case AV_SAMPLE_FMT_S32P: {
		const int32_t *l = (const int32_t *)f->data[0], *r = (const int32_t *)f->data[right];
		for (int i = 0; i < n; i++) {
			*out++ = (int16_t)(l[i] >> 16);
			*out++ = (int16_t)(r[i] >> 16);
		}
		break;
	}
	case AV_SAMPLE_FMT_S32: {
		const int32_t *s = (const int32_t *)f->data[0];
		for (int i = 0; i < n; i++) {
			*out++ = (int16_t)(s[i * ch] >> 16);
			*out++ = (int16_t)(s[i * ch + right] >> 16);
		}
		break;
	}
	case AV_SAMPLE_FMT_FLTP: {
		const float *l = (const float *)f->data[0], *r = (const float *)f->data[right];
		for (int i = 0; i < n; i++) {
			float a = l[i] * 32767.0f, b = r[i] * 32767.0f;
			*out++ = (int16_t)(a > 32767.0f ? 32767.0f : a < -32768.0f ? -32768.0f : a);
			*out++ = (int16_t)(b > 32767.0f ? 32767.0f : b < -32768.0f ? -32768.0f : b);
		}
		break;
	}
	case AV_SAMPLE_FMT_FLT: {
		const float *s = (const float *)f->data[0];
		for (int i = 0; i < n; i++) {
			float a = s[i * ch] * 32767.0f, b = s[i * ch + right] * 32767.0f;
			*out++ = (int16_t)(a > 32767.0f ? 32767.0f : a < -32768.0f ? -32768.0f : a);
			*out++ = (int16_t)(b > 32767.0f ? 32767.0f : b < -32768.0f ? -32768.0f : b);
		}
		break;
	}
	default:
		return 0;
	}
	return n;
}

// The fixed-point decoder for a codec, where FFmpeg has one. The float ones
// are the default and cost several times as much on this FPU.
static const AVCodec *audio_decoder_for(enum AVCodecID id) {
	const char *fixed = NULL;
	switch (id) {
	case AV_CODEC_ID_AAC:
		fixed = "aac_fixed";
		break;
	case AV_CODEC_ID_MP3:
		fixed = "mp3";
		break;
	case AV_CODEC_ID_MP2:
		fixed = "mp2";
		break;
	case AV_CODEC_ID_AC3:
		fixed = "ac3_fixed";
		break;
	default:
		break;
	}
	const AVCodec *codec = fixed ? avcodec_find_decoder_by_name(fixed) : NULL;
	return codec ? codec : avcodec_find_decoder(id);
}

// ---------------------------------------------------------------------------
// the worker thread
// ---------------------------------------------------------------------------

static void fail(const char *key) {
	error_key = key;
	failed_flag = true;
}

static int64_t stream_ms(const AVStream *st, int64_t ts, int64_t start_ms) {
	if (ts == AV_NOPTS_VALUE) {
		return -1;
	}
	return av_rescale_q(ts, st->time_base, (AVRational){1, 1000}) - start_ms;
}

// Takes what the decoder has finished and queues it for the display. False
// when a picture is in a format the conversion does not do, which ends the
// film (the reason is set).
static bool receive_pictures(AVCodecContext *vdec, AVFrame *frame, const AVStream *vst, int64_t start_ms, AVRational fr,
							 int64_t *next_ms) {
	while (!stop_flag && avcodec_receive_frame(vdec, frame) >= 0) {
		if (frame->format != AV_PIX_FMT_YUV420P && frame->format != AV_PIX_FMT_YUVJ420P) {
			fprintf(stderr, "video: pixel format %s is not 4:2:0\n", av_get_pix_fmt_name(frame->format) ? av_get_pix_fmt_name(frame->format) : "?");
			fail("video_not_supported");
			av_frame_unref(frame);
			return false;
		}
		int64_t pts = stream_ms(vst, frame->best_effort_timestamp, start_ms);
		if (pts < 0) {
			pts = *next_ms;
		}
		*next_ms = pts + (fr.num ? (int64_t)1000 * fr.den / fr.num : 40);

		AVFrame *keep = av_frame_alloc();
		if (!keep) {
			av_frame_unref(frame);
			continue;
		}
		av_frame_move_ref(keep, frame);
		queue_push(keep, pts);
	}
	return true;
}

// How far above the interface this thread runs.
//
// One core, and at the full panel size a film takes most of it: decoding here,
// converting and drawing on the interface thread. Add an encoder for Bluetooth
// headphones (AAC takes a sixth of the core by itself) and there is not enough
// for everything. What gives way must be the picture: a frame shown late or not
// at all is a stutter, a gap in the sound is a click. So this thread, which
// carries the sound, gets the core when both want it, and the interface drops
// the frames it cannot convert in time (video_present).
//
// A nice level and not a real-time class: this thread sleeps only when it is
// ahead, and a file too heavy for the device would otherwise leave the
// interface no time at all -- not even to take the tap that closes it.
//
// And a mild one. A heavy frame is 50 ms of decoding in one go, and at -10 the
// interface gets a tenth of the core while that lasts: its own 13 ms of
// conversion then miss their slot, and one frame in six was dropped with a
// sixth of the core idle. At -4 it was one in a hundred, and the sound did not
// break either (800x450 at 25 a second, AAC to Bluetooth headphones).
#define WORKER_NICE (-4)

static void *video_worker(void *unused) {
	(void)unused;

	if (setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), WORKER_NICE) != 0) {
		perror("video: setpriority"); // not fatal: the host build may not be allowed
	}

	AVFormatContext *fmt = NULL;
	AVCodecContext *vdec = NULL, *adec = NULL;
	AVPacket *pkt = av_packet_alloc();
	AVFrame *frame = av_frame_alloc();
	bool audio_open = false;

	if (!pkt || !frame) {
		fail("video_no_memory");
		goto done;
	}

	if (avformat_open_input(&fmt, file_path, NULL, NULL) < 0 || avformat_find_stream_info(fmt, NULL) < 0) {
		fprintf(stderr, "video: %s does not open\n", file_path);
		fail("video_cannot_open");
		goto done;
	}

	const AVCodec *vcodec = NULL;
	int vs = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &vcodec, 0);
	if (vs < 0 || !vcodec) {
		fprintf(stderr, "video: %s has no picture this build decodes\n", file_path);
		fail("video_not_supported");
		goto done;
	}
	AVStream *vst = fmt->streams[vs];

	// Refused before a decoder is opened: at 720p the reference frames alone
	// are a tenth of this device's memory, and the result would be ten frames
	// a second.
	int vw = vst->codecpar->width, vh = vst->codecpar->height;
	if (!fits_upright(vw, vh) && !fits_turned(vw, vh)) {
		fprintf(stderr, "video: %dx%d is larger than the %dx%d panel\n", vw, vh, out_w, out_h);
		fail("video_too_large");
		goto done;
	}

	turned_flag = shown_turned(vw, vh);

	vdec = avcodec_alloc_context3(vcodec);
	if (!vdec || avcodec_parameters_to_context(vdec, vst->codecpar) < 0) {
		fail("video_no_memory");
		goto done;
	}
	vdec->thread_count = 1; // one core
	if (avcodec_open2(vdec, vcodec, NULL) < 0) {
		fail("video_not_supported");
		goto done;
	}

	// Sound is optional: a file without any, or with a codec this build does
	// not have, plays silently against the monotonic clock.
	int as = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, vs, NULL, 0);
	AVStream *ast = as >= 0 ? fmt->streams[as] : NULL;
	int rate = 0;
	if (ast) {
		const AVCodec *acodec = audio_decoder_for(ast->codecpar->codec_id);
		adec = acodec ? avcodec_alloc_context3(acodec) : NULL;
		if (adec && avcodec_parameters_to_context(adec, ast->codecpar) >= 0 && avcodec_open2(adec, acodec, NULL) >= 0 && adec->sample_rate > 0) {
			rate = adec->sample_rate;
			audio_open = audio_external_begin(rate, 2, 16);
		}
		if (!audio_open) {
			fprintf(stderr, "video: no sound (%s)\n", avcodec_get_name(ast->codecpar->codec_id));
			avcodec_free_context(&adec);
			as = -1;
		}
	}

	pthread_mutex_lock(&q_lock);
	have_audio = audio_open;
	pthread_mutex_unlock(&q_lock);

	int64_t start_ms = fmt->start_time != AV_NOPTS_VALUE ? fmt->start_time / 1000 : 0;
	duration_ms = fmt->duration > 0 ? fmt->duration / 1000 : 0;

	AVRational fr = vst->avg_frame_rate.num ? vst->avg_frame_rate : vst->r_frame_rate;
	printf("video: %s -- %s %dx%d %.2f fps, %s, %s\n", file_path, vcodec->name, vw, vh, fr.den ? av_q2d(fr) : 0.0, audio_open ? avcodec_get_name(ast->codecpar->codec_id) : "silent", shown_turned(vw, vh) ? "turned" : "upright");

	// Silence for the pause: a fiftieth of a second, written again and again.
	// It keeps the PCM open and running (reopening one takes half a second
	// here) and it is what paces the paused loop.
	int silence_frames = rate > 0 ? rate / 50 : 0;
	int16_t *silence = silence_frames ? calloc((size_t)silence_frames * 2, sizeof(int16_t)) : NULL;

	bool at_eof = false;
	bool input_done = false; // the file has been read to its end; packets may still be waiting
	int late_level = 0;		 // 0, or what the last packet's lateness asked to be dropped
	bool was_paused = false;
	bool prime = false;		   // decode up to one picture although paused
	int64_t audio_next_ms = 0; // where the next sound lands when it carries no timestamp
	int64_t warm_since = mono_ms();
	int64_t shed_since = 0;
	int shed = 0;
	int64_t shed_total_ms = 0; // spent dropping frames since the last line of the log
	int64_t video_next_ms = 0;
	int64_t stats_at = mono_ms();
	int in_hand = -1;

	// ---- pacing ----
	//
	// How much sound is in hand is asked of the card: what it has been given
	// and not yet played. On the wired outputs that is exactly the figure
	// wanted. A Bluetooth PCM answers with more: bluealsa adds the delay of
	// the link and of the receiver to what is waiting in its own FIFO, a
	// constant couple of hundred milliseconds, and taking that for sound in
	// hand means sleeping while the FIFO runs dry -- the sound breaks up.
	//
	// So a second figure is kept that no output can inflate: the sound written
	// since a known moment, less the time gone by since then. While the two
	// agree the card's answer is used, as it tracks the card's own clock. The
	// first time the card claims more than AUDIO_LEAD_MS when not half of that
	// has been written ahead of the clock, its answer is not what is in hand,
	// and from then on the count paces the film.
	//
	// The Bluetooth output says what it is, and is paced by count from the
	// first sample; the test above is for one that does not.
	bool pace_by_count = audio_open && audio_external_is_bluetooth();
	int64_t pace_since = 0;		// 0: not started
	int64_t pace_written = 0;	// frames written since then
	int64_t pace_credit_ms = 0; // sound already waiting at that moment

	while (!stop_flag) {
		// How it is going, every ten seconds while it is going: the difference
		// between "plays" and "plays, dropping a frame in five" is not visible
		// from anywhere else.
		if (!paused_flag && !at_eof && mono_ms() - stats_at >= STATS_MS) {
			stats_at = mono_ms();
			pthread_mutex_lock(&q_lock);
			unsigned shown = stat_shown, late = stat_late, overflow = stat_overflow;
			stat_shown = stat_late = stat_overflow = 0;
			int64_t at = clock_last_ms;
			pthread_mutex_unlock(&q_lock);
			if (shed) {
				shed_total_ms += stats_at - shed_since;
				shed_since = stats_at;
			}
			printf("video: at %lld s: %u frames shown, %u late, %u pushed out; sound in hand %d ms; frames dropped for the sound "
				   "%lld ms%s, for the picture %u packets; %d packets waiting\n",
				   (long long)(at / 1000), shown, late, overflow, in_hand, (long long)shed_total_ms,
				   shed == 2 ? " (key frames only now)" : shed == 1 ? " (B-frames now)" : "", stat_skipping, p_count);
			shed_total_ms = 0;
			stat_skipping = 0;
		}

		// ---- a seek ----
		if (seek_wanted) {
			pthread_mutex_lock(&seek_lock);
			int delta_s = seek_pending_s;
			int64_t to_ms = seek_to_ms;
			seek_pending_s = 0;
			seek_to_ms = -1;
			seek_wanted = false;
			pthread_mutex_unlock(&seek_lock);

			pthread_mutex_lock(&q_lock);
			int64_t target = (to_ms >= 0 ? to_ms : clock_now_locked()) + (int64_t)delta_s * 1000;
			pthread_mutex_unlock(&q_lock);
			if (duration_ms > 0 && target > duration_ms - 1000) {
				target = duration_ms - 1000;
			}
			if (target < 0) {
				target = 0;
			}

			if (av_seek_frame(fmt, -1, (target + start_ms) * 1000, AVSEEK_FLAG_BACKWARD) >= 0) {
				avcodec_flush_buffers(vdec);
				if (adec) {
					avcodec_flush_buffers(adec);
				}
				queue_clear();
				packets_clear();
				pthread_mutex_lock(&q_lock);
				clock_valid = false;
				clock_uncapped = false;
				clock_last_ms = target;
				show_next = true;
				pthread_mutex_unlock(&q_lock);
				at_eof = false;
				input_done = false;
				late_level = 0;
				ended_flag = false;
				prime = true;
				audio_next_ms = target;
				video_next_ms = target;
				pace_since = 0;
				warm_since = mono_ms();
				shed = 0;
				vdec->skip_frame = AVDISCARD_DEFAULT;
			}
		}

		// ---- paused ----
		bool paused = paused_flag;
		if (paused != was_paused) {
			was_paused = paused;
			if (paused) {
				clock_hold();
			} else {
				pace_since = 0;
				warm_since = mono_ms();
				if (!audio_open) {
					// No sound to bring the clock back: it starts again from
					// where it stopped.
					pthread_mutex_lock(&q_lock);
					int64_t at = clock_last_ms;
					pthread_mutex_unlock(&q_lock);
					clock_anchor(at, at);
				}
			}
		}
		if ((paused && !prime) || at_eof) {
			if (at_eof && !paused && queue_count() == 0 && (!audio_open || audio_external_delay_ms() <= 0)) {
				ended_flag = true;
			}
			// Silence while paused, and no more of it than of sound: whatever
			// is waiting in the card has to play out before the film is heard
			// again. Counting, it is written at the rate it is played: a
			// fiftieth of a second, then most of a fiftieth asleep.
			//
			// "At the rate it is played" has to be exact. A fiftieth of a
			// second written every 15 ms and a bit is a little faster than
			// that, and there is a megabyte of pipe to take the difference:
			// half a minute paused left seconds of silence queued, and on
			// resume the picture stood still behind it while the sound went
			// on. So it is written only while less is waiting in the pipe
			// than the film itself keeps there (or, for an output that
			// cannot show that, by its delay).
			if (audio_open && paused && silence && pace_by_count) {
				int queued = audio_external_queued_ms();
				if (queued >= 0 ? queued < AUDIO_LEAD_COUNTED_MS
								: audio_external_delay_ms() < AUDIO_LEAD_COUNTED_MS + PAUSE_SILENCE_OVER_MS) {
					audio_external_write(silence, silence_frames);
				}
				usleep(15 * 1000);
			} else if (audio_open && paused && silence && audio_external_delay_ms() < AUDIO_LEAD_MS) {
				audio_external_write(silence, silence_frames);
				audio_external_start();
			} else {
				usleep(20 * 1000);
			}
			continue;
		}

		// ---- video that is nearly due ----
		//
		// The packets at the front of the line go to the decoder when their
		// time, by the clock the picture is shown by, is DECODE_AHEAD_MS off.
		// Before the clock is running (a start, a seek) there is nothing to
		// go by, and a few are decoded so that there is a first picture. Two
		// at most in one pass: the sound has to be seen to in between.
		for (int pass = 0; pass < 2 && p_count > 0 && !stop_flag; pass++) {
			pthread_mutex_lock(&q_lock);
			bool running = clock_valid && !paused_flag;
			int64_t shown_at = clock_now_locked() - sync_offset_ms;
			int waiting = q_count;
			pthread_mutex_unlock(&q_lock);

			AVPacket *next = packets[p_head];
			int64_t due = stream_ms(vst, next->dts != AV_NOPTS_VALUE ? next->dts : next->pts, start_ms);
			bool crowded = p_count >= PACKETS_MAX - 4; // should not happen; better a frame early than a packet lost
			if (!crowded) {
				if (waiting >= QUEUE_MAX - 1) {
					break; // the display has not taken what there is
				}
				if (running ? due > shown_at + DECODE_AHEAD_MS : (!prime && waiting >= 3)) {
					break; // not yet
				}
			}

			// How late it is says how the decoder is doing: see LATE_SKIP_B_MS.
			int64_t late = running ? shown_at - due : 0;
			late_level = late > LATE_KEY_ONLY_MS ? 2 : late > LATE_SKIP_B_MS ? 1 : late < 0 ? 0 : late_level;
			int level = shed > late_level ? shed : late_level;
			vdec->skip_frame = level == 2 ? AVDISCARD_NONKEY : level == 1 ? AVDISCARD_NONREF : AVDISCARD_DEFAULT;
			if (late_level) {
				stat_skipping++;
			}

			avcodec_send_packet(vdec, next);
			av_packet_free(&packets[p_head]);
			p_head = (p_head + 1) % PACKETS_MAX;
			p_count--;
			if (!receive_pictures(vdec, frame, vst, start_ms, fr, &video_next_ms)) {
				goto done;
			}
			if (queue_count() > 0) {
				prime = false;
			}
			if (!audio_open) {
				// No sound: the clock starts at the first picture.
				pthread_mutex_lock(&q_lock);
				bool need_anchor = !clock_valid && !paused_flag && q_count > 0;
				int64_t first = need_anchor ? queue[q_head].pts_ms : 0;
				pthread_mutex_unlock(&q_lock);
				if (need_anchor) {
					clock_anchor(first, first);
				}
			}
		}

		// The file is read and every packet has been decoded: what the decoder
		// still holds comes out, and the loop waits above for the last frame.
		if (input_done && p_count == 0) {
			avcodec_send_packet(vdec, NULL);
			if (!receive_pictures(vdec, frame, vst, start_ms, fr, &video_next_ms)) {
				goto done;
			}
			at_eof = true;
			prime = false;
			continue;
		}

		// ---- one packet from the file ----
		//
		// Not when the file is finished, and not when there is nowhere to put
		// it. Without sound nothing else holds the reading back, so it stops
		// while there is half a second of video in the line.
		if (input_done || p_count >= PACKETS_MAX || (!audio_open && p_count >= 12)) {
			usleep(4 * 1000);
			continue;
		}
		int r = av_read_frame(fmt, pkt);
		if (r < 0) {
			// The end of the file. The clock stops being held to the sound,
			// which has no more to come.
			input_done = true;
			pthread_mutex_lock(&q_lock);
			clock_uncapped = true;
			pthread_mutex_unlock(&q_lock);
			continue;
		}
		if (pkt->stream_index == vs) {
			AVPacket *keep = av_packet_alloc();
			if (keep) {
				av_packet_move_ref(keep, pkt);
				packets[(p_head + p_count) % PACKETS_MAX] = keep;
				p_count++;
			}
		} else if (pkt->stream_index == as && !(paused && prime)) {
			avcodec_send_packet(adec, pkt);
			while (!stop_flag && avcodec_receive_frame(adec, frame) >= 0) {
				int n = to_s16_stereo(frame);
				int64_t pts = stream_ms(ast, frame->best_effort_timestamp, start_ms);
				if (pts < 0) {
					pts = audio_next_ms;
				}
				av_frame_unref(frame);
				if (n <= 0) {
					continue;
				}
				int64_t end = pts + (int64_t)n * 1000 / rate;
				audio_next_ms = end;

				audio_external_write(pcm_buf, n);

				int delay = audio_external_delay_ms();
				clock_anchor(end - (delay >= 0 ? delay : AUDIO_LEAD_MS), end);

				// What has been written ahead of the clock (see "pacing").
				// Started afresh, with whatever the card holds as credit,
				// after a start, a seek or a pause, and whenever the count
				// falls behind the clock: the decoder was late, and there is
				// no debt to work off.
				int64_t now = mono_ms();
				if (pace_since == 0) {
					pace_since = now;
					pace_written = 0;
					pace_credit_ms = !pace_by_count && delay > 0 ? delay : 0;
				}
				pace_written += n;
				int64_t ahead = pace_credit_ms + pace_written * 1000 / rate - (now - pace_since);
				if (ahead < 0) {
					pace_since = now;
					pace_written = 0;
					pace_credit_ms = 0;
					ahead = 0;
				}

				if (!pace_by_count && delay > AUDIO_LEAD_MS && ahead < AUDIO_LEAD_MS / 2) {
					pace_by_count = true;
					printf("video: the output reports %d ms waiting with %lld ms written ahead; pacing by count\n", delay, (long long)ahead);
				}
				in_hand = pace_by_count || delay < 0 ? (int)ahead : delay;
				// Better than the count, where the output can show what it
				// has not taken yet: that is what is in hand, whatever has
				// held the output up. Counting alone, a second of sound was
				// written while aplay was still starting, and stayed queued
				// ahead of every sample for the rest of the film.
				int queued = pace_by_count ? audio_external_queued_ms() : -1;
				if (queued >= 0) {
					in_hand = queued;
				}
				int lead = pace_by_count ? AUDIO_LEAD_COUNTED_MS : AUDIO_LEAD_MS;

				// The pacing: no further ahead of the DAC than AUDIO_LEAD_MS.
				// One write adds twenty-odd milliseconds, so this is never a
				// long sleep, and a stop or a seek is seen right after it.
				//
				// The card is started by hand the first time there is that
				// much: left to itself it waits for most of its buffer, which
				// at this pace never arrives (see audio_external_start).
				if (in_hand > lead) {
					audio_external_start();
					usleep((useconds_t)(in_hand - lead) * 1000);
				}

				// Losing the race? See SHED_BELOW_MS and SHED_COUNTED_BELOW_MS.
				int shed_below = pace_by_count ? SHED_COUNTED_BELOW_MS : SHED_BELOW_MS;
				int shed_above = pace_by_count ? SHED_COUNTED_ABOVE_MS : SHED_ABOVE_MS;
				int shed_hold = pace_by_count ? SHED_COUNTED_HOLD_MS : SHED_HOLD_MS;
				if (now - warm_since > SHED_WARMUP_MS) {
					if (in_hand < shed_below) {
						if (shed == 0) {
							shed = 1;
							shed_since = now;
						} else if (shed == 1 && now - shed_since > 2 * SHED_HOLD_MS) {
							shed_total_ms += now - shed_since;
							shed_since = now;
							shed = 2;
						}
					} else if (in_hand > shed_above && shed && now - shed_since > shed_hold) {
						shed_total_ms += now - shed_since;
						shed = 0;
					}
				}
			}
		}
		av_packet_unref(pkt);
	}

	free(silence);

done:
	if (audio_open) {
		audio_external_end();
	}
	queue_clear();
	packets_clear();
	av_frame_free(&frame);
	av_packet_free(&pkt);
	avcodec_free_context(&vdec);
	avcodec_free_context(&adec);
	avformat_close_input(&fmt);
	running_flag = false;
	return NULL;
}

// ---------------------------------------------------------------------------
// the page's side
// ---------------------------------------------------------------------------

void video_set_output(int width, int height) {
	if (width > 0 && height > 0 && !running_flag) {
		out_w = width;
		out_h = height;
	}
}

bool video_running(void) { return running_flag; }
bool video_failed(void) { return failed_flag; }
const char *video_error(void) { return error_key; }
bool video_ended(void) { return ended_flag; }
bool video_paused(void) { return paused_flag; }
bool video_turned(void) { return turned_flag; }
void video_set_paused(bool paused) { paused_flag = paused; }
int64_t video_duration_ms(void) { return duration_ms; }

int64_t video_position_ms(void) {
	pthread_mutex_lock(&q_lock);
	int64_t at = clock_now_locked();
	pthread_mutex_unlock(&q_lock);
	return at < 0 ? 0 : at;
}

void video_seek_to(int64_t ms) {
	pthread_mutex_lock(&seek_lock);
	seek_to_ms = ms < 0 ? 0 : ms;
	seek_pending_s = 0;
	seek_wanted = true;
	pthread_mutex_unlock(&seek_lock);
}

void video_seek_relative(int seconds) {
	pthread_mutex_lock(&seek_lock);
	seek_pending_s += seconds;
	seek_wanted = true;
	pthread_mutex_unlock(&seek_lock);
}

bool video_start(const char *path, int64_t start_ms) {
	if (!path || !path[0] || running_flag) {
		return false;
	}

	// A previous thread may have ended on its own without being joined: a file
	// that does not open returns at once.
	if (thread_live) {
		pthread_join(worker, NULL);
		thread_live = false;
	}

	av_log_set_level(AV_LOG_ERROR);

	// Black, and it stays black wherever the picture does not reach.
	free(screen);
	screen = calloc((size_t)out_w * out_h, sizeof(uint16_t));
	if (!screen) {
		fail("video_no_memory");
		return false;
	}

	snprintf(file_path, sizeof(file_path), "%s", path);

	pthread_mutex_lock(&q_lock);
	clock_valid = false;
	clock_uncapped = false;
	clock_last_ms = 0;
	have_audio = false;
	stat_shown = stat_late = stat_overflow = 0;
	show_next = true; // the first picture goes up as soon as it exists
	pthread_mutex_unlock(&q_lock);

	// Starting part way in is a seek from zero, asked for before the thread
	// has read anything: it is the first thing the loop does.
	pthread_mutex_lock(&seek_lock);
	seek_to_ms = -1;
	seek_pending_s = start_ms > 0 ? (int)(start_ms / 1000) : 0;
	seek_wanted = seek_pending_s > 0;
	pthread_mutex_unlock(&seek_lock);

	tables_ready = false;
	turned_flag = false;
	duration_ms = 0;
	paused_flag = false;
	ended_flag = false;
	failed_flag = false;
	stop_flag = false;
	running_flag = true;

	if (pthread_create(&worker, NULL, video_worker, NULL) != 0) {
		fprintf(stderr, "video: the thread does not start\n");
		running_flag = false;
		fail("video_no_memory");
		return false;
	}
	thread_live = true;
	return true;
}

void video_stop(void) {
	if (thread_live) {
		stop_flag = true;
		paused_flag = false;
		pthread_join(worker, NULL);
		thread_live = false;
	}
	running_flag = false;

	// The thread has ended, so nothing writes these; that nothing reads the
	// image is the caller's job (see video.h).
	av_frame_free(&held);
	free(screen);
	screen = NULL;
	free(pcm_buf);
	pcm_buf = NULL;
	pcm_cap = 0;
}
