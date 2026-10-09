# Third-party code and licenses

- **msfa** (`engines/dx7/msfa`): Google's music-synthesizer-for-android FM core as
  carried in Dexed. Apache License 2.0. Copyright 2012 Google Inc., 2016-2025
  Pascal Gauthier, 2019 Jean Pierre Cimalando. Modifications are listed in
  `engines/dx7/msfa/NOTICE.md`.
- **Dexed UI** (`plugin/dexed_ui`): look-and-feel, envelope and pitch-envelope
  displays, image combo box, algorithm diagram, knob/switch/panel images and
  the Noto Sans font from Dexed. GPL-3.0-or-later, copyright 2013-2025 Pascal
  Gauthier and contributors; envelope tables from legasynth / DX7 Patch Editor.
  See `plugin/dexed_ui/NOTICE.md`.
- **Felucca** (`engines/felucca/upstream`, generated tables, FM6 patches and
  samples in `engines/felucca/generated`): Leo Kuroshita's FM-1 firmware,
  v1.0.2. GPL-3.0-only, copyright 2026 Leo Kuroshita (@kurogedelic), Hügelton
  Instruments, including the Hügelton Sample Pack drum sounds. Its other
  sample sets are the Versilian Studios samples (CC0). Parts of it come from
  others under their own licences, listed in its `LICENSING.md`: msfa
  (Apache-2.0) in the FM6 engine, and DaisySP and Rings (MIT) in the PHYS
  engine; their licence texts are in `upstream/LICENSES/`. Its screen's
  generated tables are rasterised from the Inter Tight font (SIL OFL 1.1, The
  Inter Project Authors) and the Fukiai icon font (MIT, Hügelton
  Instruments). See `engines/felucca/UPSTREAM.md`.
- **SLOOP** (`engines/sloop/upstream`, generated tables, samples, drum kits,
  font, FM6 patches and logo in `engines/sloop/generated`): isod89's FM-1 firmware,
  v2.5, a fork of Felucca. GPL-3.0-only, copyright 2026 Leo Kuroshita (@kurogedelic),
  Hügelton Instruments, and its contributors; its FM6 core (`fm6_core.c`) is
  msfa's, Apache-2.0 (`engines/sloop/upstream/LICENSES/Apache-2.0-msfa.txt`), and
  its PHYS engine's models are DaisySP's and Rings' (MIT, the notice in
  `phys_dsp.c` and `phys_symp.c`). Its samples are CC0 (Versilian
  Studios VSCO-2 CE and VCSL, Sonic Pi), its font Terminus (SIL OFL 1.1); see
  `engines/sloop/UPSTREAM.md`, which also says how its icon atlas is taken.
- **Melodee** (`engines/melodee/upstream`, generated tables, fonts, icons, FM6
  patches and CZ-1 tones in `engines/melodee/generated`): Kerem Kilic's FM-1
  firmware, v0.13, built from Felucca. GPL-3.0-only, copyright 2026 Leo Kuroshita
  (@kurogedelic), Hügelton Instruments, with modifications copyright 2026 Kerem
  Kilic. Parts of it come from others under their own licences, listed in its
  `LICENSING.md` with their texts in `upstream/LICENSES/`: msfa (Apache-2.0) and
  Dexed (GPL-3.0-or-later) in the FM6 engine, MAME's uPD933 model (BSD-3-Clause)
  in the CZ-1 engine, DaisySP and Rings (MIT) in the PHYS engine, the Inter
  Tight font (SIL OFL 1.1) and the Fukiai icons (MIT). Its microtonal scales
  come from the Huygens-Fokker Scala archive (`upstream/assets/scales/README.md`).
- **Casio CZ-1 preset tones** (`engines/melodee/upstream/assets/cz1-factory/cz1-factory.syx`,
  compiled into `engines/melodee/generated/melodee_cz1.h`): the CZ-1's 64
  factory sounds (Casio Computer Co., Ltd., 1986), included with Melodee as
  sound data for CZ-1 compatibility. As Melodee says, no licence is granted for
  them and the rights stay with Casio; they are carried here the same way. See
  `upstream/assets/cz1-factory/README.md` for where they come from.
- **Sequential Prophet-5/10 factory programs v1.03** (`engines/melodee/upstream/assets/prophet5-factory/prophet5-v1.03.syx`,
  compiled into `engines/melodee/generated/melodee_prophet_factory.h`): the 200
  programs of Sequential's factory bank (Sequential, 2025), included with Melodee
  as PROPHET's presets, as sound data for Prophet-5 compatibility. As Melodee
  says, no licence is granted for them and the rights stay with Sequential; they
  are carried here the same way. See `upstream/assets/prophet5-factory/README.md`.
- **JUCE 9** (`third_party/JUCE`, not vendored): dual-licensed under the
  AGPLv3 and a commercial licence. Open-source builds of this plugin use JUCE
  under the AGPLv3, which GPLv3 section 13 permits combining with.
- **VST3 SDK** 3.8 (bundled with JUCE): MIT licence.
- **Test vectors** (`tests/golden.json`): generated from baud girl's FM-1+VA
  web modules (GPL-3.0-or-later) by `tests/gen_golden.mjs`. Her modules are not
  vendored.

The FM-1's factory presets and firmware are not included.
