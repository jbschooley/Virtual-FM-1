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
changes and the CCs in her manual (`firmwares/fm1_common/Fm1Edit.cpp`). The settings below have
none of those, so today they reach the synth only through **Store to FM-1**,
which writes the whole preset with her exact-write SysEx and saves it to flash.

| Setting | Record bytes | Today | Look for in her source |
|---|---|---|---|
| FM filter (on, type, cutoff, resonance, envelope, decay, shape, velocity, key tracking, LFO to cutoff) | 23-26, 47, 49-51 | Store only; the send report says so | The CC handler (`CC 24-31, 52-57 do nothing while an FM preset is playing`): either allow those CCs on FM presets, or a SysEx that writes the live record |
| Distortion type (soft clip, hard clip, foldback) | 29+3k for the distortion's chain position | Store only; `kTypeCc` has no entry (`firmwares/fm1_common/Fm1Edit.cpp`) | A free FX-channel CC, or the live-record write above |
| Effect order | 27+3k (effect id per chain position) | Store only; the editor shows the stored order and cannot change it | Same |
| Envelope off | 58 (see 4.1) | Envelope CCs 73/75/70/72 switch it on; nothing switches it off | A CC for the ENV hold, or the live-record write |
| VA oscillator, level, mono, glide | 19-22, 45-46, 48, others unknown | Not edited in the plugin yet (no VA engine); VA CCs 24-30 exist. JSON exports VA presets without FM settings; their VA settings travel only in `raw` | Level and Mono bytes; whether Mono and Glide are per preset; every VA setting's byte and range, for readable VA fields in the JSON format (`plugin/Fm1Json.cpp`, `docs/virtual-fm1.schema.json`) |
| Knob assignments (KNOB1-4), per preset | Unknown; likely in 45-53 or 58 | Kept as stored, not edited or shown. JSON exports carry them only inside `raw` | Where the four choices live, the list of assignable targets and their ids, and whether each knob also stores a range; then show them in the editor, send them with Store, and add a readable `knobs` field to the JSON format |

The cleanest fix for all of these is one SysEx message that writes the live
settings record (the 59 bytes at edit buffer + 156) the way her exact-write
message writes a stored preset. If her source has a memory write, or one can be
added, `Fm1Edit::fullSound` and `delta` can send the record bytes directly and
`recordByteSettable` (`firmwares/fm1_common/Fm1Edit.cpp`) can return true for every byte.

## 2. Sequencer data that cannot be synced

