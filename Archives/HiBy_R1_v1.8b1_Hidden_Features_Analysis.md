# HiBy R1 v1.8.b1 — Hidden Features Analysis

**Device:** HiBy R1 Digital Audio Player
**Firmware:** v1.8.b1 (updated from v1.7b1; Built May 2026)
**Binary:** `hiby_player` — MIPS32 Little-Endian ELF, 4,984,856 bytes
**SoC:** Ingenic MIPS
**Analysis Date:** 2026-07-13 (updated from 2026-07-12)
**Method:** Ghidra 11.3 decompilation + config/resource file inspection

---

## How Features Are Gated

The firmware uses three independent gating mechanisms:

| Mechanism | File | How It Works |
|-----------|------|-------------|
| **UI Visibility** | `set_functions.json` | `0` = hidden from Settings menu, `1` = visible |
| **Runtime Flags** | `config.json` | Feature flags in `fcn0` array control behavior |
| **Binary String Gates** | `hiby_player` binary | Strings like `noradio`, `no_record` checked at runtime — if present, feature suppressed |

All three can be modified independently. JSON edits are safest. Binary patches are permanent.

---

## Hidden System Settings

**File:** `squashfs-root/usr/resource/set_functions.json`

11 features currently set to `0` (hidden). Change to `1` to reveal:

| Feature Key | UI Label | What It Does |
|------------|----------|-------------|
| `usb_mode` | USB device mode | Selector: **Storage** / **Audio** (USB DAC) / **Dock** (ADB shell access) |
| `dac_charge_disable` | USB current limited | Limits USB charge current in DAC mode — reduces charging noise in audio |
| `dac_feedback` | USB DAC feedback | USB DAC feedback mode toggle |
| `car_mode` | Car mode | Auto-play on power, screen behavior changes for car use |
| `car_mode_auto_play` | Auto play in car mode | Starts music automatically when car mode activates |
| `standby` | Standby | Low-power standby instead of full shutdown — faster wake |
| `double_touch_wakeup` | Double tap to wake up | Wake screen by double-tapping |
| `volkey_locked` | Volume when screen off | Physical volume keys work with screen off |
| `rotation` | Screen rotation 180° | Flip display upside down — for inverted mounting |
| `operating_instruction` | Operating Instructions | Built-in user manual |
| `about` | About | About page in Settings (already on launcher) |

### Patch — Enable All Hidden Settings

Replace entire `set_functions.json` content with:

```json
[{"type":"sys_set","funs":[{"language":1},{"backlight_set":1},{"color":1},{"font_size":1},{"theme":1},{"usb_working_mode":1},{"usb_mode":1},{"dac_charge_disable":1},{"dac_feedback":1},{"car_mode":1},{"car_mode_auto_play":1},{"time_set":1},{"powersave_switch":1},{"sleep_switch":1},{"battery_percent":1},{"standby":1},{"line_control":1},{"led":1},{"double_touch_wakeup":1},{"lock_key":1},{"volkey_locked":1},{"pull_menu_type":1},{"screensaver":1},{"rotation":1},{"restore":1},{"upgrade":1},{"certificate":1},{"operating_instruction":1},{"about":1}]}]
```

---

## Hidden config.json Flags

**File:** `squashfs-root/usr/resource/config.json`

| Flag | Default | Recommended | Effect |
|------|---------|-------------|--------|
| `dac_to_store` | 0 | **1** | Quick-switch between DAC and storage USB modes |
| `tf_image_cache_enable` | 0 | **1** | Cache album art on SD — faster browsing |
| `tf_music_db_enable` | 0 | **1** | Store music DB on SD — survives factory reset |
| `explorer_in_cue_enable` | 0 | **1** | **NEW in v1.8.b1** — CUE sheet browsing in file explorer |
| `otg_scan_enable` | 0 | **1** | **NEW in v1.8.b1** — Scan music on OTG USB devices |
| `qplay3_enable` | 0 | **1** | **NEW in v1.8.b1** — QPlay 3.0 (Tencent QQ Music streaming) |
| `vol_warn_enable` | 1 | **0** | Disable volume warning popup |
| `lock_vol.headset` | 50 | **100** | Remove volume cap (default caps at 50/100) |
| `warn_vol.headset` | 34 | **50** | Move warning threshold higher |
| `default_vol.headset` | 20 | any | Change power-on default volume |

---

## Developer Options

### Activation

About screen → tap firmware version number repeatedly (Android-style).

Binary evidence: `in_developer_mode` at `0x0079e158`, `about_dev_tv_developer` at `0x0079dd90`

### Available Options

| Option | Description |
|--------|-------------|
| **Gain adjustment** | Enable hardware gain control |
| **Volume lock** | Lock volume at current level |
| **Screenshot** | Short-press play button = screenshot |

---

## Factory Test Mode

### Activation

