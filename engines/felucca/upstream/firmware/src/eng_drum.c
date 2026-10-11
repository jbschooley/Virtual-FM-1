/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* DRUM: a drum kit of 8 lanes (drum_voice.c, Felucca's own): KICK, SNARE, CLAP, HAT CL, HAT OP, TOM, RIM,
 * BELL. Before 1.0 it was PHYS's MODEL DRUM; projects and user presets saved then load as this engine
 * (drum_from_phys in core.h).
 *
 * Keys and notes: the keys play the General MIDI drum map with the kick on the first C (C KICK, C# RIM,
 * D SNARE, D# CLAP, F# HAT CL, A# HAT OP, F G A B C D TOMS, C# CYM, G# BELL; OCT -/+ move it), and a note
 * plays its GM drum (35..81: kicks, snares, toms, hats, cymbals, bells, congas, claves; other notes as
 * their octave of 36..47), so GM patterns and MIDI parts play as they did on SAMPLE PERC (retired: its sounds
 * load as this engine, core.h drum_from_perc).
 *
 * Parameters: KIT the kit: STD (Felucca's own: TOM RIM BELL on lanes 6..8, other GM notes their congas, claves,
 * cymbals) or a model kit (80 10 66 55 77: every lane that kit's own piece, drum_voice.c DV_KIT; KICK does not
 * apply). Its stored values 1..3 were HAND CYM H+CYM (STD with CONGA CLAVE / CYM on lanes 6..8) until 1.0.4: they
 * play 66 10 77 now (DK_PLAYS), and whatever stores them lands there (params.c enum_orig, param_fit: the knob
 * steps over them, the editor's SET, projects, user presets, motion). KICK the kick (PUNCH, ROUND). TUNE (64: as
 * designed, +-12 semitones), TONE, DECY and SNAP (each lane's extra: the kick's drive, the snare's snappiness,
 * the clap's spread, the hats' noise, the toms' bend, the rim's drive, the bell's strike) move every lane from its designed value. ACC
 * is the accent at full velocity (velocity scales it), DRV a soft clip on every hit (x1..x4, level kept).
 * Each lane has its LEVEL (P_LN0..P_LN7, EDIT > LANES / LANES 2, #97: square law, 100 % the kit as designed, the
 * default, so every older sound plays as it did), on the voice amplitude after the knee (a quieter lane is cleaner).
 * A hit keeps the drum and the variant it was struck with; the knobs move it while it rings.
 *
 * Polyphony: a lane per drum and part, mono: a hit reuses its lane's voice even at another pitch, the 8 lanes ring
 * together. A sounding voice plays one lane (s[0]); the lane remembers its voice (owner), and a voice
 * whose lane was struck again by another voice, or has rung out (-84 dB), ends itself (drum_amp). The
 * voices come from the shared budget (voice.c), up to 8 per part. A closed hat chokes the open one.
 *
 * What applies (engine_t.oneshot): the hit is the envelope. VOICE (always POLY), GLIDE, the ADSR, ENV DEST,
 * LFO DEST PIT / FLT / SHP, the global TUNE and the matrix's PITCH CUT SHP do nothing; LEVEL, PAN, the
 * sends, DIST, the SLICER, LFO DEST AMP, velocity, the matrix's AMP and its E1..E8 (the knobs, per
 * block) do. A released key does not end a hit.
 *
 * State: 8 lanes per part in the part's engine memory (engines.c eng_mem; drum_kit_of: the parameters, coefficients, voice and metal
 * source of each lane). */
#include "drum_voice.c"

enum { DK_STD, DK_HAND, DK_CYM, DK_HCYM, DK_80, DK_10, DK_66, DK_55, DK_77, DK_COUNT };   /* (stored values) */
/* the kit a stored KIT value plays: HAND CYM H+CYM (retired after 1.0.4) -> 66 (a conga on TOM), 10 (a cymbal on
 * BELL), 77 (claves on RIM, a cymbal on BELL) */
static const uint8_t DK_PLAYS[DK_COUNT] = {DK_STD, DK_66, DK_10, DK_77, DK_80, DK_10, DK_66, DK_55, DK_77};
static uint32_t drum_kit_plays(int32_t v) { return DK_PLAYS[clamp(v, 0, DK_COUNT - 1)]; }
/* the model kits' pieces where a lane plays another than its own (the lane's name): 1 TOM -> CONGA, 2 RIM -> CLAVE,
 * 4 BELL -> CYM */
static const uint8_t DK_SWAP[DV_NKIT] = {0, 4, 1, 0, 2 | 4};
static const uint8_t DV_TYPE_LANE[DVT_COUNT] = {
    DV_KICK, DV_KICK, DV_SNARE, DV_CLAP, DV_HATC, DV_HATO, DV_TOM, DV_TOM, DV_RIM, DV_RIM, DV_BELL, DV_BELL,
};

typedef struct {
    dv_param_t key;              /* the parameters the coefficients were set up with */
    dv_coef_t c;
    dv_voice_t v;
    dv_metal_t mb;
    uint8_t owner;               /* the voice playing the lane: index + 1, 0 = none */
    uint8_t role;                /* the drum struck (DVT_*) */
    int8_t st;                   /* its semitones from the designed pitch (the GM map) */
    uint8_t pad;
} drum_lane_t;

ENG_MEM_FITS("DRUM", sizeof(drum_lane_t) * DV_NLANE);
/* 1.4.1: each part's lanes muted (bit l: lane l), the EDIT layer's black keys 1..8 (ui_layer.c EQA_DRUM): a channel
 * mute. The lane plays on as always (hits, keys, MIDI: its voice, the CPU, the choke) and drum_render drops its output,
 * so unmuted it sounds as if it never was. Performance state: not saved, not recorded, no undo; a sound load into the
 * track (ui.c load_begin) and a project load (project_restore_runtime) clear it, power-off too */
static uint8_t drum_mute[NPART];

/* 1..3 named as the kit they play: aliases, never shown or offered (EDITOR_PROTOCOL.md: retired values) */
static const char *const N_DRUM_KIT[] = {"STD", "66", "10", "77", "80", "10", "66", "55", "77"};
static const char *const N_DRUM_KICK[] = {"PUNCH", "ROUND"};

/* General MIDI notes 35..81 -> the drum (DVT_*; DVT_PUNCH: the kick KICK picks) and semitones from its
 * designed pitch */
static const int8_t DRUM_GM[47][2] = {
    {DVT_PUNCH, -2}, {DVT_PUNCH, 0}, {DVT_RIM, 0}, {DVT_SNARE, 0}, {DVT_CLAP, 0}, {DVT_SNARE, 2},     /* 35 */
    {DVT_TOM, -7}, {DVT_HATC, 0}, {DVT_TOM, -4}, {DVT_HATC, -2}, {DVT_TOM, 0}, {DVT_HATO, 0},         /* 41 */
    {DVT_TOM, 3}, {DVT_TOM, 5}, {DVT_CYM, 0}, {DVT_TOM, 8}, {DVT_CYM, -3}, {DVT_CYM, 2},              /* 47 */
    {DVT_BELL, 5}, {DVT_HATC, 5}, {DVT_CYM, 4}, {DVT_BELL, 0}, {DVT_CYM, 1}, {DVT_CLAVE, -12},        /* 53 */
    {DVT_CYM, -2}, {DVT_CONGA, 5}, {DVT_CONGA, 2}, {DVT_CONGA, 0}, {DVT_CONGA, 0}, {DVT_CONGA, -5},   /* 59 */
    {DVT_TOM, 10}, {DVT_TOM, 7}, {DVT_BELL, 7}, {DVT_BELL, 3}, {DVT_HATC, 3}, {DVT_HATC, 7},          /* 65 */
    {DVT_CLAVE, 7}, {DVT_CLAVE, 5}, {DVT_HATC, -4}, {DVT_HATC, -6}, {DVT_CLAVE, 0}, {DVT_CLAVE, -4},  /* 71 */
    {DVT_CLAVE, -7}, {DVT_CONGA, 7}, {DVT_CONGA, 3}, {DVT_BELL, 12}, {DVT_BELL, 12},                  /* 77 */
};
/* the drum of a note (DVT_*, or a model kit's lane: DV_KTYPE) and its semitones; KICK picks the kick; a model kit
 * plays its own piece on the note's lane */
static uint32_t drum_gm(const int16_t *p, uint32_t note, int32_t *st)
{
    uint32_t n = note >= 35u && note <= 81u ? note : 36u + (note + 120u - 36u) % 12u, t = (uint32_t)DRUM_GM[n - 35u][0];
    uint32_t kit = drum_kit_plays(p[P_E0]);           /* STD, the model kits */
    *st = DRUM_GM[n - 35u][1];
    if (kit >= DK_80)
        return DV_KTYPE(kit - DK_80 + 1u, DV_TYPE_LANE[t]);
    return t == DVT_PUNCH && p[P_E6] > 0 ? DVT_ROUND : t;
}

/* ----------------------------------------------------- the grid's lanes --- */
/* A step's lane hits (step_t.hit / acc, the DRUM grid: SEQ > STEP on a DRUM track) play these GM notes, on any
 * engine: DRUM strikes its lanes, a synth plays the pitches. Each is the designed pitch of
 * its lane (st 0 in DRUM_GM) */
static const uint8_t DRUM_LANE_NOTE[NLANE] = {36, 38, 39, 42, 46, 45, 37, 56};

/* the lane a note strikes (KIT-proof: a kit plays its own piece inside the lane) */
static uint32_t drum_lane(uint32_t note)
{
    uint32_t n = note >= 35u && note <= 81u ? note : 36u + (note + 120u - 36u) % 12u;
    return DV_TYPE_LANE[DRUM_GM[n - 35u][0]];
}

/* KIT's swaps of lanes 6..8 (1 TOM -> CONGA, 2 RIM -> CLAVE, 4 BELL -> CYM) */
static uint32_t drum_swaps(const track_t *t)
{
    uint32_t kit = drum_kit_plays(t->p[P_E0]);
    return kit >= DK_80 ? DK_SWAP[kit - DK_80] : 0u;
}

/* the lane's name as the track's KIT plays it (5 characters at most) */
static const char *drum_lane_name(const track_t *t, uint32_t l)
{
    static const char *const N[NLANE] = {"KICK", "SNARE", "CLAP", "HATCL", "HATOP", "TOM", "RIM", "BELL"};
    uint32_t kit = drum_swaps(t);
    l &= NLANE - 1u;
    if (l == DV_TOM && (kit & 1u))
        return "CONGA";
    if (l == DV_RIM && (kit & 2u))
        return "CLAVE";
    return (kit & 4u) && l == DV_BELL ? "CYM" : N[l];
}
/* .. in two letters, the drum machine way (the grid's lane column) */
static const char *drum_lane_abbr(const track_t *t, uint32_t l)
{
    static const char *const N[NLANE] = {"BD", "SD", "CP", "CH", "OH", "TM", "RS", "CB"};
    uint32_t kit = drum_swaps(t);
    l &= NLANE - 1u;
    if (l == DV_TOM && (kit & 1u))
        return "CG";
    if (l == DV_RIM && (kit & 2u))
        return "CL";
    return (kit & 4u) && l == DV_BELL ? "CY" : N[l];
}

/* the lanes step s strikes: its hits, and its notes on their lanes (a NOTE step only) */
static uint32_t step_lanes(const step_t *s)
{
    uint32_t m = 0, i;
    if (s->time != ST_NOTE)
        return 0;
    for (i = 0; i < s->n && i < 4u; i++)
        m |= 1u << drum_lane(s->note[i]);
    return m | s->hit;
}

/* .. and which of them are accented */
static uint32_t step_accents(const step_t *s) { return s->flags & SF_ACCENT ? step_lanes(s) : s->acc & step_lanes(s); }

/* a step's notes that are a lane's note become that lane's hits (the same note, the same velocity: nothing
 * sounds different). Other notes (a low tom 41, a crash 49) stay notes, shown on their lane. A step accent
 * of a step left with hits only becomes the hits' accents. Pattern loads into a DRUM track, projects of
 * before the grid, the grid's edits */
static void step_to_grid(step_t *s)
{
    uint32_t i, k = 0;
    if (s->time != ST_NOTE)
        return;
    for (i = 0; i < s->n && i < 4u; i++) {
        uint32_t l = drum_lane(s->note[i]);
        if (s->note[i] == DRUM_LANE_NOTE[l])
            s->hit |= (uint8_t)(1u << l);
        else
            s->note[k++] = s->note[i];
    }
    for (i = k; i < 4u; i++)
        s->note[i] = 0;
    s->n = (uint8_t)k;
    if (!k && (s->flags & SF_ACCENT)) {
        s->acc |= s->hit;
        s->flags &= (uint8_t)~SF_ACCENT;
    }
}

static drum_lane_t *drum_kit_of(const track_t *t)
{
    return t >= &trk[0] && t < &trk[NPART] ? (drum_lane_t *)ENG_MEM(t - trk) : 0;
}

/* the lane voice v plays, 0 when it plays none (any more) */
static drum_lane_t *drum_lane_of(track_t *t, const voice_t *v)
{
    drum_lane_t *K = drum_kit_of(t), *L;
    uint32_t i = (uint32_t)(v - t->v);
    if (!K || i >= NVOICE)
        return 0;
    L = &K[(uint32_t)v->s[0] & (DV_NLANE - 1u)];
    return L->owner == i + 1u ? L : 0;
}

/* Different GM pitches on a lane still share its one sounding voice. */
static voice_t *drum_reuse(track_t *t, uint32_t note)
{
    drum_lane_t *K = drum_kit_of(t);
    uint32_t lane = drum_lane(note), owner = K ? K[lane].owner : 0;
    voice_t *v;
    if (!owner || owner > NVOICE)
        return 0;
    v = &t->v[owner - 1u];
    return v->active && (uint32_t)v->s[0] == lane ? v : 0;
}

static void drum_note_on(track_t *t, voice_t *v)
{
    drum_lane_t *K = drum_kit_of(t), *L;
    uint32_t i = (uint32_t)(v - t->v), role, lane;
    int32_t st;
    if (!K || i >= NVOICE)
        return;
    role = drum_gm(t->p, v->note, &st);
    lane = drum_lane(v->note);
    L = &K[(uint32_t)v->s[0] & (DV_NLANE - 1u)];
    if (L->owner == i + 1u)                              /* this voice played another lane: it stops there */
        L->owner = 0;
    L = &K[lane];
    if (L->role != role || L->v.type != dv_run_type(role)) {   /* another drum on this lane: from rest */
        dv_init(&L->v, role);
        L->key.type = 0xFF;
        L->role = (uint8_t)role;
    }
    L->st = (int8_t)st;
    L->owner = (uint8_t)(i + 1u);                        /* (its last voice, if another, ends: drum_amp) */
    v->s[0] = (int32_t)lane;
    v->env_out = v->vel * 258;                           /* the hit starts at its level (no ramp from 0) */
    v->env = 1 << 24;                                    /* the ADSR held at full from here, not from drum_amp:
                                                          * a key-off before the first block (zero-length MIDI
                                                          * notes) would end a fresh voice at env 0 in env_tick */
    dv_trigger(&L->v);
    if (lane == DV_HATC && K[DV_HATO].v.live)            /* a closed hat chokes the open one */
        dv_choke(&K[DV_HATO].v);
}

/* the voice's amplitude: the hit, not the ADSR (which still runs, held at full so a release never ends
 * it). A voice that plays no lane any more, or whose drum has rung out, ends here */
static int32_t drum_amp(track_t *t, voice_t *v, int32_t adsr)
{
    const drum_lane_t *L = drum_lane_of(t, v);
    (void)adsr;
    if (!v->active)                                      /* (taken for another part: env_tick ended it) */
        return 0;
    if (!L || (!L->v.live && !L->v.trig)) {
        v->active = v->gate = 0;
        v->stage = 0;
        v->env = 0;
        return 0;
    }
    v->env = 1 << 24;
    return 32767;
}

static void drum_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    drum_lane_t *L = drum_lane_of(t, v);
    dv_param_t k;
    int32_t y[CTL], mb[CTL], acc = clamp(p[P_E5] * v->vel * 4, 0, 65536), drv = p[P_E7], g = 0, mk = 0;   /* acc Q16 */
    int32_t lv = clamp(p[P_LN0 + ((uint32_t)v->s[0] & (DV_NLANE - 1u))], 0, 127);
    uint32_t i, r;
    vmod_t ml;                                           /* (only its amplitude ramp is read: voice_amp) */
    if (!L)
        return;
    if (lv < 127) {                                      /* the lane's LEVEL (square law) on the block's amplitude
                                                          * ramp, not per sample; 100 %: the ramp as it was */
        int32_t gl = lv * lv * 2 + (lv * lv >> 6);       /* Q15, 127: 32508 (not used), 64: 8256 */
        ml.amp0 = (m->amp0 >> 4) * gl >> 11;
        ml.amp1 = (m->amp1 >> 4) * gl >> 11;
        m = &ml;
    }
    if (n > CTL)
        n = CTL;
    r = L->role;
    dv_default(&k, r);                                   /* the knobs move every lane from its design (64) */
    k.decay = (uint8_t)clamp(k.decay + p[P_E3] - 64, 0, 127);
    k.tone = (uint8_t)clamp(k.tone + p[P_E2] - 64, 0, 127);
    k.extra = (uint8_t)clamp(k.extra + p[P_E4] - 64, 0, 127);
    k.accent = (uint8_t)clamp(acc >> 9, 0, 127);         /* (Q16 -> 0..127) */
    k.tune = (int16_t)(L->st * 16 + (p[P_E1] - 64) * 3);   /* +-12 semitones */
    if (((const uint32_t *)&k)[0] != ((const uint32_t *)&L->key)[0] ||
        ((const uint32_t *)&k)[1] != ((const uint32_t *)&L->key)[1]) {
        dv_setup(&L->c, &k);                             /* (a parameter moved) */
        L->key = k;
        if (dv_uses_metal(L->c.type))
            dv_metal_tune(&L->mb, &L->c);
    }
    if (dv_uses_metal(L->c.type) && (L->v.live || L->v.trig))
        dv_metal_run(&L->mb, mb, n);
    dv_run(&L->c, &L->v, mb, y, n);
    if ((drum_mute[(uint32_t)(t - trk) % NPART] >> ((uint32_t)v->s[0] & (DV_NLANE - 1u))) & 1u)
        return;                                          /* (a muted lane rings on unheard) */
    if (drv > 0) {                                       /* DRV: x1..x4 into the soft clip, the level kept (Q12) */
        g = 4096 + drv * 3 * 4096 / 127;
        mk = (int32_t)((19661u << 15) / (uint32_t)softclip((19661 * g) >> 12));
    }
    for (i = 0; i < n; i++) {                            /* x1.25 and the knee below, in 32 bits */
        int32_t s = y[i];
        if (g)
            s = (softclip((s * g) >> 12) * mk) >> 15;
        out[i] += voice_amp(soft_knee(s + (s >> 2), 24000), m, i) << 1;
    }
}

