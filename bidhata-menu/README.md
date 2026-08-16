# 🧙 Bidhata Menu — Boot Menu Launcher for HiBy R1

> Your HiBy R1's new front door. A tiny, nerdy, framebuffer-drawn menu that greets you at every boot, lets you pick what to do, and then gets out of the way. Think GRUB, but for a pocket music player, and with more `killall hiby_player`.

Built for the **HiBy R1** (Ingenic X1600, MIPS32r2, 480×800, Linux). Originally a Game Boy emulator's launcher; the emu moved out and the menu stayed -- now it's a self-contained, config-driven boot utility that fits in one folder.

**Upload just `bidhata-menu/` to your report. That's it. Everything downstream (binary, scripts, compress, payload) comes from here.**

---

## ✨ What It Does

- **Shows a menu every boot** -- pick MUSIC PLAYER, utilities, or your own custom entries
- **Hands back to stock `hiby_player` untouched** -- no forks, no patches to the player itself
- **Auto-boots to player after 5s idle** -- leave it alone, it orders for you
- **Config-driven** -- add menu rows / submenus without touching C
- **Optional cover-art compressor** -- shrink bloated FLAC/MP3 art to 480px, save SD + RAM
- **Reverts cleanly** -- one command restores stock boot, forever

---

## 🗂️ What's Inside `bidhata-menu`

```
bidhata-menu/
├── config/
│   └── bidhata-menu.conf.default    # Shipped menu layout (edit to customize)
├── include/
│   ├── menu.h / menu_config.h / platform.h / font.h / types.h
│   └── ...                          # Headers (nerdy-commented, delightful)
├── src/
│   ├── main.c                       # Entry + signal + exit-code dance (42!)
│   ├── menu.c                       # Drawer + poller + confirm gate + idle timer
│   ├── platform.c                   # /dev/fb0 mmap + /dev/input evdev
│   ├── menu_config.c                # Pipe-delimited parser (CSV's chaotic cousin)
│   ├── font.c                       # 5×7 bitmap, 480×800 love
│   └── bootmenu.c                   # Legacy stub (unused, here for archaeology)
├── scripts/
│   ├── bidhata-launcher.sh          # The bouncer -- shows menu, starts player
│   └── bidhata-toggle.sh            # Light switch: menu / player / status
├── tools/
│   └── compress-art/                # Self-contained shrinker (stb_image family)
│       ├── compress_art.c           # The JPEG diet plan
│       ├── compress_art_all.sh      # Batch: embedded FLAC/MP3 art
│       ├── compress_folder_art.sh   # Batch: folder.jpg / cover.png ...
│       ├── stb_image*.h             # Public-domain, no deps
│       └── Makefile                 # Cross or native
├── patch/
│   ├── bidhata-patch.sh             # Firmware surgeon (install / check / revert)
│   ├── build-firmware.sh            # One-liner: stock .upt -> patched .upt
│   ├── payload/                     # Prebuilt MIPS files (no toolchain needed)
│   └── README.md
├── Makefile                         # Top-level: build menu (+ optionally compress_art)
└── README.md                        # You are here. Hi. 👋
```

Everything you need is under this tree. Build `bidhata-menu` and `compress_art` from here, patch from here, flash from here.

---

## 🔧 Building

### Native (x86_64, for testing on your laptop -- USB keyboard works as input)

```bash
cd bidhata-menu
make                  # builds ./bidhata-menu
make compress_art     # builds ./tools/compress-art/compress_art
```

### Cross for HiBy R1 (MIPS32-le, static musl -- drop-in for device)

```bash
cd bidhata-menu
make clean
make CROSS=/opt/mipsel-linux-musl-cross/bin/mipsel-linux-musl- STATIC=1
make compress_art CROSS=/opt/mipsel-linux-musl-cross/bin/mipsel-linux-musl- STATIC=1
```

Produces:
- `bidhata-menu` -- static MIPS32r2 ELF (no deps, no musl on device needed)
- `tools/compress-art/compress_art` -- static MIPS32r2 ELF

The payload under `patch/payload/` already carries prebuilts. If you rebuild, the patch prefers your fresh binary automatically -- just re-run it.

---

## 📋 Configuring the Menu -- Very Easy

The whole menu is a text file: one line per row, `|` separated. No JSON, no XML. Editors love it, MIPS loves it.

```
LABEL|COLOR|ACTION|PARAM|CONFIRM_TEXT|GROUP
```

- **LABEL** -- text shown, e.g. `HIBY PLAYER`, `MY GAME`
- **COLOR** -- palette name `PLAYER|SHUTDOWN|FW_UPDATE|DANGER|STRIP|TEXT` or raw `0xRRGGBB`
- **ACTION** -- what happens when you pick it:
  - `run` with `PARAM=player` -- sentinel: exit 10, launcher starts `hiby_player`
  - `run` with `PARAM=/path/to/bin [args]` -- launch that binary (Rockbox, retro emu, your shell -- go wild)
  - `exec` with `PARAM=shell command` -- run in-process, return to menu (what compress + `adbon` do)
  - `submenu` with `PARAM=GROUP_NAME` -- open that submenu screen
  - `shutdown` / `fw_update` / `factory_reset` -- built-ins (handle reboot sequencing safely)
