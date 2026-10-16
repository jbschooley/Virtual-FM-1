#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# ChoralRoot FM-1 layout tool: the preview module (docs/LAYOUT-TOOL.md). The whole firmware with the layout API
# (tools/layout/layout_web.c, -DCR_LAYOUT_PREVIEW) as a standalone WebAssembly module, and its loader:
#   [CR_VERSION=1.0] sh tools/layout/build_layout.sh
#   -> build/layout-web/crlayout.wasm   the module
#      build/layout-web/crlayout.js     the loader with the same bytes in it as base64 (the page loads this one file,
#                                       so it works from file://)
# The page itself is web/layout.html (+ web/fm1layout.js): web/make_site.py puts both and build/layout-web/* into the
# site's webapp/layout/; python3 tools/layout/make_page.py makes the same folder alone (build/layout-web/index.html).
# Needs emcc (the Emscripten SDK: ~/emsdk/emsdk_env.sh is sourced when emcc is not on the PATH) and the firmware's
# generated headers (build/gen: the generate step of tools/build.py runs when they are missing), as
# tools/emu/web/build_web.sh, whose compiler flags these are. CR_VERSION: the version the firmware shows; default
# firmware/src/choralroot.c's FELUCCA_VERSION.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"
# P5's factory header follows the shipped list (written only when its text changes)
[ -d build/gen ] && python3 tools/gen_prophet_factory.py build/gen/melodee_prophet_factory.h >/dev/null
# the compiled layouts follow their designs (tools/gen_layout.py; written only when its text changes)
[ -d build/gen ] && python3 tools/gen_layout.py build/gen/cr_layouts.h >/dev/null
if ! command -v emcc >/dev/null 2>&1; then
    for e in "${EMSDK:-}/emsdk_env.sh" "$HOME/emsdk/emsdk_env.sh"; do
        [ -f "$e" ] && { . "$e" >/dev/null 2>&1; break; }
    done
fi
command -v emcc >/dev/null 2>&1 || { echo "emcc not found: install the Emscripten SDK (~/emsdk)"; exit 1; }
export DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib${DYLD_FALLBACK_LIBRARY_PATH:+:$DYLD_FALLBACK_LIBRARY_PATH}
for h in ui_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h felucca_tables.h felucca_fm6.h felucca_samples.h cr_fonts.h cr_layouts.h; do
    if [ ! -f "build/gen/$h" ]; then
        echo "build/gen/$h missing: running the generate step (tools/build.py)"
        python3 tools/build.py
        break
    fi
done
if [ -n "${CR_VERSION:-}" ]; then
    case "$CR_VERSION" in
        ChoralRoot*) VER=$CR_VERSION ;;
        *) VER="ChoralRoot $(printf %s "$CR_VERSION" | tr A-Z a-z)" ;;
    esac
else
    VER=$(sed -n 's/^#define FELUCCA_VERSION "\([^"]*\)".*/\1/p' firmware/src/choralroot.c | head -1)
fi
OUT=build/layout-web
mkdir -p "$OUT"
# through tests/hostcc.sh: compiled again only when the command (the version is in it) or a file it was compiled from
# has changed (hashes, not dates; HOSTCC_FORCE=1: always)
sh tests/hostcc.sh emcc -O2 -w -std=gnu11 -Itools/emu/web/compat -Ibuild/gen -Ifirmware/src -DEMU_WEB -DCR_LAYOUT_PREVIEW \
    "-DFELUCCA_VERSION=\"$VER\"" \
    --no-entry -sSTANDALONE_WASM -sFILESYSTEM=0 -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=67108864 \
    -o "$OUT/crlayout.wasm" tools/layout/layout_web.c
python3 tools/layout/make_page.py --loader "$VER"
echo "layout-web: $OUT ($VER)"
for f in crlayout.wasm crlayout.js; do
    printf '  %-16s %8d B\n' "$f" "$(wc -c < "$OUT/$f")"
done
