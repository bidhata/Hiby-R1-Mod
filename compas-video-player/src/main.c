// Video, an external app for CompasOS.
//
// Started from the Apps list it shows what is in the card's Video folder;
// started with a file (a plugin's plugin.launch_app) it plays that file and
// quits when it ends. Either way it owns the screen, the glass, the buttons
// and the sound until it exits: CompasOS waits, and reads none of them.

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include <linux/input.h>
#include <sys/resource.h>

#include "aout.h"
#include "draw.h"
#include "screen.h"
#include "touch.h"
#include "video.h"
#include "videoresume.h"

// The card's Video folder. The app is started in its own folder,
// <card>/.apps/video, so the card is two levels up.
#define VIDEO_DIR "../../Video"

#define WHITE RGB565(255, 255, 255)
#define GREY RGB565(150, 150, 155)
#define BLACK RGB565(0, 0, 0)
#define PAGE_BG RGB565(24, 26, 28)
#define ROW_BG RGB565(44, 46, 50)

#define DISPLAY_NICE (-8)

// A finger that came down and went up again within this many pixels was a
// tap; further, it was dragging.
#define TAP_SLOP 24

static int panel_w, panel_h;

static uint32_t now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

// ---------------------------------------------------------------------------
// the film
// ---------------------------------------------------------------------------

// How long the controls stay up while the film is running. Paused, they stay.
#define CONTROLS_SHOWN_MS 3000
// And the volume bar, after a press of a volume key.
#define VOLUME_SHOWN_MS 1500

// A film left paused has the screen switched off after this long, exactly as
// the power key would: the app keeps the backlight up for as long as it is
// open (screen.c), CompasOS's own timeout does not run under an app, and a
// paused film forgotten on the table would otherwise light the panel until
// the battery was flat. Any key or touch brings the screen back.
#define PAUSED_SCREEN_OFF_MS (2 * 60 * 1000)
#define SEEK_STEP_S 10

// Carrying on where a film was left: a little before, to pick up the thread.
// A film left in its first half minute or its last twentieth (ten seconds at
// least) was not left part way through, and opens from the beginning.
#define RESUME_BACK_MS 5000
#define RESUME_MIN_MS 30000
#define RESUME_END_MIN_MS 10000

#define BTN_R 46
#define BTN_BIG_R 58
#define BTN_SPREAD 170
#define EDGE 26

typedef enum { HIT_NONE, HIT_PLAY, HIT_BACK, HIT_FORWARD, HIT_CLOSE, HIT_SYNC_LESS, HIT_SYNC_MORE } hit_t;

// ---------------------------------------------------------------------------
// sound against picture
// ---------------------------------------------------------------------------
//
// How long a sample takes from the app to the ear is known for the sound card
// and only roughly for Bluetooth: the headphones buffer for themselves and do
// not all say how much. So there is a correction the viewer sets by eye, in
// the top right of the controls, one for each kind of output and remembered
// between films (sync.txt in the app's data folder). More holds the picture
// back, for sound that arrives late; less shows it sooner.

#define SYNC_STEP_MS 50
#define SYNC_MAX_MS 1000

// Where the correction starts for Bluetooth, until the viewer sets their own:
// what looked right by eye on a HiBy R1 with AAC headphones (HUAWEI FreeLace
// Pro 2), on top of the delay aout.c already allows for. Other headphones
// will want another figure, which is what the control is for.
#define SYNC_BLUETOOTH_DEFAULT_MS 150

static int sync_ms[2] = {0, SYNC_BLUETOOTH_DEFAULT_MS}; // the card, Bluetooth
static char sync_path[600];

static void sync_load(const char *dir) {
	snprintf(sync_path, sizeof(sync_path), "%s/sync.txt", dir);
	FILE *f = fopen(sync_path, "r");
	if (f) {
		int card, bluetooth;
		if (fscanf(f, "%d %d", &card, &bluetooth) == 2) {
			sync_ms[0] = card;
			sync_ms[1] = bluetooth;
		}
		fclose(f);
	}
	for (int i = 0; i < 2; i++) {
		sync_ms[i] = sync_ms[i] < -SYNC_MAX_MS ? -SYNC_MAX_MS : sync_ms[i] > SYNC_MAX_MS ? SYNC_MAX_MS : sync_ms[i];
	}
}

