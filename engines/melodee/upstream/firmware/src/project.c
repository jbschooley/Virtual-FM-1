/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Projects: four slots in NOR, each holding 32 independent pattern banks.
 * FBKG (pattern_store.c) contains the FUN13 sound/current-pattern record below.
 * Hardware retains only slot names/occupancy in RAM; full records use one
 * main-loop staging buffer. Host fixtures also provide a RAM storage backend.
 *
 * Format 7 ("FUN7", read only) stores P_COUNT (byte 66) and maps an older count as user presets do: FUN7 of
 * 89 parameters (before the chord keys P_CHRD / P_VOIC) loads its engine values at today's P_E0..P_E7,
 * the chord keys OFF / CLOSE, and its motion events' ids from its P_E0 on move up with them (proj_motion_ids).
 * 68 + 4 x (91 + 2 + 64 x 9) + chain + motion = 3040 of the 3372 bytes before the name: 332 spare, room for
 * 83 more track parameters (4 bytes each).
 * Format 6 ("FUN6") adds a 36-byte song chain before the checksum.
 * Format 5 ("FUN5", read only) = format 4 with the drum grid: a step is 10 bytes (step_t: its lane hits and
 * their accents after the 8 bytes it was), and each track has 40 reserved bytes (lane[8][5], written 0: room
 * for per-lane sounds of the DRUM engine). 4 ("FUN4": PROJ_NP_V4 parameters, the modulation matrix), 3
 * ("FUN3": PROJ_NP_V3, the SLICER), 2 ("FUN2") and 1 ("FUN1": PROJ_NP_V2) are read and converted, mapped by
 * count as user presets are (the first np - 8 are P_LEVEL.. in order, the last 8 P_E0..P_E7; the
 * parameters added since take their defaults: the SLICER OFF, every matrix slot OFF). Their steps get no
 * hits; on a DRUM track their notes that are a lane's note become its hits (proj_grid: the same notes and
 * velocities play, the grid shows them as its own).
 * Their engine bytes are kept: formats 1 and 2 had engines 0..7 (ANALOG .. WHEEL), and the engines
 * added since (SLICE 8, ..) were appended, no index moved.
 *
 * Track 4 was the GM drum part until 1.0 (no engine: its byte 0; its level and reverb send in the
 * globals G_DRLVL / G_DRREV, which are inert now). A project says which it has in `parts`: NPART
 * when written since, 0 before (a reserved byte, always written 0: the format and its size did not
 * change). proj_drums_to_part turns such a track 4 into the 808 DRUM part
 * (the GM kit, so its drum steps still play drums), keeping its steps, its pattern and mix parameters
 * (LEN DIV SWING GATE, PAN MUTE), its SLICER, and the drum level and reverb send as LEVEL and REV.
 *
 * The byte `phys` (reserved, always 0, before 1.0) says what a PHYS track's MODEL means: 0 MODEL 2 was
 * DUST (dropped: it loads as MODAL bowed, eng_phys.c phys_legacy); 1 MODEL 4 was DRUM (the kit is the DRUM
 * engine since: such a track loads as DRUM, core.h drum_from_phys); 2 (PROJ_PHYS) as today. proj_phys.
 *
 * Format 8 ("FUN8", written since 1.0) = FUN7 with each track's FM6 patch (eng_fm6.c, the 128-byte packed
 * record, 4 x 128 bytes just before the name): a project is self-contained, whatever the patch bank holds.
 * It is 3584 bytes (FUN7: 3388); FUN7 is read (its tracks get the init patch).
 * Legacy single-pattern flash records load into bank 1; their old project-based
 * arrangement is cleared rather than interpreted as bank assignments.
 *
 * DIGITAL (engine 1) was retired in 1.0 (fm4_convert.c): a track of it, in any format, loads as FM6 with the
 * patch converted from its values as the track's own (proj_fm4, on every import; the stored record keeps what it
 * holds until saved again). Its motion events on the EDIT or OP ENV values are dropped.
 *
 * Built on the Mac too (tests/project_test.c, -DPROJ_HOST): the part above the #ifndef
 * PROJ_HOST needs core.h, params.c (TP) and engines.c. */
#include "cz_legacy.h"
#define PROJ_LEGACY_CZ_OLD 4236u
#define PROJ_LEGACY_CZ 4244u
#define PROJ_MAGIC_V8 0x46554E38u
#define PROJ_MAGIC_V10 0x46554E3Au
#define PROJ_MAGIC_V11 0x46554E3Bu
#define PROJ_MAGIC_V12 0x46554E3Cu
#define PROJ_MAGIC 0x46554E3Du                 /* FUN13: 1024 original timed notes */
#define PROJ_MAGIC_V7 0x46554E37u              /* "FUN7": serialized (byte params, packed steps), chain, motion */
#define PROJ_MAGIC_V6 0x46554E36u              /* FUN6: 69 parameters, drum grid, chain */
#define PROJ_MAGIC_V5 0x46554E35u              /* "FUN5": the grid, without the chain; read only */
#define PROJ_MAGIC_V4 0x46554E34u              /* "FUN4": four tracks, PROJ_NP_V4 parameters, 8-byte steps; read only */
#define PROJ_MAGIC_V3 0x46554E33u              /* "FUN3": four tracks, PROJ_NP_V3 parameters; read only */
#define PROJ_MAGIC_V2 0x46554E32u              /* "FUN2": four tracks, PROJ_NP_V2 parameters; read only */
#define PROJ_MAGIC_V1 0x46554E31u              /* "FUN1": one instrument; loads into track 1 */
#define PROJ_NP_V2 53u                         /* P_COUNT of formats 1 and 2 (P_E0 was 45) */
#define PROJ_NG_V2 27u                         /* G_COUNT of formats 1 and 2 */
#define PROJ_NP_V3 57u                         /* P_COUNT of format 3 (P_E0 was 49) */
#define PROJ_NP_V4 69u                         /* P_COUNT of format 4 (P_E0 61) */
#define PROJ_PHYS 2u                           /* project_t.phys: PHYS without DUST and DRUM (see the top) */
#define PROJ_NAME_LEN 12u                      /* the name: FUN7 bytes PROJ_NAME_OFF.. (the reserved tail's end) */
typedef struct {                               /* one track */
    int16_t p[P_COUNT];
    uint8_t engine, preset;
    step_t step[NSTEP];
} proj_trk_t;
typedef struct {
    uint32_t magic, size;
    int16_t g[G_COUNT];
    uint8_t sel;                               /* the selected track */
    uint8_t parts;                             /* NPART; 0: track 4 is the old GM drum part (see the top) */
    uint8_t phys;                              /* PROJ_PHYS: PHYS MODEL values as today; 1: MODEL 4 was DRUM;
                                                * 0 (a reserved byte before 1.0): MODEL 2 was DUST */
    uint8_t rsv;
    proj_trk_t t[NTRK];
    chain_config_t chain;
    motion_store_t motion;
    uint8_t fm6[NTRK][FM6_PACKED];             /* each track's FM6 patch, packed (eng_fm6.c) */
    uint8_t fm6_fn[NTRK][FM6_NFN];             /* .. and its function settings (fm6_fn_ok; else Dexed's): not in
                                                * FUN8, an FBK9 record keeps them (pattern_store.c BANK_FN_OFF) */
    uint8_t fm6_fn_ok, rsv2[3];
    cz_patch_t cz[NTRK];
    recorded_note_t recording[RECORD_MAX];
    uint8_t pattern[NTRK];                     /* FUN13's active-bank ids (also in the full container) */
    char name[PROJ_NAME_LEN];                  /* the project's name: upper-case ASCII 32..126, 0-padded; "" = none */
    uint32_t sum;
} project_t;
/* Historical FUN5/6 types are frozen, independent of today's P_COUNT/step_t. */
typedef struct { uint8_t note[4], n, time, flags, vel, hit, acc; } step10_t;
typedef struct { int16_t p[69]; uint8_t engine, preset; step10_t step[NSTEP]; uint8_t lane[NLANE][5]; } proj_trk_v5_t;
typedef struct {                               /* format 5, before the song chain */
    uint32_t magic, size;
    int16_t g[G_COUNT];
    uint8_t sel, parts, phys, rsv;
    proj_trk_v5_t t[NTRK];
    uint32_t sum;
} project_v5_t;
typedef struct { uint32_t magic, size; int16_t g[G_COUNT]; uint8_t sel, parts, phys, rsv;
    proj_trk_v5_t t[NTRK]; chain_config_t chain; uint32_t sum; } project_v6_t;
_Static_assert(sizeof(project_v5_t) == 3352u && sizeof(project_v6_t) == 3388u, "frozen formats 5 / 6 sizes");
/* Serialized FUN7 keeps the retained cache's exact extent. Params are biased
 * bytes, steps pack n/time/flags. Reserved tail is zero and covered by hash.
 * The tail's last 12 bytes (PROJ_NAME_OFF, just before the hash) are the project's name since 1.0:
 * ASCII 32..126 (upper case), 0-padded, all 0 = no name ("PROJECT A"). Firmware before wrote them 0 and
 * never reads them, so every FUN7 file stays valid both ways; FUN6..FUN1 imports get no name.
 * FUN8: the same, 3584 bytes, the four packed FM6 patches at PROJ_FM6_OFF (before the name); 12 bytes of the
 * reserved tail are left for parameters added later (P_MPCDEG took 4). */
