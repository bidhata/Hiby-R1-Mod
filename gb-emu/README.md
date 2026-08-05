# GB-Emu: Game Boy / Game Boy Color Emulator for HiBy R1

A lightweight Game Boy (DMG) and Game Boy Color emulator designed for the HiBy R1 portable music player.

## Features

- Full LR35902 CPU emulation (256 main opcodes + 256 CB-prefixed opcodes)
- PPU with background, window, and sprite rendering
- MBC1, MBC2, MBC3, MBC5 memory bank controllers
- Square wave audio (channels 1 & 2)
- Framebuffer video output (480x800 LCD)
- ALSA audio output
- Touchscreen + hardware button input
- Boot mode selector (Music Player / Game Boy)

## Building

### Native build (x86_64 for testing)
```bash
make
make bootmenu
```

### Cross-compile for HiBy R1 (MIPS32-le, static musl)
```bash
make clean
make CROSS=/opt/mipsel-linux-musl-cross/bin/mipsel-linux-musl- STATIC=1
```
Produces a fully static `gb-emu` (no ALSA on device; audio is silent in this build).

## Quick Deploy (via ADB)

```bash
# 1. Cross-compile
make CROSS=mipsel-linux-gnu-

# 2. Deploy everything to device
make deploy

# 3. Push ROM files (SD card games/ folder)
mkdir -p games
cp /path/to/your/*.gb games/
# put the games/ folder on the SD card, or push to /mnt/sd_0/games

# 4. Launch emulator
adb shell /usr/bin/gb-toggle.sh launch-emu
```

## Mode Switching

The emulator integrates with the HiBy R1 boot process. You can switch between Music Player and Game Boy mode:

```bash
# Switch to Game Boy mode (persists across reboots)
adb shell /usr/bin/gb-toggle.sh emu
adb reboot

# Switch back to Music Player mode
adb shell /usr/bin/gb-toggle.sh player
adb reboot

# Check current mode
adb shell /usr/bin/gb-toggle.sh status

# Launch emulator immediately (kills music player)
adb shell /usr/bin/gb-toggle.sh launch-emu
```

### How it works

The `hiby_player.sh` startup script checks for `/usr/data/emulator_mode`:
- **Flag exists** → launches GB emulator instead of music player
- **Flag absent** → launches music player (default)

When the emulator exits, the music player automatically restarts.

## Controls

| Button | Game Boy Key |
|--------|-------------|
| Vol Up | D-pad Up |
| Vol Down | D-pad Down |
| Next Track | D-pad Right |
| Play/Pause | D-pad Left |
| Touch top-right | A button |
| Touch bottom-right | B button |
| Touch top-left | Start |
| Touch bottom-left | Select |
| Power (long press) | Quit |

## File Structure

```
gb-emu/
├── include/           # Header files
├── src/
│   ├── main.c         # Entry point
│   ├── cpu.c          # LR35902 CPU (all opcodes)
│   ├── ppu.c          # Pixel Processing Unit
│   ├── mmu.c          # Memory Management Unit
│   ├── apu.c          # Audio Processing Unit
│   ├── platform.c     # Framebuffer/ALSA/input
│   └── bootmenu.c     # Boot mode selector
├── scripts/
│   └── gb-toggle.sh   # Mode switching script
├── Makefile
└── README.md
```

## Deployment Files

| File | Device Path | Purpose |
|------|-------------|---------|
| `gb-emu` | `/usr/data/gb-emu` | Emulator binary |
| `gb-toggle.sh` | `/usr/bin/gb-toggle.sh` | Mode toggle script |
| `hiby_player.sh` | `/usr/bin/hiby_player.sh` | Modified startup (checks mode flag) |

## Technical Details

- **Display**: 160x144 native, scaled 3x to 480x432, centered on 480x800 screen
- **Audio**: 44100Hz, 16-bit mono via ALSA
- **Input**: Linux evdev (`/dev/input/event*`)
- **Video**: Direct framebuffer mmap (`/dev/fb0`)
- **Target**: Ingenic X1600 (MIPS32-le), 1GHz dual-core

## Limitations

- Only DMG/Game Boy mode (no GBC color correction yet)
- Channels 3 & 4 audio stubbed
- No save states
- No ROM browser (command-line only)
- Cannot add GUI button inside hiby_player (closed-source binary)
