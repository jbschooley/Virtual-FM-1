# Changelog

What changed in each release of Virtual FM-1. The section for a version
becomes the notes of its GitHub release, so write entries for players, not
for the code. New work goes under the next version at the top.

## [0.8.0] - 2026-10-09

### Changed

- **Much smaller downloads**: Felucca, SLOOP and Melodee were each built in
  sixteen times so that instances could play apart; each is now built in once,
  and every instance keeps its own copy of the firmware's state. The macOS
  installer goes from 128 MB to 31 MB, Linux from 74 MB to 24 MB, Windows from
  16 MB to 12 MB, the Android app from 40 MB to 13 MB. Any number of instances now play at once without
  costing extra CPU the way the 17th and later did before, and they sound as
  before, bit for bit.
- **SLOOP 2.5** is built in (it was 2.4.1): its PHYS (physical models) and
  NOISE engines, 153 factory sounds, the SYN drum kits (kept with its projects
  and synced with an FM-1), and the drums' delay send (GLO > DRUMS > DLY, also a
  host parameter, "SLOOP DRDLY"). An FM-1 on SLOOP 2.4 is pulled from and sent
  to, without the SYN kits it does not have (it has no PHYS or NOISE either:
  user presets on them read as empty there, and tracks on them open with
  another engine); Live needs 2.5 on both, and says so. Tried
  with an FM-1 on 2.5: pull, send (the SYN kits too) and Live.

## [0.7.0] - 2026-10-08

### Added

- **Melodee 0.13** (Kerem Kilic's firmware, built on Felucca) is a firmware an
  instance can play, built from its source: its engines on four parts, PROPHET
  among them with Sequential's 200 Prophet-5 factory programs, and CZ-1 with
  Casio's 64 CZ-1 tones; its 32 pattern banks, screen, front panel, projects and
  user presets. Its device file is kept in the library as Melodee's web editor
  writes a backup (`Melodee/Melodee device.json`).
- **Sync with an FM-1 running Melodee 0.13**: Pull and Send of everything (all
  pattern banks, projects, user presets, CZ, FM6 and PROPHET banks), a part's
  sound with its own CZ-1 tone or PROPHET patch, the patterns of the bank each
  part plays with Melodee's song, and Live, pattern bank switches included. An
  FM-1 still on Melodee 0.12 is not sent to or followed live: update it with
  Melodee's installer (pulling from it has not been tried).
- **Melodee's host parameters** ("Melodee P1 LEVEL" ...): each part's
  parameters and the engine's eight and its playback quantize; the song's
  scale, quantize and scale degree (one for every part, as on the device); and
  the tempo, swing and effects, for automation. Not its tuning, A4, boot
  project or drum channel, which are the device's settings.
- Melodee's Sequencer tab works on the pattern bank each part plays.

## [0.6.0] - 2026-10-07

### Changed

- **SLOOP 2.4.1** is built in (it was 2.3): its FM6 engine (DX7-style, with
  its factory patches and a 27-slot bank), each track's filter (FILT, the drums
  too), STRUM and VLEAD for chords, DIV up to 2BAR, the delay's dotted times,
  a fourth user sample slot, and its full-screen visualiser on the Device
  tab. Its FM6 bank is kept with its
  projects and user presets and syncs with an FM-1 running 2.4. The host
  parameters gain FILT, STRUM and VLEAD for each part and FILT for the drums;
  DIV and the delay TIME span their new values, so automation of those two
  written for 2.3 lands on other values. An FM-1 still on SLOOP 2.3 is
  pulled from only (2.3 does not read 2.4's projects): update it with
  SLOOP's installer to send to it or go Live.
- SLOOP's sequencer tab edits 2.4's **parameter locks**, **step nudges** and
  **fill conditions**: the step editor has a nudge (in 64ths of a step), when
  the step plays (always, only in a fill, or not in a fill), and a lock for any
  sound parameter on that step. Pull, Send and Live carry them, and MIDI export
  plays nudged steps where SLOOP plays them.
- An FM-1 on SLOOP 2.4 is recognised and synced as 2.4.1 (the same projects);
  the update to 2.4.1 fixes imported FM6 patches that use AMS playing noise.
- Felucca's and SLOOP's sequencer: **drag a note sideways** to hold it longer
  or shorter (it stops at the next note), as the FM-1+VA sequencer already
  did. A row of **step numbers** above the grid selects a step without
  changing it, so a touchscreen can reach a step's settings. On a phone the
  piano roll shows one octave, with rows big enough to touch.
- In every sequencer tab, the **left and right arrow keys** move the selected
  step.

## [0.5.1] - 2026-10-07

### Fixed

- An FM-1 running **Melodee** (keremimo's firmware, built from Felucca) was
  taken for a newer Felucca, and Pull or Send would have used Felucca's
  objects on it. It is now recognised by its editor's name, said to be not
  supported yet, and not connected to; Melodee's backup files (which carry
  Felucca's file format) are not restored into Felucca.

## [0.5.0] - 2026-10-06

### Added

- **Felucca 1.0.3** is built in (it was 1.0.2): its FM6 patch bank is gone
  (each track has its own patch, SLOT is F1-F8 or OWN, and user presets keep
  their FM6 patch), its LARGE screen layout and layer lock on the Device tab,
  and ROUT CH1-4 listening to channels 1-4 only. An FM-1 still on 1.0 to 1.0.2
  is synced too: Send never empties its bank (nor sends it what it cannot
  take), and Pull hands its bank to the Felucca built in, which turns it into
  the user presets' patches as the update does (per Felucca's protocol notes;
  not tried with an FM-1 on 1.0.2).

- **SLOOP 2.3** (isod89's groovebox firmware, built on Felucca) as a firmware
  choice, built from its source: its nine engines on three parts and a drum
  track, played and edited in the Felucca editor and as host parameters for
  automation ("SLOOP P1 ...", "SLOOP DRUMS KIT", "SLOOP DUST" ...), with its own
  screen and front panel on the Device tab. Its projects and user presets are kept in the
  library as SLOOP's own backup file (`SLOOP/SLOOP device.json`). With an FM-1
  running SLOOP: Pull, Send, Live and a part's sound, as with Felucca (tried on
  an FM-1 running SLOOP 2.3: a full backup, Send, and Live from the plugin's
  side). Live carries the drum track's steps whole (every lane, its level and
  ratchet).

