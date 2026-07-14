# HiBy R1 — Top Hidden Features Unlock Guide

**Firmware:** v1.8.b1 (MIPS32 LE)  
**DAC:** TCS1421  
**Generated:** 2026-07-13 (updated from 2026-07-12)  
**Purpose:** Step-by-step instructions to enable the 5 biggest hidden features found via R3 Pro II cross-reference

---

## Quick Reference

| # | Feature | Difficulty | Method |
|---|---------|-----------|--------|
| 1 | DRE (Dynamic Range Enhancement) | Medium | Config → Binary patch fallback |
| 2 | Playback Speed (0.5x–2.0x) | Medium | Config → Binary patch fallback |
| 3 | VU Meter (Volume Indicator) | Medium | Config → Binary patch fallback |
| 4 | Lineout Mode | Easy | JSON edit only (`ot_devices.json`) |
| 5 | Play Key Remap | Easy–Medium | Config → Binary patch fallback |

---

## General Approach

1. **Try config edits first** — safe, reversible, worst case unknown keys get ignored
2. **Binary patch only if config fails** — trace xrefs in Ghidra from string address → find visibility branch → NOP/invert
3. **String files already present** — R1's `play_settings.ini` already contains all UI strings for these features

### Files to Edit

| File | Path | Purpose |
|------|------|---------|
| `config.json` | `squashfs-root/usr/resource/config.json` | Feature flags (`fcn0` array) |
| `set_functions.json` | `squashfs-root/usr/resource/set_functions.json` | Settings menu visibility |
| `ot_devices.json` | `squashfs-root/usr/resource/ot_devices.json` | Audio port registration |
| `play_settings.ini` | `squashfs-root/usr/resource/str/english/play_settings.ini` | UI strings (already present, no edit needed) |

---

## 1. DRE (Dynamic Range Enhancement)

**What it does:** Compresses dynamic range — boosts quiet parts, tames peaks. Good for noisy environments or low-volume listening.

### Evidence in R1 Binary

| Item | Address | Notes |
|------|---------|-------|
| `DRE_EN` | `0x0077a560` | DAC register flag string |
| `NOS_EN` | `0x0077a558` | Adjacent DAC flag (NOS mode — already accessible) |
| DAC sysfs path | `0x0077a4e4` | `/sys/devices/platform/tcs1421/tcs1421_cfg` |
| UI string | `play_settings.ini` line 65 | `<dre>DRE</dre>` — already present |

### How DRE Works Internally

`DRE_EN` is written to `/sys/devices/platform/tcs1421/tcs1421_cfg` — same mechanism as `NOS_EN`. The DAC driver interprets this flag and applies dynamic range compression at the hardware level.

### Enable Method

**Step 1 — Config attempt:**
```json
// config.json → fcn0 array, add:
{"dre_enable":1}
```

**Step 2 — If config doesn't work, binary patch:**
1. Open `hiby_player` in Ghidra
2. Go to `0x0077a560` (`DRE_EN` string)
3. Find xrefs (right-click → References → Find references to)
4. Trace to the function that reads DRE setting and writes to sysfs
5. Find the visibility/enable check — likely a branch comparing a config flag to 0
6. NOP the conditional branch or change `beq` → `bne` (MIPS: `0x10` → `0x14` at branch opcode byte)

### Verification

After flashing, DRE should appear in **Play Settings** menu. Toggle on → audio should sound more compressed/leveled.

---

## 2. Playback Speed

**What it does:** Adjust playback speed (typically 0.5x to 2.0x). Useful for podcasts, audiobooks, or pitch-shifted listening.

### Evidence in R1 Binary

| Item | Address | Notes |
|------|---------|-------|
| `speed_play` | `0x007711f4` | Settings ini key |
| `set_playspeed.dlg` | `0x0088dcd8` | Full dialog layout (embedded in binary) |
| `vg_dialog_set_playspeed` | `0x0088dddc` | Dialog widget group |
| `playing_plane_iv_playspeed` | `0x0079bd44` + `0x008a375c` | Playspeed button in playing plane view |
| UI string (settings) | `play_settings.ini` line 53 | `<playspeed>Playback Speed</playspeed>` |
| UI string (menu) | `play_settings.ini` line 15 | `<speed_play>Speed play</speed_play>` |

### Architecture

- **Dialog:** `set_playspeed.dlg` — complete speed selection dialog embedded in binary
- **Playing plane button:** `playing_plane_iv_playspeed` — icon in now-playing screen, visibility gated
- **View layout:** `hiby_playing_plane.view` (at `0x008a30b8`) already includes the playspeed widget
- **Create function:** `lg_playing_plane_create.c` (at `0x0079be10`) — builds the view, sets visibility per element

