# Applying the boot menu launcher to a HiBy R1 firmware

Binary, launcher/toggle scripts, and a default menu config. New firmware release re-patch possible, no repeat of analysis behind it.

## One command

```bash
./build-firmware.sh r1_new.upt r1_gb.upt
```

Unpacks stock firmware, installs launcher, repacks it. Flash `r1_gb.upt` like any HiBy update. Needs `7z`, `unsquashfs`, `mksquashfs`, `genisoimage`; nothing written outside output file.

## Step by step

Want intermediate tree, or other changes same time:

```bash
./unpack.sh r1_new.upt                    # from the project root
./bidhata-menu/patch/bidhata-patch.sh squashfs-root  # install
./repack.sh                               # writes r1_repacked.upt
```

## What it does

| Change | Path |
|--------|------|
| Adds the boot menu binary | `/usr/bin/bidhata-menu` |
| Adds the boot script | `/usr/bin/bidhata-launcher.sh` |
| Adds the boot-mode switch | `/usr/bin/bidhata-toggle.sh` |
| Adds the default menu config | `/usr/bin/bidhata-menu.conf` |
| Points the boot script at the launcher | `/etc/init.d/S9*_start_music_player` |

Four files plus the init-script edit, nothing else. `hiby_player.sh` untouched — stock path stays intact, launcher hands back to it whenever user picks MUSIC PLAYER.

## Other commands

```bash
./bidhata-patch.sh --check  squashfs-root   # what is installed, and which init script
./bidhata-patch.sh --revert squashfs-root   # restore the stock boot exactly
```

`--revert` restores init script from backup patch made (`<init>.bidhata-orig`), removes the four added files. Re-running install safe: backup only taken first time, always holds stock version.

## Surviving a firmware update

Init script found by **what it does**, not name: patch scans `/etc/init.d/S*` for one launching `hiby_player.sh`, reads variable name out of it rather than assuming `PL01`. Firmware renumbering script or renaming variable — handled, no changes needed here.

If HiBy ever stops starting player from init script, patch stops, says so instead of guessing:

```
error: no init script starts hiby_player.sh; firmware layout has changed, not patching
```

That's when this directory needs revisiting — boot chain retraced: `/etc/inittab` → `rcS` → `S92_03_*` → `hiby_player.sh`.

## The binary

`payload/bidhata-menu` prebuilt static MIPS32r2 executable — patch works, no toolchain needed. Own build instead: compile it, patch prefers it automatically:

```bash
make -C .. clean
make -C .. CROSS=/opt/mipsel-linux-musl-cross/bin/mipsel-linux-musl- STATIC=1
```

Patch checks whatever it picks is actually MIPS binary — native x86 build left in project directory can't end up in image by mistake.