#define PROJ_STORE_V8 3584u
#define PROJ_CZ_BYTES (NTRK * CZ_BYTES)
#define PROJ_STORE_V11 (PROJ_STORE_V8 + PROJ_CZ_BYTES)
#define PROJ_STORE_V12 (PROJ_STORE_V11 + 152u * 8u)
#define PROJ_STORE_SIZE (PROJ_STORE_V11 + RECORD_MAX * 8u)
#define PROJ_BANK_BYTES (19008u + RECORD_MAX * 8u)
#define PROJ_STORE_V7 3388u                    /* FUN7 */
#define PROJ_NAME_OFF (PROJ_STORE_SIZE - 4u - PROJ_NAME_LEN)
#define PROJ_REC_OFF (PROJ_STORE_V11 - 4u - PROJ_NAME_LEN)
#define PROJ_CZ_OFF (PROJ_REC_OFF - PROJ_CZ_BYTES)
#define PROJ_FM6_OFF (PROJ_CZ_OFF - NTRK * FM6_PACKED)
typedef union { uint32_t align; uint8_t raw[PROJ_STORE_SIZE]; } project_store_t;
_Static_assert(G_COUNT == 27u, "FUN7 globals retain original IDs");
_Static_assert(sizeof(project_store_t) == 12352u && PROJ_STORE_V7 == sizeof(project_v6_t), "FUN13 / FUN7 sizes");
typedef struct {                               /* a track of format 4, read only */
    int16_t p[PROJ_NP_V4];
    uint8_t engine, preset;
    step8_t step[NSTEP];
} proj_trk_v4_t;
typedef struct {                               /* format 4 (1.0 development builds), read only */
    uint32_t magic, size;
    int16_t g[G_COUNT];
    uint8_t sel, parts, phys, rsv;
    proj_trk_v4_t t[NTRK];
    uint32_t sum;
} project_v4_t;
typedef struct {                               /* a track of format 3, read only */
    int16_t p[PROJ_NP_V3];
    uint8_t engine, preset;
    step8_t step[NSTEP];
} proj_trk_v3_t;
typedef struct {                               /* format 3 (0.9 .. 1.0), read only */
    uint32_t magic, size;
    int16_t g[G_COUNT];
    uint8_t sel, parts, rsv[2];
    proj_trk_v3_t t[NTRK];
    uint32_t sum;
} project_v3_t;
#define PROJ_DEF_SOUND 0xFFu                   /* preset byte: the track's power-on sound, no steps (format 1) */
#define PROJ_DEF_KEEP 0xFEu                    /* .. the 808 DRUM sound, steps and the rest kept (old drums) */
typedef struct {                               /* a track of formats 1 and 2, read only */
    int16_t p[PROJ_NP_V2];
    uint8_t engine, preset;
    step8_t step[NSTEP];
} proj_trk_v2_t;
typedef struct {                               /* format 2 (until 0.9), read only */
    uint32_t magic, size;
    int16_t g[PROJ_NG_V2];
    uint8_t sel, rsv[3];
    proj_trk_v2_t t[NTRK];
    uint32_t sum;
} project_v2_t;
typedef struct {                               /* format 1 (until 0.5 beta), read only */
    uint32_t magic, size;
    int16_t g[PROJ_NG_V2];
    proj_trk_v2_t t;
    uint32_t sum;
} project_v1_t;
_Static_assert(sizeof(project_v2_t) == 2552u && sizeof(project_v1_t) == 688u && sizeof(project_v3_t) == 2584u &&
               sizeof(project_v4_t) == 2680u, "formats 1 / 2 / 3 / 4 as they were stored");
#if !defined(FM1_IRQ_TARGET)
project_store_t proj_slot[4];                 /* host-only cache; hardware reads complete projects from NOR */
static uint8_t proj_bank_slot[4][PROJ_BANK_BYTES];
#endif
static struct { uint8_t used; char name[PROJ_NAME_LEN + 1u]; } proj_meta[4];

static uint32_t proj_hash(const void *p, uint32_t n)   /* FNV-1a over n bytes */
{
    const uint8_t *b = (const uint8_t *)p;
    uint32_t i, s = 0x811C9DC5u;
    for (i = 0; i < n; i++)
        s = (s ^ b[i]) * 16777619u;
    return s;
}
static uint32_t proj_sum(const project_t *p) { return proj_hash(p, sizeof *p - 4u); }
static int proj_ok(const project_t *q)
{
    if (q->magic != PROJ_MAGIC || q->size != sizeof *q || q->sum != proj_sum(q) ||
        !chain_valid(&q->chain) || !motion_valid(&q->motion)) return 0;
    for (uint32_t k = 0; k < NTRK; k++) if (q->pattern[k] >= NPAT) return 0;
    for (uint32_t i = 0; i < RECORD_MAX; i++) if (!recording_valid(&q->recording[i])) return 0;
    return 1;
}

/* G_RTYPE (id 24) was G_DRCH, the GM drum part's MIDI channel (0..16, 10 by default) until 1.0: a project of a
 * format before FUN7 may hold any channel there. FUN7 came after it was inert (always 0), so only those imports
 * set it: ROOM, the only reverb they knew */
static void proj_rtype_room(int16_t *g) { g[G_RTYPE] = 0; }

/* the globals of formats 1 and 2 (G_* unchanged since; any added later: their defaults) */
static void proj_g_from_v2(int16_t *g, const int16_t *g2)
{
    uint32_t i;
    for (i = 0; i < G_COUNT; i++)
        g[i] = i < PROJ_NG_V2 ? g2[i] : GP[i].def;
    proj_rtype_room(g);
}

/* np stored parameters, engine, preset and steps of an older track -> today's, mapped by count (see the top);
 * the steps without hits */
static void proj_trk_from(proj_trk_t *d, const int16_t *p, uint32_t np, uint8_t engine, uint8_t preset, const step8_t *step)
{
    int16_t def[P_E0];
    uint32_t k;
    for (k = 0; k < P_E0; k++)
        def[k] = TP[k].def;
    params_by_count(d->p, p, np, def);
    d->p[P_RECQ] = 0; /* this reserved field was inert in old projects */
    d->engine = engine;                         /* (indices 0..7 of formats 1 and 2 as they were) */
    d->preset = preset;
    for (k = 0; k < NSTEP; k++) {
        step_t *s = &d->step[k];
        memcpy(s->note, step[k].note, 4);
        s->n = step[k].n;
        s->time = step[k].time;
        s->flags = step[k].flags;
        s->vel = step[k].vel;
        s->hit = s->acc = 0;
    }
}

/* the DRUM tracks of a project of before the grid: their lanes' notes as hits (see the top) */
static void proj_grid(project_t *q)
{
    uint32_t k, i;
    for (k = 0; k < NTRK; k++)
        if (q->t[k].engine == ENGI_DRUM)
            for (i = 0; i < NSTEP; i++)
                step_to_grid(&q->t[k].step[i]);
    q->sum = proj_sum(q);
}
static void proj_trk_from_v2(proj_trk_t *d, const proj_trk_v2_t *s)
{
    proj_trk_from(d, s->p, PROJ_NP_V2, s->engine, s->preset, s->step);
}

/* a project written with the GM drum part as track 4 (parts 0) -> track 4 a part (see the top);
 * project_load gives it the 808 DRUM sound (PROJ_DEF_KEEP). Idempotent */
static void proj_drums_to_part(project_t *q)
{
    proj_trk_t *d = &q->t[NTRK - 1u];
    if (q->parts == NPART)
        return;
    d->engine = ENGI_DRUM;                      /* Retired separate kit now uses the 808. */
    d->preset = PROJ_DEF_KEEP;
    d->p[P_LEVEL] = (int16_t)clamp(q->g[G_DRLVL], 0, 127);   /* the drum part's level and reverb send */
    d->p[P_REV] = (int16_t)clamp(q->g[G_DRREV], 0, 127);
    q->parts = NPART;
    q->sum = proj_sum(q);
}

/* PHYS tracks of a project written before PROJ_PHYS (see the top) -> today's: DUST as MODAL bowed, MODEL
 * DRUM as the DRUM engine (its E values moved, the preset its first). Idempotent */
static void proj_phys(project_t *q)
{
    uint32_t k;
    if (q->phys >= PROJ_PHYS)
        return;
    for (k = 0; k < NTRK; k++) {
        proj_trk_t *d = &q->t[k];
        if (d->engine != ENGI_PHYS)
            continue;
        if (!q->phys)
            phys_legacy(&d->p[P_E0]);
        if (drum_from_phys(d->engine, &d->p[P_E0])) {
            d->engine = ENGI_DRUM;
            d->preset = 0;
        }
    }
    q->phys = PROJ_PHYS;
    q->sum = proj_sum(q);
}

/* DIGITAL tracks (engine 1; without MELODEE_FM4) -> FM6 with the converted patch as the track's own (fm4_convert.c,
 * whatever format the project is: the conversion runs on every load, the stored record keeps what it holds until it
 * is saved again). Their motion events on the EDIT values or the OP ENV values go: DIGITAL's meanings do not carry
 * over to FM6's macros. Idempotent */
