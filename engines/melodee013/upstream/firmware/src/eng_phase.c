/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Phase distortion. The waveforms are a C port of the oscillator of
 * CrispyZebra (Leo Kuroshita, GPL-3.0, github.com/hugelton/CrispyZebra), the
 * author's own phase-distortion core: a -cos table read through a bent phase whose
 * bend is DCW. WAVE2 alternates with WAVE every other cycle. A second
 * line (DTN) can be mixed or ring-modulated; SUB adds a sine an octave down.
 * Native rate/target envelopes run separately for DCO, DCW and DCA on both
 * lines. The existing four-knob controls construct three-point CZ envelopes;
 * they retain Melodee's time scale (not a measured CZ-1 rate calibration).
 * See README.md (PHASE) for controls and remaining fidelity limits. */
static const char *const N_PD_WAVE[] = {"SAW", "SQR", "PLS", "DSIN", "SPLS", "RSAW", "RTRI", "RTRP"};
static const char *const N_PD_WAVE2[] = {"-", "SAW", "SQR", "PLS", "DSIN", "SPLS", "RSAW", "RTRI", "RTRP"};
static const char *const N_PD_LINE[] = {"MIX", "RING"};
#include "cz_patch.h"
static const char *const N_PD_ENV[] = {"LINK", "SPLIT"};
static cz_patch_t cz_patch[NTRK] __attribute__((section(".pool")));
static void cz_init(void) { for (uint32_t k = 0; k < NTRK; k++) cz_patch_init(cz_patch[k].raw); }

/* Eight rate/target points, with explicit sustain/end. Rates are Q24 per
 * control tick. No exponential asymptote and no extra release after END. */
typedef struct { uint32_t rate[8]; int32_t level[8]; uint8_t sustain, end; } cz_env_def_t;
typedef struct { int32_t level; uint8_t stage, gate; } cz_env_t;
typedef struct { cz_env_t eg[2][3]; uint32_t vib_phase, vib_ticks, noise; } cz_voice_t;
typedef struct { cz_voice_t v[NPOLY]; } cz_part_t;
static cz_part_t *cz_part(uint32_t part);              /* engines.c: shared runtime pool */
static cz_voice_t *cz_voice(track_t *t, voice_t *v)
{
    return &cz_part((uint32_t)(t - trk) % NPART)->v[(uint32_t)(v - t->v) % NPOLY];
}

static int32_t cz_env_tick(cz_env_t *e, const cz_env_def_t *d, uint32_t gate)
{
    uint32_t left = 65536u;
    if (e->gate && !gate && d->sustain <= d->end && e->stage <= d->sustain)
        e->stage = d->sustain + 1u;                    /* release even during attack */
    e->gate = (uint8_t)gate;
    /* Carry unused tick time across points, including zero-distance points.
     * A short segment must not acquire an extra 32-sample delay. */
    for (uint32_t k = 0; k < 8u && e->stage <= d->end; k++) {
        uint32_t st = e->stage, rate = d->rate[st];
        int32_t target = st == d->end ? 0 : d->level[st];
        int32_t delta = target - e->level;
        uint32_t dist = (uint32_t)(delta < 0 ? -delta : delta);
        uint32_t step = (uint32_t)mulq16((int32_t)rate, left);
        if (dist > step) {
            e->level += delta < 0 ? -(int32_t)step : (int32_t)step;
            break;
        }
        e->level = target;
        if (gate && st == d->sustain) break;
        e->stage++;
        if (dist && rate) {
            uint32_t used = (uint32_t)(((uint64_t)dist << 16) / rate);
            left = used < left ? left - used : 0;
        }
        if (!left) break;
    }
    return e->level;
}

/* A, D and R are speeds across the entire level range: shorter excursions
 * take proportionally less time, as the CZ manual's RATE diagrams specify.
 * Keep stored Melodee times compatible instead of inventing a hardware table. */
static void cz_env_adsr(cz_env_def_t *d, const int16_t *p, uint32_t base, int32_t peak)
{
    memset(d, 0, sizeof *d);
    d->rate[0] = ENV_LIN[p[base] & 127];
    d->rate[1] = ENV_LIN[p[base + 1u] & 127];
    d->rate[2] = ENV_LIN[p[base + 3u] & 127];
    d->level[0] = peak;
    d->level[1] = (peak / 127) * p[base + 2u];
    d->sustain = 1;
    d->end = 2;
}

static void cz_defs(track_t *t, cz_env_def_t d[2][3])
{
    const int16_t *p = t->p;
    int split = p[P_E7] != 0;
    for (uint32_t l = 0; l < 2u; l++) {
        uint32_t wave = split ? (l ? P_FM2_ATK : P_FM1_ATK) : P_ATK;
        uint32_t amp = split && l ? P_FM3_ATK : P_ATK;
        cz_env_adsr(&d[l][0], p, split ? P_FM4_ATK : P_ATK, 1 << 24);
        cz_env_adsr(&d[l][1], p, wave, 1 << 24);
        cz_env_adsr(&d[l][2], p, amp, 1 << 24);
    }
}

