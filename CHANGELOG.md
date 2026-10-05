# Changelog

What changed in each release of Virtual FM-1. The section for a version
becomes the notes of its GitHub release, so write entries for players, not
for the code. New work goes under the next version at the top.

## [Unreleased]

### Changed

- **Firmware choice**: a dropdown at the top sets which firmware an
  instance plays and syncs with (M-VAVE stock, FM-1+VA, Felucca), saved
  with the project. When an FM-1 running another firmware connects, the
  plugin asks before doing anything with it; if you keep your choice it
  stays disconnected. Felucca shows a page saying it is not in the plugin
  yet, and is silent.
- **The editor is rearranged**: the preset list now sits beside the FM,
  Effects & Envelope and (on FM-1+VA) VA pages and the sync buttons, all in
  the Library tab; Sequencer, Arpeggiator and Settings have their own tabs;
  the MIDI connection moved to the top bar. The window is a little larger.
- **Your presets are a folder**: the shared library moved to
  Documents/Virtual FM-1/Banks/FM-1, one readable JSON file per preset, so
  it can be backed up and synced with ordinary tools. The first run copies
  the old library file over and leaves it where it was. When two instances
  change the same preset, the one replaced is kept in a .trash folder.
- **Projects keep the sound they use**, not the whole 128-preset bank, so
  they are about half the size. If that preset has changed in your library
  since, the project still sounds as saved and the difference shows as
  unsaved changes. Settings > Library can keep the whole bank in projects,
  to take them to another computer.
- **Hardware character** in a project not at 44.1 kHz now runs the engine
  at the FM-1's own 44.1 kHz and converts, so the FM-1's aliasing comes
  through too. It adds under a millisecond of latency, reported to the host.

### Added

- **Init...** starts the current preset over from a blank FM sound, as an
  unsaved change.
- **Bluetooth MIDI**: the iPad app can pair with Bluetooth MIDI devices,
  and Find FM-1 also finds an FM-1 paired over Bluetooth. Syncing still
  needs USB: FM-1+VA answers only over USB.
- **FM-1_094** (baud girl's beta): reading the current sound and the GLOBE
  settings works as on FM-1_093. 094 changes only the identity reply's
  checksum, which M-VAVE's own updater rejected on 093.
- **iPad**: the app and an AUv3 build from source for iOS and iPadOS and
  sync with the FM-1 over USB-C. Not distributed yet; see `docs/IOS.md`.

### Fixed

- Two instances at different sample rates (two projects at 44.1 and
  96 kHz open in one host, say) put each other out of tune and changed
  each other's envelope speeds.
- Presets with operator feedback or an LFO could start slightly
  differently each time, from whatever memory held; they now start the
  same way every time.
- A host sending a larger audio block than it announced could crash the
  plugin when the chorus or phaser was on.
- An FM-1 running **Felucca** is recognised as Felucca instead of being
  taken for FM-1+VA: the plugin no longer tries to pull from it, and live
  editing sends it nothing, since Felucca does not take FM-1+VA presets.

## [0.3.0] - 2026-10-02

### Added

- **Linux**: the Standalone app, VST3 and LV2 for x86-64, in a `.tar.gz`
  with an install script that installs for your account or system-wide and
  lets you leave parts out. LV2 hosts list the 128 slots as presets; picking
  one loads that slot from your library, though the names in the host's
  list are placeholders. Syncing with an FM-1 on Linux is untested with the
  hardware.
- **Hardware character** (Settings tab, saved with the project): makes the
  plugin sound like the FM-1's USB audio, with 16-bit output at the FM-1's
  level and its slightly softer high end, both measured from the hardware.
  An FM-1 volume control sets how far below full the synth's volume is, for
  the grain its output has at lower volumes. Off by default.
- **MIDI files for the sequencer**: export this pattern or all 16 (one track
  each) as a standard MIDI file with the notes as they play, including
  ratchets, accents, ties and transpose; import a MIDI file's notes into the
  selected pattern on its note-value grid, with held notes tied.
- **Settings tab**: pitch-bend range, the MIDI channel the plugin plays
  from the host, and a fixed velocity for the on-screen keyboard (and step
  recording with it). Changes take effect at once and are saved with the
  project; **Save as default** makes them what new instances start with,
  and **Revert to default** goes back to them.
- **Reads the FM-1's GLOBE settings** when it connects (FM-1_093): its MIDI
  and FX channels, pitch-bend range, key velocity, glide, drive, CC7 volume
  and overdub setting, shown on the Settings tab. The plugin talks to the
  FM-1 on its channels, so the FX channel no longer has to be set by hand.
  **Copy from FM-1** takes its bend range and key velocity into the
  plugin's settings.

### Changed

- The pitch-bend range defaults to ±12 semitones, the FM-1's default; it
  was ±2.

### Fixed

- On FM-1_093, Show on FM-1, Store to FM-1 and Send to FM-1 select the
  preset on the synth's own MIDI channel, so they work when the FM-1
  listens on a channel other than 1.

## [0.2.0] - 2026-10-01

### Added

- **JSON presets and patterns.** A readable, editable format for backups and
  for writing sounds and patterns by hand or with a script or AI. A file can
  hold one preset, a group, a bank, all 128, one pattern, all 16, or
  everything. Each preset carries its raw bytes too, so a file read back
  unchanged gives exactly the same sound and a hand edit changes only what
  was edited. Described in `docs/JSON-FORMAT.md`, with a JSON Schema that
  editors can check files against and an example file.
- **Export menu** on the Library & Sync tab: the selected presets, the
  current bank, all 128, or everything (presets and patterns) as JSON; the
  selected presets or all 128 as `.syx`.
- **Select several presets** in the list with Shift-click or Cmd-click
  (Ctrl-click on Windows) to export them together. A plain click still picks
  the preset to play.
- **Import and Export on the Sequencer tab**: one pattern or all 16 as JSON,
  with every step setting, including ratchet, chance, accent, ties,
  transpose and Chain.
- **Import asks where presets go** when a file says which slots they came
  from: back into their own slots, or in from the selected slot.

### Changed

- **Import...** on the Library & Sync tab now takes `.json` files as well
  as `.syx`.
- Exporting the preset being edited saves it with its unsaved changes, in
  both formats.
- Virtual Analog presets are exported without FM settings, which the VA
  engine does not use; their raw bytes carry the whole sound.
- macOS installer: no "How do you want to install this software?" step;
  everything installs on the startup disk.
- macOS installer: the license reads as normal paragraphs instead of
  breaking lines in odd places.

## [0.1.0] - 2026-10-01

First release.

- A software FM-1 as a Standalone app and VST3/AU plugin for macOS (Apple
  Silicon and Intel) and Windows: the FM-1's 12-voice FM engine, its
  effects, global envelope and per-note filter.
- Two-way sync with an FM-1 running baud girl's FM-1+VA firmware: pull and
  push one preset or all 128, push only what changed, send unsaved edits to
  the synth, show a preset on the synth.
- An editor for every sound setting in Dexed's layout, with host automation
  and a preset library shared by every instance.
- The FM-1's sequencer (16 patterns, up to 64 steps, up to nine notes a
  step, with per-note velocity editing) and arpeggiator, synced to the host.
- `.syx` import and export: FM-1+VA backups, DX7 banks and voices.
- Installers that let you choose the Standalone app, VST3 and AU.

[0.3.0]: https://github.com/jbschooley/Virtual-FM-1/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/jbschooley/Virtual-FM-1/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/jbschooley/Virtual-FM-1/releases/tag/v0.1.0
