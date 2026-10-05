# msfa engine

These files are Google's music-synthesizer-for-android FM core ("msfa") as
carried in Dexed (https://github.com/asb2m10/dexed, Source/msfa), licensed
under the Apache License 2.0. Copyright 2012 Google Inc., 2016-2025 Pascal
Gauthier, 2019 Jean Pierre Cimalando.

Modifications made in this project:

- `controllers.h`, `env.cc`: removed the `#include "../Dexed.h"` dependency.
- `fm_core.cc`: algorithms 4 and 6 use `0x41` for the first operator, as in
  Google's original table and in the M-VAVE FM-1 firmware (Dexed uses `0xc1`).
- `tuning.h`, `tuning.cc`: replaced Dexed's Surge-based tuning support with a
  standard 12-TET implementation.
- `libMTSClient.h`: stub; MTS-ESP is not supported.
- `freqlut.cc`, `env.*`, `pitchenv.*`, `lfo.*`, `porta.*`: the sample-rate tables
  and factors are `thread_local`, so instances at different sample rates, or
  rendered on different threads, do not overwrite each other's tables
  (`FmSynth::useRate` in `engines/dx7/FmSynth.cpp` sets them).