static void sync_save(void) {
	FILE *f = fopen(sync_path, "w");
	if (f) {
		fprintf(f, "%d %d\n", sync_ms[0], sync_ms[1]);
		fclose(f);
	}
}

static int *sync_current(void) { return &sync_ms[audio_external_is_bluetooth() ? 1 : 0]; }

static void sync_step(int direction) {
	int *value = sync_current();
	int want = *value + direction * SYNC_STEP_MS;
	*value = want < -SYNC_MAX_MS ? -SYNC_MAX_MS : want > SYNC_MAX_MS ? SYNC_MAX_MS : want;
	video_set_sync_offset(*value);
	sync_save();
}

// The two small buttons and the figure between them, top right.
#define SYNC_R 30
static void sync_centres(int *less_x, int *more_x, int *y) {
	*more_x = draw_width() - EDGE - SYNC_R;
	*less_x = *more_x - 190;
	*y = EDGE + SYNC_R;
}

static void format_time(char *out, size_t size, int64_t ms) {
	long s = (long)(ms / 1000);
	if (s >= 3600) {
		snprintf(out, size, "%ld:%02ld:%02ld", s / 3600, s / 60 % 60, s % 60);
	} else {
		snprintf(out, size, "%ld:%02ld", s / 60, s % 60);
	}
}

// Where the controls are, in the viewer's coordinates, for drawing them and
// for finding which one a tap landed on.
static void control_centres(int *cx, int *cy) {
	*cx = draw_width() / 2;
	*cy = draw_height() / 2;
}

static hit_t control_at(int vx, int vy) {
	int cx, cy;
	control_centres(&cx, &cy);
	struct {
		int x, y, r;
		hit_t hit;
	} buttons[] = {
		{cx, cy, BTN_BIG_R, HIT_PLAY},
		{cx - BTN_SPREAD, cy, BTN_R, HIT_BACK},
		{cx + BTN_SPREAD, cy, BTN_R, HIT_FORWARD},
		{EDGE + BTN_R, EDGE + BTN_R, BTN_R, HIT_CLOSE},
		{0, 0, SYNC_R, HIT_SYNC_LESS},
		{0, 0, SYNC_R, HIT_SYNC_MORE},
	};
	sync_centres(&buttons[4].x, &buttons[5].x, &buttons[4].y);
	buttons[5].y = buttons[4].y;
	for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
		// A fingertip is wider than the drawing: half as much again.
		int dx = vx - buttons[i].x, dy = vy - buttons[i].y, reach = buttons[i].r * 3 / 2;
		if (dx * dx + dy * dy <= reach * reach) {
			return buttons[i].hit;
		}
	}
	return HIT_NONE;
}

// The strip along the bottom: the time so far, the bar, the film's length.
// One place works out where they are, for drawing them and for the finger.
typedef struct {
	int band_y, band_h; // the strip itself
	int text_y;
	int bar_x, bar_y, bar_w;
	int right_w;
} strip_t;

static strip_t strip_layout(const char *left, const char *right) {
	strip_t g;
	int w = draw_width(), h = draw_height();
	g.band_h = draw_line_height() + 30;
	g.band_y = h - g.band_h;
	g.text_y = g.band_y + 6;
	int left_w = draw_text_width(left);
	g.right_w = draw_text_width(right);
	g.bar_x = EDGE + left_w + 16;
	g.bar_w = w - 2 * EDGE - left_w - g.right_w - 32;
	g.bar_y = g.text_y + draw_line_height() / 2 - 3;
	return g;
}

// A finger on the bar: where in the film it points, or -1 when it is not on
// the strip at all. The strip is taken a little taller than it is drawn; the
// bar itself is six pixels.
#define STRIP_REACH 28
static int64_t strip_position(int vx, int vy) {
	int64_t total = video_duration_ms();
	char left[24], right[24];
	// The widest the time so far can be is the film's length: laid out with
	// that, the bar does not shift under the finger as the figure changes.
	snprintf(right, sizeof(right), "%s", "");
	{
		long sec = (long)(total / 1000);
		if (sec >= 3600) {
			snprintf(right, sizeof(right), "%ld:%02ld:%02ld", sec / 3600, sec / 60 % 60, sec % 60);
		} else {
			snprintf(right, sizeof(right), "%ld:%02ld", sec / 60, sec % 60);
		}
	}
	snprintf(left, sizeof(left), "%s", right);
	strip_t g = strip_layout(left, right);
	if (total <= 0 || g.bar_w <= 20 || vy < g.band_y - STRIP_REACH) {
		return -1;
	}
	int x = vx < g.bar_x ? g.bar_x : vx > g.bar_x + g.bar_w ? g.bar_x + g.bar_w : vx;
	return total * (x - g.bar_x) / g.bar_w;
}