static int phase_done(track_t *t, voice_t *v)
{
    cz_voice_t *c = cz_voice(t, v);
    int line2 = t->p[P_E4] != 0 || t->p[P_E5] != 0;
    return c->eg[0][2].stage > 2u && (!line2 || c->eg[1][2].stage > 2u);
}

/* Retired FM4 parameter storage is shared with PHASE's split envelopes. The
 * ids, ranges and defaults remain compatible with existing projects/presets. */
static const param_desc_t CZ_ED[20] = {
#define CZ_ROW(n) {"A" n, F_TIME, 0,127,0,0,0}, {"D" n, F_TIME,0,127,0,0,0}, \
    {"S" n,F_PCT,0,127,127,0,0}, {"R" n,F_TIME,0,127,0,0,0}, {"LV" n,F_PCT,0,127,127,0,0}
    CZ_ROW("W1"), CZ_ROW("W2"), CZ_ROW("A2"), CZ_ROW("P"),
#undef CZ_ROW
};

static inline int32_t pd_cos(uint32_t ph16)             /* -cos, Q15, from the 16-bit PD phase */
{
    return -sine_i(((ph16 & 0xFFFFu) << 16) + 0x40000000u);
}

/* one wave's bend for a block: break points and the slopes of the bent phase
 * (Q16 reciprocals), so a sample costs a multiply instead of a divide (the
 * bent phase comes out the same or 1 lower in 65536) */
typedef struct {
    uint32_t w, x, peak, rez;
    uint32_t k0, k1;
} pd_t;

static inline uint32_t pd_slope(uint32_t span, uint32_t len) { return (span << 16) / (len ? len : 1u); }

static void pd_setup(pd_t *b, uint32_t w, uint32_t dcw)
{
    uint32_t x;
    b->w = w;
    switch (w) {
    case 0:                                                  /* SAW */
        x = 32768u - ((dcw * 31120u) >> 16);
        b->k0 = pd_slope(32768u, x);
        b->k1 = pd_slope(32767u, 65535u - x);
        break;
    case 1:                                                  /* SQUARE */
        x = 32768u - ((dcw * 31120u) >> 16);
        b->k0 = pd_slope(32768u, x);
        b->k1 = pd_slope(32767u, x);
        break;
    case 2:                                                  /* PULSE */
        x = 65535u - ((dcw * 63487u) >> 16);
        b->k0 = pd_slope(65535u, x);
        break;
    case 3:                                                  /* DOUBLE SINE */
        x = 65535u - ((dcw * 49151u) >> 16);
        b->k0 = pd_slope(65535u, x);
        b->k1 = pd_slope(65535u, 65535u - x);
        break;
    case 4:                                                  /* SAW-PULSE */
        x = 65535u - ((dcw * 32767u) >> 16);
        b->peak = (x * (32768u + ((dcw * 29491u) >> 16))) >> 16;
        if (!b->peak)
            b->peak = 1;
        b->k0 = pd_slope(32768u, b->peak);
        b->k1 = pd_slope(32767u, x - b->peak);
        break;
    default:                                                 /* RESONANCE */
        x = 0;
        b->rez = (dcw * 7u);
        b->peak = dcw;
        break;
    }
    b->x = x;
}

/* one sample at phase ph (16 bit): bipolar Q15 */
static __attribute__((noinline)) int32_t pd_wave(const pd_t *b, uint32_t ph)
{
    uint32_t pd, x = b->x;
    switch (b->w) {
    case 0:                                                  /* SAW */
        pd = ph < x ? (ph * b->k0) >> 16 : 32768u + (((ph - x) * b->k1) >> 16);
        break;
    case 1:                                                  /* SQUARE */
        if (ph < x)
            pd = (ph * b->k0) >> 16;
        else if (ph < 32768u)
            pd = 32768u;
        else if (ph < 32768u + x)
            pd = 32768u + (((ph - 32768u) * b->k1) >> 16);
        else
            pd = 65535u;
        break;
    case 2:                                                  /* PULSE */
        pd = ph < x ? (ph * b->k0) >> 16 : 65535u;
        break;
    case 3:                                                  /* DOUBLE SINE */
        pd = ph < x ? (ph * b->k0) >> 16 : ((ph - x) * b->k1) >> 16;
        break;
    case 4:                                                  /* SAW-PULSE */
        if (ph >= x)
            pd = 65535u;
        else if (ph < b->peak)
            pd = (ph * b->k0) >> 16;
        else
            pd = 32768u + (((ph - b->peak) * b->k1) >> 16);
        break;
    default: {                                               /* hard-synced cosine with a window */
        uint32_t rp = ph + ((ph * (b->rez >> 3)) >> 13), win;
        int32_t core = pd_cos(rp);
        if (b->w == 5)
            win = 65535u - ph;
        else if (b->w == 6)
            win = ph < 32768u ? ph << 1 : (65535u - ph) << 1;
        else
            win = ph < 32768u ? 65535u : (65535u - ph) << 1;
        /* Window the unipolar carrier, then return to bipolar. Returning to
         * the baseline at sync avoids a discontinuity when the resonant core
         * has a fractional number of cycles. Blend with the undistorted cosine
         * so zero DCW has the same pitch and level as the other five waves. */
        int32_t shaped = (int32_t)(((uint32_t)(core + 32767) * win) >> 16) - 32767;
        int32_t plain = pd_cos(ph);
        return clamp(plain + mulq15((shaped - plain) >> 1, (int32_t)b->peak), -32767, 32767);

    }
    }
    return pd_cos(pd);
}

