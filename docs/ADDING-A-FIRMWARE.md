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
   firmware whose source is C with all its state in file-level statics, as
   Felucca's, SLOOP's and Melodee's are, is compiled once and its state moved
   into a block each instance owns: see "A firmware built from its C source"
   below. It must follow those rules, and so must every update of it.
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
   the plugin's Felucca instances), the engine's own (`felucca_test`), and a
   check with the real synth through `tests/fm1_probe.cpp`.

## A firmware built from its C source (Felucca, SLOOP, Melodee)

These firmwares keep all their state in file-level and function-local statics:
compiled as written, one copy of the code is one synth. The plugin compiles each
once and gives every instance its own copy of the state instead:

- `engines/<kind>/<kind>_core.c` (the "core") includes the firmware's sources,
  unchanged, as one compilation unit, replaces the hardware, and exposes the
  API in `engines/felucca/felucca_core.h`.
- `engines/felucca/fel_state.py` rewrites the core's LLVM IR (CMakeLists.txt
  `fm1_core`, through `fel_state_build.py`): every writable variable marked by
  the core's section pragma becomes a field of one struct, reached through a
  thread-local pointer. Each instance owns one such block (`state_size()`,
  `state_init()`), and `FeluccaEngine` points the code at it (`bind()`) under
  the instance's lock before every call. So any number of instances play at
  once, on any threads, with one copy of the code, at a few percent of CPU on
  the firmware's own work.

**Adding such a firmware, or updating one (a new upstream release), needs:**

1. **Every writable variable inside the pragma.** The core opens
   `FEL_SECTIONS(FEL_BSS_SECTION, FEL_DATA_SECTION)` before the firmware's
   first include and closes it (`#pragma clang section bss = "" data = ""`)
   after its own last variable. A writable variable outside that range, or in
   another compiled file, is one variable shared by every instance: a bug the
   compiler does not report. Only read-only data may live elsewhere (the sample
   sets in `<kind>_shared.c`, written by CMakeLists.txt). When an update adds a
   source file, include it inside the range, in the order upstream's own unity
   file (felucca.c, melodee.c ...) does.
2. **Upstream's attributes stripped**: `#define __attribute__(x)` before the
   firmware's includes (so its `section`, `used`, `aligned` ... attributes do
   not fight the pragma) and `#undef __attribute__` after them.
3. **Clang**, with no `-g` for the core (debug information would name the
   variables the rewrite removes), and Python 3 for the build.
4. **What `fel_state.py` handles**: any use of a variable inside a function,
   directly or inside constant expressions; a variable initialized with the
   address of another (a pointer or an array of pointers); clang's private
   constants holding such addresses (local aggregate initializers). **What it
   refuses** (the build stops with `fel_state: ...` and the line):
   `thread_local` variables, other initializers holding a state address (a
   struct containing a pointer to state, for instance), state named by `switch`,
   `indirectbr` or `blockaddress`, and state named outside a function (other
   than the cases above). On such an error: change the core if it is the
   core's own code; if it is upstream's, extend `fel_state.py` for that form
   (keep it refusing everything it does not understand), and add a check that
   exercises it.
5. **No heap state that outlives an instance.** Memory the firmware takes with
   `malloc` is not part of the block: it would leak when an instance closes,
   and must not be shared between instances. Felucca, SLOOP and Melodee use
   none in the code the plugin builds.
6. **Pointers the hardware provides** (a memory-mapped flash address, a DMA
   buffer) replaced by the core with state or constants, as the cores already
   do for the flash (RAM sectors) and the screen.

**Then check** (CONTRIBUTING.md has the commands):

- the build prints, per firmware, `fel_state: N globals -> one state of X bytes`;
  a large jump in X is worth a look (a new buffer upstream);
- `<kind>_test`: its instance checks (20 instances, each sounding as alone; six
  on six threads at once; a new instance after a used one plays as the first),
  its frozen parameters, the editor protocol and objects;
- `<kind>_regress`: upstream's own golden renders, on the vendored source;
- `plugin_checks`, and on macOS a universal build
  (`-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`) whose `<kind>_test` also passes
  under Rosetta (`arch -x86_64`): the rewrite runs per architecture, and its
  layout check (`state_init` returns 0 if a field is misaligned; the engine
  then refuses to start) must pass on each. CI builds Windows, Linux and
  Android through the same tool.

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
