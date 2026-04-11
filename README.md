# HiBy R1 / MiDi R1 Firmware Modifications

Firmware patches for the **HiBy R1** and **MiDi R1** portable music players.  
Device runs HiBy Linux on an **Ingenic X1600** MIPS SoC with a **Cirrus Logic CS43131** DAC/amp chip.

---

## Changes Made

### 1. Volume Cap Removed
**File:** `usr/resource/config.json`

The player hard-capped the UI volume slider at 50/100 steps regardless of the CS43131's actual output range.

| Field | Before | After |
|-------|--------|-------|
| `lock_vol[headset]` | `50` | `100` |
| `warn_vol[headset]` | `34` | `100` |

---

### 2. MDB / LDB Hardware Gain Tables Fixed
**File:** `usr/resource/ot_devices.json`

The MDB and LDB volume tables stopped at CS43131 register value `12` instead of `0`.  
Register `0` = 0 dB (true maximum output). Register `12` ≈ −0.75 dB below full output.  
Changed the last entry in both `MDB` and `LDB` `Values` arrays from `12` → `0`.

---

### 3. Native DSD Enabled
**File:** `usr/resource/ot_devices.json`

All three DSD output paths were forced to DoP even when the user selected Native mode.  
The CS43131 supports hardware native DSD over I2S.

| Field | Before | After |
|-------|--------|-------|
| `AnalogDsdNative` | `"dop"` | `"native"` |

`AnalogDsdD2p` and `AnalogDsdDop` remain as `"dop"` — correct for their respective modes.

---

### 4. Missing Audio Engine Keys — Investigated, Not Applied
**File:** `usr/resource/ot_devices.json`

> **Reverted — not applied in this build.**

The player binary reads several keys from `ot_devices.json` that were absent in stock firmware, causing fallback to hardcoded defaults. The following keys were tested:

```json
"DOP_MAX_VOLUME":           100,
"ANALOG_PCM_HW_SAMPLE_MAX": 384000,
"SPDIF_MAX_VOLUME":         100,
"USB_OUT_MAX_VOLUME":       100,
"USE_VOLUME_CHIP":          0,
"SW_MIX_GAIN_ENABLE":       1,
"SPDIF_MAX_192K":           0
```

`ANALOG_PCM_HW_SAMPLE_MAX: 384000` matches the CS43131's hardware ceiling and the analog device `SampleRate` list already declared in the same file.  
`SPDIF_MAX_192K: 0` removes the 192 kHz cap on the SPDIF output path.  
`USE_VOLUME_CHIP: 0` — **must be `0`** if used. Setting this to `1` forces volume writes directly to the CS43131 ALSA hardware registers, which are not wired up in this firmware version. With `1`, the volume buttons show UI movement but produce no audio change.

These keys were removed after testing — the firmware behaves correctly without them using its built-in defaults.

---

### 5. Hardware-Only Volume Control
**File:** `usr/resource/ot_devices.json`

> **⚠ Reverted — do not set to `0`.**  
> Setting `SET_HW_SW_VOL_BOTH: 0` causes the volume buttons to show UI movement with no audio change (volume buttons stop working). Leave at `1`.

| Field | Value |
|-------|-------|
| `SET_HW_SW_VOL_BOTH` | `1` (stock value — leave unchanged) |

`SET_HW_SW_VOL_BOTH: 1` means the firmware applies volume changes to both the CS43131 hardware registers and the software attenuation stage simultaneously. Setting it to `0` was expected to use HW-only volume, but on this firmware version the result is that neither path is driven — the UI slider moves but audio level does not change. This change has been reverted.

---

### 6. Internet Radio Enabled for English — MiDi R1
**File:** `usr/resource/layout/midi/theme1/hiby_stream_media.view`

On the MiDi R1 the Stream Media screen only showed Tidal and Qobuz for non-Chinese languages. The radio entry was present in `hiby_stream_media_cn.view` but absent from the base layout file loaded for all other locales.