/* the GM drum map, the first C (key 7) is the kick (C2, 36) */
static int32_t drum_keys(const track_t *t, uint32_t k)
{
    (void)t;
    return clamp(29 + 12 * song.octave + (int32_t)k, 0, 127);
}

/* {KIT, TUNE, TONE, DECY, SNAP, ACC, KICK, DRV}; every kit suggests the BEAT pattern (GM notes) */
static const preset_t DRUM_PRESETS[] = {
    {"DRUM KIT", DRUM_KIT_E, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 20), PAT(12)},   /* (core.h: SAMPLE PERC's too) */
};

static const engine_t ENG_DRUM = {
    .name = "DRUM",
    .page_title = {"KIT", "HIT"},
    .edit = {
        {"KIT", F_ENUM, 0, DK_COUNT - 1, DK_STD, N_DRUM_KIT, 0},
        {"TUNE", F_PCT, 0, 127, 64, 0, 0},
        {"TONE", F_PCT, 0, 127, 64, 0, 0},
        {"DECY", F_PCT, 0, 127, 64, 0, 0},
        {"SNAP", F_PCT, 0, 127, 64, 0, 0},
        {"ACC", F_PCT, 0, 127, 100, 0, 0},
        {"KICK", F_ENUM, 0, 1, 0, N_DRUM_KICK, 0},
        {"DRV", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = DRUM_PRESETS,
    .npresets = NELEM(DRUM_PRESETS),
    .note_on = drum_note_on,
    .render = drum_render,
    .amp = drum_amp,
    .knob = {P_E1, P_E2, P_E3, P_E4},
    .poly = DV_NLANE,
    .oneshot = 1,
    .keys = drum_keys,
    .mem = sizeof(drum_lane_t) * DV_NLANE,
};
