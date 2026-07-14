# HiBy R1 v1.8.b1 Firmware — Complete Hidden Features Analysis & Modding Guide

**Date:** 2026-07-13 (updated from v1.7b1 analysis of 2026-07-12)
**Binary:** `hiby_player` — MIPS32 Little-Endian ELF, 4,984,856 bytes
**SoC:** Ingenic (MIPS) — jz-i2c, jzfb display driver
**Engine:** HiBy Engine 45e6d09, Built Apr 22 2026 / May 28 2026
**Tools Used:** Ghidra 11.3 headless decompilation, string extraction, xref analysis, config file analysis

---

## Table of Contents

1. [Filesystem Layout](#1-filesystem-layout)
2. [Configuration Files — Full Paths](#2-configuration-files--full-paths)
3. [Feature Gate System](#3-feature-gate-system)
4. [Hidden Features — set_functions.json](#4-hidden-features--set_functionsjson)
5. [Hidden Features — config.json](#5-hidden-features--configjson)
6. [Developer Options](#6-developer-options)
7. [Factory Test Mode](#7-factory-test-mode)
8. [ADB / Dock Mode](#8-adb--dock-mode)
9. [Darwin DAC Control](#9-darwin-dac-control)
10. [Parametric EQ (PEQ)](#10-parametric-eq-peq)
11. [Sound Field & Balance](#11-sound-field--balance)
12. [SPDIF Input/Output](#12-spdif-inputoutput)
13. [Phone/Blue/PC DAC Modes](#13-phonebluepc-dac-modes)
14. [Recording / Voice Recorder](#14-recording--voice-recorder)
15. [Internet Radio](#15-internet-radio)
16. [Pedometer / Step Counter](#16-pedometer--step-counter)
17. [Tube Amplifier Emulation](#17-tube-amplifier-emulation)
18. [NOS Mode](#18-nos-mode)
19. [DSD Native over USB](#19-dsd-native-over-usb)
20. [MQA Decoder](#20-mqa-decoder)
21. [Bluetooth Codecs](#21-bluetooth-codecs)
22. [Streaming Services](#22-streaming-services)
23. [Binary Patch Points](#23-binary-patch-points)
24. [Feature Gate Binary Addresses](#24-feature-gate-binary-addresses)
25. [Shell Scripts Reference](#25-shell-scripts-reference)
26. [Libraries & Capabilities](#26-libraries--capabilities)
27. [Network Endpoints](#27-network-endpoints)
28. [MiDi R1 Dual Firmware](#28-midi-r1-dual-firmware)
29. [Audio Output Device Map](#29-audio-output-device-map)
30. [Complete Launcher Apps Map](#30-complete-launcher-apps-map)
31. [Quick Reference — How to Enable Everything](#31-quick-reference--how-to-enable-everything)

---

## 1. Filesystem Layout

```
squashfs-root/
├── bin/                          # BusyBox symlinks
├── dev/
├── etc/
│   ├── bluetooth/
│   │   └── main.conf             # BT config, Name="HiBy Music"
│   ├── dbus-1/
│   │   └── system.d/
│   │       ├── bluetooth.conf
│   │       ├── bluealsa.conf
│   │       └── wpa_supplicant.conf
│   ├── alsa/conf.d/20-bluealsa.conf
│   ├── mdev.conf
│   ├── ntp.conf
│   ├── resolv.conf
│   ├── thttpd.conf               # Web server config
│   ├── nsswitch.conf
│   └── wpa_supplicant.conf
├── lib/                          # System libs (libc, ld, etc.)
├── module_driver/                # Kernel modules
├── opt/
├── sbin/
├── usr/
│   ├── bin/                      # Main executables
│   │   ├── hiby_player           # *** MAIN BINARY ***
│   │   ├── hiby_player.sh        # Startup script
│   │   ├── sys_server            # System daemon
│   │   ├── shairport             # AirPlay receiver
│   │   ├── bluealsa              # BT audio daemon
│   │   ├── bluealsa-aplay        # BT audio player
│   │   ├── bsa_server            # Broadcom BT stack
│   │   ├── brcm_patchram_plus    # BCM BT firmware
│   │   ├── thttpd                # HTTP server
│   │   ├── cgic_deamon           # Web CGI daemon
│   │   ├── udp_server            # HiByLink discovery
│   │   ├── dmrd                  # DLNA renderer
│   │   ├── adbd                  # Android Debug Bridge
│   │   ├── adbon / adboff        # ADB toggle scripts
│   │   ├── cmd_jpeg_display      # Direct JPEG display
│   │   ├── memtester             # RAM tester
│   │   ├── strace                # Syscall tracer
│   │   ├── arecord / aplay       # ALSA utilities
│   │   ├── ntpdate               # NTP time sync
│   │   ├── bt_enable / bt_disable
│   │   ├── cgic_enable / cgic_disable
│   │   ├── wifi_on.sh / wifi_off.sh
│   │   ├── shairport_on.sh / shairport_off.sh
│   │   ├── dut.sh                # BT Device Under Test
│   │   ├── bootmode.sh           # Boot partition switch
│   │   ├── recovery_all.sh       # Factory reset
│   │   ├── uac_device_config.sh  # USB Audio Class setup
│   │   └── usb_dev_mass_storage.sh
│   ├── data/
│   │   ├── asound.conf           # ALSA config
│   │   └── region                # Region setting
│   ├── lib/                      # Application libraries
│   │   ├── libaac.so             # AAC decoder
│   │   ├── libmp3.so             # MP3 decoder
│   │   ├── libwma.so             # WMA decoder
│   │   ├── libopus.so            # Opus codec
│   │   ├── libvorbis.so          # Vorbis codec
│   │   ├── libsndfile.so         # Audio format lib
│   │   ├── libfdk-aac.so         # Fraunhofer AAC
│   │   ├── libdsd2pcm.so         # DSD to PCM
│   │   ├── libldacBT_enc.so      # LDAC encoder
│   │   ├── libldacBT_abr.so      # LDAC ABR
│   │   ├── libldacdec.so         # LDAC decoder
│   │   ├── libopenaptx.so        # aptX codec
│   │   ├── libsbc.so             # SBC codec
│   │   ├── libupnp.so            # UPnP/DLNA
│   │   ├── libixml.so            # XML (for DLNA)
│   │   ├── libthreadutil.so      # Thread utils (DLNA)
│   │   ├── libcurl.so            # HTTP client
│   │   ├── libcrypto.so          # OpenSSL crypto
│   │   ├── libssl.so             # OpenSSL SSL
│   │   ├── libbluetooth.so       # BT stack
│   │   ├── libqrencode.so        # QR code gen
│   │   ├── libfuse.so            # FUSE filesystem
│   │   ├── libntfs-3g.so         # NTFS support
│   │   ├── libjpeg.so            # JPEG support
│   │   └── libical.so            # iCal support
│   ├── resource/                 # *** UI RESOURCES ***
│   │   ├── config.json           # Product config + feature flags
│   │   ├── midi_config.json      # MiDi R1 alternate config
│   │   ├── set_functions.json    # System settings visibility flags
│   │   ├── midi_set_functions.json
│   │   ├── ot_devices.json       # Audio output devices + volumes
│   │   ├── audio_back.conf       # Bluetooth audio service config
│   │   ├── eq.ini                # EQ preset data (binary)
│   │   ├── bt_name               # Bluetooth device name ("HiBy R1")
│   │   ├── bt_more_than_192k_fail # BT sample rate >192kHz fail flag (=1)
│   │   ├── dac_out_more_than_384k_fail # DAC output >384kHz fail flag (=1)
│   │   ├── diff_layout.txt       # Layout version change tracking
│   │   ├── hl_json/              # HibyLink remote settings
│   │   ├── coordinate/           # Touch coordinate mapping
│   │   ├── eula/                 # End User License Agreement
│   │   │   ├── en                # English EULA text
│   │   │   ├── sc                # Simplified Chinese EULA text
│   │   │   └── status            # EULA acceptance status
│   │   ├── fonts/                # UI fonts
│   │   ├── layout/               # View definitions
│   │   │   ├── theme1/           # Light theme
│   │   │   ├── theme2/           # Dark theme
│   │   │   └── midi/theme1/      # MiDi variant
│   │   ├── litegui/              # PNG image assets
│   │   │   ├── theme1/
│   │   │   ├── theme2/
│   │   │   └── midi/theme1/
│   │   ├── str/                  # Localized strings (13 languages)
│   │   │   └── english/          # 50 files
│   │   ├── filter/               # Built-in digital filters
│   │   └── unicode/              # Unicode data
│   └── share/
│       ├── alsa/                 # ALSA config
│       ├── web/                  # Web file manager UI
│       │   ├── index-EN.html
│       │   ├── index-CN.html
│       │   └── cgi-bin/
│       └── dbus-1/
└── var/
```

---

## 2. Configuration Files — Full Paths

### Primary Config Files (EDIT THESE)

| File | Full Path | Purpose |
|------|-----------|---------|
| **set_functions.json** | `squashfs-root/usr/resource/set_functions.json` | Controls which settings appear in System Settings menu |
| **config.json** | `squashfs-root/usr/resource/config.json` | Product identity, volume config, feature flags |
| **ot_devices.json** | `squashfs-root/usr/resource/ot_devices.json` | Audio output devices, volume tables, DSD mode, sample rates |
| **audio_back.conf** | `squashfs-root/usr/resource/audio_back.conf` | Bluetooth audio service — codec enables, SCO routing |
| **midi_config.json** | `squashfs-root/usr/resource/midi_config.json` | MiDi R1 alternate product identity |
| **midi_set_functions.json** | `squashfs-root/usr/resource/midi_set_functions.json` | MiDi R1 settings visibility |

### String Files (UI Labels)

| File | Full Path | Feature Area |
|------|-----------|-------------|
| sys_set.ini | `squashfs-root/usr/resource/str/english/sys_set.ini` | System settings labels |
| developer_options.ini | `squashfs-root/usr/resource/str/english/developer_options.ini` | Developer options labels |
| darwin.ini | `squashfs-root/usr/resource/str/english/darwin.ini` | Darwin DAC control labels |
| mseb.ini | `squashfs-root/usr/resource/str/english/mseb.ini` | MSEB tuning labels |
| eq.ini (strings) | `squashfs-root/usr/resource/str/english/eq.ini` | EQ preset labels |
| play_settings.ini | `squashfs-root/usr/resource/str/english/play_settings.ini` | Playback settings labels |
| record.ini | `squashfs-root/usr/resource/str/english/record.ini` | Voice recorder labels |
| radio.ini | `squashfs-root/usr/resource/str/english/radio.ini` | Internet radio labels |
| line_out.ini | `squashfs-root/usr/resource/str/english/line_out.ini` | Line out warning label |
| bluetooth.ini | `squashfs-root/usr/resource/str/english/bluetooth.ini` | Bluetooth labels |
| tidal.ini | `squashfs-root/usr/resource/str/english/tidal.ini` | Tidal labels |
| net_settings.ini | `squashfs-root/usr/resource/str/english/net_settings.ini` | Network settings (DLNA/AirPlay) |
| launcher.ini | `squashfs-root/usr/resource/str/english/launcher.ini` | Launcher/home screen labels |
| step.ini | `squashfs-root/usr/resource/str/english/step.ini` | Pedometer labels |
| book.ini | `squashfs-root/usr/resource/str/english/book.ini` | E-book reader labels |
| about_dev.ini | `squashfs-root/usr/resource/str/english/about_dev.ini` | About device labels |
| usb.ini | `squashfs-root/usr/resource/str/english/usb.ini` | USB mode labels |
| darwin.ini | `squashfs-root/usr/resource/str/english/darwin.ini` | Darwin DAC labels |

### HibyLink Settings JSON

| File | Full Path | Purpose |
|------|-----------|---------|
| ui_main.json | `squashfs-root/usr/resource/hl_json/ui_main.json` | HibyLink main menu |
| hl_play_set_a.json | `squashfs-root/usr/resource/hl_json/hl_play_set_a.json` | Play settings via HibyLink |
| hl_sys_set_a.json | `squashfs-root/usr/resource/hl_json/hl_sys_set_a.json` | System settings via HibyLink |
| hl_balance_d.json | `squashfs-root/usr/resource/hl_json/hl_balance_d.json` | Balance control (-10..+10) |
| hl_digital_filter_d.json | `squashfs-root/usr/resource/hl_json/hl_digital_filter_d.json` | Digital filter selection |
| hl_dsd_output_d.json | `squashfs-root/usr/resource/hl_json/hl_dsd_output_d.json` | DSD output mode (PCM/DoP/Native) |
| hl_dsd_gain_d.json | `squashfs-root/usr/resource/hl_json/hl_dsd_gain_d.json` | DSD gain compensation |
| hl_replaygain_type_d.json | `squashfs-root/usr/resource/hl_json/hl_replaygain_type_d.json` | ReplayGain mode |
| hl_max_vol_d.json | `squashfs-root/usr/resource/hl_json/hl_max_vol_d.json` | Maximum volume |
| hl_default_vol_d.json | `squashfs-root/usr/resource/hl_json/hl_default_vol_d.json` | Default volume |
| create.json | `squashfs-root/usr/resource/hl_json/create.json` | HibyLink init |

### Runtime Data Paths (on device filesystem)

| Path | Purpose |
|------|---------|
| `/data/usrlocal_media.db` | Music library database |
| `/data/radio.db` | Internet radio database |
| `/data/harmonic.json` | Harmonic/tube emulation config |
| `/data/peq/` | PEQ preset directory |
| `/data/peq/%s.peq` | Individual PEQ preset files |
| `/data/menu_cfg` | Menu configuration |
| `/data/theme_id` | Current theme ID |
| `/data/bt_list.txt` | Bluetooth paired devices |
| `/data/samba_password` | SMB credentials |
| `/data/samba_list.txt` | SMB share list |
| `/data/samba_shares` | Mounted SMB shares |
| `/data/wifi_signal.txt` | WiFi signal data |
| `/data/step` | Pedometer data |
| `/data/mnt/sd_0/.tidal` | Tidal offline cache |
| `/data/mnt/sd_0/.temp/most_played.db` | Most played database |
| `/data/mnt/sd_0/filter/%s` | Custom digital filter files |
| `/data/mnt/sd_0/hiby_linux_factory_mode` | Factory mode trigger |
| `/data/mnt/sd_0/hiby_linux_auto_test` | Auto test trigger |
| `/usr/data/region` | Region setting |
| `/usr/resource/hostname` | Device hostname |
| `/usr/resource/bt_name` | Bluetooth device name |
| `/usr/resource/filter/` | Built-in digital filter files |
| `/mnt/sd_0/batlog.txt` | Battery logging output |
| `/mnt/sd_0/screensavers/` | Custom screensaver images |

### System Interfaces (sysfs)

| Path | Purpose |
|------|---------|
| `/sys/devices/platform/hm100/filter_path` | Custom digital filter loader |
| `/sys/devices/platform/jz-i2c.1/i2c-1/1-0048/set_spdif_input` | SPDIF input switching (op/co) |
| `/sys/class/android_usb/f_uac_sa/dsd_native_enable` | DSD native over USB |
| `/sys/class/input/input%s/stepcnt_enable` | Step counter enable |
| `/sys/class/input/input%s/stepcnt_steps` | Step count value |
| `/sys/class/input/input%s/stepcnt_clear` | Step counter clear |
| `/sys/class/timed_output/vibrator/enable` | Vibrator control |
| `/etc/bluetooth/pts_enable` | BT PTS test mode |
| `/dev/hidg0` | USB HID gadget (MQA status reporting) |

---

## 3. Feature Gate System

Binary reads feature flags from two JSON files at startup:

### `config.json` — Product & Feature Flags
- Binary reads string `z:\config.json` at `0x0077bdc4`
- Flags in `fcn0` array control runtime behavior
- String addresses for each flag (used as lookup keys):

| Flag String | Binary Address | Read At |
|-------------|---------------|---------|
| `dac_to_store` | `0x0076cde0` | — |
| `book_set_percent` | `0x0076cdf0` | — |
| `playmenu_eq_disable` | `0x0076ce04` | `0x00419168` |
| `dark_theme_enable` | `0x0076ce18` | `0x00419174` |
| `screen_short_enable` | `0x0076ce2c` | `0x00419180` |
| `lyric_color_enable` | `0x0076ce40` | `0x0041918c` |
| `tf_image_cache_enable` | `0x0076ce54` | `0x00419198` |
| `tf_music_db_enable` | `0x0076ce6c` | `0x004191b8` |
| `vol_warn_enable` | `0x0076cf08` | `0x00419c98` |

### `set_functions.json` — UI Visibility
- Binary reads string `z:\set_functions.json` at `0x007982a0`
- Each `"feature": 0/1` hides/shows the corresponding settings item

### `noradio` / `no_record` Feature Suppression
These strings in binary gate whether Radio and Recording features appear:

| String | Binary Address | Referenced By (xrefs) |
|--------|---------------|----------------------|
| `no_record` | `0x007909b8` | `0x00519c54, 0x00519ec4, 0x00519c60, 0x0051a0c8, 0x0049be4c, 0x0049d864, 0x004a0e28, 0x00546484, 0x005464f0, 0x00546598` |
| `noradio` | `0x007909c4` | `0x004bdf3c, 0x00519bcc, 0x00519bd8, 0x00519cd8, 0x0051a140, 0x0051a14c, 0x0051a020, 0x0051a02c, 0x004bd0e4, 0x004b1864` |
| `mqa_disable` | `0x0079a98c` | `0x0053f474, 0x00544b90, 0x004f4ba8` |

---

## 4. Hidden Features — set_functions.json

**File:** `squashfs-root/usr/resource/set_functions.json`

### Current State (features set to 0 = HIDDEN):

```json
[
    {
        "type":"sys_set",
        "funs":[
            {"language":1},
            {"backlight_set":1},
            {"color":1},
            {"font_size":1},
            {"theme":1},
            {"usb_working_mode":1},
            {"usb_mode":0},
            {"dac_charge_disable":0},
            {"dac_feedback":0},
            {"car_mode":0},
            {"car_mode_auto_play":0},
            {"time_set":1},
            {"powersave_switch":1},
            {"sleep_switch":1},
            {"battery_percent":1},
            {"standby":0},
            {"line_control":1},
            {"led":1},
            {"double_touch_wakeup":0},
            {"lock_key":1},
            {"volkey_locked":0},
            {"pull_menu_type":1},
            {"screensaver":1},
            {"rotation":0},
            {"restore":1},
            {"upgrade":1},
            {"certificate":1},
            {"operating_instruction":0},
            {"about":0}
        ]
    }
]
```

### Patched Version (ALL ENABLED):

```json
[
    {
        "type":"sys_set",
        "funs":[
            {"language":1},
            {"backlight_set":1},
            {"color":1},
            {"font_size":1},
            {"theme":1},
            {"usb_working_mode":1},
            {"usb_mode":1},
            {"dac_charge_disable":1},
            {"dac_feedback":1},
            {"car_mode":1},
            {"car_mode_auto_play":1},
            {"time_set":1},
            {"powersave_switch":1},
            {"sleep_switch":1},
            {"battery_percent":1},
            {"standby":1},
            {"line_control":1},
            {"led":1},
            {"double_touch_wakeup":1},
            {"lock_key":1},
            {"volkey_locked":1},
            {"pull_menu_type":1},
            {"screensaver":1},
            {"rotation":1},
            {"restore":1},
            {"upgrade":1},
            {"certificate":1},
            {"operating_instruction":1},
            {"about":1}
        ]
    }
]
```

### What Each Hidden Feature Does

| Feature | UI Label | Description |
|---------|----------|-------------|
| `usb_mode` | "USB device mode" | Shows selector: **Storage** (mass storage), **Audio** (USB DAC), **Dock** (ADB debug) |
| `dac_charge_disable` | "USB current limited" | Limits USB charging current when connected as DAC — prevents noise from charging circuit |
| `dac_feedback` | "USB DAC feedback" | USB DAC feedback mode toggle |
| `car_mode` | "Car mode" | Dedicated car mode — auto-starts playback, screen behavior changes |
| `car_mode_auto_play` | "Auto play in car mode" | Automatically starts music playback when entering car mode |
| `standby` | "Standby" | Low-power standby instead of full shutdown — faster resume |
| `double_touch_wakeup` | "Double tap to wake up" | Wake the screen by double-tapping on it |
| `volkey_locked` | "Volume operation when screen off" | Allow volume adjustment with physical keys when screen is off |
| `rotation` | "Screen rotation" — "180 degrees" | Rotate display 180 degrees — useful for upside-down mounting |
| `operating_instruction` | "Operating Instructions" | Built-in user manual/help system |
| `about` | "About" | Shows About in settings (already on launcher, this adds it to settings too) |

---

## 5. Hidden Features — config.json

**File:** `squashfs-root/usr/resource/config.json`

### Current State (v1.8.b1):

```json
[
    {"type":"slef", "version":1},
    {
        "type":"product",
        "company":"HiBy",
        "device":"R1",
        "version":"1.8.b1",
        "ota_name":"HiBy R1",
        "usb_pid":"0x0101",
        "dac_pid":"0x0004",
        "vid":"0x32BB"
    },
    {"type":"screen", "hor":480, "ver":800, "topbar_height":50, "sub_back_height":84, "list_item_height":126},
    {
        "type":"volume",
        "vol_warn_enable":1,
        "lock_vol":[{"headset":50}],
        "default_vol":[{"headset":20}],
        "warn_vol":[{"headset":34}]
    },
    {
        "type":"function",
        "fcn0":[
            {"dac_to_store":0},
            {"book_set_percent":1},
            {"most_played":1},
            {"playmenu_eq_disable":0},
            {"dark_theme_enable":1},
            {"screen_short_enable":1},
            {"lyric_color_enable":1},
            {"tf_image_cache_enable":0},
            {"tf_music_db_enable":0},
            {"explorer_in_cue_enable":0},
            {"otg_scan_enable":0},
            {"qplay3_enable":0}
        ]
    }
]
```

### Editable Flags

| Flag | Current | Recommended | Effect |
|------|---------|-------------|--------|
| `dac_to_store` | 0 | 1 | Enables switching between DAC and storage USB modes without menu |
| `tf_image_cache_enable` | 0 | 1 | Caches album art on SD card — faster browsing, uses SD space |
| `tf_music_db_enable` | 0 | 1 | Stores music database on SD card — survives factory reset |
| `explorer_in_cue_enable` | 0 | 1 | **NEW in v1.8.b1** — Enables CUE sheet browsing in file explorer |
| `otg_scan_enable` | 0 | 1 | **NEW in v1.8.b1** — Enables scanning music on OTG-connected USB devices |
| `qplay3_enable` | 0 | 1 | **NEW in v1.8.b1** — Enables QPlay 3.0 (Tencent QQ Music streaming protocol) |
| `playmenu_eq_disable` | 0 | 0 | Keep at 0 = EQ enabled in play menu. Set to 1 = disable EQ |
| `vol_warn_enable` | 1 | 0 | Set to 0 to disable volume warning popup at volume 34 |
| `warn_vol.headset` | 34 | 50 | Change volume level that triggers warning |
| `lock_vol.headset` | 50 | 100 | Change maximum volume limit (100 = no limit) |
| `default_vol.headset` | 20 | any | Change default power-on volume |

---

## 6. Developer Options

### Activation Method

**Binary evidence:**
- String: `in_developer_mode` at `0x0079e158`
- String: `developer_mode` at `0x0079e16c`
- String: `about_dev_tv_developer` at `0x0079dd90`
- About screen reads `about_dev.ini` which contains: "Perform 2 more steps to enter development mode"

**Steps:** Go to About screen, tap firmware version number multiple times (Android-style)

### Developer Options Available

From `developer_options.ini`:
1. **Enable gain adjustment** — `gain_enable` — allows adjusting gain level
2. **Volume lock** — `volume_locked` — locks volume at current level
3. **Screenshot** — `screen_short` — short press play button = take screenshot

### Binary References

| String | Binary Address |
|--------|---------------|
| `developer_options.ini` | `0x00798a10` |
| `vg_listview_developer_options` | `0x00798470`, `0x0089d05c` |
| `VG_LISTVIEW_DEVELOPER_OPTIONS` | `0x0079079c` |
| Layout file | `z:\layout\theme1\listview\vg_listview_developer_options.listview` at `0x0089cf50` |

---

## 7. Factory Test Mode

### Activation

**Method 1:** Create empty file on SD card root:
```
/data/mnt/sd_0/hiby_linux_factory_mode
```

**Method 2:** For automated testing:
```
/data/mnt/sd_0/hiby_linux_auto_test
```

### Binary References

| String | Binary Address |
|--------|---------------|
| `/data/mnt/sd_0/hiby_linux_factory_mode` | `0x0079b38c` |
| `factory_mode` | `0x0076fdb0` |
| `factory` | `0x0079b3b4` |

### Available Tests (from test.ini + binary)

| # | Test | Description |
|---|------|-------------|
| 1 | QR Code | QR code scanning test |
| 2 | One-Key | All-in-one hardware test |
| 3 | LCD | Display test (colors, patterns) |
| 4 | Key/Button | Physical button test |
| 5 | LED | LED indicator test |
| 6 | WiFi | WiFi connectivity test |
| 7 | Touch | Touchscreen calibration test |
| 8 | TF Card | MicroSD card read/write test |
| 9 | OTG | USB OTG test |
| 10 | Charge | Charging circuit test (MP2731 charger IC) |
| 11 | BT Power | Bluetooth power-on test |
| 12 | Audio | Audio output test |
| 13 | Standby | Standby current measurement |
| 14 | RF | RF test |
| 15 | Bluetooth | Bluetooth communication test |
| 16 | Line Control | Wired remote control test |
| 17 | Order | Automated test sequence |
| 18 | Monkey | Random input stress test |
| 19 | Shutdown | Shutdown test |
| 20 | FM | FM radio test (90.1, 96.0, 106.0 MHz) |
| 21 | G-Sensor | Accelerometer test |
| 22 | Recorder | Microphone/recording test |

---

## 8. ADB / Dock Mode

### Enabling

1. Edit `set_functions.json`: change `"usb_mode":0` to `"usb_mode":1`
2. On device: System Settings -> USB device mode -> Select "Dock"
3. Device runs `adbon` script which starts `adbd`

### Files

| File | Path | Purpose |
|------|------|---------|
| adbd | `squashfs-root/usr/bin/adbd` | ADB daemon binary |
| adbon | `squashfs-root/usr/bin/adbon` | Start ADB script |
| adboff | `squashfs-root/usr/bin/adboff` | Stop ADB script |
| ADB dialog | `squashfs-root/usr/resource/layout/theme1/dialog/adb.dlg` | ADB mode UI |
| ADB icon | `squashfs-root/usr/resource/litegui/theme1/topbar/adb.png` | Status bar icon |

---

## 9. Darwin DAC Control

Advanced DAC-level DSP features, tied to specific DAC chips used in HiBy players.

### Available Controls

From `darwin.ini`:
1. **Digital Filter** — PCM oversampling filter selection
2. **DSD Filter** — DSD-specific reconstruction filter
3. **NOS** — Non-Oversampling mode (bypass digital filter)
4. **Harmonic Controller** — DAC harmonic distortion tuning / tube emulation
5. **Customized Presets** — Save/load Darwin configurations

### Binary References

| String | Binary Address |
|--------|---------------|
| `darwin.ini` | `0x00798938` |
| `vg_listview_darwin` | `0x0079949c`, `0x00892678` |
| `VG_LISTVIEW_DARWIN` | `0x007904f8` |
| `darwin_control` | `0x007a378c` |
| `lg_darwin_listview` | `0x007a3778` |
| `lg_darwin_digital_filter_listview` | `0x007a35f0` |
| Layout | `z:\layout\theme1\listview\vg_listview_darwin.listview` at `0x0089256c` |
| `pull_down_menu_iv_darwin` | `0x008ab208` |
| `launcher_apps_vg_darwin` | `0x008ac698` |

### Custom Filter Loading

```
# Custom filters on SD card:
/data/mnt/sd_0/filter/<filter_name>

# System loads via:
echo <filter_name> > /sys/devices/platform/hm100/filter_path

# Built-in filters at:
/usr/resource/filter/<filter_name>
```

---

## 10. Parametric EQ (PEQ)

Full parametric equalizer with individual band control.

### Parameters Per Band

| Parameter | Binary String | Address |
|-----------|--------------|---------|
| Channel select | `peq_param_set_channel` | `0x007cb780` |
| Frequency | `peq_param_set_fvalue` | `0x007cb7b0` |
| Gain | `peq_param_set_gvalue` | `0x007cb798` |
| Q factor | `peq_param_set_qvalue` | `0x007cb7e0` |
| Pre-gain | `peq_param_set_pregain` | `0x007cb7c8` |
| Band enable | `iv_band_enable` | `0x0079a3bc`, `0x008a264c` |
| PEQ enable | `iv_peq_enable` | `0x0079a394`, `0x008a252c` |
| PEQ reset | `iv_peq_reset` | `0x0079a384`, `0x008a258c` |

### Preset Storage

```
/data/peq/               # Preset directory
/data/peq/<name>.peq      # Individual preset files
```

### UI Layout

| Element | Address |
|---------|---------|
| `hiby_peq` | `0x00798d34` |
| `vg_peq_hiby` | `0x0076fc00`, `0x008a246c` |
| `hiby_peq.view` | `0x008a2368` |
| `peq_filter.dlg` | `0x00890c20` |

### PEQ Available in DAC Modes

| Element | Address |
|---------|---------|
| `phone_dac_iv_peq_switch` | `0x0079e930`, `0x008a8270` |
| `phone_dac_vg_peq` | `0x0079e9e0`, `0x008a8390` |
| `blue_dac_iv_peq_switch` | `0x0079ebe4`, `0x008a85b4` |

---

## 11. Sound Field & Balance

### Sound Field (Stereo Width)

| Parameter | Value |
|-----------|-------|
| Width range | 0.0 to 2.0 |
| Step | 0.05 |
| Default | 1.0 (normal stereo) |
| Enable/disable | Toggle |

### Balance Control

**From HibyLink JSON (`hl_balance_d.json`):**
- Range: -10 to +10
- Step: 1
- Labels: L (left) / R (right)

**From binary (inline JSON at `0x0077b1b8`):**
- Range: -20 to +20
- Step: 0.5 dB
- UI: "Balance settings" dialog with enable checkbox and horizontal slider

---

## 12. SPDIF Input/Output

### SPDIF Output

| String | Address |
|--------|---------|
| `SPDIF Output` | `0x00779c8c` |
| `spdif` | `0x0076cef4` |
| `SPDIF_MAX_VOLUME` | `0x007f7754` |
| `SPDIF_MAX_192K` | `0x007f7768` |

### SPDIF Input

| String | Address |
|--------|---------|
| `vg_spdif_input_hiby` | `0x007a3268`, `0x008ad56c` |
| `spdif_input_iv_icon` | `0x007a327c` |
| `spdif_input_tv_sample_rate` | `0x007a3290` |
| `spdif_input_tv_type` | `0x007a32ac` |
| `hiby_spdif_input` | `0x007a3314` |
| `spdif_set_vg_coaxial` | `0x008ad730` |
| `spdif_set_vg_optical` | `0x008ad790` |
| `launcher_apps_vg_spdif_in` | `0x008ac578` |
| `launcher_apps_vg_coax` | `0x008ac758` |
| `launcher_apps_vg_optical` | `0x008ac7b8` |

### SPDIF Input Switching (sysfs)

```bash
# Switch to optical input:
echo op > /sys/devices/platform/jz-i2c.1/i2c-1/1-0048/set_spdif_input

# Switch to coaxial input:
echo co > /sys/devices/platform/jz-i2c.1/i2c-1/1-0048/set_spdif_input
```

---

## 13. Phone/Blue/PC DAC Modes

### Phone DAC Mode
Acts as USB DAC for connected phone.

| Element | Address |
|---------|---------|
| `hiby_phone_dac` | `0x007a1168` |
| `vg_phone_dac_hiby` | `0x0076c2e0`, `0x008a81b0` |
| `launcher_apps_vg_phone_dac` | `0x008abd98` |
| `phone_dac_iv_gain_mode` | `0x0079e998`, `0x008a8330` |
| `phone_dac_iv_output_mode` | `0x0079e964`, `0x008a8390` |
| `phone_dac_iv_peq_switch` | `0x0079e930`, `0x008a8270` |
| `phone_dac_iv_format` | `0x0079e904` |
| `phone_dac_nv_sample_rate` | `0x0079e9c4` |
| `phone_dac_nv_battery` | `0x0079e8b0` |

### Blue DAC Mode (Bluetooth Receiver DAC)

| Element | Address |
|---------|---------|
| `launcher_apps_vg_blue_dac` | `0x008ac878` |
| `blue_dac_iv_peq_switch` | `0x0079ebe4`, `0x008a85b4` |
| `blue_dac_tv_peq` | `0x0079ec40` |

### PC DAC Mode

| Element | Address |
|---------|---------|
| `launcher_apps_vg_pc_dac` | `0x008ac818` |

### USB DAC System Functions

| String | Address | Purpose |
|--------|---------|---------|
| `sytem_if_connect_usb_dac` | `0x0077abe8` | Connect USB DAC |
| `sytem_if_disconnect_usb_dac` | `0x0077ac04` | Disconnect USB DAC |
| `usb_dac_init` | `0x007f5ae8` | Init USB DAC |
| `usb_dac_done` | `0x007f5ad8` | Shutdown USB DAC |

---

## 14. Recording / Voice Recorder

### Features

From `record.ini`:
- Format selection (WAV)
- Sample rate selection
- Channel: mono / stereo
- Bit depth selection
- Speech noise reduction (denoiser)

### Binary References

| String | Address |
|--------|---------|
| `vg_record_hiby` | `0x00773d54` |
| `vg_listview_record_operation` | `0x00773df0` |
| `vg_listview_record_set` | `0x0079754c` |
| `record.ini` | `0x00796b24` |
| `launcher_apps_vg_recorder` | `0x008ac278` |
| `VG_LISTVIEW_RECORD_OPERATION` | `0x00790414` |
| `VG_LISTVIEW_RECORD_SET` | `0x00790438` |

---

## 15. Internet Radio

### Availability

Full implementation exists. Available only in Chinese region UI variant (`hiby_stream_media_cn.view`). International variant hides it.

### Data Source

```
https://otaserver.hiby.com/app/radio/getRadioInfo
```

### Categories (from `radio.ini`)

- Central radio
- Provincial radio
- My collection
- Custom radio (from `a:\radio.txt` on SD card)

### Binary References

| String | Address | Xrefs |
|--------|---------|-------|
| `net_radio` | `0x00770614` | `0x004bdadc, 0x004c674c, 0x0055f838, 0x00560e88, ...` |
| `vg_radio_hiby` | `0x00773cec` | multiple |
| `radio.ini` | `0x00794f2c` | `0x00532944, 0x004b9370, 0x004b1740` |
| `/data/radio.db` | `0x0076d90c` | multiple |
| `launcher_apps_vg_net_radio` | `0x008ac038` | — |
| `noradio` (gate) | `0x007909c4` | 10 xrefs |
| `a:\radio.txt` | `0x00794f44` | `0x004b1bf0` |

---

## 16. Pedometer / Step Counter

### Hardware

- Chip: KX126 3-axis accelerometer with built-in step counter
- String: `kx126-stepcnt` at `0x00779cc0`

### Sysfs Interface

```
/sys/class/input/input%s/stepcnt_enable   # Enable/disable
/sys/class/input/input%s/stepcnt_steps    # Read step count
/sys/class/input/input%s/stepcnt_clear    # Reset counter
```

### Binary References

| String | Address |
|--------|---------|
| `launcher_apps_vg_step` | `0x008ac158` |
| Step data path | `/data/step` |

---

## 17. Tube Amplifier Emulation

Hidden in the Harmonic Controller feature within Darwin DAC Control.

| String | Address |
|--------|---------|
| `Harmonic_control` | in `darwin.ini` |
| `/data/harmonic.json` | runtime config |
| `V3_ANALOG_2025` | in binary strings |
| `v3_analog` | in binary strings |
| `TUBE=` | parameter in binary |

The DAC's harmonic distortion profile can be tuned to emulate the warm character of vacuum tube amplifiers. Configuration stored in `/data/harmonic.json`.

---

## 18. NOS Mode

Non-Oversampling mode bypasses the digital reconstruction filter in the DAC.

| String | Address |
|--------|---------|
| `NOS` | in `darwin.ini` |
| `NOS_EN` | in binary |
| `pull_down_menu_iv_nos` | pull-down quick toggle |

Bypasses digital filter for more "analog" sound. Available as quick toggle in pull-down menu or via Darwin settings.

---

## 19. DSD Native over USB

### Sysfs Control

```bash
# Enable DSD native:
echo 1 > /sys/class/android_usb/f_uac_sa/dsd_native_enable

# Disable:
echo 0 > /sys/class/android_usb/f_uac_sa/dsd_native_enable
```

### Binary References

| String | Address |
|--------|---------|
| `dsd native enable` | `0x007f5648` |
| `dsd native disable` | `0x007f565c` |
| `/sys/class/android_usb/f_uac_sa/dsd_native_enable` | `0x007f5990` |

### DSD Output Modes (from `ot_devices.json`)

```json
{
    "DSDMode": 1,
    "AnalogDsdD2p": "dop",
    "AnalogDsdDop": "dop",
    "AnalogDsdNative": "dop"
}
```

**To enable true DSD native on analog output:** Change `"AnalogDsdNative": "dop"` to `"AnalogDsdNative": "native"` in `ot_devices.json`.

### DSD Gain Compensation

7 levels available: 0, -2, -4, -6, -8, -10, -12 dB (from `ot_devices.json` DSDGain.Values)

---

## 20. MQA Decoder

### Status

MQA **detection** is present. Full decode capabilities unclear from binary analysis.

| String | Address | Xrefs |
|--------|---------|-------|
| `mqa_decoder` | `0x00793330` | `0x004a911c, 0x004a9128, 0x004d3744, 0x004d3750` |
| `mqa_disable` | `0x0079a98c` | `0x0053f474, 0x00544b90, 0x004f4ba8` |
| `report_mqa_info` | `0x0077a3bc` | — |
| MQA reporting via HID | `/dev/hidg0` | — |
| MQA flag in Tidal DB | `mqa INT` column | — |

MQA playback status reported to USB HID device (`/dev/hidg0`) for external DAC/amp MQA indicator LEDs.

---

## 21. Bluetooth Codecs

### Configuration File

**File:** `squashfs-root/usr/resource/audio_back.conf`

```ini
[A2DP]
SBCSources=1
LDACSources=1
LDACSinks=1
APTXSources=1
AACSources=1
AACSinks=1
UATSources=1
UATSinks=1
MPEG12Sources=0     # Can enable MPEG1/2 audio over BT
```

### Codec Details from Binary

| Codec | Notes |
|-------|-------|
| SBC | Standard, always available |
| AAC | Source + Sink |
| aptX | Source, toggleable (`bt_aptx on/off` at `0x00779300`) |
| aptX HD | `APTX_HD` at `0x0077c51c` |
| LDAC | HQ/SQ/ABR modes (`LDAC_HQ/SQ/ABR` at `0x00779370-0x00779380`) |
| UAT | HiBy proprietary — 600K, 900K, 1.2M bitrate |

### Enabling MPEG1/2

In `audio_back.conf`, change `MPEG12Sources=0` to `MPEG12Sources=1`.

---

## 22. Streaming Services

### Tidal — Full Integration

| Component | Value |
|-----------|-------|
| API base | `https://api.tidal.com/v1` |
| OAuth device auth | `https://auth.tidal.com/v1/oauth2/device_authorization` |
| Token endpoint | `https://auth.tidal.com/v1/oauth2/token` |
| Resources | `https://resources.tidal.com` |
| Offline cache | `/data/mnt/sd_0/.tidal` |

### Qobuz — Full Integration

| Component | Value |
|-----------|-------|
| API base | `http://www.qobuz.com/api.json/0.2` |

### Sony — Partial/Planned

String references exist ("No SONY account is bound") but no dedicated icon assets.

### NOT Present

- Spotify: No references
- Apple Music: No references
- Roon: No references

---

## 23. Binary Patch Points

### Feature Gate Config Readers

Binary reads `config.json` flags starting around `0x00419168`:

| Flag | String Address | Config Read Address |
|------|---------------|-------------------|
| `playmenu_eq_disable` | `0x0076ce04` | `0x00419168` |
| `dark_theme_enable` | `0x0076ce18` | `0x00419174` |
| `screen_short_enable` | `0x0076ce2c` | `0x00419180` |
| `lyric_color_enable` | `0x0076ce40` | `0x0041918c` |
| `tf_image_cache_enable` | `0x0076ce54` | `0x00419198` |
| `tf_music_db_enable` | `0x0076ce6c` | `0x004191b8` |
| `vol_warn_enable` | `0x0076cf08` | `0x00419c98` |
| `spdif` | `0x0076cef4` | `0x00419c58` |

### Feature Suppression Gates — Patching Guide

| Feature | Gate String | Address (v1.8.b1) | Address (v1.7b1) | Xrefs (Check locations) |
|---------|-------------|-------------------|------------------|-------------------------|
| Recording | `no_record` | `0x007954f8` | `0x007909b8` | `0x00519c54, 0x00519ec4, 0x00519c60, 0x0051a0c8, 0x0049be4c, 0x0049d864, 0x004a0e28, 0x00546484, 0x005464f0, 0x00546598` |
| Radio | `noradio` | `0x00795504` | `0x007909c4` | `0x004bdf3c, 0x00519bcc, 0x00519bd8, 0x00519cd8, 0x0051a140, 0x0051a14c, 0x0051a020, 0x0051a02c, 0x004bd0e4, 0x004b1864` |
| MQA | `mqa_disable` | `0x0079f65c` | `0x0079a98c` | `0x0053f474, 0x00544b90, 0x004f4ba8` |
| EQ disable | `playmenu_eq_disable` | `0x00771e84` | `0x0076ce04` | `0x00419168` |

### String Nullification Examples (v1.8.b1 Addresses)

**Option A - Nullify String:** Overwrite the first character of the target string in the binary with `0x00` (null terminator), effectively erasing it.

For "noradio" at binary offset corresponding to VA `0x00795504`:
1. Find hex sequence: `6e 6f 72 61 64 69 6f 00`
2. Change to: `00 6f 72 61 64 69 6f 00`

For "no_record" at binary offset corresponding to VA `0x007954f8`:
1. Find hex sequence: `6e 6f 5f 72 65 63 6f 72 64 00`
2. Change to: `00 6f 5f 72 65 63 6f 72 64 00`

For "mqa_disable" at binary offset corresponding to VA `0x0079f65c`:
1. Find hex sequence: `6d 71 61 5f 64 69 73 61 62 6c 65 00`
2. Change to: `00 71 61 5f 64 69 73 61 62 6c 65 00`

### Calculating File Offsets

To convert virtual address to file offset, examine the ELF program headers:
```
file_offset = virtual_address - segment_vaddr + segment_offset
```
Use `readelf -l hiby_player` or Ghidra's memory map to determine the mapping.

---

## 24. Feature Gate Binary Addresses

### Master Feature Table

| Feature Area | Key String | String Addr | Code Reference(s) |
|-------------|-----------|-------------|-------------------|
| DLNA on/off | `DLNA:TURN_ON` | `0x0077c898` | — |
| DLNA on/off | `DLNA:TURN_OFF` | `0x0077c8a8` | `0x006fc034` |
| DLNA messages | `DLNA_ON_MSG` | `0x0077c638` | `0x0047a950` |
| DLNA messages | `DLNA_OFF_MSG` | `0x0077c648` | `0x0047a914` |
| AirPlay on | `LG_MSG_AIRPLAY_TURN_ON` | `0x00773f40` | `0x00451084` |
| AirPlay off | `LG_MSG_AIRPLAY_TURN_OFF` | `0x00773f74` | — |
| Bluetooth init | `bluetooth_hiby_init` | `0x00770a40` | `0x00445cd4` |
| Bluetooth codec | `BLUETOOTH_CODEC_MSG` | `0x0077c4f4` | `0x0047ad90` |
| BT aptX toggle | `bt_aptx on` | `0x00779300` | `0x0046a264` |
| BT aptX toggle | `bt_aptx off` | `0x00779318` | `0x0046a234, 0x0046c3b4` |
| GEQ enable | `geq_enable` | `0x0077ad98` | `0x00470a14, 0x0047213c, 0x00472148` |
| GEQ band gain | `geq_band_gain` | `0x0077ada4` | `0x00470a54, 0x00471f74, ...` |
| GEQ pre-gain | `geq_pre_gain` | `0x0077adb4` | `0x004720b4, 0x0047266c, ...` |
| SW mix gain | `SW_MIX_GAIN_ENABLE` | `0x007f77f0` | `0x0070c3f8` |
| DSD gain | `dsd_gain` | `0x00770fdc` | `0x00798d84, 0x00799c40, 0x004e3750` |
| DSD output | `dsd_output` | `0x00771058` | `0x004c5f44, 0x00798d80, 0x00799c3c` |
| Replaygain | `replaygain_type` | `0x00771018` | `0x004c64b4, 0x00799c64` |
| Track gain | `track_gain` | `0x00771028` | `0x006573c4, 0x004e8774` |
| Album gain | `album_gain` | `0x00771034` | `0x006577ec, 0x004e855c` |
| PEQ support | `peq_support` | `0x007a1068` | — |
| Fast find disable | `fast_find_disable` | `0x0079873c` | `0x004d338c` |
| Scan music gain | `scan_music_gain` | `0x0079872c` | `0x004d3348` |
| Factory mode | `factory_mode` | `0x0076fdb0` | — |
| DAC charge disable | `dac_charge_disable` | `0x008a0de8` | — |
| Developer mode | `in_developer_mode` | `0x0079e158` | — |
| Developer mode | `developer_mode` | `0x0079e16c` | — |
| SPDIF | `spdif` | `0x0076cef4` | 10+ locations |
| SPDIF max vol | `SPDIF_MAX_VOLUME` | `0x007f7754` | — |
| SPDIF max 192K | `SPDIF_MAX_192K` | `0x007f7768` | — |

---

## 25. Shell Scripts Reference

### hiby_player.sh
**Path:** `squashfs-root/usr/bin/hiby_player.sh`
```bash
#!/bin/sh
killall    hiby_player    &>/dev/null
killall -9 hiby_player    &>/dev/null
if [ -f "/usr/bin/batd" ]; then
  killall    batd    &>/dev/null
  killall -9 batd    &>/dev/null
  /usr/bin/batd -v -s -t5 -o /mnt/sd_0/batlog.txt &
fi
/usr/bin/hiby_player
sleep 1
reboot
```
Note: Player exit = device reboot. Crash = reboot.

### Key Scripts

| Script | Path | Purpose |
|--------|------|---------|
| `wifi_on.sh` | `usr/bin/wifi_on.sh` | Start WiFi: load module, WPA supplicant, DHCP, set hostname |
| `wifi_off.sh` | `usr/bin/wifi_off.sh` | Stop WiFi |
| `shairport_on.sh` | `usr/bin/shairport_on.sh` | Start AirPlay receiver |
| `shairport_off.sh` | `usr/bin/shairport_off.sh` | Stop AirPlay |
| `bt_enable` | `usr/bin/bt_enable` | Start Bluetooth stack |
| `bt_disable` | `usr/bin/bt_disable` | Stop Bluetooth |
| `bt_enable_bsa.sh` | `usr/bin/bt_enable_bsa.sh` | Start Broadcom BSA stack |
| `dut.sh` | `usr/bin/dut.sh` | BT Device Under Test mode |
| `cgic_enable` | `usr/bin/cgic_enable` | Start web CGI daemon |
| `cgic_disable` | `usr/bin/cgic_disable` | Stop web CGI daemon |
| `adbon` | `usr/bin/adbon` | Start ADB daemon |
| `adboff` | `usr/bin/adboff` | Stop ADB daemon |
| `uac_device_config.sh` | `usr/bin/uac_device_config.sh` | Configure USB Audio Class gadget |
| `usb_dev_mass_storage.sh` | `usr/bin/usb_dev_mass_storage.sh` | Configure USB mass storage |
| `bootmode.sh` | `usr/bin/bootmode.sh` | Switch boot partition |
| `recovery_all.sh` | `usr/bin/recovery_all.sh` | Factory reset |

---

## 26. Libraries & Capabilities

| Library | Path | Capability |
|---------|------|-----------|
| `libaac.so` | `usr/lib/libaac.so` | AAC decoding |
| `libmp3.so` | `usr/lib/libmp3.so` | MP3 decoding |
| `libwma.so` | `usr/lib/libwma.so` | WMA decoding |
| `libopus.so` | `usr/lib/libopus.so` | Opus codec |
| `libvorbis.so` | `usr/lib/libvorbis.so` | Vorbis codec |
| `libsndfile.so` | `usr/lib/libsndfile.so` | Audio file I/O |
| `libfdk-aac.so` | `usr/lib/libfdk-aac.so` | Fraunhofer AAC (high quality) |
| `libdsd2pcm.so` | `usr/lib/libdsd2pcm.so` | DSD to PCM conversion |
| `libldacBT_enc.so` | `usr/lib/libldacBT_enc.so` | LDAC BT encoding |
| `libldacBT_abr.so` | `usr/lib/libldacBT_abr.so` | LDAC adaptive bitrate |
| `libldacdec.so` | `usr/lib/libldacdec.so` | LDAC decoding |
| `libopenaptx.so` | `usr/lib/libopenaptx.so` | aptX BT codec |
| `libsbc.so` | `usr/lib/libsbc.so` | SBC BT codec |
| `libupnp.so` | `usr/lib/libupnp.so` | UPnP/DLNA |
| `libixml.so` | `usr/lib/libixml.so` | XML for DLNA |
| `libcurl.so` | `usr/lib/libcurl.so` | HTTP client |
| `libcrypto.so` | `usr/lib/libcrypto.so` | OpenSSL crypto |
| `libssl.so` | `usr/lib/libssl.so` | OpenSSL TLS |
| `libbluetooth.so` | `usr/lib/libbluetooth.so` | BT stack |
| `libqrencode.so` | `usr/lib/libqrencode.so` | QR code generation |
| `libfuse.so` | `usr/lib/libfuse.so` | FUSE filesystem |
| `libntfs-3g.so` | `usr/lib/libntfs-3g.so` | NTFS support |
| `libjpeg.so` | `usr/lib/libjpeg.so` | JPEG images |
| `libical.so` | `usr/lib/libical.so` | iCal/calendar |

### Audio Format Support (from binary strings)

FLAC, WAV, MP3, AAC, WMA, AIFF, DSD (DFF/DSF), APE, ALAC, Opus, Speex, Vorbis, DTS, Atrac3, ADPCM, Ogg, HLS (M3U8 with AES-128)

---

## 27. Network Endpoints

| Endpoint | Purpose |
|----------|---------|
| `https://otaserver.hiby.com/app/ota/updateDeviceVersion` | OTA version check |
| `https://otaserver.hiby.com/app/ota/getOtaInfo` | OTA firmware download |
| `https://otaserver.hiby.com/app/disclaimer/getDisclaimer` | Legal disclaimer |
| `https://otaserver.hiby.com/app/earphoneSet/getEarphoneSet` | Earphone settings |
| `https://otaserver.hiby.com/app/radio/getRadioInfo` | Internet radio data |
| `https://track.hiby.com/app/product/guide` | Product guide |
| `https://api.tidal.com/v1` | Tidal music API |
| `https://auth.tidal.com/v1/oauth2/device_authorization` | Tidal OAuth |
| `https://auth.tidal.com/v1/oauth2/token` | Tidal token |
| `https://resources.tidal.com` | Tidal assets |
| `http://www.qobuz.com/api.json/0.2` | Qobuz API |
| `http://<device_ip>:4399` | WiFi file transfer |
| `0.pool.ntp.org` | NTP primary |
| `time.windows.com` | NTP secondary |

---

## 28. MiDi R1 Dual Firmware

Same binary supports both HiBy R1 and MiDi R1 branding.

### Config Differences

| Field | HiBy R1 | MiDi R1 |
|-------|---------|---------|
| company | HiBy | MiDi |
| device | R1 | R1 |
| version | 1.8.b1 | 1.1 |
| ota_name | HiBy R1 | MiDi R1 |
| theme | enabled | disabled |
| dark_theme_enable | 1 | 0 |

### MiDi Resources

- Layout: `usr/resource/layout/midi/theme1/`
- Images: `usr/resource/litegui/midi/theme1/`
- Boot animation: separate directory under midi/

---

## 29. Audio Output Device Map

From `ot_devices.json`:

### Devices

| Device | Card | DSD Mode | Max Sample Rate |
|--------|------|----------|----------------|
| analog | 0 | DoP (configurable to native) | 384000 Hz |
| digital | 0 | — | 192000 Hz |
| usb | 3 | — | Device-dependent |
| a2dp | — | — | 192000 Hz |

### Output Ports

| Port | Priority | Purpose |
|------|----------|---------|
| usb | 7 (highest) | USB output |
| a2dp | 6 | Bluetooth output |
| headset | 3 | Headphone jack |
| default | 0 | Fallback (headset) |

### Volume Tables

- **HDB** (High Dynamic Bias): 100 steps, 0-255 hardware values
- **MDB** (Medium Dynamic Bias): 100 steps, 12-255 hardware values
- **LDB** (Low Dynamic Bias): 100 steps, 12-255 hardware values
- **SW** (Software): 100 steps, -1500 to 0 (in 0.01dB units)

---

## 30. Complete Launcher Apps Map

Every `launcher_apps_vg_*` in the binary — all launchable features:

| Launcher App | Default Visible | Category |
|-------------|----------------|----------|
| `player` | Yes | Music |
| `wireless` | Yes | Connectivity |
| `playset` | Yes | Settings |
| `eq` | Yes | Audio |
| `mseb` | Yes | Audio |
| `sysset` | Yes | Settings |
| `about` | Yes | Info |
| `hibylink` | Yes | Connectivity |
| `airplay` | Yes | Connectivity |
| `dlna` | Yes | Connectivity |
| `tidal` | Yes | Streaming |
| `qobuz` | Yes | Streaming |
| `scan` | Yes | Music |
| `ebook` | Yes | Apps |
| `stream_media` | Yes | Streaming |
| `via` | Partial | Connectivity |
| `net_radio` | CN only | Streaming |
| `phone_dac` | **Hidden** | DAC |
| `dac` | **Hidden** | DAC |
| `pc_dac` | **Hidden** | DAC |
| `blue_dac` | **Hidden** | DAC |
| `spdif_in` | **Hidden** | Audio |
| `coax` | **Hidden** | Audio |
| `optical` | **Hidden** | Audio |
| `bt_in` | **Hidden** | Connectivity |
| `darwin` | **Hidden** | Audio |
| `recorder` | **Hidden** | Apps |
| `step` | **Hidden** | Apps |
| `usb_mode` | **Hidden** | Settings |
| `sound` | **Hidden** | Audio |

---

## 31. Quick Reference — How to Enable Everything

### Method 1: JSON Config Edits (No Binary Patching)

| What to Edit | File | Change |
|-------------|------|--------|
| All hidden system settings | `usr/resource/set_functions.json` | Change all `0` to `1` |
| TF image cache | `usr/resource/config.json` | `tf_image_cache_enable: 1` |
| TF music DB | `usr/resource/config.json` | `tf_music_db_enable: 1` |
| CUE in file explorer | `usr/resource/config.json` | `explorer_in_cue_enable: 1` (NEW in v1.8.b1) |
| OTG music scanning | `usr/resource/config.json` | `otg_scan_enable: 1` (NEW in v1.8.b1) |
| QPlay 3.0 streaming | `usr/resource/config.json` | `qplay3_enable: 1` (NEW in v1.8.b1) |
| Disable volume warning | `usr/resource/config.json` | `vol_warn_enable: 0` |
| Raise volume limit | `usr/resource/config.json` | `lock_vol.headset: 100` |
| True DSD native output | `usr/resource/ot_devices.json` | `AnalogDsdNative: "native"` |
| Enable MPEG1/2 BT | `usr/resource/audio_back.conf` | `MPEG12Sources=1` |

### Method 2: File Triggers (SD Card)

| What | File to Create | Path |
|------|---------------|------|
| Factory test mode | Empty file | `/data/mnt/sd_0/hiby_linux_factory_mode` |
| Automated test | Empty file | `/data/mnt/sd_0/hiby_linux_auto_test` |
| Custom radio stations | Text file with URLs | `a:\radio.txt` (SD root) |
| Custom digital filters | Filter binary files | `/data/mnt/sd_0/filter/` |
| Custom screensavers | JPEG images | `/mnt/sd_0/screensavers/` |
| Battery logging | Automatic if batd exists | Output: `/mnt/sd_0/batlog.txt` |
| Thermal & Battery Diag | Empty file | Trigger: `/mnt/sd_0/battery_check.txt` <br> Output: `/mnt/sd_0/battery_log.txt` & `temperature_log.txt` |
| Custom UI - List Icons | Directory w/ PNGs | `/mnt/sd_0/list_cover/` |
| Custom UI - Play Screen | Directory w/ PNGs | `/mnt/sd_0/plane_cover/` |
| Force Custom UI Reload | Empty file | `/data/mnt/sd_0/get_cover_switch` |

### Method 3: Developer Mode (No File Editing)

Go to **About** screen, tap firmware version number repeatedly until "Developer Options" appears.

### Method 4: Binary Patching (Advanced)

| Target | Patch Location | Method |
|--------|---------------|--------|
| Force-enable radio | String `noradio` at VA `0x007909c4` | Replace string bytes with non-matching value |
| Force-enable recording | String `no_record` at VA `0x007909b8` | Replace string bytes with non-matching value |
| Force-enable MQA | String `mqa_disable` at VA `0x0079a98c` | Replace string bytes with non-matching value |
| Disable EQ gate | Check at `0x00419168` | NOP the branch |

To calculate file offset from virtual address:
```
file_offset = virtual_address - segment_vaddr + segment_offset
```
Use `readelf -l hiby_player` or Ghidra memory map to find segment mapping.

### Method 5: Runtime (ADB/SSH)

```bash
# Enable DSD native over USB:
echo 1 > /sys/class/android_usb/f_uac_sa/dsd_native_enable

# Switch SPDIF to optical:
echo op > /sys/devices/platform/jz-i2c.1/i2c-1/1-0048/set_spdif_input

# Load custom digital filter:
echo filtername > /sys/devices/platform/hm100/filter_path

# Start web file manager:
cgic_enable

# Start AirPlay:
shairport_on.sh

# Start battery logging:
/usr/bin/batd -v -s -t5 -o /mnt/sd_0/batlog.txt &
```

### Method 6: HibyLink App

HibyLink exposes settings not always in local UI:
- Balance (-10 to +10)
- Digital filter selection
- DSD output mode
- DSD gain
- ReplayGain type
- Max volume / default volume
- Screen rotation
- Standby mode
- Screensaver

---

## Ghidra Project Location

For further reverse engineering:
```
Project: C:\tmp\ghidra_hiby\HibyProject
Script: C:\tmp\ghidra_hiby\ExportFeatures.py
Output: C:\tmp\ghidra_hiby\output\
  ├── functions.txt        (173 KB — all function names + addresses)
  ├── strings.txt          (347 KB — all defined strings)
  ├── feature_xrefs.txt    (81 KB — feature string cross-references)
  └── decompiled_interesting.txt (257 KB — decompiled feature functions)
```

---

*Generated via Ghidra 11.3 headless decompilation + multi-agent firmware analysis*
*Binary: hiby_player MIPS32 LE ELF, 4,984,856 bytes*
*Firmware: HiBy R1 v1.8.b1 (updated from v1.7b1 analysis)*