Create empty file on SD card:
```
/data/mnt/sd_0/hiby_linux_factory_mode
```

For automated test sequence:
```
/data/mnt/sd_0/hiby_linux_auto_test
```

### Available Tests (22)

LCD, Buttons, LED, WiFi, Touch, SD Card, OTG, Charge (MP2731 IC), BT Power, Audio Output, Standby Current, RF, Bluetooth, Line Control, Automated Sequence, Monkey Test, Shutdown, FM Radio (90.1/96.0/106.0 MHz), G-Sensor, Recorder, QR Code, One-Key Hardware Test

---

## ADB Shell Access

### Enable

1. `set_functions.json` → `"usb_mode": 1`
2. Device: Settings → USB device mode → **Dock**
3. Connect USB → ADB available

### Files

| Purpose | Path |
|---------|------|
| ADB daemon | `squashfs-root/usr/bin/adbd` |
| Start script | `squashfs-root/usr/bin/adbon` |
| Stop script | `squashfs-root/usr/bin/adboff` |

---

## Hidden Launcher Apps

31 launcher apps exist in binary. 13 hidden from default UI:

| Hidden App | Category | What It Does |
|-----------|----------|-------------|
| `phone_dac` | DAC | USB DAC mode for phone — shows format, sample rate, gain, PEQ |
| `pc_dac` | DAC | USB DAC mode for PC |
| `blue_dac` | DAC | Bluetooth receiver DAC — receive BT audio, output analog |
| `dac` | DAC | Generic DAC mode |
| `spdif_in` | Audio I/O | SPDIF digital audio input |
| `coax` | Audio I/O | Coaxial digital input |
| `optical` | Audio I/O | Optical digital input |
| `bt_in` | Connectivity | Bluetooth audio receiver mode |
| `darwin` | Audio DSP | Darwin DAC control — digital filter, DSD filter, NOS, harmonic/tube emulation |
| `recorder` | Apps | Voice recorder — WAV, configurable rate/channel/bits, noise reduction |
| `step` | Apps | Pedometer — KX126 accelerometer step counter |
| `usb_mode` | Settings | USB mode selector |
| `sound` | Audio DSP | Sound field / spatial audio |

---

## Hidden Audio DSP Features

### Darwin DAC Control

Advanced DAC-level processing. Launcher app `darwin` — hidden by default.

| Feature | Description |
|---------|-------------|
| Digital Filter | PCM oversampling filter — 3 selectable algorithms |
| DSD Filter | DSD-specific reconstruction filter |
| NOS Mode | Non-oversampling — bypasses digital filter for "analog" sound |
| Harmonic Controller | Tube amplifier emulation — shapes DAC harmonic distortion |
| Customized Presets | Save/load full Darwin configurations |

**NOS quick toggle** available in pull-down menu (`pull_down_menu_iv_nos`).

**Custom digital filters** — place filter binary in `/data/mnt/sd_0/filter/`, loaded via:
```
echo <name> > /sys/devices/platform/hm100/filter_path
```

### Parametric EQ (PEQ)

Full parametric EQ with per-band control:
- Frequency, Gain, Q factor per band
- Channel select (L/R/both)
- Pre-gain
- Per-band enable/disable
- Presets stored at `/data/peq/<name>.peq`
- Available in Phone DAC and Blue DAC modes too

### Sound Field

Stereo width control: 0.0 (mono) → 1.0 (normal) → 2.0 (wide). Step: 0.05.

### Balance

Two ranges depending on interface:
- HibyLink: -10 to +10, step 1
- Local UI: -20 to +20, step 0.5 dB

### Graphic EQ (GEQ)

| Parameter | Binary Address |
|-----------|---------------|
| `geq_enable` | `0x0077ad98` |
| `geq_band_gain` | `0x0077ada4` |
| `geq_pre_gain` | `0x0077adb4` |

### Tube Emulation

Hidden inside Darwin → Harmonic Controller.
Config: `/data/harmonic.json`
Binary refs: `V3_ANALOG_2025`, `v3_analog`, `TUBE=`

---

## SPDIF Digital I/O

### Output
- `spdif` flag at `0x0076cef4` — checked at 10+ locations
- `SPDIF_MAX_VOLUME` at `0x007f7754`
- `SPDIF_MAX_192K` at `0x007f7768` — max 192kHz output

### Input Switching (runtime)

```bash
# Optical:
echo op > /sys/devices/platform/jz-i2c.1/i2c-1/1-0048/set_spdif_input

# Coaxial:
echo co > /sys/devices/platform/jz-i2c.1/i2c-1/1-0048/set_spdif_input
```

---

## DSD Native Output

### Default: DoP only

`ot_devices.json` ships with:
```json
"AnalogDsdNative": "dop"
```

### Enable True Native

Change to:
```json
"AnalogDsdNative": "native"
```

