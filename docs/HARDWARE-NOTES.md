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
  `0x01C61D57`. The byte at `+0x6E` holds flags: bit 1 Drive −6, bit 2 Ext
  Ctrl CC7 Vol off, bit 3 Overdub Rec on (found by changing them one at a
  time during continuous dumps; bits 4 and 6 were also seen set, meaning
  unknown). The plugin reads all of these on connect and when the Settings
  tab opens (FM-1_093 only).
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

## Sound compared with the plugin

Measured on FM-1_093 from its USB audio ("FM-1 Audio", 44.1 kHz) against the
plugin rendering the same presets and notes (October 2026):

- The USB audio is mono (left and right identical, effects included), 16-bit
  (every sample on the 16-bit grid) and exactly silent between notes.
- A plain sine (INIT VOICE, effects off) has the engine's pure shape: odd
  harmonics of the 3rd to 7th order appear only near full output (about -55 to
  -61 dB) and fall into the 16-bit floor as the level drops, so they come from
  the output stage, not the engine's sine.
- Eight factory FM presets (PIANO 1, SYN LEAD 1, SYN PAD 1, SYN LEAD 3, PIANO 6,
  DS GUITAR4, WOODWIND 4, STRING 7) at C4 and C6 match the plugin within about
  0.6 dB per octave band (bands within 40 dB of each sound's peak), the same at
  a 44.1 or 48 kHz host rate. The FM-1 is consistently a little softer above
  6 kHz: about 1, 2 and 3.5 dB at 6-10, 10-14 and 14-20 kHz.
- Its level follows the MASTER knob; with the knob as it was in one session,
  believed to be at full, a full-level sine came out 2.5 dB below the plugin.
- The USB stream drops or repeats 512-sample blocks a few times a second
  (clicks); there are also faint sidebands 68 Hz either side of a held sine,
  cause unknown.

The Settings tab's Hardware character (`plugin/HardwareCharacter.h`) applies
the 16-bit output at that level and the high-frequency roll-off, which brings
the bands above 6 kHz to within 0.2 dB of the FM-1 on the same recordings (the
roll-off was fitted to them). Its FM-1 volume control rounds as coarsely as the
FM-1 would at a given number of dB below full volume.

## Record layout

The 59-byte settings record that travels with each preset is interpreted as
described in `sync/Fm1Record.h`. That layout is inferred from baud girl's
comments and from her 16-preset Virtual Analog pack, not from published
source; the bytes for Virtual Analog settings and knob assignments are kept
as they are.

## Not yet known

See [`FIRMWARE-GAPS.md`](FIRMWARE-GAPS.md).
