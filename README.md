# 🎶 HiBy R1 Ultimate Custom Firmware Mod

Welcome to the ultimate custom firmware for the **HiBy R1** (based on v1.8.b1)! This project unlocks the absolute full potential of your device by exposing hidden features, maximizing performance, improving battery health, and completely overhauling the user interface. 

Device runs HiBy Linux on an **Ingenic X1600** MIPS SoC with a **Cirrus Logic CS43131** DAC/amp chip.

---

> [!IMPORTANT]  
> ## 💖 The Mission: Support the Development!
> Endless hours of reverse engineering, Ghidra decompiling, kernel testing, and UI patching went into unlocking this device for the community. I am currently raising funds to purchase other HiBy devices to expand this modding ecosystem.
> 
> **Target: $200 USD** 🎯  
> Your support directly funds the purchase of hardware for reverse engineering. If this custom firmware breathed new life into your HiBy R1, please consider leaving a tip!
> 
> **☕ [Sponsor me on GitHub](https://github.com/sponsors/bidhata?frequency=one-time&sponsor=bidhata)**  


---

## 🚀 NEW: Exclusively in this v1.8.b1 Build

### 1. Unified Theme Port (by @Jepl4r)
We've ported the gorgeous **Unified Theme** originally designed for the R3 Pro II:
- Modernized icons across the entire UI.
- Rounded launcher design and sleek custom boot logo.
- *Default theme set to Theme 2 (Dark Mode) on fresh install.*

### 2. Battery Health Protection (95% Charge Limit)
If you use your R1 tethered as a USB DAC, keeping the battery at 100% permanently degrades its lifespan. 
- Modified the AXP2101 PMU kernel driver (`module_driver/axp2101.sh`).
- Dropped the maximum charging threshold from `4400` (4.4V) to `4350` (4.35V). 
- Based on lithium-ion discharge curves, this limits the maximum physical battery charge to roughly **90-95%**, significantly extending the lifespan of your battery!

### 3. Maximum Performance & Responsiveness Tweaks
Injected custom kernel parameters into the startup script (`hiby_player.sh`) to make the device incredibly snappy:
- **SD Card Read-Ahead:** Expanded the read-ahead buffer to 2MB (from 128KB) to eliminate UI stuttering when scanning libraries or loading large FLAC files.
- **Sysctl Cache Tuning:** Tuned `vm.vfs_cache_pressure` to keep UI assets and album art cached in RAM longer.
- **Enabled SD Card Caching:** Album art and the music database are now explicitly cached to the MicroSD card, taking the load off the slower internal flash memory.

### 4. Fully Unlocked Hidden Features
We've patched the UI and configuration JSONs (`set_functions.json` & `config.json`) to expose features HiBy hid from the stock firmware:
- **Advanced DAC Settings:** USB current limit toggle (reduces charging noise) and DAC feedback toggle.
- **Car Mode:** Auto-play music when power is connected.
- **Double Tap to Wake:** Wake the screen without pressing the power button.
- **CUE Sheet Explorer:** Browse and play CUE sheets directly from the file explorer.
- **OTG Scanning:** Scan and build a library directly from OTG-connected USB flash drives.
- **QPlay 3.0 Support:** Enabled Tencent QQ Music streaming protocol.

### 5. System Stability Fixes
- Fixed a major bug in the stock firmware where clicking "System Settings" would result in a "No Music Found" crash due to corrupted UTF-16LE XML headers in the English translation files (`.ini`). 

---

## 🔥 Classic Features (Carried Over)

### 1. Parametric Equalizer (PEQ) Fully Enabled
Combined the 1.6 Mod firmware modifications with the 1.7 Beta audio engine to fully unlock PEQ:
- Multi-band Parametric Equalizer (Frequency, Gain, Q-factor control).
- Custom EQ presets saving and loading.

### 2. Native DSD & MDB/LDB Hardware Gain Fixed
- DSD output paths were forced to DoP even when Native was selected. Now, `AnalogDsdNative` correctly outputs hardware native DSD over I2S to the CS43131.
- Volume hardware gain tables were artificially capped. Adjusted values to reach true `0 dB` maximum output.
- Headset UI volume cap safely increased from `50` to `60`, and line-out volume locked to `100`.

### 3. USB DAC Mode Unlocked
The USB DAC functionality is fully present in the kernel driver (`uac_sa`) but was hidden. We unlocked the **USB device mode** dropdown (Storage / Audio / Dock) in the settings!

### 4. Mono / Stereo Toggle (Trigger File Method)
Adds support for stereo-to-mono downmixing for users with single-sided hearing or specific monitoring needs.
- **To enable Mono:** Create an empty file named `MONO` in the root of your SD card. The device will reboot into Mono mode.

### 5. Global Font Replacement
Replaced the stock HiBy font with the highly legible **MiSans** font (renamed to `default.otf` to fix the HiBy apostrophe spacing bug).

---

## 🛠️ Installation Instructions


1. Download the modded r1.upt .
2. Copy it to sdcard.
3. Flash to your HiBy R1 via Flash Recovery from SDCARD under System Setting.
4. Enjoy!

---

## ⚙️ CS43131 Capabilities (Unlocked by These Mods)

| Feature | Spec |
|---------|------|
| PCM | 32-bit, up to 384 kHz |
| DSD | DSD64 / DSD128, Native or DoP |
| SNR | 130 dB |
| THD+N | −107 dB |
| Output power | ~112 mW @ 16 Ω |
| Digital filters | 5 hardware modes (fast/slow rolloff × min-phase/linear-phase + apodizing) |

---

## 🏆 Credits

* **@Jepl4r** for the gorgeous Unified Theme port.
* **u/hrwoyem** for PEQ enablement and USB DAC unlocking research.
* **ApolloMoonLandings** & **Much_West_2785** for feature requests and translation fixes.
* The HiBy Modding Community and Rockbox R1 development contributors.

> **Disclaimer:** These modifications are unofficial community firmware modifications. Flashing modified firmware always carries risk. Neither HiBy nor the contributors listed above are responsible for any damage resulting from use of modified firmware. Proceed at your own risk!
