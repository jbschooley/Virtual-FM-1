# Hardware notes

What was learned by testing against an FM-1, and how. The tools are
`tests/fm1_probe.cpp` (identify, pull, verified writes, memory reads, CCs,
panel-control presses, renders) and `tests/compare_audio.py` (compares a
recording of the synth's USB audio with a render of the same note).

## Tested on FM-1_093

On an FM-1 running FM-1_093 (`tests/fm1_probe.cpp`, `tests/compare_audio.py`):

- Identify, pull of single presets and all 128, verified writes, pattern reads,
  and pulling the synth's current sound all work.
- The synth's live edit buffer is at `0x01C10070` and its current preset
  number at `0x01C0FEFA` on FM-1_093; other versions are found by searching RAM.
- GLOBE settings on FM-1_093, found by changing each on the synth between
  RAM dumps: MIDI channel at `0x01C0FEFD` (0 = All), FX channel at `+1`
  (stored minus one), bend up `+2` and down `+3` (semitones), Keyboard ›
  Velocity `+4`, glide time `+0x82` (0-100) and glide mode `+0x83` (0 Full
  Time, 1 Fingered). A second copy of the bend and glide values sits near
  `0x01C61D57`. The plugin reads these on connect (FM-1_093 only). Drive,
  Ext Ctrl CC7 Vol and Overdub Rec changed in the same dump and are not
  placed yet.
- Pitch, transpose, detune beating and LFO speed match the synth. The
  firmware's LFO table is Dexed's, entry for entry.
- The FM-1 scales incoming MIDI velocity by 100/127 before its engine; the
  plugin does the same (measured on PIANO 1 and BRASS 5 at several velocities).
- Stored presets keep most settings-record bytes at `0x03` ("unset"); the
  editor writes back only settings you change, so these survive.
- The live settings record sits right after the edit buffer (+156). Effect
  bytes, chain on/type, envelope A-D-S-R and the VA/FM filter layout were
  confirmed by sending CCs and walking the synth's menu by its panel CCs.
- DX7 voice parameter changes edit the synth's edit buffer only; stored
  presets stay byte-identical. A program change drops unsaved edits.
- Ties are not synced: how FM-1_092 stores them is not yet known, and neither
  her pattern write nor read covers them. Per-pattern Chain can be read but
  not written over MIDI.

## Record layout

The 59-byte settings record that travels with each preset is interpreted as
described in `sync/Fm1Record.h`. That layout is inferred from baud girl's
comments and from her 16-preset Virtual Analog pack, not from published
source; the bytes for Virtual Analog settings and knob assignments are kept
as they are.

## Not yet known

See [`FIRMWARE-GAPS.md`](FIRMWARE-GAPS.md).