// Two triangles pointing the way the film will jump.
static void draw_skip(int cx, int cy, int dir) {
	for (int k = 0; k < 2; k++) {
		int x = cx + dir * (k * 18 - 16);
		draw_triangle(x, cy - 18, x, cy + 18, x + dir * 20, cy, WHITE);
	}
}

// `scrub_ms` is where a finger on the bar is pointing, or -1: the bar and the
// time then show that place and not where the film is.
static void draw_controls(int64_t scrub_ms) {
	int cx, cy;
	control_centres(&cx, &cy);

	draw_dim_disc(cx, cy, BTN_BIG_R);
	if (video_paused()) {
		draw_triangle(cx - 16, cy - 26, cx - 16, cy + 26, cx + 28, cy, WHITE);
	} else {
		draw_rect(cx - 20, cy - 24, 14, 48, WHITE);
		draw_rect(cx + 6, cy - 24, 14, 48, WHITE);
	}

	draw_dim_disc(cx - BTN_SPREAD, cy, BTN_R);
	draw_skip(cx - BTN_SPREAD, cy, -1);
	draw_dim_disc(cx + BTN_SPREAD, cy, BTN_R);
	draw_skip(cx + BTN_SPREAD, cy, 1);

	int qx = EDGE + BTN_R, qy = EDGE + BTN_R;
	draw_dim_disc(qx, qy, BTN_R);
	draw_line(qx - 16, qy - 16, qx + 16, qy + 16, 5, WHITE);
	draw_line(qx - 16, qy + 16, qx + 16, qy - 16, 5, WHITE);

	// Top right: sound against picture, a minus, the figure and a plus.
	int less_x, more_x, sy;
	sync_centres(&less_x, &more_x, &sy);
	draw_dim_disc(less_x, sy, SYNC_R);
	draw_rect(less_x - 12, sy - 2, 24, 5, WHITE);
	draw_dim_disc(more_x, sy, SYNC_R);
	draw_rect(more_x - 12, sy - 2, 24, 5, WHITE);
	draw_rect(more_x - 2, sy - 12, 5, 24, WHITE);
	char sync_text[32];
	snprintf(sync_text, sizeof(sync_text), "%+d ms", *sync_current());
	int text_w = draw_text_width(sync_text);
	int mid = (less_x + more_x) / 2;
	draw_dim_rect(mid - text_w / 2 - 8, sy - draw_line_height() / 2 - 2, text_w + 16, draw_line_height() + 4);
	draw_text(mid - text_w / 2, sy - draw_line_height() / 2, sync_text, WHITE, 160);

	// Along the bottom: where the film is, how long it is, and a bar. The
	// space for the time so far is the width of the length, so the bar stays
	// where it is as the figure grows.
	int64_t total = video_duration_ms();
	int64_t at = scrub_ms >= 0 ? scrub_ms : video_position_ms();
	char left[24], right[24];
	format_time(left, sizeof(left), at);
	format_time(right, sizeof(right), total);
	strip_t g = strip_layout(right, right);
	int w = draw_width();
	draw_dim_rect(0, g.band_y, w, g.band_h);
	draw_text(EDGE, g.text_y, left, WHITE, w / 3);
	draw_text(w - EDGE - g.right_w, g.text_y, right, WHITE, w / 3);
	if (g.bar_w > 20) {
		draw_rect(g.bar_x, g.bar_y, g.bar_w, 6, GREY);
		if (total > 0) {
			int64_t done = at > total ? total : at;
			int filled = (int)(g.bar_w * done / total);
			draw_rect(g.bar_x, g.bar_y, filled, 6, WHITE);
			// A knob, larger under a finger.
			int r = scrub_ms >= 0 ? 14 : 8;
			draw_rect(g.bar_x + filled - r / 2, g.bar_y + 3 - r, r, 2 * r, WHITE);
		}
	}
}

