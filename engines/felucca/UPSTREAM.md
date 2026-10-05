# Felucca, vendored

`upstream/` is part of [Felucca](https://github.com/hugelton/Felucca), Leo
Kuroshita's firmware for the M-VAVE FM-1 (Hügelton Instruments), at
**v0.9-beta** (commit `e5a908d0383848cd85149de6dc35150c792231fc`, 2026-10-03).
It is GPL-3.0-only; see `upstream/LICENSE` and `upstream/LICENSING.md`. The
author has agreed to its use in Virtual FM-1 under the GPL
([issue #1](https://github.com/hugelton/Felucca/issues/1)).

Copied unchanged:

- `firmware/src/` and `firmware/hal/`: the DSP, sequencer and the rest of the
  firmware (the plugin builds only what Felucca's own host build builds)
- `tests/regress.c`, `tests/hostsim.c`, `tests/golden.txt`: Felucca's
  regression (83 golden renders), run by ctest as `felucca_regress`
- `tools/gen_tables.py`, `gen_samples.py`, `gen_waves.py`: the generators

`generated/` holds what Felucca's build generates and the plugin needs:
`felucca_tables.h` and `felucca_samples.h` (the sample sets), made with
`python3 tools/gen_tables.py` and `python3 tools/gen_samples.py` in a full
checkout of that version (the samples need `assets/`, which is not copied).

## Samples

`felucca_samples.h` includes the CC0 Versilian sets and the drum sounds that
`tools/gen_waves.py` makes, which `LICENSING.md` calls the "Hügelton Sample
Pack", all rights reserved. They are built in for now; whether a released
plugin may carry them is still to be asked of the author.

## The plugin's side

- `felucca_core.c` compiles the sources as one unit, the way
  `tests/hostsim.c` does, with a prefix per compiled copy; it repeats the few
  functions it needs from `ui.c` (not built: it needs the display), marked as
  such.
- `FeluccaEngine` hands each plugin instance a copy of its own.
- Differences from the device, known and small:
  - applying a preset does not load its 16-step pattern into an empty
    sequencer (`load_pat16`), since the plugin does not show Felucca's
    sequencer yet; add it when it does
  - engine parameter names are the static ones, as Felucca's editor
    protocol reports them; on the device LOFI shows other names in some
    modes (`eng_lofi.c`'s `desc` hook), same ranges

## Updating

1. Check out the new release of Felucca, generate the two headers as above,
   and run its `tests/run_tests.sh` there.
2. Replace `upstream/` and `generated/` with the new files, and update the
   version and commit above.
3. Compare `felucca_core.c`'s copies of `ui.c` functions with the new `ui.c`.
4. Build and run ctest: `felucca_regress` must pass on the new goldens, and
   `felucca_test` checks the plugin's side. If the goldens changed, the
   sound changed: see the multi-firmware plan on keeping old versions.
