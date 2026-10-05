/* felucca_core.c -- Felucca 1.0's DSP and sequencer (engines/felucca/upstream, GPL-3.0-only,
 * Leo Kuroshita / Hügelton Instruments) as a library the plugin can drive.
 *
 * Felucca is built as one compilation unit whose state is all file-level statics, so
 * one compiled copy is one synth. This file is compiled several times (CMakeLists.txt,
 * FELUCCA_COPIES), each copy with its own FEL_PREFIX, so each plugin instance gets a
 * copy of its own: no state is shared or swapped. felucca_core.h declares the API.
 *
 * The firmware's own sources are included unchanged, the way its host simulator
 * (upstream/tests/hostsim.c) builds them; what lives in its UI code (ui.c, which needs
 * the display) and is needed here is repeated below and marked as such.
 *
 * A copy is given to one instance after another. To start each from the state the
 * program started with (no voices, tails or sequencer positions left from the last
 * one), every writable variable of the copy (some of them static inside
 * functions) is placed in sections of its own (FEL_BSS_SECTION, FEL_DATA_SECTION),
 * whose image is kept from the start and put back by FEL(restore). That needs Clang's
 * section pragma; CMakeLists.txt builds Felucca only with Clang. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>   /* Felucca's code past libc.c uses the C library's memcpy and memset */

#ifndef FEL_PREFIX
#error "FEL_PREFIX names this copy (felucca_core.h)"
#endif
#include "felucca_core_api.h"

/* the firmware's globals that are not static: one per copy */
#define bootguard FEL(bootguard)

#if !defined(__clang__) || !defined(FEL_BSS_SECTION)
#error "Felucca's copies need Clang and their sections (CMakeLists.txt)"
#endif
#define FEL_PRAGMA(x) _Pragma(#x)
#define FEL_SECTIONS(b, d) FEL_PRAGMA(clang section bss = b data = d)
FEL_SECTIONS(FEL_BSS_SECTION, FEL_DATA_SECTION)

#define __attribute__(x)
#define memset FEL(memset)
#define memcpy FEL(memcpy)
#define memcmp FEL(memcmp)
#include "felucca_tables.h"
#include "../upstream/firmware/src/libc.c"
#undef memset
#undef memcpy
#undef memcmp
static struct { volatile uint32_t notes, buttons; } fm1_in;
#include "../upstream/firmware/src/core.h"
#include "../upstream/firmware/src/engines.c"
#include "../upstream/firmware/src/params.c"
#include "../upstream/firmware/src/mod.c"
#include "../upstream/firmware/src/voice.c"
#include "../upstream/firmware/src/slicer.c"
#include "../upstream/firmware/src/fx.c"
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#include "../upstream/firmware/src/usb.c"
#include "../upstream/firmware/src/midi_uart.c"
#include "../upstream/firmware/src/song_chain.c"
#include "../upstream/firmware/src/seq.c"

/* ---- from Felucca 1.0's ui.c and main.c (not built here: they need the display),
 * in the simpler form its host simulator (tests/hostsim.c) uses: no undo, no motion
 * recording, no browsing aliases ------------------------------------------------------------ */
static void step_clear(step_t *st)
{
    st->n = 0;
    st->time = ST_REST;
    st->flags = 0;
    st->vel = 0;
    st->hit = st->acc = 0;
    st->probability = 0;
}

static void track_defaults_steps(track_t *t)
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        step_clear(&t->step[i]);
}

static void track_defaults(track_t *t)
{
    uint32_t i;
    for (i = 0; i < P_E0; i++)
        t->p[i] = TP[i].def;
    track_defaults_steps(t);
}

/* what a sound load leaves alone: the mix, the arpeggiator, the scale and key map, the pattern
 * parameters, the SLICER insert and the chord keys */
static int param_kept(uint32_t i)
{
    return i == P_LEVEL || i == P_PAN || i == P_MUTE || (i >= P_AMODE && i <= P_SGATE) ||
           (i >= P_SLCR && i <= P_SLDEPTH) || i == P_CHRD || i == P_VOIC;
}

/* preset_orig: a retired preset kept as an alias, so stored preset numbers stay valid: SAMPLE 1,
 * once TRANH, is PIANO (tools/gen_samples.py SMP_SET_ORIG). It loads as the original */
static uint32_t preset_orig(const engine_t *e, uint32_t k)
{
    return e->presets == SMP_PRESET_TABLE && k < SMP_NSETS ? SMP_SET_ORIG[k] : k;
}

/* apply_preset_to: preset pi of the engine the track asked for, the sound only (DIGITAL, retired:
 * its preset converted to FM6, as fm4_load_preset) */
