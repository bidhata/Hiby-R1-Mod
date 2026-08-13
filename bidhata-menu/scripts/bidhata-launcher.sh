#!/bin/sh
# Boot launcher for the HiBy R1.
#
# S92_03_start_music_player runs this instead of hiby_player.sh. It shows the
# boot menu, and starts the stock music player (or Rockbox) when the user
# picks it.
#
# The rootfs is read-only squashfs, so behaviour is controlled by a flag file on
# the writable /usr/data partition rather than by editing this script:
#
#   /usr/data/bidhata_boot_mode  containing "player"  -> skip the menu, boot straight
#                                                   to the music player
#   anything else, or absent                     -> show the launcher
#
# bidhata-toggle.sh writes that flag.

MODE_FLAG=/usr/data/bidhata_boot_mode

# Where the stock firmware mounts the SD card. /data is a symlink to /usr/data,
# so this lands on the writable UBIFS partition.
SD_MOUNT=/data/mnt/sd_0

# /usr/bin holds the copy baked into the firmware. /usr/data is a separate UBIFS
# partition mounted at boot, so a newer build pushed there over ADB wins, which
# is what allows testing without reflashing.
MENU=/usr/data/bidhata-menu
[ -x "$MENU" ] || MENU=/usr/bin/bidhata-menu

# bidhata-menu returns this when the user picked the config's reserved
# "player" sentinel item.
EXIT_RUN_PLAYER=10

# bidhata-menu returns this for any other "run" action -- the real command
# line to exec is whatever it just wrote to EXEC_TARGET_FILE. Generic by
# design: adding a menu row that launches a different binary (Rockbox, or
# anything else a config-driven fork wants) needs no changes here, only a
# new line in bidhata-menu.conf. See
# docs/superpowers/plans/2026-08-13-bidhata-menu-rename-and-config.md.
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

# Mount the SD card the way the stock firmware does.
#
# Nothing mounts it at boot: sys_server performs mounts, but only when asked,
# and the only thing that ever asks for sd_0 is hiby_player - which the launcher
# runs in place of. So without this the card is simply not there, and Rockbox's
# .rockbox/ resource tree can't be found. The player remounts it itself when it
# starts, so leaving this mounted is harmless.
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

        # Mount the first card that answers, read-only first: if it turns out
        # to be wrong somehow, nothing on it has been modified.
        mount -t vfat,exfat -o ro "$dev" "$SD_MOUNT" 2>/dev/null ||
            mount -o ro "$dev" "$SD_MOUNT" 2>/dev/null || continue

        # Remount read-write so the battery log can be written to it, and so
        # Rockbox can write its playlist/database state there too. Staying
        # read-only is survivable, so a failure here is not fatal.
        #
        # Must pass $dev explicitly rather than just "$SD_MOUNT": /data is a
        # symlink to /usr/data, but /proc/mounts records the resolved real
        # path, so this device's `mount -o remount` fails to find a match by
        # the symlinked target path alone ("can't find /data/mnt/sd_0 in
        # /proc/mounts") and silently no-ops, leaving the card read-only for
        # the rest of the session. Confirmed live on-device.
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
    # The stock script reboots once the player exits; keep that behaviour so the
    # device does not sit at a blank screen. reboot only signals init and returns
    # straight away, so exit rather than falling back into the launcher while the
    # shutdown runs.
    sleep 1
    reboot
    exit 0
}

# Generic hand-off for any config-defined "run" item other than the
# reserved player sentinel (Rockbox by default, or whatever else a
# customized bidhata-menu.conf adds -- see EXEC_TARGET_FILE above). No
# per-target special-casing here by design: the whole point of the config
# format is that adding a menu row that launches a different binary never
# touches this script.
#
# Deliberately NOT setting LD_LIBRARY_PATH: tested against real hardware
# with rockbox.r1 specifically, and the freshly cross-built alsa-lib it
# ships fails to open the device's ALSA control ("amixer: Control device
# hw:0 open error: Invalid argument"), while the device's own stock
# libasound.so.2 -- the same one hiby_player already uses for these same
# custom controls (Output Port Switch, DOP_EN, ...) -- works correctly. So
# every target here runs with the default library search path and picks up
# the device's own copy. A future target that genuinely needs its own
# bundled libs would need this revisited, not silently worked around.
start_target() {
    cmdline=$1
    target_bin=$(basename "${cmdline%% *}")
    killall    "$target_bin"  >/dev/null 2>&1
    killall -9 "$target_bin"  >/dev/null 2>&1
    # shellcheck disable=SC2086 -- word-splitting is intentional: $cmdline
    # is a command line (binary + optional args), not a single path.
    $cmdline
    # Mirrors start_player()'s safety net: if the target ever returns
    # instead of rebooting or handing off control itself, don't leave the
    # device sitting at a blank screen.
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

# Every path ends here: the user picked the player, the launcher quit, or it
# failed outright. The device is never left without something running.
start_player
