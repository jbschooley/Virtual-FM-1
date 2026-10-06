# JSON presets and patterns

Virtual FM-1 reads and writes presets and sequencer patterns as JSON. One
file can hold a single preset, a group of presets, a bank, all 128, one
pattern, all 16, or everything at once. The format is described by
[`virtual-fm1.schema.json`](virtual-fm1.schema.json), so editors such as VS
Code check a file as you type when it starts with the `$schema` line the
plugin writes. [`examples/glass-ep-and-arp.json`](examples/glass-ep-and-arp.json)
is a preset and a pattern written by hand.

## In the plugin

- **Library & Sync › Export...**: the selected presets (Shift- or Cmd-click
  the list to select several), the current bank, all 128, or everything:
  all presets and all patterns. The preset you are editing is saved with its
  unsaved changes.
- **Sequencer › Export...**: this pattern or all 16.
- **Library & Sync › Import...**: presets and patterns from a `.json` file,
  or presets from a `.syx`. **Sequencer › Import...** takes only the
  patterns from a file.

Where imported presets go: when any preset in the file has a `slot`, the
plugin asks whether to put them in their own slots or in from the selected
slot. With their own slots, presets without a `slot` fill the free slots
from the selected one on, skipping the slots the file uses. From the
selected slot, every preset goes in file order from there. A file whose
presets have no `slot` fills from the selected slot without asking.
Patterns always go to their own numbers.

A file with any problem is not imported at all; the plugin lists each
problem with its place in the file, such as
`presets[0].operators[2].level: 120 is outside 0 to 99`.

## The file

```json
{
  "$schema": "https://raw.githubusercontent.com/jbschooley/Virtual-FM-1/main/docs/virtual-fm1.schema.json",
  "format": "virtual-fm1",
  "version": 1,
  "presets": [ ... ],
  "patterns": [ ... ]
}
```

Either list may be left out.

## Presets

Every field is optional. The plugin writes all of them.

| Field | Values |
|---|---|
| `slot` | 1 (A01) to 128 (D32): the slot it was saved from. See above for where imports go. |
| `name` | Up to 10 plain ASCII characters. |
| `engine` | `"FM"`, `"VA"` or `"8-Bit"` (FM-1_096). Informational; not changed on import. |
| `algorithm` | 1 to 32. |
| `feedback` | 0 to 7. |
| `oscKeySync` | true or false. |
| `transpose` | -24 to 24 semitones. |
| `lfo` | `wave` (`"triangle"`, `"saw down"`, `"saw up"`, `"square"`, `"sine"`, `"sample and hold"`), `speed`, `delay`, `pitchModDepth`, `ampModDepth` (0 to 99), `keySync`, `pitchModSensitivity` (0 to 7). |
| `pitchEnvelope` | `rates` and `levels`, four numbers each, 0 to 99. Level 50 is no pitch change. |
| `operators` | Six objects, OP1 first. Each has `mode` (`"ratio"`, `"fixed"`), `coarse` (0 to 31), `fine` (0 to 99), `detune` (-7 to 7), `level` (0 to 99), `velocitySensitivity` (0 to 7), `ampModSensitivity` (0 to 3), `envelope` (`rates`, `levels`) and `keyScaling` (`breakpoint`, `leftDepth`, `rightDepth` 0 to 99; `leftCurve`, `rightCurve` `"-lin"`, `"-exp"`, `"+exp"`, `"+lin"`; `rate` 0 to 7). |
| `effects` | All six effects in signal order. Each has `effect`, `on`, and its own settings: filter `type` (`"low pass"`, `"band pass"`, `"high pass"`), `cutoff` 0 to 107, `resonance` 0 to 10; reverb `type` (`"room"`, `"hall"`, `"plate"`), `decay`, `mix`; delay `feedback`, `rate`, `mix`; distortion `type` (`"soft clip"`, `"hard clip"`, `"foldback"`), `gain`, `tone`, `level`; chorus and phaser `rate`, `depth`, `mix`. Values 0 to 100 unless noted. |
| `noteFilter` | The per-note filter: `on`, `type` (`"LP12"`, `"LP24"`, `"BP"`, `"HP"`), `keyTracking` (0, 33, 67 or 100), `cutoff`, `resonance`, `envelope`, `decay`, `shape`, `velocity`, `lfo` (0 to 100). |
| `envelope` | The global envelope: `on`, `attack`, `decay`, `sustain`, `release` (0 to 100). |
| `raw` | `voice` (the 128-byte DX7 voice) and `settings` (the 59-byte FM-1+VA settings record), as hex. |

