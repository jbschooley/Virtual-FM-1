# FM1 Open, vendored

`upstream/` is part of [FM1 Open](https://github.com/m-vave/fm1_open), M-VAVE's own firmware and
development project for the FM-1, at release **V12** (tag `V12`, commit
`3be7fbdbd3bb137bacbd65d68412c210eb977870`, 2026-10-14; the repository's main branch, commit
`5d3eac2`, has the same files, only two documentation commits on top). Its device answers
`FM-1_8000012` (`app/version.h` `FM1_UPDATE_PRODUCT`).

FM1 Open is two things: the application ("APP": eight tracks, six sound sources, the sampler, the
screen and panel, projects) and, on the FM-1, a closed binary SDK it links (`sdk/lib/libfm1_engine.a`,
for the FM-1's chip only) with the sound engine. Its browser preview (the workbench's simulator)
runs the same APP on an open C engine instead (`web/`). The plugin builds what that preview builds,
never the binary SDK.

## What is here

Copied unchanged: exactly what `tools/Build-Web.ps1` compiles for the browser preview (every `.c`
outside `sdk/` and `app/board/`) and the headers it includes:

- `app/*.c`, `app/*.h`, `app/synth/*.h`: the APP (not `app/board/`, the FM-1's own hardware)
- `web/engine/`: the preview's platform (`Simulator.c`: display, storage and MIDI in memory) and
  its engine (`sound.c`, `sequencer.c`, `edit.c`, `effects.c`)
- `web/synth/`: the open replacement for the SDK's synths (an MSFA/Dexed-derived FM engine, the
  sampler, PSG, mixer, the SEA sample codec and a synthesized demo sample bank)
- `third_party/synth/`: the 303, 808, Pluck and PSG cores
- `sdk/include/*.h`: the SDK's API headers the APP and the preview include (not `sdk/include/libc`,
  `sdk/lib/` or `sdk/tools/`)
- `LICENSE.md`, `THIRD_PARTY.md`, `README.md`

## Licences

- M-VAVE's APP and preview sources, and the SDK headers here: **GPL-3.0-only WITH M-VAVE's linking
  exception** (`LICENSE.md`; the exception permits linking the binary SDK, which the plugin does
  not do). GPL-3.0-only code in the plugin, as Felucca's, SLOOP's and Melodee's.
- `web/synth/dx7core`, `dx7env`, `dx7lfo`, `dx7note`, `dx7pitchenv`: **Apache-2.0** (MSFA, Dexed).
- `third_party/synth/*` (Open303, DaisySP, stmlib, emu2149 adaptations), `web/synth/sea_decode.c`,
  `sea_encode.h`, `sample_metadata.inc` (SEA): **MIT**.
- The demo projects (`app/presets.c`) and the synthesized demo samples (`web/synth/demo_bank.h`) are
  M-VAVE's, in those GPL source files.

Each file's SPDX line says which. Nothing here is the binary SDK or its separately licensed
resources.

## The plugin's side

- `fm1open_core.c` includes the preview's sources as one compilation unit, in a fixed order, so
  that `engines/felucca/fel_state.py` can give each instance its own copy of the state (as the
  Felucca family's cores; `docs/ADDING-A-FIRMWARE.md`). Upstream builds them as separate units, so:
  - the file-local names two files share are renamed around that file's `#include` (13 of them:
    a new clash fails the build with "redefinition of");
  - `fm1open_names.h` (written by `gen_names.py`, run again after an update) gives every external
    name the copy's prefix, so none meets another core's at link time;
  - `malloc`, `calloc` and `free` come from a first-fit allocator over an arena in the state
    (1.5 MB; the demo sample bank takes most of what is used, about 680 KB at its peak).
- Built with the preview's flags (`-fwrapv -ffp-contract=off -fno-fast-math`, `FM1_HOST_TEST`,
  `FM1_OPEN_WEB`) and include order (CMakeLists.txt `fm1CoreFlags_fm1open`).
- `Fm1OpenEngine` (C++) drives it: 44.1 kHz in blocks of 128 frames, the host's MIDI, the Device
  tab's panel (`DevicePanel`), its 240 x 240 screen, the projects area of its flash (saved with the
  host's project). Its sample bank is the preview's synthesized demo bank (user samples: not yet).
- Not yet: host parameters, syncing with an FM-1 running FM1 Open, user samples.

## Updating

Copy the new release's files as above, run `python3 engines/fm1open/gen_names.py`, build: a new
file-local clash stops the build (add its rename in `fm1open_core.c`), a new allocation the arena
cannot hold shows in `fm1open_test` ("its heap stays in its arena"). Then
`FM1OPEN_UPDATE_FROZEN=1 fm1open_test` if the demos are meant to sound different.
