# HiBy R1 / MiDi R1 Firmware Modifications

Firmware patches for the **HiBy R1** and **MiDi R1** portable music players.  
Device runs HiBy Linux on an **Ingenic X1600** MIPS SoC with a **Cirrus Logic CS43131** DAC/amp chip.

---

## 🚀 The Mission: Support the Development

To start the development and testing of these mods, I need the actual hardware. I am currently raising funds to purchase other **HiBy** devices. 

### **Target: $200 USD** 🎯
Your support will directly fund the purchase of the device for reverse engineering and firmware testing.

💖 **[Sponsor me on GitHub](https://github.com/sponsors/bidhata)**

---

---

## 🆕 New Features in this Build

### 1. Mono / Stereo Toggle (Trigger File Method)
**Requested by:** ApolloMoonLandings (Reddit)  
Adds support for stereo-to-mono downmixing for users with single-sided hearing or specific monitoring needs.

**How to use:**
- **To enable Mono:** Create an empty file or folder named `MONO` in the root of your SD card. The device will detect it, swap the ALSA configuration, and reboot automatically into Mono mode.
- **To return to Stereo:** Delete or rename the `MONO` file/folder. The device will reboot back into Stereo mode.

**Technical Implementation:**
- Custom ALSA configuration at `/usr/data/asound.conf.mono` using a `route` plugin with a `ttable` to mix L+R channels.
- Background monitor script `/usr/data/mono_monitor.sh` auto-started via `/etc/init.d/S92_03_start_music_player`.

### 2. Russian Language Fixes
**Contributor:** Much_West_2785 (Reddit)  
Extensive fixes for the Russian localization strings (`usr/resource/str/russian/`), correcting grammatical errors, truncated text, and inconsistent terminology found in the stock firmware.

### 3. Font Replacement — MiSans
**File:** `usr/resource/fonts/default.otf`  
The stock font has been replaced with **MiSans**. This provides a cleaner, more modern aesthetic and excellent legibility for both Latin and Cyrillic scripts.

## Changes Made

### 1. Volume Cap & Line Out Profile
**File:** `usr/resource/config.json`

The player originally hard-capped the UI volume slider at 50/100 steps. The `headset` limit has been safely increased to `60`. 
Additionally, a distinct `lineout` profile was injected into the config to allow full `100` volume when a high-impedance Aux connection is auto-detected.

| Field | Before | After |
|-------|--------|-------|
| `lock_vol[headset]` | `50` | `60` |
| `warn_vol[headset]` | `34` | `42` |
| `lock_vol[lineout]` | *(None)* | `100` |
| `warn_vol[lineout]` | *(None)* | `100` |

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

Removed the stock `default.otf` (155 KB, HiBy custom font) and replaced it with the Roboto font family. Roboto provides broader Latin/Cyrillic coverage and better legibility at small display sizes.

> [!IMPORTANT]
> **Apostrophe Spacing Bug Fix ("Don' t"):** 
> If you experience massive spaces after apostrophes in song titles (e.g., `Don'      t`), this is a known bug caused by the HiBy firmware struggling to read kerning metrics from OpenType (`.otf`) CFF fonts. 
> **The Fix:** You must use a TrueType (`.ttf`) version of Roboto, but **rename it to `.otf`** (e.g., `default.otf`). The player hardcodes the `.otf` extension but will natively parse the `.ttf` magic bytes perfectly, completely fixing the apostrophe spacing bug!

Also removed Thai and Korean locale string directories — these are no longer needed since Roboto does not carry Thai or Korean glyph ranges:

| Removed | Path |
|---------|------|
| Thai locale strings | `usr/resource/str/thai/` |
| Korean locale strings | `usr/resource/str/korean/` |

> **Note:** Place your Roboto `.ttf` file renamed to `default.otf` at `usr/resource/fonts/default.otf` before repacking the firmware image.

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

### 11. USB DAC Mode Unlocked
**Files:**
- `usr/resource/set_functions.json`
- `usr/resource/midi_set_functions.json`
- `usr/resource/config.json`

The USB DAC functionality is fully present in the firmware (kernel driver `uac_sa`, config scripts, UI layout), but was hidden from the menu.  
*(Inspired by the community discussion in [r/Hiby](https://www.reddit.com/r/Hiby/comments/1tji2bz/hiby_r1_as_a_dac/))*

| Flag | File | Before | After | Effect |
|------|------|--------|-------|--------|
| `usb_mode` | `set_functions.json` | `0` | `1` | Shows the "USB device mode" dropdown (Storage / Audio) |
| `dac_to_store` | `config.json` | `0` | `1` | Gracefully handles DAC mode exit / cable disconnect |
| `dac_feedback` | `set_functions.json` | `0` | `1` | Unlocks USB DAC feedback UI toggle |
| `standby` / `about` / `car_mode` | `set_functions.json` | `0` | `1` | Unlocks additional missing features |

---

## Summary Table

| # | Change | File |
|---|--------|------|
| 1 | Volume cap 60 (Headset) & 100 (Line Out) | `usr/resource/config.json` |
| 2 | MDB/LDB gain table ceiling 12 → 0 (true 0 dB) | `usr/resource/ot_devices.json` |
| 3 | Native DSD output enabled (`AnalogDsdNative`) | `usr/resource/ot_devices.json` |
| 4 | ~~Audio engine keys added~~ **REVERTED** — defaults are sufficient | `usr/resource/ot_devices.json` |
| 5 | ~~HW-only volume (`SET_HW_SW_VOL_BOTH` 1 → 0)~~ **REVERTED** — breaks volume buttons | `usr/resource/ot_devices.json` |
| 6 | Internet Radio enabled for English — MiDi R1 | `usr/resource/layout/midi/theme1/hiby_stream_media.view` |
| 7 | Internet Radio enabled for English — HiBy R1 (Theme 1 & 2) | `usr/resource/layout/theme1/hiby_stream_media.view`, `theme2/...` |
| 8 | SBC XQ enabled (~500 kbps, bitpool 76) | `usr/bin/bt_init` |
| 9 | Font replaced: stock `default.otf` → Roboto (~10 MB); Thai & Korean locale strings removed | `usr/resource/fonts/default.otf` |
| 10 | Patched player binary — article/symbol sort fix | `usr/bin/hiby_player` |
| 11 | USB DAC Mode, Standby, Car Mode & About UI Unlocked | `usr/resource/set_functions.json`, `config.json` |

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

### Pro-Tip: The "Pause Before Standby" Bug
**Type:** User Habit (No file edit required)

There is a known firmware bug in how the ALSA audio pipeline handles sleep states. 
- **Direct to Standby (Bad):** If the device goes into standby *while a track is actively playing*, the audio pipeline fails to cleanly release the ALSA hardware stream. This holds a kernel wakelock that prevents the Cirrus Logic DAC chip from powering down, leading to massive battery drain during standby.
- **Pause Before Standby (Good):** Always manually press **Pause** before putting the device to sleep. Pausing cleanly closes the ALSA stream, allowing the DAC chip and I2S clocks to enter a deep hardware sleep state, which dramatically increases your standby battery life!

*(Credit for finding this workaround goes to [u/OOOOOOO0OOOOOOOOO](https://www.reddit.com/user/OOOOOOO0OOOOOOOOO/))*

---

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
## HiBy R1 — File Limit Patch (50,000 → 65,000 Tracks)

**Target binary:** `/usr/bin/hiby_player` (MIPS32 LE ELF, 4,855,400 bytes)  
**Change:** Increases the music database track limit from 50,000 to 65,000 (`0xC350` → `0xFDE8`)

---

### Patch Locations

| Offset (hex) | Offset (dec) | Before | After | Instruction |
|---|---|---|---|---|
| `0x000EDF74` | 974,708 | `50 C3 02 34` | `E8 FD 02 34` | `ORI $v0, $zero, 65000` |
| `0x002DE3B0` | 3,007,408 | `50 C3 02 34` | `E8 FD 02 34` | `ORI $v0, $zero, 65000` |
| `0x002DFEA0` | 3,014,304 | `50 C3 04 34` | `E8 FD 04 34` | `ORI $a0, $zero, 65000` |
| `0x002E937C` | 3,052,412 | `50 C3 04 34` | `E8 FD 04 34` | `ORI $a0, $zero, 65000` |

---

### How to Apply

> **Back up your original binary before patching.**

Using `dd` on Linux/macOS:

```bash
FILE="hiby_player"
cp "$FILE" "${FILE}.orig"
for offset in 974708 3007408 3014304 3052412; do
    printf '\xe8\xfd' | dd of="$FILE" bs=1 seek=$offset conv=notrunc
done
```

----

# HiBy R1 Sound Enhancement

Config-only audio improvements for the HiBy R1 portable DAP. No binaries compiled. All changes are reversible.

---

## What This Does

| Change | File | Effect |
|--------|------|--------|
| ALSA PCM pipeline | `usr/data/asound.conf` | High-quality resampling + 256-step software volume |
| Volume tuning | `usr/resource/config.json` | Lower startup volume, wider range |
| EQ preset rename | `usr/resource/str/english/eq.ini` | "Custom" → "Headphones" slot for PEQ settings |

---

## Device

- **Model:** HiBy R1
- **OS:** Embedded Linux (MIPS32, kernel 2.6.32)
- **DAC:** Cirrus Logic CS43131 — confirmed at card 0 (`pcmC0D0p`)

---

## Files Changed

```
usr/data/asound.conf                          ← ALSA plug + softvol chain
usr/resource/config.json                      ← Volume defaults
usr/resource/str/english/eq.ini               ← EQ preset label
```

**Originals backed up in:** `docs/superpowers/backups/`

---

## ALSA Signal Chain

```
hiby_player
    └── pcm.!default (asym)
            └── playback → pcm.sv_out (softvol, 256 steps, −51 dB to 0 dB)
                                └── pcm.hq_out (plug, resampling)
                                        └── hw:0,0  (CS43131 DAC)
            └── capture  → hw:0,0 (direct)
```

---

## Volume Settings

| Setting | Before | After |
|---------|--------|-------|
| Default volume | 20 | 15 |
| Warning threshold | 34 | 42 |
| Max (lock) volume | 50 | 60 |

---

## Deploy

### Option A — ADB push (device running)

```bash
adb push usr/data/asound.conf /usr/data/asound.conf
adb push usr/resource/config.json /usr/resource/config.json
adb push "usr/resource/str/english/eq.ini" "/usr/resource/str/english/eq.ini"
adb shell reboot
```

### Option B — Repack squashfs

```bash
mksquashfs squashfs-root hiby_r1_enhanced.sqsh -comp xz -b 131072 -no-xattrs
```

Flash via OTA or recovery mode.

---
## HiBy R1 — Brightness Floor Patch (minimum 5 → 1)
**Target binary:** `/usr/bin/hiby_player` (MIPS32 LE ELF)
**Change:** Removes the backlight minimum clamp so the lowest slider position reaches sysfs value `1` instead of `5`. Measured ~25–30 lux → ~7–8 lux on this panel. Much darker for night use.
---
### Background
The player floors the backlight: slider values map 1:1 to the sysfs node (`6→6 … 100→100`), but anything resolving to **1–4 is forced up to 5**. The panel dims well below that — writing `1` directly is clearly darker and still on. The floor is a single clamp in the backlight-set routine:

```asm
li    a1, 5            ; floor value
addiu v1, v0, -1
sltiu v1, v1, 4        ; v1 = (1 <= value <= 4) ? 1 : 0
movz  a1, v0, v1       ; value NOT in 1..4 -> a1 = value; else a1 stays 5
jal   <write_to_sysfs>
```

**Change:** `sltiu $v1, $v1, 4` → `sltiu $v1, $v1, 0` (`0x04` → `0x00`). The test is then always false, `movz` always passes the real value, the floor never fires. `0` (backlight off) is unaffected.
---
### Patch Locations
Offset is build-dependent. bidhata's firmware ships the Sorting-patch player, so that is the primary target; stock listed for reference.

| Binary | Offset (hex) | Offset (dec) | Before | After | Instruction |
|---|---|---|---|---|---|
| Sorting variant | `0x0007C900` | 510,208 | `04 00 63 2C` | `00 00 63 2C` | `SLTIU $v1, $v1, 4` → `0` |
| Stock | `0x00067440` | 422,976 | `04 00 63 2C` | `00 00 63 2C` | `SLTIU $v1, $v1, 4` → `0` |
---
### How to Apply
> **Back up your original binary before patching.**

**Option A — `dd` (fixed offset).** Sorting variant:
```bash
FILE="hiby_player"
cp "$FILE" "${FILE}.orig"
printf '\x00' | dd of="$FILE" bs=1 seek=510208 conv=notrunc
```
For the stock binary, use `seek=422976`.

**Option B — `patch_brightness.py` (any build).** Locates the clamp by instruction signature, so it works regardless of binary layout (handy for other community variants where the offset differs):
```bash
python3 patch_brightness.py hiby_player   # writes hiby_player_patched
```
---
### Verification
On device, set the slider to minimum:
```bash
cat /sys/class/backlight/backlight_pwm0/brightness   # should read 1, not 5
```
---
## HiBy R1 — Bluetooth Audio Input Unlock
**Target binary:** `/usr/bin/hiby_player` (MIPS32 LE ELF)
**Change:** Unlocks the natively supported but hidden "Bluetooth audio input" (DAC/Sink mode) in the Bluetooth settings list. Works flawlessly with UI navigation.
---
### Background
The firmware fully supports Bluetooth Sink mode (used in R3 Pro II), but in R1 the option is removed from the settings list, and a blocklist routine prevents its view (`vg_bt_input_hiby`) from opening. 

**Change 1 (Bypass Blocklist):** The blocklist routine (`FUN_004f7220`) explicitly checks against `vg_bt_input_hiby`. We shift the string pointer by 1 byte, making it check for `g_bt_input_hiby` instead, effectively unblocking the view.
```asm
addiu s1, s1, LOW(vg_addr)   ->   addiu s1, s1, LOW(vg_addr) + 1
```

**Change 2 (Inject Menu Item):** The UI builds the Bluetooth menu item by item. We overwrite one item addition (`volume_sync`) with a jump to a code cave (40 bytes of `0x00` in `.rodata`). In the cave, we execute the original `volume_sync` addition, followed by our new `bt_input` addition, and jump back seamlessly.
```asm
lui a1, HIGH(bt_in_addr)
move a0, s2
jal add_item_func
addiu a1, a1, LOW(bt_in_addr)
```
---
### Patch Locations
Offset is build-dependent. Values for the Sorting-patch player (fw 1.6):

| Description | Offset (hex) | Offset (dec) | Before | After | Change |
|---|---|---|---|---|---|
| Blocklist bypass | `0x000F723C` | 1,012,284 | `58 3C 31 26` | `59 3C 31 26` | `addiu s1,s1,15448` → `15449` |
| Cave Jump | `0x000AD034` | 708,660 | `79 00 05 3C` | `FE BD 1D 08` | `lui a1,0x79` → `j 0x76f7f8` |
| Code Cave | `0x0036F7F8` | 3,602,424 | `00 00 00 ...` | *(Cave payload)* | 40 bytes of payload |
---
### How to Apply
> **Back up your original binary before patching.**

**Using `patch_bt_input.py` (any build).** Locates the UI menu builder, blocklist offsets, and an empty code cave automatically by instruction signature. It works regardless of binary layout (handy for other community variants where the offset differs):
```bash
python3 patch_bt_input.py hiby_player   # patches hiby_player in place
```
---
### Verification
On device, open Bluetooth settings. "Bluetooth audio input" should appear in the list. Tapping it opens the DAC/Sink mode. Tapping the back arrow returns to the menu seamlessly.
---

## After Flashing — Manual Setup

Configure once on the device:

1. **ReplayGain** — Settings → Play → ReplayGain → select **by Track**
2. **Crossfade** — Settings → Play → Crossfade → **On**
3. **Gapless playback** — Settings → Play → Gapless playback → **On**
4. **Parametric EQ** — Settings → Equalizer → select **Headphones** → tap **PEQ**

PEQ settings (Headphones preset):

| Band | Type | Frequency | Gain | Q |
|------|------|-----------|------|---|
| 1 | Low Shelf | 100 Hz | +3.0 dB | — |
| 2 | Peaking | 3000 Hz | +1.5 dB | 1.2 |
| 3 | High Shelf | 10000 Hz | −2.0 dB | — |

---
# Additional Community Modifications

## 12. Parametric Equalizer (PEQ) Fully Enabled

**Files:**

* `usr/bin/hiby_player`
* `usr/resource/eq.ini`
* `usr/resource/str/*/eq.ini`

The complete Parametric Equalizer implementation already exists inside the HiBy R1 firmware resources and UI layouts. However, the stock 1.6 firmware does not fully expose the functionality through the bundled audio engine.

Testing revealed that replacing the stock 1.6 `hiby_player` binary with the newer 1.7 Beta player binary enables the complete PEQ feature set while remaining compatible with the existing 1.6 Mod firmware modifications.

### Enabled Features

* PEQ Combined mode
* Multi-band Parametric Equalizer
* Frequency control
* Gain control
* Q-factor control
* Filter type selection
* Preset save and recall
* Full integration with the existing EQ menu

### Required Supporting Files

The following files from firmware 1.7 Beta should also be migrated:

```text
/usr/resource/eq.ini
/usr/resource/str/*/eq.ini
```

These files contain updated PEQ definitions and language resources used by the newer player binary.

### Result

The resulting firmware successfully combines:

* All 1.6 Mod enhancements
* USB DAC Mode
* Standby Menu
* Car Mode
* About Menu
* Full Parametric EQ (PEQ)
* Existing battery optimizations
* Bluetooth SBC XQ support
* Native DSD support
* Internet Radio support

### Stability

This configuration has been tested on real HiBy R1 hardware and is confirmed operational.

No regressions were observed in:

* Local playback
* Bluetooth audio
* USB DAC mode
* Library scanning
* Internet Radio
* MSEB
* Standard EQ functionality

---

# Current Recommended Firmware Configuration

The most feature-complete community firmware currently known is:

**HiBy R1 1.6 Mod + HiBy R1 1.7 Beta Audio Engine**

This combination provides:

| Feature               | Status |
| --------------------- | ------ |
| Volume Unlock         | ✅      |
| Native DSD            | ✅      |
| USB DAC Mode          | ✅      |
| Standby Menu          | ✅      |
| Car Mode              | ✅      |
| About Menu            | ✅      |
| Internet Radio        | ✅      |
| SBC XQ Bluetooth      | ✅      |
| PEQ Combined          | ✅      |
| Parametric EQ Editor  | ✅      |
| Sorting Patch         | ✅      |
| Battery Optimizations | ✅      |

---

# Credits

## Original Research

Many of the firmware discoveries documented here were made through community reverse engineering efforts and testing by members of the HiBy community.

Special thanks to:

* HiBy Modding Community
* HiBy Linux reverse-engineering contributors
* Rockbox R1 development contributors

## PEQ and USB DAC Enablement Research

PEQ enablement, USB DAC unlocking, firmware repacking validation, OTA package reconstruction research, and compatibility testing between the 1.6 Mod firmware and the 1.7 Beta audio engine were performed and documented by:

**u/hrwoyem**

Reddit:
https://www.reddit.com/user/hrwoyem/

These findings demonstrated that the HiBy R1 firmware already contained dormant functionality that could be unlocked through configuration changes and selective component migration rather than requiring binary patch development.

---

# Disclaimer

These modifications are unofficial community firmware modifications.

Flashing modified firmware always carries risk, including device malfunction or data loss.

Neither HiBy nor the contributors listed above are responsible for any damage resulting from use of modified firmware.

Proceed at your own risk and always keep a copy of the original firmware before flashing.


