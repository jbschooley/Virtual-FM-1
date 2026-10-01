# Virtual FM-1

A software M-VAVE FM-1: a Standalone app and VST3/AU plugin that plays the
FM-1's FM engine, edits every sound setting, runs its sequencer and
arpeggiator, and syncs presets and patterns with the hardware, much as
Yamaha's Expanded Softsynth Plugin does with the Montage M and MODX M. Pull or push the preset you're
playing, send an edit to the synth without saving it, or sync the whole
128-preset library in either direction.

It works on its own as a synth too; the FM-1 is only needed for syncing.

> Not affiliated with M-VAVE or with baud girl. Use at your own risk.

## What you need

- **macOS 11 or later** (Apple Silicon or Intel) or **Windows 10/11** (64-bit).
- To sync with a synth: an **M-VAVE FM-1** on USB running baud girl's
  **[FM-1+VA firmware](https://baudgirl.com/work/FM-1+VA)**. Two-way sync uses
  features only that firmware has. On M-VAVE's own firmware the plugin still
  plays and edits sounds, but cannot read anything back from the synth.

## Install

Download the installer for your system from the
[Releases](https://github.com/jbschooley/Virtual-FM-1/releases) page.

- **macOS**: open `Virtual-FM-1-…-macOS.pkg`. Under **Customize** you can
  choose the **Standalone app**, the **VST3 plugin** and the **Audio Unit
  plugin**; all three are installed by default. The installer is not signed
  yet, so macOS will say it cannot verify it: open **System Settings ›
  Privacy & Security** and click **Open Anyway**.
- **Windows**: run `Virtual-FM-1-…-Windows.exe` and choose the **Standalone
  app** and/or the **VST3 plugin**. Windows SmartScreen may warn about an
  unknown publisher: click **More info › Run anyway**.

Then rescan plugins in your host. It appears as **Virtual FM-1** by Bockage.
Logic needs a restart to pick up a new Audio Unit.

## Using it

1. Connect the FM-1 with USB and switch it on. The plugin finds it by itself;
   the Library & Sync tab shows its firmware version.
2. **Pull all 128** copies the synth's presets into the plugin. The library is
   shared by the Standalone and every plugin instance in every host, so you do
   this once.
3. Pick a preset in the list to play it. **Pull what the FM-1 is playing**
   loads the synth's current sound, unsaved changes included.
4. Edit in the **FM Editor** and **Effects & Envelope** tabs. Changes stay
   unsaved until **Store**, as on the FM-1; **Revert** drops them.
5. **Send to FM-1** plays your sound on the synth without saving it there;
   **Live** sends every change as you make it. **Store to FM-1** saves the
   preset on the synth. **Push changed** writes every preset that differs.
6. The **Sequencer** tab pulls and pushes patterns; **Arpeggiator** works like
   the FM-1's. Both follow your host's tempo and transport.

Every sound setting is a host parameter, so it can be automated, and a host
project saves its own current preset, edits, patterns and arpeggiator.

## What's in it

- **Engine**: the same FM engine the FM-1 uses (Google's msfa, the core of
  Dexed), 12 voices, matched to the hardware where it was measured: pitch,
  per-preset transpose, velocity response, detune and LFO speed.
- **FM Editor** in Dexed's layout: six operators, algorithm diagram, pitch
  envelope, LFO.
- **Effects and envelope**: the FM-1's filter, reverb, delay, distortion,
  chorus and phaser in each preset's chain order, its global ADSR, and the
  per-note filter FM presets gained in FM-1_092. Settings sync exactly; the
  effects' sound is an approximation of the firmware's.
- **Sequencer**: 16 patterns of up to 64 steps, up to nine notes a step,
  ratchet, chance, gate, transpose, accent, Tie & Slide per step and per
  note, per-pattern Chain, swing, step recording and real-time recording.
- **Arpeggiator**: Up, Down, Inclusive, Exclusive, Random, Order and Repeat
  over 1 to 4 octaves.
- **Sync**: identify, pull and push one preset or all 128, push only what
  changed, send unsaved edits, show a preset on the synth, `.syx` import and
  export (FM-1+VA backups, DX7 banks and voices).
- **MIDI out**: what the sequencer and arpeggiator play leaves the plugin as
  MIDI, so a host can route it to the synth as well.

## Not done yet

- **Virtual Analog presets** sync but play through the FM engine. baud girl
  has announced her firmware's source; the VA engine waits for it.
- Some settings have no MIDI control on the synth (the FM filter, distortion
  type, effect order), so they reach it only with **Store to FM-1**.
- Ties, ratchet, chance and accent stay in the plugin; how the synth stores
  them is not known yet. Per-pattern Chain can be read from the synth but not
  written.
- Knob assignments and mono/glide are kept as stored but not edited.
- **AAX** (Pro Tools) is planned; see [`docs/AAX.md`](docs/AAX.md).

[`docs/FIRMWARE-GAPS.md`](docs/FIRMWARE-GAPS.md) lists each gap and what to
look for in baud girl's source once it is published.

## Build

Requires CMake 3.22+, a C++20 compiler and Ninja on macOS (`brew install cmake
ninja`), or Visual Studio 2022 on Windows. JUCE 8.0.8 is a submodule:

```
git clone --recurse-submodules https://github.com/jbschooley/Virtual-FM-1
cd Virtual-FM-1
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

The plugins are copied into your user plug-in folders after building
(`-DFM1_COPY_PLUGIN=OFF` to skip). `scripts/package-macos.sh` and
`scripts/installer.iss` build the installers. CI builds both on every push,
installs them on clean macOS and Windows machines and checks the result, and
attaches them to a draft release for every `v*` tag.

Source layout:

- `engine/` msfa (Apache-2.0, from Dexed; changes in `engine/msfa/NOTICE.md`)
  and the voice, envelope and filter code around it
- `sync/` the FM-1 protocols (sounds, patterns, settings record, edit buffer),
  the MIDI link and the sync operations; no JUCE in the protocol code
- `plugin/` the processor, editor, parameters, effects, sequencer and
  arpeggiator; `plugin/dexed_ui/` holds Dexed's look-and-feel and panels
- `tests/` protocol tests against vectors generated from baud girl's own code,
  engine, sequencer and parameter tests, an end-to-end sync test against a
  simulated FM-1, a host test that loads the built plugin like a DAW, and the
  hardware probe used to study the synth
- `docs/` [hardware notes](docs/HARDWARE-NOTES.md),
  [firmware gaps](docs/FIRMWARE-GAPS.md), [AAX plan](docs/AAX.md)

## Credits

- **baud girl** for the [FM-1+VA firmware](https://baudgirl.com/work/FM-1+VA),
  whose read-back protocol makes two-way sync possible.
- **Google's music-synthesizer-for-android** (msfa) and **Dexed** by Pascal
  Gauthier and contributors, for the FM engine and the editor's look.
- Reverse-engineering work on the FM-1 by
  [AL-255](https://github.com/AL-255/FM-1-RE),
  [ip2k](https://github.com/ip2k/mvave-fm1-open-firmware),
  [aroum](https://github.com/aroum/fm1-custom-fw) and
  [kagaimiq](https://github.com/kagaimiq/jl-misctools).
- Built with [JUCE](https://juce.com).

## License

GPL-3.0-or-later (see `LICENSE`). Third-party code and its licenses are listed
in [`THIRD_PARTY.md`](THIRD_PARTY.md): msfa is Apache-2.0, the Dexed UI parts
are GPL-3.0-or-later, JUCE is used under the AGPLv3 and the VST3 SDK under the
GPLv3.