static void proj_fm4(project_t *q)
{
#if !MELODEE_FM4
    uint32_t k, i, n, hit = 0;
    for (k = 0; k < NTRK; k++) {
        proj_trk_t *d = &q->t[k];
        uint8_t v[FP_SIZE + 1u];
        uint32_t pr;
        if (d->engine != ENGI_DIGITAL)
            continue;
        for (i = 0; i < P_COUNT; i++)
            d->p[i] = (int16_t)clamp(d->p[i], param_desc_of(ENGI_DIGITAL, i)->min, param_desc_of(ENGI_DIGITAL, i)->max);
        pr = fm4_convert(d->p, v);
        fm6_pack(v, q->fm6[k]);
        d->engine = ENGI_FM6;
        if (d->preset < PROJ_DEF_KEEP)
            d->preset = (uint8_t)pr;
        hit |= 1u << k;
    }
    if (!hit)
        return;
    for (i = n = 0; i < q->motion.count && i < MOTION_MAX; i++) {
        const motion_event_t *e = &q->motion.event[i];
        if (((hit >> (e->place >> 6)) & 1u) && (e->param >= P_E0 || (e->param >= P_FM1_ATK && e->param <= P_FM4_LEVEL)))
            continue;
        q->motion.event[n++] = *e;
    }
    for (i = n; i < q->motion.count && i < MOTION_MAX; i++)
        memset(&q->motion.event[i], 0, sizeof q->motion.event[i]);
    q->motion.count = (uint8_t)n;
    q->sum = proj_sum(q);
#else
    (void)q;
#endif
}

/* a format 4 project (n bytes in *v4) -> slot q as format 6 */
static int proj_from_v4(project_t *q, const project_v4_t *v4, int n)
{
    uint32_t i;
    if (n != (int)sizeof *v4 || v4->magic != PROJ_MAGIC_V4 || v4->size != sizeof *v4 ||
        v4->sum != proj_hash(v4, sizeof *v4 - 4u))
        return 0;
    memset(q, 0, sizeof *q);
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    memcpy(q->g, v4->g, sizeof q->g);
    proj_rtype_room(q->g);
    q->sel = v4->sel;
    q->parts = v4->parts;
    q->phys = v4->phys;
    for (i = 0; i < NTRK; i++)
        proj_trk_from(&q->t[i], v4->t[i].p, PROJ_NP_V4, v4->t[i].engine, v4->t[i].preset, v4->t[i].step);
    q->sum = proj_sum(q);
    proj_drums_to_part(q);
    return 1;
}

/* a format 3 project (n bytes in *v3) -> slot q as format 6 */
static int proj_from_v3(project_t *q, const project_v3_t *v3, int n)
{
    uint32_t i;
    if (n != (int)sizeof *v3 || v3->magic != PROJ_MAGIC_V3 || v3->size != sizeof *v3 ||
        v3->sum != proj_hash(v3, sizeof *v3 - 4u))
        return 0;
    memset(q, 0, sizeof *q);
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    memcpy(q->g, v3->g, sizeof q->g);
    proj_rtype_room(q->g);
    q->sel = v3->sel;
    q->parts = v3->parts;
    for (i = 0; i < NTRK; i++)
        proj_trk_from(&q->t[i], v3->t[i].p, PROJ_NP_V3, v3->t[i].engine, v3->t[i].preset, v3->t[i].step);
    q->sum = proj_sum(q);
    proj_drums_to_part(q);                     /* (a format 3 of firmware before 1.0: parts 0) */
    return 1;
}

/* a format 2 project (n bytes in *v2) -> slot q as format 6 */
static int proj_from_v2(project_t *q, const project_v2_t *v2, int n)
{
    uint32_t i;
    if (n != (int)sizeof *v2 || v2->magic != PROJ_MAGIC_V2 || v2->size != sizeof *v2 ||
        v2->sum != proj_hash(v2, sizeof *v2 - 4u))
        return 0;
    memset(q, 0, sizeof *q);
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    proj_g_from_v2(q->g, v2->g);
    q->sel = v2->sel;
    for (i = 0; i < NTRK; i++)
        proj_trk_from_v2(&q->t[i], &v2->t[i]);
    proj_drums_to_part(q);                     /* (format 2 had the drum track: parts 0) */
    return 1;
}

/* a format 1 project (n bytes in *v1) -> slot q as format 6: the instrument becomes track 1,
 * tracks 2..4 start empty (their sounds as at power-on) */
static int proj_from_v1(project_t *q, const project_v1_t *v1, int n)
{
    uint32_t i;
    if (n != (int)sizeof *v1 || v1->magic != PROJ_MAGIC_V1 || v1->size != sizeof *v1 ||
        v1->sum != proj_hash(v1, sizeof *v1 - 4u))
        return 0;
    memset(q, 0, sizeof *q);
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    proj_g_from_v2(q->g, v1->g);
    q->parts = NPART;                          /* (format 1 had no track 4) */
    proj_trk_from_v2(&q->t[0], &v1->t);
    for (i = 1; i < NTRK; i++) {               /* the other tracks: their defaults, no steps */
        uint32_t k;
        for (k = 0; k < P_COUNT; k++)
            q->t[i].p[k] = param_desc_of(trk_def_engine(i), k)->def;
        q->t[i].engine = (uint8_t)trk_def_engine(i);
        q->t[i].preset = PROJ_DEF_SOUND;
        for (k = 0; k < NSTEP; k++)
            q->t[i].step[k].time = ST_REST;
    }
    q->sum = proj_sum(q);
    return 1;
}

/* n bytes of a stored project (any format) -> slot q as format 6; 0 = not a project */
static int proj_unpack(project_t *q, const uint8_t *b, uint32_t size);
/* the init patch on every track (a project of a format before FUN8) */
static void proj_fm6_init(project_t *q)
{
    uint32_t t;
    for (t = 0; t < NTRK; t++)
        memcpy(q->fm6[t], FM6_INIT, FM6_PACKED);
    for (t = 0; t < NTRK; t++) {
        cz_patch_init(q->cz[t].raw);
    }
    q->sum = proj_sum(q);
}
static int proj_import_old(project_t *q, const void *b, int n);
static int proj_import_any(project_t *q, const void *b, int n);
/* n bytes of a stored project (any format) -> q as today's, DIGITAL tracks converted; 0 = not a project */
static int proj_import(project_t *q, const void *b, int n)
{
    if (!proj_import_any(q, b, n))
        return 0;
    proj_fm4(q);
    return 1;
}
static int proj_fn_none(project_t *q)          /* a record without FM6 function settings: Dexed's */
{
    memset(q->fm6_fn, 0, sizeof q->fm6_fn);
    q->fm6_fn_ok = 0;
    q->sum = proj_sum(q);
    return 1;
}
static int proj_import_any(project_t *q, const void *b, int n)
{
    if((n==PROJ_LEGACY_CZ && ((const uint32_t *)b)[0]==0x46554E42u) || (n==PROJ_LEGACY_CZ_OLD && ((const uint32_t *)b)[0]==0x46554E41u))return proj_unpack(q,b,(uint32_t)n) && proj_fn_none(q);
    if (n == PROJ_STORE_SIZE && ((const uint32_t *)b)[0] == PROJ_MAGIC)
        return proj_unpack(q, b, PROJ_STORE_SIZE) && proj_fn_none(q);
    if (n == PROJ_STORE_V12 && ((const uint32_t *)b)[0] == PROJ_MAGIC_V12)
        return proj_unpack(q, b, PROJ_STORE_V12) && proj_fn_none(q);
    if (n == PROJ_STORE_V11 && (((const uint32_t *)b)[0] == PROJ_MAGIC_V11 || ((const uint32_t *)b)[0] == PROJ_MAGIC_V10))
        return proj_unpack(q, b, PROJ_STORE_V11) && proj_fn_none(q);
    if (n == PROJ_STORE_V8 && ((const uint32_t *)b)[0] == PROJ_MAGIC_V8)
        return proj_unpack(q, b, PROJ_STORE_V8) && proj_fn_none(q);
    if (n == PROJ_STORE_SIZE && ((const uint32_t *)b)[1] >= 8u && ((const uint32_t *)b)[1] < PROJ_STORE_SIZE)
        return proj_import_any(q, b, (int)((const uint32_t *)b)[1]); /* padded cache: validate the original format */
    if (n == (int)PROJ_STORE_V7 && ((const uint32_t *)b)[0] == PROJ_MAGIC_V7)
        return proj_unpack(q, b, PROJ_STORE_V7) && proj_fn_none(q);
    if (n == (int)sizeof *q && proj_ok((const project_t *)b)) {
        memcpy(q, b, sizeof *q);
        proj_drums_to_part(q);
        proj_phys(q);
        return 1;
    }
    if (!proj_import_old(q, b, n))
        return 0;
    proj_fm6_init(q);
    return proj_fn_none(q);
}
static int proj_import_old(project_t *q, const void *b, int n)
{
    if (n == (int)sizeof(project_v5_t) || n == (int)sizeof(project_v6_t)) {
        const project_v5_t *v = b;
        const project_v6_t *v6 = b;
        uint32_t bytes = n, i, k;
        if (((bytes == sizeof *v && v->magic == PROJ_MAGIC_V5) ||
             (bytes == sizeof *v6 && v->magic == PROJ_MAGIC_V6 && chain_valid(&v6->chain))) &&
            v->size == bytes && ((const uint32_t *)b)[bytes / 4u - 1u] == proj_hash(b, bytes - 4u)) {
            memset(q, 0, sizeof *q);
            q->magic = PROJ_MAGIC; q->size = sizeof *q;
            memcpy(q->g, v->g, sizeof q->g);
            proj_rtype_room(q->g);
            q->sel = v->sel; q->parts = v->parts; q->phys = v->phys;
            for (i = 0; i < NTRK; i++) {
                int16_t def[P_COUNT];
                for (k = 0; k < P_COUNT; k++) def[k] = param_desc_of(v->t[i].engine % NENGINES, k)->def;
                params_by_count(q->t[i].p, v->t[i].p, 69u, def);
                q->t[i].p[P_RECQ] = 0;
                q->t[i].engine = v->t[i].engine; q->t[i].preset = v->t[i].preset;
                for (k = 0; k < NSTEP; k++) memcpy(&q->t[i].step[k], &v->t[i].step[k], sizeof(step10_t));
            }
            if (bytes == sizeof *v6) q->chain = v6->chain; else chain_defaults(&q->chain);
            q->sum = proj_sum(q); proj_drums_to_part(q); proj_phys(q);
            return 1;
        }
    }
    if (proj_from_v4(q, (const project_v4_t *)b, n) || proj_from_v3(q, (const project_v3_t *)b, n) ||
        proj_from_v2(q, (const project_v2_t *)b, n) || proj_from_v1(q, (const project_v1_t *)b, n)) {
        proj_phys(q);                          /* (formats 1..3 had no PHYS track: only the byte) */
        proj_grid(q);                          /* (after it: a PHYS DRUM track is DRUM now) */
        return 1;
    }
    return 0;
}

