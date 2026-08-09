#!/bin/sh
# Boot launcher for the HiBy R1.
#
# S92_03_start_music_player runs this instead of hiby_player.sh. It shows the
# Game Boy launcher, and starts the stock music player when the user picks it.
#
# The rootfs is read-only squashfs, so behaviour is controlled by a flag file on
# the writable /usr/data partition rather than by editing this script:
#
#   /usr/data/gb_boot_mode  containing "player"  -> skip the menu, boot straight
#                                                   to the music player
#   anything else, or absent                     -> show the launcher
#
# gb-toggle.sh writes that flag.

MODE_FLAG=/usr/data/gb_boot_mode

# Where the stock firmware mounts the SD card. /data is a symlink to /usr/data,
# so this lands on the writable UBIFS partition.
SD_MOUNT=/data/mnt/sd_0

# /usr/bin holds the copy baked into the firmware. /usr/data is a separate UBIFS
# partition mounted at boot, so a newer build pushed there over ADB wins, which
# is what allows testing without reflashing.
EMU=/usr/data/gb-emu
[ -x "$EMU" ] || EMU=/usr/bin/gb-emu

# gb-emu returns this when the user asked for the music player.
EXIT_RUN_PLAYER=10

log() {
    echo "gb-launcher: $*" > /dev/console 2>/dev/null
}

start_battery_log() {
    [ -x /usr/bin/batd ] || return 0
    killall    batd  >/dev/null 2>&1
    killall -9 batd  >/dev/null 2>&1
    /usr/bin/batd -v -s -t5 -o "$SD_MOUNT/batlog.txt" &
}

# Mount the SD card the way the stock firmware does.
#
# Nothing mounts it at boot: sys_server performs mounts, but only when asked,
# and the only thing that ever asks for sd_0 is hiby_player - which the launcher
# runs in place of. So without this the card is simply not there, and no ROMs
# are found. The player remounts it itself when it starts, so leaving this
# mounted is harmless.
mount_sd_card() {
    # Already mounted (by a previous run, or by the player before a restart).
    if grep -q " $SD_MOUNT " /proc/mounts 2>/dev/null; then
        log "SD already mounted at $SD_MOUNT"
        return 0
    fi

    mkdir -p "$SD_MOUNT" 2>/dev/null

    # MMC probing is asynchronous, and this runs early in the boot, so the node
    # may not exist yet. Wait briefly rather than declaring the card missing.
    waited=0
    while [ "$waited" -lt 50 ]; do
        if [ -b /dev/mmcblk0 ] || [ -b /dev/mmcblk1 ]; then
            break
        fi
        sleep 0.1
        waited=$((waited + 1))
    done

    # The SD card is mmcblk0: internal storage is raw NAND mounted as UBIFS
    # (see mount_ubifs.sh), not MMC, so nothing else claims that number, and
    # adboff exports /dev/mmcblk0 as USB mass storage - which is the card.
    # mmcblk1 is the second, normally empty slot sys_server also scans; it is
    # tried afterwards in case a variant populates it. The bare device covers
    # cards written without a partition table.
    for dev in /dev/mmcblk0p1 /dev/mmcblk0 /dev/mmcblk1p1 /dev/mmcblk1; do
        [ -b "$dev" ] || continue

        # Read-only first: if this turns out to be the wrong device, nothing on
        # it can have been modified before the check below unmounts it again.
        mount -t vfat,exfat -o ro "$dev" "$SD_MOUNT" 2>/dev/null ||
            mount -o ro "$dev" "$SD_MOUNT" 2>/dev/null || continue

        # A games/ folder is what makes this the card we want. Without it there
        # is nothing to list, so let it go and keep looking.
        if [ ! -d "$SD_MOUNT/games" ]; then
            log "$dev has no games/ folder, skipping"
            umount "$SD_MOUNT" 2>/dev/null
            continue
        fi

        # Remount read-write so battery saves can be written next to the ROM.
        # Staying read-only is survivable, so a failure here is not fatal.
        mount -o remount,rw "$SD_MOUNT" 2>/dev/null ||
            log "$SD_MOUNT stays read-only, saves will not persist"

        log "mounted $dev at $SD_MOUNT"
        return 0
    done

    log "no SD card with a games/ folder found (tried mmcblk0/mmcblk1)"
    return 1
}

start_player() {
    killall    hiby_player  >/dev/null 2>&1
    killall -9 hiby_player  >/dev/null 2>&1
    /usr/bin/hiby_player
    # The stock script reboots once the player exits; keep that behaviour so the
    # device does not sit at a blank screen. reboot only signals init and returns
    # straight away, so exit rather than falling back into the launcher while the
    # shutdown runs.
    sleep 1
    reboot
    exit 0
}

# Explicit "boot to the player" request: skip the menu entirely. Do this before
# touching the card, so the player mounts it itself exactly as it always has.
if [ -f "$MODE_FLAG" ] && [ "$(cat "$MODE_FLAG" 2>/dev/null)" = "player" ]; then
    log "boot mode is player, skipping launcher"
    start_battery_log
    start_player
fi

mount_sd_card
start_battery_log

if [ -x "$EMU" ]; then
    "$EMU"
    status=$?
    [ "$status" -ne "$EXIT_RUN_PLAYER" ] && log "gb-emu exited with status $status"
else
    log "gb-emu not found at $EMU"
fi

# Every path ends here: the user picked the player, the launcher quit, or it
# failed outright. The device is never left without something running.
start_player
