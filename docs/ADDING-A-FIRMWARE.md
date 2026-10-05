# Adding a firmware

Virtual FM-1 plays and syncs with several FM-1 firmwares: M-VAVE's own, baud
girl's FM-1+VA and Felucca. Each instance is set to one (the dropdown at the
top), saved with the project. This page is for someone adding another, or a
new release of one of these. [`CONTRIBUTING.md`](CONTRIBUTING.md) covers
building, the tests and the rules for working with a real FM-1.

## How the pieces fit

| Folder | What | JUCE |
|---|---|---|
| `core/` | `Fm1Link`: the MIDI ports, a request and its reply with retries, a listener for every SysEx frame. `Fm1Session`: a thread that runs one operation at a time with progress and cancel, and `job()` for a firmware's own. `Firmware.h`: the profile interface. | yes |
| `firmwares/` | `Firmwares.{h,cpp}`: the choices in the dropdown, which firmware an identity belongs to, each firmware's known releases. One folder per firmware with its profile and sync; `fm1_common/` the FM-1 protocol code stock and FM-1+VA share. | `fm1_common/` no |
| `engines/` | Sound: `dx7/` (msfa, the FM-1's FM engine), `fm1_fx/` (its effects, the Hardware character stage, the rate converter), `felucca/` (Felucca's own firmware, built from its source). | no |
| `plugin/` | The processor and editor; per firmware its panel, host parameters and stored data (`Felucca*`). | yes |

| Firmware | Identity (M-VAVE's updater handshake) | Sync | Sound |
|---|---|---|---|
| M-VAVE (stock) | `FM-1_019` or earlier (V15 is `FM-1_015`) | profile: takes DX7 voices and parameter changes, reads nothing back | `engines/dx7` |
| FM-1+VA | `FM-1_020` to `FM-1_899` | profile: presets, patterns, the current sound, GLOBE settings (`F0 43 00 7D`) | `engines/dx7` (VA presets play as FM until her VA engine is public) |
| Felucca | `FM-1_900` and up (release X.Y is `FM-1_9XY`) | its editor protocol (`F0 7D 46 4C`): full backup and restore, live sync, through `firmwares/felucca/FeluccaSync` | its own firmware, `engines/felucca` |

## Steps

1. **A choice.** Add it to `firmwareChoices()` in `firmwares/Firmwares.cpp`:
   its id (saved in projects: never change it), its name, whether presets can
   use baud girl's VA engine, whether the plugin can play it. Add its releases
   to `knownVersions()` (below), with exactly one `Current` (`plugin_checks`
   checks it).
2. **Recognise it.** `firmwareIdFor()` picks the firmware from the identity
   the synth gives M-VAVE's updater handshake (`FM-1_093`). A firmware with
   its own identity range or name gets a line there. If its MIDI port has its
   own name, add it to `Fm1Link::findFm1()` (Felucca's is "Felucca"; its update
   loader's, "Felucca Update", must never be picked).
3. **Sync**, one of two ways:
   - **A profile**, for FM-1-style preset and pattern sync: a subclass of
     `fm1::Firmware` with `name()`, `summary()`, `has(Feature)` and
     `cannot(Feature)`, and the operations it supports (`readPreset`,
     `writePreset`, `readPattern`, `writePattern`, `readCurrent`, `readLive`,
     `readGlobals`, `editMessages`, `editChanges`, `writePaceMs()`). Each gets
     a `Port` (the link, a cancel check, progress) and returns nothing, or
     false, with a reason. `Fm1Session` runs them, paces writes and checks a
     write by reading it back.
   - **Its own sync**, for a firmware whose protocol is different in kind:
     code under `firmwares/<id>/` run as session jobs (`Fm1Session::job`).
     `FeluccaSync` is the example: an `Endpoint` interface with one class for a
     real synth over the link and one for the plugin's own Felucca, so the
     same backup, restore and live mirror code works both ways and is tested
     with two simulated ends.

   Either way, send only the firmware's own request frames. Never send an
   update or boot loader message, and back up what a write replaces first
   (Felucca's Send saves the synth's own objects to the library before
   writing).
4. **Sound.** An engine under `engines/<id>/`: plain C or C++ with no JUCE, so
   its tests stay small. It may run at its own rate: the plugin converts
   (`RateConverter`) and reports the latency, as for Felucca at 44.1 kHz. A
   firmware whose source is C with file-level state, as Felucca's is, can be
   compiled several times with a prefix per copy, each instance playing in
   one; with more instances than copies, they share one and each one's state
   is saved out and put back. `engines/felucca/felucca_core.c` and its
   `UPSTREAM.md` show how (Clang only: the copies' state lives in sections of
   their own).
5. **What it saves.** `FM1Processor::getStateInformation` saves the firmware
   id, the release it was made for (`firmwareVersion`) and a child per
   firmware with its state (Felucca's: the music as Felucca saves a project).
   What the synth itself keeps (Felucca's project slots, user presets, FM6
   bank) goes in the library folder, shared by every instance
   (`plugin/FeluccaDevice`: the file Felucca's web editor writes).
6. **Host parameters**, so a host can automate it: stable ids named after the
   firmware's own parameter names, never its numbering (`plugin/FeluccaParams`),
   with a version hint. `tests/params-frozen.txt` keeps every released id:
   append new ones with `FM1_APPEND_FROZEN=1 params_test`; a released id never
   changes or goes.
7. **Its editor.** `FM1Editor::showFirmware` shows the firmware's panel when
   an instance is set to it (`FeluccaPanel`), or the FM-1 tabs.
8. **Tests**, in `CONTRIBUTING.md`'s list: the sync against a simulated synth
   (`sync_test` for profiles; `plugin_test` runs Felucca's sync between two of
   the plugin's Felucca copies), the engine's own (`felucca_test`), and a check
   with the real synth through `tests/fm1_probe.cpp`.

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
Felucca is supported from 1.0: any Felucca release before it (the 0.x betas,
FM-1_901 to 909) is treated as Deprecated, with Felucca's installer to update
it; a build that is not a release (FM-1_900) is Older.

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

## Not covered yet

- **baud girl's VA engine.** VA presets sync but play through the FM engine
  until her source is published.
- **A generic editor.** A firmware without a panel of its own has none yet;
  one built from the parameters a firmware describes (Felucca's `DESC`) is
  planned.
- **Several releases of one firmware built side by side** (above): not needed
  yet.
