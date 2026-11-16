#include "aout.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/wait.h>

#include <tinyalsa/mixer.h>
#include <tinyalsa/pcm.h>

extern char **environ;

// The card and device CompasOS plays through on these players.
#define CARD 0
#define DEVICE 0

// Four periods, as CompasOS opens it: on the R1 two and three are refused by
// the driver whatever the period size. 2048 frames is 46 ms at 44.1 kHz.
#define PERIOD_FRAMES 2048
#define PERIOD_COUNT 4

// What aplay is told to buffer before it starts playing, and how long each of
// its periods is.
#define BT_APLAY_BUFFER_MS 200
#define BT_APLAY_PERIOD_MS 100

// The link's own delay -- encoder, radio, the headphones' buffer -- when
// BlueALSA does not say: a usual figure for AAC and SBC.
#define BT_LINK_DELAY_MS 250

// How many times aplay is started again when it goes away in the middle of a
// film.
#define BT_RETRIES 5

static struct pcm *card;
static int card_shift; // 16-bit samples go out in 32-bit words, shifted left by this
static int32_t *wide;
static int wide_cap;

static pid_t bt_pid = -1;
static int bt_fd = -1;
static int64_t bt_since_ms;
static int64_t bt_written;
// What lies between a sample being written and being heard, beyond what the
// count of written against elapsed knows: aplay's wait before it starts and
// the link's delay. The picture is held back by it so lips and voice meet.
static int bt_latency_ms = BT_LINK_DELAY_MS;
// What the count of written against elapsed does not know, in 1/64 ms: see
// audio_external_delay_ms().
static int64_t bt_correction_64;
static bool bt_correction_known;
static int64_t bt_said_ms; // when the figures were last put in the log
// When the link's delay is to be asked for again. Asked before any sound has
// gone out BlueALSA answers with a figure for an idle link (50 ms where the
// running one was 280), and the picture ran a quarter of a second ahead of the
// voices: so it is asked again once the sound is flowing, and now and then
// after that.
static int64_t bt_ask_ms;
#define BT_ASK_FIRST_MS 2500
#define BT_ASK_EVERY_MS 60000

static int rate;
static bool on_bluetooth;
static int64_t bt_retry_ms; // when aplay was last started again
static int bt_retries;

static int64_t mono_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// The BlueALSA PCM headphones are played through, as a D-Bus path:
// `bluealsactl list-pcms` prints one path per PCM, and the one for sound going
// out ends in /a2dpsrc/sink. Empty when no headphones are connected.
static char bt_path[256];

static bool bluetooth_find(void) {
	bt_path[0] = '\0';
	FILE *p = popen("bluealsactl list-pcms 2>/dev/null", "r");
	if (!p) {
		return false;
	}
	char line[256];
	while (fgets(line, sizeof(line), p)) {
		if (strstr(line, "a2dpsrc/sink")) {
			line[strcspn(line, "\r\n")] = '\0';
			snprintf(bt_path, sizeof(bt_path), "%s", line);
		}
	}
	pclose(p);
	return bt_path[0] != '\0';
}

// One number out of `bluealsactl <what> <path>`: the first after `label`.
static int bluetooth_query(const char *what, const char *label) {
	char cmd[320];
	snprintf(cmd, sizeof(cmd), "bluealsactl %s %s 2>/dev/null", what, bt_path);
	FILE *p = popen(cmd, "r");
	if (!p) {
		return -1;
	}
	char line[256];
	int value = -1;
	while (fgets(line, sizeof(line), p)) {
		const char *at = strstr(line, label);
		if (at && value < 0) {
			value = atoi(at + strlen(label));
		}
	}
	pclose(p);
	return value;
}

// The name of the ALSA PCM aplay is given, and the file that defines it.
//
// The headphones' link runs at one rate, settled when they connected. Opening
// the plain "bluealsa" PCM at any other makes BlueALSA take the link down and
// build it again at the new rate: the PCM aplay had open is gone ("No such
// device"), aplay exits, and the film plays on with no sound. So the PCM
// opened is one pinned to the rate the link already has, with ALSA's own
// converter in front of it -- what CompasOS does for music. It is defined in
// an .asoundrc in the app's data folder, which aplay reads because that folder
// is given to it as its home.
#define BT_PCM_NAME "compasvideo"