- **CONFIRM_TEXT** -- non-empty => "Are you sure?" gate. Empty => no gate.
- **GROUP** -- empty => main screen. Non-empty => only inside the submenu named `GROUP`. Flat file, flat logic. No trees, no drama.

### Default layout (shipped)

**Main:**

1. **HIBY PLAYER** -- sentinel, always there even if config is broken
2. **UTILITIES** -- submenu
3. **DANGER ZONE** -- submenu

**UTILITIES:**

- `SHUTDOWN`, `FIRMWARE UPDATE (SD)`, `COMPRESS FILE ART`, `COMPRESS ALBUM ART`, `ENABLE ADB`

**DANGER ZONE:**

- `FACTORY RESET` (its own scary submenu, as it should be)

### Add a menu item (no rebuild needed)

Top-level:

```
MY PLAYER|PLAYER|run|/usr/bin/my-player|
```

Inside UTILITIES:

```
MY TOOL|STRIP|exec|my_cmd||UTILITIES
```

### Add a submenu

```
MY TOOLS|STRIP|submenu|MYTOOLS|
MY TOOL ONE|STRIP|exec|echo hello||MYTOOLS|
MY TOOL TWO|STRIP|exec|do_thing --fast||MYTOOLS|
```

That's it. Delete a line => gone. Reorder lines => reorder menu.

### Where configs live (priority order)

1. `/usr/data/bidhata-menu.conf` -- writable, `adb push` friendly, live without reflash
2. `/usr/bin/bidhata-menu.conf` -- baked into image (`bidhata-patch.sh` installs the default)
3. Hardcoded fallback in `src/menu_config.c` -- menu can never be empty

Live edit (no reflash):

```bash
adb push my.conf /usr/data/bidhata-menu.conf
adb shell /usr/bin/bidhata-toggle.sh launch-menu   # test now
```

---

## 🎨 Optional: Compress Utilities -- On or Off with One Flag

Embedded FLAC/MP3 cover art can be 2000px+ (HiBy still decodes it on a 480px screen). The shrinker resizes to 480, re-encodes as JPEG, saves RAM + SD + scan time. It's completely optional.

**Inside `bidhata-menu`:** `tools/compress-art/compress_art` + two batch shells. Everything there is self-contained; the top-level `tools/compress-art/` is just a legacy mirror now.

| How | What happens |
|-----|--------------|
| **Default (on)** | Patch installs `compress_art` to `/usr/bin/compress_art` + scripts + two menu rows. Just flash. |
| **`BIDHATA_COMPRESS=0 --no-compress`** | Omit binary + strip compress rows from config. Menu ships without them. |
| **Edit config** | Delete/comment the two `COMPRESS` lines -- rows vanish, binary stays but unused. |
| **No binary but rows present** | Rows show, command fails gracefully, warns how to build. |

Examples:

```bash
./bidhata-menu/patch/bidhata-patch.sh squashfs-root                       # with compress (default)
BIDHATA_COMPRESS=0 ./bidhata-menu/patch/bidhata-patch.sh squashfs-root     # without
./bidhata-menu/patch/bidhata-patch.sh --no-compress squashfs-root          # same, CLI flag

# Build compress for device:
make -C bidhata-menu/tools/compress-art CROSS=/opt/mipsel-linux-musl-cross/bin/mipsel-linux-musl- STATIC=1
```

On device (from menu or ADB):

```bash
# Via menu: UTILITIES -> COMPRESS FILE ART / COMPRESS ALBUM ART
# Via ADB:
adb shell compress_art_all.sh            # embedded art, whole SD
adb shell compress_folder_art.sh -f      # standalone folder art
```

Placement after patch: `compress_art` → `/usr/bin/compress_art`, shells → `/usr/bin/compress_art_all.sh` + `/usr/bin/compress_folder_art.sh`.

---

## 🚀 Quick Deploy (ADB, no flash)

For iteration without reflashing -- rootfs is squashfs, `/usr/data` shadows it:

```bash
cd bidhata-menu
make clean
make CROSS=/opt/mipsel-linux-musl-cross/bin/mipsel-linux-musl- STATIC=1
make deploy                       # pushes to /usr/data/bidhata-menu
adb shell /usr/bin/bidhata-toggle.sh launch-menu   # open now
```

Boot toggle:

```bash
adb shell /usr/bin/bidhata-toggle.sh player   # skip menu next boot
adb shell /usr/bin/bidhata-toggle.sh menu     # menu every boot (default)
adb shell /usr/bin/bidhata-toggle.sh status   # what's installed + mode
adb reboot
```

