# FM-1 Companion

A VST3 / AU / Standalone instrument that plays the M-VAVE FM-1's FM engine in
software, edits every sound setting, runs the FM-1+VA sequencer and
arpeggiator, and syncs presets and patterns with the hardware the way Yamaha's
MODX Connect works with a MODX: pull or push the current preset, or the whole
128-preset library, in either direction.

The FM-1's stock engine is Google's msfa (the Dexed core). The FM-1+VA custom
firmware by baud girl (https://baudgirl.com/work/FM-1+VA) adds a read-back
protocol; this plugin speaks it. Stock M-VAVE firmware can only *receive* DX7
dumps, so two-way sync needs FM-1+VA (FM-1_020 or later).

## What it does

- **Engine**: msfa, 12 voices, DX7 VCED patches, sustain, pitch bend, mod
  wheel, breath, foot, aftertouch, the FM-1's global ADSR envelope. Algorithm
  table matches the FM-1 firmware (Google's original rows 4 and 6, which
  differ from Dexed).
- **Editor**: all 145 DX7 voice parameters, the six effects (on, type, three
  parameters each) and the envelope are host parameters: automatable, saved
  with the session. The FM Editor uses Dexed's operator and global layouts,
  look-and-feel, envelope displays and algorithm diagram (GPL-3.0-or-later,
  see `plugin/dexed_ui/NOTICE.md`); click an operator's number to switch it
  off while auditioning. The preset name is edited there too.
- **Effects**: filter, reverb, delay, distortion, chorus, phaser in the chain
  order stored with the preset, built on juce::dsp. The values sync exactly
  with the synth; the sound is an approximation of the firmware's.
- **Sequencer**: 16 patterns, up to 64 steps, up to nine notes a step, per-step
  note value, ratchet, gate, chance, transpose, accent and slide, pattern
  chaining, swing, step recording from the keyboard, host transport and tempo
  sync. Pull and push patterns with the synth (notes, velocities, note values,
  length, tempo, gate, swing and preset; the per-step extras stay in the plugin
  until the firmware's layout for them is published).
- **Arpeggiator**: Up, Down, Inclusive, Exclusive, Random, Order, Repeat over
  1 to 4 octaves, note value, gate, swing, latch, host sync.
- **Sync**: identify, pull/push current preset, pull all 128, push changed,
  push all, show a preset on the synth, `.syx` import/export in FM-1+VA's own
  format plus DX7 banks and voices, DX7 SysEx from the host.
- **MIDI out**: what the sequencer and arpeggiator play leaves the plugin as
  MIDI, so a host can route it to the hardware too.

Not done yet:

- **Virtual Analog engine.** VA presets sync and are marked, but play through
  the FM engine. Baud girl's source is announced as GPL-3.0-or-later and not
  yet published; the VA DSP is either ported from that or reverse engineered.
- **"Current preset on the synth."** The plugin's current slot is what it
  pushes and pulls; it does not yet learn which preset the FM-1 is showing.
  The firmware's memory-read command makes this findable with the hardware.
- Knob assignments, mono/glide, and the VA bytes of the record are stored and
  synced but not edited. Baud girl's four Dexed fixes are not applied yet.

## Install

Installers for macOS (VST3, AU, Standalone; universal) and Windows (VST3,
Standalone) are built by GitHub Actions on every push and attached to a
release for every `v*` tag. The macOS package is unsigned unless signing
secrets are configured, so on first open use right-click, Open (or
`xattr -d com.apple.quarantine` on the .pkg). For Gig Performer, rescan
plugins and add "FM-1 Companion" (VST3).

## Build

Requires CMake and a C++20 compiler; on macOS also Ninja (`brew install cmake
ninja`), on Windows Visual Studio 2022. JUCE 8.0.8 is a submodule:

```
git clone --recurse-submodules https://github.com/jbschooley/Virtual-FM-1
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build
ctest --test-dir build
```

The VST3 and AU are copied into the user plug-in folders after the build
(`-DFM1_COPY_PLUGIN=OFF` to skip). `scripts/package-macos.sh` builds the .pkg
and `scripts/installer.iss` the Windows installer, as CI does.

## Using it

1. Connect the FM-1 over USB and switch it on.
2. Library & Sync tab: press **Find FM-1** (or pick its ports and **Connect**).
   The firmware version appears; FM-1+VA enables everything, stock enables
   nothing but receiving DX7 dumps.
3. **Pull all 128** copies the synth's presets into the plugin (about 15 s).
4. Select a preset in the list to play it. **Pull current** / **Push current**
   move one preset. **Push changed** writes only the slots marked `*`.
5. **Show on FM-1** sends a program change so the synth shows the same preset.
6. Sequencer tab: **Pull patterns from FM-1** / **Push patterns to FM-1**.

## Layout

- `engine/msfa` msfa (Apache-2.0, from Dexed) with the changes listed in its `NOTICE.md`
- `engine/FmSynth` voice allocation, global envelope and rendering around msfa
- `sync/Fm1Codec` the sound protocol, no JUCE
- `sync/Fm1Seq` the pattern protocol
- `sync/Fm1Record` the effects and envelope bytes of the settings record
- `sync/Fm1Link` MIDI ports, request/reply with retries
- `sync/Fm1Session` the sync operations on a worker thread
- `plugin/` JUCE processor, editor tabs, parameters, effects, sequencer, arpeggiator
- `plugin/dexed_ui/` Dexed's look-and-feel, displays and images, and the operator/global panels built on them
- `tests/` codec test against `golden.json`, engine smoke test, sequencer test,
  end-to-end sync test over virtual MIDI ports against a fake FM-1
- `tests/gen_golden.mjs` regenerates `golden.json` from baud girl's web modules

## Record layout

The 59-byte settings record that travels with each preset is interpreted as
described in `sync/Fm1Record.h`. That layout is inferred from baud girl's
comments and from her 16-preset Virtual Analog pack, not from published
source; the bytes for Virtual Analog settings and knob assignments are kept
as they are.

## License

GPL-3.0-or-later (see `LICENSE`). Third-party components and their licenses
are listed in `THIRD_PARTY.md`: msfa is Apache-2.0, JUCE is used under the
AGPLv3, the VST3 SDK under the GPLv3. The AAX format is not built because
Avid's SDK terms do not allow GPL distribution.

## Finding the synth's current preset

FM-1+VA's memory-read command returns up to 256 bytes of RAM. The sequencer's
settings block is at `0x01C0E840 + 5816`; the preset number the synth shows is
not yet located. A probe that reads a window repeatedly while presets are
changed on the synth will find it; that is the next sync feature.