// The level after a press of a volume key: a bar across the top.
static void draw_volume(int level) {
	int w = draw_width();
	int bar_w = w / 2, x = (w - bar_w) / 2, y = EDGE + 10;
	draw_dim_rect(x - 20, y - 16, bar_w + 40, 40);
	draw_rect(x, y, bar_w, 8, GREY);
	draw_rect(x, y, bar_w * level / 100, 8, WHITE);
}

static void show_message(const char *line1, const char *line2) {
	uint16_t *page = screen_back();
	memset(page, 0, (size_t)panel_w * panel_h * 2);
	draw_target(page, panel_w, panel_h, false);
	int y = panel_h / 2 - draw_line_height();
	draw_text((panel_w - draw_text_width(line1)) / 2, y, line1, WHITE, panel_w - 32);
	if (line2) {
		draw_text((panel_w - draw_text_width(line2)) / 2, y + draw_line_height() + 8, line2, GREY, panel_w - 32);
	}
	screen_flip();
}

// What to say for each reason the engine gives for not playing a file.
static void show_failure(void) {
	const char *why = video_error();
	if (!strcmp(why, "video_too_large")) {
		show_message("This video is larger than the screen.", "Convert it first.");
	} else if (!strcmp(why, "video_not_supported")) {
		show_message("This kind of video is not supported.", "Convert it first.");
	} else if (!strcmp(why, "video_no_memory")) {
		show_message("Not enough memory to play this video.", NULL);
	} else {
		show_message("This file cannot be opened.", NULL);
	}
}

