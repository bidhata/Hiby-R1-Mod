**Please Remember:** I'm not a developer — I'm just a **curious reverse-engineering addict** who can't resist poking at firmware to see what happens. 😄

I basically flip every switch I can find, cross my fingers, and release the modded firmware into the wild. There's no way I can test every single feature or setting on my own. So think of this as a community experiment: you break it, test it, report it, and together we'll figure out what actually works and what still needs tweaking. Every bug report, success story, and weird discovery helps make the next release better. Without your feedback, I'm just a guy enthusiastically pressing random buttons in an editor.
>I am currently raising funds to purchase other HiBy devices to expand this modding ecosystem.
> 
> ### 🎯 Target: $200 USD
> Your support directly funds the purchase of hardware for reverse engineering and firmware testing. Next Targetted device Hiby R3 Pro II.
> 
> **💖 [Sponsor me on GitHub](https://github.com/sponsors/bidhata?frequency=one-time&sponsor=bidhata)**  

> 
> *Every dollar keeps this project alive and motivates future updates!*

---
# BUGS Alert - What to Expect
- "Developer Options" removed by Hiby in this new build.
- Volume getting locked multiple times
- USB DAC and Car modes are not stable like before.
- Partial ported theme looks broken on 1st boot. So, after flashing the firmware, restore factory settings, and then choose theme 2 from system settings.
    
# 🎶 HiBy R1 — Ultimate Custom Firmware Mod

The most feature-complete custom firmware for the **HiBy R1** portable music players.  
Based on **v1.8.b1** — unlocks hidden features, maximizes performance, protects battery health, and overhauls the UI.

Device runs HiBy Linux on an **Ingenic X1600** MIPS SoC with a **Cirrus Logic CS43131** DAC/amp chip.

## Quick Install

* Download the ```r1.upt``` file in the [releases](https://github.com/bidhata/Hiby-R1-Mod/releases) page and place it in the root of your SD card.
* On your Hiby R1, go to ```Settings → Firmware Update → Via SD-card```. Select "OK" when prompted to "Update system firmware?".
* The player will reboot and then update with the found custom firmware. The device will reboot again, and you'll see a custom splash screen before the Hiby logo, which confirms the custom firmware was successfully installed. 



---

## 🚀 NEW: Exclusively in this v1.8.b1 Build

### 1. Unified Theme (by @Jepl4r) Ported partially.
Ported the gorgeous **Unified Theme** originally designed for the R3 Pro II:
- Modernized icons across the entire UI.
- Rounded launcher design and sleek custom boot logo.
- *Default theme set to Theme 2 (Dark Mode) on fresh install.*

### 2. Battery Health Protection (95% Charge Limit)
If you use your R1 tethered as a USB DAC, keeping the battery at 100% permanently degrades its lifespan. 
- Modified the AXP2101 PMU kernel driver (`module_driver/axp2101.sh`).
- Dropped the maximum charging voltage from `4400` (4.4V) to `4350` (4.35V). 
- Based on lithium-ion discharge curves, this limits the maximum physical battery charge to roughly **90-95%**, significantly extending the lifespan of your battery!

### 3. Maximum Performance & Responsiveness Tweaks
Injected custom kernel parameters into the startup script (`hiby_player.sh`):
- **SD Card Read-Ahead:** Expanded the read-ahead buffer to 2MB (from 128KB) — eliminates UI stuttering when scanning libraries or loading large FLAC files.
- **Sysctl Cache Tuning:** Tuned `vm.vfs_cache_pressure` to keep UI assets and album art cached in RAM longer.
- **SD Card Caching:** Album art and the music database are now cached to the MicroSD card, taking the load off the slower internal flash.
- **Optimized Flash Mount Options (/usr/bin/mount_ubifs.sh):** Dramatically lowering NAND flash wear and speeding up settings/saving operations by changing mount option from *sync* to *noatime*. ( TESTING )

### 4. Fully Unlocked Hidden Features
Patched the UI and configuration JSONs (`set_functions.json` & `config.json`) to expose features HiBy hid from the stock firmware:

| Feature | Config Flag | Effect |
|---------|-------------|--------|
| USB Device Mode | `usb_mode` | Switch between Storage / USB DAC / Dock (ADB) |
| DAC Charge Disable | `dac_charge_disable` | Toggle USB charging noise reduction |
| DAC Feedback | `dac_feedback` | USB DAC feedback UI toggle |
| Car Mode **( Disabled Due to Issues )**| `car_mode` | Auto-play music when power is connected |
| Double Tap to Wake **( Not Working )**| `double_touch_wakeup` | Wake screen without power button |
| Standby | `standby` | Standby/sleep settings |
| CUE Sheet Explorer | `explorer_in_cue_enable` | Browse CUE sheets from file explorer |
| OTG Scanning | `otg_scan_enable` | Build library from OTG USB drives |
| Volume Meter **( Disabled Due to Issues )** | `volume_meter_enable` | On-screen volume meter |
| Auto-Scroll Playplane | `auto_scroll_playplane` | Scrolling now-playing text |
| Speed Play | `speed_play_enable` | Playback speed control |
| SD Image Cache | `tf_image_cache_enable` | Cache album art to SD card |
| SD Music DB | `tf_music_db_enable` | Store music database on SD card |
| Volume Lock Bypass | `lock_vol[headset]` | Max volume raised to 100, warning popup disabled |

### 5. System Stability Fixes
- Fixed a major bug in the stock firmware where clicking "System Settings" resulted in a "No Music Found" crash — caused by corrupted UTF-16LE XML headers in the English translation files (`.ini`).

### 6. Bluetooth Audio Input Unlocked (Sink/DAC Mode)
Your R1 can now act as a **Bluetooth DAC receiver**! Stream audio from your phone or tablet **to** the R1 and listen through its CS43131 DAC + headphone jack.
- The feature was fully present in the firmware but explicitly blocked from the UI by a view blocklist.
- Two binary patches to `hiby_player`: bypassed the UI blocklist and injected the missing menu item into the Bluetooth settings list using a MIPS code cave.
- Back button and disconnect dialog work flawlessly.
- *Credit: [@japq7s](https://github.com/japq7s) for the original patch and `patch_bt_input.py` script.*

---

## 🔥 Classic Features (Carried Over from Previous Builds)

### Volume Cap & Line Out Profile
**File:** `usr/resource/config.json`

| Field | Before | After |
|-------|--------|-------|
| `lock_vol[headset]` | `50` | `100` |
| `vol_warn_enable` | `1` | `0` |

---

### MDB / LDB Hardware Gain Tables Fixed
**File:** `usr/resource/ot_devices.json`

Changed last entry in both `MDB` and `LDB` `Values` arrays from `12` → `0` (true 0 dB maximum output, was artificially capped ~0.75 dB below full).

---

### USB DAC Mode Unlocked
The USB DAC functionality is fully present in the firmware (kernel driver `uac_sa`, config scripts, UI layout) but was hidden from the menu.  
*(Inspired by [r/Hiby](https://www.reddit.com/r/Hiby/comments/1tji2bz/hiby_r1_as_a_dac/))*

---

### Mono / Stereo Toggle (Trigger File Method)
**Requested by:** ApolloMoonLandings (Reddit)

- **To enable Mono:** Create an empty file named `_MONO_` in the root of your SD card → device reboots into Mono mode.
- **To return to Stereo:** Delete the `_MONO_` file → device reboots back to Stereo.
- Uses a custom ALSA `route` plugin with `ttable` to mix L+R channels.

### Serial Console Shell Disabled 
- Spawns a root shell on the hardware serial pins (TX/RX) on the motherboard. Unless you plan on taking the device apart and soldering a UART cable to the motherboard for low-level debugging, this doesn't do anything for you. May save some extra memory even if very tiny.
- /etc/inittab --> #console::respawn:-/bin/sh # GENERIC_SERIAL 
---

### Font Replacement
**File:** `usr/resource/fonts/default.otf`  
Stock font replaced with **MiSans** for cleaner aesthetics and better Latin/Cyrillic legibility.

> [!TIP]
> **Apostrophe Spacing Bug Fix:** If you see massive spaces after apostrophes (e.g., `Don'      t`), use a TrueType (`.ttf`) font but **rename it to `.otf`**. The player hardcodes the `.otf` extension but parses `.ttf` magic bytes perfectly.

---

### Russian Language Fixes
**Contributor:** Much_West_2785 (Reddit)  
Corrected grammatical errors, truncated text, and inconsistent terminology across all Russian localization strings.

---

## 🔋 Battery Optimizations

### Pro-Tip: The "Pause Before Standby" Bug
> **Always press Pause before putting the device to sleep.** If the device goes into standby while a track is playing, the ALSA audio pipeline fails to release the hardware stream, holding a kernel wakelock that prevents the DAC chip from powering down → massive battery drain during standby.  


---

## 🎛️ Sound Tuning Guide

### MSEB (Multi-dimensional Sound Enhancement Bass)
**Path:** Play Settings → MSEB

| Slider | Range | Bass direction |
|--------|-------|----------------|
| Bass extension | Light ↔ Deep | Deep = more sub-bass |
| Bass texture | Fast ↔ Thumpy | Thumpy = more decay/weight |
| Note thickness | Crisp ↔ Thick | Thick = more body |
| Overall Temperature | Cool/Bright ↔ Warm/Dark | Warm = darker, fuller tone |

Tap the settings icon inside MSEB to set range: Fine-tuning (±20), Middling (±40), or Excessive (±100).

### CS43131 Digital Filters (Play Settings → Digital Filter)

| ALSA value | In-app label | Rolloff | Phase | Character |
|------------|--------------|---------|-------|-----------|
| `0` | Minimum-phase | Fast | Minimum | No pre-ringing, some post-ringing |
| `1` | Sharp / late rolloff | Fast | Linear | Symmetric pre+post ringing |
| `2` | *(hidden)* | Slow | Minimum | Gentle rolloff, no pre-ringing |
| `3` | Slow / early rolloff | Slow | Linear | Gentlest rolloff, symmetric ringing |
| `4` | *(hidden)* | Fast | Apodizing | Minimised pre-ringing |

Values `2` and `4` are only accessible via ADB:
```sh
adb shell amixer cset name='Digital Filter' 2   # slow rolloff, minimum phase
adb shell amixer cset name='Digital Filter' 4   # apodizing fast roll-off
adb shell amixer cget name='Digital Filter'     # read current value
```

---

## 🔧 Binary Patches Reference

### Brightness Floor Patch (minimum 5 → 1)
Removes the backlight minimum clamp so the lowest slider position reaches sysfs value `1` instead of `5`. Measured ~25–30 lux → ~7–8 lux. Much darker for night use.

| Binary | Offset (hex) | Before | After |
|--------|-------------|--------|-------|
| Sorting variant | `0x0007C900` | `04 00 63 2C` | `00 00 63 2C` |
| Stock | `0x00067440` | `04 00 63 2C` | `00 00 63 2C` |

```bash
# Apply (sorting variant):
printf '\x00' | dd of=hiby_player bs=1 seek=510208 conv=notrunc
# Or use the signature-based script:
python3 patch_brightness.py hiby_player
```

### File Limit Patch (50,000 → 65,000 Tracks)

| Offset (hex) | Before | After | Instruction |
|-------------|--------|-------|-------------|
| `0x000EDF74` | `50 C3 02 34` | `E8 FD 02 34` | `ORI $v0, $zero, 65000` |
| `0x002DE3B0` | `50 C3 02 34` | `E8 FD 02 34` | `ORI $v0, $zero, 65000` |
| `0x002DFEA0` | `50 C3 04 34` | `E8 FD 04 34` | `ORI $a0, $zero, 65000` |
| `0x002E937C` | `50 C3 04 34` | `E8 FD 04 34` | `ORI $a0, $zero, 65000` |

```bash
FILE="hiby_player"
cp "$FILE" "${FILE}.orig"
for offset in 974708 3007408 3014304 3052412; do
    printf '\xe8\xfd' | dd of="$FILE" bs=1 seek=$offset conv=notrunc
done
```

### Bluetooth Audio Input Patch
```bash
python3 patch_bt_input.py hiby_player   # signature-based, works across variants
```

---

## 📱 Enabling ADB

ADB can be enabled on any firmware version (including stock) **without** any modification:

1. Go to **Settings → About**
2. Tap the **"About"** text **10 times**
3. A **"Developer Mode"** confirmation message will appear
4. Connect to PC via USB → run `adb devices`

To disable, tap "About" 10 times again.

> **Note:** ADB and USB mass storage share the same USB interface and cannot run simultaneously.

**Internally:** The toggle creates/removes `/usr/data/disableadb`. Both init scripts (`S310adb`, `S440adb`) check for this file at startup. USB gadget uses Google's standard ADB VID/PID (`0x18d1:0xd002`) — no custom driver needed.

---

## 🛠️ Installation

### Option A — ADB Push (device running)
```bash
adb push usr/data/asound.conf /usr/data/asound.conf
adb push usr/resource/config.json /usr/resource/config.json
adb push usr/resource/set_functions.json /usr/resource/set_functions.json
adb shell reboot
```

### Option B — Repack squashfs
```bash
mksquashfs squashfs-root hiby_r1_enhanced.sqsh -comp xz -b 131072 -no-xattrs
```
Flash via OTA or recovery mode.

---

## ⚙️ Hardware Reference

| Component | Details |
|-----------|---------|
| SoC | Ingenic X1600 (MIPS32r2) |
| DAC / Amp | Cirrus Logic CS43131 (I2C bus 3, GPIO PB02 power, PB21 reset) |
| PMU | X-Powers AXP2101 |
| OS | HiBy Linux (kernel 4.4.94) |
| Display | LG35583 480×800 |
| WiFi | Cypress CYW (cywdhd) |
| Headset ADC | sa_earpods_adc on AXP2101 ALDO4 |

### CS43131 Capabilities (Unlocked by These Mods)

| Feature | Spec |
|---------|------|
| PCM | 32-bit, up to 384 kHz |
| DSD | DSD64 / DSD128, Native or DoP |
| SNR | 130 dB |
| THD+N | −107 dB |
| Output power | ~112 mW @ 16 Ω |
| Digital filters | 5 hardware modes (fast/slow rolloff × min-phase/linear-phase + apodizing) |

---

## ⚠️ Known Pitfalls (Do NOT Apply)

| Setting | Why Not |
|---------|---------|
| `SET_HW_SW_VOL_BOTH: 0` | Breaks volume buttons — UI slider moves but no audio change. Leave at `1`. |
| `USE_VOLUME_CHIP: 1` | Forces volume writes to unwired CS43131 registers. Volume becomes non-functional. |
| Missing audio engine keys | Tested `DOP_MAX_VOLUME`, `ANALOG_PCM_HW_SAMPLE_MAX`, etc. — firmware defaults are correct. Reverted. |

---

## After Flashing — Recommended Manual Setup

1. **ReplayGain** — Settings → Play → ReplayGain → **by Track**
2. **Crossfade** — Settings → Play → Crossfade → **On**
3. **Gapless playback** — Settings → Play → Gapless playback → **On**
4. **Parametric EQ** — Settings → Equalizer → **Headphones** → tap **PEQ**

---

## 📊 Feature Status Matrix

| Feature | Status |
|---------|--------|
| Volume Unlock (100 max) | ✅ |
| Native DSD | ✅ |
| USB DAC Mode | ✅ |
| Bluetooth Audio Input (Sink) | ✅ NEW |
| Unified Theme Port | ✅ NEW |
| Battery Health (95% Cap) | ✅ NEW |
| Performance Tweaks | ✅ NEW |
| Standby / Car Mode / About | ✅ |
| Internet Radio (English) | ✅ |
| PEQ Combined | ✅ |
| Parametric EQ Editor | ✅ |
| Sorting Patch | ✅ |
| Battery Optimizations | ✅ |
| Mono/Stereo Toggle | ✅ |
| Brightness Floor Patch | ✅ |
| Russian Language Fixes | ✅ |

---

## 🏆 Credits

* **[@Jepl4r](https://github.com/hiby-modding/hiby-mods/tree/main/themes)** — Unified Theme port
* **[@japq7s](https://github.com/japq7s)** — Bluetooth Audio Input patch
* **[u/hrwoyem](https://www.reddit.com/user/hrwoyem/)** — PEQ enablement, USB DAC unlocking, and OTA research
* **ApolloMoonLandings** — Mono/Stereo toggle request
* **Much_West_2785** — Russian language fixes
* **[u/OOOOOOO0OOOOOOOOO](https://www.reddit.com/user/OOOOOOO0OOOOOOOOO/)** — Pause Before Standby battery workaround
* **[HiBy Modding Community](https://github.com/hiby-modding/hiby-mods)** — Sorting patch, binary research
* **Rockbox R1 development contributors**

---

> **Disclaimer:** These modifications are unofficial community firmware modifications. Flashing modified firmware always carries risk, including device malfunction or data loss. Neither HiBy nor the contributors listed above are responsible for any damage resulting from use of modified firmware. Proceed at your own risk and always keep a copy of the original firmware before flashing.
