#include "touch.h"

#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <linux/input.h>
#include <sys/ioctl.h>

#define MAX_DEVICES 8
#define QUEUE 32

static struct pollfd fds[MAX_DEVICES];
static bool is_touch[MAX_DEVICES];
static int count;

// One finger is all the controls need. Both multitouch protocols are read:
// type B says a finger has gone with a tracking id of -1, type A (what the
// R1's panel driver speaks) with a packet that holds no position at all, and
// either may send BTN_TOUCH as well.
static int cur_x, cur_y;
static bool pos_seen;	// a position arrived in this packet
static bool lifted;		// this packet said the finger has gone
static bool down;

static input_event_t queue[QUEUE];
static int q_head, q_len;

static void push(input_kind_t kind, int x, int y, int code) {
	if (q_len == QUEUE) {
		return;
	}
	queue[(q_head + q_len) % QUEUE] = (input_event_t){.kind = kind, .x = x, .y = y, .code = code};
	q_len++;
}

bool touch_open(void) {
	count = 0;
	for (int i = 0; i < 16 && count < MAX_DEVICES; i++) {
		char path[32];
		snprintf(path, sizeof(path), "/dev/input/event%d", i);
		int fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd < 0) {
			continue;
		}
		unsigned long abs_bits[(ABS_MAX + 1 + 8 * sizeof(long) - 1) / (8 * sizeof(long))];
		memset(abs_bits, 0, sizeof(abs_bits));
		ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits);
		bool touch = (abs_bits[ABS_MT_POSITION_X / (8 * sizeof(long))] >> (ABS_MT_POSITION_X % (8 * sizeof(long)))) & 1;

		char name[64] = "?";
		ioctl(fd, EVIOCGNAME(sizeof(name)), name);
		printf("input: %s is %s%s\n", path, name, touch ? " (the glass)" : "");

		fds[count] = (struct pollfd){.fd = fd, .events = POLLIN};
		is_touch[count] = touch;
		count++;
	}
	return count > 0;
}

void touch_close(void) {
	for (int i = 0; i < count; i++) {
		close(fds[i].fd);
	}
	count = 0;
}

// The end of a packet from the glass: what it adds up to.
static void packet_done(void) {
	if (lifted || !pos_seen) {
		// Gone, or a packet with nothing in it, which is type A's way of
		// saying the same.
		if (down) {
			down = false;
			push(INPUT_TOUCH_UP, cur_x, cur_y, 0);
		}
	} else if (!down) {
		down = true;
		push(INPUT_TOUCH_DOWN, cur_x, cur_y, 0);
	} else {
		push(INPUT_TOUCH_MOVE, cur_x, cur_y, 0);
	}
	pos_seen = false;
	lifted = false;
}

static void read_device(int index) {
	struct input_event ev;
	while (read(fds[index].fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
		if (!is_touch[index]) {
			if (ev.type == EV_KEY && ev.value == 1) {
				push(INPUT_KEY, 0, 0, ev.code);
			}
			continue;
		}
		if (ev.type == EV_ABS) {
			if (ev.code == ABS_MT_POSITION_X) {
				cur_x = ev.value;
				pos_seen = true;
			} else if (ev.code == ABS_MT_POSITION_Y) {
				cur_y = ev.value;
				pos_seen = true;
			} else if (ev.code == ABS_MT_TRACKING_ID && ev.value == -1) {
				lifted = true;
			}
		} else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
			if (ev.value == 0) {
				lifted = true;
			}
		} else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
			packet_done();
		}
	}
}

input_event_t touch_poll(int timeout_ms) {
	if (q_len == 0 && count > 0 && poll(fds, (nfds_t)count, timeout_ms) > 0) {
		for (int i = 0; i < count; i++) {
			if (fds[i].revents & POLLIN) {
				read_device(i);
			}
		}
	} else if (q_len == 0 && count == 0 && timeout_ms > 0) {
		usleep((useconds_t)timeout_ms * 1000);
	}
	if (q_len == 0) {
		return (input_event_t){.kind = INPUT_NONE};
	}
	input_event_t ev = queue[q_head];
	q_head = (q_head + 1) % QUEUE;
	q_len--;
	return ev;
}
