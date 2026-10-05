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
| Felucca | identity version 900 or later (release X.Y is FM-1_9XY: 1.0 is FM-1_910) | Its own editor protocol, through `firmwares/felucca/FeluccaSync` rather than the profile's features: full backup and restore (Pull, Send) and live sync |

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

## Versions

Each firmware's releases the plugin knows are listed in `knownVersions()`
(`firmwares/Firmwares.cpp`), oldest first, each with its identity number, its
author's name for it, how well the plugin works with it, and a note:

| Support | Means |
|---|---|
| Current | the release the plugin plays and syncs with (one per firmware), tried on an FM-1 |
| Tested | tried on an FM-1 |
| Older | should work, with what the note says is missing; not tried |
| Deprecated | syncing needs a newer release; the note says which and how to update (Felucca's Pull, Send and Live are off for it) |

`checkVersion()` says what the plugin makes of a connected synth's release, and
the editor shows it when the synth is not on a current or tested one. A release
the list does not have is never refused: a newer one is synced as the newest
the plugin knows and said to be untested; an older one is said to be older.
Felucca is supported from 1.0: any Felucca before it (the 0.x betas) is
treated as Deprecated, with Felucca's installer to update it.

**When a firmware releases a new version:**

1. Add a line to its list. If it changes nothing the plugin uses, make it
   Current and the previous one Tested once both have been tried on an FM-1.
2. If its protocol changed, change the profile (`firmwares/<id>/`), keyed on
   `version` where the old releases still need the old way.
3. For Felucca, whose sound the plugin builds from source: update the vendored
   copy (`engines/felucca/UPSTREAM.md`). Keep the old release built beside
   the new one (not done yet: there has been one release) when the new one is
   not backwards compatible:
   - its golden renders changed (the sound changed), or
   - a parameter changed meaning, range or default, or was removed, so old
     values would play differently, or
   - a factory preset changed or moved, so a project naming it would get a
     different sound.
   Parameters only added (with defaults that change nothing) and presets only
   added are compatible: then the new release replaces the old one.
   `felucca_test` checks the last two: `tests/felucca-frozen.txt` keeps every
   parameter (by label: range, default, value names), each engine's eight, the
   globals and every factory preset (a hash of the sound it loads). A change
   or removal fails it; new ones are appended with
   `FELUCCA_APPEND_FROZEN=1 felucca_test`. Projects say which release they
   were made for (`firmwareVersion`) and the plugin says so when it plays
   another; Felucca reads its own older project formats itself, and music a
   Felucca cannot read (from a newer one) is kept as it was and saved again
   (in the project file only: it is not played or sent to a synth).
4. To retire an old release, mark it Deprecated with a note saying what to
   update to. It is still recognised; only its note changes what is said.

## What profiles do not cover yet

- **Sound engines.** The plugin plays FM presets with msfa. A firmware with
  other engines (Virtual Analog, samples, macro models) needs those engines in
  the plugin too, chosen per preset.
- **The interface.** Tabs and controls that only make sense for one firmware
  should show only when its profile says so.
