#ifndef VIDEORESUME_H
#define VIDEORESUME_H

#include <stdint.h>

// Where each film was left, so that opening it again carries on from there.
//
// One small text file in the app's data folder, video-resume.txt, a line per
// film: the position in milliseconds, a space, the file's name. By name and
// not by path, so a film moved into a folder keeps its place. The newest is
// first and the list stops at VIDEORESUME_MAX, which is when the oldest is
// forgotten.

#define VIDEORESUME_MAX 100

// The folder the file lives in (COMPAS_APP_DATA). Call once, before anything
// else here; without it nothing is remembered.
void videoresume_set_root(const char *dir);

// Where `path` was left, or 0 when it was not, or was finished.
int64_t videoresume_get(const char *path);

// Remembers `position_ms` for `path`; 0 forgets it. Writes the file.
void videoresume_set(const char *path, int64_t position_ms);

#endif /* VIDEORESUME_H */
