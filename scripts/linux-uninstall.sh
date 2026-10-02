#!/bin/bash
# Removes what install.sh installed.
#   ./uninstall.sh [--system]
set -euo pipefail

if [ "${1:-}" = "--system" ]; then
    [ "$(id -u)" = 0 ] || { echo "--system needs root: sudo ./uninstall.sh --system" >&2; exit 1; }
    BIN=/usr/local/bin; VST3DIR=/usr/local/lib/vst3; LV2DIR=/usr/local/lib/lv2
    APPS=/usr/local/share/applications; ICONS=/usr/local/share/icons/hicolor/256x256/apps
else
    BIN="$HOME/.local/bin"; VST3DIR="$HOME/.vst3"; LV2DIR="$HOME/.lv2"
    APPS="$HOME/.local/share/applications"; ICONS="$HOME/.local/share/icons/hicolor/256x256/apps"
fi

rm -rf "$LV2DIR/Virtual FM-1.lv2" "$VST3DIR/Virtual FM-1.vst3"
rm -f "$BIN/virtual-fm1" "$APPS/virtual-fm1.desktop" "$ICONS/virtual-fm1.png"
echo "Removed Virtual FM-1. Your presets stay in ~/.config/Virtual FM-1."