### USB DSD Native (runtime)

```bash
echo 1 > /sys/class/android_usb/f_uac_sa/dsd_native_enable
```

### DSD Gain Compensation

7 levels: 0, -2, -4, -6, -8, -10, -12 dB

---

## Recording / Voice Recorder

Binary string gate: `no_record` at `0x007909b8` — checked at 10 locations.

If enabled:
- WAV format
- Configurable sample rate, channel (mono/stereo), bit depth
- Speech noise reduction (denoiser)

---

## Internet Radio

Binary string gate: `noradio` at `0x007909c4` — checked at 10 locations.

Full implementation exists. Available in CN region only by default.

- Data from: `https://otaserver.hiby.com/app/radio/getRadioInfo`
- Categories: Central, Provincial, Collection, Custom
- Custom stations: place URLs in `a:\radio.txt` on SD card
- Database: `/data/radio.db`

---

## MQA

Binary string gate: `mqa_disable` at `0x0079a98c` — checked at 3 locations.

MQA detection present. Reports MQA status via USB HID (`/dev/hidg0`) for external DAC indicators. Full decode capability unclear.

---

## Bluetooth Codecs

**File:** `squashfs-root/usr/resource/audio_back.conf`

| Codec | Status | Config Key |
|-------|--------|-----------|
| SBC | Enabled | `SBCSources=1` |
| AAC | Enabled (Source + Sink) | `AACSources=1`, `AACSinks=1` |
| aptX | Enabled | `APTXSources=1` |
| aptX HD | Enabled (via same aptX flag) | Automatic negotiation |
| LDAC | Enabled (HQ/SQ/ABR) | `LDACSources=1`, `LDACSinks=1` |
| UAT | Enabled (HiBy proprietary) | `UATSources=1`, `UATSinks=1` |
| MPEG1/2 | **Disabled** | `MPEG12Sources=0` → change to `1` |

Priority: UAT > LDAC > aptX HD > aptX > AAC > SBC

---

## Streaming Services

| Service | Status | Evidence |
|---------|--------|---------|
| Tidal | Full (OAuth, offline, quality select) | API: `api.tidal.com/v1` |
| Qobuz | Full | API: `qobuz.com/api.json/0.2` |
| Sony | Partial/planned | "No SONY account is bound" string exists |
| Spotify | Not present | — |
| Apple Music | Not present | — |
| Roon | Not present | — |

---

## MiDi R1 Dual Identity

Same binary serves both HiBy R1 and MiDi R1. Alternate configs at:
- `squashfs-root/usr/resource/midi_config.json`
- `squashfs-root/usr/resource/midi_set_functions.json`
- Layout: `usr/resource/layout/midi/theme1/`
- Images: `usr/resource/litegui/midi/theme1/`

Differences: MiDi version = 1.1, theme disabled, dark theme disabled.

> **Note:** v1.8.b1 adds `explorer_in_cue_enable`, `otg_scan_enable`, and `qplay3_enable` to the HiBy config.json but NOT to the MiDi midi_config.json.

---

## Binary Patch Reference

### String Gate Patches (Recommended — single edit disables all checks)

| Feature | Gate String | Virtual Address | Bytes to Replace | Method |
|---------|------------|----------------|-----------------|--------|
| Radio | `noradio` | `0x007909c4` | 8 bytes | Overwrite with `XXXXXXX\0` |
| Recording | `no_record` | `0x007909b8` | 10 bytes | Overwrite with `XXXXXXXXX\0` |
| MQA | `mqa_disable` | `0x0079a98c` | 12 bytes | Overwrite with `XXXXXXXXXXX\0` |

Replaces string content so all runtime comparisons fail → feature treated as enabled.

### config.json Flag Readers (code addresses)

| Flag | String VA | Code Read VA |
|------|----------|-------------|
| `playmenu_eq_disable` | `0x0076ce04` | `0x00419168` |
| `dark_theme_enable` | `0x0076ce18` | `0x00419174` |
| `screen_short_enable` | `0x0076ce2c` | `0x00419180` |
| `lyric_color_enable` | `0x0076ce40` | `0x0041918c` |
| `tf_image_cache_enable` | `0x0076ce54` | `0x00419198` |
| `tf_music_db_enable` | `0x0076ce6c` | `0x004191b8` |
| `vol_warn_enable` | `0x0076cf08` | `0x00419c98` |
| `spdif` | `0x0076cef4` | `0x00419c58` |

### Cross-Reference Table — All Check Locations

**`noradio` (10 checks):**
```
0x004bdf3c, 0x00519bcc, 0x00519bd8, 0x00519cd8, 0x0051a140,
0x0051a14c, 0x0051a020, 0x0051a02c, 0x004bd0e4, 0x004b1864
```

