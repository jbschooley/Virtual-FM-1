# Melodee, vendored

`upstream/` is part of [Melodee](https://github.com/keremimo/melodee), Kerem
Kilic's firmware for the M-VAVE FM-1, built from
[Felucca](https://github.com/hugelton/Felucca) (Leo Kuroshita, Hügelton
Instruments) 1.0, at **v0.13** (tag `v0.13`, commit
`ed42da642be26562dcf5601bd33c5b905733f2d2`, 2026-10-08). It is GPL-3.0-only,
with parts under their own licences (`upstream/LICENSING.md`,
`upstream/LICENSES/`), and Casio's CZ-1 preset tones and Sequential's Prophet-5
factory programs, for which no licence is granted (below).

Melodee has its own engines (PROPHET, CZ-1, a Dexed-exact FM6; no sample engines),
32 pattern banks, its own project and backup objects and editor commands from
72, so it is vendored and built apart from `engines/felucca`.

Copied unchanged:

- `firmware/src/` and `firmware/hal/`: the firmware
- `tests/regress.c`, `tests/hostsim.c`, `tests/golden.txt`: its regression
  (340 golden renders), run by ctest as `melodee_regress` with no CPU baseline
- `tools/gen_aa_font.py`, `gen_aa_icons.py`, `gen_aa_keycaps.py`,
  `gen_ui_palettes.py`, `gen_tables.py`, `gen_fm6_patches.py`,
  `gen_cz1_factory.py`, `gen_prophet_factory.py`, `gen_scales.py`,
  `gen_waves.py`, `sampleio.py`: the generators
- `LICENSE`, `LICENSING.md`, `LICENSES/`, `assets/fonts/OFL.txt`,
  `assets/scales/README.md`, `assets/cz1-factory/` (the CZ-1 tones and their
  README) and `assets/prophet5-factory/` (the Prophet-5 programs and their README)

`generated/` holds what its `tools/build.py` `generate()` makes, run in a clean
export of v0.13 with Python 3 and Pillow (it needs that checkout's `assets/`,
not all copied): `ui_fonts.h`, `ui_icons.h`, `ui_keycaps.h`, `ui_palettes.h`,
`melodee_tables.h`, `melodee_fm6.h`, `melodee_cz1.h` and
`melodee_prophet_factory.h`. Generating twice gave the same bytes.

## The CZ-1 tones and the Prophet-5 programs

`assets/cz1-factory/cz1-factory.syx` (and `generated/melodee_cz1.h`, made from
it) holds Casio's 64 CZ-1 preset tones. Melodee includes them "as sound data
for compatibility with the instrument" and grants no licence for them; the
rights stay with Casio. `assets/prophet5-factory/prophet5-v1.03.syx` (and
`generated/melodee_prophet_factory.h`) holds the 200 programs of Sequential's
Prophet-5/10 v1.03 factory bank, PROPHET's presets and the start of its user
slots, included by Melodee as sound data for Prophet-5 compatibility, with no
licence granted; the rights stay with Sequential. This project carries both the
same way, unchanged.

## The plugin's side

- `melodee_core.c` compiles the whole firmware as one unit, in its
  `melodee.c`'s order, behind the same API as `engines/felucca/felucca_core.c`
  (`felucca_core.h`), and is compiled `MELODEE_COPIES` times (16) the same way:
  each copy with its own prefix (`mel0_` ...) and sections, an instance's state
  saved out of its copy and put back when instances share one.
  `FeluccaEngine` plays it with `Flavor::Melodee`, from a pool of its own.
- The hardware is replaced as for Felucca: the panel and screen are the
  plugin's, the flash is RAM, the clock follows the audio rendered and the main
  loop runs every 16 ms of it. Not built: `lcd.c`, `audio.c`, the update and
  boot loader (`ota.c`), the serial console and the USB audio device. Its audio
  memory arena (`resources.c`, 0.13) is the host build's 128 KB array, in each
  copy's state; on the device its size is the linker's, so running out of it
  may differ.
- The flash: Melodee keeps far more than Felucca (98 sectors when every object
  is used: bank projects of five sectors and a two-sector extension per copy,
  eight CZ banks, user preset and tone pools). A sector takes RAM when first
  written; one never written reads erased. A project is read across its sectors,
  and `editor_backup.c`'s flash pointer (`ED_BK_FLASH_PTR`) is only a placeholder:
  its reads, and `FEL(object_get)`'s, go through `st_read_range`.
- The stored objects are those of its editor backup: 0 the music (all 32 pattern
  banks), 1 the settings and template, 2-5 the projects, 6, 7, 17 and 18 the user
  preset banks, 8 retired, 9-16 the CZ banks, 19-22 the FM6 and native tone
  pools, 23-27 the PROPHET user banks.
- The LEDs: Melodee has two dim planes, the keys a layout shows and the buttons'
  idle glow; `FEL(leds)` gives them as 2 and 1. It lights PLAY's own LED while
  playing, not Felucca's green one.
- `tests/melodee_test.cpp` checks the copies, sound (a CZ-1 tone and a PROPHET program too),
  parameters, the editor protocol and the objects (a project slot across its
  extension sectors, a CZ bank); `tests/melodee-frozen.txt` keeps its parameters
  and factory presets, so a later Melodee that changes them is noticed.
