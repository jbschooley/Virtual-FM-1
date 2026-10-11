/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Engine table (order = PRESETS browsing order and the engine numbers of the editor protocol) and the parts'
 * sounds at power-on. */
#include "dsp.c"
#include "eng_analog.c"
#if MELODEE_PROPHET
#include "eng_prophet_test.c"
#endif
#include "eng_phase.c"
#include "eng_cz.c"
#include "eng_sid.c"
#include "eng_formant.c"
#include "eng_trio.c"
#include "eng_wheel.c"
#include "eng_phys.c"           /* PHYS: DaisySP physical models (phys_dsp.c, MIT) */
#include "eng_drum.c"           /* DRUM: synthesized 808 circuits (drum_808.c) */
#include "eng_noise.c"
#include "eng_fm6.c"            /* FM6: 6-operator FM rendered as Dexed renders it (fm6_core.c) */
#include "fm4_convert.c"        /* DIGITAL's tables, and its sounds -> FM6 */
#if MELODEE_FM4
#include "eng_digital.c"        /* DIGITAL: four-operator FM (retired; MELODEE_FM4=1 builds it) */
#endif
#if MELODEE_SLICE
#include "eng_slice.c"
#else
static void slice_gone_note_on(struct track *t, voice_t *v) { (void)t; (void)v; }
static void slice_gone_render(struct track *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    (void)t; (void)v; (void)out; (void)n; (void)m;
}
static const engine_t ENG_RETIRED = {         /* 13 without MELODEE_SLICE: reserved, never offered (eng_ok) */
    .name = "-",
    .page_title = {"-", "-"},
    .edit = {{"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0},
             {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0},
             {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0}},
    .note_on = slice_gone_note_on,
    .render = slice_gone_render,
    .knob = {P_E4, P_E5, P_E6, P_REL},
};
#endif

/* Each track owns only its selected engine's working state. Simple engines
 * use voice_t alone. State is released after the old engine's switch fade. */
#include "resources.c"
static uint32_t eng_state_size(uint32_t e)
{
    switch (e) {
#if MELODEE_PROPHET
    case ENGI_PROPHET: return sizeof(p5_part_t);
#if MELODEE_PROPHET_PROTOTYPE
    case 0: return sizeof(p5_part_t);
#endif
#endif
#if MELODEE_LEGACY_EXTRAS
    case ENGI_PHYS: return sizeof(phys_slot_t) * PHYS_POLY;
    case 7: return sizeof(drw_part_t);
#endif
#if MELODEE_LEGACY_EXTRAS
    case 2:                                         /* (PHASE: CZ-1's core) */
#endif
    case ENGI_CZ: return sizeof(cz_part_t);
    case 3: return sizeof(sid_part_t);              /* SID (eng_sid.c) */
    case ENGI_DRUM: return sizeof(drum909_part_t);
    case ENGI_FM6: return sizeof(fm6_part_t);
#if MELODEE_SLICE
    case ENGI_SLICE: return sizeof(slc_rb_t);
#endif
    default: return 0;
    }
}
static void eng_state_clear(uint32_t part)
{
    if (part < NPART) resource_release(RES_ENGINE0 + part);
}
static void eng_state_reset(void) { for (uint32_t k = 0; k < NPART; k++) eng_state_clear(k); }
static int eng_state_prepare(const track_t *t)
{
    uint32_t size = t->engine==ENGI_DRUM?drum_bytes(t):eng_state_size(t->engine);
    return !size || resource_get(RES_ENGINE0 + (uint32_t)(t - trk), size) != 0;
}
static phys_slot_t *phys_slots(uint32_t part) { return resource_get(RES_ENGINE0 + part % NPART, sizeof(phys_slot_t) * PHYS_POLY); }
static drum909_part_t *drum909_part(uint32_t part) { return resource_get(RES_ENGINE0 + part % NPART, sizeof(drum909_part_t)); }
static drum_lane_t *drum_kit_part(uint32_t part) { return resource_get(RES_ENGINE0 + part % NPART, sizeof(drum_lane_t) * DV_NLANE); }
static drw_part_t *drw_of(const track_t *t) { return resource_get(RES_ENGINE0 + (uint32_t)(t - trk), sizeof(drw_part_t)); }
static fm6_part_t *fm6_part(uint32_t part) { return resource_get(RES_ENGINE0 + part % NPART, sizeof(fm6_part_t)); }
static cz_part_t *cz_part(uint32_t part) { return resource_get(RES_ENGINE0 + part % NPART, sizeof(cz_part_t)); }
static sid_part_t *sid_part(uint32_t part) { return resource_get(RES_ENGINE0 + part % NPART, sizeof(sid_part_t)); }
#if MELODEE_PROPHET
static p5_part_t *p5_part(uint32_t part) { return resource_get(RES_ENGINE0 + part % NPART, sizeof(p5_part_t)); }
#endif
#if MELODEE_SLICE
static int16_t (*slc_rbuf(uint32_t part))[SLC_RB] { return resource_get(RES_ENGINE0 + part % NPART, sizeof(slc_rb_t)); }
#endif

/* the editor protocol, user presets and projects store these indices: append, never reorder */
static const engine_t *const ENGINES[NENGINES] = {
#if MELODEE_PROPHET_PROTOTYPE
    &ENG_P5_TEST,                /* test firmware only: RAM patches, NEVER persist */
#else
    &ENG_ANALOG,                 /* 0 */
#endif
#if MELODEE_FM4
    &ENG_DIGITAL,                /* 1 (ENGI_DIGITAL) */
#else
    &ENG_FM4_GONE,               /* 1: reserved (DIGITAL, retired: its sounds convert to FM6, fm4_convert.c) */
#endif
#if MELODEE_LEGACY_EXTRAS
    &ENG_PHASE,                  /* 2 */
#else
    &ENG_RETIRED,                /* 2: retired PHASE; stored ID reserved (its core: CZ-1's) */
#endif
    &ENG_SID,                    /* 3: SID replaces LOFI without reusing a stored ID */
    &ENG_RETIRED,                /* 4: retired SAMPLE; reserved to preserve stored indices */
#if MELODEE_LEGACY_EXTRAS
    &ENG_FORMANT,                /* 5 VOICE (eng_formant.c: "voice" is a sounding note in voice.c) */
#else
    &ENG_RETIRED,                /* 5: retired VOICE; stored ID reserved */
#endif
#if MELODEE_LEGACY_EXTRAS
    &ENG_TRIO,
#else
    &ENG_RETIRED,                /* 6: retired TRIO; stored ID reserved */
#endif
#if MELODEE_LEGACY_EXTRAS
    &ENG_WHEEL,
#else
    &ENG_RETIRED,                /* 7: retired WHEEL; stored ID reserved */
#endif
    &ENG_RETIRED,                /* 8: retired GRAIN; stored index reserved */
#if MELODEE_LEGACY_EXTRAS
    &ENG_PHYS,
#else
    &ENG_RETIRED,                /* 9: retired PHYS; stored ID reserved */
#endif
    &ENG_DRUM,                   /* 10 (ENGI_DRUM) */
#if MELODEE_LEGACY_EXTRAS
    &ENG_NOISE,                  /* 11 */
#else
    &ENG_RETIRED,                /* 11: retired NOISE; stored ID reserved */
#endif
    &ENG_FM6,                    /* 12 (ENGI_FM6) */
#if MELODEE_SLICE
    &ENG_SLICE,                  /* 13 (ENGI_SLICE) */
#else
    &ENG_RETIRED,             /* 13: reserved (MELODEE_SLICE=0 builds without SLICE) */
#endif
    &ENG_RETIRED,                /* 14: retired OBXF; stored index reserved */
    &ENG_CZ,                     /* 15: native Casio CZ-1 */
    &ENG_RETIRED, &ENG_RETIRED, &ENG_RETIRED, /* 16..18 reserved */
    &ENG_P5_TEST,                /* 19: Prophet, never reuse ANALOG's ID */
};

/* a track's engine number as an index (the audio paths: a compare, cheaper than % NENGINES; a bad number: 0) */
static inline uint32_t eng_idx(uint32_t e) { return e < NENGINES ? e : 0u; }

/* the order the engines are shown in (PRESETS browsing and its ENG knob, the EDIT layer's keys, the editor's list):
 * engine indices, never DIGITAL's reserved 1 (with MELODEE_FM4 it follows FM6). The indices stay as they are (the
 * stores and the protocol hold them); only this table orders them */
static const uint8_t ENGINE_ORDER[NENG_SHOWN] = {
    ENGI_PROPHET,                /* Prophet replaces ANALOG in new-sound browsing */
    12,                          /* FM6 */
#if MELODEE_FM4
    1,                           /* DIGITAL */
#endif
#if MELODEE_LEGACY_EXTRAS
    2, ENGI_CZ, 3, 5,             /* PHASE CZ-1 SID VOICE */
    6, 7, 9,
    11,                          /* NOISE */
#else
    ENGI_CZ, 3,                   /* CZ-1 SID */
#endif
#if MELODEE_SLICE
    13,                          /* SLICE */
#endif
    10,                          /* DRUM */
};

/* Retired extra synth records retain their stored identity and render silence.
 * DIGITAL keeps its existing FM6 conversion. Never relabel retired records FM6. */
static int eng_extra_retired(uint32_t e)
{
    return !MELODEE_LEGACY_EXTRAS && (e == 2u || e == 5u || e == 6u || e == 7u || e == 9u || e == 11u);
}
/* the engines one can pick (engine 1 only with MELODEE_FM4), in ENGINE_ORDER: eng_ok(e), the n-th of them
 * eng_vis(n), e's place among them eng_rank(e), the next / previous one eng_step(e, dir) (wraps) */
static int eng_ok(uint32_t e)
{
    return e < NENGINES && !(e >= 16u && e <= 18u) && !eng_extra_retired(e) && e != 4u && e != 8u && e != 14u && (MELODEE_FM4 || e != ENGI_DIGITAL) && (MELODEE_SLICE || e != ENGI_SLICE);
}
static uint32_t eng_sound_idx(uint32_t e)
{
    return e == 2u || e == 5u || e == 6u || e == 7u || e == 9u || e == 11u ? e : eng_ok(e) ? e : ENGI_FM6;
}
static uint32_t eng_vis(uint32_t n) { return ENGINE_ORDER[n % NENG_SHOWN]; }
static uint32_t eng_rank(uint32_t e)
{
    uint32_t n;
    if (!eng_ok(e))
        e = ENGI_FM6;                            /* (DIGITAL without MELODEE_FM4: its sounds play as FM6) */
    for (n = 0; n < NENG_SHOWN && ENGINE_ORDER[n] != e; n++)
        ;
    return n < NENG_SHOWN ? n : 0u;
}
static uint32_t eng_step(uint32_t e, int32_t dir)
{
    return eng_vis((eng_rank(e % NENGINES) + (dir > 0 ? 1u : NENG_SHOWN - 1u)) % NENG_SHOWN);
}

/* the parts at power-on (engine, preset; the steps empty): bass, pad, lead, drums */
static const uint8_t TRK_DEF[NPART][2] = {{0, 4}, {ENGI_FM6, 4}, {3, 0}, {ENGI_DRUM, 0}}; /* ANALOG ACID,
                                                                       * FM6 PAD (was DIGITAL PAD), SID INIT SID, DRUM KIT */
static uint32_t trk_def_engine(uint32_t i) { return TRK_DEF[i % NPART][0]; }
