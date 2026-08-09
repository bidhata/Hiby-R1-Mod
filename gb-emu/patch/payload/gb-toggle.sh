#!/bin/sh
# GB-Emu boot control for the HiBy R1.
# Run over ADB: gb-toggle.sh [emu|player|status|launch-emu]
#
# The rootfs is read-only squashfs, so this does not edit any init script. The
# firmware image already boots /usr/bin/gb-launcher.sh; this only writes a flag
# on the writable /usr/data partition that tells the launcher what to do.

MODE_FLAG=/usr/data/gb_boot_mode
# Where the stock firmware (and gb-launcher.sh) mounts the SD card.
SD_MOUNT=/data/mnt/sd_0
LAUNCHER=/usr/bin/gb-launcher.sh
INIT=/etc/init.d/S92_03_start_music_player

EMU=/usr/data/gb-emu
[ -x "$EMU" ] || EMU=/usr/bin/gb-emu

case "$1" in
    emu)
        rm -f "$MODE_FLAG"
        sync
        echo "Boot mode: GAME BOY LAUNCHER (menu at every boot)"
        echo "Reboot to apply."
        ;;

    player)
        echo "player" > "$MODE_FLAG" || exit 1
        sync
        echo "Boot mode: MUSIC PLAYER (launcher skipped)"
        echo "Reboot to apply."
        ;;

    status)
        if [ -f "$MODE_FLAG" ] && [ "$(cat "$MODE_FLAG" 2>/dev/null)" = "player" ]; then
            echo "Boot mode:  MUSIC PLAYER (launcher skipped)"
        else
            echo "Boot mode:  GAME BOY LAUNCHER"
        fi
        echo "Boot script: $(grep '^PL01=' "$INIT" 2>/dev/null)"
        if grep -q "^PL01=$LAUNCHER" "$INIT" 2>/dev/null; then
            echo "            (launcher firmware installed)"
        else
            echo "            WARNING: this firmware does not boot the launcher"
        fi
        [ -x "$EMU" ] && echo "Emulator:   $EMU" || echo "Emulator:   NOT FOUND"
        echo "ROMs:"
        # One ls over both patterns: a per-pattern || would print "none" for
        # whichever extension happened to be absent.
        roms=$(ls "$SD_MOUNT/games/" 2>/dev/null | grep -iE '\.gbc?$')
        if [ -n "$roms" ]; then
            echo "$roms" | sed 's|^|  |'
        else
            echo "  none in $SD_MOUNT/games/"
        fi
        ;;

    launch-emu)
        # Open the launcher now, without changing the boot mode.
        killall hiby_player 2>/dev/null
        sleep 1
        if [ -x "$EMU" ]; then
            "$EMU"
        else
            echo "Error: emulator not found at $EMU"
            exit 1
        fi
        ;;

    *)
        echo "GB-Emu boot control"
        echo "Usage: $0 [command]"
        echo ""
        echo "Commands:"
        echo "  emu        - show the ROM launcher at every boot (default)"
        echo "  player     - boot straight to the music player, skipping the menu"
        echo "  status     - show the boot mode, the installed build and the ROMs"
        echo "  launch-emu - open the launcher now (stops the music player)"
        ;;
esac