**How a preset is read.** The plugin starts from `raw`, or from an INIT
VOICE when there is none, and writes only the readable settings whose value
differs from it. So:

- A file read back unchanged gives exactly the same bytes, including the
  Virtual Analog engine's settings, knob assignments and the firmware's
  "unset" markers, which have no readable field.
- Changing one value in a file changes only that setting.
- To build a sound from scratch, leave `raw` out and give the settings you
  want; everything else is the INIT VOICE.

**Virtual Analog presets** are written with `name`, `engine`, `effects`,
`noteFilter`, `envelope` and `raw` only: the FM settings mean nothing to the
VA engine, and the VA engine's own settings are not mapped yet, so `raw`
carries them. Readable VA settings will be added once the firmware's source
shows what those bytes are. Knob assignments (KNOB1-4) are also kept only
in `raw` for now, for every preset, since where they are stored is not known.

**8-Bit presets** (FM-1_096) are written with `name`, `engine`, `effects` and
`raw` only: where other presets keep the FM settings, an 8-Bit preset keeps
its kit, bass and lead, and where they keep the note filter, its waveforms,
arpeggios and Key; what the envelope's bytes do on it is not known yet.
Importing one with any of those fields is an error, so that nothing is
written over them.

Some factory presets store values outside the ranges above, such as a
detune of +8. The plugin leaves such a field out when writing, and `raw`
keeps the value.

## Patterns

| Field | Values |
|---|---|
| `pattern` | 1 to 16. Required. |
| `length` | 1 to 64 steps (16 when left out). |
| `noteValue` | `"1/1"`, `"1/2"`, `"1/4"`, `"1/4T"`, `"1/8"`, `"1/8T"`, `"1/16"`, `"1/16T"`, `"1/32"`, `"1/32T"` (`"1/16"`). |
| `tempo` | 30 to 300 (120). |
| `gate` | 5 to 100 percent (50). |
| `swing` | 50 to 75 (50). |
| `transpose` | -24 to 24 semitones (0). |
| `chain` | `"repeat"` or the pattern to play next, 1 to 16 (`"repeat"`). |
| `repeats` | With a chain: how many times the pattern plays first, 1, 2, 3, 4, 6, 8, 12 or 16 (1). |
| `steps` | The steps that are not empty, each with `step` (1 to 64) and any of: `notes` (up to nine, each `note` 0 to 127, `velocity` 1 to 127, `hold`: the steps it sounds past its own, 0 when left out), `noteValue`, `ratchet` (1 to 4), `gate` (0 for the pattern's, or 5 to 100), `chance` (5 to 95, or 100 for always), `transpose`, `accent`, `locks` (FM-1_096's parameter locks, up to four `{what, value}` in the firmware's own codes; kept and sent back, not played). Files from before `hold` marked a held note with `tie` on each step that repeated it, and a step's `tieSlide`; they still read, joined into `hold`. |

A pattern in a file replaces the whole pattern: settings left out take the
defaults in brackets and steps left out are empty. Note 60 is C3 on the
FM-1.

Pushing patterns to an FM-1 running FM-1_096 sends every step setting, held
notes, Chain and Repeats and the locks. Earlier releases take only the notes,
velocities, note values and pattern settings; the rest stays in the plugin.