static bool bluetooth_write_config(const char *home, int link_rate) {
	// .../dev_20_18_5B_4C_00_96/a2dpsrc/sink -> 20:18:5B:4C:00:96
	const char *dev = strstr(bt_path, "/dev_");
	char mac[18];
	if (!dev || strlen(dev + 5) < 17) {
		return false;
	}
	memcpy(mac, dev + 5, 17);
	mac[17] = '\0';
	for (char *c = mac; *c; c++) {
		if (*c == '_') {
			*c = ':';
		}
	}

	char path[600];
	snprintf(path, sizeof(path), "%s/.asoundrc", home);
	FILE *f = fopen(path, "w");
	if (!f) {
		return false;
	}
	fprintf(f,
			"pcm.%s {\n\ttype plug\n\tslave {\n\t\tpcm {\n\t\t\ttype bluealsa\n\t\t\tdevice \"%s\"\n"
			"\t\t\tprofile \"a2dp\"\n\t\t}\n\t\trate %d\n\t\tchannels 2\n\t}\n}\n",
			BT_PCM_NAME, mac, link_rate);
	return fclose(f) == 0;
}

static bool bluetooth_open(void) {
	const char *home = getenv("COMPAS_APP_DATA");
	if (!home || !home[0]) {
		home = ".";
	}
	int link_rate = bluetooth_query("info", "Rate:");
	if (link_rate <= 0 || !bluetooth_write_config(home, link_rate)) {
		fprintf(stderr, "aout: the Bluetooth link's rate is not known, or its PCM cannot be described\n");
		return false;
	}

	int fds[2];
	if (pipe(fds) != 0) {
		return false;
	}

	// BlueALSA reports the link's delay when the headphones tell it (A2DP 1.3
	// and later do): "Delay: 280.1 ms".
	int link_delay = bluetooth_query("info", "Delay:");
	if (link_delay <= 0 || link_delay > 2000) {
		link_delay = BT_LINK_DELAY_MS;
	}
	bt_latency_ms = link_delay;
	bt_correction_known = false;

	char rate_text[16], period_text[40], buffer_text[40];
	snprintf(rate_text, sizeof(rate_text), "%d", rate);
	// 100 ms periods, as CompasOS asks for: they divide evenly at 44.1 and
	// 48 kHz, which keeps the converter's tables small. And a short buffer:
	// aplay does not start until it is full, and every millisecond of that is
	// a millisecond the picture has to be held back.
	snprintf(period_text, sizeof(period_text), "--period-time=%d", BT_APLAY_PERIOD_MS * 1000);
	snprintf(buffer_text, sizeof(buffer_text), "--buffer-time=%d", BT_APLAY_BUFFER_MS * 1000);
	char *argv[] = {"aplay", "-q", "-D", BT_PCM_NAME, "-t", "raw", "-f", "S16_LE", "-r", rate_text, "-c", "2",
					period_text, buffer_text, NULL};

	// The environment as it is, with HOME pointing at the .asoundrc above.
	char home_var[620];
	snprintf(home_var, sizeof(home_var), "HOME=%s", home);
	size_t n = 0;
	while (environ[n]) {
		n++;
	}
	char **env = calloc(n + 2, sizeof(*env));
	if (!env) {
		close(fds[0]);
		close(fds[1]);
		return false;
	}
	size_t used = 0;
	for (size_t i = 0; i < n; i++) {
		if (strncmp(environ[i], "HOME=", 5) != 0) {
			env[used++] = environ[i];
		}
	}
	env[used++] = home_var;

	posix_spawn_file_actions_t fa;
	posix_spawn_file_actions_init(&fa);
	posix_spawn_file_actions_adddup2(&fa, fds[0], STDIN_FILENO);
	posix_spawn_file_actions_addclose(&fa, fds[0]);
	posix_spawn_file_actions_addclose(&fa, fds[1]);
	int rc = posix_spawnp(&bt_pid, "aplay", &fa, NULL, argv, env);
	posix_spawn_file_actions_destroy(&fa);
	free(env);
	close(fds[0]);
	if (rc != 0) {
		close(fds[1]);
		bt_pid = -1;
		fprintf(stderr, "aout: aplay does not start: %s\n", strerror(rc));
		return false;
	}

	// A large pipe. aplay takes its sound a period at a time, and with a small
	// one every write waits for the next of those: the decoder then gets the
	// core in 100 ms lumps and frames arrive late in threes (a third of them
	// were dropped that way). With room in the pipe a write returns at once
	// and the writer keeps its own time, by count.
	fcntl(fds[1], F_SETPIPE_SZ, 1 << 20);
	bt_fd = fds[1];
	bt_since_ms = 0;
	bt_written = 0;
	bt_ask_ms = mono_ms() + BT_ASK_FIRST_MS;
	printf("aout: Bluetooth, through aplay, %d Hz into a %d Hz link, %d ms behind\n", rate, link_rate, bt_latency_ms);
	return true;
}