static void phase_note_on(track_t *t, voice_t *v)
{
    cz_voice_t *c = cz_voice(t, v);
    memset(c, 0, sizeof *c);
    for (uint32_t l = 0; l < 2u; l++)
        for (uint32_t e = 0; e < 3u; e++) c->eg[l][e].gate = 1;
    v->ph[0] = v->ph[1] = v->ph[2] = 0;
    v->s[0] = v->s[1] = v->s[4] = 0;
    if (!voice_was) v->s[2] = v->s[3] = 0;
    c->noise = 0x6D2B79F5u ^ (uint32_t)(v - t->v) * 0x9E3779B9u;
}


static __attribute__((noinline)) void cz_prepare(track_t *t, voice_t *v, uint32_t n, const vmod_t *m,
    pd_t b[2][2], uint32_t inc[2], int32_t gain[2][2], int32_t gain_step[2])
{
    const int16_t *p = t->p;
    cz_voice_t *c = cz_voice(t, v);
    cz_env_def_t defs[2][3];
    uint32_t w1 = (uint32_t)p[P_E0] & 7u, w2 = (uint32_t)p[P_E1];
    int32_t det = p[P_E4];
    int32_t depth[2], pitch[2];

    cz_defs(t, defs);
    /* Two resonant windows cannot be combined on the original front panel.
     * Preserve arbitrary old stored choices safely using the first window. */
    if (w1 >= 5u && w2 >= 6u) w2 = w1 + 1u;
    for (uint32_t l = 0; l < 2u; l++) {
        uint32_t wb = l ? P_FM2_ATK : P_FM1_ATK;
        int32_t wl = p[P_E7] ? p[wb + 4u] : 127;
        int32_t al = p[P_E7] && l ? p[P_FM3_LEVEL] : 127;
        gain[l][0] = c->eg[l][2].level >> 9;
        pitch[l] = cz_env_tick(&c->eg[l][0], &defs[l][0], v->gate) >> 9;
        depth[l] = cz_env_tick(&c->eg[l][1], &defs[l][1], v->gate) >> 9;
        gain[l][1] = cz_env_tick(&c->eg[l][2], &defs[l][2], v->gate) >> 9;
        for (uint32_t k = 0; k < 2u; k++)
            gain[l][k] = mulq15(gain[l][k], v->vel * 258) * al / 127;
        gain_step[l] = (gain[l][1] - gain[l][0]) * 256 / (int32_t)n;
        /* Pitch ENV is independent of DCW/DCA. The legacy ENV DEST pitch
         * amount remains about +/-12 semitones; split mode adds its own depth. */
        int32_t amount = p[P_ED_PIT];
        if (p[P_E7]) amount = amount * p[P_FM4_LEVEL] / 127;
        int32_t note = clamp(m->pitch16 + ((pitch[l] * amount * 3) >> 15) + (l ? det * 16 / 100 : 0), 0, 2047);
        inc[l] = tuned_pitch_inc((uint32_t)note);
        int32_t fine = m->fine + (l ? (det * 16 % 100) * 2367 / 16000 : 0);
        inc[l] += (uint32_t)((int32_t)(inc[l] >> 12) * fine);
        int32_t dep = (p[P_E2] << 8) + m->cutoff + mulq15(depth[l], p[P_E3] * 256);
        dep += (depth[l] * p[P_ED_FLT]) >> 7;
        dep += m->shape - (64 << 8) + ((depth[l] * p[P_ED_SHP]) >> 7);
        uint32_t dcw = (uint32_t)clamp(dep, 0, 127 << 8) * 65535u / (127u << 8);
        dcw = dcw * (uint32_t)wl / 127u;
        pd_setup(&b[l][0], w1, dcw);
        pd_setup(&b[l][1], w2 ? w2 - 1u : w1, dcw);
    }
}

