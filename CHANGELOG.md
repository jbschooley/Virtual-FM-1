# Changelog

What changed in each release of Virtual FM-1. The section for a version
becomes the notes of its GitHub release, so write entries for players, not
for the code. New work goes under the next version at the top.

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

[0.2.0]: https://github.com/jbschooley/Virtual-FM-1/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/jbschooley/Virtual-FM-1/releases/tag/v0.1.0
