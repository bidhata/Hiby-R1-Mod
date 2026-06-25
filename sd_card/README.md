# HiBy R1 SD Card Configuration Files

Copy all `.ini` files to the root of your microSD card (maps to `a:\` on device).
Device reads these on boot — changes require restart.
Removing the SD card reverts to firmware defaults (safe fallback).

---

## play_settings.ini

| Key | Values | Default | Description |
|---|---|---|---|
| `play_mode` | 0=sequential, 1=repeat all, 2=repeat one, 3=shuffle | 0 | Playback order mode |
| `gapless_play` | 0=off, 1=on | 0 | Gapless/seamless playback between tracks |
| `dsd_output` | 0=DoP (DSD over PCM), 1=Native DSD | 0 | DSD output method (depends on DAC support) |
| `digital_filter` | 0=sharp roll-off, 1=slow roll-off, 2=short delay sharp, 3=short delay slow | 0 | DAC digital filter type (requires model unlock) |
| `replaygain_type` | 0=off, 1=track gain, 2=album gain | 0 | Replay gain mode |
| `type` | Integer (EQ preset index) | 0 | Active EQ preset type |
| `enable` | 0=off, 1=on | 0 | EQ master enable |
| `fade` | 0=off, 1=on | 0 | Track fade in/out (requires model unlock) |
| `memory` | 0=off, 1=on | 1 | Remember last volume on boot |
| `balance` | -20 to +20 (negative=left, positive=right) | 0 | Channel balance (requires model unlock) |
| `db_storage_change` | 0/1 | 0 | DB storage location change notification |

---

## sys_set.ini

| Key | Values | Default | Description |
|---|---|---|---|
| `language` | Locale string (e.g., `en`, `zh`, `ja`, `ko`) | en | UI language |
| `theme` | Integer (0-based theme index) | 0 | Active UI theme |
| `screen` | 0-100 | 80 | Screen brightness percentage |
| `screen_timeout` | Seconds (0=never off) | 60 | Screen auto-off timeout |
| `sleep_timer` | Minutes (0=disabled) | 0 | Auto-shutdown timer |
| `scan_music_gain` | String (display text) | (empty) | Scan progress display text |
| `fast_find_disable` | 0=fast find enabled, 1=disabled | 0 | Disable alphabetic fast-find index |
| `stream_media` | String | (empty) | Streaming media display label |
| `developer_options` | 0=hidden, 1=shown | 0 | Show developer options in settings |
| `japan_certificate` | String | (empty) | Japan certification display |

---

## eq.ini

| Key | Values | Default | Description |
|---|---|---|---|
| `eq_style` | Preset name string (e.g., `Custom`, `Rock`, `Pop`, `Jazz`, `Classical`) | (empty) | Active EQ preset name |

---

## exception.ini

| Key | Values | Default | Description |
|---|---|---|---|
| `dac_to_store` | 0=off, 1=on | 0 | DAC output to storage mode |
| `playmenu_eq_disable` | 0=EQ available, 1=EQ menu hidden | 0 | Hide EQ from play menu |
| `dark_theme_enable` | 0=off, 1=on | 0 | Enable dark theme |
| `screen_short_enable` | 0=off, 1=on | 0 | Enable screenshot feature |
| `lyric_color_enable` | 0=off, 1=on | 0 | Enable colored lyrics display |
| `tf_image_cache_enable` | 0=off, 1=on | 0 | Cache album art on SD card |
| `tf_music_db_enable` | 0=off, 1=on | 0 | Store music database on SD card |
| `disable_vol_warn` | 0=show warning, 1=suppress | 0 | Suppress EU volume warning popup |
| `locked_digital_vol` | Integer (e.g., 2000) | — | Fixed digital volume in DAC mode |

---

## mseb.ini

| Key | Values | Default | Description |
|---|---|---|---|
| `reset` | 0=keep settings, 1=reset to defaults | 0 | Reset MSEB on next boot |
| `name_repeat` | 0/1 | 0 | Preset name duplicate check |
| `mseb_name` | String | Default | Active MSEB preset name |
| `mseb_input` | Encoded parameter string | (empty) | MSEB DSP parameters (bass, vocal, warmth, etc.) |
| `set` | String (display label) | — | MSEB settings menu label |
| `save` | String (display label) | — | MSEB save button label |

**MSEB DSP Parameters** (set via SmartAudio engine, stored in mseb_input):
- `master_temp` — Overall warmth/coolness
- `boost` — Bass boost level
- `en_preamp` — Preamp enable
- `preamp` — Preamp gain
- `bass0` — Sub-bass
- `bass1` — Mid-bass
- `vocal` — Vocal presence
- `female` / `female_vocal` — Female vocal tuning
- `male_vocal` — Male vocal tuning
- `instruments` — Instrument clarity

> **Note:** MSEB UI is only accessible if the device model is spoofed to RS2/RS2II.

---

## bluetooth.ini

| Key | Values | Default | Description |
|---|---|---|---|
| `bt` | on/off | on | Bluetooth master switch |
| `aptx` | 0=off, 1=on | 1 | aptX codec enable (only SBC+aptX supported) |
| `bt_unpair` | String | — | Unpair confirmation text |
| `bt_err` | String | — | Bluetooth error display text |

---

## wifi.ini

| Key | Values | Default | Description |
|---|---|---|---|
| `wifi` | on/off | on | WiFi master switch |
| `ssid_input` | String | — | SSID input field label |
| `user_input` | String | — | Username input label |
| `connect_ok` | String | — | Connection success message |
| `connect_fail` | String | — | Connection failure message |
| `login_fail` | String | — | Login failure message |
| `login_timeout` | String | — | Login timeout message |
| `logining` | String | — | "Logging in..." display text |

---

## music.ini

| Key | Values | Default | Description |
|---|---|---|---|
| `album` | Display string | — | Album display format |
| `genre` | Display string | — | Genre display format |
| `year` | Display string | — | Year display format |
| `format` | Display string | — | Audio format display |
| `sample_rate` | Display string | — | Sample rate display |
| `unknown` | Display string | — | Unknown field placeholder text |

---

## Other INI Files (Referenced in Firmware)

| File | Key Keys | Purpose |
|---|---|---|
| `keyborad.ini` | `max_count=2000` | Maximum keyboard/search results |
| `radio.ini` | `self_broadcast`, `rm_broadcast` | Radio feature config |
| `tidal.ini` | Login state, quality tier | Tidal streaming account |
| `darwin.ini` | `digital_filter` | Darwin DAC filter setting |
| `m3u.ini` | `common_list`, `check_name`, `create_success`, `msg_remove_ask` | Playlist management labels |
| `songlist.ini` | (various) | Song list display |
| `firmware_update.ini` | `unknow_fail`, `net_update`, `downloading`, `download_success`, `update_if` | OTA update messages |

---

## Features Requiring Only INI Changes (No Binary Patch)

1. **Gapless playback** → `play_settings.ini`: `gapless_play=1`
2. **Dark theme** → `exception.ini`: `dark_theme_enable=1`
3. **Screenshots** → `exception.ini`: `screen_short_enable=1`
4. **Colored lyrics** → `exception.ini`: `lyric_color_enable=1`
5. **Album art cache on SD** → `exception.ini`: `tf_image_cache_enable=1`
6. **Music DB on SD** → `exception.ini`: `tf_music_db_enable=1`
7. **No volume warning** → `exception.ini`: `disable_vol_warn=1`
8. **Developer options** → `sys_set.ini`: `developer_options=1`
9. **aptX enabled** → `bluetooth.ini`: `aptx=1`
10. **EQ enabled** → `play_settings.ini`: `enable=1`

## Features Requiring Binary Patch + INI

- **Digital filter** → Needs model spoof to RS2/RS2II + `play_settings.ini`: `digital_filter=N`
- **Balance** → Needs model spoof + `play_settings.ini`: `balance=N`
- **Fade** → Needs model spoof + `play_settings.ini`: `fade=1`
- **MSEB** → Needs model spoof + `mseb.ini` configuration
