# GB-Emu: Game Boy Emulator for HiBy R1

Game Boy (DMG) emulator for HiBy R1 portable music player. Boot launcher included — pick game or hand device back to music player.

## Features

- Full LR35902 CPU emulation (256 main opcodes + 256 CB-prefixed opcodes)
- **Game Boy Color**: RGB555 palettes, both VRAM banks, banked WRAM, tile
  attributes, HDMA/GDMA, double-speed mode
- Selectable shades for plain DMG games: green, grey, pocket, amber
- Cycle-interleaved memory access: hardware advances during instruction, not
  after — timer and PPU reads mid-instruction return hardware-accurate values
- PPU with background, window, sprite rendering
- MBC1, MBC2, MBC3 (with RTC), MBC5 memory bank controllers
- All four audio channels: two squares, wave, noise
- Battery-backed saves, written next to ROM as `<rom>.sav`
- Framebuffer video output (16 and 32 bpp)
- ALSA audio output on builds that have it
- Boot launcher: pick ROM from SD card, or start music player

### Test ROM status

Verified with [Blargg's test ROMs](https://github.com/retrio/gb-test-roms) via
headless runner:

| ROM | Result |
|-----|--------|
| `cpu_instrs` | Passes all 11 tests |
| `instr_timing` | Passes |
| `mem_timing` | Passes all 3 tests |
| `mem_timing-2` | Passes all 3 tests |
| `halt_bug` | **Fails** — HALT bug not emulated |
| `cgb-acid2` | Pixel-perfect against reference image |

## Building

### Native build (x86_64, for testing)
```bash
make              # gb-emu, with ALSA
make gb-headless  # headless test runner, no framebuffer or audio
```

### Cross-compile for HiBy R1 (MIPS32-le, static musl)
```bash
make clean
make CROSS=/opt/mipsel-linux-musl-cross/bin/mipsel-linux-musl- STATIC=1
```
Produces fully static `gb-emu`. Device has no mipsel `libasound` — build has no audio.

## Quick Deploy (via ADB)

```bash
# 1. Cross-compile
make clean
make CROSS=/opt/mipsel-linux-musl-cross/bin/mipsel-linux-musl- STATIC=1

# 2. Push binary and scripts to the device
make deploy

# 3. Put ROMs on the SD card, in a games/ folder at its root
adb push game.gb /data/mnt/sd_0/games/

# 4. Open the launcher now, without changing the boot setup
adb shell /usr/bin/gb-toggle.sh launch-emu
```

Without ADB: put ROMs on SD card directly — make `games` folder at card's root, copy `.gb` / `.gbc` files into it.

### Where the SD card lives

Stock firmware mounts card at **`/data/mnt/sd_0`** (`/data` symlink to `/usr/data`), not `/mnt/sd_0`. Nothing mounts it at boot: `sys_server` handles mounts, only when asked — only `hiby_player` ever asks for `sd_0`, and launcher runs in place of it. `gb-launcher.sh`
therefore mounts card itself before starting emulator, trying
`mmcblk1p1`, `mmcblk1`, `mmcblk0p1`, `mmcblk0` in turn as vfat/exfat. Music player remounts it on start — harmless to it.

## Building a flashable firmware image

Emulator can be baked into `.upt` so flashed device boots straight into launcher, no ADB needed after.

Quickest route, handles future firmware releases too:

```bash
./gb-emu/patch/build-firmware.sh r1_new.upt r1_gb.upt
```

Unpacks stock firmware, installs launcher, repacks it. See
`gb-emu/patch/README.md` for what changes, how to undo, what happens
if HiBy rearranges boot scripts.

By hand instead, from project root (one level above `gb-emu/`):

```bash
# 1. Cross-compile, and install into the unpacked rootfs
cd gb-emu
make clean
make CROSS=/opt/mipsel-linux-musl-cross/bin/mipsel-linux-musl- STATIC=1
cp gb-emu           ../squashfs-root/usr/bin/gb-emu
cp scripts/gb-launcher.sh ../squashfs-root/usr/bin/
cp scripts/gb-toggle.sh   ../squashfs-root/usr/bin/
chmod 755 ../squashfs-root/usr/bin/gb-emu ../squashfs-root/usr/bin/gb-*.sh

# 2. Point the boot script at the launcher
sed -i 's|^PL01=.*|PL01=/usr/bin/gb-launcher.sh|' \
    ../squashfs-root/etc/init.d/S92_03_start_music_player

# 3. Repack
cd ..
./repack.sh          # writes r1_repacked.upt
```

Flash `r1_repacked.upt` same way as any stock firmware update.

Note: `/usr/data` is a **separate UBIFS partition** mounted at boot — anything placed there inside image hidden once mount happens. Binary must live on rootfs, at `/usr/bin/gb-emu`. Launcher still
prefers `/usr/data/gb-emu` when it exists — makes it possible to
test new build over ADB without reflashing:

```bash
adb push gb-emu /usr/data/gb-emu && adb shell chmod +x /usr/data/gb-emu
```

## The boot launcher

Device flashed with image above shows launcher every boot. Row order:

1. **HIBY PLAYER**
2. **GAMES** (divider — not selectable, cursor skips over it)
3. **PALETTE**
4. every ROM found on SD card
5. **SHUTDOWN**
6. **FIRMWARE UPDATE (SD)**
7. **FACTORY RESET**
8. **STRIP FILE ART**
9. **STRIP ALBUM ART**

Selecting PALETTE cycles four shades used by plain DMG games (green, grey,
pocket, amber); choice kept in `/usr/data/gb_palette`, survives
reboots. Game Boy Color titles ignore it, use own colours.
Volume Up/Down moves cursor, Next Track selects. Picking HIBY PLAYER starts stock HiBy
player exactly as firmware normally would. Picking ROM runs it; quitting
game (Power) returns to menu — another game startable without rebooting.

**Idle timeout:** 5 seconds with no input auto-selects HIBY PLAYER — a
countdown ("STARTING PLAYER IN Ns...") shows in the footer the whole time
and resets the instant any key or tap arrives, so a device sitting at the
menu (or one that just rebooted with nobody in front of it) doesn't stay
stuck there.

All five entries below PALETTE/the ROM list open a confirm screen first
(defaults to CANCEL; Power always backs out) — none of them are undoable
from the menu once picked:
- **SHUTDOWN** — runs `poweroff`.
- **FIRMWARE UPDATE (SD)** — runs `bootmode.sh Recovery` and reboots into
  the updater, same path `Settings → Firmware Update → Via SD-card` uses on
  stock firmware. Needs a `.upt` file at the SD card's root.
- **FACTORY RESET** — writes `recovery_all` to `/data/recovery_all` and
  reboots; `recovery_all.sh` wipes `/data` on the next boot. Same mechanism
  stock firmware's own factory reset uses.
- **STRIP FILE ART** — runs `strip_art_all.sh` over the SD card, then
  returns to the menu (unlike the three above, which never come back).
  Removes embedded FLAC/MP3 art; see "Stripping Embedded Album Art" in the
  top-level README.
- **STRIP ALBUM ART** — runs `remove_folder_art.sh -f`, then returns to the
  menu. Deletes standalone cover files (`folder.jpg`, `cover.png`, ...).

To boot straight to music player, skip menu:

```bash
adb shell /usr/bin/gb-toggle.sh player
adb reboot

# and to bring the menu back
adb shell /usr/bin/gb-toggle.sh emu
adb reboot
```

### How it works

Stock boot chain: `inittab` → `rcS` → `/etc/init.d/S92_03_start_music_player`
→ `hiby_player.sh`. Firmware image changes that init script's `PL01=` line to
point at `gb-launcher.sh` instead.

Rootfs is read-only squashfs — nothing can rewrite that init script on running device. `gb-toggle.sh` switches modes via flag file on
writable `/usr/data` partition: `/usr/data/gb_boot_mode` containing `player`
makes launcher skip menu, start music player straight away.

Emulator never starts music player itself: exits with status 10 to
request it, `gb-launcher.sh` does the starting. Any other exit — crash, no
screen, no working buttons — also falls through to music player, so broken
emulator can't leave device stuck. `hiby_player.sh` untouched in
image — stock path always recoverable.

## Controls

### Launcher

R1 has only three buttons — volume rocker, Next Track, Power. No Play/Pause key on this hardware, so **Next Track confirms**.

| Control | Action |
|---------|--------|
| Vol Up | Move up |
| Vol Down | Move down |
| Next Track | Select |
| Power | Quit to music player |
| Tap a row | Select it directly |

### In game

Three buttons can't cover a Game Boy pad — game sits in upper part of
screen, on-screen pad drawn below. Physical keys double up
on most-used inputs, two sources combined: holding Vol Up while
tapping A presses both.

| Control | Game Boy Key |
|---------|-------------|
| Vol Up | D-pad Up |
| Vol Down | D-pad Down |
| Next Track | A button |
| On-screen D-pad | Up / Down / Left / Right |
| On-screen A, B | A, B |
| On-screen START, SELECT | Start, Select |
| Power | Quit to launcher |

## Running a ROM directly

Passing a path skips launcher — how headless tests and manual
runs work:

```bash
./gb-emu /data/mnt/sd_0/games/tetris.gb     # on device
./gb-headless rom.gb 600 --screen      # host: run 600 frames, print the screen
```

`gb-headless` echoes serial port to stdout — how test ROMs report
results.

## File Structure

```
gb-emu/
├── include/            # Header files
├── src/
│   ├── main.c          # Entry point and launcher loop
│   ├── menu.c          # ROM launcher
│   ├── font.c          # 5x7 bitmap font
│   ├── cpu.c           # LR35902 CPU (all opcodes)
│   ├── ppu.c           # Pixel Processing Unit
│   ├── mmu.c           # Memory Management Unit
│   ├── apu.c           # Audio Processing Unit
│   ├── platform.c      # Framebuffer/ALSA/input, on-screen pad
│   ├── headless.c      # Headless test runner
│   └── bootmenu.c      # Unused standalone mode selector, superseded by menu.c
├── scripts/
│   ├── gb-launcher.sh  # Boot script: launcher, falling back to the player
│   └── gb-toggle.sh    # Boot-mode switch: emu/player/status/launch
├── patch/              # Apply to any firmware release
│   ├── gb-patch.sh     # install / --revert / --check against a rootfs
│   ├── build-firmware.sh  # stock .upt in, patched .upt out
│   └── payload/        # prebuilt MIPS binary and scripts
├── Makefile
└── README.md
```

## Deployment Files

| File | Device Path | Purpose |
|------|-------------|---------|
| `gb-emu` | `/usr/bin/gb-emu` | Emulator and launcher binary (in the firmware image) |
| `gb-launcher.sh` | `/usr/bin/gb-launcher.sh` | Boot script |
| `gb-toggle.sh` | `/usr/bin/gb-toggle.sh` | Boot-mode switch |

## Technical Details

- **Display**: 160x144 native, scaled to fit, centred on panel
- **Audio**: 44100Hz, 16-bit mono via ALSA where available
- **Input**: Linux evdev (`/dev/input/event*`)
- **Video**: Direct framebuffer mmap (`/dev/fb0`), 16 or 32 bpp
- **Target**: Ingenic X1600 (MIPS32-le)

## Limitations

- CGB colour correction not applied: palettes scaled straight to 8 bits
  per channel — vivid on modern panel, not faithful to
  console's dim screen.
- HALT bug not emulated, so `halt_bug.gb` fails. Games rarely depend on it.
- OAM DMA completes instantly rather than over its 160-cycle transfer window.
- No save states.
- No serial link cable; writes echoed for test ROMs, read back as 0xFF.
- Untested on real hardware this session — no device attached, all
  verification done on host with headless runner and offscreen
  rendering.

## Credits

- **Emulator core** (CPU/PPU/MMU/APU) is based on [jgilchrist/gbemu](https://github.com/jgilchrist/gbemu).
- Boot launcher, R1 framebuffer/input platform layer, palette selector, boot-mode toggle, firmware patching toolchain, and the SHUTDOWN/FIRMWARE UPDATE/FACTORY RESET/STRIP ART menu additions are specific to this port.