static inline __attribute__((always_inline)) int32_t cz_line_sample(
    const pd_t b[2], uint32_t *ph, uint32_t inc, int32_t *tg, int32_t *dc, int32_t amp)
{
    uint32_t old = *ph;
    int32_t raw = pd_wave(*tg ? &b[1] : &b[0], *ph >> 16);
    *ph += inc;
    if (*ph < old) *tg ^= 1;
    /* AC-couple before DCA; preserve asymmetric peaks without clipping. */
    *dc += raw - (*dc >> 10);
    raw = (raw - (*dc >> 10)) >> 1;
    return mulq15(raw, amp);
}

static int32_t phase_env_source(track_t *t, voice_t *v)
{
    (void)t;
    return v->s[4];                                      /* cached unscaled DCA1, Q15 */
}

static void phase_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    int line2 = p[P_E4] != 0 || p[P_E5], ring = p[P_E5];
    uint32_t inc[2], ph[2] = {v->ph[0], v->ph[1]}, phsub = v->ph[2];
    int32_t tg[2] = {v->s[0], v->s[1]}, dc[2] = {v->s[2], v->s[3]};
    int32_t gain[2][2], gain_step[2];
    pd_t b[2][2];
    if (!n) return;
    cz_prepare(t, v, n, m, b, inc, gain, gain_step);
    v->s[4] = cz_voice(t, v)->eg[0][2].level >> 9;
    for (uint32_t i = 0; i < n; i++) {
        int32_t amp = gain[0][0] + ((gain_step[0] * (int32_t)i) >> 8);
        int32_t sample = cz_line_sample(b[0], &ph[0], inc[0], &tg[0], &dc[0], amp);
        if (line2) {
            int32_t amp2 = gain[1][0] + ((gain_step[1] * (int32_t)i) >> 8);
            int32_t s2 = cz_line_sample(b[1], &ph[1], inc[1], &tg[1], &dc[1], amp2);
            sample = ring ? mulq15(sample, s2) : (sample + s2) >> 1;
        }
        if (p[P_E6]) {
            phsub += inc[0] >> 1;
            sample += mulq15(mulq15(osc_sine(phsub) >> 1, p[P_E6] * 200), amp);
        }
        out[i] += voice_amp(sample, m, i) * 4;
    }
    v->ph[0] = ph[0]; v->ph[1] = ph[1]; v->ph[2] = phsub;
    v->s[0] = tg[0]; v->s[1] = tg[1]; v->s[2] = dc[0]; v->s[3] = dc[1];
}

static const preset_t PHASE_PRESETS[] = {
    {"BRASS", {0, 0, 30, 90, 0, 0, 0, 0}, {8, 70, 90, 40}, 0, 0, FX(0, 20, 20, 40), PAT(6)},
    {"ORGAN", {3, 0, 40, 0, 0, 0, 0, 0}, {0, 127, 127, 30}, 0, 0, FX(0, 40, 0, 30), PAT(6)},
    {"STRING", {0, 4, 50, 40, 12, 0, 0, 0}, {40, 90, 100, 70}, 0, 0, FX(0, 50, 20, 60), PAT(5)},
    {"RESO", {5, 0, 60, 60, 0, 0, 0, 0}, {0, 70, 30, 60}, 0, 0, FX(0, 0, 40, 40), PAT(1)},
    {"BELL", {6, 0, 80, 50, 0, 0, 0, 0}, {0, 95, 0, 90}, 0, 0, FX(0, 0, 30, 70), PAT(7)},
    {"WIRE", {4, 7, 70, 40, 7, 0, 0, 0}, {10, 80, 80, 60}, 0, 0, FX(15, 30, 30, 40), PAT(4)},
};

static const engine_t ENG_PHASE = {
    .name = "PHASE",
    .page_title = {"PHS", "LINE"},
    .edit = {
        {"WAVE", F_ENUM, 0, 7, 0, N_PD_WAVE, 0},
        {"WAVE2", F_ENUM, 0, 8, 0, N_PD_WAVE2, 0},
        {"DCW", F_PCT, 0, 127, 60, 0, 0},
        {"ENV", F_PCT, 0, 127, 64, 0, 0},
        {"DTN", F_INT, 0, 127, 0, 0, "ct"},
        {"LINE", F_ENUM, 0, 1, 0, N_PD_LINE, 0},
        {"SUB", F_PCT, 0, 127, 0, 0, 0},
        {"EG", F_ENUM, 0, 1, 0, N_PD_ENV, 0},
    },
    .presets = PHASE_PRESETS,
    .npresets = NELEM(PHASE_PRESETS),
    .ownenv = 1, .done = phase_done, .keep = 0x0fu,
    .note_on = phase_note_on, .render = phase_render,
    .knob = {P_E2, P_E3, P_E4, P_REL},
};
