# Melodee licensing

Melodee is a modified version of Felucca by Leo Kuroshita (@kurogedelic), Hügelton Instruments
(<https://github.com/hugelton/Felucca>), and keeps Felucca's licensing.

Melodee is free software, licensed under the GNU General Public License, version 3 only
(`GPL-3.0-only`, full text in `LICENSE`). That covers the code and its own assets. A few
bundled or ported parts keep their own licences; they are listed below, and their licence
texts are in `LICENSES/`.

Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments\
Modifications Copyright (C) 2026 Kerem Kilic (Ellic Studio)

## What is GPL-3.0-only

Every file in this tree that carries an `SPDX-License-Identifier: GPL-3.0-only` header:

- the firmware: `firmware/` (app, HAL, update loader)
- the build script and tools: `build.sh`, `tools/`
- the web pages (installer, editor) and their tests: `web/` (not the Fukiai font, below)
- the host tests: `tests/`

Three source files are ports and keep the licence of their originals: `firmware/src/phys_dsp.c`
(DaisySP, MIT), `firmware/src/phys_symp.c` (Rings, MIT), and `firmware/src/cz_native.c`
(MAME uPD933, BSD-3-Clause). `firmware/src/fm6_core.c` and
`firmware/src/eng_fm6.c` restate MSFA (Apache-2.0) and Dexed (GPL-3.0-or-later) in C with native fp32 signal arithmetic (below).
The firmware built with them is GPL-3.0-only as a whole.

You may use, study, change and share Melodee under the GPL. If you distribute Melodee, or
firmware derived from it, you must also give your recipients its complete corresponding
source under the same licence. That includes devices that ship with modified Melodee inside.

## Hügelton Instruments' own work

All by Hügelton Instruments (Leo Kuroshita), in this tree:

| What | Licence | Where |
| --- | --- | --- |
| Felucca: firmware, tools, web pages, tests | GPL-3.0-only | `LICENSE` |
| The PHASE engine's waveforms: a C port of the oscillator of CrispyZebra (<https://github.com/hugelton/CrispyZebra>) | GPL-3.0 | `firmware/src/eng_phase.c` |
| The synthesized 808 drum kit (TR-808 circuit models) | GPL-3.0-only | `firmware/src/drum_808.c`, `firmware/src/eng_drum.c` |
| The panel picture | GPL-3.0-only | `docs/panel.jpg` |
| The Fukiai icon font (<https://github.com/hugelton/Fukiai>): the web editor's icons (the firmware draws its own: `tools/gen_icons.py`, GPL-3.0-only; `tools/gen_aa_icons.py` still rasterises Fukiai for comparison) | MIT | `web/fukiai.ttf`, `LICENSES/MIT-Fukiai.txt`, `web/FUKIAI-LICENSE.txt` |

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Rubik font by The Rubik Project Authors (<https://github.com/googlefonts/rubik>): the UI text and the keycap labels, rasterised into the firmware at build time (`tools/gen_aa_font.py`, `tools/gen_aa_keycaps.py`; the generated tables are not offered as a font, and the font declares no Reserved Font Name) | SIL OFL 1.1 | `assets/fonts/Rubik[wght].ttf`, `LICENSES/OFL-Rubik.txt` |
| Inter Tight font by The Inter Project Authors: the panel diagram (`tools/gen_panel_diagram.py`, `docs/panel.jpg`) and the `inter-tight` preset of `tools/gen_aa_font.py` (the firmware's text was Inter Tight up to Melodee 0.13) | SIL OFL 1.1 | `assets/fonts/InterTight[wght].ttf`, `LICENSES/OFL-InterTight.txt` (also `assets/fonts/OFL.txt`) |
| DaisySP by Electrosmith, Corp and Emilie Gillet (<https://github.com/electro-smith/DaisySP>): the PHYS engine's modal and string models and the resonator, ported to fixed point | MIT | `firmware/src/phys_dsp.c`, `LICENSES/MIT-DaisySP.txt` |
| Rings by Emilie Gillet (<https://github.com/pichenettes/eurorack>): the PHYS engine's sympathetic strings, ported to fixed point | MIT | `firmware/src/phys_symp.c`, `LICENSES/MIT-Rings.txt` |
| MSFA (Music Synthesizer for Android, Copyright 2012 Google Inc.) and Dexed (Copyright 2013-2025 Pascal Gauthier, <https://github.com/asb2m10/dexed>; portamento rates by Jean Pierre Cimalando): the FM6 engine restates their synthesis in C with native fp32 signals and discrete DX7 envelopes/ROMs (MSFA's envelopes, pitch envelope, LFO, operator kernels and Dx7Note, Apache-2.0; Dexed's MARK I and OPL engines and its voice handling, GPL-3.0-or-later); their DX7 measurement tables and the tables Dexed computes at start (sine, 2^x, frequency, log-sine / exponent, OPL ROM, detune, LFO, portamento) are used as data. `tests/dexed_ref.cc` builds Dexed's own sources (from a checkout, not in this tree) to compare against. The FM6 factory patches are Felucca's own (F1..F8) and Melodee's own (F9..F24) | Apache-2.0, GPL-3.0-or-later | `firmware/src/fm6_core.c`, `firmware/src/eng_fm6.c`, `tools/gen_tables.py`, `tests/dexed_ref.cc`, `LICENSES/Apache-2.0-msfa.txt` |
| reSID by Dag Lem (<https://github.com/libsidplayfp/resid>): the SID engine restates in C, at the register level, its waveform generator (accumulator, noise register, ring modulation, hard sync), envelope generator (rate and exponential counters, the ADSR delay), its R-2R DAC model (reSID 1.0 `dac.cc`), the 6581 / 8580 filter's resonance laws and output sum and the C64 output stage; its measured 6581 and 8580 cutoff curves and its spline interpolation (reSID 0.16) make the cutoff tables at build time; its samples of a real 6581's and 8580's combined waveforms (`wave*.dat`, unchanged) are packed into the firmware as data. Used under GPL-3.0 | GPL-2.0-or-later | `firmware/src/eng_sid.c`, `tools/gen_tables.py`, `assets/resid/` |
| schwung-space-delay (TapeDelay for Schwung on Ableton Move) by Charles Vestal (<https://github.com/charlesvestal/schwung-space-delay>), Copyright (c) 2025 Charles Vestal: where the tape delay originates (a delay line read through a flutter LFO, a one-pole tone on the repeats, soft saturation in the feedback, ping-pong), by way of fm1-x0x below | MIT | `firmware/src/delay.c`, `LICENSES/MIT-schwung-space-delay.txt` |
| fm1-x0x by Charles Vestal (<https://github.com/charlesvestal/fm1-x0x>): the delay bus's DIGI / TAPE / ping-pong modes as Melodee has them (its `fxbus.c` `fx_delay`: TAPE after schwung-space-delay above; DIGI is 9W9's `er99_dly_tick` by athousanddetails, <https://github.com/athousanddetails/schwung-9W9>, after ER-99 by Matthew Cieplak), ported from float to fixed-point C at half the rate | GPL-3.0-only | `firmware/src/delay.c` |
| MAME uPD933 device model by Devin Acker: native CZ phase functions, envelope rate law and logarithmic amplitude adapted to C with native fp32 playback | BSD-3-Clause | `firmware/src/cz_native.c`, `LICENSES/BSD-3-Clause-uPD933.txt` |
| Casio CZ-1 preset tones A-1..H-8 (Casio Computer Co., Ltd., 1986): the 64 factory sounds as native tone data, combined from the "64 original CZ-1 patches" SysEx (GeoCities CZ-101 page, archived 2009) and Oli Larkin's VirtualCZ conversions of the same presets (LCD names, line levels, velocity). Included as sound data for CZ-1 compatibility; the rights stay with Casio. See `assets/cz1-factory/README.md` | Casio's (no licence granted) | `assets/cz1-factory/cz1-factory.syx`, `tools/make_cz1_factory.py`, `tools/gen_cz1_factory.py` |
| Sequential Prophet-5/10 factory programs v1.03 (Sequential, 2025; <https://sequential.com/support/download/prophet-5-10-sounds/>): the 200 programs of `P5_Factory_Programs_USER_v1.03.syx`, unchanged native data, as PROPHET's factory presets and its default native user collection. Included as sound data for Prophet-5 compatibility; the rights stay with Sequential. See `assets/prophet5-factory/README.md` | Sequential's (no licence granted) | `assets/prophet5-factory/prophet5-v1.03.syx`, `tools/gen_prophet_factory.py` |
| klattsch by Tony Gies (<https://github.com/tgies/klattsch>): design reference for the VOICE engine; no code copied. Formant data from Klatt (1980) / Hillenbrand et al. (1995) | MIT (klattsch) | credit only |
| JieLi AC79 SDK by JieLi Technology: three of its files go into every `.fwsc` package (below); none are in this tree | Apache-2.0 | `LICENSES/Apache-2.0.txt` |

On the device, HOME held > ABOUT opens the information screen; turning PRESETS scrolls on into
CREDITS, a short list of these authors, licences and source URLs.

## JieLi SDK files in the packages (Apache-2.0)

A `.fwsc` package made by `tools/build.py` (with `tools/fm1pkg_make.py`; this is the package the
web installer installs) holds three unmodified files from the JieLi AC79 SDK
(<https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK>, `cpu/wl82/tools/`). They are read from your SDK
checkout at build time (see BUILDING.md); no SDK files are in this tree.

| File in the package | What it is |
| --- | --- |
| `uboot.boot` | the first-stage boot loader (SPL) |
| `cfg_tool.bin` | the chip configuration block |
| `eq_cfg_hw.bin` | the default hardware EQ table |

They are licensed under the Apache License, Version 2.0 (`LICENSES/Apache-2.0.txt`), not under
the GPL, and their copyright stays with JieLi Technology. The SDK has no NOTICE file. Apache-2.0
files may be distributed together with GPL-3.0 code; Melodee itself stays GPL-3.0-only.

Every distribution of a package carries the licence texts with it: the release folder
(`tools/build.py --release`) and the site (`web/make_site.py`, next to the package in
`firmware/` and linked from the installer page).

## No vendor material

No M-VAVE material is part of Melodee. The firmware links no vendor code and contains no vendor
data, and the packages hold only Melodee and the three SDK files above. The installer's
"Return to official V15" holds only the official file's size and SHA-256: you select the
official firmware file you downloaded yourself, and it is checked and installed in your browser,
never uploaded or redistributed.

## Contributions

Contributions are welcome under GPL-3.0-only.

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments.

"M-VAVE" and "FM-1" are trademarks of their respective owners. Melodee is independent
firmware that runs on FM-1 hardware. It is not affiliated with, endorsed by or supported
by those owners, nor by Hügelton Instruments.

## Radio

Melodee never enables the Bluetooth / Wi-Fi radio of the hardware.
