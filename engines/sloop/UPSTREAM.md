# Sloop, vendored

`upstream/` is part of [SLOOP](https://github.com/isod89/sloop-fm1), isod89's
groovebox firmware for the M-VAVE FM-1, a fork of
[Felucca](https://github.com/hugelton/Felucca) (Leo Kuroshita, Hügelton
Instruments), at **v2.5** (commit `fa9ce5742f4aea9484e7c129dd81079d07e7321f`,
2026-10-08). It is GPL-3.0-only, but for its FM6 core (`fm6_core.c`, from
msfa: Apache-2.0, `upstream/LICENSES/Apache-2.0-msfa.txt`); see
`upstream/LICENSE` and `upstream/LICENSING.md`.

Sloop forked from Felucca before 1.0 (about 0.9-beta) and has its own engines
(twelve from 2.5: FM6 joined them in 2.4, PHYS and NOISE from Felucca 1.0 in 2.5), parts (three synths and a drum track), project format and editor
commands above 32, so it is vendored and built apart from `engines/felucca`.

Copied unchanged:

- `firmware/src/` and `firmware/hal/`: the firmware
- `tests/regress.c`, `tests/hostsim.c`, `tests/golden.txt`: its regression
  (182 golden renders), run by ctest as `sloop_regress`. Its CPU budget depends
  on the host, so ctest gives it no baseline file and it only prints the counts
- `tools/gen_font.py`, `gen_icons.py`, `gen_tables.py`, `gen_samples.py`,
  `gen_drumkits.py`, `gen_logo.py`, `gen_waves.py`, `gen_fm6_patches.py`,
  `sampleio.py`: the generators
- `LICENSE`, `LICENSING.md`, `LICENSES/Apache-2.0-msfa.txt`, `assets/fonts/Terminus-LICENSE.txt` and
  `assets/samples-cc0/ATTRIBUTION.txt`: its licences, and those of the font and
  samples compiled into it

`generated/` holds what its `tools/build.py` `generate()` makes, run in a clean
checkout of v2.5 with Python 3 and Pillow (it needs that checkout's `assets/`,
not copied): `felucca_font.h`, `felucca_icons.h`, `felucca_tables.h`,
`felucca_samples.h`, `felucca_drumkits.h`, `felucca_fm6.h` and `sloop_logo.h`.
Generating twice gave the same bytes.

## Licences

The code is GPL-3.0-only. Sloop's `LICENSING.md` still has Felucca's text from
before 1.0, under which the icon atlas compiled into `felucca_icons.h`
(`assets/icons.png`) is Hügelton Instruments' with "all rights reserved" and a
section 7 permission. Felucca 1.0 has since put its assets under GPL-3.0-only
with the rest; this project takes Sloop's copy of the icon atlas the same way.
The samples compiled in are CC0 (Versilian Studios VSCO-2 CE and VCSL, Sonic
Pi), the font is Terminus (SIL OFL 1.1).

## The plugin's side

- `sloop_core.c` compiles the whole firmware as one unit, in its `felucca.c`'s
  order, behind the same API as `engines/felucca/felucca_core.c`
  (`felucca_core.h`), and is compiled once the same way, its functions
  prefixed `slp_`, its state (about 700 KB) a block each instance owns
  (`engines/felucca/fel_state.py`; `docs/ADDING-A-FIRMWARE.md`, "A firmware
  built from its C source"). `FeluccaEngine` plays it with `Flavor::Sloop`.
- The hardware is replaced as for Felucca: the panel and screen are the
  plugin's, the flash is RAM, the clock follows the audio rendered and the main
  loop runs every 16 ms of it. Not built: `lcd.c`, `audio.c`, the update and
  boot loader (`ota.c`, `recovery.c`), the serial console, the TRS MIDI input
  and the USB audio input. The LEDs are stubs; the MASTER knob is not read: the
  level stays at the 2048 `felucca_init` starts with (the plugin has its own
  volume).
- The sample sets (`SMP_DATA`) are compiled apart, read only, not state
  (`sloop_shared.c`, written by CMakeLists.txt), with the empty user sample
  slots.
- The stored objects are those of Sloop's editor backup (v10): 0 the working
  project, 1 the settings, 2-5 the projects A-D, 6 and 7 the user preset banks,
  8 the FM6 patch bank (read in place on the device, from its RAM sectors here:
  `FM6_BANK_XIP`), 9 the SYN drum kits (2.5, kept with the settings). The user
  sample slots (32-35) are not kept.
- The visualiser (`ui_vis.c`, 2.4) reads the mix left and right, fed in
  `FEL(render)` from fx.c's `vis_tap` as audio.c's `audio_block` does.
- `tests/sloop_test.cpp` checks its instances (many at once, on several threads), sound, parameters, the editor
  protocol and the objects; `tests/sloop-frozen.txt` keeps its parameters and
  factory presets, so a later Sloop that changes them is noticed.

## Updating

1. In a clean checkout of the new tag, run its `tools/build.py` `generate()` (twice: the
   same bytes) and replace `upstream/` and `generated/` with the new files; update the version
   and commit above, and the release in `firmwares/Firmwares.cpp` (`knownVersions`).
2. Compare `sloop_core.c` with the new `felucca.c` (its include order: a new source file is included
   there too, inside the section pragma) and `main.c` (its init and main loop), and keep to
   `docs/ADDING-A-FIRMWARE.md`'s "A firmware built from its C source": every writable variable
   inside the pragma, so it is per instance. The build stops with `fel_state: ...` on what the
   rewrite cannot handle; the line it prints says where.
3. Build and run ctest: `sloop_regress` must pass on the new goldens, `sloop_test` checks the
   plugin's side (its instance checks included), and its frozen list says what changed in
   SLOOP's parameters and presets.