Added the `stream_media_vg_radio` viewgroup block (identical to the CN variant) between the closing of `stream_media_vg_qobuz` and `"name":"vg_stream_media_hiby"`.

---

### 7. Internet Radio Enabled for English — HiBy R1 Theme 1 & Theme 2
**Files:**
- `usr/resource/layout/theme1/hiby_stream_media.view`
- `usr/resource/layout/theme2/hiby_stream_media.view`

Same fix applied to the standard HiBy R1 themes to ensure radio is visible under all language settings.

---

### 8. Bluetooth Audio Quality — SBC XQ
**File:** `usr/bin/bt_init`

Added `--sbc-quality=xq` to the BlueALSA launch. SBC XQ uses bitpool 76 in dual-channel HD mode (~500 kbps). Falls back automatically to standard high quality (bitpool 53) if the connected device does not advertise XQ support.

**BlueALSA launch line:**
```sh
/usr/bin/bluealsa -p a2dp-source --a2dp-volume --sbc-quality=xq &
```

---

### 9. Font Replaced — Roboto
**File:** `usr/resource/fonts/default.otf`

Removed the stock `default.otf` (155 KB, HiBy custom font) and replaced it with the Roboto font family as `default.otf` (~10 MB total). Roboto provides broader Latin/Cyrillic coverage and better legibility at small display sizes.

Also removed Thai and Korean locale string directories — these are no longer needed since Roboto does not carry Thai or Korean glyph ranges:

| Removed | Path |
|---------|------|
| Thai locale strings | `usr/resource/str/thai/` |
| Korean locale strings | `usr/resource/str/korean/` |

> **Note:** Place your Roboto `.otf` file at `usr/resource/fonts/default.otf` before repacking the firmware image. The `hiby_player` binary loads the font by this exact filename.

---

### 10. Patched `hiby_player` Binary — Sorting Fix
**File:** `usr/bin/hiby_player`

