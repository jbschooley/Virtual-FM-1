#!/bin/bash
# Builds the macOS installer from a finished build: one package per format,
# combined into an installer whose Installation Type step lets the user pick the
# Standalone app, the VST3 and the AU (all selected by default).
#   scripts/package-macos.sh <build dir> <output dir> <version>
# Signing and notarization happen in CI when the certificates are available
# (see .github/workflows/build.yml).
set -euo pipefail
export COPYFILE_DISABLE=1   # no AppleDouble ._ files in the payload

BUILD=${1:?build dir}
OUT=${2:?output dir}
VERSION=${3:?version}
ART="$BUILD/VirtualFM1_artefacts/Release"
NAME="Virtual FM-1"
ID=com.bockage.virtualfm1
WORK=$(mktemp -d)
mkdir -p "$OUT" "$WORK/pkgs" "$WORK/resources"

# component <choice id> <source bundle> <install folder>
component() {
    local choice=$1 src=$2 dest=$3
    local root="$WORK/root-$choice"
    mkdir -p "$root$dest"
    cp -R "$src" "$root$dest/"
    if [ -n "${MACOS_SIGN_IDENTITY:-}" ]; then
        codesign --force --deep --options runtime --timestamp --sign "$MACOS_SIGN_IDENTITY" "$root$dest/$(basename "$src")"
    fi
    # Bundles are "relocatable" by default: if one with the same id exists anywhere
    # on the disk (a build folder, say), Installer updates that copy instead of
    # installing where the package says. Turn that off.
    local plist="$WORK/$choice.plist"
    pkgbuild --analyze --root "$root" "$plist" > /dev/null
    plutil -convert xml1 "$plist"
    perl -0pi -e 's|(<key>BundleIsRelocatable</key>\s*)<true/>|$1<false/>|g' "$plist"
    if grep -A1 BundleIsRelocatable "$plist" | grep -q '<true/>'; then echo "could not make $choice non-relocatable"; exit 1; fi
    pkgbuild --root "$root" --component-plist "$plist" --identifier "$ID.$choice" --version "$VERSION" \
             --install-location / "$WORK/pkgs/$ID.$choice.pkg" > /dev/null
}

component app  "$ART/Standalone/$NAME.app"      /Applications
component vst3 "$ART/VST3/$NAME.vst3"           /Library/Audio/Plug-Ins/VST3
component au   "$ART/AU/$NAME.component"        /Library/Audio/Plug-Ins/Components
# component aax "$ART/AAX/$NAME.aaxplugin"      "/Library/Application Support/Avid/Audio/Plug-Ins"   (see docs/AAX.md)

# The license pane wraps text itself, so give it one line per paragraph: the
# GPL's own line breaks would otherwise split lines in odd places. Centered
# headings (deeply indented lines) keep their own lines.
awk '
    function flush() { if (p != "") print p; p = "" }
    /^[[:space:]]*$/ { flush(); print ""; next }
    /^          / { flush(); line = $0; sub(/^[[:space:]]+/, "", line); print line; next }
    { line = $0; sub(/^[[:space:]]+/, "", line); sub(/[[:space:]]+$/, "", line); p = (p == "" ? line : p " " line) }
    END { flush() }
' "$(dirname "$0")/../LICENSE" > "$WORK/resources/LICENSE.txt"
cat > "$WORK/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>$NAME $VERSION</title>
    <license file="LICENSE.txt"/>
    <!-- customize="always": the Installation Type step shows the checkboxes directly.
         rootVolumeOnly and no <domains>: everything goes on the startup disk, the only
         place plugins and apps are looked for. Any <domains> element, even with one
         domain, makes Installer ask "How do you want to install this software?". -->
    <options customize="always" rootVolumeOnly="true" require-scripts="false" hostArchitectures="x86_64,arm64"/>
    <choices-outline>
        <line choice="app"/>
        <line choice="vst3"/>
        <line choice="au"/>
    </choices-outline>
    <choice id="app" title="Standalone app" description="$NAME as an app, in /Applications." start_selected="true">
        <pkg-ref id="$ID.app"/>
    </choice>
    <choice id="vst3" title="VST3 plugin" description="For Gig Performer, Reaper, Cubase, Ableton Live and other VST3 hosts." start_selected="true">
        <pkg-ref id="$ID.vst3"/>
    </choice>
    <choice id="au" title="Audio Unit plugin" description="For Logic Pro, GarageBand, MainStage and other AU hosts." start_selected="true">
        <pkg-ref id="$ID.au"/>
    </choice>
    <pkg-ref id="$ID.app" version="$VERSION">$ID.app.pkg</pkg-ref>
    <pkg-ref id="$ID.vst3" version="$VERSION">$ID.vst3.pkg</pkg-ref>
    <pkg-ref id="$ID.au" version="$VERSION">$ID.au.pkg</pkg-ref>
</installer-gui-script>
EOF

PKG="$OUT/Virtual-FM-1-$VERSION-macOS.pkg"
productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK/pkgs" --resources "$WORK/resources" "$PKG"
if [ -n "${MACOS_INSTALLER_IDENTITY:-}" ]; then
    productsign --sign "$MACOS_INSTALLER_IDENTITY" "$PKG" "$PKG.signed" && mv "$PKG.signed" "$PKG"
fi
rm -rf "$WORK"
echo "wrote $PKG"