### Enable Method

**Step 1 — Config attempt:**
```json
// config.json → fcn0 array, add:
{"speed_play_enable":1}
```

**Step 2 — If config doesn't work, binary patch:**
1. Go to `0x007711f4` (`speed_play` string) in Ghidra
2. Find xrefs → trace to play settings list builder
3. Find conditional branch that skips adding `speed_play` row
4. NOP or invert the branch

**Also check playing plane visibility:**
1. Go to `0x0079bd44` (`playing_plane_iv_playspeed`)
2. Find xrefs → should show `lg_view_set_visible()` or `lg_view_set_alpha()` call
3. Change visibility argument from `0` → `1`

### Verification

- **Play Settings:** "Playback Speed" option should appear
- **Now Playing screen:** Speed icon should appear, tapping opens `set_playspeed.dlg`

---

## 3. VU Meter (Volume Indicator)

**What it does:** Real-time left/right volume level bars on the now-playing screen.

### Evidence in R1 Binary

| Item | Address | Notes |
|------|---------|-------|
| `playing_plane_vg_volume_meter_left` | `0x0079bd98` | Left channel meter widget |
| `playing_plane_vg_volume_meter_right` | `0x0079bdbc` | Right channel meter widget |
| UI string | `play_settings.ini` line 64 | `<volume_meter>Volume Indicator</volume_meter>` |
| View layout | `hiby_playing_plane.view` at `0x008a30b8` | Both meter widgets in layout definition |
| Create function | `lg_playing_plane_create.c` at `0x0079be10` | Sets visibility per element |

### Enable Method

**Step 1 — Config attempt:**
```json
// config.json → fcn0 array, add:
{"volume_meter_enable":1}
```

**Step 2 — If config doesn't work, binary patch:**
1. Go to `0x0079bd98` (`playing_plane_vg_volume_meter_left`) in Ghidra
2. Find xrefs → locate the `lg_view_set_visible()` or equivalent call
3. Change visibility argument from `0` → `1`
4. Repeat for `0x0079bdbc` (right channel)

### Verification

Now-playing screen should show animated volume bars (left + right channels) responding to audio playback level.

---

## 4. Lineout Mode

**What it does:** Fixed-level line output bypassing volume control. For external amplifiers or active speakers.

### Evidence in R1 Binary

| Item | Address | Notes |
|------|---------|-------|
| `lineout` | `0x0076ceec` | Port name string |
| `ot_device_analog_switch_port lineout` | `0x007f7938` | Analog switch handler |
| `pull_down_menu_iv_lineout` | `0x00799e88` + `0x008ab388` | Pull-down menu icon (2 refs) |
| `play_settings_tv_lo` / `play_settings_iv_lo` | `0x00799a94` / `0x007740a0` | Settings UI elements |
| UI string | `play_settings.ini` line 45 | `<lineout>Line out</lineout>` |

### Enable Method — JSON Only (no binary patch needed)

**Edit `ot_devices.json`**, add to PORTS array:
```json
{
    "Name": "lineout",
    "Enable": 1,
    "Priority": 2
}
```

**Also add to root level:**
```json
"LO_VOLUME_INDEX": 100
```

**Full `ot_devices.json` PORTS section should look like:**
```json
"PORTS":
[
    {
        "Name": "usb",
        "Enable": 1,
        "Priority": 7
    },
    {
        "Name": "a2dp",
        "Enable": 1,
        "Priority": 6
    },
    {
        "Name": "headset",
        "Enable": 1,
        "Priority": 3
    },
    {
        "Name": "lineout",
        "Enable": 1,
        "Priority": 2
    },
    {
        "Name": "default",
        "Port": "headset",
        "Enable": 1,
        "Priority": 0
    }
]
```

### Hardware Caveat

R1's 3.5mm jack may or may not support true line-level output. Some DAPs:
- **Same jack, voltage switch** — DAC/amp chip changes output impedance and level. Lineout works.
- **Single mode only** — jack always outputs headphone-level. Lineout mode would register but output same signal.

TCS1421 DAC supports lineout mode in hardware (`ot_device_analog_switch_port lineout` handler exists). Likely works.

### Verification

- Pull-down menu should show lineout icon
- Play Settings → Output selection should list "Line out"
- Volume control should lock at max when lineout active

---

