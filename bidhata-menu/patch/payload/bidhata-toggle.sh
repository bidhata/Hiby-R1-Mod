#!/bin/sh
# Boot menu control for the HiBy R1.
# Run over ADB: bidhata-toggle.sh [menu|player|status|launch-menu]
#
# The rootfs is read-only squashfs, so this does not edit any init script. The
# firmware image already boots /usr/bin/bidhata-launcher.sh; this only writes a flag
# on the writable /usr/data partition that tells the launcher what to do.

MODE_FLAG=/usr/data/bidhata_boot_mode
LAUNCHER=/usr/bin/bidhata-launcher.sh
INIT=/etc/init.d/S92_03_start_music_player

MENU=/usr/data/bidhata-menu
[ -x "$MENU" ] || MENU=/usr/bin/bidhata-menu

case "$1" in
    menu)
        rm -f "$MODE_FLAG"
        sync
        echo "Boot mode: BIDHATA MENU (menu at every boot)"
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
            echo "Boot mode:  BIDHATA MENU"
        fi
        echo "Boot script: $(grep '^PL01=' "$INIT" 2>/dev/null)"
        if grep -q "^PL01=$LAUNCHER" "$INIT" 2>/dev/null; then
            echo "            (launcher firmware installed)"
        else
            echo "            WARNING: this firmware does not boot the launcher"
        fi
        [ -x "$MENU" ] && echo "Launcher:   $MENU" || echo "Launcher:   NOT FOUND"
        ;;

    launch-menu)
        # Open the launcher now, without changing the boot mode.
        killall hiby_player 2>/dev/null
        sleep 1
        if [ -x "$MENU" ]; then
            "$MENU"
        else
            echo "Error: bidhata-menu not found at $MENU"
            exit 1
        fi
        ;;

    *)
        echo "Boot menu control"
        echo "Usage: $0 [command]"
        echo ""
        echo "Commands:"
        echo "  menu        - show the boot menu at every boot (default)"
        echo "  player      - boot straight to the music player, skipping the menu"
        echo "  status      - show the boot mode and the installed build"
        echo "  launch-menu - open the launcher now (stops the music player)"
        ;;
esac
