#!/bin/bash
# Builds the Linux package from a finished build: a .tar.gz holding the
# Standalone app, the VST3 and the LV2 with an install script that lets the
# user choose which to install, for their own account or the whole system.
#   scripts/package-linux.sh <build dir> <output dir> <version>
set -euo pipefail

BUILD=${1:?build dir}
OUT=${2:?output dir}
VERSION=${3:?version}
ART="$BUILD/VirtualFM1_artefacts/Release"
HERE="$(cd "$(dirname "$0")" && pwd)"
NAME="Virtual FM-1"
DIR="Virtual-FM-1-$VERSION"
WORK=$(mktemp -d)
mkdir -p "$OUT" "$WORK/$DIR"

cp -R "$ART/LV2/$NAME.lv2" "$WORK/$DIR/"
cp -R "$ART/VST3/$NAME.vst3" "$WORK/$DIR/"
cp "$ART/Standalone/$NAME" "$WORK/$DIR/virtual-fm1"
cp "$HERE/../assets/icon-256.png" "$WORK/$DIR/virtual-fm1.png"
cp "$HERE/../LICENSE" "$WORK/$DIR/LICENSE.txt"
cp "$HERE/linux-install.sh" "$WORK/$DIR/install.sh"
cp "$HERE/linux-uninstall.sh" "$WORK/$DIR/uninstall.sh"
chmod +x "$WORK/$DIR/install.sh" "$WORK/$DIR/uninstall.sh" "$WORK/$DIR/virtual-fm1"

cat > "$WORK/$DIR/README.txt" <<EOF
Virtual FM-1 $VERSION for Linux (x86-64)

Install for your own account (no root needed):

    ./install.sh

or for everyone on this computer:

    sudo ./install.sh --system

All three parts are installed unless you leave some out with --no-app,
--no-vst3 or --no-lv2. ./uninstall.sh (or sudo ./uninstall.sh --system)
removes them again.

  Standalone app   ~/.local/bin/virtual-fm1, with a menu entry
  VST3             ~/.vst3/Virtual FM-1.vst3
  LV2              ~/.lv2/Virtual FM-1.lv2
  (--system: /usr/local/bin, /usr/local/lib/vst3, /usr/local/lib/lv2)

Rescan plugins in your host afterwards. Syncing with an FM-1 uses ALSA MIDI;
see https://github.com/jbschooley/Virtual-FM-1 for the rest.
EOF

TAR="$OUT/Virtual-FM-1-$VERSION-Linux-x86_64.tar.gz"
tar -C "$WORK" -czf "$TAR" "$DIR"
rm -rf "$WORK"
echo "wrote $TAR"
