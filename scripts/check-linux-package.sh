#!/bin/bash
# Checks the Linux package the way a user would use it: unpack, install for a
# user (all parts, then only the LV2), load the installed plugins in the host
# test, uninstall, then the same system-wide when run as root. Needs a display
# (DISPLAY set, with a window manager) for the host test.
#   scripts/check-linux-package.sh <tar.gz> <host_test>
set -euo pipefail
TAR=$(realpath "${1:?package}")
HOST=$(realpath "${2:?host_test}")
WORK=$(mktemp -d)
tar -C "$WORK" -xzf "$TAR"
PKG=$(echo "$WORK"/Virtual-FM-1-*)

export HOME="$WORK/home"; mkdir -p "$HOME"
"$PKG/install.sh"
for f in "$HOME/.lv2/Virtual FM-1.lv2/manifest.ttl" "$HOME/.vst3/Virtual FM-1.vst3/Contents/x86_64-linux/Virtual FM-1.so" \
         "$HOME/.local/bin/virtual-fm1" "$HOME/.local/share/applications/virtual-fm1.desktop"; do
    test -e "$f" || { echo "missing after install: $f"; exit 1; }
done
"$HOST" "$HOME/.vst3/Virtual FM-1.vst3" 1
LV2_PATH="$HOME/.lv2" "$HOST" "urn:bockage:virtualfm1" 1
"$PKG/uninstall.sh"
test ! -e "$HOME/.lv2/Virtual FM-1.lv2" && test ! -e "$HOME/.vst3/Virtual FM-1.vst3" && test ! -e "$HOME/.local/bin/virtual-fm1" \
    || { echo "uninstall left files behind"; exit 1; }

"$PKG/install.sh" --no-app --no-vst3
test -e "$HOME/.lv2/Virtual FM-1.lv2" && test ! -e "$HOME/.vst3/Virtual FM-1.vst3" && test ! -e "$HOME/.local/bin/virtual-fm1" \
    || { echo "choosing only the LV2 did not work"; exit 1; }
"$PKG/uninstall.sh"
echo "user install, choices and uninstall: ok"

if [ "$(id -u)" = 0 ]; then
    "$PKG/install.sh" --system
    test -e "/usr/local/lib/lv2/Virtual FM-1.lv2" && test -e "/usr/local/lib/vst3/Virtual FM-1.vst3" && test -x /usr/local/bin/virtual-fm1 \
        || { echo "system install incomplete"; exit 1; }
    LV2_PATH=/usr/local/lib/lv2 "$HOST" "urn:bockage:virtualfm1" 1
    "$PKG/uninstall.sh" --system
    test ! -e "/usr/local/lib/lv2/Virtual FM-1.lv2" && test ! -e /usr/local/bin/virtual-fm1 || { echo "system uninstall left files"; exit 1; }
    echo "system install and uninstall: ok"
fi
rm -rf "$WORK"
