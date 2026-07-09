# HiBy R1 Firmware Analysis Report
## Binary: hiby_player (MIPS32 R2 LE ELF)
## Firmware Built: May 28 2026 18:21:46
## File Size: 4,962,892 bytes

---

## 1. ELF STRUCTURE

| Section    | Virtual Address | File Offset  | Size      | Purpose                    |
|------------|-----------------|--------------|-----------|----------------------------|
| .text      | 0x0040C3C0      | 0x0000C3C0   | 3,536,800 | Executable code            |
| .rodata    | 0x0076BD90      | 0x0036BD90   | 875,764   | Read-only data/strings     |
| .data      | 0x00866000      | 0x00456000   | 411,008   | Writable initialized data  |
| .bss       | 0x008CB3C0      | ---          | 299,568   | Uninitialized data         |
| .got       | 0x008CA590      | 0x004BA590   | 3,624     | Global Offset Table        |
| .plt       | 0x00853120      | 0x00453120   | 4,192     | Procedure Linkage Table    |

- Entry Point: 0x00411650
- Architecture: MIPS32 Release 2, Little Endian
- Static symbols: STRIPPED
- Dynamic symbols: 941 (777 FUNC, 153 OBJECT)
- Libraries: libcurl, libssl, libcrypto, libasound, libqrencode, libz, libpthread, libc

---

## 2. CONFIGURATION TABLE (play_settings.ini)

### Table Location
- **Table Start VA:** 0x00887518
- **Table Start File Offset:** 0x00477518
- **Entry Size:** 72 bytes (0x48)
- **Total Entries:** ~50+

### Struct Layout (72 bytes per entry)
```
Offset  Type    Field
0x00    ptr     ini_file_name (ptr to "play_settings.ini")
0x04    ptr     key_name (ptr to setting key string)
0x08    i32     current_value / -1 for enums
0x0C    i32     default_value / max_value
0x10    ptr     label_string (UI label like "<1001>")
0x14    i32     type (0=hidden, 1=numeric, 2=enum)
0x18    ptr     link_ini or 0x007F8024 (sentinel)
0x1C    ptr     link_key or sentinel
0x20    i32     min/value
0x24    i32     max/value
0x28    ptr     sub_label or 0
0x2C    i32     sub_type
0x30    ptr     sentinel or sub_ini
0x34    ptr     sentinel or sub_key
0x38    i32     sub_value
0x3C    i32     sub_max
0x40    ptr     display_label
0x44    i32     display_flags
```

---

## 3. PATCHABLE SETTINGS - EXACT FILE OFFSETS

### Audio Settings (play_settings.ini)

| Setting             | File Offset   | Default | Range/Options                     | Description                |
|---------------------|---------------|---------|-----------------------------------|----------------------------|
| digital_volume_max  | 0x004776F4¹   | 93      | 0-?? (numeric)                    | Max digital volume level   |
| dsd_gain            | 0x0047773C¹   | 75      | 0-?? (numeric)                    | DSD playback gain          |
| max_vol             | 0x004775BC¹   | 16      | 0-?? (numeric)                    | Maximum volume steps       |
| default_vol         | 0x00477604¹   | 19      | 0-?? (numeric)                    | Default volume on boot     |
| balance             | 0x004776AC¹   | 20      | 0-20 (numeric)                    | L/R balance range          |
| fade                | 0x00477528¹   | 50      | 0-50 (numeric)                    | Crossfade duration         |
| gapless_play        | 0x00477574¹   | 27      | Settings ID 27                    | Gapless playback toggle    |
| speed_play          | 0x00478354¹   | 100     | Max 100                           | Playback speed control     |
| repeater            | 0x0047832C¹   | 102     | A-B repeat                        | Repeater function          |
| gain                | 0x004781BC¹   | 21      | Settings ID 21                    | Pre-amplifier gain         |

¹ Offset points to field[3] (default value) in the config struct entry

### Audio Enum Settings

| Setting          | File Offset (entry) | Default Value | Options                         |
|------------------|---------------------|---------------|---------------------------------|
| digital_filter   | 0x004777D0          | "sharp" (0)   | sharp=0, slow=1, minimum_phase=2|
| dsd_output       | 0x00477A10          | "d2p" (0)     | d2p=0, dop=1, native=2         |
| replaygain_type  | 0x00477890          | "off" (0)     | off=0, track_gain=1, album_gain=2|
| play_mode        | 0x00477AD0          | "order" (0)   | order=0, single=1, random=2, loop=3|
| break_point      | 0x00477950          | "off" (0)     | off=0, track=1, position=2     |
| output           | 0x00477770          | 0             | Device output selection         |

