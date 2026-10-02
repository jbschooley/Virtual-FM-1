# Proposal: a shared SysEx protocol for FM-1 firmwares

**Status: draft, for discussion with the firmware authors.** Nothing here is
required. Virtual FM-1 supports each firmware through its own profile
([`FIRMWARE-PROFILES.md`](FIRMWARE-PROFILES.md)), so a firmware that keeps its
own protocol, or has none, still works as far as its protocol allows. This
proposal is about saving everyone that work: a firmware that speaks it works
with the plugin, and with any other tool that speaks it, without new code.

It builds on baud girl's FM-1+VA protocol, which is already in use and works
well. Her messages stay exactly as they are; FM-1+VA already speaks the core
of this. The additions fill the gaps listed in
[`FIRMWARE-GAPS.md`](FIRMWARE-GAPS.md) and let a firmware say what it can do.
The command numbers marked *proposed* are suggestions to agree with her, since
the `7D` space is hers in FM-1+VA.

## Framing (as FM-1+VA today)

- **Requests**: `F0 43 00 7D <command> <data> <sum> F7`. `<sum>` is the 7-bit
  sum of `(b XOR FF)` over command and data.
- **Replies**: `F0 7D <body> F7`, where `<body>` is an 8-bit buffer packed as a
  7-bit, least-significant-bit-first stream:
  `7D <kind> <status> <arg: 4 bytes LE> <length: 2 bytes LE> <data> <sum>`,
  `<sum>` being the 8-bit complement of the sum of everything before it.
- **Status**: `0` done, `1` a value out of range, `2` damaged in transit, `3`
  the sequencer is playing. *Proposed:* `4` not supported, so a tool can ask
  for anything and learn what is there.

## Core (FM-1+VA today)

| Request | Reply kind | What |
|---|---|---|
| `10 <slot>` | `50` | Read a preset: 128-byte DX7 voice plus the 59-byte settings record |
| `04 <slot> <155-byte VCED> <record, 8-into-7 packed> <sum>` | none | Write a preset exactly, stored |
| `11 <address: 5x7 bits> <count: 2x7 bits>` | `51` | Read up to 256 bytes of memory |
| `20 <pattern> <part> <save> ...` | `52` | Write eight steps of a pattern |

Identity today is M-VAVE's updater handshake (`F0 00 32 45 00 00 00 40 7F F7`),
answered with a name such as `FM-1_093`.

## Additions

### 1. Hello: which firmware, and what it can do (*proposed* `12`, reply `53`)

The most useful single addition. Data, all ASCII text NUL-terminated:

- firmware name (`FM-1+VA`, `Felucca`, ...), version, author or URL
- protocol version of this document it follows (1)
- a capability bitmask: read presets, write presets, read and write the edit
  buffer, read and write patterns, read and write global settings
- counts: preset slots, patterns, steps per pattern, notes per step
- the preset format it uses, and whether a slot holds a preset or a project
  (next section)

With Hello, a tool picks the right profile by name instead of guessing from a
version number, and greys out what the firmware cannot do.

### 2. Presets in any format (*proposed* `13` read, `14` write, reply `54`)

A firmware with a different preset layout (other engines, samples, macro
models) reads and writes its presets as an opaque block with a format id and a
format version. A tool stores the block as it is (Virtual FM-1's JSON format
keeps raw bytes for exactly this) and only interprets it when it has a profile
that knows the format. FM-1+VA's `10`/`04` stay as they are for its format.

A firmware also says in Hello what one slot holds, since not every firmware
splits sounds and sequences the way FM-1+VA does: a *preset* (a sound only, with
patterns stored apart) or a *project* (a sound and its sequence saved together,
as Felucca does). A project travels as one block through the same commands, and
such a firmware reports no separate patterns.

### 3. The edit buffer: what is playing, unsaved (*proposed* `15` read, `16` write)

- **Read**: the current slot and the live preset, unsaved edits included.
  Today this needs RAM addresses that move with every build.
- **Write**: put a preset into the edit buffer without storing it. Today only
  some settings can be sent this way (by parameter changes and CCs); the
  filter on FM presets, distortion type, effect order, envelope off and knob
  assignments cannot.

### 4. Patterns, readable and complete (*proposed* `17` read, reply `55`)

Read a pattern as a structure rather than raw memory: settings, then each step
with its notes, velocities and per-step fields. Extend the write (`20`, or a
new command) with what it cannot carry today: ties and Tie & Slide, ratchet,
chance, step gate, step transpose, accent, per-pattern Chain.

### 5. Global settings (*proposed* `18` read, `19` write, reply `56`)

A list of `(id, value)` pairs, so new settings need no new messages. Proposed
ids: MIDI channel, FX channel, bend up, bend down, key velocity, glide mode,
glide time, drive, CC7 volume, overdub. A firmware sends the ones it has.

## Rules that keep it compatible

- A firmware answers a request it does not know with status 4, or not at all;
  tools treat silence after retries as "not supported".
- New fields go at the end of a reply; tools ignore what they do not know.
- Command numbers are agreed once and never reused.

## If a firmware uses a different protocol

That is fine. Virtual FM-1 adds a profile for it: the plugin's sync code asks
the profile for every operation, and the profile speaks whatever the firmware
speaks. The plugin works with a firmware as far as its protocol allows: one
that only takes DX7 data (like M-VAVE's own) gets sending sounds without
reading back, as it does today. Adopting this proposal only means nobody has
to write that profile.
