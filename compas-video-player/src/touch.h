#ifndef TOUCH_H
#define TOUCH_H

#include <stdbool.h>

// The glass and the buttons, read straight from the kernel's input devices:
// while an app runs, CompasOS reads none of them.

typedef enum {
	INPUT_NONE,
	INPUT_TOUCH_DOWN,
	INPUT_TOUCH_MOVE,
	INPUT_TOUCH_UP,
	INPUT_KEY, // a key going down; `code` is the Linux key code
} input_kind_t;

typedef struct {
	input_kind_t kind;
	int x, y; // panel pixels, for the touch kinds
	int code;
} input_event_t;

bool touch_open(void);
void touch_close(void);

// Waits up to `timeout_ms` for something to happen and reports one thing.
// INPUT_NONE when nothing did. Call again with 0 until it says so: one read
// can hold several.
input_event_t touch_poll(int timeout_ms);

#endif /* TOUCH_H */