- **baud girl's FM-1_096.** 8-Bit presets show as 8-Bit in the library, with
  an 8-Bit tab in place of the FM editor. The plugin has no 8-Bit engine (her
  source is not published), so they are silent here, and only their effects
  can be edited; everything else in them is kept exactly and syncs unchanged.
  JSON writes their effects and raw bytes only.
- **Parameter locks** (FM-1_096): pulling a pattern reads its locks, and
  sending it puts them back. Without this, a Send to an FM-1 on 096 would
  clear the locks of every step it writes (as baud girl documents 096).
  Locks are kept in projects and in JSON (`locks` on a step); the plugin
  does not play them.
- **Every step setting syncs with FM-1_096.** Pulling a pattern reads each
  step's accent and ratchet, its own gate, chance and transpose, how long
  each note is held, and the Chain's Repeats; sending puts them all back with
  096's whole-step message. A pattern not edited in the plugin goes back as
  the very bytes read. Before this, a Send cleared those settings on the
  FM-1. Earlier releases are read the same way (accents, ratchets, held
  notes from 0.92), but a Send to them still carries notes only, as baud
  girl's own app does.

- **A Sequencer tab for Felucca and SLOOP**: each part's steps on a grid (a
  roll for notes, lanes for drum hits), the selected step's time, accent,
  slide, velocity and chance (SLOOP: each note's level and ratchet), each
  part's LEN, DIV, swing and gate, pages of 16, 32 or 64 steps; Felucca's song
  chain and motion, SLOOP's live sections A-D, song, SONG REC, mute and solo.
  Pull and Send the patterns, and every edit reaches the FM-1 while Live runs
  (tried on an FM-1 with Felucca 1.0.3 and with SLOOP 2.3: steps, recording,
  drum hits both ways; Felucca's chain and motion are not tried on one yet).
  Export the patterns as a MIDI file, or import one onto a part.
- **The FM-1+VA Sequencer, as 0.96's Pattern screen**: sixteen steps a page
  in lanes (Notes and Locks, or an 8-Bit preset's Lead, Bass, SFX, Drums and
  Locks), each lane opening into a row for each note (a roll to click notes
  into and drag longer), its drum and SFX keys by name, or its locks as bars.
  Below, the step as the FM-1's step list has it, with each note's velocity
  and length and its locks by their FM-1 names (added, set and removed here,
  sent to the FM-1; not played by the plugin). The Chain has its Repeats.
- **The front panel looks like the FM-1's** on Felucca's and SLOOP's Device
  tab: OCT- and OCT+ above, FX to GLO and HOME to REC on two rows, printed
  labels (SEL, PLAY over STOP, OP1 .. POLY on the black keys), and every key
  lit as the firmware lights it (white, REC red, PLAY/STOP green while
  playing).
- **An Android app** (arm64, Android 7 and later; tried on a Galaxy S22): the
  standalone, Felucca and SLOOP included. CI builds the APK; a release has
  it once the repository has a signing key.

### Changed

- **Live is in the top bar**, beside Connect and Find FM-1, for every tab:
  Felucca's and SLOOP's two-way sync, or stock and FM-1+VA's sound sent as
  you edit it. A reply the FM-1 drops (another program talking to it) is
  asked for again instead of ending Live.
- Audio/MIDI, Connect, Find FM-1, Live and the sequencers' Play are icons.
- FM-1+VA 0.96 is the release the plugin syncs with, tried on an FM-1:
  presets (FM, VA and 8-Bit) and patterns with locks and step settings come
  back unchanged. 0.93 and 0.94 are synced as before, without 8-Bit presets
  or locks.
- Writing presets to 0.96 waits 120 ms between them, as its own editor
  does, not 3 s: Push all takes well under a minute.
- Note 60 is C4 in the FM-1+VA Sequencer and on the on-screen keyboard, as
  the FM-1 (0.96) and Felucca name it; it was C3.
- A held note is one note with a length (in steps), as on the FM-1, not a
  note repeated on the steps after it. Projects and JSON files with the old
  ties still load, joined into lengths; a tie into a different note plays
  held into the next step, as Tie & Slide does on the FM-1.

## [0.4.1] - 2026-10-05

### Changed

- **Felucca 1.0.2** is built in (it was 1.0). A sound that used SAMPLE's
  PERC set plays as the DRUM engine's kit now: Felucca 1.0.2 converts it
  whenever a project loads, so projects saved with 0.4.0 change too. QNT
  gains SEQ (the sequencer's notes follow the scale too). An automation
  lane for QNT recorded with 0.4.0 may play a different setting, since its
  range grew.
  An FM-1 still on Felucca 1.0 is recognised and synced as 1.0.2; the
  line beside Find FM-1 says it is older and to update it.

- **Felucca 1.0.1 and 1.0.2 on the FM-1**: the plugin tells them from 1.0
  (they answer as 1.0 does, FM-1_910) by what their editor says. The line
  beside Find FM-1 says when the FM-1 runs an older Felucca than the
  plugin's 1.0.2 (1.0, 1.0.1) or a newer one. Live with an FM-1 on 1.0.2
  no longer waits for a reload that 1.0.2 does not send back, which could
  hide a real one for two seconds.
- **Pull from FM-1** keeps the plugin's own Felucca settings (palette, panel
  calibration, favourites, LED mode), as Send keeps the FM-1's. A newer
  Felucca's settings could make the whole Pull fail.

## [0.4.0] - 2026-10-05

### Changed

- **Firmware choice**: a dropdown at the top sets which firmware an
  instance plays and syncs with (M-VAVE stock V15, FM-1+VA 0.93, Felucca
  1.0), saved with the project. When an FM-1 running another firmware connects, the
  plugin asks before doing anything with it; if you keep your choice it
  stays disconnected.
- **The editor is rearranged**: the preset list now sits beside the
  preset's pages (FM, or VA for a VA preset, and Effects & Envelope) and
  the sync buttons, all in the Library tab; Sequencer, Arpeggiator and
  Settings have their own tabs; the MIDI connection, and in the app the
  Audio/MIDI settings, moved to the top bar. The window is a little larger.
- **On an iPad**: Select above the preset list picks several presets by
  tapping; Effects & Envelope wraps to the screen and scrolls; drop-down
  lists open below their box instead of hiding items behind a scroll
  arrow.
- **On a phone, or an iPad upright**: the editor fits a narrow window. The
  top bar takes two lines (the firmware and Audio/MIDI, then the MIDI
  in and out, Connect and Find FM-1); the library shows the preset list or
  the preset's pages, switched with Presets and Sound; the FM page shows
  one of Algorithm, LFO, Pitch EG and OP1 to OP6 at a time; the sequencer
  is one column that scrolls. The app stays clear of the Dynamic Island
  and the home indicator. Tried in the iPhone simulator; turned sideways,
  a phone does not fit yet.
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
- **Plugins connect to the FM-1 only when asked**: with Connect or Find
  FM-1. Before, every instance in a project connected as soon as an FM-1
  was plugged in. The standalone app still connects by itself.
- **Hardware character** in a project not at 44.1 kHz now runs the engine
  at the FM-1's own 44.1 kHz and converts, so the FM-1's aliasing comes
  through too. It adds under a millisecond of latency, reported to the host.

### Added

- **Felucca**: an instance set to Felucca is an FM-1 running Felucca 1.0,
  built from Felucca's own source (all of it: sound, sequencer, screen,
  projects, user presets, FM6 bank and editor protocol) with its author's
  agreement. Its engines are ANALOG, FM6, PHASE, LOFI, SAMPLE, VOICE, TRIO,
  WHEEL, GRAIN, PHYS, NOISE, SLICE and DRUM; MIDI channels 1-4 play its
  four parts, as on the FM-1; the on-screen keyboard plays the selected
  part. Any number of instances can play Felucca; beyond sixteen, each
  costs a little more CPU.
  - Its editor has two tabs. **Library**: the 32 user presets and four
    projects in a list, beside the Sound page (each part's engine, preset
    and every parameter by Felucca's own names, the modulation matrix and
    chords too, and the global settings) and the Sync page. Load a user
    preset into the selected part or a project into all four; save the
    selected part's sound as a user preset with a name, or the music as a
    project; rename and erase user presets. **Device**: Felucca's own
    screen, buttons, knobs and keys, where its sequencer, projects and
    user presets work as on the device.
  - The project keeps the music playing as Felucca saves a project. The
    device's four project slots, 32 user presets, FM6 bank and settings
    are kept in the library folder (Felucca/Felucca device.json, the file
    Felucca's web editor writes for a full backup), shared by every
    instance.
  - Its tempo, PLAY and STOP follow the host unless you turn that off.
    Its parameters can be automated from the host (292 of them: each
    part's and the global ones). In a plugin, SysEx from the host reaches its
    editor protocol (its replies are not sent back to the host yet).
  - With an FM-1 running Felucca connected: Pull from FM-1, Send to FM-1
    (the synth's own are backed up to the library first; its settings stay
    its own) and Live, which keeps both the same as either changes (both
    must run the same Felucca version). Two buttons on the Sound page copy
    the selected part's sound from the FM-1, or to it without saving it
    there. Tried with an FM-1 running Felucca
    1.0: reading everything from it; sending everything to it, read back
    the same; and Live with knob turns, tempo, step edits and part changes
    on the synth, and a value changed in the plugin, arriving on the other
    side.
  - On macOS, Windows and Linux alike.
  - Not yet: user sample slots.
- **Firmware versions**: the firmware list says which release of each the
  plugin plays and syncs with. When an FM-1 connects, the line beside Find
  FM-1 (not shown on a phone yet) says what its release means for syncing if it is not one the plugin
  has tried: a newer one is synced as the newest known, an older one may
  lack something (the note says what), and a Felucca before 1.0 does not
  sync (update it with Felucca's installer). A project remembers the
  release it was made for and says so when loaded where the plugin plays
  another. Felucca music a project holds that this Felucca cannot read
  (from a newer Felucca, say) is kept in the project, not replaced.
- **Sloop** (a groovebox firmware made from Felucca) is recognised when an
  FM-1 running it connects: the plugin says it does not support Sloop yet
  and leaves the synth alone, rather than taking it for a Felucca build.
- **Init...** starts the current preset over from a blank FM sound, as an
  unsaved change.
- **Bluetooth MIDI**: the iPad app can pair with Bluetooth MIDI devices,
  and Find FM-1 also finds an FM-1 paired over Bluetooth. Syncing still
  needs USB: FM-1+VA answers only over USB.
- **FM-1_094** (baud girl's beta): reading the current sound and the GLOBE
  settings works as on FM-1_093. 094 changes only the identity reply's
  checksum, which M-VAVE's own updater rejected on 093.
- **iPad and iPhone**: the app and an AUv3 build from source for iOS and
  iPadOS and sync with the FM-1 over USB-C. Not distributed yet; see
  `docs/IOS.md` to build and install them yourself.

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

[0.8.0]: https://github.com/jbschooley/Virtual-FM-1/compare/v0.7.0...v0.8.0
[0.7.0]: https://github.com/jbschooley/Virtual-FM-1/compare/v0.6.0...v0.7.0
[0.6.0]: https://github.com/jbschooley/Virtual-FM-1/compare/v0.5.1...v0.6.0
[0.5.1]: https://github.com/jbschooley/Virtual-FM-1/compare/v0.5.0...v0.5.1
[0.5.0]: https://github.com/jbschooley/Virtual-FM-1/compare/v0.4.1...v0.5.0
[0.4.1]: https://github.com/jbschooley/Virtual-FM-1/compare/v0.4.0...v0.4.1
[0.4.0]: https://github.com/jbschooley/Virtual-FM-1/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/jbschooley/Virtual-FM-1/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/jbschooley/Virtual-FM-1/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/jbschooley/Virtual-FM-1/releases/tag/v0.1.0
