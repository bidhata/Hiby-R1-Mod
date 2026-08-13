# bidhata-menu: Boot Menu Launcher for HiBy R1

Boot utility launcher for the HiBy R1 portable music player. Shows a menu of
system actions, hands the device back to the stock music player (or to
Rockbox) once the user picks one.

Originally a Game Boy emulator; the emulation core (CPU/PPU/MMU/APU, ROM
scanning, palette selection) has since been removed — this is now just the
boot menu, fully renamed to Bidhata Menu (binary, scripts, on-screen title,
all of it). Menu rows are config-driven (see "Configuring the menu" below)
rather than hardcoded in C, specifically so this is easy to adopt on a
different modded firmware: add/remove/reorder rows, or point a row at a
different alternate player, by editing a plain-text file — no rebuild.

## Building

### Native build (x86_64, for testing)
```bash
make
```

### Cross-compile for HiBy R1 (MIPS32-le, static musl)
```bash
make clean
make CROSS=/opt/mipsel-linux-musl-cross/bin/mipsel-linux-musl- STATIC=1
```
Produces fully static `bidhata-menu`.

## Quick Deploy (via ADB)

```bash
# 1. Cross-compile
make clean
make CROSS=/opt/mipsel-linux-musl-cross/bin/mipsel-linux-musl- STATIC=1

# 2. Push binary and scripts to the device
make deploy

# 3. Open the launcher now, without changing the boot setup
adb shell /usr/bin/bidhata-toggle.sh launch-menu
```

### Where the SD card lives

Stock firmware mounts card at **`/data/mnt/sd_0`** (`/data` symlink to
`/usr/data`), not `/mnt/sd_0`. Nothing mounts it at boot: `sys_server`
handles mounts, only when asked — only `hiby_player` ever asks for `sd_0`,
and launcher runs in place of it. `bidhata-launcher.sh` therefore mounts the
card itself before starting the menu, trying `mmcblk0p1`, `mmcblk0`,
`mmcblk1p1`, `mmcblk1` in turn as vfat/exfat. This is what makes Rockbox's
`.rockbox/` resource tree on the card visible. Music player remounts it on
start — harmless to it.

## Building a flashable firmware image

Launcher can be baked into `.upt` so a flashed device boots straight into
it, no ADB needed after.

Quickest route, handles future firmware releases too:

```bash
./bidhata-menu/patch/build-firmware.sh r1_new.upt r1_gb.upt
```

Unpacks stock firmware, installs launcher, repacks it. See
`bidhata-menu/patch/README.md` for what changes, how to undo, what happens
if HiBy rearranges boot scripts.

By hand instead, from project root (one level above `bidhata-menu/`):

```bash
# 1. Cross-compile, and install into the unpacked rootfs
cd bidhata-menu
make clean
make CROSS=/opt/mipsel-linux-musl-cross/bin/mipsel-linux-musl- STATIC=1
cp bidhata-menu           ../squashfs-root/usr/bin/bidhata-menu
cp scripts/bidhata-launcher.sh ../squashfs-root/usr/bin/
cp scripts/bidhata-toggle.sh   ../squashfs-root/usr/bin/
cp config/bidhata-menu.conf.default ../squashfs-root/usr/bin/bidhata-menu.conf
chmod 755 ../squashfs-root/usr/bin/bidhata-menu ../squashfs-root/usr/bin/bidhata-*.sh

# 2. Point the boot script at the launcher
sed -i 's|^PL01=.*|PL01=/usr/bin/bidhata-launcher.sh|' \
    ../squashfs-root/etc/init.d/S92_03_start_music_player

# 3. Repack
cd ..
./repack.sh          # writes r1_repacked.upt
```

Flash `r1_repacked.upt` same way as any stock firmware update.

Note: `/usr/data` is a **separate UBIFS partition** mounted at boot —
anything placed there inside the image is hidden once the mount happens.
The binary must live on rootfs, at `/usr/bin/bidhata-menu`. Launcher still
prefers `/usr/data/bidhata-menu` when it exists — makes it possible to
test a new build over ADB without reflashing:

```bash
adb push bidhata-menu /usr/data/bidhata-menu && adb shell chmod +x /usr/data/bidhata-menu
```