// Plays one file to its end, or until it is closed.
static void play(const char *path) {
	int64_t resume = videoresume_get(path);
	screen_hold_brightness();
	uint32_t brightness_at = now_ms();
	show_message("Opening...", NULL);
	if (!video_start(path, resume > RESUME_BACK_MS ? resume - RESUME_BACK_MS : 0)) {
		show_failure();
		sleep(2);
		return;
	}

	bool sync_applied = false; // which output it is, is known once the film has opened
	bool controls = false;
	bool blanked = false;
	bool failed_shown = false;
	bool dirty = false; // the picture on screen is out of date though no new frame came
	int cleared = 0;	// pages made black around the picture so far
	uint32_t controls_at = 0;
	int volume = -1; // the level to show, while volume_until has not passed
	uint32_t volume_until = 0;
	int down_x = 0, down_y = 0;
	bool moved = false;
	bool quit = false;
	int64_t scrub_ms = -1; // a finger is on the bar, pointing here
	uint32_t active_at = now_ms(); // the last touch or key, or the last moment the film was running

	while (!quit) {
		// ---- the glass and the buttons ----
		for (input_event_t ev = touch_poll(2); ev.kind != INPUT_NONE; ev = touch_poll(0)) {
			hit_t hit = HIT_NONE;
			active_at = now_ms();
			// A screen that is off comes back at the first touch, and that
			// touch does nothing else: nobody can see what it would land on.
			if (blanked && ev.kind != INPUT_KEY) {
				if (ev.kind == INPUT_TOUCH_UP) {
					blanked = false;
					screen_blank(false);
					controls = true;
					controls_at = now_ms();
					dirty = true;
				}
				continue;
			}
			if (ev.kind == INPUT_KEY) {
				switch (ev.code) {
				case KEY_POWER:
					// The screen off and the film stopped; again, and the
					// screen comes back with the film still stopped.
					blanked = !blanked;
					if (blanked && !video_paused()) {
						video_set_paused(true);
					}
					screen_blank(blanked);
					controls = true;
					dirty = true;
					break;
				case KEY_VOLUMEUP:
				case KEY_VOLUMEDOWN: {
					int level = audio_volume_step(ev.code == KEY_VOLUMEUP ? 1 : -1);
					if (level >= 0) {
						volume = level;
						volume_until = now_ms() + VOLUME_SHOWN_MS;
						dirty = true;
					}
					break;
				}
				case KEY_PLAYPAUSE:
				case KEY_PLAYCD:
				case KEY_PAUSECD:
					hit = HIT_PLAY;
					break;
				case KEY_NEXTSONG:
					hit = HIT_FORWARD;
					break;
				case KEY_PREVIOUSSONG:
					hit = HIT_BACK;
					break;
				default:
					break;
				}
				// A key that moves the film brings the screen back with it: a
				// film running where nobody can see it is only the battery
				// running down.
				if (hit != HIT_NONE && blanked) {
					blanked = false;
					screen_blank(false);
				}
			} else if (ev.kind == INPUT_TOUCH_DOWN) {
				down_x = ev.x;
				down_y = ev.y;
				moved = false;
				// On the bar, with the controls up: the finger now says where
				// in the film, and the film goes there when it lifts.
				if (controls && !blanked && !video_failed()) {
					int vx, vy;
					draw_target(screen_back(), panel_w, panel_h, video_turned());
					draw_from_panel(ev.x, ev.y, &vx, &vy);
					scrub_ms = strip_position(vx, vy);
					if (scrub_ms >= 0) {
						controls_at = now_ms();
						dirty = true;
					}
				}
			} else if (ev.kind == INPUT_TOUCH_MOVE) {
				if (scrub_ms >= 0) {
					int vx, vy;
					draw_target(screen_back(), panel_w, panel_h, video_turned());
					draw_from_panel(ev.x, ev.y, &vx, &vy);
					// Wherever the finger wanders: only how far along counts.
					int64_t to = strip_position(vx, draw_height());
					if (to >= 0) {
						scrub_ms = to;
					}
					controls_at = now_ms();
					dirty = true;
				} else if (abs(ev.x - down_x) > TAP_SLOP || abs(ev.y - down_y) > TAP_SLOP) {
					moved = true;
				}
			} else if (ev.kind == INPUT_TOUCH_UP && scrub_ms >= 0) {
				video_seek_to(scrub_ms);
				scrub_ms = -1;
				controls_at = now_ms();
				dirty = true;
			} else if (ev.kind == INPUT_TOUCH_UP && !moved && !blanked) {
				if (video_failed()) {
					quit = true;
					break;
				}
				if (!controls) {
					controls = true;
					controls_at = now_ms();
					dirty = true;
				} else {
					int vx, vy;
					draw_target(screen_back(), panel_w, panel_h, video_turned());
					draw_from_panel(down_x, down_y, &vx, &vy);
					hit = control_at(vx, vy);
					if (hit == HIT_NONE && !video_paused()) {
						controls = false;
						dirty = true;
					}
				}
			}

			if (video_failed()) {
				continue;
			}
			switch (hit) {
			case HIT_PLAY:
				video_set_paused(!video_paused());
				break;
			case HIT_BACK:
				video_seek_relative(-SEEK_STEP_S);
				break;
			case HIT_FORWARD:
				video_seek_relative(SEEK_STEP_S);
				break;
			case HIT_CLOSE:
				quit = true;
				break;
			case HIT_SYNC_LESS:
				sync_step(-1);
				break;
			case HIT_SYNC_MORE:
				sync_step(1);
				break;
			default:
				break;
			}
			if (hit != HIT_NONE) {
				controls = true;
				controls_at = now_ms();
				dirty = true;
			}
		}
		if (quit) {
			break;
		}

		if (video_failed()) {
			if (!failed_shown) {
				failed_shown = true;
				show_failure();
			}
			continue; // a tap closes it, above
		}
		if (video_ended()) {
			break;
		}
		if (!sync_applied && video_screen() && video_position_ms() > 0) {
			sync_applied = true;
			video_set_sync_offset(*sync_current());
		}
		if (blanked) {
			continue;
		}
		if (!video_paused()) {
			active_at = now_ms();
		} else if (now_ms() - active_at > PAUSED_SCREEN_OFF_MS) {
			blanked = true;
			screen_blank(true);
			continue;
		}
		// The backlight, again every two seconds: a dimming CompasOS had
		// asked for just before the app started can still arrive after it.
		if (now_ms() - brightness_at > 2000) {
			brightness_at = now_ms();
			screen_hold_brightness();
		}

		if (controls && scrub_ms < 0 && !video_paused() && now_ms() - controls_at > CONTROLS_SHOWN_MS) {
			controls = false;
			dirty = true;
		}
		if (volume >= 0 && (int32_t)(now_ms() - volume_until) > 0) {
			volume = -1;
			dirty = true;
		}

		// ---- the picture ----
		// Into the page that is off screen, and then the pages are swapped: a
		// frame is never seen half drawn. Each of the two pages is made black
		// once before the first picture goes into it, for what lies around a
		// picture smaller than the panel.
		uint16_t *page = screen_back();
		if (cleared < 2 && video_screen()) {
			memset(page, 0, (size_t)panel_w * panel_h * 2);
		}
		bool fresh = video_present(page);
		// Nothing new, but the controls came or went, or the film is paused
		// under them and their clock moved: the last frame again.
		if (!fresh && (dirty || (controls && !video_paused()))) {
			fresh = video_repaint(page);
		}
		if (fresh) {
			draw_target(page, panel_w, panel_h, video_turned());
			if (controls) {
				draw_controls(scrub_ms);
			}
			if (volume >= 0) {
				draw_volume(volume);
			}
			screen_flip();
			dirty = false;
			if (cleared < 2) {
				cleared++;
			}
		}
	}

	// Where it was left, read before the thread goes. A film that ran to its
	// end, or was left close to either end, is forgotten instead.
	if (!video_failed()) {
		int64_t at = video_position_ms(), total = video_duration_ms();
		int64_t tail = total / 20 > RESUME_END_MIN_MS ? total / 20 : RESUME_END_MIN_MS;
		bool part_way = !video_ended() && at >= RESUME_MIN_MS && (total <= 0 || at < total - tail);
		videoresume_set(path, part_way ? at : 0);
	}
	if (blanked) {
		screen_blank(false);
	}
	video_stop();
}

