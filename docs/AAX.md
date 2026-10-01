# Adding AAX (Pro Tools)

AAX is not built yet. Avid offers the AAX SDK under the GPLv3 as well as a
commercial license, and this project is GPL-3.0-or-later with JUCE under the
AGPLv3, so an AAX build can be distributed under the GPL as long as its source
stays published. Pro Tools itself (other than Pro Tools Developer) only loads
AAX plugins signed with PACE's tools.

## Prerequisites (one time)

1. Create an Avid developer account and join the AAX developer program
   (developer.avid.com). This involves Avid's developer agreement/NDA.
2. Download the AAX SDK. Use it under its GPLv3 option.
3. Request PACE signing access through the program (PACE "Eden" tools,
   `wraptool`) and get a signing account and a code-signing certificate (on
   macOS, a Developer ID Application certificate; on Windows, a code-signing
   certificate).
4. For testing before signing: Pro Tools Developer (free with the program)
   loads unsigned AAX builds.

## Build changes (to make once the SDK is in hand)

- `CMakeLists.txt`: an `FM1_BUILD_AAX` option that calls
  `juce_set_aax_sdk_path(<path>)` and adds `AAX` to the formats. The AAX
  category is a software instrument (`AAX_CATEGORIES AAX_ePlugInCategory_SWGenerators`).
  The plugin and manufacturer codes stay `Fm1c` / `Jscn`.
- `scripts/package-macos.sh`: the commented `component aax ...` line installs
  to `/Library/Application Support/Avid/Audio/Plug-Ins`, plus an `aax` choice
  in the distribution.
- `scripts/installer.iss`: the commented `aax` component installs to
  `{commoncf64}\Avid\Audio\Plug-Ins`.
- CI: the SDK cannot be committed (its download terms), so CI fetches it from
  a private location given as a secret, builds AAX only when the secret is
  present, and signs with `wraptool sign` using the PACE account and
  certificate from secrets. The host test does not load AAX; check it in Pro
  Tools.

## Signing in CI, in short

```
wraptool sign --verbose --account "$PACE_ACCOUNT" --password "$PACE_PASSWORD" \
    --wcguid "$PACE_WCGUID" --signid "$MACOS_SIGN_IDENTITY" \
    --in "Virtual FM-1.aaxplugin" --out "Virtual FM-1.aaxplugin"
```

(on Windows, `--keyfile` and `--keypassword` take the place of `--signid`).
The exact options come with PACE's documentation once access is granted.