/* 12 stored name bytes -> d (PROJ_NAME_LEN + 1): up to the first 0, upper case; a byte outside 32..126 makes it
 * no name (a damaged tail never refuses the project) */
static uint32_t proj_name_get(char *d, const uint8_t *s)   /* its length */
{
    uint32_t i;
    for (i = 0; i < PROJ_NAME_LEN && s[i]; i++) {
        if (s[i] < 32u || s[i] > 126u) { i = 0; break; }
        d[i] = (char)(s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32u : s[i]);
    }
    d[i] = 0;
    return i;
}

/* A stable serialized schema: first header retains FUN6's fields; at byte66
 * np, format flags, then four byte-param tracks and nine-byte steps; FUN8: the patches at PROJ_FM6_OFF. */
static int proj_pack(project_store_t *out, const project_t *q)
{
    uint8_t *b = out->raw; uint32_t pos = 68u, t, i; uint32_t magic = PROJ_MAGIC, size = PROJ_STORE_SIZE, sum;
    for (t = 0; t < NTRK; t++)
        for (i = 0; i < FM6_PACKED; i++)
            if (q->fm6[t][i] > 127u) return 0;
    if (!chain_valid(&q->chain) || !motion_valid(&q->motion) || P_COUNT > 127u) return 0;
    memset(out, 0, sizeof *out); memcpy(b, &magic, 4); memcpy(b + 4, &size, 4);
    memcpy(b + 8, q->g, sizeof q->g); b[62] = q->sel; b[63] = q->parts; b[64] = q->phys; b[66] = P_COUNT;
    uint32_t banks = 0;
    for (t = 0; t < NTRK; t++) { if (q->pattern[t] >= NPAT) return 0; banks |= q->pattern[t] << (3u * t); }
    b[65] = (uint8_t)banks; b[67] = (uint8_t)(banks >> 8);
    for (t = 0; t < NTRK; t++) {
        for (i = 0; i < P_COUNT; i++) {
            if (q->t[t].p[i] < -64 || q->t[t].p[i] > 127) return 0;
            b[pos++] = (uint8_t)(q->t[t].p[i] + 64);
        }
        b[pos++] = q->t[t].engine; b[pos++] = q->t[t].preset;
        for (i = 0; i < NSTEP; i++) {
            const step_t *s = &q->t[t].step[i];
            if (s->n > 4u || s->time > ST_REST || (s->flags & ~7u) || s->probability > 101u) return 0;
            for (uint32_t j = 0; j < 4u; j++)           /* what proj_unpack checks, so a saved project always loads: */
                b[pos++] = s->note[j] > 127u ? 127u : s->note[j];   /* notes and velocity 0..127, accents */
            b[pos++] = (uint8_t)(s->n | s->time << 3 | (s->flags & 3u) << 5 | (s->flags & SF_RECORDED ? 128u : 0u));
            b[pos++] = s->vel > 127u ? 127u : s->vel; b[pos++] = s->hit; b[pos++] = s->hit ? s->acc & s->hit : s->acc;
            b[pos++] = s->probability;
        }
    }
    if (pos + sizeof q->chain + sizeof q->motion > PROJ_FM6_OFF) return 0;
    memcpy(b + pos, &q->chain, sizeof q->chain); pos += sizeof q->chain;
    memcpy(b + pos, &q->motion, sizeof q->motion);
    memcpy(b + PROJ_FM6_OFF, q->fm6, sizeof q->fm6);
    for (t = 0; t < NTRK; t++) {
        if (!cz_patch_valid(q->cz[t].raw)) return 0;
        memcpy(b + PROJ_CZ_OFF + t*CZ_BYTES, q->cz[t].raw, CZ_BYTES);
    }
    for (i = 0; i < RECORD_MAX; i++) if (!recording_valid(&q->recording[i])) return 0;
    memcpy(b + PROJ_REC_OFF, q->recording, sizeof q->recording);
    {   /* the name (0-padded; stops at the first 0) */
        char n[PROJ_NAME_LEN + 1u];
        memcpy(b + PROJ_NAME_OFF, n, proj_name_get(n, (const uint8_t *)q->name));
    }
    sum = proj_hash(b, PROJ_STORE_SIZE - 4u); memcpy(b + PROJ_STORE_SIZE - 4u, &sum, 4);
    return 1;
}
/* the motion of a FUN7 written with np parameters (np < P_COUNT: before the chord keys, 89) -> today's ids:
 * an event names a parameter id, and the ids from that store's P_E0 (np - 8) on moved up with P_E0, as its
 * values did (params_by_count); the ones below kept theirs */
static void proj_motion_ids(motion_store_t *m, uint32_t np)
{
    uint32_t i;
    for (i = 0; i < m->count && i < MOTION_MAX; i++)
        if (np < P_COUNT && m->event[i].param >= np - 8u && m->event[i].param < np)
            m->event[i].param = (uint8_t)(m->event[i].param + P_COUNT - np);
}
/* Serialized FUN13 / FUN12 / FUN11 / FUN10 / FUN8 / FUN7: older records initialize their missing patches. */
static int proj_unpack(project_t *q, const uint8_t *b, uint32_t st)
{
    uint32_t pos = 68u, t, i, magic, size, sum, np = b[66], v7 = st == PROJ_STORE_V7;
    uint32_t v13 = st == PROJ_STORE_SIZE, v12 = v13 || st == PROJ_STORE_V12, v10 = v12 || st == PROJ_STORE_V11;
    uint32_t rec_bytes = v13 ? RECORD_MAX * 8u : v12 ? 152u * 8u : 0u;
    uint32_t lc = st==PROJ_LEGACY_CZ?165u:st==PROJ_LEGACY_CZ_OLD?163u:0u;
    uint32_t name_off = st - 4u - PROJ_NAME_LEN, end = v7 ? name_off : name_off - rec_bytes - NTRK * FM6_PACKED - (lc ? NTRK*lc : v10 ? PROJ_CZ_BYTES : 0u);
    memcpy(&magic, b, 4); memcpy(&size, b + 4, 4); memcpy(&sum, b + st - 4u, 4);
    if ((magic != (lc ? (lc==165u?0x46554E42u:0x46554E41u) : v7 ? PROJ_MAGIC_V7 : v13 ? PROJ_MAGIC : v12 ? PROJ_MAGIC_V12 : v10 ? PROJ_MAGIC_V11 : PROJ_MAGIC_V8) && !(v10 && !v12 && magic == PROJ_MAGIC_V10)) || size != st || sum != proj_hash(b, st - 4u) ||
        np < 8u || np > P_COUNT || 68u + NTRK * (np + 2u + NSTEP * 9u) + sizeof q->chain + sizeof q->motion > end)
        return 0;
    memset(q, 0, sizeof *q); q->magic = PROJ_MAGIC; q->size = sizeof *q;
    memcpy(q->g, b + 8, sizeof q->g); q->sel = b[62]; q->parts = b[63]; q->phys = b[64];
    if (v12) {
        uint32_t banks = b[65] | (uint32_t)b[67] << 8;
        if (banks & 0xF000u) return 0;
        for (t = 0; t < NTRK; t++) q->pattern[t] = (uint8_t)((banks >> (3u * t)) & 7u);
    }
    if (v7 && (q->g[G_RTYPE] < 0 || q->g[G_RTYPE] > 1))   /* a FUN7 may still hold the old drum channel there */
        proj_rtype_room(q->g);
    for (t = 0; t < NTRK; t++) {
        int16_t values[P_COUNT], def[P_COUNT];
        for (i = 0; i < np; i++) { if (b[pos] > 191u) return 0; values[i] = (int16_t)b[pos++] - 64; }
        q->t[t].engine = b[pos++]; q->t[t].preset = b[pos++];
        for (i = 0; i < P_COUNT; i++) def[i] = param_desc_of(q->t[t].engine % NENGINES, i)->def;
        params_by_count(q->t[t].p, values, np, def);
        if (!v12) q->t[t].p[P_RECQ] = 0;
        for (i = 0; i < NSTEP; i++) {
            step_t *s = &q->t[t].step[i]; uint32_t meta;
            memcpy(s->note, b + pos, 4); pos += 4; meta = b[pos++];
            s->n = meta & 7u; s->time = (meta >> 3) & 3u; s->flags = (meta >> 5) & 3u;
            if (v12 && (meta & 128u)) s->flags |= SF_RECORDED;
            s->vel = b[pos++]; s->hit = b[pos++]; s->acc = b[pos++]; s->probability = b[pos++];
            if ((!v12 && meta > 127u) || s->n > 4u || s->time > ST_REST || s->probability > 101u) return 0;
            for (uint32_t j = 0; j < 4u; j++) if (s->note[j] > 127u) return 0;
            if (s->vel > 127u || (magic == PROJ_MAGIC || magic == PROJ_MAGIC_V12 || magic == PROJ_MAGIC_V11 ? !step_acc_valid(s) : (s->acc & ~s->hit))) return 0;
        }
    }
    memcpy(&q->chain, b + pos, sizeof q->chain); pos += sizeof q->chain;
    memcpy(&q->motion, b + pos, sizeof q->motion);
    proj_motion_ids(&q->motion, np);
    if (!chain_valid(&q->chain) || !motion_valid(&q->motion)) return 0;
    for (t = 0; t < NTRK; t++) {
        if (v7)
            memcpy(q->fm6[t], FM6_INIT, FM6_PACKED);
        else
            for (i = 0; i < FM6_PACKED; i++)
                q->fm6[t][i] = b[end + t * FM6_PACKED + i] & 0x7Fu;
    }
    for (t = 0; t < NTRK; t++) {
        if(lc){
            if(!cz_legacy_tone(q->cz[t].raw,b+PROJ_CZ_OFF+t*lc,lc))return 0;
            if(q->t[t].engine==14u){q->t[t].engine=ENGI_CZ;q->t[t].preset=0;for(uint32_t k=P_E0;k<P_COUNT;k++)q->t[t].p[k]=param_desc_of(ENGI_CZ,k)->def;q->t[t].p[P_E7]=CZ_NATIVE;}
        } else if (v10) {
            memcpy(q->cz[t].raw, b + PROJ_CZ_OFF + t*CZ_BYTES, CZ_BYTES);
            if (!cz_patch_valid(q->cz[t].raw)) return 0;
        } else { cz_patch_init(q->cz[t].raw); if (q->t[t].engine == ENGI_CZ && q->t[t].p[P_E7] == CZ_NATIVE) q->t[t].p[P_E7] = 0; }
    }
    if (v12) {
        memcpy(q->recording, b + PROJ_REC_OFF, rec_bytes);
        for (i = 0; i < RECORD_MAX; i++) if (!recording_valid(&q->recording[i])) return 0;
    }
    {
        char n[PROJ_NAME_LEN + 1u];
        memcpy(q->name, n, proj_name_get(n, b + name_off));
    }
    q->sum = proj_sum(q); proj_drums_to_part(q); proj_phys(q);
    return 1;
}