## 5. Play Key Remap (Double-click Previous Song)

**What it does:** Changes play button behavior: single click = play/pause, double click = previous song.

### Evidence in R1 Binary

| Item | Address | Notes |
|------|---------|-------|
| `playkey_function` | `0x00796ba0` | Settings key name |
| `play_prev_song` | `0x00799d50` | Feature description string |
| `auto_scroll_playplane` | `0x00799ad4` | Related: auto-scroll to playing screen |
| UI string (feature) | `play_settings.ini` line 66 | `<playkey_function>Play key function</playkey_function>` |
| UI string (description) | `play_settings.ini` line 69 | `<play_prev_song>Single click: Play/Pause, double click: Previous song</play_prev_song>` |

### Enable Method

**Step 1 — Config attempt:**
```json
// config.json → fcn0 array, add:
{"playkey_function":1}
```

**Step 2 — If config doesn't work, binary patch:**
1. Go to `0x00796ba0` (`playkey_function`) in Ghidra
2. Find xrefs → locate settings list builder that decides whether to show this option
3. NOP/invert the visibility branch

### Bonus: Auto-scroll to Playback

While you're at it, also try:
```json
{"auto_scroll_playplane":1}
```
This makes the UI automatically navigate to the now-playing screen when a track starts.

### Verification

Play Settings should show "Play key function" option. When enabled, double-clicking the play button should skip to previous track.

---

## All-in-One Config Edit

### config.json — Complete fcn0 Addition

```json
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
        {"qplay3_enable":0},
        {"dre_enable":1},
        {"speed_play_enable":1},
        {"volume_meter_enable":1},
        {"playkey_function":1},
        {"auto_scroll_playplane":1}
    ]
}
```

### set_functions.json — Additional Toggles

```json
{"standby":1},
{"dac_feedback":1},
{"about":1}
```

### ot_devices.json — Add Lineout Port + LO_VOLUME_INDEX

See Section 4 above for full JSON.

---

## Binary Patching Quick Reference

### MIPS32 LE Branch Patch Cheat Sheet

| Instruction | Encoding (LE) | Use |
|-------------|---------------|-----|
| `beq` (branch if equal) | `0x10` at byte 3 | Conditional skip |
| `bne` (branch if not equal) | `0x14` at byte 3 | Inverted conditional |
| `nop` | `0x00000000` | Remove branch entirely |
| `b` (unconditional) | `0x1000xxxx` | Force-take branch |

**To invert a gate:** Change `beq` (`0x10`) → `bne` (`0x14`) or vice versa at the branch instruction.

**To remove a gate:** Replace 4-byte branch instruction with `0x00000000` (NOP).

### Key String Addresses for Ghidra Xref Tracing

| String | Address | Trace to find |
|--------|---------|---------------|
| `DRE_EN` | `0x0077a560` | DAC sysfs write function → settings visibility check |
| `speed_play` | `0x007711f4` | Play settings list builder → visibility branch |
| `playing_plane_vg_volume_meter_left` | `0x0079bd98` | Playing plane create → `set_visible()` call |
| `playing_plane_vg_volume_meter_right` | `0x0079bdbc` | Same as above, right channel |
| `playkey_function` | `0x00796ba0` | Play settings list builder → visibility branch |
| `playing_plane_iv_playspeed` | `0x0079bd44` | Playing plane create → `set_visible()` call |
| `set_playspeed.dlg` | `0x0088dcd8` | Dialog load path — verify dialog exists |
| `lineout` | `0x0076ceec` | Port registration — should work via JSON |

---

## Workflow

```
1. Edit config.json, set_functions.json, ot_devices.json
2. Repack squashfs: mksquashfs squashfs-root system.bin -comp xz -b 131072
3. Flash to device
4. Check Play Settings menu for new options
5. If features don't appear → Ghidra binary patch approach
6. After binary patch → repack and reflash
```

---

## Related Files

- `R3_Pro_II_vs_R1_Feature_Comparison.md` — Full cross-device feature matrix
- `HiBy_R1_v1.8b1_Hidden_Features_Analysis.md` — All hidden features (not just top 5)
- `HIBY_R1_HIDDEN_FEATURES_ANALYSIS.md` — Complete 31-section firmware reference

> **v1.8.b1 Note:** Three features previously listed as R3 Pro II-exclusive (`explorer_in_cue_enable`, `otg_scan_enable`, and `qplay3_enable`) now have native config.json entries in v1.8.b1. They ship disabled (=0) but can be enabled via simple JSON edit — no binary patching needed.
