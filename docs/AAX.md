# AAX (Pro Tools)

The build can make an AAX plugin, but it is off until signing is set up:

```
cmake -B build -DFM1_BUILD_AAX=ON
```

## The SDK

Nothing has to be downloaded. JUCE 9 ships the AAX SDK 2.9 in
`third_party/JUCE/modules/juce_audio_plugin_client/AAX/SDK`, and its
`LICENSE.txt` offers it under Avid's commercial terms or the GPLv3. This
project uses it under the GPLv3, like the rest of its code. With the option
on, the build was checked with JUCE 9.0.3 on macOS: it compiles and puts an
AAX build next to the AU and VST3 (category: software instrument). It has
not been loaded in Pro Tools yet.

## Making Pro Tools load it

Pro Tools only loads AAX plugins signed with PACE's tools, except the
developer edition:

1. **Pro Tools Developer** loads unsigned plugins, for testing. With an
   Avid developer account, ask `devauth@avid.com` for a Pro Tools Developer
   activation code, then download it from the developer account's AAX
   toolkit page.
2. **Signing for every Pro Tools.** Write to `audiosdk@avid.com` saying who
   you are, what the plugin is (with a link to the repository) and that you
   want to ship AAX. Once Avid agrees, PACE sets up an account for its Eden
   signing tools (`wraptool`); signing needs a physical iLok.

## When signing is available

- `CMakeLists.txt`: turn `FM1_BUILD_AAX` on (or pass it in CI).
- `scripts/package-macos.sh`: enable the commented `component aax ...` line,
  which installs to `/Library/Application Support/Avid/Audio/Plug-Ins`, and
  add an `aax` choice to the distribution.
- `scripts/installer.iss`: enable the commented `aax` component, installing
  to `{commoncf64}\Avid\Audio\Plug-Ins`.
- CI: sign with `wraptool` from repository secrets (the PACE account, and on
  macOS the Developer ID certificate), and check the installed `.aaxplugin`
  like the other formats. The host test does not load AAX; test in Pro Tools.

```
wraptool sign --verbose --account "$PACE_ACCOUNT" --password "$PACE_PASSWORD" \
    --wcguid "$PACE_WCGUID" --signid "$MACOS_SIGN_IDENTITY" \
    --in "Virtual FM-1.aaxplugin" --out "Virtual FM-1.aaxplugin"
```

(On Windows, `--keyfile` and `--keypassword` take the place of `--signid`.
The exact options come with PACE's documentation.)