SD lives at `/data/mnt/sd_0` (not `/mnt/sd_0`); launcher mounts it itself before menu starts. Player remounts on launch -- harmless to leave mounted.

---

## 💿 Building a Flashable `.upt`

### One command (isolated work dir, leaves tree untouched)

```bash
./bidhata-menu/patch/build-firmware.sh r1_stock.upt r1_bidhata.upt
# Flash r1_bidhata.upt like any HiBy update: SD root -> Settings -> Firmware Update -> Via SD-card
```

### By hand (inspectable tree)

```bash
./unpack.sh r1_stock.upt                                 # from project root
./bidhata-menu/patch/bidhata-patch.sh squashfs-root     # install
./repack.sh                                              # writes r1_repacked.upt
# optional: --no-compress or BIDHATA_COMPRESS=0 to omit compress
```

**What changes:** 4-7 files + one init edit, nothing else. `hiby_player.sh` untouched.

| Change | Path |
|--------|------|
| Menu binary | `/usr/bin/bidhata-menu` |
| Launcher | `/usr/bin/bidhata-launcher.sh` |
| Toggle | `/usr/bin/bidhata-toggle.sh` |
| Menu config | `/usr/bin/bidhata-menu.conf` |
| Compress (optional) | `/usr/bin/compress_art` + two `.sh` |
| Boot redirect | `/etc/init.d/S9*_start_music_player` (`PL01=.../bidhata-launcher.sh`) |

Revert:

```bash
./bidhata-menu/patch/bidhata-patch.sh --check  squashfs-root
./bidhata-menu/patch/bidhata-patch.sh --revert squashfs-root   # restores stock, removes added files
```

`--revert` uses a hidden dotfile backup (`.S92_03_....bidhata-orig`) so `rcS`'s `S*` glob never double-starts the player -- the old visible-backup race is gone forever. You're welcome, future you.

---

## 🎮 Controls

R1 has three physical buttons + touch. No Play/Pause, so Next Track does the honors.

| Control | Action |
|---------|--------|
| Vol Up | Move up |
| Vol Down | Move down |
| Next Track | Select / Confirm |
| Power | Back (submenu) / Quit to player (main) |
| Tap a row | Select it directly |

Details: confirm gates start on **CANCEL** (Power always bails). Idle 5s auto-picks **HIBY PLAYER** with a countdown in footer -- freezes the moment you scroll (browsing shouldn't race the clock), re-arms when you enter/leave a submenu or finish a compress.

---

## 🧠 How It Works (60-second version)

Stock: `inittab -> rcS -> S92_03_start_music_player (PL01=hiby_player.sh) -> hiby_player.sh`

Patched: `PL01=bidhata-launcher.sh`. Launcher mounts SD, kills any stray `hiby_player` (pre-flight), runs `bidhata-menu`. Exit codes:

- `10` -- sentinel `player` picked: launcher `exec`s `hiby_player`
- `42` -- other `run` picked: launcher execs what `bidhata-menu` wrote to `/usr/data/bidhata_exec_target`
- anything else / crash / no fb / no input -- fall through to `hiby_player` so device never bricks

`/usr/data/bidhata_boot_mode` = `player` => skip menu. Flag lives on writable UBIFS, rootfs stays pristine.

---

## 📦 Deployment Recap (for your report)

**Upload only `bidhata-menu/`.** That's the deliverable. Everything else derives from it:

- Build both binaries from `bidhata-menu/`
- Patch any stock `.upt` via `bidhata-menu/patch/`
- Toggle compress with one env/flag
- Customize menu by editing one config file, no C rebuild

Bins land where they must:

- `/usr/bin/bidhata-menu` (menu), `/usr/bin/compress_art` + shells (optional), all from `bidhata-menu/patch/payload/` or your fresh build -- exactly where `squashfs-root/usr/bin/` expects them.

---

## 📝 Technical Bits

- **Input:** Linux evdev (`/dev/input/event*`, 0..7 scanned, `O_NONBLOCK`, queued)
- **Video:** raw `/dev/fb0` mmap, 16/32 bpp, `memset` clear + clipped rects
- **Targets:** Ingenic X1600, MIPS32r2, static musl (no runtime deps)
- **Config:** 32 items max, 64/128 char caps, safe truncation with `..`
- **Idle:** 50ms poll, 5s timeout, freeze on scroll, 1s repaint tick

---

## 🚧 Known Limits (honest ones)

- Patch's `hiby_player.sh` detection is hardcoded for HiBy-family firmware. Different vendor => needs making configurable.
- Launcher's `run` dispatch uses stock `LD_LIBRARY_PATH`. Works for Rockbox (stock `libasound.so.2` wins), but a future target needing its own libs would need per-target config plumbing (not built -- deliberately out-of-scope).
- Submenus are one level deep in testing. Config *can* point a submenu row at another group's name, but nested `submenu` inside a submenu hasn't been torture-tested.

---

*Made with 💙, stb_image, and an unreasonable fondness for exit code 42. Don't Panic.*