## The boot menu

Device flashed with the image above shows the menu every boot. Row order,
labels, colors, and actions all come from `bidhata-menu.conf` (see
"Configuring the menu" below) -- the default config ships these 7 rows:

1. **HIBY PLAYER** -- the reserved sentinel row; always available even if
   the config is missing or broken.
2. **ROCKBOX (BETA)** -- starts `rockbox.r1`.
3. **SHUTDOWN** -- runs `poweroff`.
4. **FIRMWARE UPDATE (SD)** -- runs `bootmode.sh Recovery` and reboots into
   the updater, same path `Settings → Firmware Update → Via SD-card` uses
   on stock firmware. Needs a `.upt` file at the SD card's root.
5. **FACTORY RESET** -- writes `recovery_all` to `/data/recovery_all` and
   reboots; `recovery_all.sh` wipes `/data` on the next boot. Same
   mechanism stock firmware's own factory reset uses.
6. **STRIP FILE ART** -- runs `strip_art_all.sh` over the SD card, then
   returns to the menu (unlike the entries above, which never come back).
   Removes embedded FLAC/MP3 art; see "Stripping Embedded Album Art" in the
   top-level README.
7. **STRIP ALBUM ART** -- runs `remove_folder_art.sh -f`, then returns to
   the menu. Deletes standalone cover files (`folder.jpg`, `cover.png`,
   ...).

Volume Up/Down moves cursor, Next Track selects. Every row with a
non-empty `CONFIRM_TEXT` in the config opens a confirm screen first
(defaults to CANCEL; Power always backs out) before running -- that's
every default row except HIBY PLAYER and ROCKBOX (BETA).

**Idle timeout:** 5 seconds with no input auto-selects whichever row is the
reserved "player" sentinel (HIBY PLAYER by default) -- a countdown
("STARTING \<LABEL\> IN Ns...") shows in the footer the whole time and
resets the instant any key or tap arrives, so a device sitting at the menu
(or one that just rebooted with nobody in front of it) doesn't stay stuck
there. If a customized config has no player-sentinel row at all, idle
timeout quits the menu instead of guessing.

## Configuring the menu

`bidhata-menu.conf` is a plain-text, pipe-delimited file: one line per
menu row, `#`-prefixed and blank lines ignored.

```
LABEL|COLOR|ACTION|PARAM|CONFIRM_TEXT
```

- **LABEL** -- row text, e.g. `ROCKBOX (BETA)`.
- **COLOR** -- a name from the built-in palette (`PLAYER`, `ROCKBOX`,
  `SHUTDOWN`, `FW_UPDATE`, `DANGER`, `STRIP`, `TEXT`), or a raw `0xRRGGBB`
  for anything the palette doesn't cover.
- **ACTION** -- one of:
  - `run` with `PARAM=player` -- the reserved sentinel: exits with status
    10 to hand back to the stock music player. Always safe even with a
    broken config elsewhere in the file.
  - `run` with `PARAM=<path> [args...]` -- launches a different binary.
    Exits with a generic status; `bidhata-launcher.sh` execs whatever was
    selected. Adding a row that launches an entirely different alternate
    player is a **one-line change here, no code, no rebuild**.
  - `exec` with `PARAM=<shell command>` -- runs in-process via `system()`,
    then returns to the menu (what STRIP FILE ART/ALBUM ART do).
  - `shutdown` / `factory_reset` / `fw_update` -- reserved built-in
    keywords (PARAM ignored) for the three actions that need exact
    `sync()`-before-reboot sequencing; not available as raw config-supplied
    commands, on purpose.
- **CONFIRM_TEXT** -- non-empty shows the Yes/No gate with this detail
  text before running; empty skips it.

Worked example -- adding a row for a different alternate player:

```
MY PLAYER|PLAYER|run|/usr/bin/my-player|
```

Delete a row by deleting its line. Reorder by reordering lines.

**Where it lives:** `/usr/data/bidhata-menu.conf` if present (writable,
push over ADB to customize without reflashing), else the bundled default
at `/usr/bin/bidhata-menu.conf` (baked into the image, installed by
`bidhata-patch.sh`), else a compiled-in fallback identical to
`config/bidhata-menu.conf.default` -- a missing or malformed config file
can never leave the menu empty.

