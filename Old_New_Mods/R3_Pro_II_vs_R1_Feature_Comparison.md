# HiBy R3 Pro II vs R1 — Feature Comparison

**R3 Pro II firmware:** v1.4.b3  
**R1 firmware:** v1.8.b1 (updated from v1.7b1)  
**Generated:** 2026-07-13 (updated from 2026-07-12)  
**Purpose:** Identify R3 Pro II features that can be enabled in R1's firmware

---

## CAN Enable (code exists in R1 binary)

| Feature | R3 Config Source | R1 Binary Address | Enable Method |
|---------|-----------------|-------------------|---------------|
| **DRE** (Dynamic Range Enhancement) | `play_settings.ini` | `0x0077f0e0` (`DRE_EN`) | Add UI string + `set_functions.json` entry |
| **Volume Meter / VU Meter** | `play_settings.ini` | `0x007a0a80` / `0x007a0aa4` (left/right) | UI layout references exist, needs settings gate |
| **Playback Speed** | `play_settings.ini` | `0x00775a24` (`speed_play`), `0x00892bfc` (`set_playspeed.dlg`) | Full dialog + logic present. Add to `set_functions.json` |
| **Play Key Function** (button remap) | `play_settings.ini` | `0x0079b738` (`playkey_function`) | Add settings entry |
| **Auto Scroll to Playback** | `play_settings.ini` | `0x0079e754` (`auto_scroll_playplane`) | Add settings entry |
| **Double-click Previous Song** | `play_settings.ini` | `0x0079e9e0` (`play_prev_song`) | Add settings entry |
| **Digital Volume Max** | `config.json` | `0x007757f8` (`digital_volume_max`) | Already in `config.json` — change value |
| **Timezone Settings** | `zone_info.ini` | `0x00797f94` (`zone_set`) | UI + data present |
| **Character Encoding Sort** | `play_settings.ini` | `0x00797f48` (`char_first_than_pinyin`) | Add settings entry |
| **EULA System** | — | `0x00778a68` (`vg_listview_eula`) + multiple refs | Full implementation, just hidden |
| **Scan Music Gain** | `play_settings.ini` | `0x0079d31c` (`scan_music_gain`) | Add settings entry |
| **Fast Find Toggle** | `play_settings.ini` | `0x0079d32c` (`fast_find_disable`) | Add settings entry |
| **Lineout Mode** | `ot_devices.json` | `0x007f9ef0` (`balance_lineout`), `0x007fc844` (switch port) | Port switching code present. Add to `ot_devices.json` |
| **Balance Output** (software only) | `ot_devices.json` | `0x007716f4` (`balance`), `0x007fc81c` (switch port) | Code exists but **R1 has no balanced jack** — software path only, no physical output |
| **DAC Feedback** | `set_functions.json` (`dac_feedback:1`) | — | R1 has `dac_feedback:0` — flip to `1` |
| **Standby Mode** | `set_functions.json` (`standby:1`) | — | R1 has `standby:0` — flip to `1` |
| **Double Touch Wakeup** | `set_functions.json` (`double_touch_wakeup:1`) | — | R1 has `0` — flip to `1` (needs touchscreen) |
| **About Page** | `set_functions.json` (`about:1`) | — | R1 has `about:0` — flip to `1` |
| **CUE in File Explorer** | `config.json` (`explorer_in_cue_enable`) | Now in `config.json` fcn0 | **NEW in v1.8.b1** — flip `0` → `1` in config.json |
| **OTG Scan** | `config.json` (`otg_scan_enable`) | Now in `config.json` fcn0 | **NEW in v1.8.b1** — flip `0` → `1` in config.json |
| **QPlay 3.0** | `config.json` (`qplay3_enable`) | Now in `config.json` fcn0 | **NEW in v1.8.b1** — flip `0` → `1` in config.json |
| **PEQ (Parametric EQ)** | `play_settings.ini` | `0x007a5dc8` (`peq_support`) | **NEW in v1.8.b1** — Add settings entry |
| **Chapter Progress** | `play_settings.ini` | `0x00794648` (`vg_listview_chapter`) | **NEW in v1.8.b1** — Add settings entry |
| **Tech Support Link** | `sys_set.ini` | `0x0089e9f8` (approx) | **NEW in v1.8.b1** — Add settings entry |

---

## CANNOT Enable (not in binary or hardware-dependent)

