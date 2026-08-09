# GB-Emu: Game Boy Emulator for HiBy R1

A Game Boy (DMG) emulator for the HiBy R1 portable music player, with a boot
launcher for picking a game or handing the device back to the music player.

## Features

- Full LR35902 CPU emulation (256 main opcodes + 256 CB-prefixed opcodes)
- **Game Boy Color**: RGB555 palettes, both VRAM banks, banked WRAM, tile
  attributes, HDMA/GDMA, and double-speed mode
- Selectable shades for plain DMG games: green, grey, pocket or amber
- Cycle-interleaved memory access: hardware advances during an instruction, not
  after it, so timer and PPU reads mid-instruction return hardware-accurate values
- PPU with background, window, and sprite rendering
- MBC1, MBC2, MBC3 (with RTC), MBC5 memory bank controllers
- All four audio channels: two squares, wave, and noise
- Battery-backed saves, written next to the ROM as `<rom>.sav`
- Framebuffer video output (16 and 32 bpp)
- ALSA audio output on builds that have it
- Boot launcher: pick a ROM from the SD card, or start the music player

### Test ROM status

Verified with [Blargg's test ROMs](https://github.com/retrio/gb-test-roms) via
the headless runner:

| ROM | Result |
|-----|--------|
| `cpu_instrs` | Passes all 11 tests |
| `instr_timing` | Passes |
| `mem_timing` | Passes all 3 tests |
| `mem_timing-2` | Passes all 3 tests |
| `halt_bug` | **Fails** — the HALT bug is not emulated |
| `cgb-acid2` | Pixel-perfect against the reference image |

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
Produces a fully static `gb-emu`. The device has no mipsel `libasound`, so this
build has no audio.

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

Without ADB, put the ROMs on the SD card directly: make a `games` folder at the
card's root and copy `.gb` / `.gbc` files into it.

### Where the SD card lives

The stock firmware mounts the card at **`/data/mnt/sd_0`** (`/data` is a symlink
to `/usr/data`), not `/mnt/sd_0`. Nothing mounts it at boot: `sys_server` carries
out mounts, but only when something asks, and the only thing that ever asks for
`sd_0` is `hiby_player` — which the launcher runs in place of. `gb-launcher.sh`
therefore mounts the card itself before starting the emulator, trying
`mmcblk1p1`, `mmcblk1`, `mmcblk0p1` and `mmcblk0` in turn as vfat/exfat. The
music player remounts it when it starts, so this is harmless to it.

## Building a flashable firmware image

The emulator can be baked into a `.upt` so a flashed device boots straight into
the launcher, with no ADB needed afterwards.

The quickest route, which also handles future firmware releases:

```bash
./gb-emu/patch/build-firmware.sh r1_new.upt r1_gb.upt
```

That unpacks the stock firmware, installs the launcher and repacks it. See
`gb-emu/patch/README.md` for what it changes, how to undo it, and what happens
if HiBy rearranges the boot scripts.

To do it by hand instead, from the project root (one level above `gb-emu/`):

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

Flash `r1_repacked.upt` the same way as any stock firmware update.

Note that `/usr/data` is a **separate UBIFS partition** mounted at boot, so
anything placed there inside the image is hidden once that mount happens. The
binary has to live on the rootfs, at `/usr/bin/gb-emu`. The launcher still
prefers `/usr/data/gb-emu` when it exists, which is what makes it possible to
test a new build over ADB without reflashing:

```bash
adb push gb-emu /usr/data/gb-emu && adb shell chmod +x /usr/data/gb-emu
```

## The boot launcher

A device flashed with the image above shows the launcher at every boot. It lists
**MUSIC PLAYER** first, then **PALETTE**, then every ROM found on the SD card.
Selecting PALETTE cycles the four shades used by plain DMG games (green, grey,
pocket, amber); the choice is kept in `/usr/data/gb_palette` and survives
reboots, and Game Boy Color titles ignore it in favour of their own colours.
Volume Up/Down moves
the cursor, Play/Pause selects. Picking the music player starts the stock HiBy
player exactly as the firmware normally would. Picking a ROM runs it; quitting
the game (Power) returns to the menu, so another game can be started without
rebooting.

To boot straight to the music player and skip the menu:

```bash
adb shell /usr/bin/gb-toggle.sh player
adb reboot

# and to bring the menu back
adb shell /usr/bin/gb-toggle.sh emu
adb reboot
```

### How it works

The stock boot chain is `inittab` → `rcS` → `/etc/init.d/S92_03_start_music_player`
→ `hiby_player.sh`. The firmware image changes that init script's `PL01=` line to
point at `gb-launcher.sh` instead.

The rootfs is read-only squashfs, so nothing can rewrite that init script on a
running device. `gb-toggle.sh` therefore switches modes with a flag file on the
writable `/usr/data` partition: `/usr/data/gb_boot_mode` containing `player`
makes the launcher skip the menu and start the music player straight away.

The emulator never starts the music player itself: it exits with status 10 to
ask for it, and `gb-launcher.sh` does the starting. Any other exit — a crash, no
screen, no working buttons — also falls through to the music player, so a broken
emulator cannot leave the device stuck. `hiby_player.sh` is left untouched in the
image, so the stock path is always recoverable.

## Controls

### Launcher

The R1 has only three buttons — a volume rocker, Next Track and Power. There is
no Play/Pause key on this hardware, so **Next Track confirms**.

| Control | Action |
|---------|--------|
| Vol Up | Move up |
| Vol Down | Move down |
| Next Track | Select |
| Power | Quit to music player |
| Tap a row | Select it directly |

### In game

Three buttons cannot cover a Game Boy pad, so the game sits in the upper part of
the screen and an on-screen pad is drawn below it. The physical keys double up
on the most-used inputs, and the two sources are combined: holding Vol Up while
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

Passing a path skips the launcher, which is how the headless tests and manual
runs work:

```bash
./gb-emu /data/mnt/sd_0/games/tetris.gb     # on device
./gb-headless rom.gb 600 --screen      # host: run 600 frames, print the screen
```

`gb-headless` echoes the serial port to stdout, which is how test ROMs report
their results.

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

- **Display**: 160x144 native, scaled to fit and centred on the panel
- **Audio**: 44100Hz, 16-bit mono via ALSA where available
- **Input**: Linux evdev (`/dev/input/event*`)
- **Video**: Direct framebuffer mmap (`/dev/fb0`), 16 or 32 bpp
- **Target**: Ingenic X1600 (MIPS32-le)

## Limitations

- CGB colour correction is not applied: palettes are scaled straight to 8 bits
  per channel, which is vivid on a modern panel rather than faithful to the
  console's dim screen.
- The HALT bug is not emulated, so `halt_bug.gb` fails. Games rarely depend on it.
- OAM DMA completes instantly rather than over its 160-cycle transfer window.
- No save states.
- No serial link cable; writes are echoed for test ROMs and read back as 0xFF.
- Untested on real hardware in this session — no device was attached, so all
  verification was done on the host with the headless runner and offscreen
  rendering.