#ifndef PROJ_HOST
/* Old GM projects used this kit before DRUM existed. Its saved initializer is private now;
 * restore the same sound without passing its index through factory preset browsing. */
static void proj_legacy_perc(track_t *t)
{
    static const uint8_t FX_DEF[4] = {0, 24, 28, 36};
    const preset_t *pr = &DRUM_PRESETS[0];
    t->eng_req = ENGI_DRUM;
    uint32_t i;
    t->preset = 0;                              /* display metadata; SET remains the legacy kit */
    for (i = 0; i < P_E0; i++)
        if (!param_kept(i))
            t->p[i] = TP[i].def;
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = pr->e[i];
    t->p[P_ATK] = pr->env[0];
    t->p[P_DEC] = pr->env[1];
    t->p[P_SUS] = pr->env[2];
    t->p[P_REL] = pr->env[3];
    t->p[P_ED_FLT] = pr->fenv;
    t->p[P_VOICE] = pr->mono ? V_LEGATO : V_POLY;
    for (i = 0; i < 4u; i++)
        t->p[P_DIST + i] = (int16_t)(pr->fx[i] ? pr->fx[i] - 1 : FX_DEF[i]);
}

static project_t proj_scratch __attribute__((section(".pool")));              /* decoded main-loop work, never audio ISR */
static char proj_name[PROJ_NAME_LEN + 1u]    /* the name of the music as it is now (loaded, saved, the editor's */
    __attribute__((section(".pool")));       /* runtime restore); "" = none. A save takes it unless one is given */
#define PROJ_NO_SLOT 0xFFu
static uint8_t proj_cur = PROJ_NO_SLOT;      /* the slot the music was loaded from or last saved to (a rename of it
                                              * renames the music too); PROJ_NO_SLOT none (the editor's restore) */
static union {                               /* serialized main-loop work, also used for archive transfers */
    project_store_t p;                       /* standalone legacy record view */
    uint8_t raw[PROJ_BANK_BYTES];              /* complete FBKG, including legacy imports */
} proj_wire_u __attribute__((section(".pool")));
#define proj_wire proj_wire_u.p
static uint8_t proj_wire_gen;                /* +1 whenever proj_wire is rewritten (a backup's runtime copy lives there) */

static void proj_steps(step_t *s)            /* a loaded sequence stays inside its fixed fields */
{
    uint32_t i, j;
    for (i = 0; i < NSTEP; i++) {
        if (s[i].n > 4u) s[i].n = 4;
        if (s[i].time > ST_REST) s[i].time = ST_REST;
        for (j = 0; j < 4u; j++) s[i].note[j] &= 127u;
        if (s[i].hit) s[i].acc &= s[i].hit;
        else if (s[i].time == ST_REST) s[i].acc = 0;
    }
}

/* an imported older format may hold values FUN7 cannot pack: keep them inside the fields and ranges */
static void proj_bound(project_t *q)
{
    uint32_t t, i;
    for (t = 0; t < NTRK; t++) {
        proj_steps(q->t[t].step);
        for (i = 0; i < NSTEP; i++) {
            step_t *s = &q->t[t].step[i];
            s->flags &= 7u;
            s->vel &= 127u;
            if (s->probability > 101u) s->probability = 0;
        }
        for (i = 0; i < P_COUNT; i++) {
            const param_desc_t *d = param_desc_of(q->t[t].engine % NENGINES, i);
            q->t[t].p[i] = (int16_t)clamp(q->t[t].p[i], d->min, d->max);
        }
    }
    q->sum = proj_sum(q);
}

#include "pattern_store.c"

static int proj_read_slot(uint32_t slot)
{
    int n = -1;
    slot &= 3u;
    proj_wire_gen++;
#if MELODEE_FLASH
    if (flash_ok) {
        n = st_load(OBJ_BANK0 + slot, proj_wire_u.raw, sizeof proj_wire_u.raw);
        if (n < 0) n = st_load(OBJ_PROJECT0 + slot, proj_wire_u.raw, sizeof proj_wire_u.raw);
    }
#else
    if (bank_valid(proj_bank_slot[slot], BANK_STORE_SIZE) &&
        !memcmp(proj_bank_slot[slot] + 8u, &proj_slot[slot], sizeof proj_slot[slot])) {
        n = BANK_STORE_SIZE; memcpy(proj_wire_u.raw, proj_bank_slot[slot], BANK_STORE_SIZE);
    } else { n = sizeof proj_slot[slot]; memcpy(proj_wire_u.raw, &proj_slot[slot], sizeof proj_slot[slot]); }
#endif
    int valid = bank_full((uint32_t)n) ? bank_valid(proj_wire_u.raw, n) : proj_import(&proj_scratch, proj_wire_u.raw, n);
    if (valid && bank_full((uint32_t)n)) { bank_upgrade(proj_wire_u.raw); n = BANK_STORE_SIZE; }
#if MELODEE_FLASH && !defined(FM1_IRQ_TARGET)
    if (valid) {
        if (n == BANK_STORE_SIZE) { memcpy(proj_bank_slot[slot], proj_wire_u.raw, BANK_STORE_SIZE); memcpy(&proj_slot[slot], proj_wire_u.raw + 8u, sizeof proj_slot[slot]); }
        else { memset(proj_bank_slot[slot], 0, sizeof proj_bank_slot[slot]); proj_bound(&proj_scratch); proj_pack(&proj_slot[slot], &proj_scratch); }
    }
#endif
    proj_meta[slot].used = (uint8_t)valid;
    if (valid) proj_name_get(proj_meta[slot].name, (const uint8_t *)proj_scratch.name);
    else proj_meta[slot].name[0] = 0;
    return valid ? n : 0;
}
static void proj_fetch(uint32_t slot) { (void)proj_read_slot(slot); }
static int proj_write_slot(uint32_t slot, const uint8_t *raw, uint32_t len)
{
    slot &= 3u;
#if MELODEE_FLASH
    if (!flash_ok || st_save(OBJ_BANK0 + slot, raw, len)) return 2;
#endif
#if !defined(FM1_IRQ_TARGET)
    memset(proj_bank_slot[slot], 0, sizeof proj_bank_slot[slot]);
    memset(&proj_slot[slot], 0, sizeof proj_slot[slot]);
    if (len) { memcpy(proj_bank_slot[slot], raw, len); memcpy(&proj_slot[slot], raw + 8u, sizeof proj_slot[slot]); }
#endif
    proj_meta[slot].used = len != 0;
    if (len) proj_name_get(proj_meta[slot].name, (const uint8_t *)proj_scratch.name);
    else proj_meta[slot].name[0] = 0;
    return 0;
}

