# Video on CompásOS: an external app and a plugin, and four things I ran into

Hello,

I've built a video player for CompásOS on the HiBy R1, using your external app
protocol and a small plugin. Nothing in the player is modified. I'd like to
offer the plugin for the store, and to pass on what I hit while building the
app, since some of it is on the CompásOS side.

Source, a prebuilt zip and a full README: https://github.com/bidhata/Hiby-R1-Mod
(folder `compas-video-player`). GPL-3.0-or-later.

## What it is

- **`video`, an external app (protocol 1).** One static MIPS executable, 3.5 MB:
  FFmpeg 9.0.2 cut down to MPEG-4, H.264, MPEG-1/2 and fixed-point audio
  decoders, tinyalsa for the sound card, its own drawing and touch handling.
  It plays files from `<card>/Video` over the whole panel, turned sideways for
  a picture wider than tall.
- **`Video.lua`, a plugin.** A row under More that lists `<card>/Video` and
  starts the app with the file tapped, through `plugin.launch_app`. This is
  the pull request to compas-plugins.

Videos have to be converted on a computer first (MPEG-4, up to 800x450, 25
fps): the X1600 decodes a full-size MPEG-4 frame in about 15 ms and an H.264
one in about 50 ms, measured on the device. The README has the conversion
steps.

## How far it is tested

On one HiBy R1, CompásOS v1.5.1, with one pair of Bluetooth headphones (AAC):
play, pause, seek by dragging and by keys, volume keys, power key, resume,
and return to the player. In steady play the app's log shows every frame in
most ten-second windows and three to five missing in the rest.

Not tested: the R3 Pro II, the R3 II 2025, the Tempotec V1, and the headphone
socket since the Bluetooth work. The app's manifest does not restrict boards.

## Four things on the CompásOS side

**1. A dimmed screen stays dimmed for the whole app session.**
Leave any screen untouched for the dim delay (10 s here), then start an app.
The backlight stays at the dimmed level until the app exits. I measured the
backlight at 9 of 101 from the moment the list dimmed through 24 seconds of
the app running. The dimming and un-dimming are decided in an LVGL timer
(`gui_screen_power.c`), and `app_session_run()` blocks the UI thread, so the
tap that launches the app never gets to undo the dim. Calling
`backlight_set_dimmed(false)` before the spawn would fix it for every app and
emulator. My app works around it by writing `brightness_percent` from
`/usr/data/.compas/settings.txt` to the backlight itself.

**2. A plugin cannot bring its app with it.**
`tools/build_index.py` limits a store file to 1 MiB (`MAX_FILE_BYTES`), and
this app is 3.5 MB, so the plugin installs from the store but the user has to
copy the app to `.apps/video` by hand. A larger limit for files destined for
`.apps/`, or an app section in the store like the emulators have, would let a
plugin and its app install together.

**3. `opens` is only used by plugins.**
The apps guide says the manifest's `opens` list is used only when a plugin
launches the app with a file. Tapping an `.mp4` in the File Manager does not
offer the app. An "open with" for registered extensions would make the plugin
unnecessary for apps like this one.

**4. Nothing dims or times out under an app, including a paused one.**
That is the right default, but it means every app has to handle its own idle
screen-off or risk draining the battery when left alone. Mine switches the
screen off after two minutes paused. Worth a line in the apps guide.

## What the app depends on, in case any of it is about to change

- The framebuffer as 16 bpp with two pages, swapped with `FBIOPAN_DISPLAY`
  (which waits for the panel's refresh on this kernel).
- Bluetooth sound by piping raw PCM into `aplay` on a `type plug` PCM over
  `type bluealsa`, pinned to the rate `bluealsactl info` reports for the
  `a2dpsrc/sink` PCM. Opening plain `bluealsa` at another rate made BlueALSA
  rebuild the link and `aplay` exit with "No such device", as your own code
  comments describe.
- Volume through the `Left Playback Volume` and `Right Playback Volume` mixer
  controls on card 0 (put back on exit), or `bluealsactl volume` over
  Bluetooth.
- `brightness_percent` in `/usr/data/.compas/settings.txt`, and
  `/sys/class/graphics/fb0/blank` with the backlight for screen off.
- Card 0, device 0, four periods of 2048 frames, as `audio_output.c` opens it.

Thanks for the app protocol. It made this possible without touching the
firmware.

Krish Paul
