#include "videoresume.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/types.h>

#define NAME_LEN 256

typedef struct {
	int64_t ms;
	char name[NAME_LEN];
} entry_t;

static char file_path[600];
static char dir_path[560];

// Read whole and written whole, on the interface thread, when a film is opened
// and when it is left: a hundred short lines, a few times an evening.
static entry_t *entries;
static int count;
static int loaded;

static const char *base_name(const char *path) {
	const char *slash = path ? strrchr(path, '/') : NULL;
	return slash ? slash + 1 : (path ? path : "");
}

void videoresume_set_root(const char *dir) {
	if (!dir || !dir[0]) {
		return;
	}
	snprintf(dir_path, sizeof(dir_path), "%s", dir);
	snprintf(file_path, sizeof(file_path), "%s/video-resume.txt", dir_path);
}

static void load(void) {
	if (loaded || !file_path[0]) {
		return;
	}
	loaded = 1;

	entries = calloc(VIDEORESUME_MAX, sizeof(*entries));
	if (!entries) {
		return;
	}

	FILE *f = fopen(file_path, "r");
	if (!f) {
		return; // nothing remembered yet
	}
	char line[NAME_LEN + 32];
	while (count < VIDEORESUME_MAX && fgets(line, sizeof(line), f)) {
		char *end = NULL;
		long long ms = strtoll(line, &end, 10);
		if (!end || *end != ' ' || ms <= 0) {
			continue;
		}
		end++;
		end[strcspn(end, "\r\n")] = '\0';
		if (!end[0]) {
			continue;
		}
		entries[count].ms = ms;
		snprintf(entries[count].name, sizeof(entries[count].name), "%s", end);
		count++;
	}
	fclose(f);
}

// Through a temporary file and a rename: a card pulled out half way leaves the
// old list, not half of the new one.
static void save(void) {
	if (!file_path[0] || !entries) {
		return;
	}
	mkdir(dir_path, 0777);

	char tmp[sizeof(file_path) + 8];
	snprintf(tmp, sizeof(tmp), "%s.tmp", file_path);
	FILE *f = fopen(tmp, "w");
	if (!f) {
		return;
	}
	for (int i = 0; i < count; i++) {
		fprintf(f, "%lld %s\n", (long long)entries[i].ms, entries[i].name);
	}
	if (fclose(f) != 0 || rename(tmp, file_path) != 0) {
		remove(tmp);
	}
}

static int find(const char *name) {
	for (int i = 0; i < count; i++) {
		if (strcmp(entries[i].name, name) == 0) {
			return i;
		}
	}
	return -1;
}

int64_t videoresume_get(const char *path) {
	load();
	if (!entries) {
		return 0;
	}
	int i = find(base_name(path));
	return i >= 0 ? entries[i].ms : 0;
}

void videoresume_set(const char *path, int64_t position_ms) {
	load();
	const char *name = base_name(path);
	// A name with a line break in it cannot be written back the way it is read.
	if (!entries || !name[0] || strlen(name) >= NAME_LEN || strpbrk(name, "\r\n")) {
		return;
	}

	int i = find(name);
	if (i < 0 && position_ms <= 0) {
		return; // nothing to forget
	}
	if (i >= 0) {
		memmove(&entries[i], &entries[i + 1], (size_t)(count - i - 1) * sizeof(*entries));
		count--;
	}
	if (position_ms > 0) {
		if (count == VIDEORESUME_MAX) {
			count--; // the oldest goes
		}
		memmove(&entries[1], &entries[0], (size_t)count * sizeof(*entries));
		entries[0].ms = position_ms;
		snprintf(entries[0].name, sizeof(entries[0].name), "%s", name);
		count++;
	}
	save();
}
