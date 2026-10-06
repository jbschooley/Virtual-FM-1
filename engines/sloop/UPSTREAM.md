# Sloop, vendored

`upstream/` is part of [SLOOP](https://github.com/isod89/sloop-fm1), isod89's
groovebox firmware for the M-VAVE FM-1, a fork of
[Felucca](https://github.com/hugelton/Felucca) (Leo Kuroshita, Hügelton
Instruments), at **v2.3** (commit `d691ba7b2d922f1a1f41a3622cffe29ce41c5506`,
2026-10-05). It is GPL-3.0-only; see `upstream/LICENSE` and
`upstream/LICENSING.md`.

Sloop forked from Felucca before 1.0 (about 0.9-beta) and has its own engines
(nine), parts (three synths and a drum track), project format and editor
commands above 32, so it is vendored and built apart from `engines/felucca`.

Copied unchanged:

- `firmware/src/` and `firmware/hal/`: the firmware
- `tests/regress.c`, `tests/hostsim.c`, `tests/golden.txt`: its regression
  (97 golden renders), run by ctest as `sloop_regress`. Its CPU budget depends
  on the host, so ctest gives it no baseline file and it only prints the counts
- `tools/gen_font.py`, `gen_icons.py`, `gen_tables.py`, `gen_samples.py`,
  `gen_drumkits.py`, `gen_logo.py`, `gen_waves.py`, `sampleio.py`: the
  generators
- `LICENSE`, `LICENSING.md`, `assets/fonts/Terminus-LICENSE.txt` and
  `assets/samples-cc0/ATTRIBUTION.txt`: its licences, and those of the font and
  samples compiled into it

`generated/` holds what its `tools/build.py` `generate()` makes, run in a clean
checkout of v2.3 with Python 3 and Pillow (it needs that checkout's `assets/`,
not copied): `felucca_font.h`, `felucca_icons.h`, `felucca_tables.h`,
`felucca_samples.h`, `felucca_drumkits.h` and `sloop_logo.h`. Generating twice
gave the same bytes.

## Licences

The code is GPL-3.0-only. Sloop's `LICENSING.md` still has Felucca's text from
before 1.0, under which the icon atlas compiled into `felucca_icons.h`
(`assets/icons.png`) is Hügelton Instruments' with "all rights reserved" and a
section 7 permission. Felucca 1.0 has since put its assets under GPL-3.0-only
with the rest; this project takes Sloop's copy of the icon atlas the same way.
The samples compiled in are CC0 (Versilian Studios VSCO-2 CE and VCSL, Sonic
Pi), the font is Terminus (SIL OFL 1.1).