// ---------------------------------------------------------------------------
// the list
// ---------------------------------------------------------------------------

#define NAME_LEN 256
#define TITLE_H 96
#define ROW_H 76
#define ROW_GAP 10
#define SIDE 16

typedef struct {
	char name[NAME_LEN];
} entry_t;

static entry_t *entries;
static int entry_count;

static int by_name(const void *a, const void *b) { return strcasecmp(((const entry_t *)a)->name, ((const entry_t *)b)->name); }

static void scan(void) {
	free(entries);
	entries = NULL;
	entry_count = 0;

	DIR *dir = opendir(VIDEO_DIR);
	if (!dir) {
		return;
	}
	int cap = 0;
	struct dirent *de;
	while ((de = readdir(dir)) != NULL) {
		if (de->d_name[0] == '.' || !video_is_video_file(de->d_name)) {
			continue;
		}
		if (entry_count == cap) {
			int grown = cap ? cap * 2 : 32;
			entry_t *bigger = realloc(entries, (size_t)grown * sizeof(*entries));
			if (!bigger) {
				break;
			}
			entries = bigger;
			cap = grown;
		}
		snprintf(entries[entry_count].name, sizeof(entries[entry_count].name), "%s", de->d_name);
		entry_count++;
	}
	closedir(dir);
	if (entry_count > 1) {
		qsort(entries, (size_t)entry_count, sizeof(*entries), by_name);
	}
}

static int list_max_scroll(void) {
	int content = entry_count * (ROW_H + ROW_GAP) + ROW_GAP;
	int room = panel_h - TITLE_H;
	return content > room ? content - room : 0;
}

static void draw_list(int scroll) {
	uint16_t *page = screen_back();
	draw_target(page, panel_w, panel_h, false);
	draw_rect(0, 0, panel_w, panel_h, PAGE_BG);

	if (entry_count == 0) {
		int y = panel_h / 2 - draw_line_height() * 2;
		const char *lines[] = {"No videos.", "Put them in the Video folder", "on the card, converted", "for this screen."};
		for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) {
			draw_text((panel_w - draw_text_width(lines[i])) / 2, y + (int)i * (draw_line_height() + 4), lines[i],
					  i == 0 ? WHITE : GREY, panel_w - 2 * SIDE);
		}
	}

	for (int i = 0; i < entry_count; i++) {
		int y = TITLE_H + ROW_GAP + i * (ROW_H + ROW_GAP) - scroll;
		if (y + ROW_H <= TITLE_H || y >= panel_h) {
			continue;
		}
		draw_rect(SIDE, y, panel_w - 2 * SIDE, ROW_H, ROW_BG);
		// Without the extension: every row would end in the same four letters.
		char shown[NAME_LEN];
		snprintf(shown, sizeof(shown), "%s", entries[i].name);
		char *dot = strrchr(shown, '.');
		if (dot && dot != shown) {
			*dot = '\0';
		}
		draw_text(SIDE + 18, y + (ROW_H - draw_line_height()) / 2, shown, WHITE, panel_w - 2 * SIDE - 36);
	}

	// The title last, over whatever rows have scrolled up under it.
	draw_rect(0, 0, panel_w, TITLE_H, PAGE_BG);
	draw_text(SIDE + 8, (TITLE_H - draw_line_height()) / 2, "Video", WHITE, panel_w / 2);
	int qx = panel_w - SIDE - 36, qy = TITLE_H / 2;
	draw_line(qx - 14, qy - 14, qx + 14, qy + 14, 4, WHITE);
	draw_line(qx - 14, qy + 14, qx + 14, qy - 14, 4, WHITE);

	screen_flip();
}