static void apply_preset_to(track_t *t, uint32_t pi)
{
    const engine_t *e = ENGINES[t->eng_req % NENGINES];
    uint32_t i;
    panic_req |= (uint8_t)(1u << trk_index(t));
    t->user = 0;
#if !FELUCCA_FM4
    if (t->eng_req % NENGINES == ENGI_DIGITAL) {
        uint8_t v[FP_SIZE + 1u];
        const uint32_t tr = trk_index(t);
        for (i = 0; i < P_E0; i++)
            if (!param_kept(i))
                t->p[i] = TP[i].def;
        fm4_preset_values(t->p, pi);
        t->preset = (uint8_t)fm4_convert(t->p, v);
        t->eng_req = ENGI_FM6;
        fm6_set_patch(tr, v);
        fm6_slot[tr] = (uint8_t)t->p[P_E7];
        return;
    }
#endif
    if (!e->npresets)
        return;
    pi = preset_orig(e, pi % e->npresets);
    t->preset = (uint8_t)pi;
    for (i = 0; i < P_E0; i++)
        if (!param_kept(i))
            t->p[i] = TP[i].def;
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = (int16_t)e->presets[pi].e[i];
    t->p[P_ATK] = e->presets[pi].env[0];
    t->p[P_DEC] = e->presets[pi].env[1];
    t->p[P_SUS] = e->presets[pi].env[2];
    t->p[P_REL] = e->presets[pi].env[3];
    t->p[P_ED_FLT] = e->presets[pi].fenv;
    t->p[P_VOICE] = e->presets[pi].mono ? V_LEGATO : V_POLY;
    {
        static const uint8_t FX_DEF[4] = {0, 24, 28, 36};
        const preset_t *pr = &e->presets[pi];
        for (i = 0; i < 4u; i++)
            t->p[P_DIST + i] = (int16_t)(pr->fx[i] ? pr->fx[i] - 1 : FX_DEF[i]);
    }
    fm6_track_loaded(t);   /* FM6: the preset's patch */
}

/* set_engine_of: the engine's defaults and its first preset (the audio side switches after a fade) */
static void set_engine_of(track_t *t, uint32_t ei)
{
    const engine_t *e = ENGINES[ei % NENGINES];
    uint32_t i;
    t->eng_req = (uint8_t)(ei % NENGINES);
    if (ei % NENGINES != ENGI_DIGITAL)
        for (i = 0; i < 8u; i++)
            t->p[P_E0 + i] = e->edit[i].def;
    apply_preset_to(t, 0);
}

#undef __attribute__                     /* Felucca's code is done: attributes mean something again */
static uint8_t fel_bss_marker;          /* make sure both sections exist */
static uint8_t fel_data_marker = 1;
#pragma clang section bss = "" data = ""

/* ---- the copy's state, as the program started ----------------------------------------------- */
extern uint8_t fel_bss_start __asm(FEL_BSS_START);
extern uint8_t fel_bss_stop __asm(FEL_BSS_STOP);
extern uint8_t fel_data_start __asm(FEL_DATA_START);
extern uint8_t fel_data_stop __asm(FEL_DATA_STOP);
static uint8_t *fel_pristine;            /* outside the sections: kept across restores */

void *malloc(size_t);

/* never inlined, and a compiler barrier after: the section bounds are 1-byte symbols to
 * the optimizer, which must not move reads of the state across the copy */
__attribute__((noinline)) void FEL(restore)(void)
{
    const size_t nb = (size_t)(&fel_bss_stop - &fel_bss_start);
    const size_t nd = (size_t)(&fel_data_stop - &fel_data_start);
    (void)fel_bss_marker; (void)fel_data_marker;
    if (!fel_pristine) {                 /* the first time: nothing has run yet, keep the image */
        fel_pristine = (uint8_t *)malloc(nd ? nd : 1);
        if (fel_pristine)
            FEL(memcpy)(fel_pristine, &fel_data_start, nd);
        FEL(memset)(&fel_bss_start, 0, nb);
        return;
    }
    FEL(memset)(&fel_bss_start, 0, nb);
    FEL(memcpy)(&fel_data_start, fel_pristine, nd);
    __asm__ volatile("" ::: "memory");
}

uint32_t FEL(state_bytes)(void) { return (uint32_t)((&fel_bss_stop - &fel_bss_start) + (&fel_data_stop - &fel_data_start)); }

/* ---- the API ------------------------------------------------------------------------------ */

static const param_desc_t *track_param_desc(uint32_t track, uint32_t id)
{
    if (track >= NTRK || id >= P_COUNT)
        return 0;
    if (id >= P_E0) {   /* as params.c's track_desc: an engine's mode-dependent label and names first */
        const engine_t *e = ENGINES[trk[track].eng_req % NENGINES];
        const param_desc_t *d = e->desc ? e->desc(&trk[track], id - P_E0) : 0;
        return d ? d : &e->edit[id - P_E0];
    }
    return &TP[id];
}

static void fill(fel_desc_t *out, const param_desc_t *d)
{
    uint32_t n = 0;
    out->label = d->label;
    out->unit = d->unit;
    out->fmt = d->fmt;
    out->min = d->min;
    out->max = d->max;
    out->def = d->def;
    out->names = d->names;
    if (d->fmt == F_ENUM && d->names)
        n = (uint32_t)(d->max - d->min + 1);
    out->nnames = n;
}

