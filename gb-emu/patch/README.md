# Applying the Game Boy launcher to a HiBy R1 firmware

Two scripts, so a new firmware release can be re-patched without repeating any
of the analysis behind it.

## One command

```bash
./build-firmware.sh r1_new.upt r1_gb.upt
```

Unpacks the stock firmware, installs the launcher, repacks it. Flash
`r1_gb.upt` the way you would any HiBy update. Needs `7z`, `unsquashfs`,
`mksquashfs` and `genisoimage`; nothing is written outside the output file.

## Step by step

If you would rather see the intermediate tree, or make other changes at the same
time:

```bash
./unpack.sh r1_new.upt                    # from the project root
./gb-emu/patch/gb-patch.sh squashfs-root  # install
./repack.sh                               # writes r1_repacked.upt
```

## What it does

| Change | Path |
|--------|------|
| Adds the emulator and launcher | `/usr/bin/gb-emu` |
| Adds the boot script | `/usr/bin/gb-launcher.sh` |
| Adds the boot-mode switch | `/usr/bin/gb-toggle.sh` |
| Points the boot script at the launcher | `/etc/init.d/S9*_start_music_player` |

Four files, nothing else. `hiby_player.sh` is untouched, so the stock path stays
intact and the launcher hands back to it whenever the user picks MUSIC PLAYER.

## Other commands

```bash
./gb-patch.sh --check  squashfs-root   # what is installed, and which init script
./gb-patch.sh --revert squashfs-root   # restore the stock boot exactly
```

`--revert` restores the init script from the backup the patch made
(`<init>.gb-orig`), then removes the three added files. Re-running the install is
safe: the backup is only taken the first time, so it always holds the stock
version.

## Surviving a firmware update

The init script is found by **what it does**, not by its name: the patch scans
`/etc/init.d/S*` for the one that launches `hiby_player.sh`, and reads the
variable name out of it rather than assuming `PL01`. A firmware that renumbers
the script or renames the variable is handled without changes here.

If HiBy ever stops starting the player from an init script, the patch stops and
says so rather than guessing:

```
error: no init script starts hiby_player.sh; firmware layout has changed, not patching
```

That is the point at which this directory needs revisiting — the boot chain
would have to be traced again, as `/etc/inittab` → `rcS` → `S92_03_*` →
`hiby_player.sh`.

## The binary

`payload/gb-emu` is a prebuilt static MIPS32r2 executable, so the patch works
with no toolchain installed. To use your own build instead, compile it and the
patch will prefer it automatically:

```bash
make -C .. clean
make -C .. CROSS=/opt/mipsel-linux-musl-cross/bin/mipsel-linux-musl- STATIC=1
```

The patch checks that whatever it picks is actually a MIPS binary, so a native
x86 build left in the project directory cannot end up in the image by mistake.
