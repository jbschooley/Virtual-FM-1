# Felucca, vendored

`upstream/` is part of [Felucca](https://github.com/hugelton/Felucca), Leo
Kuroshita's firmware for the M-VAVE FM-1 (Hügelton Instruments), at
**v1.0** (commit `727f272015da26eb2d0291bd652eba28ff57cb37`, 2026-10-05).
It is GPL-3.0-only; see `upstream/LICENSE` and `upstream/LICENSING.md`. The
author has agreed to its use in Virtual FM-1 under the GPL
([issue #1](https://github.com/hugelton/Felucca/issues/1)).

Copied unchanged:

- `firmware/src/` and `firmware/hal/`: the DSP, sequencer and the rest of the
  firmware (the plugin builds only what Felucca's own host build builds)
- `tests/regress.c`, `tests/hostsim.c`, `tests/golden.txt`: Felucca's
  regression (87 golden renders), run by ctest as `felucca_regress`
- `tools/gen_tables.py`, `gen_samples.py`, `gen_waves.py`,
  `gen_fm6_patches.py`: the generators
- `LICENSE`, `LICENSING.md`, `LICENSES/`: its licences, and those of the
  parts it takes from others (msfa in FM6, DaisySP and Rings in PHYS)

`generated/` holds what Felucca's build generates and the plugin needs:
`felucca_tables.h`, `felucca_fm6.h` (FM6's factory patches) and
`felucca_samples.h` (the sample sets), made with `python3 tools/gen_tables.py`,
`python3 tools/gen_fm6_patches.py` and `python3 tools/gen_samples.py` in a full
checkout of that version (the samples need `assets/`, which is not copied).

## Samples

`felucca_samples.h` includes the CC0 Versilian sets and the drum sounds that
`tools/gen_waves.py` makes, the "Hügelton Sample Pack". Since 1.0 Felucca's
`LICENSING.md` puts the Sample Pack under GPL-3.0-only with the rest.

## The plugin's side

- `felucca_core.c` compiles the sources as one unit, the way
  `tests/hostsim.c` does, with a prefix per compiled copy; it repeats the few
  functions it needs from `ui.c` (not built: it needs the display), marked as
  such.
- `FeluccaEngine` hands each plugin instance a copy of its own.
- Felucca 1.0 has four parts and no separate drum track (DRUM is an engine,
  on part 4 at power-on). Engine 1 (DIGITAL) is retired and not built
  (`FELUCCA_FM4` 0): a DIGITAL sound arrives as FM6 with a converted patch,
  as on the device, and the operator envelopes (`P_FM1_ATK` ..
  `P_FM4_LEVEL`), which only DIGITAL reads, are neither shown nor automated.
- FM6 keeps a patch per part. The plugin calls `fm6_poll` (Felucca's main
  loop does) before each render, and saves each part's patch in the project,
  as Felucca's own projects do.
- One known difference from the device: the FM6 patch bank (PTCH B1..B27)
  lives in the device's flash, which the plugin does not have yet, so those
  PTCH values play the init voice.

## Updating

1. Check out the new release of Felucca, generate the two headers as above,
   and run its `tests/run_tests.sh` there.
2. Replace `upstream/` and `generated/` with the new files, and update the
   version and commit above.
3. Compare `felucca_core.c`'s copies of `ui.c` functions with the new `ui.c`.
4. Build and run ctest: `felucca_regress` must pass on the new goldens, and
   `felucca_test` checks the plugin's side. If the goldens changed, the
   sound changed: see the multi-firmware plan on keeping old versions.
