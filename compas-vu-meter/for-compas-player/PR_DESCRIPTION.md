# Now Playing: level meters and a spectrum that XML layouts can show

People have asked for a VU meter. This adds one as something a Player layout
can include, in the same way a layout asks for a waveform seek bar: by naming
widgets. The built-in layout is unchanged.

**For a layout author.** Name widgets and the player moves them. A level or
peak can be an `lv_bar`, or an `lv_arc` for a round dial:

| Name | Shows |
| --- | --- |
| `vu_left`, `vu_right` | the level, quick up and slower down |
| `vu_peak_left`, `vu_peak_right` | the peak, held about 0.8 s, then falling |
| `vu_needle_left`, `vu_needle_right` | any object, turned about its transform pivot: 45° left of upright when empty, 45° right when full |
| `vu_band_1` … `vu_band_10`, `vu_band_peak_1` … | a spectrum in ten octave bands, 31 Hz to 16 kHz, each with a cap that is held and then falls |
| `vu_meter` | optional object around the meter; hidden as one with the controls while the lyrics are open |

The scale is linear in dB from -48 to full scale; the level is RMS + 3 dB, so
a full-scale sine fills the bar. A peak bar with `mode="range"` is drawn as a
short mark, to lay over the level bar; a peak arc always is. Indicators take the theme accent and
the level bars' track takes the backdrop rail style; local XML styles win.

**How it works.**

- `src/audio/visualization/level_meter.c`: a lock-free ring of ~10 ms blocks
  (peak and mean square per channel), written by the output owner thread and
  read by the UI thread. Each block carries the time it is heard, so the
  meter does not run a buffer ahead of the sound.
- `audio_output.c`: the two write functions feed it after a successful write.
  Local output uses `pcm_get_buffer_size()` for the delay. The aplay pipe
  targets use `FIONREAD` on the pipe plus 500 ms for aplay's buffer, and
  250 ms more for a Bluetooth link. DoP is skipped. The host SDL sink feeds
  it too, with the queued size as the delay.
- `src/ui/player/meter/player_vu_meter.c`: finds the bars, applies the
  ballistics, sets the values. Called from `player_ui_timer_cb` (the 50 ms
  tick), only while Now Playing is the active screen and the screen is on.
- The spectrum is ten second-order band-pass filters on the mono mix, in
  float, run only while a layout that names a band is asking. Filter state
  is zeroed below 1e-15 at each block so it never decays into denormals.
- The audio thread measures only while a meter asked within the last second.
  Otherwise the cost per chunk is one atomic load, and one clock read once a
  meter has ever been shown. Above 48 kHz every Nth frame is measured.
- Capability token `ui.player_layout_meter`. `PLUGIN_API_VERSION` is not
  raised.

**Tested.**

- `make target` builds and links with the target toolchain
  (`mipsel-linux-musl-gcc`); no warnings in the changed and new files.
- `level_meter.c` on the host under ASan/UBSan: levels of known signals
  (S16 and S24, 44.1 and 192 kHz), blocks not readable before they are due,
  ring overflow, the request timing out.
- `player_vu_meter.c` against this tree's patched LVGL and `lv_xml`, headless:
  the VU Meter Player layout XML at 480x800, 480x720, 320x480 and 240x320,
  driven with synthetic PCM. Bars rise to the expected value, fall, hold the
  peak, and return to empty when nothing arrives.

**On a device.** A HiBy R1. First on v1.5.1 with this change ported there,
then on v1.6.0 (the official player installed from the SD card, and this
build run from the card in its place):

- All three layouts of the VU Meter Player plugin load and move with an MP3
  playing through the local output: the bars, the dials with their needles
  and peak marks (both versions), and the spectrum (v1.6.0). Paused, the
  bars return to empty. The pictures in `../screenshots/` are framebuffer
  grabs.
- v1.6.0, dial layout: 40 screen off/on cycles with the power key, then a
  track skip with the screen off. No display failure. The player logged 64
  "Error panning display: Resource busy" lines over those cycles; I did not
  run the same loop on the Default layout, so I do not know whether the
  meter adds to them.
- Player CPU, single samples. v1.5.1, 15 s, same track: 10.7 % with the
  Default layout, 11.2 % and 14.2 % in two windows with the bars. v1.6.0,
  10 s with the screen on: 23 % with the spectrum.

**A display failure on v1.5.1, traced to v1.5.1.** Twice on v1.5.1 the
framebuffer driver stopped scanout (`soc_fb: continuous pan failed -5;
ST=0000008d`, then "Resource busy" on every pan until a reboot). With
display events logged, the cause was visible: after the screen goes off, a
scrolling title keeps invalidating, each invalidation restarts the paused
refresh, and the flush pans a blanked panel. The same happens on the Default
layout with the meter inactive. v1.6.0 pauses the marquees on screen-off
(`ui_marquees_sync`), and the meter itself draws nothing while
`backlight_screen_is_on()` is false: in the logged run no area of the meter
was invalidated after screen-off.

**Not tested.**

- Timing by measurement. With headphones in, the bars looked right to the
  one person who watched them; nothing more exact than that.
- Bluetooth and USB output. Their delay is an estimate and will likely want
  tuning; the two constants are at the top of the tap in `audio_output.c`.
- The bars layout on v1.6.0, and any layout with the lyrics open.
- The source of the "Resource busy" lines above.
- The R3 Pro II, the R3 II 2025 and the Tempotec V1, and the host simulator.

**For the docs.** `docs/reference/PLAYER_LAYOUTS.md` and the layouts page on
the site are not in this repository; the text is in `PLUGINS.md` of the
companion pull request to compas-plugins and can be copied from there.

The layout that uses this is `plugins/VUMeterPlayer` in that pull request.
