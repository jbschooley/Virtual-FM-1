/* fm1open_core.c -- M-VAVE's FM1 Open V12 (engines/fm1open/upstream: GPL-3.0-only WITH M-VAVE's
 * linking exception, with Apache-2.0 and MIT parts; see UPSTREAM.md), as a library the plugin can
 * drive: the firmware's app (eight tracks, six sound sources, the sampler, its screen and panel,
 * projects) on the open C engine its browser preview runs (web/engine, web/synth,
 * third_party/synth), never on the closed binary SDK.
 *
 * Built as its browser preview builds it (sdk/../tools/Build-Web.ps1: every .c but sdk/ and
 * app/board/, FM1_HOST_TEST and FM1_OPEN_WEB, -fwrapv -ffp-contract=off), but as one compilation
 * unit, so that fel_state.py can move its state into a block each instance owns, as the Felucca
 * family's cores do (felucca_core.c; docs/ADDING-A-FIRMWARE.md). Upstream's files are separate
 * units, so the file-local names two of them share are renamed per file around their includes
 * below; fm1open_names.h (gen_names.py) gives every external name this copy's prefix.
 *
 * Upstream allocates a little at boot (the FM voices, the synthesized demo sample bank, an import
 * buffer): here from an arena in the state, so it is per instance and goes with it. */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifndef FEL_PREFIX
#error "FEL_PREFIX names this copy (fm1open_core.h)"
#endif
#include "fm1open_core.h"

#ifndef FO_NAMES_PROBE
#include "fm1open_names.h"
#endif

#if !defined(__clang__) || !defined(FEL_BSS_SECTION)
#error "the firmware cores are built with Clang through fel_state.py (CMakeLists.txt fm1_core)"
#endif
#define FEL_PRAGMA(x) _Pragma(#x)
#define FEL_SECTIONS(b, d) FEL_PRAGMA(clang section bss = b data = d)
FEL_SECTIONS(FEL_BSS_SECTION, FEL_DATA_SECTION)

/* ---- the heap: first fit over an arena in the state ---------------------------------------- */
/* Upstream's allocations: 8 FM synths (about 40 KB each), the demo sample bank (466944 bytes) with
 * 192 KB of scratch while it is made, an encoded pad at a time, a transfer buffer (4 KB) */
#define FO_HEAP (1536u * 1024u)
typedef struct { uint32_t size, used; } fo_block;   /* size: the bytes after this header */
static union { uint64_t align[FO_HEAP / 8]; uint8_t bytes[FO_HEAP]; } fo_heap;
static uint32_t fo_heap_ready, fo_heap_high;
static void fo_heap_init(void)
{
    fo_block *b = (fo_block *)fo_heap.bytes;
    b->size = FO_HEAP - sizeof *b; b->used = 0;
    fo_heap_ready = 1;
}
static void *fo_malloc(size_t n)
{
    if (!fo_heap_ready) fo_heap_init();
    n = (n + 15u) & ~(size_t)15u;
    for (uint32_t at = 0; at < FO_HEAP;) {
        fo_block *b = (fo_block *)(fo_heap.bytes + at);
        if (!b->used && b->size >= n) {
            if (b->size >= n + sizeof(fo_block) + 16u) {   /* split */
                fo_block *rest = (fo_block *)(fo_heap.bytes + at + sizeof *b + n);
                rest->size = b->size - (uint32_t)n - sizeof *rest; rest->used = 0;
                b->size = (uint32_t)n;
            }
            b->used = 1;
            const uint32_t end = at + (uint32_t)sizeof *b + b->size;
            if (end > fo_heap_high) fo_heap_high = end;
            return b + 1;
        }
        at += (uint32_t)sizeof *b + b->size;
    }
    return NULL;   /* (upstream handles it: an allocation it cannot make) */
}
static void fo_free(void *p)
{
    if (!p) return;
    fo_block *b = (fo_block *)p - 1;
    b->used = 0;
    for (uint32_t at = 0; at < FO_HEAP;) {   /* coalesce free neighbours */
        fo_block *c = (fo_block *)(fo_heap.bytes + at);
        const uint32_t next = at + (uint32_t)sizeof *c + c->size;
        if (!c->used && next < FO_HEAP && !((fo_block *)(fo_heap.bytes + next))->used) {
            c->size += (uint32_t)sizeof(fo_block) + ((fo_block *)(fo_heap.bytes + next))->size;
            continue;
        }
        at = next;
    }
}
static void *fo_calloc(size_t n, size_t size)
{
    if (size && n > SIZE_MAX / size) return NULL;
    void *p = fo_malloc(n * size);
    if (p) memset(p, 0, n * size);
    return p;
}
#ifndef FO_NAMES_PROBE
#define malloc fo_malloc
#define calloc fo_calloc
#define free fo_free
#endif

