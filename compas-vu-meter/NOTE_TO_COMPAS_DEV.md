# VU meters and a spectrum for Now Playing: two patches

Hello,

A few people have asked for a VU meter like the ones on Lotoo and Sony
players, and said the needle style would not suit CompásOS. I looked at doing
it as a plugin and it cannot be one: a plugin has no access to audio levels
and its timers run once a second, and an external app stops playback. So
this is an addition to the player and a plugin with three layouts that use
it: bars, round dials with needles, and a ten-band spectrum, drawn flat in
the theme's accent rather than as imitation hardware.

- `for-compas-player/0001-…patch`, for compas-player (on v1.6.0, `bd71aa6`): the level
  tap and the widget names a Player layout can use. The built-in layout is
  not touched, and nothing is measured unless a layout with a meter is on
  screen.
- `for-compas-plugins/0001-…patch`, for compas-plugins (on `871f69f`): the VU Meter
  Player layout and a section in `PLUGINS.md`.

Each folder has a `PR_DESCRIPTION.md` with what was and was not tested. The
short version: the meter logic is tested on the host, and all three layouts
run on my R1 (the pictures in `screenshots/`), on v1.5.1 with the
change ported and on v1.6.0. The timing has only been judged by eye, and
Bluetooth and USB output are untried.

One thing went wrong along the way, and it turned out to be v1.5.1's: twice
the framebuffer driver stopped scanout (`soc_fb: continuous pan failed -5`)
and only a reboot brought the display back. Logging display events showed a
scrolling title still invalidating after screen-off, restarting the refresh
and panning the blanked panel, on the Default layout as well. You already
pause the marquees in v1.6.0. On v1.6.0, 40 screen off/on cycles with the
dials playing gave no failure, though the player logged some "Resource busy"
pans whose source I have not found. Details are in the player PR description.

Things you may want to decide differently:

1. **Where the tap sits.** In `audio_output_write()` and `_write_s24()`, so
   it also sees the USB DAC bridge, and sees USB output after its digital
   gain. Tapping before the gain would need a call at each `peq_process`
   site instead.
2. **The delay for Bluetooth and USB.** Pipe contents plus fixed 500 ms and
   250 ms. If you have a better figure for what aplay and BlueALSA hold, the
   two constants are in one place.
3. **The names.** `vu_left`, `vu_right`, `vu_peak_left`, `vu_peak_right`,
   `vu_meter`, and the capability `ui.player_layout_meter`.
4. **The needle.** An object named `vu_needle_left`/`_right` is turned with
   `transform_rotation`, 45 degrees either side of upright. That is a
   transformed layer redrawn up to twenty times a second; an image rotated
   with `lv_image_set_rotation` may be cheaper if it turns out to matter.

GPL-3.0-or-later, like the rest.