static void project_capture(project_t *p)
{
    uint32_t i;
    uint32_t f = motion_guard();
    memset(p, 0, sizeof *p);
    p->magic = PROJ_MAGIC;
    p->size = sizeof *p;
    for (i = 0; i < G_COUNT; i++)
        p->g[i] = song.g[i];
    p->sel = song.sel;
    p->parts = NPART;
    p->phys = PROJ_PHYS;
    p->chain = chain_config;
    for (i = 0; i < NTRK; i++) {
        for (uint32_t j = 0; j < P_COUNT; j++) p->t[i].p[j] = motion_base_value(&trk[i], j);
        p->t[i].engine = trk[i].eng_req;
        p->t[i].preset = trk[i].preset;
        p->pattern[i] = trk[i].pattern;
        memcpy(p->t[i].step, trk[i].step, sizeof trk[i].step);
        p->cz[i] = cz_patch[i];
        fm6_pack(fm6_patch[i], p->fm6[i]);
        memcpy(p->fm6_fn[i], fm6_fn[i], FM6_NFN);
    }
    p->fm6_fn_ok = 1;
    p->motion = motion;
    for (i = 0; i < RECORD_MAX; i++) p->recording[i] = recording_snapshot(i);
    p->motion.rsv[0] = 1;                     /* full container supplies the bank tags */
    motion_unguard(f);
    memcpy(p->name, proj_name, str_len(proj_name));
    p->sum = proj_sum(p);
}

/* the music as it is now -> slot, named `name` (0: the current name; "" none); 0 saved, nonzero refused or failed.
 * The current name becomes the saved one */
static int project_save_as(uint32_t slot, const char *name)
{
    project_t *p = &proj_scratch;
    if (transport_busy()) {                            /* a flash erase silences the audio and stalls the */
        ui_message("STOP TO SAVE");                     /* sequencer (storage_hw.c): only while stopped */
        return 1;
    }
    project_capture(p);
    if (name) {
        memset(p->name, 0, sizeof p->name);
        memcpy(p->name, name, str_len(name) < PROJ_NAME_LEN ? str_len(name) : PROJ_NAME_LEN);
    }
    proj_wire_gen++;
    if (!bank_pack(proj_wire_u.raw, p, 1)) { ui_message("SAVE FORMAT ERROR"); return 2; }

    if (proj_write_slot(slot, proj_wire_u.raw, BANK_STORE_SIZE)) { ui_message("SAVE ERROR"); return 2; }
    proj_name_get(proj_name, (const uint8_t *)p->name);
    proj_cur = (uint8_t)(slot & 3u);
    ui_message(MELODEE_FLASH ? "SAVED" : "SAVED (RAM)");
    return 0;
}
static int project_save(uint32_t slot) { return project_save_as(slot, 0); }

/* SAVE + REC (ui_input.c, stopped): the music back to the slot it was loaded from or last saved to, at once ("SAVED
 * B"); a new one (the template, the power-on sounds) has none yet: PROJECT opens on a free slot, SAVE picked */
static uint32_t project_free_slot(void);
static void project_quick_save(void)
{
    uint32_t i;
    if (proj_cur < 4u) {
        uint32_t slot = proj_cur;
        if (!project_save(slot)) {
            char b[12] = "SAVED A";
            b[6] = (char)('A' + slot);
            ui_message(b);
        }
        return;
    }
    song.g[G_SLOT] = (int16_t)(project_free_slot() + 1u);
    for (i = 0; i < NPAGES && PAGES[i].graph != GR_SLOTS; i++)
        ;
    ui.home = 0;
    ui.page = (uint8_t)i;
    page_entered();
    ui.act = 4;                                         /* (SAVE: OCT+ names and writes it) */
    ui.force = 1;
    ui_message("NEW PROJECT: PICK SLOT");
}
static void project_cur_name(char *b) { str_cpy(b, proj_name, PROJ_NAME_LEN + 1u); }   /* b: 13 bytes */

/* slot's name -> b (PROJ_NAME_LEN + 1 bytes); 0 = an empty slot (b ""). Uses proj_scratch */
static int project_name(uint32_t slot, char *b)
{
    b[0] = 0;
    if (!project_used(slot)) return 0;
    str_cpy(b, proj_meta[slot & 3u].name, PROJ_NAME_LEN + 1u);
    return 1;
}

/* a stored project renamed in place (nothing else of it changes; the music playing is not touched, but the slot
 * it was loaded from / saved to: its name is the new one, the next SAVE's prefill): 0 done, 1 refused (playing,
 * an empty slot), 2 failed (the slot as it was) */
static int project_rename(uint32_t slot, const char *name)
{
    project_t *p = &proj_scratch;
    if (transport_busy()) {
        ui_message("STOP TO SAVE");
        return 1;
    }
    int n = proj_read_slot(slot);
    if (!n) { ui_message("EMPTY SLOT"); return 1; }
    memset(p->name, 0, sizeof p->name);
    memcpy(p->name, name, str_len(name) < PROJ_NAME_LEN ? str_len(name) : PROJ_NAME_LEN);
    p->sum = proj_sum(p);
    if (n == BANK_STORE_SIZE) {
        memcpy(proj_wire_u.raw + BANK_MOTION_OFF, bank_import_tags, sizeof bank_import_tags);
        if (!proj_pack((project_store_t *)(proj_wire_u.raw + 8u), p)) return 2;
        bank_checksum(proj_wire_u.raw);
    } else {
        proj_bound(p); chain_defaults(&p->chain);
        if (!bank_pack(proj_wire_u.raw, p, 0)) return 2;
    }
    if (proj_write_slot(slot, proj_wire_u.raw, BANK_STORE_SIZE)) { ui_message("SAVE ERROR"); return 2; }
    if (proj_cur == (slot & 3u))
        proj_name_get(proj_name, (const uint8_t *)p->name);
#if MELODEE_FLASH
    if (flash_ok) { ui_message("RENAMED"); return 0; }
#endif
    ui_message("RENAMED (RAM)");
    return 0;
}

static int project_restore_runtime(const project_t *input)
{
    project_t *p = &proj_scratch;
    uint32_t i, k;
    if (!proj_ok(input)) return 1;
    if (p != input) memcpy(p, input, sizeof *p);
    proj_drums_to_part(p);                              /* a RAM slot of firmware before 1.0 */
    proj_phys(p);                                       /* .. before PHYS lost DUST and DRUM */
    proj_fm4(p);                                        /* .. that had DIGITAL tracks */
    transport_req = 2;
    panic_req = (1u << NTRK) - 1u;
    fm1_irq_off();                                      /* the audio ISR must not see half a project */
    seq_stop();
    transport_req = 0;
    pattern_init();
    memcpy(recording, p->recording, sizeof recording);
    recording_reindex();
    chain_defaults(&chain_config);
    motion = p->motion;
    memset(motion_active, 0, sizeof motion_active);
    motion_base_valid = 0;
    ui.song_row = 0;
    for (i = 0; i < G_COUNT; i++)
        if (i != G_SLOT && i != G_LOAD && i != G_SAVE)
            song.g[i] = (int16_t)clamp(p->g[i], GP[i].min, GP[i].max);
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        const proj_trk_t *s = &p->t[k];
        uint32_t e = s->engine % NENGINES;
        t->eng_req = (uint8_t)e;
        t->user = 0; t->user_native=0;                                    /* (no user preset slot is saved) */
        for (i = 0; i < P_COUNT; i++) {                 /* every value back inside its range */
            const param_desc_t *d = param_desc_of(e, i);
            t->p[i] = (int16_t)clamp(s->p[i], d->min, d->max);
        }
        t->preset = (uint8_t)(ENGINES[e]->npresets ? (s->preset >= PROJ_DEF_KEEP ? 0u : s->preset) % ENGINES[e]->npresets : 0u);
        t->pattern = p->pattern[k];
        memcpy(t->step, s->step, sizeof t->step);
        proj_steps(t->step);
        pattern_commit(t);
        {   /* the project's own FM6 patch; PTCH as it was saved, without loading its slot (fm6_poll) */
            uint8_t v[FP_SIZE + 1u];
            fm6_unpack(p->fm6[k], v);
            cz_patch[k] = p->cz[k];
            cz_track_accept(t);
            fm6_set_patch(k, v);
            fm6_adopt(k);
            memcpy(fm6_fn[k], p->fm6_fn_ok && fm6_fn_ok(p->fm6_fn[k]) ? p->fm6_fn[k] : FM6_FNDEF, FM6_NFN);
        }
    }
    song.sel = (uint8_t)(p->sel < NTRK ? p->sel : 0u);
    fm1_irq_on();
    proj_name_get(proj_name, (const uint8_t *)p->name);
    proj_cur = PROJ_NO_SLOT;                            /* (project_load: its slot) */
    undo.trk = 0;                                       /* (ui.c) the undo copy belongs to the old project */
    undo_depth++;                                       /* and these loads take none */
    for (k = 0; k < NTRK; k++) {                        /* the power-on sounds: format 1 (tracks 2..4), old drums */
        track_t *t = &trk[k];
        int16_t keep[P_COUNT];
        if (p->t[k].preset == PROJ_DEF_SOUND) {
            apply_preset_to(t, TRK_DEF[k][1]);
            track_defaults_steps(t);
        } else if (p->t[k].preset == PROJ_DEF_KEEP) {   /* the steps, LEVEL PAN MUTE, LEN DIV SWING GATE kept */
            memcpy(keep, t->p, sizeof keep);
            fm1_irq_off();                           /* publish the legacy sound as one bounded parameter batch */
            proj_legacy_perc(t);                      /* (keeps the SLICER: param_kept) */
            t->p[P_REV] = keep[P_REV];                  /* and the drums' reverb send */
            for (i = P_AMODE; i <= P_TRANS; i++)        /* the drum part had no arp or scale */
                t->p[i] = TP[i].def;
            fm1_irq_on();
        }
        pat_sig[k] = ~steps_sig(t);                     /* a project's steps are the user's */
    }
    scale_share(TSEL);                                  /* (older projects: one scale per part, the selected wins) */
    undo_depth--;
    sync_reload = 1;
    ui.force = 1;
    ui_message("LOADED");
    return 0;
}
static void project_load(uint32_t slot)
{
    int n = proj_read_slot(slot);
    if (!n) { ui_message("EMPTY SLOT"); return; }
    if (!project_restore_runtime(&proj_scratch)) {
        if (n == BANK_STORE_SIZE) bank_restore(proj_wire_u.raw);
        proj_cur = (uint8_t)(slot & 3u);
    }
}