**`no_record` (10 checks):**
```
0x00519c54, 0x00519ec4, 0x00519c60, 0x0051a0c8, 0x0049be4c,
0x0049d864, 0x004a0e28, 0x00546484, 0x005464f0, 0x00546598
```

**`mqa_disable` (3 checks):**
```
0x0053f474, 0x00544b90, 0x004f4ba8
```

### File Offset Calculation

```
file_offset = virtual_address - segment_vaddr + segment_offset
```
Use `readelf -l hiby_player` or Ghidra memory map for segment mapping.

---

## Key System Functions (Binary)

| Function | Address | Purpose |
|----------|---------|---------|
| `system_if_dlna_turn_on` | `0x0046c834` | Start DLNA |
| `system_if_dlna_turn_off` | `0x0046c958` | Stop DLNA |
| `system_if_airplay_turn_on` | `0x004510c8` | Start AirPlay |
| `system_if_bluetooth_turn_on` | `0x00469b10` | Start BT |
| `system_if_bluetooth_turn_off` | `0x00469b70` | Stop BT |
| `system_if_bluetooth_connect` | `0x00469a5c` | Connect BT |
| `system_if_bluetooth_disconnect` | `0x00469abc` | Disconnect BT |
| `system_if_bluetooth_pair` | `0x00469df4` | Pair BT |
| `system_if_set_digital_filter` | `0x0046c568` | Set digital filter |
| `system_if_radio_tune_freq` | `0x0046d500` | Tune FM frequency |
| `sytem_if_connect_usb_dac` | `0x004694b0` | Connect USB DAC |
| `sytem_if_disconnect_usb_dac` | `0x00469450` | Disconnect USB DAC |

---

## Runtime Commands (via ADB/SSH)

```bash
# DSD native over USB
echo 1 > /sys/class/android_usb/f_uac_sa/dsd_native_enable

# SPDIF optical input
echo op > /sys/devices/platform/jz-i2c.1/i2c-1/1-0048/set_spdif_input

# SPDIF coaxial input
echo co > /sys/devices/platform/jz-i2c.1/i2c-1/1-0048/set_spdif_input

# Load custom digital filter
echo filtername > /sys/devices/platform/hm100/filter_path

# Start/stop services
cgic_enable          # Web file manager
cgic_disable
shairport_on.sh      # AirPlay
shairport_off.sh
bt_enable            # Bluetooth
bt_disable

# Battery logging
/usr/bin/batd -v -s -t5 -o /mnt/sd_0/batlog.txt &

# Vibrator test
echo 200 > /sys/class/timed_output/vibrator/enable
```

---

## Quick Enable Checklist

| # | Action | Risk | Method |
|---|--------|------|--------|
| 1 | Enable all hidden settings | None | Edit `set_functions.json`: all `0` → `1` |
| 2 | Enable SD caching | None | `config.json`: `tf_image_cache_enable: 1`, `tf_music_db_enable: 1` |
| 3 | Enable CUE browsing | None | `config.json`: `explorer_in_cue_enable: 1` (NEW in v1.8.b1) |
| 4 | Enable OTG scanning | None | `config.json`: `otg_scan_enable: 1` (NEW in v1.8.b1) |
| 5 | Enable QPlay 3.0 | Low | `config.json`: `qplay3_enable: 1` (NEW in v1.8.b1) |
| 6 | Remove volume cap | Low | `config.json`: `lock_vol.headset: 100` |
| 7 | Disable volume warning | None | `config.json`: `vol_warn_enable: 0` |
| 8 | Enable DSD native | Low | `ot_devices.json`: `AnalogDsdNative: "native"` |
| 9 | Enable MPEG1/2 BT | None | `audio_back.conf`: `MPEG12Sources=1` |
| 10 | Developer options | None | Tap FW version repeatedly in About |
| 11 | Factory test mode | Medium | Create empty `/data/mnt/sd_0/hiby_linux_factory_mode` |
| 12 | ADB access | None | Steps 1 + USB mode → Dock |
| 13 | Force-enable radio | Medium | Binary patch: overwrite `noradio` string |
| 14 | Force-enable recording | Medium | Binary patch: overwrite `no_record` string |
| 15 | Force-enable MQA | Medium | Binary patch: overwrite `mqa_disable` string |

---

## Ghidra Project

```
Project:  C:\tmp\ghidra_hiby\HibyProject
Script:   C:\tmp\ghidra_hiby\ExportFeatures.py
Output:   C:\tmp\ghidra_hiby\output\
  ├── functions.txt              173 KB — all function names + addresses
  ├── strings.txt                347 KB — all defined strings
  ├── feature_xrefs.txt           81 KB — feature string cross-references
  └── decompiled_interesting.txt 257 KB — decompiled feature functions
```

---

*Generated via Ghidra 11.3 headless decompilation + firmware file analysis*
*Firmware: HiBy R1 v1.8.b1 (updated from v1.7b1)*
