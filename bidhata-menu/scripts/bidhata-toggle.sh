#!/bin/sh
# bidhata-toggle.sh -- the menu's light switch. Run over ADB:
#   bidhata-toggle.sh [menu|player|status|launch-menu]
#
# Rootfs is read-only squashfs (immutable, like your professor's syllabus), so
# we don't edit init scripts. We just drop/touch a flag on writable /usr/data
# and the launcher obeys. Think feature flag, but make it boot mode. 

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
        # Pop the menu RIGHT NOW, don't touch boot mode. Like sneak preview night.
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
