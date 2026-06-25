# HiBy R1 SD Card Configuration Files

Copy all `.ini` files to the root of your microSD card (maps to `a:\` on device).

## File Summary

| File | Purpose |
|---|---|
| `play_settings.ini` | Playback: play mode, gapless, DSD output, digital filter, replay gain, EQ |
| `sys_set.ini` | System: language, theme, screen, developer options, scan settings |
| `eq.ini` | Equalizer preset name |
| `exception.ini` | **Feature flags** — enables dark theme, screenshots, colored lyrics, disables volume warning |
| `mseb.ini` | MSEB DSP tuning (requires model spoof to access UI) |
| `bluetooth.ini` | Bluetooth state and aptX toggle |
| `wifi.ini` | WiFi state |
| `music.ini` | Music library display/sort fields |

## Key Features Enabled by exception.ini (No Binary Patch Needed)

- `dark_theme_enable=1` — Dark theme
- `screen_short_enable=1` — Screenshot capability
- `lyric_color_enable=1` — Colored lyrics display
- `tf_image_cache_enable=1` — Cache album art on SD (reduces re-scanning)
- `tf_music_db_enable=1` — Store music database on SD (preserves across reboots)
- `disable_vol_warn=1` — Suppress EU volume warning popup

## Notes

- The device reads these on boot. Changes require restart.
- Empty values (`key=`) mean "use device default" or "read from firmware."
- `play_settings.ini` keys like `digital_filter` and `fade` only work if the model allows those features (RS2/RS2II). On stock R1 they are ignored unless you patch the model check.
- Removing the SD card reverts all settings to firmware defaults (safe fallback).