The plugin reads a pattern by memory read (the step blocks, the settings block and, on 096, the
lock table) and reads each step's 32 bytes whole, as baud girl's `fm1seq.js` describes them
(`firmwares/fm1_common/Fm1Seq.cpp`): notes and velocities, the step's note value, accent and
ratchet (FM-1_092 on), its own gate, chance and transpose (FM-1_096), and each note's end, so its
length in steps. On **FM-1_096** it writes with the whole-step message (`0x22`, four steps' bytes
with the pattern's five settings, Chain and Repeats included), then every lock message (`0x21`):
an unchanged pattern goes back as the very bytes read, an edited one is laid out as the synth's own
step entry would (tried on an FM-1 2026-10-06: settings made on the device survive a Send; the
synth rewrites one bookkeeping byte, +31 of an extended step). Not covered:

| Data | Today | Look for in her source |
|---|---|---|
| Before FM-1_096: writing anything but notes, velocities and note values | The pattern message (`0x20`) is all those releases take, and it clears the rest of each step it writes: accents, ratchets and note lengths read from the synth are lost by a Send (her app has the same limit). Chain is not written either ("not stored since FM-1_060") | Nothing: update to 096 |
| A step's gate, chance and transpose before FM-1_096 | Plugin-side only (those releases have none) | — |
| Pattern transpose, tempo per pattern | Plugin-side only: the synth's Tempo and Transpose are global (GLOBE) | — |
| Per-pattern preset | Plugin-side only; the synth stopped storing it at FM-1_060 | Nothing; confirm it is gone |
| Parameter locks | Read, kept and sent back (FM-1_096); not played here | What each lock code does to the sound, to play them |

## 2a. Global settings (the GLOBE screen)

The GLOBE screen holds the synth-wide settings: the MIDI, Keyboard and Glide
groups, Drive, Theme and Overdub Rec. Her protocol has no message that reads
or writes them, so the plugin cannot see them. Some matter to it:

| Setting | Why it matters to the plugin | Today | Look for in her source |
|---|---|---|---|
| MIDI channels, including FX Channel | Send to FM-1 and Live send effect CCs on the FX channel; a mismatch silently stops effects following edits | Read on connect on FM-1_093 (RAM, see HARDWARE-NOTES) and used for every message to the synth; other builds: set by hand | A read and write message, so other builds work too |
| Pitch bend up and down | The plugin's bend range should match | Read on FM-1_093 and applied to the engine; otherwise ±12, the FM-1's default | Same |
| Keyboard › Velocity | Sets how hard the FM-1's own keys play, which is what a recorded step gets | Read and shown on FM-1_093 | Same |
| Glide group | Changes the sound if it is global; the plugin's engine has no glide yet | Mode and time read and shown on FM-1_093; not modelled | The glide curve, so the engine (msfa's portamento) can match it |
| Drive (0 or −6) | Changes the sound; not modelled | Read and shown on FM-1_093 (a flag bit) | What it does to the signal, so the engine can match it |
| Ext Ctrl CC7 Vol | Whether CC 7 from the plugin changes the synth's volume | Read and shown on FM-1_093 (a flag bit) | Nothing more needed to read it |
| Other flag bits | Bits 4 and 6 of the flags byte were seen set | Unknown | What they are |
| Overdub Rec | The plugin has its own Overdub switch | Separate | Nothing needed |

Reading these without her source: each setting's RAM address was found by
changing it on the synth between memory dumps (`fm1_probe mem`), as was
done for the edit buffer. The addresses need checking for each firmware
build (section 3). Writing them needs a message in her firmware.

## 2b. The sound, exactly

The plugin's engine matches the FM-1 closely in measurements (HARDWARE-NOTES,
"Sound compared with the plugin"), and Hardware character copies the
measured output stage. Getting it exact needs her source:

| What | Today | Look for in her source |
|---|---|---|
| Engine internals: sine table, envelope and LFO steps, fixed-point rounding, any changes to msfa | The plugin uses msfa as in Dexed (with the FM-1's algorithm table, velocity scaling and transpose found by measurement) | Her engine code, to diff against `engines/dx7/msfa` |
| Effects (filter, reverb, delay, distortion, chorus, phaser) and the per-note filter | Settings sync exactly; the sound is an approximation | Each effect's algorithm and parameter curves |
| Output stage: the high-frequency roll-off, the odd harmonics near full level, Drive | Roll-off fitted from recordings (Hardware character); harmonics and Drive not modelled | The output path: any filter, saturation or gain stage, and what Drive does |
| The master volume's scaling | Hardware character's FM-1 volume is set in dB below full, not in knob positions | How MASTER scales the output, so the setting can follow the knob |
| The USB audio's dropped or repeated blocks, the 68 Hz sidebands | Not modelled (a transport fault) | The USB audio buffer handling, in case it can be fixed in the firmware |

## 2c. Syncing over Bluetooth MIDI

Over Bluetooth the FM-1 plays notes, but the plugin cannot sync: it reports
that the FM-1 did not answer. Found by disassembling FM-1_089 (FM-1_093 not
checked; it behaves the same on an iPad):

| What | Over Bluetooth MIDI | Where in FM-1_089 |
|---|---|---|
| `F0 43 00 7D` requests | Reach the same handler as over USB; writes are applied (not tested on hardware) | BLE MIDI writes go to the same MIDI parser as USB, then the `7D` handler |
| Replies (`F0 7D ...`) | Sent only over USB. With no USB host connected, a read is dropped without a reply; with one, the reply goes to the USB host | The reply sender fills a USB-only transmit buffer and returns early when no USB host is attached |
| Identity request (`F0 00 32 45 ...`) | Not answered | Recognised only in the USB receive path, and on a separate (non-MIDI) vendor Bluetooth service |

The plugin cannot work around this. Reads need replies, so the plugin can only
push over Bluetooth, unconfirmed (not built). Look for in her source: send each
reply back over the transport the request came in on (USB or Bluetooth), and
answer the identity request on both.

## 2d. FM-1_096's 8-Bit presets and Bitcrush

An 8-Bit preset (record byte 18 = `0xC3`) keeps a kit of twelve drums, twelve
sound effects, a bass and a lead where other presets keep the FM voice, and the
bass's and lead's waveforms, its User Arpeggios and its Key in record bytes
19-26 and 45-51 (her 096 release notes and `fm1preset.js`). Whether the
envelope (54-58) applies to one is not known: her `newPreset` says Erase Preset
writes "the Envelope's switch on an 8-Bit preset" among bytes 52-53. The plugin
keeps every byte but the effects' as stored and syncs them unchanged, but has
no 8-Bit engine: such a preset is silent, and only its effects can be edited.
Look for `chip.h` and the 8-Bit engine in her source.

096 also adds a seventh effect, Bitcrush (Bits, Sample Rate, Mix; its switch
and place in record byte 5, her `fm1preset.js`), and banks for the four knobs
(her release notes). The plugin neither plays nor edits them; their bytes
travel unchanged.

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
| 4.1 Envelope on/off is byte 58 | Her `fm1sound.js` comment; her VA pack (ACID 303 has 58 = 1). But an envelope CC also set byte 53 to `0x40`, and pressing ENV by CC changed neither | `plugin/Params.cpp` (`Kind::EnvOn`), `firmwares/fm1_common/Fm1Record.cpp` |
| 4.2 Bytes 45-53 on FM presets | Read `06 00 00 07 00 00 08 00 00` on every stored preset; on VA presets they hold sub, noise, filter velocity, PWM, shape, LFO to cutoff, decay | `firmwares/fm1_common/Fm1Record.h` |
| 4.3 Filter cutoff default when unset | Nudging up then down read back 99, so the default is 99 or 100; the plugin uses 100 | `fm1::VaFilter` |
| 4.4 LFO to cutoff sign | Stored 0-100 from CC 0-127; the manual says ±4 octaves. The plugin treats it as an unsigned depth applied to a bipolar LFO | `engines/dx7/FmSynth.cpp` |
| 4.5 VA waveform encoding | Byte 19 = 1 sine, 2 saw, 3 tri, 4 square (from her pack and a CC test) | not used yet |

## 5. Sound approximations

| Part | How it was matched | Exact source needed |
|---|---|---|
| FM filter | Exponential 20 Hz-20 kHz cutoff and an SVF with damping 1.55, fitted to LP24 recordings at cutoff 30/50/70 (within ~2 dB above the noise floor). LP12, BP, HP, resonance, envelope shape and timing, velocity and LFO amounts follow the manual's wording, not measurement | Her filter code and its cutoff/resonance tables |
| Effects (filter, reverb, delay, distortion, chorus, phaser) | juce::dsp stand-ins with the manual's ranges | Her (or M-VAVE's) effect code |
| Global envelope | Times are a guess at the 0-100 scale | Her envelope code |
| Velocity | Measured: the synth scales MIDI velocity by 100/127 (`engines/dx7/FmSynth.cpp`) | Confirm the exact formula, especially at low velocities |
| Virtual Analog engine | Not implemented; VA presets play through the FM engine | Her VA engine |
| Her four Dexed fixes | Detune and LFO speed measured identical to Dexed on her build; the algorithm 4/6 feedback state and velocity-0 note-on fixes are not applied | Her changes to msfa |
| Polyphony with the filter on | The synth drops to 9-12 notes; the plugin keeps 12 | Her voice limit rule |
