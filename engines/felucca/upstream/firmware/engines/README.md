# Felucca-Engines

Five synthesis engines of [Felucca](https://github.com/hugelton/Felucca), with the DSP helpers
and the tables they share, under the MIT licence. Plain C99, fixed point only (no float, no
allocation, no library calls), written for a small 32-bit microcontroller at 44.1 kHz.

| Engine | File | What it is |
| --- | --- | --- |
| ANALOG | `src/eng_analog.c` | Two band-limited oscillators (saw, square, triangle, sine, PWM; hard sync and a sub oscillator), noise, drive, a trapezoidal state-variable filter (low-, band- or high-pass) |
| LOFI | `src/eng_lofi.c` | A chip voice: pulse, triangle, saw, shift-register noise and a 32 x 4-bit wave memory (16 tables), bit depth, sample hold, pitch sweep, arpeggio, a stepped volume envelope |
| TRIO | `src/eng_trio.c` | Three oscillators with ring modulation, hard sync and combined (AND) waves into a 12 dB/oct multimode filter |
| WHEEL | `src/eng_wheel.c` | A tonewheel-style additive organ: nine drawbar partials, 16 registrations, percussion, key click, drive and a rotary effect |
| NOISE | `src/eng_noise.c` | White / pink / brown noise, random impulses and shift-register noise through a resonant filter, with drift and a bit crusher |

Shared:

- `src/felucca_engine.h`: the engine interface (parameter descriptors, the voice, its per-block
  modulation, presets, the engine table entry) and what a host must provide.
- `src/dsp.c`: oscillators with polyBLEP, the state-variable filter, soft clipping, noise, the
  amplitude ramp (all `static inline`).
- `tools/gen_engine_tables.py`: the lookup tables (`SINE`, `PITCH_OCT` / `pitch_inc()`, `SVF_G`,
  `TANH_Q15`, `FS`, `CTL`) as a C header.

## Using the engines

The engines are compiled as one translation unit with their host, in this order:

```c
#define NVOICE 8                    /* voices per part */
#define NPART 4                     /* parts */
#include "fe_tables.h"              /* python3 tools/gen_engine_tables.py fe_tables.h */
#include "felucca_engine.h"
enum { P_ATK, P_REL, P_FTYPE, P_E0, P_E1, P_E2, P_E3, P_E4, P_E5, P_E6, P_E7, P_COUNT };
typedef struct track { int16_t p[P_COUNT]; voice_t v[NVOICE]; } track_t;
static track_t trk[NPART];
#include "dsp.c"
#include "eng_analog.c"             /* ... the engines you want */
```

Each engine is an `engine_t` (`ENG_ANALOG`, `ENG_LOFI`, `ENG_TRIO`, `ENG_WHEEL`, `ENG_NOISE`):
its eight parameters with their ranges and names, its presets, and the functions the host calls
per voice (`note_on`, `render`) and per block (`block`, `amp`). `render` adds one block (at most
`CTL` = 32 samples) of a voice to a buffer. The header documents the rest of the contract: the
host's track and parameter numbers, how a voice is started and retriggered, and the envelope
fields that LOFI and WHEEL read.

`tests/fe_host.c` is a complete, minimal host: it plays every preset through a short phrase.

## Tests

```sh
sh tests/run.sh
```

builds the tables and the host, renders 108 phrases and compares a hash of each with
`tests/golden.txt`. A change of any engine's output fails. Felucca runs the same host with its own
generated tables, so the two cannot drift apart.

## In Felucca

Felucca includes this repository as a git submodule (`engines/`, next to its `src/`). Its
`engines.c` includes `dsp.c` and the five `eng_*.c` files from here, its `core.h` includes
`felucca_engine.h`, and its table generator imports `tools/gen_engine_tables.py`. The firmware
built from them is GPL-3.0-only as a whole; the files of this repository stay MIT.

Clone Felucca with `git clone --recurse-submodules` (or run `git submodule update --init` in a
clone): its complete source includes this repository.

## Credits

The state-variable filter follows Andrew Simper's trapezoidal design; the pink noise filter uses
Paul Kellet's coefficients (the "economy" version), in fixed point. polyBLEP and xorshift32 are
the well-known techniques.

## Licence

MIT, see [LICENSE](LICENSE). Copyright (c) 2026 Leo Kuroshita (@kurogedelic), Hügelton
Instruments.

Changes to these files are welcome as pull requests here, under the MIT licence.
