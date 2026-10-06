# Felucca, vendored

`upstream/` is part of [Felucca](https://github.com/hugelton/Felucca), Leo
Kuroshita's firmware for the M-VAVE FM-1 (Hügelton Instruments), at
**v1.0.3** (commit `b22a24b64b09ccd9d7e7ddd245858b5d4283365c`, 2026-10-06).
It is GPL-3.0-only; see `upstream/LICENSE` and `upstream/LICENSING.md`. The
author has agreed to its use in Virtual FM-1 under the GPL
([issue #1](https://github.com/hugelton/Felucca/issues/1)).

Copied unchanged:

- `firmware/src/` and `firmware/hal/`: the DSP, sequencer and the rest of the
  firmware (the plugin builds only what Felucca's own host build builds)
- `tests/regress.c`, `tests/hostsim.c`, `tests/golden.txt`: Felucca's
  regression (87 golden renders), run by ctest as `felucca_regress`. From 1.0 to 1.0.2
  three changed, all where SAMPLE's PERC set played (retired in 1.0.2: such a sound is
  the DRUM engine's kit now); none from 1.0.2 to 1.0.3
- `tools/gen_tables.py`, `gen_samples.py`, `gen_waves.py`,
  `gen_fm6_patches.py`, `gen_aa_font.py`, `gen_aa_icons.py`,
  `gen_aa_keycaps.py`, `gen_ui_palettes.py`, `aa_raster.py`: the generators
- `LICENSE`, `LICENSING.md`, `LICENSES/`: its licences, and those of the
  parts it takes from others (msfa in FM6, DaisySP and Rings in PHYS)

`generated/` holds what Felucca's build generates and the plugin needs:
`felucca_tables.h`, `felucca_fm6.h` (FM6's factory patches),
`felucca_samples.h` (the sample sets) and the screen's `ui_fonts.h`,
`ui_icons.h`, `ui_keycaps.h` and `ui_palettes.h`, made in a full checkout of
that version as its `tools/build.py` makes them (`generate()`): for example
`python3 tools/gen_aa_font.py ui_fonts.h --preset inter-tight`. The samples
and the font need `assets/`, the icons `web/fukiai.ttf`; neither is copied.

## Samples

`felucca_samples.h` includes the CC0 Versilian sets and the drum sounds that
`tools/gen_waves.py` makes, the "Hügelton Sample Pack". Since 1.0 Felucca's
`LICENSING.md` puts the Sample Pack under GPL-3.0-only with the rest.

## The plugin's side

- `felucca_core.c` compiles the whole firmware as one unit, in
  `felucca.c`'s order, the way Felucca's host tests (`tests/ui_test.c`,
  `editor_test.c`, `backup_test.c`) build it, with a prefix per compiled copy:
  sound, sequencer, screen and front panel, projects, user presets, the FM6
  bank and the editor protocol. The hardware is replaced: the panel and screen
  are the plugin's, the flash is RAM, the clock follows the audio rendered and
  the main loop runs every 16 ms of it. Only `main.c`'s `felucca_init` is
  repeated. Never built: the update and boot loader code (`ota.c`, `main.c`'s
  boot loader paths); requests for them are taken and ignored.
- `FeluccaEngine` gives each plugin instance one of the compiled copies, alone
  while there are no more instances than copies; beyond that instances share
  one, each instance's state saved out of the copy and put back before it
  plays (`felucca_core.c` `state_get` / `state_put`). An instance always plays
  in the copy it started in, since its state holds pointers into that copy.
- What every copy reads and none writes is compiled once: CMakeLists.txt writes,
  from `generated/felucca_samples.h`, a copy where `SMP_DATA` (the sample sets)
  is only declared, and `felucca_shared.c` defining it, with the empty user
  sample slots. A new `gen_samples.py` that names it differently stops the
  configure with a message.
- Felucca 1.0 has four parts and no separate drum track (DRUM is an engine,
  on part 4 at power-on). Engine 1 (DIGITAL) is retired and not built
  (`FELUCCA_FM4` 0): a DIGITAL sound arrives as FM6 with a converted patch,
  as on the device, and the operator envelopes (`P_FM1_ATK` ..
  `P_FM4_LEVEL`), which only DIGITAL reads, are neither shown nor automated.
- The main loop's pass in the plugin is `ed_service`, `ui_input` (which runs
  `fm6_poll`) and `settings_poll`; the screen is drawn when the plugin shows
  it. HOME > PANEL (calibrating the device's button matrix) only says
  "PANEL: ON THE FM-1": the plugin's buttons go by label.
- The editor protocol answers SysEx from the host, but its replies and
  pushes are not sent back to the host (they go to the plugin's own sync).
- The LEDs are not shown: `fm1_led` and, since 1.0.2, `fm1_led_dim` and
  `fm1_led_dim_level` (the idle glow) are stubs, as in Felucca's `tests/ui_test.c`.
- Not yet: user sample slots (USR1..USR3 are empty), the LEDs, and MIDI clock in (`G_CLOCK`
  USB or TRS: the plugin gives the host's tempo instead).

## Updating

1. Check out the new release of Felucca, generate the seven headers as above,
   and run its `tests/run_tests.sh` there.
2. Replace `upstream/` and `generated/` with the new files, and update the
   version and commit above.
3. Compare `felucca_core.c` with the new `felucca.c` (its include order),
   `main.c` (`felucca_init`, the main loop's pass) and the host tests' stubs
   (`tests/ui_test.c`, `editor_test.c`, `backup_test.c`); its non-static
   globals must still get a name per copy (`nm` on one copy's object: only
   `fel*_` symbols).
4. Build and run ctest: `felucca_regress` must pass on the new goldens, and
   `felucca_test` checks the plugin's side. If the goldens changed, the
   sound changed: see the multi-firmware plan on keeping old versions.