static void bluetooth_close(void) {
	if (bt_fd >= 0) {
		close(bt_fd);
		bt_fd = -1;
	}
	if (bt_pid > 0) {
		// It would play out what it still holds; the film has moved on.
		kill(bt_pid, SIGTERM);
		waitpid(bt_pid, NULL, 0);
		bt_pid = -1;
	}
}

// The card takes 16 bits on some of these players and only 24 or 32 on others,
// and tinyalsa converts nothing: the formats are tried in turn and the samples
// widened here when it comes to that.
static bool card_open(void) {
	static const struct {
		enum pcm_format format;
		int shift;
	} tries[] = {{PCM_FORMAT_S16_LE, 0}, {PCM_FORMAT_S24_LE, 8}, {PCM_FORMAT_S32_LE, 16}};

	for (size_t i = 0; i < sizeof(tries) / sizeof(tries[0]); i++) {
		struct pcm_config config;
		memset(&config, 0, sizeof(config));
		config.channels = 2;
		config.rate = (unsigned)rate;
		config.format = tries[i].format;
		config.period_size = PERIOD_FRAMES;
		config.period_count = PERIOD_COUNT;
		// One period and it plays: the writer keeps a fifth of a second
		// waiting, not the whole buffer.
		config.start_threshold = PERIOD_FRAMES;
		config.stop_threshold = PERIOD_FRAMES * PERIOD_COUNT;

		struct pcm *pcm = pcm_open(CARD, DEVICE, PCM_OUT, &config);
		if (pcm && pcm_is_ready(pcm)) {
			card = pcm;
			card_shift = tries[i].shift;
			printf("aout: card %d,%d at %d Hz, %s\n", CARD, DEVICE, rate,
				   tries[i].shift == 0 ? "16 bit" : tries[i].shift == 8 ? "24 bit" : "32 bit");
			return true;
		}
		fprintf(stderr, "aout: card refuses format %d: %s\n", (int)tries[i].format, pcm ? pcm_get_error(pcm) : "no memory");
		if (pcm) {
			pcm_close(pcm);
		}
	}
	return false;
}

bool audio_external_begin(int sample_rate, int channels, int bits) {
	if (channels != 2 || bits != 16 || sample_rate <= 0) {
		return false;
	}
	rate = sample_rate;

	on_bluetooth = bluetooth_find() && bluetooth_open();
	return on_bluetooth || card_open();
}

bool audio_external_is_bluetooth(void) { return on_bluetooth; }

