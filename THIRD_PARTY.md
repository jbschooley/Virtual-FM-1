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
- **Felucca** (`engines/felucca/upstream`, generated tables and samples in
  `engines/felucca/generated`): Leo Kuroshita's FM-1 firmware, v0.9-beta.
  GPL-3.0-only, copyright 2026 Leo Kuroshita (@kurogedelic), Hügelton
  Instruments. Its sample sets are the Versilian Studios samples (CC0) and
  the Hügelton Sample Pack drum sounds, which Felucca's LICENSING.md lists as
  all rights reserved. See `engines/felucca/UPSTREAM.md`.
- **JUCE 9** (`third_party/JUCE`, not vendored): dual-licensed under the
  AGPLv3 and a commercial licence. Open-source builds of this plugin use JUCE
  under the AGPLv3, which GPLv3 section 13 permits combining with.
- **VST3 SDK** 3.8 (bundled with JUCE): MIT licence.
- **Test vectors** (`tests/golden.json`): generated from baud girl's FM-1+VA
  web modules (GPL-3.0-or-later) by `tests/gen_golden.mjs`. Her modules are not
  vendored.

The FM-1's factory presets and firmware are not included.