### Default Value Patch Points (exact u32 locations)

To change **digital_volume_max** default from 93 to e.g. 120:
- File offset: **0x004776FC** (field[3] of entry 6)
- Current bytes: `5D 00 00 00`
- Patch to: `78 00 00 00` (for 120)

To change **dsd_gain** default from 75 to e.g. 100:
- File offset: **0x00477744** (field[3] of entry 7)
- Current bytes: `4B 00 00 00`
- Patch to: `64 00 00 00` (for 100)

To change **max_vol** default from 16 to e.g. 100:
- File offset: **0x004775C0** (field[3] of entry 2)
- Current bytes: `10 00 00 00`
- Patch to: `64 00 00 00` (for 100)

To change **default_vol** default from 19 to e.g. 30:
- File offset: **0x0047760C** (field[3] of entry 3)
- Current bytes: `13 00 00 00`
- Patch to: `1E 00 00 00` (for 30)

---

## 4. DSP/AUDIO MODULES TABLE

**Table VA:** 0x00888B9C | **File Offset:** 0x00478B9C

| Index | Module Name        | Purpose                          |
|-------|--------------------|----------------------------------|
| 0     | MSEB               | HiBy MSEB audio enhancement      |
| 1     | PEQ Combined       | Parametric EQ                    |
| 2     | graphic_equalizer  | Graphic EQ                       |
| 3     | Sound Field        | Spatial audio / sound field      |
| 4     | Balance_external   | External balance control         |
| 5     | Format Adapter     | Sample rate/format conversion    |
| 6     | ALSA               | ALSA PCM output                  |

---

## 5. HIDDEN/DEBUG FEATURES

### Factory Mode
- String: "factory_mode" @ 0x0076FDB0 (file: 0x0036FDB0)
- Activates factory test suite with LCD, LED, BT, key, audio, FM, charge tests
- Test UI strings at 0x007A3958-0x007A3F84

### Factory Test Suite Components
- test_lcd, test_led, test_key, test_bt_on, test_charge
- test_audio, test_fm, test_order, test_version
- test.ini config file at string 0x007A3A40

### Debug Mode
- "debug.txt" @ 0x0077852C
- Debug heap size monitoring (Start/Stop)
- "autotest_capture:%d" @ 0x0077CC10

### Internal Theme
- "vg_listview_internal_theme" @ 0x007983C8
- Layout file: z:\layout\theme1\listview\vg_listview_internal_theme.listview
- VG_LISTVIEW_INTERNAL_THEME @ 0x00790780

---

## 6. SYSTEM CONFIGURATION

### Config Files Used
| File                    | Purpose                    |
|-------------------------|----------------------------|
| play_settings.ini       | Audio/playback settings    |
| system_ui.ini           | UI configuration           |
| bluetooth.ini           | BT settings                |
| wifi.ini                | WiFi settings              |
| exception.ini           | Exception/recovery config  |
| mseb.ini                | MSEB audio enhancement     |
| line_out.ini            | Line-out configuration     |
| test.ini                | Factory test config        |
| firmware_update.ini     | OTA update config          |
| wifi_song.ini           | WiFi music transfer        |
| z:\config.json          | Device configuration       |
| /data/harmonic.json     | Harmonic/sound config      |
| /usr/data/alsa.conf     | ALSA audio config          |

### System Feature Toggles (system_config)
| Key                    | String VA    | Purpose                      |
|------------------------|--------------|------------------------------|
| playmenu_eq_disable    | 0x0076CE04   | Hide EQ from play menu       |
| dark_theme_enable      | 0x0076CE18   | Dark theme toggle            |
| screen_short_enable    | 0x0076CE2C   | Screenshot feature           |
| lyric_color_enable     | 0x0076CE40   | Colored lyrics               |
| tf_image_cache_enable  | 0x0076CE54   | TF card image cache          |
| tf_music_db_enable     | 0x0076CE6C   | TF card music database       |
| vol_warn_enable        | 0x0076CF08   | Volume warning               |
| lock_vol               | 0x0076CE80   | Volume lock                  |
| cgic_enable            | 0x00779C64   | CGIC feature                 |