/* ---- FM1 Open, unchanged -------------------------------------------------------------------- */
#define __attribute__(x)
#include "upstream/app/audio_output.c"
#include "upstream/app/presets.c"
#include "upstream/app/project.c"
#define checksum fofile_samples_checksum
#include "upstream/app/samples.c"
#undef checksum
#define ARP fofile_screen_ARP
#define header fofile_screen_header
#include "upstream/app/screen.c"
#undef ARP
#undef header
#include "upstream/app/transfer.c"
#define cursor fofile_edit_cursor
#include "upstream/web/engine/edit.c"
#undef cursor
#include "upstream/web/engine/effects.c"
#include "upstream/web/engine/sequencer.c"
#define leds fofile_sim_leds
#define state fofile_sim_state
#include "upstream/web/engine/Simulator.c"
#undef leds
#undef state
#define midi fofile_sound_midi
#include "upstream/web/engine/sound.c"
#undef midi
#include "upstream/web/synth/adsr.c"
#define algorithms fofile_dx7core_algorithms
#include "upstream/web/synth/dx7core.c"
#undef algorithms
#include "upstream/web/synth/dx7env.c"
#include "upstream/web/synth/dx7lfo.c"
#include "upstream/web/synth/dx7math.c"
#include "upstream/web/synth/dx7note.c"
#define advance fofile_dx7pitchenv_advance
#include "upstream/web/synth/dx7pitchenv.c"
#undef advance
#include "upstream/web/synth/dx7synth.c"
#include "upstream/web/synth/fixed.c"
#include "upstream/web/synth/mixer.c"
#include "upstream/web/synth/psg.c"
#include "upstream/web/synth/sampledrum.c"
#define valid fofile_sea_decode_valid
#include "upstream/web/synth/sea_decode.c"
#undef valid
#include "upstream/web/synth/voices.c"
#define parameters fofile_bass303_parameters
#include "upstream/third_party/synth/bass303.c"
#undef parameters
#define render fofile_drum808_render
#include "upstream/third_party/synth/drum808.c"
#undef render
#include "upstream/third_party/synth/emu2149.c"
#define parameters fofile_pluck_parameters
#include "upstream/third_party/synth/pluck.c"
#undef parameters
#undef __attribute__

#ifndef FO_NAMES_PROBE
#undef malloc
#undef calloc
#undef free
#pragma clang section bss = "" data = ""

/* ---- the plugin's side ----------------------------------------------------------------------- */
void FEL(fo_boot)(int clear) { sim_boot(clear ? 1u : 0u); }
const float *FEL(fo_render)(void) { return sim_render(); }
int FEL(fo_midi)(const uint8_t *bytes, unsigned n)
{
    if (!bytes || !n) return -1;
    if (n <= 3) return fm1_midi_post(bytes, n);
    if (n > sizeof file_buffer) return -1;
    memcpy(file_buffer, bytes, n);
    return sim_midi(n);
}
void FEL(fo_control)(unsigned kind, unsigned index, int value) { sim_control(kind, index, value); }
void FEL(fo_release)(void) { sim_release(); }
const uint16_t *FEL(fo_pixels)(void) { return sim_pixels(); }
uint32_t FEL(fo_frame_revision)(void) { return sim_frame_revision(); }
const uint8_t *FEL(fo_state)(void) { return sim_state(); }
uint8_t *FEL(fo_flash)(void) { return sim_flash(); }
uint32_t FEL(fo_flash_size)(void) { return (uint32_t)sizeof flash; }
uint32_t FEL(fo_flash_revision)(void) { return sim_flash_revision(); }
int FEL(fo_export)(uint8_t *out)
{
    const int r = sim_export();
    if (!r && out) memcpy(out, file_buffer, FO_PROJECT_BYTES);
    return r;
}
int FEL(fo_import)(const uint8_t *in, unsigned n)
{
    if (!in || n > sizeof file_buffer) return -1;
    memcpy(file_buffer, in, n);
    const int r = sim_import(n);
    fm1_web_control();
    return r;
}
int FEL(fo_factory_project)(unsigned index, uint8_t *out)
{
    const int r = sim_factory_project(index);
    if (!r && out) memcpy(out, file_buffer, FO_PROJECT_BYTES);
    return r;
}
const char *FEL(fo_factory_name)(unsigned index) { return sim_factory_name(index); }
const char *FEL(fo_project_name)(void) { return sim_project_name(); }
int FEL(fo_preset)(unsigned slot) { return sim_preset(slot); }
uint32_t FEL(fo_heap_used)(void) { return fo_heap_high; }
#endif