int audio_external_write(const void *frames, int count) {
	if (count <= 0) {
		return 0;
	}

	if (on_bluetooth && bt_fd < 0) {
		// aplay went away under the film (below). Another, a few times and
		// not more often than twice a second: the link may only have been
		// rebuilt, or the headphones may be off for good.
		if (bt_retries >= BT_RETRIES || mono_ms() - bt_retry_ms < 500) {
			return 0;
		}
		bt_retry_ms = mono_ms();
		bt_retries++;
		if (!bluetooth_find() || !bluetooth_open()) {
			return 0;
		}
	}
	if (bt_fd >= 0) {
		const char *p = frames;
		size_t left = (size_t)count * 4;
		while (left > 0) {
			ssize_t n = write(bt_fd, p, left);
			if (n < 0) {
				if (errno == EINTR) {
					continue;
				}
				// aplay has gone: the headphones were switched off, or the
				// link was rebuilt under it. What it said is in the log.
				fprintf(stderr, "aout: aplay is gone (%s)\n", strerror(errno));
				bluetooth_close();
				return 0;
			}
			p += n;
			left -= (size_t)n;
		}
		if (bt_since_ms == 0) {
			bt_since_ms = mono_ms();
		}
		bt_written += count;

		if (mono_ms() >= bt_ask_ms) {
			bt_ask_ms = mono_ms() + BT_ASK_EVERY_MS;
			int link_delay = bluetooth_query("info", "Delay:");
			if (link_delay > 0 && link_delay <= 2000) {
				int latency = link_delay;
				if (latency != bt_latency_ms) {
					printf("aout: the link says %d ms; sound is %d ms behind\n", link_delay, latency);
					bt_latency_ms = latency;
				}
			}
		}
		return count;
	}

	if (!card) {
		return 0;
	}

	const void *out = frames;
	if (card_shift) {
		if (count > wide_cap) {
			int32_t *grown = realloc(wide, (size_t)count * 2 * sizeof(int32_t));
			if (!grown) {
				return 0;
			}
			wide = grown;
			wide_cap = count;
		}
		const int16_t *in = frames;
		for (int i = 0; i < count * 2; i++) {
			wide[i] = (int32_t)in[i] << card_shift;
		}
		out = wide;
	}

	for (int attempt = 0; attempt < 3; attempt++) {
		int n = pcm_writei(card, out, (unsigned)count);
		if (n >= 0) {
			return n;
		}
		// Ran dry: the decoder was late. Prepare and write again.
		pcm_prepare(card);
	}
	return 0;
}

int audio_external_delay_ms(void) {
	if (on_bluetooth) {
		if (bt_fd < 0 || bt_since_ms == 0) {
			return bt_latency_ms;
		}
		int64_t waiting = bt_written * 1000 / rate - (mono_ms() - bt_since_ms);
		if (waiting < 0) {
			// The writer fell behind and the headphones went silent for a
			// moment: the count starts again from here.
			bt_since_ms = mono_ms();
			bt_written = 0;
			waiting = 0;
		}

		// The count is smooth, and blind. It takes aplay to have been playing
		// since the first write, when aplay first has to start, open the link
		// and fill its own buffer -- and whatever was written in that time is
		// still ahead of the sample being played, for the rest of the film.
		// How much that is has differed by 300 ms from one arrangement of the
		// buffers to the next, and the viewer's correction had to follow it
		// each time.
		//
		// What can be seen is what waits in the pipe. With sound always
		// waiting there, aplay's buffer behind it is full, give or take the
		// period it is refilled by: so the pipe, plus that buffer, is what
		// really lies ahead. It is seen in steps of a period, too coarse to
		// time the picture by, so it only corrects the count, slowly: the
		// difference between the two, averaged over a second or so.
		int pipe_bytes = 0;
		if (ioctl(bt_fd, FIONREAD, &pipe_bytes) == 0) {
			int64_t ahead = (int64_t)pipe_bytes * 1000 / (rate * 4) + BT_APLAY_BUFFER_MS - BT_APLAY_PERIOD_MS / 2;
			int64_t off_64 = (ahead - waiting) * 64;
			if (!bt_correction_known) {
				bt_correction_64 = off_64;
				bt_correction_known = true;
			} else {
				bt_correction_64 += (off_64 - bt_correction_64) / 64;
			}
			if (mono_ms() - bt_said_ms > 30000) {
				bt_said_ms = mono_ms();
				printf("aout: %lld ms counted, %d ms in the pipe, correction %lld ms, link %d ms\n", (long long)waiting,
					   (int)((int64_t)pipe_bytes * 1000 / (rate * 4)), (long long)(bt_correction_64 / 64), bt_latency_ms);
			}
		}
		return (int)(waiting + bt_correction_64 / 64) + bt_latency_ms;
	}
	if (!card) {
		return -1;
	}
	long frames = pcm_get_delay(card);
	return frames < 0 ? -1 : (int)(frames * 1000 / rate);
}

