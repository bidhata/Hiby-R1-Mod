**Please remember:** this is a community reverse-engineering mod, not an official HiBy release. Everything below was verified by diffing a full unpacked stock rootfs against the modded one, file by file. Test it, break it, report back. Also, I'm not a developer — I'm just a **curious reverse-engineering addict** who can't resist poking at firmware to see what happens. 😄

I basically flip every switch I can find, cross my fingers, and release the modded firmware into the wild. There's no way I can test every single feature or setting on my own. So think of this as a community experiment: you break it, test it, report it, and together we'll figure out what actually works and what still needs tweaking. Every bug report, success story, and weird discovery helps make the next release better. Without your feedback, I'm just a guy enthusiastically pressing random buttons in an editor.
>I am currently raising funds to purchase other HiBy devices to expand this modding ecosystem.
> 
> ### 🎯 Target: $200 USD
> Your support directly funds the purchase of hardware for reverse engineering and firmware testing. Next Targeted devicees - Hiby R3 Pro II, Hiditzs AP80 Pro-X, Tempotec V1 and others
> 
> **💖 [Sponsor me on GitHub](https://github.com/sponsors/bidhata?frequency=one-time&sponsor=bidhata)**
>
> via **USDT :** 0xcb6989985389f0a983bb20582d97e665128735e1 ( BEP20 )
>
> via **Paypal :** bidhata@gmail.com 
>
> **Every dollar keeps this project alive and motivates future updates!**



# 🔬 HiBy R1 Firmware V2.0: A Teardown, Byte by Byte

> HiBy pushed V2.0 to their OTA server before posting a public download link anywhere, so I asked the server politely and it handed it over. Then I spent a weekend inside a MIPS binary with Ghidra and a lot of coffee. This is what I found.
>
> The rule this time: **nothing here is a guess from `strings`.** Every claim about `hiby_player` was traced to the code that actually uses it. (Lesson learned the hard way. More on that in §9.)

**The package:** official `r1.upt`, MD5 `c9577de224859fc76272ca340ed6eb82`, 44,255,232 bytes. It came from `otaserver.hiby.com`, which redirects to `oss.hiby.com/HiBy R1/en-us/2.0/…`. Built **2026-10-09**.

---

## 📦 1. What's in the box (`r1.upt`)

Surprise: the `.upt` is just an **ISO 9660 image** wearing a fancy extension. Same container as previous releases, so the repo's `unpack_pack/unpack.sh` and `repack.sh` still apply (with one catch, below).

```
ota_config.in                      current_version=0
ota_v0/ota_compatibility.in        compatibility=hiby_r1_x1600_auto_rf_auto
ota_v0/ota_update.in               kernel  xImage           3,731,520 B  md5 c446c0a3…
                                   rootfs  rootfs.squashfs 40,103,936 B  md5 f50a0ca0…
ota_v0/ota_md5_<img>.<md5>         per-chunk md5 list
ota_v0/xImage.NNNN.<md5>           8 × 512 KiB chunks
ota_v0/rootfs.squashfs.NNNN.<md5>  77 × 512 KiB chunks
ota_v0/ota_v0.ok
```

Things worth noticing:

- **The compatibility string grew a suffix: `…_auto_rf_auto`.** One image now serves *two* different wireless chipsets and figures out at boot which one it's on. See §3; this is the biggest change in the whole release.
- **Integrity is MD5 only. No signature.** Each chunk's file name carries the MD5 of the chunk before it, a cute little hash chain. It catches corrupted downloads, and it lets us roll our own firmware. 🎉
- ⚠️ The repo's `repack.sh` doesn't write `ota_v0/ota_compatibility.in`. V2.0 ships one, so a V2.0 mod should too.

---

## 🐧 2. The system underneath

| Thing | Value |
|---|---|
| SoC | Ingenic X1600 family. MIPS32r2, 32-bit little-endian. Yes, MIPS, in 2026. Respect. |
| Kernel | Linux 4.4.94+ PREEMPT, gcc 5.2.0 (Ingenic r3.2.1), build #112, Fri Oct 9 12:04 CST 2026 |
| Userland | Buildroot 2020.02.1 + BusyBox 1.31.1 (built 2026-09-14) |
| Libraries | OpenSSL 1.1.1f, libcurl 7.68.0, BlueZ 5 (libbluetooth 3.19.2), bluealsa, ALSA, glib/dbus |
| Hostname | `ingenic` (nobody renamed it, and honestly, same) |
| Rootfs | read-only squashfs, 88 MB unpacked, 4,409 files |
| Your stuff | UBIFS volume `userdata` on `/usr/data` (`/data` is a symlink to it) |

### How it boots (`/etc/init.d`)

| Script | What it does |
|---|---|
| S10mdev | creates device nodes |
| S11jpeg_display_shell | the boot logo. Reads a **`theme:N` byte from `/dev/mtd5` at 0x20000** and shows `logo.jpeg`, `logo1.jpeg` or `logo2.jpeg` to match your theme, then sets the backlight to 60 |
| S11module_driver_default | loads every kernel module in `/module_driver` |
| S21mount_ubifs | mounts userdata. If the mount fails it *formats the partition and tries again*. Bold. |
| S30dbus | dbus |
| S39_recovery.recovery | if `/data/recovery_all` exists: factory reset, then reboot |
| S40network | networking |
| S43wifi_bcm_init_config | MAC setup, **Broadcom units only** (bails out on AIC units) |
| S50sys_server | the `sys_server` daemon (clears `/mnt/*` first) |
| S52wifi_aic_init_config | **🆕 new:** MAC setup for the AIC8800 |
| S80_bt_init | Bluetooth |
| S92_03_start_music_player | runs `hiby_player.sh`, which starts `hiby_player` and **reboots the device if the player ever exits**. No desktop to fall back to here. |
| T90adb | ADB gadget (VID 0x18d1 / PID 0xd002) |

`bootmode.sh` flips between two kernel slots by writing `ota:kernel` or `ota:kernel2` to `/dev/mtd5`.

---

## 📡 3. Plot twist: there's a new R1 board

This is the headline, and it isn't in any changelog. A new script, `module_driver/r1_wireless_select.sh`, asks the AXP2101 power chip for the SoC ID and loads a different radio driver depending on the answer:

| SoC ID | SoC | Wireless chip | Driver |
|---|---|---|---|
| 0x0A00 | X1600E | Broadcom/Cypress (the original R1) | `cywdhd.ko` via `cywdhd.r1` |
| 0x4A00 | **X1600EN** | **AIC8800D40** | `aic8800_bsp.ko`, `aic8800_btlpm.ko`, `aic8800_fdrv.ko` |
| 0x4900 | **X1600HN** | **AIC8800D40** | same as above |
| unknown | — | falls back to Broadcom | — |

So HiBy quietly swapped the Wi-Fi/Bluetooth chip (and the SoC variant) on newer units. Details:

- The verdict is saved to `/run/r1_wireless_type` (`bcm` or `aic`), and other scripts check it.
- On AIC units the Wi-Fi driver loads **only when needed** (`aic8800_fdrv.load`) with **`disable_5g=1`**. New-board R1s are **2.4 GHz only**.
- `axp2101.sh` got a new knob, `dcdc4_auto_x1600e_en_hn=1`, which sets the core voltage per SoC variant.
- New helpers came along: `aic_radio.sh`, `bt_enable_aic.sh`, `bt_test`, `cmd_gpio`, and even `iperf3`.

> 🚨 **For modders, this matters a lot.** If you bought an R1 recently, it may have the new X1600EN/HN + AIC8800 board. Mods built on ≤1.8b2 have **no AIC drivers**, so **Wi-Fi and Bluetooth simply won't work** on those units. Build new mods on 2.0, and don't touch `r1_wireless_select.sh`, `aic8800_*`, `lib/firmware/aic8800d40_sdio_fw` or `S52wifi_aic_init_config`.

### The rest of the board (read off kernel module parameters)

| Part | Detail |
|---|---|
| DAC | Cirrus **CS43131**. I²C bus 3 is *bit-banged* on PB30/PB31 (old school), power PB02, reset PB21 |
| PMIC | AXP2101, charges to 4.40 V, 100 mA termination |
| Fuel gauge | CW2015 with a custom battery profile |
| Display | LG35583 panel, 480×800, SPI bus 5, PWM backlight on PC00 at 30 kHz (max 101) |
| Touch | CST8xx at 0x15 on I²C1, single touch |
| Keys | Power PC31, Next PC28 (GPIO). ADC keys: 164 Play/Pause, 114/115 Vol |
| Inline remote | `sa_earpods_adc` on ADC ch2 (play/pause, vol+, vol−) |
| LEDs | red PC01 (breathing!), blue PC02 |
| USB-C | TCS1421 (CFG0 PA09 / CFG1 PB24) |
| SD | msc1, removable, card-detect PB22, power PC25, 50 MHz |
| `sau.ko` | a small new "SmartAction" module exporting `sa_set/get_sau_verify`. Smells like a device-verification hook. Keeping an eye on it 👀 |

---

## 🎵 4. `hiby_player`: the whole show in one binary

- `usr/bin/hiby_player`: 5,103,852 bytes, MIPS32 ELF, stripped, dynamically linked (libasound, libcurl, OpenSSL 1.1, libqrencode, libz, libwpa_client).
- **One binary runs three products:** HiBy R1, MiDi R1 and the unreleased **R1 Pro**. A config JSON decides which one it is.

### What's new in 2.0

**🎚️ PEQ (parametric EQ)**, in `layout/theme*/peq/hiby_peq.view` and `dialog/peq_filter.dlg`
- Filter types: Low Shelf, Peaking, High Shelf. Fc 30–20,000 Hz, Q 0.2–20, gain ±9 dB, preamp −9…+2 dB.
- Presets live in `/data/peq/<name>.peq`.
- Import: drop files exported from the HiBy Music app into a `peq` folder on the SD card.
- 🤓 **It also parses AutoEQ text format**: `Preamp: %f dB` and `Filter N: ON PK Fc %f Hz Gain %f dB Q %f`. A `ParametricEQ.txt` from AutoEQ should import directly. (The error message for a bad file reads, verbatim, "Parse autoeq faile not txt". We love it.)
- There's a hidden internal debug dialog, "PEQ Combined Settings", with **32 bands** (Band 0–31) and an FFT view.

**🎮 Games**, with a launcher icon at `litegui/theme*/launcher/games.png`
- **2048**: save file `/data/game_2048.dat`
- **Sudoku**: difficulty levels, hints, a timer, best score. Save file `/data/game_sudoku.dat`
- **24 Points**: the card game where you make 24 from four numbers (`games/hiby_24.view`, icon `games/24_points.png`). It's right there in the Games menu, not hidden.

**⏸️ Timed playback pause**, under Play settings → "Timed playback pause". If it clashes with standby, power-saving shutdown or sleep shutdown, the app offers to turn those off.

### Who it phones home to

| Endpoint | Why |
|---|---|
| `otaserver.hiby.com/app/ota/getOtaInfo` (POST `productName=HiBy R1` — yes, with a space — plus `versionNumber`, `deviceNumber`, `languageCode`) | firmware updates (this is how I got V2.0 early) |
| `otaserver.hiby.com/app/ota/updateDeviceVersion?productName=%s%%20%s` | reports your version |
| `otaserver.hiby.com/app/radio/getRadioInfo` | internet radio list |
| `otaserver.hiby.com/app/earphoneSet/getEarphoneSet` | headphone presets |
| `otaserver.hiby.com/app/disclaimer/getDisclaimer` | the EULA |
| `track.hiby.com/app/product/guide` | online manual |
| `api.tidal.com`, `auth.tidal.com` (device OAuth) | TIDAL |
| `www.qobuz.com/api.json/0.2` | Qobuz |
| `http://<ip>:4399` | Wi-Fi file transfer (`thttpd` + `udp_server`) |

Also on board: AirPlay (`shairport`), a DLNA renderer (`dmrd`), a Samba client, HiByLink, Bluetooth input, USB DAC (UAC) and SPDIF screens.

### The audio stack (from strings)

LDAC (HQ/SQ/ABR), aptX/aptX-HD, SBC. Gapless, ReplayGain (track/album), a resampler, DSD2PCM (DSD64–512) and native DSD.
Demuxers: FLAC, APE, DSF, DFF, AAC, AIFF, ASF, TS, WAV, MP3, and M4B with bookmarks kept in `/usr/data/m4b_bookmarks.sqlite3`. Audiobook people, rejoice.

---

## ⚙️ 5. Config files, feature flags, and the R1 Pro

### `usr/resource/config.json` (stock R1)

```
product:  HiBy / R1 / version 2.0 / ota_name "HiBy R1" / VID 0x32BB PID 0x0101 / DAC PID 0x0004
screen:   480×800, topbar 50, list item 126
volume:   default 20, warn 34, lock 50 (headset)
function: ON:  book_set_percent most_played dark_theme_enable screen_short_enable lyric_color_enable
          OFF: dac_to_store tf_image_cache_enable tf_music_db_enable explorer_in_cue_enable
               otg_scan_enable qplay3_enable playmenu_eq_disable
```

### `usr/resource/set_functions.json`: rows HiBy switched off (0)

`usb_mode`, `dac_charge_disable`, `dac_feedback`, `car_mode`, `car_mode_auto_play`, `double_touch_wakeup`, `volkey_locked`, `rotation`, `operating_instruction`.

Tempting! But most of these come from HiBy's shared codebase for its other players, so "the row exists" doesn't mean "it does anything on the R1". §9.2 has the verdicts from the actual code.

### 👀 The R1 Pro (unreleased)

Three files give it away: `r1pro_config.json`, `r1pro_ot_devices.json`, and `r1pro_bt_name` ("HiBy R1PRO").

- `device: R1PRO`, `version: 0.01`, `ota_name: HiBy R1PRO`. Same 480×800 screen, the same USB VID/PID and the same feature flags.
- Its output-device table is identical to the R1's except for **three extra ports**: `balance` (priority 5), `balance_lineout` (4) and `lineout` (2).
- Translation: **the R1 Pro adds a balanced headphone output, balanced line out and single-ended line out.** Sample rates (up to 384 kHz), DSD over DoP and the volume tables are unchanged.
- So it's the same brain with more jacks. You can't "enable" that on an R1: there are no extra jacks to enable.

### MiDi R1

`midi_config.json`, `midi_set_functions.json`, `layout/midi`, `litegui/midi`. The MiDi-branded twin is still alive and gets PEQ too.

### Languages (13)

english, french, german, italy, japanese, korean, poland, russian, simplified_chinese, spain, thai, traditional_chinese, ukrainian. The `.ini` files are UTF-16 XML (because of course they are).

Fonts: `default.otf` (a chunky 8.3 MB), `msyh.ttf`, `Korean.ttf`, `Thai.ttf`.

---

## 💾 6. Magic files on the SD card and in userdata

| Path | What it does |
|---|---|
| `<SD>/r1.upt` | local firmware update (the code looks for `%s/sd_0/%s.upt`) |
| `<SD>/peq/` | PEQ imports |
| `<SD>/hiby_linux_factory_mode` | 🏭 opens the **factory test screen** on every boot until you delete it (§9.3) |
| `<SD>/hiby_linux_auto_test` | arms a touch-logging auto-test that wakes up after 5 taps in one spot (§9.3) |
| `<SD>/battery_log.txt`, `temperature_log.txt` | if the file exists, battery or temperature logging turns on. `battery_check.txt` gets deleted on mount (§9.3) |
| `<SD>/batlog.txt` | written by `batd` if `/usr/bin/batd` exists (it isn't shipped) |
| `<SD>/.temp/{usrlocal_media,most_played,image_cache}.db` | library databases |
| `<SD>/list_cover`, `plane_cover`, `get_cover_switch` | cover-art caches and a debug switch |
| `<SD>/%s_Screenshot` | screenshots |
| `/data/recovery_all` | factory reset on next boot. Containing `recovery_all` wipes everything; anything else keeps the media DB |
| `/data/peq/*.peq`, `/data/game_*.dat`, `/data/theme_id`, `/data/time_zone` | your state |
| `/usr/data/macaddr.txt`, `wpa_supplicant.conf` | Wi-Fi |

---

## 🔓 7. Security, or "the door is open and there's tea inside"

- `root` has an **empty password** (`/etc/shadow`), and there's a root shell on the serial console.
- `busybox` is **setuid root**.
- `thttpd` runs **as root** on port 4399 with CGI on (`**.cgi`) and no chroot, whenever Wi-Fi transfer is active. Maybe don't enable that on airport Wi-Fi. ✈️
- ADB is available (USB gadget, plus an `sa_adb` path inside the player).
- OpenSSL 1.1.1f and curl 7.68.0 date from 2020 and have plenty of known CVEs.
- TIDAL and Qobuz client credentials sit in the binary in plain text.
- No firmware signature, only MD5. Bad for security, great for us.

It's a music player, not a bank. But it's good to know what you're carrying around.

---

## 🛠️ 8. Notes for modders

1. **Rebase on 2.0.** It's the first release that runs the new X1600EN/HN + AIC8800 board with working radios. Old bases = deaf radios on new units.
2. **PEQ is official now** (shelves + peaking + AutoEQ import), so the repo's PEQ enablement patch is probably redundant. Check how many bands the UI exposes before you drop it.
3. `patch_brightness.py` works on 2.0 unchanged. `patch_bt_input.py` needed a fix (§9.5). Any other patch with hard-coded 1.7/1.8b offsets has to be re-located.
4. Replace **all three** boot logos (`logo.jpeg`, `logo1.jpeg`, `logo2.jpeg`). The theme byte in mtd5 decides which one shows up, and they should be 480×800.
5. Flags the code proves will work: `car_mode`, `dac_charge_disable`, `usb_mode`, `tf_music_db_enable`, `tf_image_cache_enable`, `explorer_in_cue_enable`. Flags that do nothing: `double_touch_wakeup`, `car_mode_auto_play`, `dac_feedback`, `volkey_locked`, and `rotation` (§9.2).
6. Keep `ota_compatibility.in` as `hiby_r1_x1600_auto_rf_auto` when repacking.

---

## 🧠 9. Deep dive: what's really hidden in `hiby_player` (Ghidra-verified)

**Confession time.** My first pass at this binary was `strings | grep` plus vibes, and the vibes lied. I called the 24-point game "hidden" (it's in the Games menu). I said `battery_check.txt` was a trigger (the player *deletes* it). I told people to avoid the factory-mode file (it's actually the door to the factory test menu). So I threw all of that out and did it properly.

**Method:**
- Ghidra 12.1.4, headless.
- Ghidra missed about 120 functions, so I found them by scanning for MIPS function prologues (`addiu sp,sp,-N`, `0x27bdXXXX`) and created them by hand.
- 6,920 functions decompiled; every interesting string traced to the code that reads it.
- The project, scripts and exports live in `re/`. For code and rodata, file offset = VA − 0x400000.

### 9.1 Feature flags in `config.json` (`function.fcn0`)

`FUN_00419ee0` parses these into a bitmask at **`DAT_008EDF6C`** (bit *n* = the n-th key), and `FUN_0041c040(bit)` reads them back.

| Bit | Key | What the code actually does when it's 1 |
|---|---|---|
| 0 | dac_to_store | `FUN_0052e840`/`FUN_0052ec40` save and restore settings 0x71/0x33/0x23/0x08/0x5f around USB-DAC mode, so your player setup survives being a DAC |
| 7 | tf_image_cache_enable | `FUN_004305a0` puts the album-art cache DB in `<SD>/.temp` (with `PRAGMA synchronous=OFF`, living dangerously) |
| 8 | tf_music_db_enable | adds **"The song database is stored in TF card"** to *Play settings → Music update mode* (`FUN_004ddec0`). Once on, it needs **>512 MB free** on the card (`FUN_004de2c0`), stores the DB at `<SD>/.temp/usrlocal_media.db`, and 🚀 **raises the library limit from 80,000 to 200,000 songs** (`FUN_00505920`) |
| 9 | explorer_in_cue_enable | adds "Supports song embedded CUE in file browsing" (setting 0x91) to the same page |
| 10 | otg_scan_enable | adds "Scan OTG" (setting 0x92) to the same page |
| 11 | qplay3_enable | allows setting 0x94 (QPlay3 over DLNA). With the bit off, the setting is forced to 0 at boot (`FUN_004191a0`) |

Bonus ghost: `mqa_decoder` ("MQA decoder detection") has a tap handler but is never added to any list. Dead code on the R1. RIP. 🪦

### 9.2 System settings rows (`set_functions.json`)

`FUN_0041c080` builds *System settings* from `z:\set_functions.json`, in file order. Here's the fun bit: **any row name the code knows can be added**, even rows that aren't in the stock file. Just write `"name":1`.

The master list sits in `.data` at `0x8C2FF4` (stride 0x44, `int id; char name[64]`), and `FUN_004dcd20` handles taps.

| Row (id) | Stock | What tapping it really does | Works on R1? |
|---|---|---|---|
| car_mode (12) | 0 | toggles setting 0x72, which is used twice: **resumes playback at boot** (`FUN_00508760`), and when the charger is unplugged it pops up **"Whether to enter the vehicle shutdown"** (`FUN_00482600` → msg 0x104 → `FUN_004ea460`). Real car-stereo behaviour 🚗 | ✅ Yes |
| usb_mode (9) | 0 | opens **"USB device mode": Storage / Audio**. That's internal setting 8 (1 = Storage, 0 = Audio). Audio is **USB DAC mode**; the same switch also lives in the pull-down menu (`FUN_005544a0`) | ✅ Yes |
| dac_charge_disable (10) | 0 | "USB current limited", setting 0x5f. In DAC mode it writes `MaxPower=10`, `charge_control_limit_max=100`, `constant_charge_current_max=0`, so **no charging while it's a DAC** (`FUN_00474000`/`FUN_00474200`/`FUN_0047fe80` → `FUN_00473e00`). Less noise, kinder to your laptop battery | ✅ Yes |
| rotation (28) | 0 | opens *Screen rotation*. `FUN_0047bb20` → `ioctl JZFB_ROTATE` (0° or 180°). `soc_fb.ko` takes the ioctl, then hands it to the panel driver's rotate callback at `lcdc_data+160`… which **`lcd_lg35583.ko` leaves NULL**. The ioctl returns success and nothing happens. Confirmed dead on a real device 😔 | ❌ No (needs a kernel panel driver) |
| car_mode_auto_play (13) | 0 | stores setting 0x73. **Nothing ever reads it** | ❌ No effect |
| dac_feedback (11) | 0 | setting 0x75 → `FUN_00474860`, which is an **empty function**. A button wired to nothing | ❌ No effect |
| volkey_locked (25) | 0 | toggles setting 0x5b (stored inverted). No other reader in `hiby_player` or `sys_server` | ❌ No effect found |
| double_touch_wakeup (22) | 0 | stores setting 0x6f. **Never read**, and the touch driver has no gesture code anyway | ❌ No effect |
| operating_instruction (33) | 0 | opens `track.hiby.com/app/product/guide?...` | 🟡 online manual |
| update_type (1) | absent | the *Music update mode* page, already reachable from Play settings | — duplicate |
| step_record (23) | absent | setting 0x59 → writes `/sys/class/input/input*/stepcnt_enable` for a KX126 accelerometer… which the R1 doesn't have | ❌ No sensor |
| fn_key (37) | absent | opens the `vg_listview_fn_key` page | 🟡 untested |
| wireless_set (35), bluetooth_set (36), stream_media (2), brightness (3), bkl_time (34) | absent | shortcuts to pages that already exist | 🟡 duplicates |

### 9.3 SD-card triggers

| File in the SD root | Where in the code | What happens |
|---|---|---|
| `hiby_linux_factory_mode` | `FUN_00508760` (main activity create) | The start screen becomes `factory` and **the factory test screen opens** (`FUN_00577060` → `hiby_test`): keys, LCD, touch, TF card, charging, Bluetooth, audio channels. It resets the next start to `launcher`, but the file is still there, so it opens again **every boot until you delete it**. On entry it also sets 0x27=1, 0x15=1, 0x7a=0, 0x0e=0 and starts a 3-minute timer |
| `hiby_linux_auto_test` | `FUN_004868a0` | The file only *arms* it. It activates after **5 taps in a row inside one fixed rectangle** on screen, then logs every touch and swipe (`MONKEY` log lines, `autotest_capture`). A touch logger, not a random tapper |
| `battery_log.txt` / `temperature_log.txt` | `FUN_004555a0` (on SD mount) | If the file **exists**, battery or temperature logging switches on (msg 0x19 / 0x1a) |
| `battery_check.txt` | `FUN_004555a0` | **Deleted** on mount. Not a trigger, just tidying up |
| `get_cover_switch` | `FUN_0047ef60` (startup) | creates `<SD>/list_cover` and `<SD>/plane_cover` and flips a cover-art debug flag |

### 9.4 Present in the code, impossible on this hardware

- Darwin FPGA filters (`.flt` files written to `jz-spi0.0/filter_path`). That's R-series DAC magic, not R1.
- Step counter (KX126): no sensor.
- FM radio and recording: no tuner.
- Voice recorder: no dmic driver.
- SPDIF, coax and optical: no jack.
- AirKiss: no `akiss` binary.
- `/data/harmonic.json`: zero code references, a leftover from an older JSON-driven version. 👻 But the **Harmonic Controller itself is real**; see §9.7.

### 9.5 The repo's patch scripts vs V2.0

- **`patch_brightness.py`:** one clean match at file offset `0x6e760` (`sltiu v1,v1,4`, VA `0x46e760`). **Works as is.**
- **`patch_bt_input.py`:** the string and instruction search still works (blocklist at `0x10407c`, menu at `0xb6eb4`). **But** its code-cave finder grabbed `0x38b69a`. That's not 4-byte aligned (MIPS will not run code there), and it overwrote the NUL ending the `z:\layout\` string the UI depends on. Double whammy. 💥
  - **Fixed:** it now leaves the terminator alone and aligns the cave, giving `0x38b69c`. The disassembly checks out: `lui/move/jal 0x4b58e0/addiu` for `volume_sync`, again for `bt_input`, then `j 0x4b6ec4` back home.

### 9.6 "bt_input" is not what we thought

The repo has been calling `patch_bt_input.py` the "Bluetooth controller input unlock". After tracing it: **it's not about game controllers at all.** `bt_input` is the **Bluetooth audio receiver** screen (A2DP sink). Your phone streams to the R1 and the R1 plays it, showing title, artist, album and sample rate (`bt_input_tv_media_*`, `bt_input_tv_sample_rate`). HiBy built it, then hid it on the R1 with a blocklist entry (`FUN_004f7220`). The patch removes that entry and adds a menu row.

What happens when you open it:
1. `FUN_00522080` (screen create) sets setting **0x25 = 1**.
2. The settings setter (`FUN_00417800`, case 0x25) calls `FUN_004702c0("a2dp-sink")`, which sends `BT:A2DPPROFILE:a2dp-sink` and `BT:DISCOVER` to `sys_server`.
3. `sys_server` runs `/usr/bin/bluealsa_profile a2dp-sink`. That kills bluealsa and bt-agent and restarts them as `bluealsa -p a2dp-sink --a2dp-volume` plus `bt-agent -c NoInputNoOutput`.
4. `FUN_0071e1e0` starts the `bt_sink` playback thread.
5. Leaving the screen (`FUN_005210c0`) sets 0x25 = 0 and switches back to `a2dp-source`.

So the whole software chain is there. **On a real device:** the phone sees the R1, tries to connect, and fails. Main suspects:
- The phone cached the R1's old SDP records from when it was source-only, and needs to forget and re-pair while the screen is open.
- HiBy hid it for a reason (unfinished on the R1, or a radio/firmware limitation).
- `btmon` logs from the device will tell which.

### 9.7 The Harmonic Controller: a "tube warmth" knob hiding in the DAC path

"Harmonic Controller" (`Harmonic_control` in `darwin.ini`) is a row in HiBy's **Darwin** menu, next to NOS, Digital Filter and DSD Filter. The chain:

1. Tapping the row (`FUN_004efc60`) opens a slider dialog bound to **setting 0x6b**: 0 = "off", up to 255.
2. The setter (`FUN_00417800` case 0x6b) → `FUN_00474640` (rejects >255) → `FUN_00721aa0`.
3. That writes two fields of the **analog output device object** at `0x9292f0`: `+0xc0` = level, and `+0xc4` = gain = (65536 − level) / 65536.
4. In the analog device's write callback (`0x7255e0`, a label Ghidra never made into a function), if level ≠ 0:

```
for each 32-bit sample s:
    if (s < 0) s = (int)(s * gain);     // only the NEGATIVE half-wave is scaled
```

Shrinking one half of the waveform makes it asymmetric, and asymmetry = **even-order harmonics** (mostly 2nd). That's the classic single-ended-tube "warmth" recipe, done in about 10 MIPS instructions. 🔥

How much? With asymmetry a = level/65536, a sine gets a 2nd harmonic of about a·2/(3π):

| Level | Gain on negative half | 2nd harmonic |
|---|---|---|
| 1 | 0.99998 | ≈ −110 dB |
| 64 | 0.99902 | ≈ −74 dB (0.02%) |
| 255 (max) | 0.99611 | ≈ −62 dB (0.08%) |

It also adds a tiny DC offset (≈ a/π of full scale). Subtle, a seasoning, not a fuzz pedal.

**Real-device verdict: "almost no effect."** That matches the math. HiBy's scale tops out at a −62 dB 2nd harmonic, which is inaudible through IEMs. The path *is* active: the analog device declares 4-byte samples (field `0xd = 4`), so the 32-bit loop fits, and only DSD (`+4 == 2`) skips it. So the mod rescales it. In the writer (`0x721ac4`), `lui v1,0x1` becomes `li v1,2048`, and the 1/65536 load is pointed at a new 1/2048 float in the cave (`0x875b6c`). Gain is now (2048 − L)/2048:

| Level | 2nd harmonic (mod) |
|---|---|
| 32 | ≈ −50 dB (0.33%) |
| 128 | ≈ −38 dB (1.3%) |
| 255 | ≈ −32 dB (2.6%) |

That's single-ended-tube territory at the top of the slider. 🔥

**Round two: still "almost null" at 2048.** I re-traced every early exit in the analog write callback:
- `+184` is a one-shot error log.
- `+4 == 2` is DSD bit-reversal (`FUN_0072cbc0`).
- The loop length is in bytes: `FUN_0072c280` divides by channels × bits / 8 for `WRITEI_FRAMES`.
- Samples are 32-bit, then `>>8` to 24-bit in `FUN_0072cb80`.

None of these skips PCM. So either 2.6% H2 is simply subtle on music (likely), or something only visible at runtime is in the way. The scale is now **512**, so the top of the slider is unmistakable: L=16 ≈ 0.7%, L=64 ≈ 2.6%, L=128 ≈ 5%, L=255 ≈ 10.6% H2.

**On the R1 (stock):** the DSP code is pure software, so it would run. But the Darwin menu can't be opened. The R1's resources have no `vg_listview_darwin` / `dw_*` layout files and no launcher or pull-down entry for it, and the rest of Darwin (NOS, `.flt` filters, `osr_bypass` on `jz-spi0.0`) needs the FPGA the R-series has and the R1 doesn't. Turning harmonics on would mean setting 0x6b some other way (a patched default, or a small new settings row).

**The mod fixes this** with `Old_New_Mods/patch_harmonic.py`:
- The System settings row `operating_instruction` (the online manual, row 0x21) now opens HiBy's own Harmonic Controller slider (`FUN_004efc60`). The branch at `0x4dd2f0` gets the same 5-instruction call the brightness row uses.
- The row is relabelled in all 13 languages, using each language's own `Harmonic_control` translation from `darwin.ini`.
- The level is saved in `/data/user.ini` like every other setting, but stock never re-applies it at boot: the analog device init (which resets it to off) runs before settings load. So the single `jal FUN_00418ec0` (settings load) at `0x47f0ec` now goes to an 11-instruction stub that loads the settings, then calls `FUN_00474640(get(0x6b))`.
- The stub lives in the 1,216 bytes of zero padding at the end of the executable segment (`0x875b40`), and the first `PT_LOAD`'s size is extended to cover it.
- Every site is found by instruction signature, and the script is idempotent.


---

## 🧪 10. The V2.0 mod build

All of the above, turned into a flashable firmware.

| What | Change |
|---|---|
| Boot | Bidhata boot menu + new 480×800 "HiBy R1 · with Bidhata's" logos for all three themes |
| `hiby_player` | brightness down to 1, the hidden **Bluetooth receiver** screen unlocked (with the fixed cave; see §9.6) |
| Play settings | SD-card song DB (200k library), SD-card art cache, CUE + OTG scan rows, settings saved around DAC mode |
| System settings | **Harmonic Controller** (0–255 slider, §9.7), plus rows for USB device mode (DAC), USB current limited and car mode. All still off until you turn them on. (Rotation was dropped after device testing proved it dead.) |
| Audio | no volume warning popup **and no 50/100 headphone cap** (`lock_vol` 50 → 100, see below), last MDB/LDB volume step 12 → 0 |
| Storage | `noatime` on root and userdata, SD read-ahead 2048 KB, slower cache reclaim (via a new `S91_bidhata_tune` init script, because the boot menu skips `hiby_player.sh`) |

**The volume-lock gotcha (found by a real-device test):** setting **0x7a** ("volume locked", default 1) makes `FUN_00416de0` cap the max volume at `lock_vol` (headset **50**). Stock firmware only clears 0x7a when you accept the "Extended exposure to high volume…" dialog. With `vol_warn_enable: 0` that dialog never appears, so the old mod left you **stuck at 50 with "Set maximum volume reached"**. Setting `lock_vol` to 100 turns the lock into a no-op.

**Round two:** the popup still appeared. There's a *second* trigger. At every boot `FUN_004191a0` sets 0x77 = (0x7a == 1). Then `FUN_00536180`, called from the volume handlers `FUN_0043ae40`/`FUN_0043af40`, shows the hearing popup whenever 0x77 == 1 and the new volume is above `warn_vol` (headset 34). **That path never checks `vol_warn_enable`.** Fix: `warn_vol` → 100, which volume can never exceed. The other readers of the warn level only turn the volume number red. Both changes are config-only, no binary patch.

If you flashed an earlier build, raise *Play settings → Max volume* back to 100 once.

**Left out on purpose:**
- the five do-nothing toggles (rotation joined the club after a real-device test)
- the tiny font, which would eat CJK glyphs
- the old Game Boy launcher, which the Bidhata menu replaces

**Checked:** the hash chain and layout match stock, and the rootfs differs from stock *only* in the intended files. Ownership, modes and even CRLF line endings are kept. The kernel is byte-identical to stock.

**Real-device scoreboard** (older Broadcom-board R1, 2026-10-17):

| Feature | Result |
|---|---|
| Flashing, boot logos, Bidhata menu | ✅ works |
| Full volume range, no hearing popup | ✅ works (after the `lock_vol` + `warn_vol` fixes) |
| Harmonic Controller slider | ✅ appears and saves. 🟡 Effect still too faint to hear at scales 65536 and 2048; scale 512 build plus sine-tone test pending |
| Bluetooth receiver (`bt_input`) | 🟡 screen opens, phone sees the R1, connection fails. Needs `btmon` logs |
| Everything else | 🟡 untested. Flash it, poke everything, report back 🫡 |

---

*Built by a curious reverse-engineering addict who can't leave firmware alone. If something here is wrong, the code is the source of truth. Tell me and I'll go back into Ghidra.* ☕
