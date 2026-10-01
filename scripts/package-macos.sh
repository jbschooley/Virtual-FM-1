#!/bin/bash
# Builds an unsigned macOS installer package from a finished build.
#   scripts/package-macos.sh <build dir> <output dir> <version>
# Installs the VST3 and AU into /Library/Audio/Plug-Ins and the Standalone
# into /Applications. Signing and notarization happen in CI when the
# certificates are available (see .github/workflows/build.yml).
set -euo pipefail
export COPYFILE_DISABLE=1   # no AppleDouble ._ files in the payload

BUILD=${1:?build dir}
OUT=${2:?output dir}
VERSION=${3:?version}
ART="$BUILD/VirtualFM1_artefacts/Release"
STAGE=$(mktemp -d)
NAME="Virtual FM-1"

mkdir -p "$STAGE/Library/Audio/Plug-Ins/VST3" "$STAGE/Library/Audio/Plug-Ins/Components" "$STAGE/Applications" "$OUT"
cp -R "$ART/VST3/$NAME.vst3" "$STAGE/Library/Audio/Plug-Ins/VST3/"
cp -R "$ART/AU/$NAME.component" "$STAGE/Library/Audio/Plug-Ins/Components/"
cp -R "$ART/Standalone/$NAME.app" "$STAGE/Applications/"

if [ -n "${MACOS_SIGN_IDENTITY:-}" ]; then
    for b in "$STAGE/Library/Audio/Plug-Ins/VST3/$NAME.vst3" "$STAGE/Library/Audio/Plug-Ins/Components/$NAME.component" "$STAGE/Applications/$NAME.app"; do
        codesign --force --deep --options runtime --timestamp --sign "$MACOS_SIGN_IDENTITY" "$b"
    done
fi

PKG="$OUT/Virtual-FM-1-$VERSION-macOS.pkg"
# Bundles in a package are "relocatable" by default: if a bundle with the same id
# exists anywhere on the disk (a build folder, say), Installer updates that copy
# instead of installing where the package says. Turn that off for every bundle.
PLIST="$(mktemp -d)/components.plist"
pkgbuild --analyze --root "$STAGE" "$PLIST"
plutil -convert xml1 "$PLIST"
perl -0pi -e 's|(<key>BundleIsRelocatable</key>\s*)<true/>|$1<false/>|g' "$PLIST"
if grep -A1 BundleIsRelocatable "$PLIST" | grep -q '<true/>'; then echo "could not make the bundles non-relocatable"; exit 1; fi
pkgbuild --root "$STAGE" --component-plist "$PLIST" --identifier com.bockage.virtualfm1 --version "$VERSION" --install-location / "$PKG"
rm -f "$PLIST"

if [ -n "${MACOS_INSTALLER_IDENTITY:-}" ]; then
    productsign --sign "$MACOS_INSTALLER_IDENTITY" "$PKG" "$PKG.signed" && mv "$PKG.signed" "$PKG"
fi
rm -rf "$STAGE"
echo "wrote $PKG"