// Shows the list until a film is picked (its index) or the app is closed (-1).
static int pick(void) {
	static int scroll;
	screen_hold_brightness();
	scan();
	if (scroll > list_max_scroll()) {
		scroll = list_max_scroll();
	}
	draw_list(scroll);

	int down_x = 0, down_y = 0, down_scroll = 0;
	bool moved = false;
	for (;;) {
		bool redraw = false;
		for (input_event_t ev = touch_poll(50); ev.kind != INPUT_NONE; ev = touch_poll(0)) {
			if (ev.kind == INPUT_TOUCH_DOWN) {
				down_x = ev.x;
				down_y = ev.y;
				down_scroll = scroll;
				moved = false;
			} else if (ev.kind == INPUT_TOUCH_MOVE) {
				if (abs(ev.y - down_y) > TAP_SLOP || abs(ev.x - down_x) > TAP_SLOP) {
					moved = true;
				}
				if (moved) {
					int want = down_scroll - (ev.y - down_y);
					int max = list_max_scroll();
					want = want < 0 ? 0 : want > max ? max : want;
					if (want != scroll) {
						scroll = want;
						redraw = true;
					}
				}
			} else if (ev.kind == INPUT_TOUCH_UP && !moved) {
				if (down_y < TITLE_H) {
					if (down_x > panel_w - 110) {
						return -1; // the cross
					}
					continue;
				}
				int index = (down_y - TITLE_H - ROW_GAP + scroll) / (ROW_H + ROW_GAP);
				int into = (down_y - TITLE_H - ROW_GAP + scroll) % (ROW_H + ROW_GAP);
				if (index >= 0 && index < entry_count && into < ROW_H) {
					return index;
				}
			}
		}
		if (redraw) {
			draw_list(scroll);
		}
	}
}

int main(int argc, char **argv) {
	setvbuf(stdout, NULL, _IOLBF, 0); // the session log is read while the app runs

	// This thread converts each frame and shows it: 13 ms of work that has to
	// start on time, and then a wait for the panel's refresh (the swap blocks
	// until it, up to 29 ms). That leaves almost nothing of a 40 ms frame to
	// be late by, so it runs above the decoder (video.c, WORKER_NICE), which
	// works in longer stretches and has a fifth of a second of sound in hand
	// to be late with.
	setpriority(PRIO_PROCESS, 0, DISPLAY_NICE);

	if (!screen_open()) {
		return 1;
	}
	panel_w = screen_width();
	panel_h = screen_height();
	if (!touch_open()) {
		fprintf(stderr, "no input devices: there would be no way to close a film\n");
		return 1;
	}

	const char *data = getenv("COMPAS_APP_DATA");
	videoresume_set_root(data && data[0] ? data : ".");
	sync_load(data && data[0] ? data : ".");
	video_set_output(panel_w, panel_h);

	if (argc > 1 && argv[1][0]) {
		// Opened with a file: that film, and back to whoever asked.
		play(argv[1]);
	} else {
		for (int index; (index = pick()) >= 0;) {
			char path[1024];
			snprintf(path, sizeof(path), "%s/%s", VIDEO_DIR, entries[index].name);
			play(path);
		}
	}

	audio_volume_restore();
	touch_close();
	screen_close();
	return 0;
}
