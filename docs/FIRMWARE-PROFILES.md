# Firmware profiles

Virtual FM-1 talks to different FM-1 firmwares through profiles
(`core/Firmware.h`). A profile says what a firmware can do and implements the
protocol for each thing it can do. `Fm1Session` runs the operations (threading,
progress, cancelling, pacing between writes, checking a write by reading it
back) and asks the profile for everything that depends on the firmware.

| Profile | Chosen when | Can do |
|---|---|---|
| FM-1+VA | identity version 20 to 899 | Everything: presets, patterns, current sound, unsaved edits checked by read-back; GLOBE settings on FM-1_093 and FM-1_094 |
| M-VAVE | identity version 19 or earlier | Nothing read back; sends unsaved edits unchecked |
| Felucca | identity version 900 or later (0.4 beta is FM-1_904) | Nothing: its SysEx covers identity and firmware updates only |

## Adding a firmware

1. **Identify it.** `firmwareFor()` in `firmwares/Firmwares.cpp` picks the profile
   from the synth's identity. If the firmware answers M-VAVE's updater
   handshake with its own name, match on that; if it answers a Hello as in
   [`PROTOCOL-PROPOSAL.md`](PROTOCOL-PROPOSAL.md), match on its name there.
2. **Write the profile**: a subclass of `fm1::Firmware` with
   - `name()` and `summary()` for the identity line,
   - `has(Feature)` for what it can do, and `cannot(Feature)` for the sentence
     shown when something is missing,
   - the operations it supports: `readPreset`, `writePreset`, `readPattern`,
     `writePattern`, `readCurrent`, `readLive`, `readGlobals`, `editMessages`,
     `editChanges` (live editing),
   - `writePaceMs()` if writes need spacing.
   Operations get a `Port` with the MIDI link, a cancel check and a progress
   callback. They return nothing (or false) with a reason on failure.
3. **Map its data** to the plugin's: `fm1::Sound` for presets and
   `fm1::seq::Pattern` for patterns. A firmware with a different preset format
   needs that format added to the plugin's data model first, so presets that
   are not DX7 voices can be stored (as raw bytes at least, which the JSON
   format already supports) and played by an engine that understands them.
4. **Test it**: `tests/sync_test.cpp` runs the session against a simulated
   FM-1+VA on virtual MIDI ports; a new profile gets a simulated firmware the
   same way, and a check against the real synth with `tests/fm1_probe.cpp`.

## What profiles do not cover yet

- **Sound engines.** The plugin plays FM presets with msfa. A firmware with
  other engines (Virtual Analog, samples, macro models) needs those engines in
  the plugin too, chosen per preset.
- **The interface.** Tabs and controls that only make sense for one firmware
  should show only when its profile says so.