---

## 7. BLUETOOTH & CODEC SETTINGS

### BT Codec Control
- "bt_aptx on" @ 0x00779300, "bt_aptx off" @ 0x00779318
- "LDAC" @ 0x0077933C, "LDAC_HQ" @ 0x00779370
- "LDAC_SQ" @ 0x00779378, "LDAC_ABR" @ 0x00779380
- "APTX_HD" @ 0x0077C51C
- "ldac_eqmid " @ 0x00779344
- "uat_eqmid " @ 0x00779364
- "bluetooth_test_switch" referenced at 0x007798C8

### LDAC Quality Modes
| Mode    | String VA    | Description      |
|---------|--------------|------------------|
| LDAC_HQ | 0x00779370   | High Quality     |
| LDAC_SQ | 0x00779378   | Standard Quality |
| LDAC_ABR| 0x00779380   | Adaptive Bitrate |

---

## 8. KEY EXPORTED FUNCTIONS (Patch Entry Points)

| Function                    | VA           | Purpose                        |
|-----------------------------|--------------|--------------------------------|
| dev_cfg_init                | 0x007129D4   | Device config initialization   |
| dev_info_init               | 0x00712BE8   | Device info init               |
| media_ctrl_init             | 0x00713A20   | Media control init             |
| media_info_init             | 0x00713C6C   | Media info init                |
| hls_init                    | 0x007153B4   | HibyLink service init          |
| hl_wifi_server_init         | 0x00717F90   | WiFi server init               |
| hl_bt_br_edr_server_init   | 0x00716B00   | Bluetooth classic init         |
| hl_bt_le_server_init       | 0x00716EA8   | BLE init                       |
| sys_cmd_init                | 0x0071A2F8   | System command init            |
| sonicSetSpeed               | 0x00681FFC   | Playback speed modification    |

---

## 9. USB/SPDIF/HARDWARE CONTROL

### USB DAC Mode
- UAC config: `/usr/bin/uac_device_config.sh -v %s -p %s -m "%s" -n "%s" start/stop`
- USB gadget: `/sys/kernel/config/usb_gadget/android0/`
- Mass storage: `functions/mass_storage.0/lun.0/file`
- OTG UDC: `13500000.otg_new`

### SPDIF Input Control
- Optical: `echo op > /sys/devices/platform/jz-i2c.1/i2c-1/1-0048/set_spdif_input`
- Coaxial: `echo co > /sys/devices/platform/jz-i2c.1/i2c-1/1-0048/set_spdif_input`

### Hardware
- SoC: Ingenic (jz platform, 13500000.otg_new)
- DAC I2C address: 1-0048
- Charger: mp2731
- USB Type-C controller: tcs1421
- Display: backlight_pwm0
- Storage: mmcblk0 (internal), mmcblk1 (SD)

---

## 10. OTA & NETWORK

- OTA Server: `https://otaserver.hiby.com/app/ota/getOtaInfo`
- Update check: `productName=%s&versionNumber=%s&deviceNumber=%s&languageCode=%s`
- WiFi transfer: `http://%s:4399` (port 4399)
- HibyLink: WiFi + BLE + BT Classic protocols
- Firmware file: `%s/sd_0/%s.upt`

---

## 11. GHIDRA DECOMPILATION FINDINGS

### Config System Architecture (from decompiled code)

**INI Reader Function:** `FUN_00413020(ini_file, key, buffer, buffer_size)`
- Used throughout to read settings: `FUN_00413020("play_settings.ini", "type", buf, 0x104)`
- Reads string values from INI files stored on device

**GEQ Enable Handler** @ 0x004709E0:
```c
void geq_enable_handler(int param_1) {
    if (DAT_008cebc0 == 0 && DAT_008cebbc == 0) {
        FUN_00470940("geq_enable", param_1, 0, 2);  // enable with type=2
    } else {
        FUN_006fe7a0();  // some other handler
    }
}
```
- **Patch point:** DAT_008cebc0 (VA: 0x008CEBC0, file: ~0x004BEBC0) — force to 0 to always allow GEQ
- **Patch point:** DAT_008CEBBC (VA: 0x008CEBBC, file: ~0x004BEBBC) — force to 0

