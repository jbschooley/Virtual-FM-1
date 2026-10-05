# Contributing

How to build Virtual FM-1, what its tests guard, and the rules for working
with a real FM-1. Adding a firmware has its own page:
[`ADDING-A-FIRMWARE.md`](ADDING-A-FIRMWARE.md).

## Building

```
git clone --recurse-submodules https://github.com/jbschooley/Virtual-FM-1
cd Virtual-FM-1
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

Felucca is built into the plugin from its own C source, which needs **Clang**:
the default on macOS; on Linux configure with `CC=clang CXX=clang++`. With
another compiler (MSVC on Windows) the plugin builds without Felucca and says
so when an instance is set to it.

| Option | Default | What |
|---|---|---|
| `FM1_BUILD_FELUCCA` | on with Clang | Felucca's engines (eight copies, so eight instances can play it) |
| `FM1_BUILD_PLUGIN` / `_AU` / `_LV2` / `_AAX` | on / on / on / off | the formats ([`AAX.md`](AAX.md)) |
| `FM1_BUILD_TOOLS` | on | `fm1_probe`, the command-line check against a real FM-1 |
| `FM1_COPY_PLUGIN` | on | copy the plugins into your plug-in folders after building |
| `FM1_SANITIZE` | empty | e.g. `address,undefined` or `thread`: a sanitizer build for testing |

The iPad app and AUv3: [`IOS.md`](IOS.md). Packaging: `scripts/`.

## Tests

`ctest` runs them all. None touches your library or a connected FM-1: the
plugin's tests point the library at a temporary folder (`FM1_DATA_DIR`) and
never connect (`FM1_NO_DEVICE`). A scratch program of your own that creates an
`FM1Processor` would save into your real library: set those two first.

| Test | What it checks |
|---|---|
| `codec_test` | the FM-1 protocols (`firmwares/fm1_common`) against vectors made from baud girl's own code (`tests/golden.json`, by `tests/gen_golden.mjs`) |
| `engine_test` | the FM engine makes sound from real presets, goes silent after release and follows a patch change; the Hardware character stage and the rate converter |
| `seq_test` | the sequencer and arpeggiator |
| `json_test` | the JSON preset and pattern format ([`JSON-FORMAT.md`](JSON-FORMAT.md)) |
| `params_test` | the host parameters: every released id is still there, unchanged (`tests/params-frozen.txt`) |
| `sync_test` | the sync session against a simulated FM-1+VA on virtual MIDI ports; identities and each firmware's releases |
| `plugin_checks` | the whole plugin without a host: rates and latency, projects, the library, Felucca (its state, host parameters, transport, device file, sync between two of its copies), the version messages |
| `state_test` | projects saved by earlier versions (`tests/states/`) still load as they did |
| `library_test` | the library folder: moving the old library file in, instances seeing each other's writes |
| `render_smoke` | renders every preset in many configurations (below) |
| `host_test` | loads the built VST3 (and LV2 on Linux) as a host would, with its editor |
| `felucca_regress` | Felucca's own regression: its golden renders, on the vendored source |
| `felucca_test` | Felucca as the plugin drives it: the copies, sound, the editor protocol, its objects, its panel; and `tests/felucca-frozen.txt` |

### What guards what

- **Released host parameters** never change: `params_test` fails when one
  does. New ones are appended with `FM1_APPEND_FROZEN=1 build/params_test_artefacts/Release/params_test`.
- **Felucca's parameters and presets**: `tests/felucca-frozen.txt`. A Felucca
  update that changes or removes one fails `felucca_test`; that is when the
  old release is kept beside the new one ([`ADDING-A-FIRMWARE.md`](ADDING-A-FIRMWARE.md),
  Versions). New ones: `FELUCCA_APPEND_FROZEN=1 build/felucca_test`.
- **The sound**: `plugin_test render <golden.json> <out.txt>` writes a hash of
  the exact output of every case. Run it before and after a change on the
  same machine and compare: a change that should not alter the sound must
  leave every line the same. (ctest only runs it; the hashes differ between
  machines.) Felucca's own sound is guarded by `felucca_regress`.
- **Old projects**: `state_test`. When the saved state changes, add a new
  corpus folder with `plugin_test state-write`, keeping the old ones.
- **Felucca's backup file**: `node tests/felucca_backup_check.mjs <Felucca's web/fm1backup.js> <file.json>`
  reads a file the plugin wrote with Felucca's own web editor code.
- **Plugin formats**: pluginval at its strictest level runs in CI on the
  installed VST3 and AU (macOS).
- **Linux**: `scripts/test-linux-docker.sh` builds and tests as CI does, in
  Docker. CI (`.github/workflows/build.yml`) builds, tests and packages on
  macOS, Windows and Linux for a version tag, or when started by hand.
- **Memory and threads**: a build with `-DFM1_SANITIZE=address,undefined`
  (or `thread`) and `ctest`.

## Working with a real FM-1

`fm1_probe` (`tests/fm1_probe.cpp`) talks to a connected FM-1 from the command
line: `identify`, `pull`, `dump <file.syx>` (all 128 presets), `patterns 0 15
<file.syx>`, `roundtrip`, and for Felucca `felucca-backup` and `felucca-live`.

- **Back up before any write**: `dump` and `patterns ... <file>` keep the
  presets and patterns as the messages that write them back.
- **Never send an update or boot loader message** (M-VAVE's `F0 22 24 35 ...`,
  Felucca's update frames). The plugin's code never does; keep it that way.
- **Installing firmware** is done only with each author's own installer
  (Felucca's `tools/fm1_install.py` or web installer, baud girl's web
  installer, M-VAVE's updater), from a starting point the author tested. If an
  install fails half way and the FM-1 no longer starts, recovering it needs
  [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter) and a
  Seeed XIAO RP2040 wired to its USB data lines.

## Code from elsewhere

- `engines/dx7/msfa/`: Google's msfa (Apache-2.0), changes listed in its
  `NOTICE.md`. Its files have CRLF line endings: keep them.
- `engines/felucca/upstream/`: Felucca (GPL-3.0-only), unchanged; updating it
  is described in `engines/felucca/UPSTREAM.md`.
- `plugin/dexed_ui/`: Dexed's look and panels (GPL-3.0).
- `third_party/JUCE`: a submodule. Licences: [`THIRD_PARTY.md`](../THIRD_PARTY.md).

## Changes

- One change per commit; a commit message says what changed and why.
- [`CHANGELOG.md`](../CHANGELOG.md), under the next version: what a player
  notices, not the code.
