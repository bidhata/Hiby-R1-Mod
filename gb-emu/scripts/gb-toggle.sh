#!/bin/sh
# GB-Emu toggle script
# Run via ADB shell to switch between Music Player and Game Boy Emulator
# Usage: gb-toggle.sh [player|emu|status]

FLAG="/usr/data/emulator_mode"
EMU="/usr/bin/gb-emu"
EMU_DATA="/usr/data/gb-emu"

case "$1" in
    emu)
        echo "emulator" > "$FLAG"
        echo "Mode set to: GAME BOY EMULATOR"
        echo "Reboot to apply, or run: reboot"
        ;;
    player)
        rm -f "$FLAG"
        echo "Mode set to: MUSIC PLAYER"
        echo "Reboot to apply, or run: reboot"
        ;;
    status)
        if [ -f "$FLAG" ]; then
            echo "Current mode: GAME BOY EMULATOR"
        else
            echo "Current mode: MUSIC PLAYER"
        fi
        ;;
    launch-emu)
        # Launch emulator immediately (kill music player first)
        killall hiby_player 2>/dev/null
        sleep 1
        if [ -x "$EMU" ]; then
            $EMU &
        elif [ -x "$EMU_DATA" ]; then
            $EMU_DATA &
        else
            echo "Error: emulator not found. Build and deploy first."
        fi
        ;; 
    *)
        echo "GB-Emu Mode Toggle"
        echo "Usage: $0 [command]"
        echo ""
        echo "Commands:"
        echo "  emu        - Switch to Game Boy mode (reboot needed)"
        echo "  player     - Switch to Music Player mode (reboot needed)"
        echo "  status     - Show current mode"
        echo "  launch-emu - Launch emulator now (kills music player)"
        ;;
esac