**All Settings Handler** @ 0x00633FEC:
```c
// Handles: enable, dsd_bypass, sample_rate, sample_rate_index, all_settings
// Key struct offsets:
//   param_1 + 0xB0 = sample_rate
//   param_1 + 0xB4 = enable flag
//   param_1 + 0xB8 = dsd_bypass flag
```

**Dark Theme + System Config Handler** @ 0x004190C0:
```c
// Settings array loaded:
// "dac_to_store", "book_set_percent", "most_played",
// "playmenu_eq_disable", "dark_theme_enable", "screen_short_enable",
// "lyric_color_enable", "tf_image_cache_enable", "tf_music_db_enable"
// Each setting has an associated bit flag stored in DAT_008D0000 area
// Bitmask operation: *(DAT_008D0000 - 0x4734) |= (1 << (index & 0x1F))
```
- **System config bitmask base:** ~0x008CB8CC (0x008D0000 - 0x4734)
- Each feature toggle = one bit in this bitmask

**Factory Mode Check** @ 0x0051F960:
```c
// Compares activity name with "factory_mode" string
// If match: calls FUN_004f3680(param_1, "topbar", ...) to hide topbar in factory
// Factory mode triggered by: strcmp(*(param_1 + 0x174), "factory_mode") == 0
```
- **Patch to force factory mode:** Write "factory_mode\x00" to the activity name pointer
- **Or patch the strcmp to always return 0** at address 0x0051FCCC

### Key Global Variables (from decompilation)

| Variable          | VA           | Purpose                           |
|-------------------|--------------|-----------------------------------|
| DAT_008CEBC0      | 0x008CEBC0   | GEQ lock flag                     |
| DAT_008CEBBC      | 0x008CEBBC   | GEQ lock flag 2                   |
| DAT_008D1894      | 0x008D1894   | UI state flag (set to 1 on init)  |
| DAT_008D0000 area | 0x008CB8CC+  | System config bitmask             |

### PEQ (Parametric EQ) Internal Structure @ 0x0062519C
```c
// PEQ object struct offsets:
// +0x0C8 = enable (int, 0/1)
// +0x0D0 = max_samplerate
// +0x0D4 = min_samplerate
// +0x0D8 = dirty_flags bitmask (bit per band)
// +0x0DC = en_preamp (int, 0/1)
// +0x0E0 = preamp_linear (float, computed from dB)
// +0x0E4 = preamp_dB (float)
// +0x0F8 = current_band_index (0-32)
// +0x0FC = band_enable_bitmask (each bit = one band on/off)
// +0x3100 = band_data[33] array, each band = 0x30 bytes:
//     +0x00 = frequency (double)
//     +0x08 = gain_dB (double) 
//     +0x10 = boost (double)
//     +0x18 = Q (double)
//     +0x28 = mode (int: 0=peaking, etc)
// +0x3700 = master_temp[4] (gain for bass0-bass3)
//
// Settings keys handled:
//   "enable", "index", "on", "freq", "Q", "boost", "mode",
//   "preamp", "en_preamp", "all_settings", "master_temp",
//   "max_samplerate", "min_samplerate", 
//   "bass0"-"bass8" (via PTR_s_bass0_007b4c8c table)
```

### Bass Band Table @ 0x007B4C8C (.rodata)
```
Each entry: [string_ptr, band_index, gain_scale]
bass0, bass1, bass2, bass3, bass4, bass5, bass6, bass7, bass8
```

---

## 12. PATCHING STRATEGY

### Method 1: Direct Binary Patch (Config Table)
The config table at file offset 0x00477518 contains all play_settings.ini entries.
Each 72-byte entry has default values at fixed offsets. Patch the u32 value directly.

### Method 2: String Replacement
String constants in .rodata (file offset 0x0036BD90+) can be replaced with same-length
or shorter strings (NULL-padded). This can change default filter names, menu labels, etc.

### Method 3: Code Patch (NOP/Branch)
Functions at known addresses can be patched with MIPS NOP (0x00000000) or unconditional
branch instructions to skip checks or force behaviors.

### Important Notes
- All values are **Little Endian** (LSB first)
- File offsets = VA - 0x00410000 (for .data section: VA - 0x00410000)
- To convert: file_offset = VA - section_VA + section_file_offset
- For .data: file_offset = (VA - 0x00866000) + 0x00456000
- For .rodata: file_offset = (VA - 0x0076BD90) + 0x0036BD90
- For .text: file_offset = (VA - 0x0040C3C0) + 0x0000C3C0
