#!/bin/sh
# Bidhata Launcher -- the bouncer at the HiBy R1's front door.
# S92_03_start_music_player runs THIS instead of hiby_player.sh. We show the
# boot menu, then hand back to the real player (or Rockbox, or whatever
# chaos you've config'd up) once you pick something.
#
# Rootfs is read-only squashfs (the "you can't sit with us" filesystem), so
# we use a flag file on the writable /usr/data to control behaviour:
#
#   /usr/data/bidhata_boot_mode  = "player"  -> skip menu, boot straight to player
#   anything else / absent                   -> show launcher (hello, gorgeous)
#
# bidhata-toggle.sh writes that flag. It's basically a light switch for boot. 

MODE_FLAG=/usr/data/bidhata_boot_mode

# /data is actually a symlink to /usr/data, so this SD mount lands on UBIFS.
SD_MOUNT=/data/mnt/sd_0

# Prefer a test build at /usr/data (ADB push) over the baked-in one. Like hot-reload, but for firmware.
MENU=/usr/data/bidhata-menu
[ -x "$MENU" ] || MENU=/usr/bin/bidhata-menu

# Exit code 10 = "player sentinel picked" (our Hitchhiker's 10 -- not the book's 42, we save that for later).
EXIT_RUN_PLAYER=10

# Exit code 42 = "run whatever's in EXEC_TARGET_FILE" -- yes, THE 42. Don't Panic.
# Any `run` row except the player sentinel lands here. Adding a new player is
# just one line in bidhata-menu.conf, zero script edits. We love that for you.
EXIT_RUN_TARGET=42
EXEC_TARGET_FILE=/usr/data/bidhata_exec_target

log() {
    echo "bidhata-menu: $*" > /dev/console 2>/dev/null
}

start_battery_log() {
    [ -x /usr/bin/batd ] || return 0
    killall    batd  >/dev/null 2>&1
    killall -9 batd  >/dev/null 2>&1
    /usr/bin/batd -v -s -t5 -o "$SD_MOUNT/batlog.txt" &
}

# Mount the SD card. Normally hiby_player does this via sys_server, but we
# RUN INSTEAD OF it -- so the card just sits there unmounted like a forgotten USB
# stick. We handle it ourselves: try mmcblk0/1, vfat/exfat, read-only first (trust no one).
mount_sd_card() {
    # Already mounted (by a previous run, or by the player before a restart).
    if grep -q " $SD_MOUNT " /proc/mounts 2>/dev/null; then
        log "SD already mounted at $SD_MOUNT"
        return 0
    fi

    mkdir -p "$SD_MOUNT" 2>/dev/null

    # MMC probing is async at boot -- /dev nodes may not exist yet. We poll, because patience is a virtue and reboots are not.
    waited=0
    while [ "$waited" -lt 50 ]; do
        if [ -b /dev/mmcblk0 ] || [ -b /dev/mmcblk1 ]; then
            break
        fi
        sleep 0.1
        waited=$((waited + 1))
    done

    # mmcblk0 = SD card (NAND is UBIFS, not MMC, so no conflict). mmcblk1 = second slot (usually empty). Bare device = no partition table -- someone raw-copied. We try all, we're not picky.
    for dev in /dev/mmcblk0p1 /dev/mmcblk0 /dev/mmcblk1p1 /dev/mmcblk1; do
        [ -b "$dev" ] || continue

        # Mount read-only first (don't touch it until we know it's ours), then remount rw so battery logs & Rockbox can write.
        # MUST pass $dev explicitly: /data is a symlink and `mount -o remount $SD_MOUNT` silently no-ops. Found out the hard way on-device. Classic symlink gotcha, like aliasing your twin's homework.
        mount -t vfat,exfat -o ro "$dev" "$SD_MOUNT" 2>/dev/null ||
            mount -o ro "$dev" "$SD_MOUNT" 2>/dev/null || continue
        mount -o remount,rw "$dev" "$SD_MOUNT" 2>/dev/null ||
            log "$SD_MOUNT stays read-only, saves will not persist"

        log "mounted $dev at $SD_MOUNT"
        return 0
    done

    log "no SD card found (tried mmcblk0/mmcblk1)"
    return 1
}

start_player() {
    killall    hiby_player  >/dev/null 2>&1
    killall -9 hiby_player  >/dev/null 2>&1
    /usr/bin/hiby_player
    # Player exited? Reboot -- don't leave a blank screen like a projector with no film.
    sleep 1
    reboot
    exit 0
}

# Hand-off for any `run` row that isn't the player sentinel (Rockbox, crackers, your custom thing).
# We intentionally DON'T set LD_LIBRARY_PATH: Rockbox's bundled alsa-lib breaks on this hardware
# ("amixer: Control device hw:0 open error"), but the stock libasound.so.2 works fine.
# So we let every target pick up the device's own copy. Future-proof? Ish. But battle-tested.
start_target() {
    cmdline=$1
    target_bin=$(basename "${cmdline%% *}")
    killall    "$target_bin"  >/dev/null 2>&1
    killall -9 "$target_bin"  >/dev/null 2>&1
    # shellcheck disable=SC2086 -- intentional word-split: $cmdline is "bin + args"
    $cmdline
    # If target returns instead of staying alive, reboot -- blank screen is not a vibe.
    sleep 1
    reboot
    exit 0
}

# "Just give me my music" mode -- skip menu, straight to player.
if [ -f "$MODE_FLAG" ] && [ "$(cat "$MODE_FLAG" 2>/dev/null)" = "player" ]; then
    log "boot mode is player, skipping launcher"
    start_battery_log
    start_player
fi

mount_sd_card
start_battery_log

# Pre-flight: nuke any stray hiby_player ghost before menu owns the framebuffer.
killall    hiby_player  >/dev/null 2>&1
killall -9 hiby_player  >/dev/null 2>&1

if [ -x "$MENU" ]; then
    "$MENU"
    status=$?
    if [ "$status" -eq "$EXIT_RUN_TARGET" ] && [ -f "$EXEC_TARGET_FILE" ]; then
        target_cmdline=$(cat "$EXEC_TARGET_FILE")
        log "starting $target_cmdline"
        start_target "$target_cmdline"
    fi
    [ "$status" -ne "$EXIT_RUN_PLAYER" ] && log "bidhata-menu exited with status $status"
else
    log "bidhata-menu not found at $MENU"
fi

# Bottom of the world: menu quit/crashed or picked player. Start hiby_player and never return.
start_player
