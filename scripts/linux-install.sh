#!/bin/bash
# Installs Virtual FM-1 from the unpacked Linux package (see README.txt).
#   ./install.sh [--system] [--no-app] [--no-vst3] [--no-lv2]
set -euo pipefail
cd "$(dirname "$0")"

SYSTEM=0; APP=1; VST3=1; LV2=1
for a in "$@"; do
    case "$a" in
        --system) SYSTEM=1 ;;
        --no-app) APP=0 ;;
        --no-vst3) VST3=0 ;;
        --no-lv2) LV2=0 ;;
        -h|--help) sed -n '2,3p' "$0"; exit 0 ;;
        *) echo "unknown option: $a" >&2; exit 2 ;;
    esac
done

if [ $SYSTEM = 1 ]; then
    [ "$(id -u)" = 0 ] || { echo "--system needs root: sudo ./install.sh --system" >&2; exit 1; }
    BIN=/usr/local/bin; VST3DIR=/usr/local/lib/vst3; LV2DIR=/usr/local/lib/lv2
    APPS=/usr/local/share/applications; ICONS=/usr/local/share/icons/hicolor/256x256/apps
else
    BIN="$HOME/.local/bin"; VST3DIR="$HOME/.vst3"; LV2DIR="$HOME/.lv2"
    APPS="$HOME/.local/share/applications"; ICONS="$HOME/.local/share/icons/hicolor/256x256/apps"
fi

if [ $LV2 = 1 ]; then
    mkdir -p "$LV2DIR"; rm -rf "$LV2DIR/Virtual FM-1.lv2"; cp -R "Virtual FM-1.lv2" "$LV2DIR/"
    echo "LV2:        $LV2DIR/Virtual FM-1.lv2"
fi
if [ $VST3 = 1 ]; then
    mkdir -p "$VST3DIR"; rm -rf "$VST3DIR/Virtual FM-1.vst3"; cp -R "Virtual FM-1.vst3" "$VST3DIR/"
    echo "VST3:       $VST3DIR/Virtual FM-1.vst3"
fi
if [ $APP = 1 ]; then
    mkdir -p "$BIN" "$APPS" "$ICONS"
    install -m 755 virtual-fm1 "$BIN/virtual-fm1"
    install -m 644 virtual-fm1.png "$ICONS/virtual-fm1.png"
    cat > "$APPS/virtual-fm1.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Virtual FM-1
Comment=FM synthesizer and editor for the M-VAVE FM-1
Exec=$BIN/virtual-fm1
Icon=virtual-fm1
Categories=AudioVideo;Audio;Music;
Terminal=false
EOF
    echo "App:        $BIN/virtual-fm1 (and a menu entry)"
    case ":$PATH:" in *":$BIN:"*) ;; *) echo "            ($BIN is not on your PATH; the menu entry still works)";; esac
fi
echo "Done. Rescan plugins in your host."
