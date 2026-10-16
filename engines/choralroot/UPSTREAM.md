# ChoralRoot 0.23 (upstream)

Quixotic7's ChoralRoot for the FM-1 (<https://github.com/Quixotic7/ChoralRootFM1>), a chord
instrument built from Felucca, tag `v0.23` (commit 5755de8). It answers M-VAVE's identity
request as FM-1_920 in every release; its editor protocol's INFO names it ("ChoralRoot 0.23",
its single-core package "ChoralRoot 0.23 1-core"). GPL-3.0-only (`upstream/LICENSE`,
`upstream/LICENSING.md`, the other licences in `upstream/LICENSES/`).

## What was copied

Copied unchanged from a `git archive` of the tag (every file compared byte for byte):

- `firmware/src/`, `firmware/hal/`, and `firmware/stub/stub_core.h` (its backup test's)
- `tools/emu/emu_fw.c`, `emu_firmware.h`, `emu_hal_fw.h`, `emu_hooks.h`: its own emulator's
  firmware side, which the plugin's core builds (below)
- `tests/hostsim.c` (the emulator's sound side), `tests/regress.c` and `tests/golden.txt` (its
  313 golden renders, run by ctest as `choralroot_regress`), `tests/cr_backup_test.c` and
  `tests/cr_pool_shim.h` (its backup protocol's test, run as `choralroot_backup_test`)
- `tools/build.py` and the generators it runs (`gen_tables.py`, `gen_fm6_patches.py`,
  `gen_cz1_factory.py`, `gen_prophet_factory.py`, `gen_samples.py`, `gen_waves.py`,
  `gen_aa_font.py`, `gen_aa_icons.py`, `gen_aa_keycaps.py`, `gen_ui_palettes.py`,
  `gen_layout.py` with `tools/layout/`, `aa_raster.py`, `sampleio.py`, `lz4blk.py`)
- `assets/cz1-factory/` and `assets/prophet5-factory/` (the sound data the generated headers
  carry, with their READMEs; `shipped.txt` names the 115 Prophet programs built in) and
  `assets/fonts/OFL.txt`
- `LICENSE`, `LICENSING.md`, `LICENSES/`

`generated/` holds what its `tools/build.py` generates (`generate()`), made in a full checkout
of the tag: `felucca_tables.h`, `felucca_fm6.h`, `felucca_samples.h`, `melodee_cz1.h`,
`melodee_prophet_factory.h`, `cr_fonts.h`, `cr_layouts.h`, `ui_fonts.h`, `ui_icons.h`,
`ui_keycaps.h`, `ui_palettes.h`. They were generated twice, in the clone and in a clean export:
the same bytes both times. The samples need `assets/samples-cc0/` (Versilian Studios, CC0), the
fonts and icons `assets/fonts/` and `assets/icons.json`; those are not copied.

## The plugin's side

- `choralroot_core.c` builds ChoralRoot the way it builds itself for a computer: it includes
  its emulator's `tools/emu/emu_fw.c` unchanged, as the single-core package (`CR_SINGLE_CORE`
  1), and drives it through the boundary that emulator gives its window (`emu_hooks.h`):
  `emu_hal` (keys, buttons, knobs, LEDs, the screen) and the `emu_fw_*` hooks (power-on, the
  1 ms timer, a UI frame, the audio interrupt, MIDI in and out). Time is simulated: it moves
  with the audio rendered, a tick a millisecond and a UI frame every 15 ms of it. The flash is
  the emulator's RAM image. The audio interrupt's 128-frame halves are given out in the
  plugin's 32-frame blocks.
- Its backup protocol (`cr_backup.c`: INFO, LIST, GET, PUT, 72 RESTART) answers SysEx through
  the core's USB port; the emulator has no such transport, so the core assembles the frames and
  hands them to `crb_handle`, its replies (`CRB_SEND`) going out as USB-MIDI packets. The stored
  objects (`object_get`, `object_put`) go through the same protocol in-process.
- A restart: 72 RESTART, and a restored settings record (it waits for one: `cr_restore_lock`),
  make one due; `FeluccaEngine` powers the instance on again from its flash image
  (`chr_restart_take`, `chr_boot_from`: the emulator's flash file read from memory).
- Never acted on: `exit`, `atexit` and its log lines (`printf`) are no-ops in the core.
- `compat/` stands in for what the emulator takes from macOS and POSIX (`os/lock.h`, a lock
  that is never contended: the core is single-core and each instance is called under its lock).
- It is compiled once, and `fel_state.py` moves its writable state into a block each instance
  owns, as for Felucca. `gen_names.py` writes `choralroot_names.h`, which gives every external
  name the core defines this copy's prefix (`chr_`); run it after an update.
- No host parameters, Sound page, presets or sequencer: its protocol has no parameter or editor
  commands, and its sounds and loops are its own pages'. The Device view and the Sync page
  (Pull, Send, the device file) are what the plugin shows.

## Updating

Copy the same set from the new tag, run its `tools/build.py` `generate()` twice (the same bytes
both times) and copy the headers listed above into `generated/`, run `gen_names.py`, then
`choralroot_test` (with `CHORALROOT_UPDATE_FROZEN=1` if the sound changed on purpose),
`choralroot_regress` and `choralroot_backup_test`. Check `cr_backup.c` for new objects or
commands: the dialect is `felucca::choralrootDialect()` (`firmwares/felucca/FeluccaSync.cpp`).
