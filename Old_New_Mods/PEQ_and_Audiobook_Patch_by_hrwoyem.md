# HiBy R1 Community Enhancement Patch

A community-maintained collection of discoveries, patches, and firmware enhancements for the HiBy R1.

---

## Overview

This project documents community-discovered methods for unlocking hidden functionality already present within the HiBy R1 firmware.

The objective is to provide a reproducible patching process that can be applied to future firmware releases without redistributing proprietary firmware images.

---

# Features

## USB DAC Mode Unlock

The HiBy R1 firmware contains dormant USB DAC functionality that can be enabled through configuration changes.

### Enabled Options

* USB Storage Mode
* USB DAC Mode

### Required Changes

### `/usr/resource/config.json`

```json
"dac_to_store": 1
```

### `/usr/resource/set_functions.json`

```json
{"usb_mode":1},
{"dac_feedback":1}
```

### `/usr/resource/midi_set_functions.json`

```json
{"usb_mode":1},
{"dac_feedbak":1}
```

---

## Car Mode Unlock

Enables the hidden Car Mode menu.

### Required Changes

### `/usr/resource/set_functions.json`

```json
{"car_mode":1}
```

### `/usr/resource/midi_set_functions.json`

```json
{"car_mode":1}
```

---

## About Menu Unlock

Enables hidden About/System Information menu entries where supported by the firmware.

---

## Parametric Equalizer (PEQ) Enablement

Investigation of HiBy firmware versions revealed that the complete Parametric Equalizer framework already exists within the firmware resources.

The stock 1.6 firmware contains:

* PEQ UI layouts
* PEQ dialogs
* PEQ configuration framework

However, the bundled audio engine does not fully expose the functionality.

Replacing the stock 1.6 `hiby_player` with the firmware 1.7 Beta `hiby_player` enables full PEQ functionality.

### Required Files From 1.7 Beta

```text
/usr/bin/hiby_player
/usr/resource/eq.ini
/usr/resource/str/*/eq.ini
```

### Enabled Functionality

* PEQ Combined
* Multi-band Parametric EQ
* Filter Type Selection
* Frequency Adjustment
* Gain Adjustment
* Q-Factor Adjustment
* Preset Save / Recall

---

# Recommended Firmware Configuration

The currently recommended firmware combination is:

```text
HiBy R1 1.6 Mod
+
HiBy R1 1.7 Beta hiby_player
+
HiBy R1 1.7 Beta EQ Resources
```

This configuration provides:

| Feature              | Status |
| -------------------- | ------ |
| Volume Unlock        | ✅      |
| Native DSD           | ✅      |
| Internet Radio       | ✅      |
| SBC XQ Bluetooth     | ✅      |
| USB DAC Mode         | ✅      |
| Standby Menu         | ✅      |
| Car Mode             | ✅      |
| About Menu           | ✅      |
| PEQ Combined         | ✅      |
| Parametric EQ        | ✅      |
| Sorting Improvements | ✅      |

---

# Validation

Successfully tested on physical HiBy R1 hardware.

Validated functionality:

* Local Playback
* USB DAC Mode
* Bluetooth Audio
* Media Library Scanning
* MSEB
* Standard EQ
* Parametric EQ
* Internet Radio
* Standby Mode
* Car Mode

---

# Future Patch Framework

Suggested repository structure:

```text
patches/
├── 001_usb_dac.py
├── 002_car_mode.py
├── 003_about_menu.py
├── 004_peq_enable.py
├── manifest.json
└── apply_patch.py
```

Example:

```bash
python3 apply_patch.py \
  --firmware r1_1.8.upt \
  --enable-usbdac \
  --enable-peq
```

Output:

```text
r1_1.8_community_patched.upt
```

---

# Credits

## Research & Validation

The following discoveries, testing, validation work, and firmware compatibility research were performed by:

### u/hrwoyem

Reddit:

https://www.reddit.com/user/hrwoyem/

Contributions include:

* USB DAC mode enablement
* OTA package reconstruction validation
* Firmware repacking validation
* 1.6 Mod ↔ 1.7 Beta compatibility testing
* PEQ enablement research
* PEQ resource migration testing
* Community documentation

---

## Additional Credits

* HiBy Modding Community
* Rockbox R1 Contributors
* Community reverse-engineering contributors
* Firmware testing volunteers

---

# Disclaimer

This project is an unofficial community effort.

Flashing modified firmware always carries risk and may result in device malfunction, data loss, or warranty voidance.

Neither HiBy nor any community contributor is responsible for any damage resulting from the use of modified firmware.

Always keep a copy of the original firmware before flashing modified firmware.