Replaced the stock player binary with the community-patched sorting variant from [hiby-modding/hiby-mods](https://github.com/hiby-modding/hiby-mods/tree/main/binaries).

**What the sorting patch changes:**

| Behaviour | Stock | Patched |
|-----------|-------|---------|
| Article handling | "The Beatles" sorts under T | Strips leading articles (`the`, `a`, `an`, and equivalents in 13 languages) before sorting |
| Symbol-prefixed titles | Mixed into alphabet | Sorted first (before A) |
| Multi-language sort | ASCII only | Unicode-aware; handles CJK, Cyrillic, Latin extended, etc. |

**Source:** https://github.com/hiby-modding/hiby-mods/tree/main/binaries  
**Compatibility note:** The upstream repo primarily targets the HiBy R3 Pro II. Tested to boot and function on the R1 — verify your own build before flashing.

---

## Summary Table

| # | Change | File |
|---|--------|------|
| 1 | Volume cap lifted 50 → 100 | `usr/resource/config.json` |
| 2 | MDB/LDB gain table ceiling 12 → 0 (true 0 dB) | `usr/resource/ot_devices.json` |
| 3 | Native DSD output enabled (`AnalogDsdNative`) | `usr/resource/ot_devices.json` |
| 4 | ~~Audio engine keys added~~ **REVERTED** — defaults are sufficient | `usr/resource/ot_devices.json` |
| 5 | ~~HW-only volume (`SET_HW_SW_VOL_BOTH` 1 → 0)~~ **REVERTED** — breaks volume buttons | `usr/resource/ot_devices.json` |
| 6 | Internet Radio enabled for English — MiDi R1 | `usr/resource/layout/midi/theme1/hiby_stream_media.view` |
| 7 | Internet Radio enabled for English — HiBy R1 (Theme 1 & 2) | `usr/resource/layout/theme1/hiby_stream_media.view`, `theme2/...` |
| 8 | SBC XQ enabled (~500 kbps, bitpool 76) | `usr/bin/bt_init` |
| 9 | Font replaced: stock `default.otf` → Roboto (~10 MB); Thai & Korean locale strings removed | `usr/resource/fonts/default.otf` |
| 10 | Patched player binary — article/symbol sort fix | `usr/bin/hiby_player` |

---

## Hardware Reference

| Component | Details |
|-----------|---------|
| SoC | Ingenic X1600 (MIPS32r2) |
| DAC / Amp | Cirrus Logic CS43131 (I2C bus 3, GPIO PB02 power, PB21 reset) |
| PMU | X-Powers AXP2101 |
| OS | HiBy Linux (kernel 4.4.94) |
| Display | LG35583 480×800 |
| WiFi | Cypress CYW (cywdhd) |
| Headset ADC | sa_earpods_adc on AXP2101 ALDO4 |

---

## CS43131 Capabilities (Unlocked by These Changes)

| Feature | Spec |
|---------|------|
| PCM | 32-bit, up to 384 kHz |
| DSD | DSD64 / DSD128, Native or DoP |
| SNR | 130 dB |
| THD+N | −107 dB |
| Output power | ~112 mW @ 16 Ω |
| Digital filters | 5 hardware modes (fast/slow rolloff × min-phase/linear-phase + apodizing) |

---

## Sound Tuning — MSEB

HiBy's MSEB (Multi-dimensional Sound Enhancement Bass) is the primary tool for shaping the sound signature on-device.

**Path:** Play Settings → MSEB

| Slider | Range | Bass-relevant direction |
|--------|-------|------------------------|
| Bass extension | Light ↔ Deep | Deep = more sub-bass |
| Bass texture | Fast ↔ Thumpy | Thumpy = more decay/weight |
| Note thickness | Crisp ↔ Thick | Thick = more body |
| Overall Temperature | Cool/Bright ↔ Warm/Dark | Warm = darker, fuller tone |

Tap the settings icon inside MSEB to set the adjustment range: Fine-tuning (±20), Middling (±40), or Excessive (±100). Set to Middling or Excessive before pushing sliders far.

---

## Digital Filters

### CS43131 Chip Filters (in-app, Play Settings → Digital Filter)

These are the CS43131's hardware oversampling filter modes, confirmed from the `codec_cs43131.ko` ALSA driver. They are exposed as an ALSA control named `Digital Filter` and accessible in-app via **Play Settings → Digital Filter**. They affect HF rolloff shape and transient pre/post-ringing — not bass level.

| ALSA value | In-app label | Rolloff | Phase | Character |
|------------|--------------|---------|-------|-----------|
| `0` (`fast_rolloff_low_latency`) | Minimum-phase | Fast | Minimum phase | No pre-ringing, some post-ringing |
| `1` (`fast_rolloff_phase_compensated`) | Sharp / late rolloff | Fast | Linear phase | Symmetric pre+post ringing |
| `2` (`slow_rolloff_low_latency`) | *(not in UI)* | Slow | Minimum phase | Gentle rolloff, no pre-ringing |
| `3` (`slow_rolloff_phase_compensated`) | Slow / early rolloff | Slow | Linear phase | Gentlest rolloff, symmetric ringing |
| `4` | *(not in UI)* | Fast | Apodizing | Minimised pre-ringing, near-linear phase |

Values `2` and `4` are not exposed in the in-app Digital Filter dialog and are only accessible via ADB:
```sh
# slow rolloff, minimum phase
adb shell amixer cset name='Digital Filter' 2

# apodizing fast roll-off
adb shell amixer cset name='Digital Filter' 4

# read current value
adb shell amixer cget name='Digital Filter'
```


---

## Hidden Features Found (Not Yet Enabled)

| Feature | Location | Notes |
|---------|----------|-------|
| Factory test mode | `usr/resource/layout/theme1/test/hiby_test.view` | LCD, touch, key, BT, WiFi, FM hardware tests. Strings only in `simplified_chinese` and `english` locales |


---

## Enabling ADB

ADB can be enabled on any firmware version (including stock) without any modification:

1. Go to **Settings → About**
2. Tap the **"About"** text **10 times**
3. A **"Developer Mode"** confirmation message will appear
4. Connect the device to your PC via USB
5. Run `adb devices` to confirm the connection

To disable ADB, tap "About" 10 times again — the same toggle also disables developer mode.

> **Note:** ADB and USB mass storage share the same USB interface and cannot run simultaneously. While ADB is enabled the device will not appear as a storage drive.

> **Note:** This enables **Developer Mode** (ADB access), not the separate factory **Test Mode**. The factory test suite (`hiby_test.view` — LCD, key, touch, audio, BT, WiFi hardware tests) is a distinct feature with an unknown trigger, likely a button combination at boot. It is not accessible via the About tap sequence.

### How It Works Internally

The tap-counting logic lives entirely inside the `hiby_player` binary. The About screen (`hiby_about_dev.view`) contains a hidden textview (`about_dev_tv_developer`) that the binary progressively reveals as you tap. The locale strings confirm the countdown behaviour:

| String key | Text |
|------------|------|
| `enter_developer_mode` | "Perform 2 more steps to enter development mode" |
| `developer_mode` | "You are already in developer mode" |
| `in_developer_mode` | "You are already in developer mode, no need to do this." |

**The ADB toggle is a flag file:** `/usr/data/disableadb`

- File **present** → ADB is blocked. Both init scripts (`S310adb`, `S440adb`) check for this file at startup and exit early if it exists.
- File **absent** → ADB starts normally.

When you tap 10 times, `hiby_player` creates or removes this file and immediately starts or stops `adbd` — no reboot required.

**USB gadget configuration** (from `etc/init.d/adb/`):

| Field | Value |
|-------|-------|
| Vendor ID | `0x18d1` (Google) |
| Product ID | `0xd002` |
| Device serial | `{prefix}_{last 4 digits of WiFi MAC}`, falls back to chip ID |

Using Google's standard ADB VID/PID means no custom driver is needed on the host PC — the device shows up immediately with a standard ADB installation.

---

## Battery Optimizations

### 1. batd Battery Logger Disabled
**File:** `usr/bin/hiby_player.sh`

`batd` is a debug battery-monitoring daemon. When its binary is present it was launched at every player start with `-t5`, writing a battery log to the SD card every 5 seconds. This causes the SD card to wake on a 5-second cycle even during playback from internal storage.

The launch block has been removed. The `batd` binary itself is untouched — reinstating logging is a one-line change if needed.

| Field | Before | After |
|-------|--------|-------|
| SD card wakeup interval | Every 5 s (continuous) | Never |

---

### 2. Red LED Breathing Animation Disabled
**File:** `module_driver/leds_pwm_add.sh`

The red LED was set to `breathing` trigger — a continuous PWM fade-in/fade-out effect active whenever the device was on. Changed to `none` (LED off).

| Field | Before | After |
|-------|--------|-------|
| `led_pwm0_trigger` | `breathing` | `none` |

To restore the breathing effect or use a different trigger (`heartbeat`, `default-on`, etc.) edit this file.

---

### 3. Bluetooth Discoverable / Pairable Timeouts
**File:** `etc/bluetooth/main.conf`

Both timeouts were `0` — meaning the device stayed permanently discoverable and pairable after BT was enabled, continuously advertising over the air. Set to 180 seconds (BlueZ default).

| Field | Before | After |
|-------|--------|-------|
| `DiscoverableTimeout` | `0` (forever) | `180` s |
| `PairableTimeout` | `0` (forever) | `180` s |

After 3 minutes with no pairing the device stops advertising. Already-paired devices reconnect normally — this only affects new-pairing discovery broadcasts.

---

## Disclaimer

These are filesystem-level config and layout file edits — no kernel modifications.  
Changes affect audio output levels. Use appropriate volume and hearing protection.  
Flash and use at your own risk.
