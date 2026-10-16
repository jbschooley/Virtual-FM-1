# Melodee, vendored

`upstream/` is part of [Melodee](https://github.com/keremimo/melodee), Kerem
Kilic's firmware for the M-VAVE FM-1, built from
[Felucca](https://github.com/hugelton/Felucca) (Leo Kuroshita, Hügelton
Instruments) 1.0, at **v1.4.1** (tag `v1.4.1`, commit
`1cccd6970e6e60f136862f3016de58617010c01e`, 2026-10-15, the commit its `BUILD_INFO.json` names).
Two earlier releases are kept beside it for the projects made with them: Melodee 0.13 in
`engines/melodee013` (1.0 retired its LOFI, PHASE, VOICE and NOISE and its project objects) and
Melodee 1.1.0 in `engines/melodee110` (1.2 removed its effects and retired ANALOG and DIGITAL; 1.3's
effects are new ones). It is GPL-3.0-only,
with parts under their own licences (`upstream/LICENSING.md`,
`upstream/LICENSES/`), and Casio's CZ-1 preset tones and Sequential's Prophet-5
factory programs, for which no licence is granted (below).

Melodee has its own engines (PROPHET, CZ-1, a Dexed-exact FM6, SID from 1.0; no sample engines),
32 pattern banks, its own project and backup objects and editor commands from
72, so it is vendored and built apart from `engines/felucca`.

Copied unchanged:

- `firmware/src/` and `firmware/hal/`: the firmware
- `tests/regress.c`, `tests/hostsim.c`, `tests/golden.txt`: its regression
  (its golden renders), run by ctest as `melodee_regress` with no CPU baseline, compiled
  as its `tests/run_tests.sh` compiles it (`-ffp-contract=off`: 1.0's DSP is single-precision
  float, and a fused multiply-add changes its renders)
- `tools/gen_aa_font.py`, `gen_aa_keycaps.py`, `gen_icons.py`, `aa_raster.py` (what those three
  import), `gen_ui_palettes.py`, `gen_tables.py`, `gen_fm6_patches.py`, `gen_909_tables.py`,
  `gen_cz1_factory.py`, `gen_prophet_factory.py`, `gen_scales.py`: the generators
- `LICENSE`, `LICENSING.md`, `LICENSES/`, `assets/fonts/OFL.txt`,
  `assets/scales/README.md`, `assets/cz1-factory/` (the CZ-1 tones and their
  README) and `assets/prophet5-factory/` (the Prophet-5 programs and their README)

`generated/` holds what its `tools/build.py` `generate()` makes, run in a clean
export of v1.4.1 with Python 3 and Pillow (it needs that checkout's `assets/`,
not all copied): `ui_fonts.h`, `ui_icons.h`, `ui_keycaps.h`, `ui_palettes.h`,
`melodee_tables.h`, `melodee_fm6.h`, `melodee_cz1.h` and
`melodee_prophet_factory.h`; and `x0x_drum_tables.h`, which its `build_app()` makes
(`tools/gen_909_tables.py`). Generating twice gave the same bytes.

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

- 1.4 keeps every user preset in one shared bank (U001-U384, `shared_presets.c`), stored in the parts
  the backup objects 6, 7 and 9..28 name; an older release's bank in those objects is moved into it
  at power-on (`up_boot`). `FEL(object_put)` runs `up_boot` after a preset bank is restored, so a 1.3
  backup or device file is taken as the FM-1 takes it.
- It is built as Melodee's single-core package was (1.4 retired the package, not the build option:
  `MELODEE_DUAL_CORE=0`: the second core's worker is
  the device's; here the plugin is the CPU), with no data cache taken as RAM (`MELODEE_CACHE_RAM=0`),
  and without fused multiply-adds (`-ffp-contract=off`, CMakeLists.txt `fm1CoreFlags_melodee`), as its
  host tests are, so it renders as its golden renders say.
- `melodee_core.c` compiles the whole firmware as one unit, in its
  `melodee.c`'s order, behind the same API as `engines/felucca/felucca_core.c`
  (`felucca_core.h`), and is compiled once the same way, its functions
  prefixed `mel_`, its state (about 1.1 MB, its RAM flash included) a block each
  instance owns (`engines/felucca/fel_state.py`; `docs/ADDING-A-FIRMWARE.md`,
  "A firmware built from its C source"). `FeluccaEngine` plays it with
  `Flavor::Melodee`. Its `gfx.c` initializes a pointer with the address of its
  own screen buffer (`cv_px = cv_primary`): `fel_state.py` sets that per
  instance.
- The hardware is replaced as for Felucca: the panel and screen are the
  plugin's, the flash is RAM, the clock follows the audio rendered and the main
  loop runs every 16 ms of it. Not built: `lcd.c`, `audio.c`, the update and
  boot loader (`ota.c`), the serial console and the USB audio device. Its audio
  memory arena (`resources.c`, 0.13) is the host build's 128 KB array, in each
  instance's state; on the device its size is the linker's, so running out of it
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
- `tests/melodee_test.cpp` checks its instances (many at once, on several threads), sound (a CZ-1 tone and a PROPHET program too),
  parameters, the editor protocol and the objects (a project slot across its
  extension sectors, a CZ bank); `tests/melodee-frozen.txt` keeps its parameters
  and factory presets, so a later Melodee that changes them is noticed.

## Updating

1. In a clean checkout of the new tag, run its `tools/build.py` `generate()` (twice: the
   same bytes) and replace `upstream/` and `generated/` with the new files; update the version
   and commit above, and the release in `firmwares/Firmwares.cpp` (`knownVersions`).
2. Compare `melodee_core.c` with the new `melodee.c` (its include order: a new source file is included
   there too, inside the section pragma) and `main.c` (its init and main loop), and keep to
   `docs/ADDING-A-FIRMWARE.md`'s "A firmware built from its C source": every writable variable
   inside the pragma, so it is per instance. The build stops with `fel_state: ...` on what the
   rewrite cannot handle; the line it prints says where.
3. Build and run ctest: `melodee_regress` must pass on the new goldens, `melodee_test` checks the
   plugin's side (its instance checks included), and its frozen list says what changed in
   Melodee's parameters and presets.
