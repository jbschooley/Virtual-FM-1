/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modifications Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* FM6: six-operator FM that plays DX7 voices the way Dexed plays them. The synthesis is fm6_core.c; this file
 * is the Melodee engine around it.
 *
 * The patch is the sound: every track has one (fm6_patch, the 155-byte single-voice layout: six operators with
 * their 4-rate / 4-level envelopes, keyboard level and rate scaling, velocity, ratio or fixed frequency,
 * detune; 32 algorithms, feedback, LFO, pitch envelope, transpose). It is edited on the device (the FM6 pages),
 * in the web editor (EDITOR_PROTOCOL.md FM6_*) and over USB-MIDI as DX7 SysEx (fm6_store.c), loaded from the
 * PATCH slots and saved inside projects. The FM6 function settings (bend, portamento, the controllers, ENGINE:
 * fm6_core.c fm6_fn) are the track's, as each Dexed instance has its own, saved with the project and the
 * template (project.c), edited on the FM pages and by DX7 function SysEx. On the device the seven EDIT
 * values are macros on top of the patch, neutral at 0 (then the native patch is unmodified):
 *   ALG   PAT = the patch's algorithm, 1..32 another one
 *   FB    added to the patch's feedback (0..7)
 *   MLVL  the output level of every operator that is not a carrier (-36 .. +36 dB): the brightness
 *   MRAT  added to the coarse ratio of the modulators (ratio mode only)
 *   MEG   the modulators' envelope times: + slower (rates down to 40 steps), - faster
 *   VMOD  added to the modulators' velocity sensitivity (0..7)
 *   DTUN  spreads the carriers apart in pitch (up to about +-36 cents between the outer ones)
 * User presets and projects retain the whole voice; the preset browser loads factory voices.
 * The operator envelopes are the voice's amplitude and end it (engine_t.ownenv / done): the track's ADSR, ENV
 * DEST and the matrix's ENV do nothing here. The track's FLT moves MLVL (ENV / LFO -> FLT, the matrix's CUT), SHP
 * the feedback, PIT the pitch (glide, LFO and ENV pitch, unison detune, the matrix's: vmod_t.plog).
 *
 * Voices are chosen and handed over as Dexed does (16 of them, engine_t.alloc / legato / mono_key); a voice that
 * has ended keeps its control path running as Dexed keeps its voices computing (fm6_ghost), so its next note
 * starts where Dexed's would. Their state lives in the part's engine state (engines.c eng_state). */
#include "fm6_core.c"
#include "melodee_fm6.h"         /* tools/gen_fm6_patches.py: FM6_INIT, FM6_FACTORY[FM6_NFACTORY] (F1..F8) */

/* a Melodee factory voice: OP1..OP6 in patch order within an operator (DET 0..14, 7 = none), ALG 0..31,
 * TRNSP 24 = no transposition */
typedef struct {
    uint8_t op[6][FP_OP];
    uint8_t pr[4], pl[4], alg, fb, oks, lfs, lfd, lpmd, lamd, lfks, lfw, lpms, trnsp;
    char name[11];
} fm6_rom_t;
#include "eng_fm6_rom.h"         /* FM6_ROM[FM6_NROM]: F9.. */

#define ENGI_FM6 12u             /* engines.c ENGINES[] (append-only) */
#define FM6_NFAC (FM6_NFACTORY + FM6_NROM)        /* F1..F24: the factory patches */
#define FM6_BANK_N 32u           /* patch bank slots (fm6_bank.c): a DX7 cartridge */
#define FM6_PACKED 128u
#define FM6_ON_ALL 0x3Fu

static uint8_t fm6_patch[NTRK][FP_SIZE + 1u];   /* the tracks' patches (main loop writes, then fm6_pgen) */
static volatile uint8_t fm6_pgen[NTRK];          /* +1 after each write of fm6_patch[t] */
static volatile uint8_t fm6_lgen[NTRK];          /* +1 when the write was a new voice (a load): the notes stop */
static uint8_t fm6_on[NTRK];                     /* the operator switches (bit k: the sixth first), all on at a load */
static struct {                                  /* the patch through the macros: the audio ISR's copy */
    uint8_t p[FP_SIZE + 1u];
    uint8_t gen, ok;
    int16_t e[7];                                /* P_E0..P_E6 it was made with */
    int32_t dt[6];                               /* DTUN: per operator, Q24 log2 */
} fm6_eff[NTRK];

/* ------------------------------------------------------- patch formats --- */
/* the highest value of each byte of the 155-byte voice */
static uint32_t fm6_max(uint32_t i)
{
    static const uint8_t OPMAX[21] = {99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 3, 3, 7, 3, 7, 99, 1, 31, 99, 14};
    static const uint8_t VMAX[19] = {99, 99, 99, 99, 99, 99, 99, 99, 31, 7, 1, 99, 99, 99, 99, 1, 5, 7, 48};
    if (i < 126u)
        return OPMAX[i % 21u];
    if (i < FP_NAME)
        return VMAX[i - 126u];
    return 126u;
}

/* every value inside its range (a name byte outside 32..126 becomes a space) */
static void fm6_sanitize(uint8_t *v)
{
    uint32_t i;
    for (i = 0; i < FP_SIZE; i++) {
        if (i >= FP_NAME)
            v[i] = v[i] < 32u || v[i] > 126u ? ' ' : v[i];
        else if (v[i] > fm6_max(i))
            v[i] = (uint8_t)fm6_max(i);
    }
    v[FP_SIZE] = 0;
}

/* 128-byte packed record (VMEM, the 32-voice bank's) -> 155-byte voice; bits a record does not use are ignored */
static void fm6_unpack(const uint8_t *b, uint8_t *v)
{
    uint32_t k, i;
    for (k = 0; k < 6u; k++) {
        const uint8_t *o = b + k * 17u;
        uint8_t *d = v + k * FP_OP;
        for (i = 0; i < 11u; i++)
            d[i] = o[i] & 0x7Fu;
        d[FP_LC] = o[11] & 3u;
        d[FP_RC] = (o[11] >> 2) & 3u;
        d[FP_RS] = o[12] & 7u;
        d[FP_DET] = (o[12] >> 3) & 15u;
        d[FP_AMS] = o[13] & 3u;
        d[FP_KVS] = (o[13] >> 2) & 7u;
        d[FP_OL] = o[14] & 0x7Fu;
        d[FP_MODE] = o[15] & 1u;
        d[FP_FC] = (o[15] >> 1) & 31u;
        d[FP_FF] = o[16] & 0x7Fu;
    }
    for (i = 0; i < 9u; i++)
        v[FP_PR1 + i] = b[102 + i] & 0x7Fu;              /* pitch EG, algorithm */
    v[FP_ALG] &= 31u;
    v[FP_FB] = b[111] & 7u;
    v[FP_OKS] = (b[111] >> 3) & 1u;
    for (i = 0; i < 4u; i++)
        v[FP_LFS + i] = b[112 + i] & 0x7Fu;
    v[FP_LKS] = b[116] & 1u;
    v[FP_LFW] = (b[116] >> 1) & 7u;
    v[FP_LPMS] = (b[116] >> 4) & 7u;
    v[FP_TRNSP] = b[117] & 0x7Fu;
    for (i = 0; i < 10u; i++)
        v[FP_NAME + i] = b[118 + i] & 0x7Fu;
    fm6_sanitize(v);
}

/* 155-byte voice -> 128-byte packed record (7-bit bytes: it travels in SysEx as it is) */
static void fm6_pack(const uint8_t *v, uint8_t *b)
{
    uint32_t k, i;
    for (k = 0; k < 6u; k++) {
        const uint8_t *o = v + k * FP_OP;
        uint8_t *d = b + k * 17u;
        for (i = 0; i < 11u; i++)
            d[i] = o[i] & 0x7Fu;
        d[11] = (uint8_t)((o[FP_LC] & 3u) | (o[FP_RC] & 3u) << 2);
        d[12] = (uint8_t)((o[FP_RS] & 7u) | (o[FP_DET] & 15u) << 3);
        d[13] = (uint8_t)((o[FP_AMS] & 3u) | (o[FP_KVS] & 7u) << 2);
        d[14] = o[FP_OL] & 0x7Fu;
        d[15] = (uint8_t)((o[FP_MODE] & 1u) | (o[FP_FC] & 31u) << 1);
        d[16] = o[FP_FF] & 0x7Fu;
    }
    for (i = 0; i < 9u; i++)
        b[102 + i] = v[FP_PR1 + i] & 0x7Fu;
    b[110] &= 31u;
    b[111] = (uint8_t)((v[FP_FB] & 7u) | (v[FP_OKS] & 1u) << 3);
    for (i = 0; i < 4u; i++)
        b[112 + i] = v[FP_LFS + i] & 0x7Fu;
    b[116] = (uint8_t)((v[FP_LKS] & 1u) | (v[FP_LFW] & 7u) << 1 | (v[FP_LPMS] & 7u) << 4);
    b[117] = v[FP_TRNSP] & 0x7Fu;
    for (i = 0; i < 10u; i++)
        b[118 + i] = v[FP_NAME + i] & 0x7Fu;
}

static void fm6_rom_patch(const fm6_rom_t *r, uint8_t *v)   /* a Melodee factory voice -> 155-byte voice */
{
    uint32_t n, i;
    for (n = 1; n <= 6u; n++)
        for (i = 0; i < FP_OP; i++)
            v[(6u - n) * FP_OP + i] = r->op[n - 1u][i];
    for (i = 0; i < 4u; i++) {
        v[FP_PR1 + i] = r->pr[i];
        v[FP_PL1 + i] = r->pl[i];
    }
    v[FP_ALG] = r->alg;
    v[FP_FB] = r->fb;
    v[FP_OKS] = r->oks;
    v[FP_LFS] = r->lfs;
    v[FP_LFD] = r->lfd;
    v[FP_LPMD] = r->lpmd;
    v[FP_LAMD] = r->lamd;
    v[FP_LKS] = r->lfks;
    v[FP_LFW] = r->lfw;
    v[FP_LPMS] = r->lpms;
    v[FP_TRNSP] = r->trnsp;
    for (i = 0; i < 10u; i++)
        v[FP_NAME + i] = (uint8_t)(r->name[i] ? r->name[i] : ' ');
    fm6_sanitize(v);
}

/* factory patch k (F1..F24) as a packed record */
static void fm6_factory(uint32_t k, uint8_t *pk)
{
    uint8_t v[FP_SIZE + 1u];
    if (k < FM6_NFACTORY) {
        memcpy(pk, FM6_FACTORY[k], FM6_PACKED);
        return;
    }
    fm6_rom_patch(&FM6_ROM[(k - FM6_NFACTORY) % FM6_NROM], v);
    memset(pk, 0, FM6_PACKED);
    fm6_pack(v, pk);
}

static void fm6_name(char *d, const uint8_t *v)          /* the voice name, trailing spaces cut */
{
    uint32_t i, n = 0;
    for (i = 0; i < 10u; i++) {
        d[i] = (char)v[FP_NAME + i];
        if (d[i] != ' ')
            n = i + 1u;
    }
    d[n] = 0;
}

/* ---------------------------------------------------- the track's patch --- */
/* track tr's patch = v (155 bytes, sanitized). load: a new voice, as a DX7 program change (the notes stop); else
 * an edit, which the sounding notes follow. Main loop: the ISR takes it at its next block */
static void fm6_put_patch(uint32_t tr, const uint8_t *v, int load)
{
    uint8_t s[FP_SIZE + 1u];
    tr %= NTRK;
    memcpy(s, v, FP_SIZE);
    fm6_sanitize(s);                                /* not in place: the ISR must never copy an unsanitized byte */
    memcpy(fm6_patch[tr], s, FP_SIZE + 1u);
    if (load) {
        fm6_on[tr] = FM6_ON_ALL;
        fm6_lgen[tr]++;
    }
    RING_PUBLISH();
    fm6_pgen[tr]++;
}

/* .. a load when its name or TRANSPOSE is another (a patch sent whole, an undo, a project), else an edit */
static void fm6_set_patch(uint32_t tr, const uint8_t *v)
{
    const uint8_t *o = fm6_patch[tr % NTRK];
    fm6_put_patch(tr, v, memcmp(o + FP_NAME, v + FP_NAME, 10) || o[FP_TRNSP] != v[FP_TRNSP]);
}

/* A factory preset loads its corresponding voice directly. */
static void fm6_track_loaded(const track_t *t)
{
    uint32_t tr = (uint32_t)(t - trk);
    if (tr < NTRK && t->eng_req == ENGI_FM6) {
        uint8_t pk[FM6_PACKED], v[FP_SIZE + 1u];
        if (t->preset < FM6_NFAC)
            fm6_factory(t->preset, pk);
        fm6_unpack(t->preset < FM6_NFAC ? pk : FM6_INIT, v);   /* (past F24: INIT VOICE) */
        fm6_put_patch(tr, v, 1);
    }
}

/* power-on: every track the init voice (what a project stores for the tracks that never played FM6), the
 * function settings Dexed's (a project or the template loaded then brings its own) */
static void fm6_init(void)
{
    uint8_t v[FP_SIZE + 1u];
    uint32_t tr;
    fm6_fn_reset();
    fm6_unpack(FM6_INIT, v);
    for (tr = 0; tr < NTRK; tr++) {
        fm6_put_patch(tr, v, 1);
    }
}

/* -------------------------------------------------------------- macros --- */
/* the patch through E0..E6 (audio ISR; cheap when nothing changed). With every macro at 0 it is the patch.
 * 1 = made again (the patch or a macro changed) */
static int fm6_eff_build(const track_t *t)
{
    uint32_t tr = (uint32_t)(t - trk), k, j, car, n = 0;
    const int16_t *e = &t->p[P_E0];
    int32_t meg, step;
    if (tr >= NTRK)
        return 0;
    if (fm6_eff[tr].ok && fm6_eff[tr].gen == fm6_pgen[tr]) {
        for (k = 0; k < 7u && fm6_eff[tr].e[k] == e[k]; k++)
            ;
        if (k == 7u)
            return 0;
    }
    fm6_eff[tr].gen = fm6_pgen[tr];
    memcpy(fm6_eff[tr].p, fm6_patch[tr], FP_SIZE);
    for (k = 0; k < 7u; k++)
        fm6_eff[tr].e[k] = e[k];
    {
        uint8_t *p = fm6_eff[tr].p;
        uint32_t alg = e[0] >= 1 && e[0] <= 32 ? (uint32_t)e[0] - 1u : p[FP_ALG] & 31u;
        p[FP_ALG] = (uint8_t)alg;
        p[FP_FB] = (uint8_t)clamp(p[FP_FB] + e[1], 0, 7);
        car = fm6_carriers(alg);
        meg = clamp(e[4], -64, 63) * 40 / 64;               /* MEG: rate steps (+ slower) */
        step = clamp(e[6], 0, 127) * (12 * 13981) / 127;    /* DTUN: up to 12 cents a step (1 cent = 13981) */
        for (k = 0; k < 6u; k++) {
            uint8_t *op = p + (5u - k) * FP_OP;              /* OP1 first: carriers in their order */
            uint32_t ki = 5u - k;
            fm6_eff[tr].dt[ki] = 0;
            if ((car >> ki) & 1u) {                          /* carriers 0, +1, -1, +2, -2, +3 steps apart */
                static const int8_t SPREAD[6] = {0, 1, -1, 2, -2, 3};
                fm6_eff[tr].dt[ki] = SPREAD[n++] * step;
                continue;
            }
            if (!op[FP_MODE])
                op[FP_FC] = (uint8_t)clamp(op[FP_FC] + e[3], 0, 31);
            for (j = 0; j < 4u; j++)
                op[FP_R1 + j] = (uint8_t)clamp(op[FP_R1 + j] - meg, 0, 99);
            op[FP_KVS] = (uint8_t)clamp(op[FP_KVS] + e[5], 0, 7);
        }
    }
    fm6_eff[tr].ok = 1;
    return 1;
}

/* ----------------------------------------------- the part (Dexed's) --- */
typedef struct {
    uint32_t ph, dly;                                    /* LFO phase; delay ramp (DX7 two-slope) */
    int32_t val, depth;                                  /* Q24, 0..1 << 24 */
    uint8_t rnd;
} fm6_lfo_t;
typedef struct {
    int32_t pb;                                          /* pitch bend, Q24 */
    int32_t pmod, amod, emod;                            /* the controllers' modulation (Dexed Controllers) */
    int32_t prate;                                       /* portamento step per block */
    uint32_t clock;                                      /* Dexed blocks */
    uint32_t hash;                                       /* of the voice, to see edits */
    uint8_t tick, trig, pon, rr, lav, steal, trn, lgen;  /* rr: chooseNote's start; lav: last voice + 1 */
    uint8_t on;                                          /* the operator switches the hash was made with */
    uint8_t mcur, mlav, mnew, mlive;                     /* MONO: chooseNote's start, last keyed, taken (+ 1), live */
    int32_t mseq;
    uint32_t amd, pt[4];                                 /* AMS: the shares for this modulation (pok: figured) */
    uint8_t pok;
} fm6_ctl_t;
/* MONO / LEGATO: Dexed keys one of its 16 voices for every key and hands the sounding state from voice to
 * voice (transferSignal / transferState). FM6 plays the note on one voice and keeps what else Dexed's voices
 * hold: the note, its key-down order, whether it counts as playing, its portamento pitch and feedback memory
 * (fm6_key, fm6_note_on, fm6_legato) */
typedef struct {
    uint8_t note, fplay;                                 /* note + 1 (0: never keyed); playing, as it was left */
    int32_t seq, porta[6];
    float fb[2];
    fm6_peg_t pe;
} fm6_mono_t;
typedef struct {                                         /* a part's (engines.c eng_state) */
    fm6_voice_t v[NVOICE];
    fm6_mono_t ms[16];
    fm6_lfo_t lfo;
    fm6_ctl_t pt;
    int32_t dc_x1, dc_y1;                                /* the DC filter: the last input; the last output << 8 */
} fm6_part_t;
static fm6_part_t *fm6_part(uint32_t part);             /* engines.c */
#define FM6P(t) fm6_part((uint32_t)((t) - trk))
#define FM6F(t) fm6_fn[(uint32_t)((t) - trk) % NTRK]   /* the track's function settings */

static void fm6_lfo_step(fm6_lfo_t *l, const uint8_t *ed)   /* one block of the LFO: Lfo::getsample, getdelay */
{
    uint32_t inc = FM6_LFO_INC[ed[FP_LFS]], ph = l->ph + inc, a = 99u - (uint32_t)ed[FP_LFD], d1, d2, d;
    int32_t x;
    l->ph = ph;
    switch (ed[FP_LFW]) {
    case 0:                                              /* triangle */
        x = (int32_t)((ph >> 7) ^ (uint32_t)-(int32_t)(ph >> 31)) & ((1 << 24) - 1);
        break;
    case 1:                                              /* saw down */
        x = (int32_t)((~ph ^ (1u << 31)) >> 8);
        break;
    case 2:                                              /* saw up */
        x = (int32_t)((ph ^ (1u << 31)) >> 8);
        break;
    case 3:                                              /* square */
        x = (int32_t)(((~ph) >> 7) & (1u << 24));
        break;
    case 4:                                              /* sine */
        x = (int32_t)((0.5f + fm6_sin(ph >> 8) * 0.5f) * 16777216.0f);
        break;
    default:                                             /* sample & hold */
        if (ph < inc)
            l->rnd = (uint8_t)(l->rnd * 179u + 17u);
        x = ((l->rnd ^ 0x80) + 1) << 16;
        break;
    }
    l->val = x;
    if (a == 99u) {                                      /* DELAY 0 */
        d1 = d2 = 0xFFFFFFFFu;
    } else {
        a = (16u + (a & 15u)) << (1u + (a >> 4));
        d1 = FM6_LFO_UNIT * a;
        d2 = FM6_LFO_UNIT * (a & 0xFF80u ? a & 0xFF80u : 0x80u);
    }
    d = l->dly + (l->dly < (1u << 31) ? d1 : d2);
    if (d < l->dly) {                                    /* past the top */
        l->depth = 1 << 24;
        return;
    }
    l->dly = d;
    l->depth = d < (1u << 31) ? 0 : (int32_t)((d >> 7) & ((1u << 24) - 1u));
}

/* a controller's share (Controllers::applyMod: CC x RANGE / 100, as Dexed's float figures it) */
static int32_t fm6_ctl(int32_t cc, int32_t range)
{
    return cc * range / 100 - ((cc == 100 && (range == 53 || range == 59)) || (cc == 75 && range == 84));
}

static void fm6_ghost(track_t *t, voice_t *v, fm6_voice_t *s);

/* per part and block: the controllers, and every other block (or at a key-down retrigger) the LFO */
static void fm6_ctl_block(track_t *t, const uint8_t *ed)
{
    fm6_part_t *P = FM6P(t);
    fm6_ctl_t *c = &P->pt;
    uint32_t k, egs = 0;
    int32_t raw = t->bend_raw, cc[4], m;
    cc[0] = t->mw;
    cc[1] = t->foot;
    cc[2] = t->breath;
    cc[3] = t->at;
    c->pmod = c->amod = c->emod = 0;
    for (k = 0; k < 4u; k++) {                           /* wheel, foot, breath, aftertouch */
        uint32_t tg = FM6F(t)[FN_MWA + 2u * k];
        m = fm6_ctl(cc[k], FM6F(t)[FN_MWR + 2u * k]);
        if (tg & 1u)
            c->pmod = m > c->pmod ? m : c->pmod;
        if (tg & 2u)
            c->amod = m > c->amod ? m : c->amod;
        if (tg & 4u)
            c->emod = m > c->emod ? m : c->emod;
        egs |= tg & 4u;
    }
    if (!egs)
        c->emod = 127;
    if (!raw) {                                          /* pitch bend */
        c->pb = 0;
    } else if (!FM6F(t)[FN_PBSTEP]) {
        c->pb = fm6_f32(raw * 2048 * (raw > 0 ? FM6F(t)[FN_PBUP] : FM6F(t)[FN_PBDN])) / 12;
    } else {
        int32_t stp = 12 / FM6F(t)[FN_PBSTEP];
        c->pb = ((raw * stp / 8191) * (8191 / stp)) * 2048;
    }
    c->pon = (uint8_t)(FM6F(t)[FN_PMODE] || t->porta);
    c->prate = !c->pon ? FM6_PORTA[0] : FM6F(t)[FN_GLISS] ? FM6_GLISS[FM6F(t)[FN_PTIME]] : FM6_PORTA[FM6F(t)[FN_PTIME]];
    if (c->trig || !(c->tick++ & 1u)) {                  /* Dexed's 64-sample grid, restarted by a retrigger */
        c->trig = 0;
        c->tick = 1;
        c->clock++;
        fm6_lfo_step(&P->lfo, ed);
        for (k = 0; k < NVOICE; k++)                     /* voices over, as Dexed runs them on */
            if (P->v[k].played && !P->v[k].frozen && !t->v[k].active)
                fm6_ghost(t, &t->v[k], &P->v[k]);
    }
}

/* ------------------------------------------------------------ the voice --- */
static fm6_voice_t *fm6_state(const track_t *t, const voice_t *v) { return &FM6P(t)->v[v - t->v]; }
static const uint8_t *fm6_ed(const track_t *t) { return fm6_eff[(uint32_t)(t - trk) % NTRK].p; }
static uint32_t fm6_note(const track_t *t, const voice_t *v)   /* the note the DX7 rules see: + TRANSPOSE */
{
    return (uint32_t)clamp((int32_t)v->note + fm6_ed(t)[FP_TRNSP] - 24, 0, 127);
}
static int fm6_op_on(const track_t *t, uint32_t k) { return (fm6_on[(uint32_t)(t - trk) % NTRK] >> k) & 1u; }

/* the operators' pitch, level and rate scaling for the voice's note and velocity (Dx7Note::init / update) */
static void fm6_keyed(fm6_voice_t *s, const uint8_t *ed, uint32_t note)
{
    uint32_t k;
    for (k = 0; k < 6u; k++) {
        const uint8_t *op = &ed[k * FP_OP];
        s->ol[k] = (int16_t)fm6_outlevel(op, note, s->vel);
        s->rs[k] = (int8_t)fm6_rscale(op, note);
        s->base[k] = fm6_logfreq(op, note);
    }
}

/* Dexed's "playing" (Dx7Note::isPlaying: an output operator's envelope not done) */
static int fm6_playing(const track_t *t, uint32_t i)
{
    const fm6_voice_t *s = &FM6P(t)->v[i];
    const uint8_t *ed = fm6_ed(t);
    uint32_t k;
    if (!s->played)
        return 0;
    for (k = 0; k < 6u; k++)
        if ((FM6_ALG[ed[FP_ALG] & 31][k] & 4u) && (s->eg[k].ix < 4u || ed[k * FP_OP + FP_L1 + 3] > 0))
            return 1;
    return 0;
}

/* POLY: the voice for a key (chooseNote): free over key-up over the same note, the oldest key first */
static uint32_t fm6_alloc(track_t *t, uint32_t note)
{
    fm6_part_t *P = FM6P(t);
    uint32_t k = P->pt.rr % NVOICE, best = k, i;
    int32_t bs = -1;
    for (i = 0; i < NVOICE; i++) {
        int32_t sc = (fm6_playing(t, k) ? 0 : 4) + (t->v[k].gate ? 0 : 2) + (P->v[k].played && t->v[k].note == note);
        if (sc > bs || (sc == bs && t->v[k].age < t->v[best].age)) {
            best = k;
            bs = sc;
        }
        k = (k + 1u) % NVOICE;
    }
    P->pt.rr = (uint8_t)((best + 1u) % NVOICE);
    P->pt.steal = (uint8_t)fm6_playing(t, best);
    return best;
}

static void fm6_sync(track_t *t, const voice_t *self);

/* where portamento starts, as Dexed's initPortamento finds it: the last voice keyed (still going:
 * its pitch now; after a MONO key-up handed the note on, as it was then); 0 = no portamento */
static int fm6_psrc(const track_t *t, fm6_part_t *P, int32_t *src)
{
    uint32_t k, l = P->pt.lav;
    if (!l || !P->v[l - 1u].played || !P->pt.pon || FM6F(t)[FN_PTIME] <= 0)
        return 0;
    for (k = 0; k < 6u; k++)
        src[k] = P->v[l - 1u].porta[k];
    return 1;
}

/* MONO: Dexed's voice c is keyed and its key is down (but for the key going down now, + 1) */
static int fm6_mkd(const track_t *t, uint32_t c, uint32_t now)
{
    const fm6_part_t *P = FM6P(t);
    uint32_t i;
    if (!P->ms[c].note || P->ms[c].note == now)
        return 0;
    for (i = 0; i < 16u; i++)                            /* the key's latest voice */
        if (P->ms[i].note == P->ms[c].note && P->ms[i].seq > P->ms[c].seq)
            return 0;
    for (i = 0; i < t->nmono; i++)
        if (t->mono_stack[i] + 1u == P->ms[c].note)
            return 1;
    return 0;
}

/* MONO: the sounding state leaves Dexed's live voice for voice c (+ 1) */
static void fm6_mhand(track_t *t, uint32_t c)
{
    fm6_part_t *P = FM6P(t);
    uint32_t l = P->pt.mlive, k;
    fm6_voice_t *s = &P->v[0];
    if (l) {                                             /* the one left keeps what it had */
        P->ms[l - 1u].fplay = (uint8_t)(t->v[0].active || s->played ? fm6_playing(t, 0) : 0);
        for (k = 0; k < 6u; k++)
            P->ms[l - 1u].porta[k] = s->porta[k];
        P->ms[l - 1u].fb[0] = s->fb[0];
        P->ms[l - 1u].fb[1] = s->fb[1];
        P->ms[l - 1u].pe = s->pe;
    }
    P->pt.mlive = (uint8_t)c;
    P->pt.mnew = (uint8_t)c;
}

static void fm6_mtake(track_t *t, fm6_voice_t *s)        /* MONO: the voice plays Dexed's voice mnew */
{
    fm6_part_t *P = FM6P(t);
    uint32_t c = P->pt.mnew, k;
    if (!c)
        return;
    for (k = 0; k < 6u; k++)
        s->porta[k] = P->ms[c - 1u].porta[k];
    s->fb[0] = P->ms[c - 1u].fb[0];
    s->fb[1] = P->ms[c - 1u].fb[1];
    s->pe = P->ms[c - 1u].pe;
    P->pt.mnew = 0;
}

/* a key goes down in MONO / LEGATO: Dexed keys a voice for it (chooseNote, init, initPortamento);
 * it takes the sound if it is the only key or above the one sounding (else it waits, keyed) */
static void fm6_key(track_t *t, uint32_t note)
{
    fm6_part_t *P = FM6P(t);
    uint32_t i, k, c, l, nt;
    const uint8_t *ed;
    int32_t bs = -1;
    fm6_sync(t, 0);
    ed = fm6_ed(t);
    k = P->pt.mcur % 16u;
    c = k;
    l = P->pt.mlive;
    for (i = 0; i < 16u; i++) {
        int32_t sc = ((l == k + 1u ? fm6_playing(t, 0) : P->ms[k].fplay) ? 0 : 4) +
                     (fm6_mkd(t, k, note + 1u) ? 0 : 2) +
                     (P->ms[k].note == note + 1u);
        if (sc > bs || (sc == bs && P->ms[k].seq < P->ms[c].seq)) {
            c = k;
            bs = sc;
        }
        k = (k + 1u) % 16u;
    }
    P->pt.mcur = (uint8_t)((c + 1u) % 16u);
    nt = (uint32_t)clamp((int32_t)note + ed[FP_TRNSP] - 24, 0, 127);
    for (k = 0; k < 6u; k++)                             /* init: the note's pitch */
        P->ms[c].porta[k] = fm6_logfreq(&ed[k * FP_OP], nt);
    if (P->pt.mlav && P->ms[P->pt.mlav - 1u].note && P->pt.pon && FM6F(t)[FN_PTIME] > 0) {
        uint32_t a = P->pt.mlav - 1u;                    /* initPortamento: from the last keyed voice */
        for (k = 0; k < 6u; k++)
            P->ms[c].porta[k] = l == a + 1u && a != c ? P->v[0].porta[k] : P->ms[a].porta[k];
    }
    P->ms[c].note = (uint8_t)(note + 1u);
    P->ms[c].seq = ++P->pt.mseq;
    P->ms[c].fplay = 1;                                  /* keyed: its envelopes start (Dexed counts it playing) */
    fm6_peg_set(&P->ms[c].pe, ed);
    P->pt.mnew = 0;
    if (l && l != c + 1u && fm6_mkd(t, l - 1u, 0) && P->ms[l - 1u].note > note + 1u)
        return;                                          /* a higher key sounds: this one waits */
    fm6_mhand(t, c + 1u);
    P->pt.mlav = (uint8_t)(c + 1u);
}

/* a key goes down (DexedAudioProcessor::keydown, Dx7Note::init) */
static void fm6_note_on(track_t *t, voice_t *v)
{
    fm6_part_t *P = FM6P(t);
    uint32_t k, i, vi = (uint32_t)(v - t->v), note, vel;
    fm6_voice_t *s = fm6_state(t, v);
    const uint8_t *ed;
    int mono = t->p[P_VOICE] != V_POLY, steal = P->pt.steal || (mono && s->played), ps;
    int32_t src[6];
    P->pt.steal = 0;
    fm6_sync(t, v);
    ed = fm6_ed(t);
    ps = fm6_psrc(t, P, src);                               /* before this voice takes the note */
    for (i = 0; i < NVOICE; i++)                         /* the first key down: the LFO restarts */
        if (i != vi && t->v[i].gate)
            break;
    if (i == NVOICE) {
        if (ed[FP_LKS])
            P->lfo.ph = (1u << 31) - 1u;
        P->lfo.dly = 0;
        P->pt.trig = 1;
    }
    vel = v->mvel ? v->mvel : v->vel;                    /* (the note's: UNISON lowers v->vel) */
    s->vel = (uint8_t)(FM6F(t)[FN_VNORM] ? vel * 7874015u / 10000000u : vel);
    s->note = v->note;
    note = fm6_note(t, v);
    fm6_keyed(s, ed, note);
    for (k = 0; k < 6u; k++) {
        s->eg[k].level = 0;
        fm6_eg_go(&s->eg[k], &ed[k * FP_OP], s->ol[k], s->rs[k], 0);
        s->porta[k] = s->base[k];
    }
    fm6_peg_set(&s->pe, ed);
    s->down = 1;
    if (ed[FP_OKS] && !steal)                            /* KEY SYNC (not on a stolen voice: no click) */
        for (k = 0; k < 6u; k++)
            s->ph[k] = 0, s->gout[k] = 0;
    if (ps && !mono)                                     /* portamento from the last voice's pitch */
        for (k = 0; k < 6u; k++)
            s->porta[k] = src[k];
    if (mono)
        fm6_mtake(t, s);
    if (!mono && !ed[FP_OKS])                            /* the same note sounding: its phases */
        for (i = 0; i < NVOICE; i++)
            if (i != vi && fm6_playing(t, i) && P->v[i].played && t->v[i].note == v->note) {
                for (k = 0; k < 6u; k++)
                    s->ph[k] = P->v[i].ph[k];
                break;
            }
    P->pt.lav = (uint8_t)(vi + 1u);
    s->played = 1;
    s->sub = 0;
    s->quiet = 0;
    s->frozen = 0;
    s->still = 0;
}

/* MONO / LEGATO without a new attack: the note changes, the envelopes go on (Dexed's mono: the
 * voice keyed for the note takes over the sounding one's state, transferState), with that voice's
 * portamento pitch and feedback memory: a new key (fm6_key), or the highest key still down after
 * a key-up */
static void fm6_legato(track_t *t, voice_t *v)
{
    fm6_part_t *P = FM6P(t);
    uint32_t k, c;
    fm6_voice_t *s = fm6_state(t, v), keep = *s;
    const uint8_t *ed;
    fm6_sync(t, v);
    ed = fm6_ed(t);
    if (!P->pt.mnew) {                                   /* a key-up: the voice keyed for this key */
        for (c = 0; c < 16u && !(P->ms[c].note == v->note + 1u && fm6_mkd(t, c, 0)); c++)
            ;
        if (c < 16u)
            fm6_mhand(t, c + 1u);
    }
    s->note = v->note;
    fm6_keyed(s, ed, fm6_note(t, v));
    for (k = 0; k < 6u; k++) {                           /* the old note's levels and rates stay */
        s->ol[k] = keep.ol[k];
        s->rs[k] = keep.rs[k];
        s->porta[k] = s->base[k];
    }
    fm6_peg_set(&s->pe, ed);
    fm6_mtake(t, s);
}

/* the voice is over: released, and every output operator's envelope (and the feedback operator's) gone under
 * what any engine renders, for good (voice.c engine_t.done). Dexed keeps computing such a voice; FM6 keeps
 * only its control path running (fm6_ghost) */
static int fm6_done(track_t *t, voice_t *v) { return fm6_state(t, v)->quiet >= 2u; }

/* a new Dexed block: key-up, the envelopes, pitch and gains (Dx7Note::compute), the routing */
static void fm6_control(track_t *t, voice_t *v, fm6_voice_t *s, const vmod_t *m)
{
    fm6_part_t *P = FM6P(t);
    uint32_t tr = (uint32_t)(t - trk) % NTRK, alg, k, dead = 1;
    const uint8_t *ed = fm6_ed(t);
    int32_t lv[6], lfo = P->lfo.val, dly = P->lfo.depth, pm, sens, pbase, amd, a1, a2, tune, fbv;
    /* MLVL and FLT: the modulators' level, 0.75 dB / 32 steps (microsteps, as the output levels), Q24 */
    int32_t mod = (clamp(t->p[P_E2], -64, 63) * 24 + clamp(m->cutoff >> 8, -150, 150) * 16) * (1 << 16);
    uint32_t pmd = ((uint32_t)ed[FP_LPMD] * 165u) >> 6;
    alg = (uint32_t)ed[FP_ALG] & 31u;
    if (!v->gate && s->down) {                           /* key up: every envelope to its fourth segment */
        s->down = 0;
        for (k = 0; k < 6u; k++)
            fm6_eg_go(&s->eg[k], &ed[k * FP_OP], s->ol[k], s->rs[k], 3);
        fm6_peg_go(&s->pe, 3);
    }
    /* pitch: the LFO (PMD x PMS, after the delay, or a controller's), the pitch envelope, bend */
    sens = FM6_PMS[ed[FP_LPMS]] * (lfo - (1 << 23));
    {
        int32_t p1 = (int32_t)((float)pmd * (float)dly * (float)sens * (1.0f / 549755813888.0f));
        int32_t p2 = (int32_t)((float)P->pt.pmod * (float)sens * (1.0f / 16384.0f));
        p1 = p1 < 0 ? -p1 : p1;
        p2 = p2 < 0 ? -p2 : p2;
        pm = p1 > p2 ? p1 : p2;
        pm = fm6_peg_step(&s->pe, s->down) + (sens < 0 ? -pm : pm);
    }
    tune = song.g[G_TUNE] * 13981 + tuning_log();        /* cents + A4 reference, Q24 octaves */
    pbase = P->pt.pb + tune;
    pm += pbase + m->plog;                               /* + Melodee's glide, LFO / ENV pitch, unison */
    /* amplitude: the LFO (AMD, after the delay) or a controller's, at least the EG bias */
    lfo = (1 << 24) - lfo;
    a1 = (int32_t)((float)(((uint32_t)ed[FP_LAMD] * 165u) >> 6) * (float)dly * (float)lfo * (1.0f / 4294967296.0f));
    a2 = (int32_t)((float)P->pt.amod * (float)lfo * (1.0f / 128.0f));
    amd = a1 > a2 ? a1 : a2;
    a1 = (1 << 24) - ((P->pt.emod + 1) << 17);
    amd = (uint32_t)a1 > (uint32_t)amd ? a1 : amd;
    for (k = 0; k < 6u; k++) {
        const uint8_t *op = &ed[k * FP_OP];
        int car = fm6_carrier(alg, 6u - k);
        int32_t rs = s->rs[k], level;
        if (!fm6_op_on(t, k)) {                          /* switched off: the envelope runs on, silent */
            fm6_eg_step(&s->eg[k], op, s->ol[k], rs, s->down);
            lv[k] = 0;
            continue;
        }
        if (op[FP_MODE]) {
            s->fq[k] = fm6_freq(s->base[k] + pbase + fm6_eff[tr].dt[k]);
        } else {
            int32_t b = s->base[k];
            if (s->porta[k] != s->base[k]) {             /* portamento towards the note */
                int32_t cur = s->porta[k], up = cur < s->base[k], np = cur + (up ? P->pt.prate : -P->pt.prate);
                b = cur;
                if (FM6F(t)[FN_GLISS])
                    b -= (b - 50857777) % ((1 << 24) / 12);
                if ((up && np > s->base[k]) || (!up && np < s->base[k]))
                    np = s->base[k];
                s->porta[k] = np;
            }
            s->fq[k] = fm6_freq(b + pm + fm6_eff[tr].dt[k]);
        }
        level = fm6_eg_step(&s->eg[k], op, s->ol[k], rs, s->down);
        if (FM6_AMS[op[FP_AMS]]) {                       /* AMS: the modulation takes a part of the level */
            uint32_t a = (uint32_t)op[FP_AMS], pt;
            if (P->pt.amd != (uint32_t)amd) {            /* one modulation a block for the part: 3 shares */
                P->pt.amd = (uint32_t)amd;
                P->pt.pok = 0;
            }
            if (!((P->pt.pok >> a) & 1u)) {
                uint32_t sa = (uint32_t)((float)amd * ((float)FM6_AMS[a] * (1.0f / 16777216.0f)));
                P->pt.pt[a] = sa ? fm6_ams_pt(sa) : 198789u;   /* exp(12.2) */
                P->pt.pok |= (uint8_t)(1u << a);
            }
            pt = P->pt.pt[a];
            level -= (int32_t)((float)level * ((float)pt * (1.0f / 16777216.0f)));
        }
        if (!car && mod)
            level = clamp(level + mod, 0, 20 << 24);
        lv[k] = level;
    }
    for (k = 0; k < 6u; k++) {                           /* the output operators all gone for good? */
        const fm6_eg_t *e = &s->eg[k];               /* (and the feedback one: its memory outlives the note) */
        if ((fm6_carrier(alg, 6u - k) || ((FM6_ALG[alg][k] & 0xc0u) == 0xc0u && ed[FP_FB])) &&
            (s->down || ed[k * FP_OP + FP_L1 + 3] > 0 || e->level >= FM6_SILENT || e->ix < 3u ||
             (e->ix == 3u && e->rising)))
            dead = 0;
    }
    s->quiet = dead ? (uint8_t)(s->quiet + (s->quiet < 2u)) : 0;
    fbv = clamp(ed[FP_FB] + ((m->shape - (64 << 8)) >> 11), 0, 7);   /* SHP moves the feedback */
    fm6_plan(s, lv, alg, FM6F(t)[FN_ENGINE], fbv ? 8u - (uint32_t)fbv : 16u);
}

/* nothing moves a voice at rest: no LFO or controller pitch (PMS 0, or no depth), no amplitude
 * modulation where an operator has AMS */
static int fm6_quiet(const fm6_part_t *P, const uint8_t *ed)
{
    uint32_t k, ams = 0;
    int32_t dly = P->lfo.depth;
    for (k = 0; k < 6u; k++)
        ams |= (uint32_t)ed[k * FP_OP + FP_AMS];
    return (!ed[FP_LPMS] || (!P->pt.pmod && (!ed[FP_LPMD] || !dly))) &&
           (!ams || (!P->pt.amod && P->pt.emod == 127 && (!ed[FP_LAMD] || !dly)));
}

/* a voice that is over: its envelopes, pitch, gains and phases go on a Dexed block, without the samples (none
 * of them sounds), so that its next note starts where Dexed's would (KEY SYNC off, a voice taken while playing) */
static void fm6_ghost(track_t *t, voice_t *v, fm6_voice_t *s)
{
    static const vmod_t m0 = {.shape = 64 << 8};
    fm6_part_t *P = FM6P(t);
    const uint8_t *ed = fm6_ed(t);
    uint32_t k;
    int32_t pb = P->pt.pb + song.g[G_TUNE] * 13981 + tuning_log();
    int quiet;
    if (s->still == 2u)
        return;                                          /* key sync, not playing: its next note starts afresh */
    quiet = fm6_quiet(P, ed);
    if (s->still && quiet && pb == s->spb) {             /* at rest: only time goes on */
        for (k = 0; k < 6u; k++)
            s->ph[k] += (uint32_t)s->fq[k] << FM6_LG_N;
        return;
    }
    fm6_control(t, v, s, &m0);
    for (k = 0; k < 6u; k++)
        if ((s->plan[k] & FM6_P_RUN) || (k && k < s->loop && (s->plan[0] & FM6_P_RUN)))
            s->ph[k] += (uint32_t)s->fq[k] << FM6_LG_N;
    s->sub = 0;
    s->still = 0;
    s->spb = pb;
    if (quiet && !s->down && s->pe.pix >= 4u) {          /* every envelope done, portamento there */
        for (k = 0; k < 6u && s->eg[k].ix >= 4u && s->porta[k] == s->base[k]; k++)
            ;
        if (k == 6u)
            s->still = (uint8_t)(ed[FP_OKS] && t->p[P_VOICE] == V_POLY && !fm6_playing(t, (uint32_t)(v - t->v)) ? 2 : 1);
    }
}

static void fm6_output(const float *sum, int32_t *out, uint32_t n, const vmod_t *m, int32_t gain, int add)
{
    float scale = (float)gain * (1.0f / 131072.0f);
    for (uint32_t i = 0; i < n; i++) {
        int32_t y = (int32_t)(fm_clampf(sum[i], -16.0f, 16.0f) * (float)amp_at(m, i) * scale);
        if (add) out[i] += y; else out[i] = y;
    }
}

#if MELODEE_DUAL_CORE
/* Only the planned operator kernel and output gain run on CPU1. Controls,
 * envelopes, allocation, modulation, patches and RNG stay on CPU0. */
static struct {
    fm6_voice_t *voice;
    vmod_t modulation;
    uint32_t n;
    int32_t gain;
    float bus[2][CTL], sum[CTL];
    int32_t pcm[CTL];
} fm6_job;
static int fm6_pending;
static uint32_t fm6_pairs;
static int32_t *fm6_pending_out;

static void fm6_worker_kernel(void *unused)
{
    (void)unused;
    fm6_run_into(fm6_job.voice, fm6_job.n, fm6_job.bus, fm6_job.sum);
    fm6_output(fm6_job.sum, fm6_job.pcm, fm6_job.n, &fm6_job.modulation, fm6_job.gain, 0);
}
static void fm6_join(void)
{
    if (!fm6_pending) return;
    fm6_pending = 0;
    if (!audio_worker_join()) return;                    /* CPU1 lost: this voice's block is dropped */
    /* Voice addition order is identical to serial FM6, including rounding. */
    for (uint32_t i = 0; i < fm6_job.n; i++) fm6_pending_out[i] += fm6_job.pcm[i];
}
#else
static inline void fm6_join(void) {}
#endif

static void fm6_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    fm6_voice_t *s = fm6_state(t, v);
    int32_t k = t->p[P_VOICE] == V_UNISON ? VOICE_FS * 2 / 5 : VOICE_FS;
    if (!s->sub) fm6_control(t, v, s, m);
    s->sub ^= 1u;
#if MELODEE_DUAL_CORE && !defined(FM6_TAP)
    /* The first voice runs on CPU1; CPU0 prepares AND renders the second.
     * Join before adding either voice and before any post/next-block work. */
    uint32_t next = (uint32_t)(v - t->v) + 1u;
    while (next < NVOICE && !t->v[next].active) next++;
    if (!fm6_pending && audio_worker_online && n <= CTL && next < NVOICE) {
        fm6_job.voice = s; fm6_job.modulation = *m; fm6_job.n = n; fm6_job.gain = k;
        if (audio_worker_submit(fm6_worker_kernel, 0)) {
            fm6_pending = 1; fm6_pending_out = out;
            return;
        }
    }
#endif
    fm6_run(s, n);
#ifdef FM6_TAP
    FM6_TAP(fm6_sum, n);
#endif
#if MELODEE_DUAL_CORE
    if (fm6_pending) fm6_pairs++;
    fm6_join();
    fm6_output(fm6_sum, out, n, m, k, 1);
#else
    fm6_output(fm6_sum, out, n, m, k, 1);
#endif
}

/* the part's patch against what the voices play: a new voice (fm6_lgen) or another TRANSPOSE: the notes stop,
 * as on a DX7 program change; any other edit (the patch, the macros, the switches) reaches the sounding voices
 * (Dx7Note::update: held notes go on from L3). Before a key-down (but for its own voice) and at every block */
static void fm6_sync(track_t *t, const voice_t *self)
{
    fm6_part_t *P = FM6P(t);
    uint32_t tr = (uint32_t)(t - trk) % NTRK, i, h = 2166136261u;
    const uint8_t *ed;
    int panic;
    if (!fm6_eff_build(t) && P->pt.hash && P->pt.on == fm6_on[tr] && P->pt.lgen == fm6_lgen[tr])
        return;                                          /* nothing changed (every block: keep it cheap) */
    ed = fm6_ed(t);
    for (i = 0; i < FP_SIZE; i++)
        h = (h ^ ed[i]) * 16777619u;
    h = (h ^ fm6_on[tr]) * 16777619u;
    panic = P->pt.lgen != fm6_lgen[tr] || (P->pt.hash && ed[FP_TRNSP] != P->pt.trn);
    if (h == P->pt.hash && !panic)
        return;
    for (i = 0; i < NVOICE; i++) {
        voice_t *v = &t->v[i];
        fm6_voice_t *s = &P->v[i];
        uint32_t k;
        if (v == self || !(v->active || (s->played && !s->frozen)))
            continue;                                    /* (voices over too: Dexed keeps them live) */
        s->still = 0;
        if (panic) {                                     /* the notes stop (one block's fade), as Dexed's panic */
            if (v->active) {
                v->gate = 0;
                v->stage = 4;
            }
            s->frozen = 1;
            for (k = 0; k < 6u; k++)
                s->ph[k] = 0, s->gout[k] = 0;
            continue;
        }
        fm6_keyed(s, ed, fm6_note(t, v));
        if (s->down)
            for (k = 0; k < 6u; k++)
                fm6_eg_go(&s->eg[k], &ed[k * FP_OP], s->ol[k], s->rs[k], 2);
    }
    P->pt.hash = h;
    P->pt.trn = ed[FP_TRNSP];
    P->pt.lgen = fm6_lgen[tr];
    P->pt.on = fm6_on[tr];
}

static void fm6_block(track_t *t)                        /* per part and block: the patch, controllers, LFO */
{
    fm6_sync(t, 0);
    fm6_ctl_block(t, fm6_ed(t));
}

/* the part after its voices: Dexed's DC filter on the voices' sum (PluginFx: y = x - x' + (1 - 126 / FS) y',
 * about 20 Hz); with no voice it starts afresh */
static void fm6_post(track_t *t, int32_t *out, uint32_t n, uint32_t nr)
{
    fm6_join();
    fm6_part_t *P = FM6P(t);
    uint32_t i;
    int32_t x1 = P->dc_x1, y = P->dc_y1;
    if (!nr) {
        P->dc_x1 = P->dc_y1 = 0;
        return;
    }
    for (i = 0; i < n; i++) {
        int32_t x = out[i];
        y = (int32_t)(((int64_t)(x - x1) * 256 + (((int64_t)y * 1070673990) >> 30)));
        x1 = x;
        out[i] = (y + 128) >> 8;
    }
    P->dc_x1 = x1;
    P->dc_y1 = y;
}

/* ------------------------------------------------------ DX7 SysEx in --- */
/* Frames that start F0 43 (Yamaha), collected by the USB ISR (usb.c sysex_byte) for the main loop
 * (fm6_store.c fm6_service): a 32-voice dump is the longest. One frame at a time; a frame arriving while one
 * is waiting is dropped */
#define FM6_RX 4104u
static uint8_t fm6_rx[FM6_RX] __attribute__((section(".pool")));
static uint32_t fm6_rx_n;
static volatile uint8_t fm6_rx_ready;
static uint8_t fm6_rx_on;

static void fm6_sx_byte(uint8_t b)
{
    if (b == 0xF0) {
        fm6_rx_on = !fm6_rx_ready;
        if (fm6_rx_on)
            fm6_rx_n = 0;                               /* a pending frame belongs to the main loop */
    }
    if (!fm6_rx_on || b >= 0xF8u)
        return;                                          /* (realtime may occur anywhere in SysEx) */
    if ((b & 0x80u) && b != 0xF0u && b != 0xF7u) {
        fm6_rx_on = 0;                                  /* a channel/system status cancels the frame */
        return;
    }
    if (fm6_rx_n >= FM6_RX || (fm6_rx_n == 1u && b != 0x43)) {
        fm6_rx_on = 0;                                   /* too long, or not Yamaha */
        return;
    }
    fm6_rx[fm6_rx_n++] = b;
    if (b == 0xF7) {
        fm6_rx_on = 0;
        RING_PUBLISH();
        fm6_rx_ready = 1;
    }
}

/* --------------------------------------------------------- the engine --- */
static const char *const N_FM6_ALG[] = {"PAT", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13",
                                        "14", "15", "16", "17", "18", "19", "20", "21", "22", "23", "24", "25",
                                        "26", "27", "28", "29", "30", "31", "32", 0};
/* {ALG, FB, MLVL, MRAT, MEG, VMOD, DTUN, unused}: neutral macros, DTUN on the pad. */
#define FM6_PR(name, mono, fx) {name, {0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 127, 0}, 0, mono, fx}
static const preset_t FM6_PRESETS[] = {
    {"TINE EP", {0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 127, 0}, 0, 0, FX(0, 45, 25, 35)},
    {"BELL", {0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 127, 0}, 0, 0, FX(0, 10, 30, 70)},
    {"FM BASS", {0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 127, 0}, 0, 1, FX(0, 0, 10, 10)},
    {"BRASS", {0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 127, 0}, 0, 0, FX(0, 25, 20, 40)},
    {"PAD", {0, 0, 0, 0, 0, 0, 30, 0}, {0, 0, 127, 0}, 0, 0, FX(0, 60, 30, 70)},
    {"MARIMBA", {0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 127, 0}, 0, 0, FX(0, 0, 25, 40)},
    {"ORGAN", {0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 127, 0}, 0, 0, FX(10, 40, 0, 30)},
    {"PLUCK", {0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 127, 0}, 0, 0, FX(0, 20, 35, 30)},
    FM6_PR("DX TINE", 0, FX(0, 50, 25, 35)),
    FM6_PR("BRASS SECT", 0, FX(0, 20, 20, 40)),
    FM6_PR("SOLID BASS", 1, FX(0, 0, 10, 10)),
    FM6_PR("BELLS", 0, FX(0, 0, 30, 70)),
    FM6_PR("DX MARIMBA", 0, FX(0, 0, 25, 40)),
    FM6_PR("CLAVINET", 0, FX(10, 20, 30, 20)),
    FM6_PR("DRAWBARS", 0, FX(10, 40, 0, 30)),
    FM6_PR("STRINGS", 0, FX(0, 60, 30, 70)),
    FM6_PR("GLASS PAD", 0, FX(0, 60, 40, 80)),
    FM6_PR("SYNC LEAD", 1, FX(10, 20, 40, 30)),
    FM6_PR("HARP", 0, FX(0, 30, 30, 60)),
    FM6_PR("KALIMBA", 0, FX(0, 0, 35, 45)),
    FM6_PR("FLUTE", 1, FX(0, 20, 30, 50)),
    FM6_PR("STEEL DRUM", 0, FX(0, 0, 30, 40)),
    FM6_PR("SAW BASS", 1, FX(20, 0, 10, 10)),
    FM6_PR("TUBULAR", 0, FX(0, 0, 30, 80)),
    FM6_PR("INIT VOICE", 0, FX(0, 0, 0, 0)),          /* the DX7's init voice, dry (after F24: the stores keep the
                                                         * factory's numbers) */
};
#undef FM6_PR
_Static_assert(FM6_NROM == 16u, "FM6: the presets name F9..F24");
_Static_assert(NELEM(FM6_PRESETS) == FM6_NFAC + 1u, "FM6: F1..F24, then INIT VOICE");

static const engine_t ENG_FM6 = {
    .name = "FM6",
    .page_title = {"OPS", "PATCH"},
    .edit = {
        {"ALG", F_INT, 0, 32, 0, N_FM6_ALG, 0},
        {"FB", F_INT, -7, 7, 0, 0, 0},
        {"MLVL", F_BIPCT, -64, 63, 0, 0, 0},
        {"MRAT", F_INT, -16, 16, 0, 0, 0},
        {"MEG", F_BIPCT, -64, 63, 0, 0, 0},
        {"VMOD", F_INT, -7, 7, 0, 0, 0},
        {"DTUN", F_PCT, 0, 127, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0},
    },
    .presets = FM6_PRESETS,
    .npresets = NELEM(FM6_PRESETS),
    .note_on = fm6_note_on,
    .render = fm6_render,
    .knob = {P_E2, P_E3, P_E4, P_E6},
    .poly = NVOICE,                                      /* Dexed's 16: a voice takes one budget unit */
    .block = fm6_block,
    .ownenv = 1,
    .done = fm6_done,
    .alloc = fm6_alloc,
    .legato = fm6_legato,
    .mono_key = fm6_key,
    .post = fm6_post,
};
