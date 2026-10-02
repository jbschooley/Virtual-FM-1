# Firmware gaps to close when FM-1+VA is open-sourced

baud girl has announced the FM-1+VA source under GPL-3.0-or-later but has not
published it yet (as of 2026-09-30, FM-1_093 current, FM-1_094 beta). Until
then, some settings cannot be sent to the synth, some cannot be synced at all,
and some parts of the plugin are measured or inferred rather than taken from
her code. This file lists each one: what is missing, what the plugin does
today, and what to look for in her source.

Everything below was found on FM-1_093. The tools used to find it are
`tests/fm1_probe.cpp` (memory reads, panel-control presses, CCs) and
`tests/compare_audio.py`.

## 1. Settings with no MIDI control (cannot be sent unsaved)

"Send to FM-1" and Live write the synth's edit buffer with DX7 parameter
changes and the CCs in her manual (`sync/Fm1Edit.cpp`). The settings below have
none of those, so today they reach the synth only through **Store to FM-1**,
which writes the whole preset with her exact-write SysEx and saves it to flash.

| Setting | Record bytes | Today | Look for in her source |
|---|---|---|---|
| FM filter (on, type, cutoff, resonance, envelope, decay, shape, velocity, key tracking, LFO to cutoff) | 23-26, 47, 49-51 | Store only; the send report says so | The CC handler (`CC 24-31, 52-57 do nothing while an FM preset is playing`): either allow those CCs on FM presets, or a SysEx that writes the live record |
| Distortion type (soft clip, hard clip, foldback) | 29+3k for the distortion's chain position | Store only; `kTypeCc` has no entry (`sync/Fm1Edit.cpp`) | A free FX-channel CC, or the live-record write above |
| Effect order | 27+3k (effect id per chain position) | Store only; the editor shows the stored order and cannot change it | Same |
| Envelope off | 58 (see 4.1) | Envelope CCs 73/75/70/72 switch it on; nothing switches it off | A CC for the ENV hold, or the live-record write |
| VA oscillator, level, mono, glide | 19-22, 45-46, 48, others unknown | Not edited in the plugin yet (no VA engine); VA CCs 24-30 exist. JSON exports VA presets without FM settings; their VA settings travel only in `raw` | Level and Mono bytes; whether Mono and Glide are per preset; every VA setting's byte and range, for readable VA fields in the JSON format (`plugin/Fm1Json.cpp`, `docs/virtual-fm1.schema.json`) |
| Knob assignments (KNOB1-4), per preset | Unknown; likely in 45-53 or 58 | Kept as stored, not edited or shown. JSON exports carry them only inside `raw` | Where the four choices live, the list of assignable targets and their ids, and whether each knob also stores a range; then show them in the editor, send them with Store, and add a readable `knobs` field to the JSON format |

The cleanest fix for all of these is one SysEx message that writes the live
settings record (the 59 bytes at edit buffer + 156) the way her exact-write
message writes a stored preset. If her source has a memory write, or one can be
added, `Fm1Edit::fullSound` and `delta` can send the record bytes directly and
`recordByteSettable` (`sync/Fm1Edit.cpp`) can return true for every byte.

## 2. Sequencer data that cannot be synced

Her pattern read (memory read of the step blocks and the settings block) and
pattern write (SysEx `0x20`, eight steps per message) cover notes, velocities,
note values, length, tempo, gate and swing (`sync/Fm1Seq.cpp`). Not covered:

| Data | Today | Look for in her source |
|---|---|---|
| Per-pattern Chain (FM-1_093) | Read: the settings byte at `[118 + pattern]` holds `128 + target` (her `fm1seq.js`). Write: the `0x20` message's byte there is ignored by the firmware ("not stored since FM-1_060"), so a pushed Chain stays in the plugin (`plugin/Sequencer.h` `chain`) | A field for Chain in the `0x20` write, or how the firmware stores `[118]` so it can be written |
| Tie & Slide per step and per note (FM-1_092) | Plugin-side only (`fm1::seq::Note::tie`, `Step::slide`). Not read: her decoder and ours only know notes `+0..+9`, note value `+10`, velocities `+20..+29` of each 32-byte step | Which of the step's other bytes (`+11..+19`, `+30..+31`) or bits hold ties, and how FM-1_092 converted older patterns ("held notes shrink to one step" when going back) |
| Ratchet, chance, step gate, step transpose, accent | Plugin-side only | Their bytes in the 32-byte step |
| Pattern transpose | Plugin-side only | Whether the firmware has one per pattern |
| Per-pattern preset | Plugin-side only; the synth stopped storing it at FM-1_060 | Nothing; confirm it is gone |

## 3. Addresses that depend on the firmware build

| What | FM-1_093 | Today on other builds |
|---|---|---|
| Live edit buffer (155-byte VCED) | `0x01C10070` | Found by searching RAM for a stored preset's bytes (`Fm1Session::discoverEditBuffer`); fails if the synth has unsaved edits |
| Current preset number | `0x01C0FEFA` | Inferred from the edit buffer's name |
| Live settings record | edit buffer + 156 | Same offset assumed, checked for a plausible effect chain |
| Pattern steps and settings | Her constants (`kStepsRam`, `kExtRam`, `kGsetRam`) | Same; they come from her web tools, which track her builds |

Fix: replace the table in `Fm1Session::knownAddrs` with SysEx requests for
"current sound" and "current preset number", if her source has or can take
them, so no build-specific address is needed.

## 4. Layout details inferred, not confirmed

| Detail | Evidence so far | Where |
|---|---|---|
| 4.1 Envelope on/off is byte 58 | Her `fm1sound.js` comment; her VA pack (ACID 303 has 58 = 1). But an envelope CC also set byte 53 to `0x40`, and pressing ENV by CC changed neither | `plugin/Params.cpp` (`Kind::EnvOn`), `sync/Fm1Record.cpp` |
| 4.2 Bytes 45-53 on FM presets | Read `06 00 00 07 00 00 08 00 00` on every stored preset; on VA presets they hold sub, noise, filter velocity, PWM, shape, LFO to cutoff, decay | `sync/Fm1Record.h` |
| 4.3 Filter cutoff default when unset | Nudging up then down read back 99, so the default is 99 or 100; the plugin uses 100 | `fm1::VaFilter` |
| 4.4 LFO to cutoff sign | Stored 0-100 from CC 0-127; the manual says ±4 octaves. The plugin treats it as an unsigned depth applied to a bipolar LFO | `engine/FmSynth.cpp` |
| 4.5 VA waveform encoding | Byte 19 = 1 sine, 2 saw, 3 tri, 4 square (from her pack and a CC test) | not used yet |

## 5. Sound approximations

| Part | How it was matched | Exact source needed |
|---|---|---|
| FM filter | Exponential 20 Hz-20 kHz cutoff and an SVF with damping 1.55, fitted to LP24 recordings at cutoff 30/50/70 (within ~2 dB above the noise floor). LP12, BP, HP, resonance, envelope shape and timing, velocity and LFO amounts follow the manual's wording, not measurement | Her filter code and its cutoff/resonance tables |
| Effects (filter, reverb, delay, distortion, chorus, phaser) | juce::dsp stand-ins with the manual's ranges | Her (or M-VAVE's) effect code |
| Global envelope | Times are a guess at the 0-100 scale | Her envelope code |
| Velocity | Measured: the synth scales MIDI velocity by 100/127 (`engine/FmSynth.cpp`) | Confirm the exact formula, especially at low velocities |
| Virtual Analog engine | Not implemented; VA presets play through the FM engine | Her VA engine |
| Her four Dexed fixes | Detune and LFO speed measured identical to Dexed on her build; the algorithm 4/6 feedback state and velocity-0 note-on fixes are not applied | Her changes to msfa |
| Polyphony with the filter on | The synth drops to 9-12 notes; the plugin keeps 12 | Her voice limit rule |