int audio_external_queued_ms(void) {
	int pipe_bytes = 0;
	if (!on_bluetooth || bt_fd < 0 || ioctl(bt_fd, FIONREAD, &pipe_bytes) != 0) {
		return -1;
	}
	return (int)((int64_t)pipe_bytes * 1000 / (rate * 4));
}

void audio_external_start(void) {
	if (card) {
		pcm_start(card); // refused, harmlessly, when it is already running
	}
}

void audio_external_end(void) {
	bluetooth_close();
	on_bluetooth = false;
	bt_retries = 0;
	if (card) {
		pcm_close(card);
		card = NULL;
	}
	free(wide);
	wide = NULL;
	wide_cap = 0;
}

// ---------------------------------------------------------------------------
// volume
// ---------------------------------------------------------------------------
//
// CompasOS reads no buttons while an app runs, so the volume keys are the
// app's to act on. Each output has its own place for it.
//
// The card: the DAC's two "Playback Volume" controls, which count attenuation
// in half decibels -- 0 is the loudest, VOLUME_SILENT is silence. CompasOS
// keeps its own idea of the level and does not read the DAC back, so what the
// film changed is put back when the app closes.
//
// Bluetooth: the BlueALSA PCM's volume, 0 to 127, set with bluealsactl.
// CompasOS watches that one and follows it, so it is left where it ends up.

#define VOLUME_SILENT 230
#define VOLUME_STEP 4 // 2 dB
#define BT_VOLUME_MAX 127
#define BT_VOLUME_STEP 6

static struct mixer *mixer;
static int volume_at_start = -1;

static struct mixer_ctl *volume_control(const char *name) {
	if (!mixer) {
		mixer = mixer_open(CARD);
	}
	return mixer ? mixer_get_ctl_by_name(mixer, name) : NULL;
}

int audio_volume_step(int direction) {
	if (on_bluetooth) {
		if (!bt_path[0]) {
			return -1;
		}
		int now = bluetooth_query("volume", "Volume:");
		if (now < 0) {
			return -1;
		}
		int want = now + direction * BT_VOLUME_STEP;
		want = want < 0 ? 0 : want > BT_VOLUME_MAX ? BT_VOLUME_MAX : want;
		if (want != now) {
			char cmd[340];
			snprintf(cmd, sizeof(cmd), "bluealsactl volume %s %d %d >/dev/null 2>&1", bt_path, want, want);
			if (system(cmd) != 0) {
				return -1;
			}
		}
		return want * 100 / BT_VOLUME_MAX;
	}

	struct mixer_ctl *left = volume_control("Left Playback Volume");
	struct mixer_ctl *right = volume_control("Right Playback Volume");
	if (!left) {
		return -1;
	}
	int now = mixer_ctl_get_value(left, 0);
	if (now < 0) {
		return -1;
	}
	if (volume_at_start < 0) {
		volume_at_start = now;
	}
	int want = now - direction * VOLUME_STEP; // louder is less attenuation
	want = want < 0 ? 0 : want > VOLUME_SILENT ? VOLUME_SILENT : want;
	if (want != now) {
		mixer_ctl_set_value(left, 0, want);
		if (right) {
			mixer_ctl_set_value(right, 0, want);
		}
	}
	return (VOLUME_SILENT - want) * 100 / VOLUME_SILENT;
}

void audio_volume_restore(void) {
	if (volume_at_start >= 0) {
		struct mixer_ctl *left = volume_control("Left Playback Volume");
		struct mixer_ctl *right = volume_control("Right Playback Volume");
		if (left) {
			mixer_ctl_set_value(left, 0, volume_at_start);
		}
		if (right) {
			mixer_ctl_set_value(right, 0, volume_at_start);
		}
		volume_at_start = -1;
	}
	if (mixer) {
		mixer_close(mixer);
		mixer = NULL;
	}
}
