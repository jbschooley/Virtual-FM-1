# iOS and iPadOS

Virtual FM-1 builds for iOS as a standalone app and an AUv3 plugin from the
same source. It is not distributed yet: build it yourself with Xcode.

## Where it stands

Tried on an iPad Pro 11-inch (3rd generation) on iPadOS 26.6.1, and in the
iPad simulator:

- The app runs, with the editor filling the screen in portrait.
- Syncing with an FM-1 over USB-C works.
- It keeps playing in the background, behind other apps.
- **Settings > Audio and MIDI** opens the audio output, sample rate, buffer
  size and MIDI input settings. On the desktop these are under the app's
  Options button, which JUCE leaves out on iOS.

## Building

Needs Xcode with the iOS platform installed.

Simulator:

```sh
cmake -B build-ios-sim -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphonesimulator \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 -DFM1_COPY_PLUGIN=OFF
cmake --build build-ios-sim --config Debug --target VirtualFM1_Standalone -- -sdk iphonesimulator CODE_SIGNING_ALLOWED=NO
```

A device, signed with your Apple ID's team (Xcode > Settings > Accounts):

```sh
cmake -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 -DFM1_COPY_PLUGIN=OFF \
  -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM=<your team ID> -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGN_STYLE=Automatic
xcodebuild -project build-ios/VirtualFM1.xcodeproj -scheme VirtualFM1_Standalone -configuration Release \
  -destination "id=<device UDID>" -allowProvisioningUpdates -allowProvisioningDeviceRegistration build
xcrun devicectl device install app --device <device> "build-ios/VirtualFM1_artefacts/Release/Standalone/Virtual FM-1.app"
```

The first build for a device has to name it (`-destination "id=..."`) so
Xcode registers it with the team. With a free Personal Team the device must
trust the developer in Settings > General > VPN & Device Management, and an
install lasts 7 days. With **Connect via network** ticked for the device in
Xcode's Devices window, installs work over Wi-Fi, so the FM-1 can stay in the
iPad's USB-C port.

## Still to do

1. **Import and export.** The file pickers open, but the code takes their
   result as a file path. Files picked from the Files app, iCloud or a USB
   drive come as security-scoped URLs, so import and export probably fail
   outside the app's own folder. Read and write through JUCE's URL streams.
2. **The library in the Files app.** Turn on file sharing so the app's
   Documents folder shows in Files, to back up `library.fm1lib` and exports.
3. **Touch.** Shift/Cmd-click multi-select in the library needs a touch
   equivalent (a Select mode with checkboxes, say). Tooltips and anything
   behind hover or right-click need another way in.
4. **The AUv3.** Untested: load it in GarageBand or AUM, check it plays and
   saves with the project, and whether it can reach the FM-1 from inside the
   host.
5. **One library for the app and the AUv3.** They keep separate libraries
   now. Sharing one needs an App Group, which needs a paid developer account.
6. **iPhone.** The layout is the desktop one; iPad portrait fits, a phone
   needs its own.
7. **Distribution.** A free Personal Team installs for 7 days; a paid
   account ($99 a year) for a year. The App Store and TestFlight are a
   problem for a GPLv3 app: their terms are widely held to be incompatible
   with it, so shipping there would need JUCE under another license and
   the same for the rest of the code.
8. **CI.** A compile-only iOS job (no signing) on version tags, so desktop
   changes cannot break the iOS build unnoticed.
9. **Android.** Not started. JUCE builds a standalone app there (no plugin
   format); the iOS-only code paths are also enabled for Android but have
   never been built.
