# VU Meter Player: the Default layout with a level meter, dials or a spectrum

Adds `plugins/VUMeterPlayer/` (category "Customization") and documents the
level meter's widget names in `PLUGINS.md`.

**Depends on** the compas-player pull request "Now Playing: level meters and
a spectrum that XML layouts can show". The plugin registers its layout only when
the player reports `ui.player_layout_meter`, so on a player without it the
plugin installs and offers nothing.

**What it is.** Three layouts from the Default Waveform Player's XML, with a
plain seek bar and a smaller cover, all in the theme's accent. VU Meter
Player puts two slim bars, L and R, with a peak-hold mark between the
quality pill and the seek bar. VU Dial Player puts two round dials there,
each an arc with a needle and a peak mark. VU Spectrum Player puts a
ten-band spectrum there, with falling caps. A swipe across the meter, or a tap
on it, moves between the three (`ui.player_actions`); that lasts until the
player restarts, and the layout chosen in Settings is the one it starts with. Variants for 480x800, 480x720,
320x480 and 240x320; the smallest has no L/R captions.
`v1_back.png` is the same file the other layouts ship.

**Tested.**

- `python3 tools/build_index.py --check` passes (69 plugins, 360 files).
- All four XML files load through the player's `lv_xml` and the bars are
  found and driven, in a headless build of the player's LVGL. That build has
  no cover art and not the app's fonts.

- On a HiBy R1 (480x800): the bars and dials on a v1.5.1 player with the
  level meter change, the dials and the spectrum on v1.6.0 with it. Each
  loads and moves with the music.

**Not tested.**

- The 480x720, 320x480 and 240x320 variants on a device. The spacing of the
  two smallest was set without the real fonts and may need a few pixels
  either way.
- The swipe and the tap between layouts, on a device.
- With the lyrics open: the meter is meant to leave with the controls, and
  that has not been looked at on any of the three layouts.
- The bars layout on v1.6.0 (seen on v1.5.1 only).
- There is no `preview.jpg` and no layout `.png` yet. I have screenshots
  from the R1, of a track without cover art; I will add proper ones, or you
  are welcome to.