/* settings + learned panel table: one flash object. The flash copy wins at
 * boot (the .noinit copies are garbage after a power-off). */
#include "settings_persist.c"

/* The template (SAVE > PROJECT, SLOT TMPL): the music without its patterns: the globals, each track's sound
 * (engine, preset, parameters, FM6 patch and function settings) and the selected track. SAVE there keeps it, LOAD makes a new project
 * from it (every pattern empty, LEN / DIV / SWG / GATE their defaults, no name, SLOT on a free slot), and power-on
 * loads it when BOOT is OFF or its slot empty. No flash sector is free: it follows the settings in their record
 * (set_rec, its size and magic last). P_COUNT, G_COUNT or FM6_PACKED changing changes it (the assert): convert. */
#define TMPL_MAGIC6 0x364C5054u
#define TMPL_SIZE6 (TMPL_SIZE5 + NTRK * FM6_NFN)
#define TMPL_CZ_OLD (TMPL_SIZE6+NTRK*163u)
#define TMPL_CZ_NEXT (TMPL_SIZE6+NTRK*165u)
#define TMPL_MAGIC_A 0x414C5054u
#define TMPL_SIZE_A (TMPL_SIZE6 + PROJ_CZ_BYTES)
#define TMPL_MAGIC 0x424C5054u                    /* "TPLB" (Melodee's before 1.0 had "TMP1" / "TMP2": not read) */
#define TMPL_MAGIC5 0x354C5054u                   /* "TPL5": without the function settings (tmpl_take) */
#define TMPL_SIZE5 1320u
typedef struct {
    int16_t g[G_COUNT];
    uint8_t sel, rsv;                             /* (G_COUNT odd: the tracks start on a word) */
    struct { uint8_t engine, preset; int16_t p[P_COUNT]; } t[NTRK];
    uint8_t fm6[NTRK][FM6_PACKED];
    uint8_t fm6_fn[NTRK][FM6_NFN];                /* (TPL6) */
    cz_patch_t cz[NTRK];
    uint32_t size, magic;                         /* last: the record's end */
} tmpl_t;
_Static_assert(sizeof(tmpl_t) == 2u * G_COUNT + 2u + NTRK * (2u + 2u * P_COUNT) + NTRK * (FM6_PACKED + FM6_NFN) + PROJ_CZ_BYTES + 8u &&
               sizeof(tmpl_t) == TMPL_SIZE_A,
               "template layout (P_COUNT, G_COUNT, FM6_PACKED: a conversion)");
static tmpl_t tmpl; /* balance the new recording state across RAM and POOL */
static struct { persist_t p; tmpl_t t; } set_rec __attribute__((section(".pool")));   /* the settings record */
_Static_assert(sizeof set_rec == sizeof(persist_t) + sizeof(tmpl_t), "the template follows the settings");
static uint8_t tmpl_dirty;                        /* saved in RAM, not yet in flash (settings_poll) */

static int template_used(void) { return tmpl.magic == TMPL_MAGIC && tmpl.size == sizeof tmpl; }

/* a stored template of len bytes -> tmpl: TPL6, or TPL5 (the function settings Dexed's); 0 = none (tmpl cleared) */
/* Validate native/older tone tails before an archive replaces settings in flash. */
static int tmpl_blob_valid(const uint8_t *b, uint32_t len)
{
    uint32_t size, magic, n, off;
    if (len < 8u) return 0;
    memcpy(&size, b + len - 8u, 4); memcpy(&magic, b + len - 4u, 4);
    if (size != len) return 0;
    if ((len == TMPL_SIZE5 && magic == TMPL_MAGIC5) || (len == TMPL_SIZE6 && magic == TMPL_MAGIC6)) return 1;
    off = TMPL_SIZE6 - 8u;
    if (len == sizeof tmpl && magic == TMPL_MAGIC) { n = CZ_BYTES; }
    else if (len == TMPL_SIZE_A && (magic == TMPL_MAGIC_A || magic == 0x384C5054u)) n = CZ_BYTES;
    else if (len == TMPL_CZ_OLD && magic == 0x384C5054u) n = 163u;
    else if (len == TMPL_CZ_NEXT && magic == 0x394C5054u) n = 165u;
    else return 0;
    for (uint32_t k = 0; k < NTRK; k++) {
        uint8_t tone[CZ_BYTES]; const uint8_t *p = b + off + k * n;
        if (n == CZ_BYTES ? !cz_patch_valid(p) : !cz_legacy_tone(tone, p, n)) return 0;
    }
    return 1;
}
static int tmpl_take(const uint8_t *b, uint32_t len)
{
    memset(&tmpl, 0, sizeof tmpl);
    if (!tmpl_blob_valid(b, len)) return 0;
    uint32_t np = len == sizeof tmpl ? P_COUNT : 92u, pos = 2u * G_COUNT + 2u;
    memcpy(tmpl.g, b, sizeof tmpl.g); tmpl.sel = b[2u * G_COUNT];
    for (uint32_t k = 0; k < NTRK; k++) {
        int16_t values[P_COUNT], def[P_COUNT];
        tmpl.t[k].engine = b[pos++]; tmpl.t[k].preset = b[pos++];
        memcpy(values, b + pos, np * 2u); pos += np * 2u;
        for (uint32_t j = 0; j < P_COUNT; j++) def[j] = param_desc_of(tmpl.t[k].engine % NENGINES, j)->def;
        params_by_count(tmpl.t[k].p, values, np, def);
        uint32_t magic; memcpy(&magic, b + len - 4u, 4);
        if (magic != TMPL_MAGIC) tmpl.t[k].p[P_RECQ] = 0;
    }
    memcpy(tmpl.fm6, b + pos, sizeof tmpl.fm6); pos += sizeof tmpl.fm6;
    if (len != TMPL_SIZE5) { memcpy(tmpl.fm6_fn, b + pos, sizeof tmpl.fm6_fn); pos += sizeof tmpl.fm6_fn; }
    uint32_t n = len == TMPL_CZ_OLD ? 163u : len == TMPL_CZ_NEXT ? 165u : CZ_BYTES;
    for (uint32_t k = 0; k < NTRK; k++) {
        if (len == TMPL_SIZE5 || !fm6_fn_ok(tmpl.fm6_fn[k])) memcpy(tmpl.fm6_fn[k], FM6_FNDEF, FM6_NFN);
        if (len == TMPL_SIZE5 || len == TMPL_SIZE6) cz_patch_init(tmpl.cz[k].raw);
        else if (n == CZ_BYTES) memcpy(tmpl.cz[k].raw, b + pos + k * n, CZ_BYTES);
        else {
            cz_legacy_tone(tmpl.cz[k].raw, b + pos + k * n, n);
            if (tmpl.t[k].engine == 14u) {
                tmpl.t[k].engine = ENGI_CZ; tmpl.t[k].preset = 0;
                for (uint32_t j = P_E0; j < P_COUNT; j++) tmpl.t[k].p[j] = param_desc_of(ENGI_CZ,j)->def;
                tmpl.t[k].p[P_E7] = CZ_NATIVE;
            }
        }
    }
    tmpl.magic = TMPL_MAGIC; tmpl.size = sizeof tmpl;
    return 1;
}

/* CLK TUNE MIDI ROUT: the device keeps them as last used (settings ext.glo), not only the projects */
static const uint8_t GLO_KEPT[4] = {G_CLOCK, G_TUNE, G_MIDI, G_ROUTE};
static void glo_restore(void)                     /* power-on, before the BOOT project or the template (theirs win) */
{
    uint32_t k;
    for (k = 0; k < 4u; k++)
        song.g[GLO_KEPT[k]] = (int16_t)clamp(settings_glo[k], GP[GLO_KEPT[k]].min, GP[GLO_KEPT[k]].max);
}
/* main loop: a kept value changed (a knob, the editor, a project load): saved 1.5 s after the last change, stopped */
static void glo_poll(void)
{
    static uint32_t t;
    static uint8_t dirty;
    uint32_t k;
    for (k = 0; k < 4u; k++)
        if (song.g[GLO_KEPT[k]] != settings_glo[k]) {
            settings_glo[k] = song.g[GLO_KEPT[k]];
            dirty = 1;
            t = fm1_ms;
        }
    if (dirty && fm1_ms - t > 1500u && !song.playing) {
        dirty = 0;
        settings_save();
    }
}