void FEL(init)(void)   /* felucca_init: four parts with their default sounds (TRK_DEF), empty patterns */
{
    uint32_t i;
    chain_defaults(&chain_config);
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    fm6_init();
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        track_defaults(t);
        set_engine_of(t, TRK_DEF[i][0]);
        apply_preset_to(t, TRK_DEF[i][1]);
        t->engine = t->eng_req;
        track_defaults_steps(t);
    }
    song.sel = 0;
    song.master_q12 = 2048;
}

uint32_t FEL(ctl)(void) { return CTL; }
uint32_t FEL(rate)(void) { return FS; }
uint32_t FEL(ntracks)(void) { return NTRK; }
uint32_t FEL(nparts)(void) { return NPART; }
uint32_t FEL(nengines)(void) { return NENGINES; }
uint32_t FEL(pcount)(void) { return P_COUNT; }
uint32_t FEL(pe0)(void) { return P_E0; }
uint32_t FEL(gcount)(void) { return G_COUNT; }
uint32_t FEL(nsteps)(void) { return NSTEP; }

const char *FEL(engine_name)(uint32_t e) { return e < NENGINES ? ENGINES[e]->name : ""; }
const char *FEL(engine_page)(uint32_t e, uint32_t page) { return e < NENGINES && page < 2u ? ENGINES[e]->page_title[page] : ""; }
uint32_t FEL(npresets)(uint32_t e) { return e < NENGINES ? ENGINES[e]->npresets : 0u; }
const char *FEL(preset_name)(uint32_t e, uint32_t i)
{
    /* "" for an alias (browsing skips it on the device) */
    return e < NENGINES && i < ENGINES[e]->npresets && preset_orig(ENGINES[e], i) == i ? ENGINES[e]->presets[i].name : "";
}

int FEL(param_desc)(uint32_t track, uint32_t id, fel_desc_t *out)
{
    const param_desc_t *d = track_param_desc(track, id);
    if (!d)
        return 0;
    fill(out, d);
    return 1;
}

int FEL(global_desc)(uint32_t id, fel_desc_t *out)
{
    if (id >= G_COUNT)
        return 0;
    fill(out, &GP[id]);
    return 1;
}

int32_t FEL(param_get)(uint32_t track, uint32_t id) { return track < NTRK && id < P_COUNT ? trk[track].p[id] : 0; }
void FEL(param_set)(uint32_t track, uint32_t id, int32_t v)
{
    const param_desc_t *d = track_param_desc(track, id);
    if (d && d->max > d->min)
        trk[track].p[id] = (int16_t)clamp(v, d->min, d->max);
}
int32_t FEL(global_get)(uint32_t id) { return id < G_COUNT ? song.g[id] : 0; }
void FEL(global_set)(uint32_t id, int32_t v)
{
    if (id < G_COUNT && GP[id].max > GP[id].min)
        song.g[id] = (int16_t)clamp(v, GP[id].min, GP[id].max);
}

uint32_t FEL(engine_of)(uint32_t track) { return track < NTRK ? trk[track].eng_req : 0u; }
uint32_t FEL(preset_of)(uint32_t track) { return track < NTRK ? trk[track].preset : 0u; }
void FEL(set_engine)(uint32_t track, uint32_t e) { if (track < NPART) set_engine_of(&trk[track], e); }
void FEL(apply_preset)(uint32_t track, uint32_t pi) { if (track < NPART) apply_preset_to(&trk[track], pi); }

/* the engines one can pick, in the order Felucca shows them (never DIGITAL's reserved 1) */
uint32_t FEL(engines_shown)(void) { return NENG_SHOWN; }
uint32_t FEL(engine_shown)(uint32_t n) { return eng_vis(n); }

/* FM6's patch of a track: the 155-byte DX7 single-voice layout (VCED) */
uint32_t FEL(fm6_engine)(void) { return ENGI_FM6; }
void FEL(fm6_patch_get)(uint32_t track, uint8_t *v155)
{
    if (track < NTRK)
        FEL(memcpy)(v155, fm6_patch[track], FP_SIZE);
}
void FEL(fm6_patch_set)(uint32_t track, const uint8_t *v155)   /* after PTCH (P_E7) is set: as editor_fm6.c */
{
    if (track < NTRK) {
        fm6_set_patch(track, v155);
        fm6_slot[track] = (uint8_t)trk[track].p[P_E7];   /* the track's patch now: fm6_poll keeps it */
    }
}

void FEL(midi)(uint32_t pkt) { if (mi_w - mi_r < MQ) midi_in_q[mi_w++ % MQ] = pkt; }

void FEL(render)(int32_t *out, uint32_t frames)   /* interleaved stereo, frames a multiple of CTL */
{
    uint32_t i;
    fm6_poll();   /* as Felucca's main loop: a changed FM6 PTCH loads that patch */
    for (i = 0; i + CTL <= frames; i += CTL)
        mix_block(out + 2u * i, CTL);
}