| Feature | Reason |
|---------|--------|
| **Balanced Lineout** (physical port) | R1 has no balanced output jack. Code refs exist but no hardware |
| ~~**CUE in File Explorer**~~ | ~~String not found in R1 binary~~ → **Now available in v1.8.b1** (moved to CAN Enable) |
| ~~**OTG Scan**~~ | ~~String not found in R1 binary~~ → **Now available in v1.8.b1** (moved to CAN Enable) |
| **Default Lock Volume** | String not found in R1 binary |
| **Custom Themes** (`self_theme`) | String not found in R1 binary |
| ~~**Chapter Progress**~~ (`chapter_pro`) | ~~String not found in R1 binary~~ → **Now available in v1.8.b1** (moved to CAN Enable) |
| ~~**Tech Support Link**~~ | ~~String not found in R1 binary~~ → **Now available in v1.8.b1** (moved to CAN Enable) |
| ~~**PEQ**~~ (Parametric EQ) | ~~String not found in R1 binary~~ → **Now available in v1.8.b1** (moved to CAN Enable) |

---

## Easiest Wins (JSON-only, no binary patch needed)

### 1. `set_functions.json` — flip `0` → `1`

**File:** `squashfs-root/usr/resource/set_functions.json`

```json
"standby": 1,
"dac_feedback": 1,
"about": 1,
"double_touch_wakeup": 1
```

### 2. `config.json` — adjust values

**File:** `squashfs-root/usr/resource/config.json`

- `digital_volume_max` — raise limit
- `explorer_in_cue_enable: 1` — **NEW in v1.8.b1** (ships as 0)
- `otg_scan_enable: 1` — **NEW in v1.8.b1** (ships as 0)
- `qplay3_enable: 1` — **NEW in v1.8.b1** (ships as 0)
- Add DRE flag if config-gated

### 3. `ot_devices.json` — add lineout port

**File:** `squashfs-root/usr/resource/ot_devices.json`

Add lineout port entry (if 3.5mm jack supports line-level output):
```json
{
    "name": "lineout",
    "priority": 2,
    "io_port": "lineout"
}
```

---

## Top Hidden Gems

Biggest unlocks — full code in R1, just no UI path:

1. **DRE** — Dynamic Range Enhancement, complete implementation at `0x0077f0e0`
2. **Playback Speed** — Full dialog (`set_playspeed.dlg`) + speed control logic
3. **VU Meter** — Left/right volume meter UI elements ready
4. **Lineout Mode** — Port switching code with analog switch handler
5. **Play Key Remap** — Button function reassignment

---

## Reference: R3 Pro II Config Files Examined

| File | Path (R3) | Key Differences |
|------|-----------|-----------------|
| `config.json` | `usr/resource/config.json` | `explorer_in_cue_enable`, `otg_scan_enable`, `qplay3_enable` (all NEW in v1.8.b1), `dac_to_store`, balance volume config, 768kHz sample rate |
| `set_functions.json` | `usr/resource/set_functions.json` | `dac_feedback:1`, `standby:1`, `double_touch_wakeup:1`, `about:1` |
| `ot_devices.json` | `usr/resource/ot_devices.json` | `balance`, `balance_lineout`, `lineout` ports; `SET_HW_SW_VOL_BOTH:1`, `A2DP_FIXED_GAIN:1` |
| `play_settings.ini` | `usr/resource/str/english/play_settings.ini` | `volume_meter`, `dre`, `playkey_function`, `play_prev_song`, `auto_scroll_playplane`, `peq`, `playspeed`, `chapter_pro` |
| `play_menu.ini` | `usr/resource/str/english/play_menu.ini` | `audio_quality` selector |
| `settings.ini` | `usr/resource/str/english/settings.ini` | `net_radio`, `recorder`, `step`, `usb_dac` |

---

## Methodology

1. Read all R3 Pro II config/string files from `C:\Users\me\Desktop\Projects\HibyR3\squashfs-root\`
2. Diffed against R1 equivalents at `C:\Users\me\Desktop\Projects\HibyR1\17\squashfs-root\`
3. Searched R1 Ghidra string export (`C:\tmp\ghidra_hiby\output\strings.txt`) for every R3-exclusive feature string
4. Categorized by: string found in binary (enableable) vs not found (absent/hardware-dependent)
5. **v1.8.b1 update:** Re-scanned config.json for newly added flags — `explorer_in_cue_enable`, `otg_scan_enable`, `qplay3_enable` moved from "CANNOT" to "CAN" enable