/* the music without its patterns -> the template, saved with the settings */
static void template_save(void)
{
    uint32_t i, k;
    if (transport_busy()) {
        ui_message("STOP TO SAVE");
        return;
    }
    memset(&tmpl, 0, sizeof tmpl);
    for (i = 0; i < G_COUNT; i++)
        tmpl.g[i] = song.g[i];
    tmpl.sel = song.sel;
    for (k = 0; k < NTRK; k++) {
        tmpl.t[k].engine = trk[k].eng_req;
        tmpl.t[k].preset = trk[k].preset;
        for (i = 0; i < P_COUNT; i++)
            tmpl.t[k].p[i] = motion_base_value(&trk[k], i);
        tmpl.cz[k] = cz_patch[k];
        fm6_pack(fm6_patch[k], tmpl.fm6[k]);
        memcpy(tmpl.fm6_fn[k], fm6_fn[k], FM6_NFN);
    }
    tmpl.size = sizeof tmpl;
    tmpl.magic = TMPL_MAGIC;
    tmpl_dirty = 1;
#if MELODEE_FLASH
    if (flash_ok) {
        settings_save();                              /* (stopped: written now) */
        ui_message(tmpl_dirty ? "SAVE ERROR" : "TEMPLATE SAVED");
        return;
    }
#endif
    ui_message("TEMPLATE SAVED (RAM)");
}

/* the first empty project slot (0..3), else 0 */
static uint32_t project_free_slot(void)
{
    uint32_t i;
    for (i = 0; i < 4u; i++)
        if (!project_used(i))
            return i;
    return 0;
}

/* a new project from the template: its sounds and globals, every pattern empty; SLOT on a free slot */
static void template_load(void)
{
    project_t *p = &proj_scratch;
    uint32_t i, k;
    if (!template_used()) {
        ui_message("NO TEMPLATE");
        return;
    }
    memset(p, 0, sizeof *p);
    p->magic = PROJ_MAGIC;
    p->size = sizeof *p;
    for (i = 0; i < G_COUNT; i++)
        p->g[i] = tmpl.g[i];
    p->sel = tmpl.sel;
    p->parts = NPART;
    p->phys = PROJ_PHYS;
    chain_defaults(&p->chain);
    for (k = 0; k < NTRK; k++) {
        p->t[k].engine = tmpl.t[k].engine;
        p->t[k].preset = tmpl.t[k].preset;
        for (i = 0; i < P_COUNT; i++)
            p->t[k].p[i] = tmpl.t[k].p[i];
        for (i = 0; i < 4u; i++)                      /* LEN DIV SWG GATE: as a new track's */
            p->t[k].p[P_SLEN + i] = TP[P_SLEN + i].def;
        p->cz[k] = tmpl.cz[k];
        memcpy(p->fm6[k], tmpl.fm6[k], FM6_PACKED);
        memcpy(p->fm6_fn[k], tmpl.fm6_fn[k], FM6_NFN);
    }
    p->fm6_fn_ok = 1;
    p->sum = proj_sum(p);
    if (project_restore_runtime(p))
        return;
    for (k = 0; k < NTRK; k++) {
        track_defaults_steps(&trk[k]);                /* every pattern empty */
        pat_last[k] = 0;
        pat_sig[k] = steps_sig(&trk[k]);
    }
    proj_cur = PROJ_NO_SLOT;
    song.g[G_SLOT] = (int16_t)(project_free_slot() + 1u);
    ui_message("TEMPLATE LOADED");
}

/* power-on, after melodee_init: the BOOT project (SAVE > PROJECT KNOB 2) in place of the default sounds, SLOT on it
 * so SAVE writes back there. BOOT OFF or an empty BOOT slot: a new project, from the template if one is saved, SLOT
 * on a free slot. A boot that crashed or hung within 30 s (bootguard) skips the project or the template: one that
 * cannot load does not lock the FM-1 out */
static void project_boot(void)
{
    if (settings_boot && project_used(settings_boot - 1u)) {
        song.g[G_SLOT] = (int16_t)settings_boot;
        if (bootguard.failed)
            ui_message("BOOT PROJECT SKIPPED");
        else
            project_load(settings_boot - 1u);
        return;
    }
    if (template_used()) {
        if (bootguard.failed)
            ui_message("TEMPLATE SKIPPED");
        else
            template_load();
    }
    song.g[G_SLOT] = (int16_t)(project_free_slot() + 1u);
}
#if MELODEE_FLASH && MELODEE_SLICE
#include "slice_store.c"                          /* SLICE's MAN slices, kept in the user slots */
#endif
#if MELODEE_FLASH
static persist_t persist_saved;
static uint8_t persist_pending;                 /* 1 requested, 2 waiting after a flash error */
static uint32_t persist_retry_ms;
#endif

static void persist_boot(void)                    /* before settings_init / panel_init */
{
#if MELODEE_FLASH
    persist_t p;
    uint32_t f = irq_save();
    flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;       /* the expected 1 MiB part, else stay RAM-only */
    irq_restore(f);
    if (!flash_ok)
        return;
    fl_plain_window_init();                        /* flash above 0x93000 reads as plaintext through XIP
                                                    * (user sample sets are played from there) */
#if MELODEE_SLICE
    slc_store_boot();                              /* (the scans read each slot's stored slices) */
#endif
    {   /* the settings, then a template if one follows them */
        int n = st_load(OBJ_SETTINGS, &proj_wire_u, sizeof proj_wire_u), ns = n;
        if (n > (int)sizeof set_rec.p &&
            tmpl_take((const uint8_t *)&proj_wire_u + sizeof set_rec.p, (uint32_t)n - sizeof set_rec.p))
            ns = (int)sizeof set_rec.p;               /* (a TPL5 one: its next save writes TPL6) */
        memset(&p, 0, sizeof p);
        if (ns > 0)
            memcpy(&p, &proj_wire_u, (uint32_t)ns < sizeof p ? (uint32_t)ns : sizeof p);
        if (settings_import(&p, ns))
            persist_saved = p;
    }
    {   /* projects: fill empty RAM slots from flash, so the slot list is right after power-on */
        uint32_t i;
        for (i = 0; i < 4u; i++)
            if (!project_used(i))
                proj_fetch(i);
    }
    up_boot();                                     /* user presets */
#endif
}

static int project_used(uint32_t slot)
{
#if !MELODEE_FLASH
    slot &= 3u;
    int valid = bank_valid(proj_bank_slot[slot], BANK_STORE_SIZE) &&
        !memcmp(proj_bank_slot[slot] + 8u, &proj_slot[slot], sizeof proj_slot[slot]);
    if (!valid) valid = proj_import(&proj_scratch, &proj_slot[slot], sizeof proj_slot[slot]);
    proj_meta[slot].used = (uint8_t)valid;
    if (valid) proj_name_get(proj_meta[slot].name, (const uint8_t *)proj_scratch.name);
#endif
    return proj_meta[slot & 3u].used;
}

/* Main loop only: no flash access or copies when the ISR changes rows. */
static uint32_t chain_prepare(void)
{
    if (transport_busy()) return 2;
    if (!chain_valid(&chain_config) || !chain_config.count) return 1;
    for (uint32_t i = 0; i < chain_config.count; i++)
        for (uint32_t k = 0; k < NTRK; k++) if (chain_patterns[i][k] >= NPAT) return 1;
    uint32_t f = motion_guard();
    if (transport_busy()) { motion_unguard(f); return 2; }
    for (uint32_t k = 0; k < NTRK; k++) { pattern_commit(&trk[k]); trk[k].pattern_next = 0xff; }
    chain.config = chain_config;
    RING_PUBLISH(); chain.armed = 1; transport_req = 1;
    motion_unguard(f);
    return 0;
}

static void settings_poll(void)
{
#if MELODEE_FLASH
    persist_t p;
#if MELODEE_SLICE
    slc_store_poll();                              /* SLICE's slices edited on the SLICES page */
#endif
    if (!persist_pending || !flash_ok || transport_busy() ||
        (persist_pending == 2u && (uint32_t)(fm1_ms - persist_retry_ms) < 1000u))
        return;
    p = persist_saved;
    settings_export(&p);
    if (!memcmp(&p, &persist_saved, sizeof p) && !tmpl_dirty) {
        persist_pending = 0;
        return;                                    /* unchanged: no erase cycle */
    }
    set_rec.p = p;                                 /* the record: the settings, the template after them */
    set_rec.t = tmpl;
    if (st_save(OBJ_SETTINGS, &set_rec, sizeof set_rec.p + (template_used() ? sizeof set_rec.t : 0u)) == 0) {
        persist_saved = p;
        persist_pending = 0;
        tmpl_dirty = 0;
    } else {
        persist_pending = 2;
        persist_retry_ms = fm1_ms;
    }
#endif
}

static void settings_save(void)
{
#if MELODEE_FLASH
    persist_pending = 1;
#endif
    settings_poll();
}

#if MELODEE_FLASH
_Static_assert(PROJ_STORE_V8 <= ST_PAYLOAD_MAX, "legacy project sector");
_Static_assert(sizeof(persist_t) <= ST_PAYLOAD_MAX, "settings do not fit one flash sector");
#endif
#endif /* PROJ_HOST */