To boot straight to music player, skip menu:

```bash
adb shell /usr/bin/bidhata-toggle.sh player
adb reboot

# and to bring the menu back
adb shell /usr/bin/bidhata-toggle.sh menu
adb reboot
```

### How it works

Stock boot chain: `inittab` → `rcS` → `/etc/init.d/S92_03_start_music_player`
→ `hiby_player.sh`. Firmware image changes that init script's `PL01=` line to
point at `bidhata-launcher.sh` instead.

Rootfs is read-only squashfs — nothing can rewrite that init script on a
running device. `bidhata-toggle.sh` switches modes via a flag file on the
writable `/usr/data` partition: `/usr/data/bidhata_boot_mode` containing `player`
makes the launcher skip the menu, start the music player straight away.

The launcher never starts the music player or another target itself: it
exits with status 10 to request the player (the "player" sentinel config
row), or a generic status to request whatever it just wrote to
`/usr/data/bidhata_exec_target` (any other `run` row) — `bidhata-launcher.sh`
does the actual starting. Any other exit — crash, no screen, no working
buttons — also falls through to the music player, so a broken launcher
can't leave the device stuck. `hiby_player.sh` is untouched in the image —
the stock path is always recoverable.

## Controls

R1 has only three buttons — volume rocker, Next Track, Power. No Play/Pause
key on this hardware, so **Next Track confirms**.

| Control | Action |
|---------|--------|
| Vol Up | Move up |
| Vol Down | Move down |
| Next Track | Select |
| Power | Quit to music player |
| Tap a row | Select it directly |

## File Structure

```
bidhata-menu/
├── include/            # Header files
├── src/
│   ├── main.c          # Entry point and launcher loop
│   ├── menu.c          # Boot menu
│   ├── menu_config.c   # Config file parser (LABEL|COLOR|ACTION|PARAM|CONFIRM_TEXT)
│   ├── font.c          # 5x7 bitmap font
│   ├── platform.c      # Framebuffer/input
│   └── bootmenu.c      # Unused standalone mode selector, superseded by menu.c
├── config/
│   └── bidhata-menu.conf.default  # Shipped default, see "Configuring the menu"
├── scripts/
│   ├── bidhata-launcher.sh  # Boot script: menu, falling back to the player
│   └── bidhata-toggle.sh    # Boot-mode switch: menu/player/status/launch
├── patch/              # Apply to any firmware release
│   ├── bidhata-patch.sh     # install / --revert / --check against a rootfs
│   ├── build-firmware.sh  # stock .upt in, patched .upt out
│   └── payload/        # prebuilt MIPS binary and scripts
├── Makefile
└── README.md
```

## Deployment Files

| File | Device Path | Purpose |
|------|-------------|---------|
| `bidhata-menu` | `/usr/bin/bidhata-menu` | Boot menu binary (in the firmware image) |
| `bidhata-launcher.sh` | `/usr/bin/bidhata-launcher.sh` | Boot script |
| `bidhata-toggle.sh` | `/usr/bin/bidhata-toggle.sh` | Boot-mode switch |
| `bidhata-menu.conf.default` | `/usr/bin/bidhata-menu.conf` | Bundled default menu config, see "Configuring the menu" |

## Technical Details

- **Input**: Linux evdev (`/dev/input/event*`)
- **Video**: Direct framebuffer mmap (`/dev/fb0`), 16 or 32 bpp
- **Target**: Ingenic X1600 (MIPS32-le)

## Known limits / stretch goals

Deliberately not built yet -- noted here rather than silently scope-creeped
into the config work:

- `bidhata-patch.sh`'s stock-player detection (`hiby_player.sh`) is still a
  hardcoded string, specific to HiBy-family firmware. Adopting this on a
  genuinely different (non-HiBy) vendor firmware would need that made
  configurable too.
- `bidhata-launcher.sh`'s generic exec dispatch runs every `run`-action
  target with the same library search path (no `LD_LIBRARY_PATH`
  override) -- correct for Rockbox on real hardware (see the comment in
  that script), but a future target that genuinely needs its own bundled
  libs would need a per-target convention added to the config format,
  which doesn't exist yet.
