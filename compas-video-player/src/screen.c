#include "screen.h"

#include <fcntl.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

static int fd = -1;
static struct fb_var_screeninfo var;
static uint8_t *mem;
static size_t page_bytes;
static size_t mapped;
static int pages;
static bool blanked;

bool screen_open(void) {
	struct fb_fix_screeninfo fix;

	fd = open("/dev/fb0", O_RDWR);
	if (fd < 0 || ioctl(fd, FBIOGET_VSCREENINFO, &var) != 0 || ioctl(fd, FBIOGET_FSCREENINFO, &fix) != 0) {
		perror("screen: /dev/fb0");
		return false;
	}
	// The conversion writes 16-bit pixels at a stride of exactly the width.
	if (var.bits_per_pixel != 16 || fix.line_length != var.xres * 2) {
		fprintf(stderr, "screen: %u bits, stride %u for %u pixels: not RGB565 with a tight stride\n", var.bits_per_pixel,
				fix.line_length, var.xres);
		return false;
	}

	page_bytes = (size_t)fix.line_length * var.yres;
	pages = (var.yres_virtual >= var.yres * 2 && fix.smem_len >= page_bytes * 2) ? 2 : 1;
	mapped = page_bytes * (size_t)pages;
	mem = mmap(NULL, mapped, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (mem == MAP_FAILED) {
		perror("screen: mmap");
		mem = NULL;
		return false;
	}
	printf("screen: %ux%u, %d page(s), on page %d\n", var.xres, var.yres, pages, var.yoffset ? 1 : 0);
	return true;
}

void screen_close(void) {
	if (mem) {
		munmap(mem, mapped);
		mem = NULL;
	}
	if (fd >= 0) {
		close(fd);
		fd = -1;
	}
}

int screen_width(void) { return (int)var.xres; }
int screen_height(void) { return (int)var.yres; }

uint16_t *screen_back(void) {
	if (pages < 2) {
		return (uint16_t *)mem;
	}
	return (uint16_t *)(mem + (var.yoffset ? 0 : page_bytes));
}

void screen_flip(void) {
	if (pages < 2) {
		return;
	}
	var.yoffset = var.yoffset ? 0 : var.yres;
	if (ioctl(fd, FBIOPAN_DISPLAY, &var) != 0) {
		perror("screen: FBIOPAN_DISPLAY");
	}
}

void screen_sync(void) {
	if (pages < 2) {
		return;
	}
	const uint8_t *front = mem + (var.yoffset ? page_bytes : 0);
	memcpy(screen_back(), front, page_bytes);
}

// ---------------------------------------------------------------------------
// brightness
// ---------------------------------------------------------------------------
//
// CompasOS dims the backlight after a few seconds without a touch, and its
// timers stop while an app runs. That keeps it from dimming under a film; it
// also keeps it from undoing a dimming that had already happened. Leave the
// list alone for ten seconds, then start a film, and the film played at 5%
// brightness to the end -- measured: the backlight at 9 of 101 throughout.
// CompasOS expects an app to write the backlight if it wants (it rewrites its
// own state when the app exits), so the app puts it where the user set it.

// The user's brightness as CompasOS has it, in percent, or -1.
static int compas_brightness_percent(void) {
	FILE *f = fopen("/usr/data/.compas/settings.txt", "r");
	if (!f) {
		return -1;
	}
	char line[160];
	int percent = -1;
	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, "brightness_percent=", 19) == 0) {
			percent = atoi(line + 19);
		}
	}
	fclose(f);
	return percent >= 1 && percent <= 100 ? percent : -1;
}

static int read_number(const char *path) {
	FILE *f = fopen(path, "r");
	int value = -1;
	if (f) {
		if (fscanf(f, "%d", &value) != 1) {
			value = -1;
		}
		fclose(f);
	}
	return value;
}

static char backlight_node[300]; // .../brightness of the one backlight there is; "-" when there is none
static int backlight_target = -1;

static void backlight_find(void) {
	if (backlight_node[0]) {
		return;
	}
	DIR *dir = opendir("/sys/class/backlight");
	struct dirent *de;
	while (dir && (de = readdir(dir)) != NULL) {
		if (de->d_name[0] == '.') {
			continue;
		}
		char max_path[300];
		snprintf(max_path, sizeof(max_path), "/sys/class/backlight/%s/max_brightness", de->d_name);
		int max = read_number(max_path);
		int percent = compas_brightness_percent();
		if (max > 0) {
			snprintf(backlight_node, sizeof(backlight_node), "/sys/class/backlight/%s/brightness", de->d_name);
			// With no setting to read, what is there now if it is a
			// brightness anyone would choose, and otherwise a middling one.
			int now = read_number(backlight_node);
			backlight_target = percent > 0 ? percent * max / 100 : now * 5 >= max ? now : max * 3 / 5;
			printf("screen: backlight held at %d of %d\n", backlight_target, max);
			break;
		}
	}
	if (dir) {
		closedir(dir);
	}
	if (!backlight_node[0]) {
		snprintf(backlight_node, sizeof(backlight_node), "-"); // none: do not look again
	}
}

static void backlight_write(int value) {
	if (backlight_node[0] == '-') {
		return;
	}
	FILE *f = fopen(backlight_node, "w");
	if (f) {
		fprintf(f, "%d", value);
		fclose(f);
	}
}

void screen_hold_brightness(void) {
	backlight_find();
	if (backlight_target < 0 || blanked) {
		return;
	}
	int now = read_number(backlight_node);
	if (now >= 0 && now < backlight_target) {
		backlight_write(backlight_target);
	}
}

// The screen off and on again, the way CompasOS does it: the light to
// nothing, and the panel blanked through sysfs (FBIOBLANK alone left the
// light on at full). CompasOS puts both back as it wants them when the app
// exits, so a film closed with the screen off does not leave it off.
void screen_blank(bool blank) {
	backlight_find();
	blanked = blank;
	if (blank) {
		backlight_write(0);
	}
	FILE *f = fopen("/sys/class/graphics/fb0/blank", "w");
	if (f) {
		fputs(blank ? "4" : "0", f);
		fclose(f);
	} else if (ioctl(fd, FBIOBLANK, blank ? FB_BLANK_POWERDOWN : FB_BLANK_UNBLANK) != 0) {
		perror("screen: FBIOBLANK");
	}
	if (!blank) {
		// Blanking takes the panel's scan-out down, and some drivers come
		// back in their default mode, without the second page: the mode is
		// put back by force and the page that was showing panned to again.
		var.activate = FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;
		if (ioctl(fd, FBIOPUT_VSCREENINFO, &var) != 0) {
			perror("screen: FBIOPUT_VSCREENINFO (wake)");
		}
		if (ioctl(fd, FBIOPAN_DISPLAY, &var) != 0) {
			perror("screen: FBIOPAN_DISPLAY (wake)");
		}
		if (backlight_target > 0) {
			backlight_write(backlight_target);
		}
	}
}
