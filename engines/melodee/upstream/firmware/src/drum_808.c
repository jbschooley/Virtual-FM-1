/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modifications Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* DRUM's KIT 808 (eng_drum.c): an analog kit modelled on the TR-808's voice circuits. A lane plays one circuit at a
 * time (dr8_t, in each lane): the instrument of the note struck, from the 808's own
 * General MIDI map (DR_GM: three toms and three congas, maracas, claves, cowbell, cymbal), else the 808's nearest to
 * the drum the other kits play there. A hit kicks the circuit again while it may still ring; a closed hat chokes
 * the open one.
 *
 * The models follow the circuits (K. J. Werner et al., "A physically-informed, circuit-bendable, digital model of
 * the Roland TR-808 bass drum circuit", DAFx-14, and "The TR-808 cymbal", ICMC-SMC 2014; the service notes'
 * frequencies and decay times):
 *   bridged-T voices (BD, SD, toms, congas, rim shot, claves): a resonator (Chamberlin state variable, Q30) kicked
 *     by the trigger pulse through the BD's pulse shaper (a low shelf, its falling edge clipped at a diode drop).
 *     BD: 49.4 Hz; for the first ~6 ms (the attack envelope grounds R165) it rings near 130 Hz, and while the
 *     network swings far one way (Q43 leaks) it rings higher: the "pitch sigh" of loud hits. TONE is a passive
 *     low-pass, the output a 6.8 Hz high-pass. SD: 173 + 336 Hz (the revised board), TONE mixes them, SNAPPY is
 *     the level of high-passed (2.7 kHz) noise. Toms and congas fall slightly in pitch as they decay (the toms add
 *     a little low-passed noise). Rim shot: 455 + 1667 Hz through a swing VCA; claves 2.5 kHz.
 *   noise voices: clap (1.1 kHz band-pass, a sawtooth envelope's three fast bursts and a longer last one, plus
 *     the "reverb" envelope), maracas (high-passed, a short rise and fall).
 *   metal voices: six free-running square oscillators (205.3 .. 800 Hz, one HD14584; each lane its own) for the
 *     cowbell (540 + 800 Hz, 880 Hz band-pass) and, through band-passes at 3.44 and 7.1 kHz, the cymbal (two
 *     bands, each its envelope and high-pass, then the output's +6 dB / octave) and the hi-hats. Their swing-type
 *     VCAs clip the band-passed squares hard, the envelope sets how far: the metallic fizz.
 * Velocity is the trigger level (velocity 127 = 13.5 V, the accent's top): the shaper, the sigh and the swing VCAs
 * make loud hits brighter, not only louder. DRUM's knobs, read every block, are the 808's: TUNE the BD and the
 * toms and congas (at the hit: SD, rim shot, claves), TONE the BD's and SD's tone, DECY the BD's, the open hat's
 * and the cymbal's decay, SNAP the snappy, ACC how far velocity moves the level too (drum_amp; the circuits alone
 * make a hit at 127 only ~2 dB louder than at 64), KICK ROUND the BD without its attack, DRV as on every kit. Ported from Melodee's drum part of before 1.0 (drums.c). */
enum { DR_BD, DR_SD, DR_LT, DR_MT, DR_HT, DR_LC, DR_MC, DR_HC, DR_RS, DR_CL, DR_CP, DR_MA, DR_CB, DR_CY,
       DR_OH, DR_CH, DR_N };
#define DR_X 0xFFu
static const uint8_t DR_GM[47] = {               /* GM percussion keys 35..81 -> instrument */
    DR_BD, DR_BD, DR_RS, DR_SD, DR_CP, DR_SD, DR_LT, DR_CH,       /* 35 kick 2, kick, side stick .. */
    DR_LT, DR_CH, DR_MT, DR_OH, DR_MT, DR_HT, DR_CY, DR_HT,       /* 43 */
    DR_CY, DR_CY, DR_CY, DR_MA, DR_CY, DR_CB, DR_CY, DR_X,        /* 51 ride, china, bell, tamb. .. */
    DR_CY, DR_HC, DR_MC, DR_HC, DR_MC, DR_LC, DR_X, DR_X,         /* 59 ride 2, bongos, congas */
    DR_X, DR_X, DR_MA, DR_MA, DR_X, DR_X, DR_X, DR_X,             /* 67 cabasa, maracas */
    DR_CL, DR_CL, DR_CL, DR_X, DR_X, DR_X, DR_X,                  /* 75 claves, wood blocks */
};
static const uint8_t DR_OF_DVT[DVT_COUNT] = {    /* fallback GM role (DVT_*) -> the 808 instrument */
    DR_BD, DR_BD, DR_SD, DR_CP, DR_CH, DR_OH, DR_MT, DR_MC, DR_RS, DR_CL, DR_CB, DR_CY,
};

#define DR8_OUT 6800                             /* the kit's output into DRUM's (Q12): the 808 BD as loud as PUNCH */
#define DR_V1 16384                              /* 1 V of the trigger and the pulse shaper */
#define DR_P1 44u                                /* the trigger pulse: 1 ms */
#define DR_PEND 200u                             /* the shaped pulse has settled */
#define DR_MS(ms) ((uint32_t)(ms) * 441u / 10u)  /* ms -> samples */
#define DR_TAU(ms) (DR_MS(ms) * 10u / 46u)       /* time constant of a decay reaching -40 dB at ms */
#define DR_LVL(vel) ((int32_t)(vel) * 132104)    /* envelope level, Q24 (velocity 127 = 1.0) */
#define DR_NOISY ((1u << DR_SD) | (1u << DR_LT) | (1u << DR_MT) | (1u << DR_HT) | (1u << DR_CP) | (1u << DR_MA))
#define DR_BANDS ((1u << DR_CY) | (1u << DR_OH) | (1u << DR_CH))
#define DR_METAL (DR_BANDS | (1u << DR_CB))

typedef struct { int32_t e, k; } dre_t;          /* decay: value Q24, factor per block Q16 */
typedef struct { int16_t a1, a2, a3, k; } drf_t; /* trapezoidal SVF (A. Simper), Q13 */

typedef struct {
    uint32_t t;                  /* samples since the hit */
    int32_t v;                   /* its trigger level (DR_V1 units) */
    int32_t px;                  /* pulse shaper state */
    int32_t f, q;                /* resonator: 2 sin(pi fc / FS) and damping, Q30 */
    int32_t f2, q2;              /* the second resonator (SD, RS) */
    int32_t lp, bp, lp2, bp2;    /* their states */
    int32_t s[4];                /* filter / envelope states */
    int32_t x[5];                /* the metal voices' output filters (cymbal: two high-passes, its level buffer) */
    int32_t m1[2], m2[2];        /* the metal band-passes */
    dre_t e[2];                  /* envelopes */
    int32_t color;               /* edited timbre low-pass; neutral path bypasses it */
    uint32_t ph[6];              /* the six square oscillators (running while the lane sounds) */
    int32_t decay;               /* decay offset, zero preserves original circuit constants */
    int32_t pitch;               /* per-sound/per-hit pitch in semitones / 16 */
    int32_t nst;                 /* the noise generator */
    uint8_t ins;                 /* DR_*: the circuit struck */
    uint8_t on;                  /* it sounds */
    uint8_t choke;               /* the open hat is being discharged by a closed one */
    uint8_t pad;
} dr8_t;

/* the lane's scratch: its noise, the oscillators' sum (band 1), band 2 through its swing VCA, the cowbell's two */
static int32_t dr_nz[CTL] __attribute__((section(".pool")));
static int32_t dr_m1[CTL] __attribute__((section(".pool")));
static int32_t dr_m2[CTL] __attribute__((section(".pool")));
static int32_t dr_cbx[CTL] __attribute__((section(".pool")));

/* ------------------------------------------------------------ helpers --- */
static int32_t dr_freq(uint32_t hz16)            /* 2 sin(pi fc / FS), Q30; fc in Hz * 16 (<= 4 kHz) */
{
    int32_t x = (int32_t)(hz16 * 4781u);         /* pi fc / FS, Q30 */
    int32_t x3 = (int32_t)(((((int64_t)x * x) >> 30) * x) >> 30);
    return 2 * x - x3 / 3;                       /* 2 sin x ~ 2 x - x^3 / 3 */
}
#define DR_F(hz) dr_freq((uint32_t)((hz) * 16))

/* damping of a resonator ringing with time constant tau samples: f q = 2 / tau, q = 2^61 / (f tau) in
 * Q30. No 64-bit division on the target: f tau normalized to m 2^e (m in [2^30, 2^31)), 2^47 / m */
static int32_t dr_damp(int32_t f, uint32_t tau)
{
    uint64_t p = (uint64_t)(uint32_t)f * (tau ? tau : 1u);
    int32_t e = 0;
    uint32_t r;
    if (!p)
        return 3 << 29;
    while (p >= (1u << 31)) {
        p >>= 1;
        e++;
    }
    while (p < (1u << 30)) {
        p <<= 1;
        e--;
    }
    r = 0xFFFFFFFFu / (uint32_t)(p >> 15);       /* 2^47 / m */
    if (e >= 14)
        return (int32_t)(r >> (e - 14));
    return r >= (3u << 29) >> (14 - e) ? 3 << 29 : (int32_t)(r << (14 - e));
}

static int32_t dr_lp1(uint32_t hz)               /* one-pole low-pass coefficient ~ 1 - exp(-2 pi fc / FS), Q14 */
{
    int32_t w = (int32_t)((hz * 2390u) >> 10);   /* 2 pi fc / FS, Q14 */
    return w * 16384 / (16384 + w / 2);
}

static uint32_t dr_exp(uint32_t base, int32_t v, int32_t oct)   /* base * 2^(v / oct) */
{
    return (uint32_t)(((uint64_t)base * pow2_q16(v * 192 / oct)) >> 16);
}

/* the level of a hit at velocity vel (Q15): 1 at 127, down to 1 - ACC / 127 at 0 */
static int32_t dr8_level(uint32_t vel, int32_t acc)
{
    return 32767 - clamp(acc, 0, 127) * (127 - (int32_t)(vel > 127u ? 127u : vel)) * 32767 / (127 * 127);
}

static int32_t dr_tuned(int32_t f, int32_t tune) { return (int32_t)(((int64_t)f * pow2_q16(tune)) >> 16); }

/* one Chamberlin state-variable step, band-pass out; f, q Q30, rounded (no DC creep) */
static inline int32_t dr_res(int32_t *lp, int32_t *bp, int32_t f, int32_t q, int32_t x)
{
    int32_t b = *bp, l = *lp + (int32_t)(((int64_t)f * b + (1 << 29)) >> 30), h;
    h = x - l - (int32_t)(((int64_t)q * b + (1 << 29)) >> 30);
    *lp = l;
    *bp = b + (int32_t)(((int64_t)f * h + (1 << 29)) >> 30);
    return *bp;
}

/* trapezoidal SVF: band-pass (gain Q at fc) or high-pass; signals within +-2^16 */
static inline int32_t drf_bp(const drf_t *c, int32_t x, int32_t *s)
{
    int32_t v3 = x - s[1], v1 = (c->a1 * s[0] + c->a2 * v3) >> 13, v2 = s[1] + ((c->a2 * s[0] + c->a3 * v3) >> 13);
    s[0] = 2 * v1 - s[0];
    s[1] = 2 * v2 - s[1];
    return v1;
}
static inline int32_t drf_hp(const drf_t *c, int32_t x, int32_t *s)
{
    int32_t v3 = x - s[1], v1 = (c->a1 * s[0] + c->a2 * v3) >> 13, v2 = s[1] + ((c->a2 * s[0] + c->a3 * v3) >> 13);
    s[0] = 2 * v1 - s[0];
    s[1] = 2 * v2 - s[1];
    return x - ((c->k * v1) >> 13) - v2;
}
/* the fixed filters: g = tan(pi fc / FS), k = 1 / Q, a1 = 1 / (1 + g (g + k)), a2 = g a1, a3 = g a2 */
static const drf_t DRF_SDN = {6206, 1231, 244, 11587};          /* SD noise high-pass 2749 Hz, Q 0.71 */
static const drf_t DRF_CP = {7814, 636, 52, 4201};              /* clap band-pass 1140 Hz, Q 1.95 */
static const drf_t DRF_MA = {4921, 1831, 681, 11587};           /* maracas high-pass 5 kHz */
static const drf_t DRF_CB = {8055, 506, 32, 1707};              /* cowbell band-pass 880 Hz, Q 4.8 */
static const drf_t DRF_M1 = {7149, 1788, 447, 2731};            /* metal band-pass 1: 3440 Hz, Q 3 */
static const drf_t DRF_M2 = {5360, 2969, 1644, 3277};           /* metal band-pass 2: 7100 Hz, Q 2.5 */
static const drf_t DRF_CYA = {6050, 1313, 285, 11587};          /* cymbal band 1 high-pass 3 kHz */
static const drf_t DRF_CYB = {3110, 2886, 2677, 6827};          /* cymbal band 2 high-pass 10.5 kHz, Q 1.2 */
static const drf_t DRF_HAT = {4449, 2423, 1320, 8192};          /* hi-hat high-pass 7 kHz, Q 1 */
static const drf_t DRF_RS = {7558, 431, 25, 11587};             /* rim shot high-pass 800 Hz */

static int32_t dr_blk(uint32_t tau)              /* exp(-CTL / tau), Q16; tau in samples */
{
    uint32_t x, x2;
    if (tau < 2u * CTL)
        tau = 2u * CTL;
    x = ((uint32_t)CTL << 16) / tau;             /* <= 0.5 */
    x2 = (x * x) >> 16;
    return (int32_t)(65536u - x + (x2 >> 1) - ((x2 * x) >> 16) / 6u);
}
static void dre_hit(dre_t *e, int32_t level, uint32_t tau)   /* charged to level (Q24) unless above */
{
    if (level > e->e)
        e->e = level;
    e->k = dr_blk(tau);
}
static inline int32_t dre_next(dre_t *e, int32_t *e0)   /* this block's start (*e0) and its step << CTL_LOG2 */
{
    int32_t a = e->e;
    e->e = (int32_t)(((int64_t)a * e->k) >> 16);
    *e0 = a;
    return e->e - a;
}
#define DRE_AT(e0, de, i) (((e0) + (((de) * (int32_t)(i)) >> CTL_LOG2)) >> 9)   /* Q15 */
#define DRE_LOW (1 << 11)                        /* an envelope below -78 dB: done */
#define DR_QUIET 8                               /* a block peak below -72 dB (after the hit): done */
#define DR_LIFE DR_MS(6000)                      /* and no hit longer than this (rounding limit cycles) */

/* the trigger pulse (1 ms at the trigger level) through the BD's pulse shaper: a low shelf passes
 * the edges and 0.044 of the level; D53 keeps the falling edge above a diode drop (0.71 V) */
static inline int32_t dr_pulse(dr8_t *d)
{
    int32_t p = d->t < DR_P1 ? d->v : 0, s;
    d->px += ((p - d->px) * 6650) >> 15;         /* 0.1 ms */
    s = p - d->px + ((p * 1442) >> 15);
    if (s < 0)                                   /* -> -0.71 V (727 = 0.71 V / 16) */
        s = s * 727 / (727 - (s >> 4));
    return s;
}

static uint32_t dr8_tau(const dr8_t *d,uint32_t tau)
{return d->decay?(uint32_t)(((uint64_t)tau*(uint32_t)pow2_q16(d->decay*192/24))>>16):tau;}
static drf_t dr8_filter(const drf_t *base,int32_t pitch)
{
    if(!pitch)return *base;
    float g=(float)base->a2/(float)base->a1;
    g=fm_clampf(g*fm_exp2f((float)pitch/192.0f),0.001f,8.0f);
    float k=(float)base->k/8192.0f,a1=1.0f/(1.0f+g*(g+k));
    drf_t c={(int16_t)(a1*8192.0f),(int16_t)(g*a1*8192.0f),(int16_t)(g*g*a1*8192.0f),base->k};return c;
}

static void dr_off(dr8_t *d)                     /* silent: the circuit back to rest (the oscillators and the
                                                  * noise run on) */
{
    memset(d, 0, (uint32_t)((uint8_t *)d->ph - (uint8_t *)d));
    d->on = d->choke = 0;
}

/* ---------------------------------------------------- bridged-T voices --- */
#define DR_BD_G 6570                             /* output gains (Q16) */
#define DR_SIGH0 4000                            /* the sigh: above this swing the network rings higher, */
#define DR_SIGHS 1                               /* by (swing - DR_SIGH0) >> DR_SIGHS (Q15) */
#define DR_LEAK 7                                /* the shaped pulse past the network (the click) >> */

static __attribute__((noinline)) void dr_bd(dr8_t *d, const int16_t *p, int32_t *acc, uint32_t n)
{
    int32_t f0, q, g, c, tune = (p[P_E1] - 64) * 3 + d->pitch, pk = 0, round = p[P_E6] > 0;
    uint32_t i;
    f0 = dr_tuned(DR_F(49.4), tune);
    q = dr_damp(f0, DR_TAU(dr_exp(300u, p[P_E3] - 64, 24)));           /* DECY: 47 ms .. 1.8 s, 300 ms */
    g = (int32_t)(((int64_t)DR_BD_G * pow2_q16(-tune)) >> 16);         /* the same level at any pitch */
    c = dr_lp1(dr_exp(1000u, p[P_E2] - 64, 21));                       /* TONE: 120 Hz .. 8 kHz, 1 kHz */
    for (i = 0; i < n; i++) {
        int32_t x = 0, m, y;
        if (d->t < DR_PEND) {
            x = dr_pulse(d) * 16;                /* 1 V = 2^18 in the network */
            if (d->t < DR_P1 && !round)
                d->s[0] = 32767;
        }
        d->s[0] -= (d->s[0] >> 6) + (d->s[0] > 0);   /* attack envelope: ~5 ms after the pulse */
        m = (d->s[0] * 53412) >> 15;             /* the attack: x 2.63, 130 Hz */
        if (d->s[3] > DR_SIGH0)
            m += (d->s[3] - DR_SIGH0) >> DR_SIGHS;
        y = dr_res(&d->lp, &d->bp, f0 + (int32_t)(((int64_t)f0 * m) >> 15), q, x);
        y = clamp((int32_t)(((int64_t)y * g) >> 16), -65535, 65535);
        d->s[3] = y;
        y += x >> DR_LEAK;
        d->s[1] += ((y - d->s[1]) * c) >> 14;    /* TONE */
        d->s[2] += (d->s[1] * 256 - d->s[2]) >> 10;   /* output high-pass 6.8 Hz, Q8 */
        y = d->s[1] - (d->s[2] >> 8);
        acc[i] += y;
        if (y > pk || -y > pk)
            pk = y < 0 ? -y : y;
        d->t++;
    }
    if (d->t > DR_PEND && pk < DR_QUIET)
        dr_off(d);
}

#define DR_SD_G 1950                             /* tone */
#define DR_SD_N 18000                            /* noise at SNAP 64, Q15 */
static __attribute__((noinline)) void dr_sd(dr8_t *d, const int16_t *p, int32_t *acc, uint32_t n)
{
    int32_t m = p[P_E2] * 256, sn = p[P_E4] * DR_SD_N / 64;              /* TONE: 173 -> 336 Hz; SNAP */
    int32_t gl = (int32_t)(((int64_t)DR_SD_G * 2 * (32768 - m)) >> 15), gh = (int32_t)(((int64_t)DR_SD_G * m) >> 15);
    int32_t e0, de = dre_next(&d->e[0], &e0), pk = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t x = 0, y, a, b, w;
        if (d->t < DR_PEND)
            x = dr_pulse(d) * 16;
        a = dr_res(&d->lp, &d->bp, d->f, d->q, x);
        b = dr_res(&d->lp2, &d->bp2, d->f2, d->q2, x);
        y = (int32_t)(((int64_t)a * gl + (int64_t)b * gh) >> 16);
        w = (DRE_AT(e0, de, i) * sn) >> 15;
        y += (drf_hp(&DRF_SDN, dr_nz[i], d->s) * w) >> 15;
        acc[i] += y;
        if (y > pk || -y > pk)
            pk = y < 0 ? -y : y;
        d->t++;
    }
    if (d->t > DR_PEND && pk < DR_QUIET && d->e[0].e < DRE_LOW)
        dr_off(d);
}

/* toms and congas: Hz, the -40 dB time (ms), noise */
static const struct { uint16_t hz, ms; uint8_t nz, g; } DR_TOM[6] = {   /* g: level / 256 */
    {90, 200, 1, 255}, {135, 130, 1, 255}, {185, 100, 1, 255}, {185, 180, 0, 205}, {280, 100, 0, 205}, {400, 80, 0, 205},
};
#define DR_TOM_G 5700                            /* at 100 Hz (Q16; congas 0.8 of it), the same level at any pitch */
#define DR_TOM_N 2400                            /* toms' noise, Q15 */
#define DR_TOM_DROP 3932                         /* pitch at the hit: + 12 %, falling with the level */
static __attribute__((noinline)) void dr_tom(dr8_t *d, const int16_t *p, int32_t *acc, uint32_t n)
{
    uint32_t k = (uint32_t)(d->ins - DR_LT), i;
    int32_t tune = (p[P_E1] - 64) * 3 + d->pitch, f0, f, q, g, e0, n0, dn, pk = 0;
    f0 = dr_tuned(DR_F(DR_TOM[k].hz), tune);
    q = dr_damp(f0, dr8_tau(d,DR_TAU(DR_TOM[k].ms)));
    g = (int32_t)(((DR_TOM_G * DR_TOM[k].g >> 8) * 100 / DR_TOM[k].hz * pow2_q16(-tune)) >> 16);
    dre_next(&d->e[0], &e0);                     /* the pitch follows the level (the diodes starve it) */
    f = f0 + (int32_t)(((int64_t)f0 * ((e0 >> 9) * DR_TOM_DROP >> 15)) >> 15);
    dn = dre_next(&d->e[1], &n0);
    for (i = 0; i < n; i++) {
        int32_t x = 0, y;
        if (d->t < DR_PEND)
            x = dr_pulse(d) * 16;
        y = (int32_t)(((int64_t)dr_res(&d->lp, &d->bp, f, q, x) * g) >> 16);
        if (DR_TOM[k].nz) {
            d->s[0] += ((dr_nz[i] - d->s[0]) * 2700) >> 14;   /* ~1 kHz low-pass */
            y += (d->s[0] * ((DRE_AT(n0, dn, i) * DR_TOM_N) >> 15)) >> 15;
        } else {
            y += x >> (DR_LEAK + 2);             /* the congas' click */
        }
        acc[i] += y;
        if (y > pk || -y > pk)
            pk = y < 0 ? -y : y;
        d->t++;
    }
    if (d->t > DR_PEND && pk < DR_QUIET)
        dr_off(d);
}

#define DR_RS_G 5000
#define DR_RS_V 24                               /* into the swing VCA (Q4) */
#define DR_RS_O 15200                            /* out, Q15 */
static __attribute__((noinline)) void dr_rs(dr8_t *d, int32_t *acc, uint32_t n)
{
    int32_t e0, de = dre_next(&d->e[0], &e0), pk = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t x = 0, y;
        if (d->t < DR_PEND)
            x = dr_pulse(d) * 16;
        y = dr_res(&d->lp, &d->bp, d->f, d->q, x) + (dr_res(&d->lp2, &d->bp2, d->f2, d->q2, x) >> 1);
        y = softclip(clamp((int32_t)(((int64_t)y * DR_RS_G) >> 16) * DR_RS_V >> 4, -200000, 200000));
        y = (drf_hp(&DRF_RS, (y * DRE_AT(e0, de, i)) >> 15, d->s) * DR_RS_O) >> 15;
        acc[i] += y;
        if (y > pk || -y > pk)
            pk = y < 0 ? -y : y;
        d->t++;
    }
    if (d->t > DR_PEND && d->e[0].e < DRE_LOW)
        dr_off(d);
}

#define DR_CL_G 414
static __attribute__((noinline)) void dr_cl(dr8_t *d, int32_t *acc, uint32_t n)
{
    int32_t pk = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t x = 0, y;
        if (d->t < DR_PEND)
            x = dr_pulse(d) * 16;
        y = (int32_t)(((int64_t)dr_res(&d->lp, &d->bp, d->f, d->q, x) * DR_CL_G) >> 16);
        acc[i] += y;
        if (y > pk || -y > pk)
            pk = y < 0 ? -y : y;
        d->t++;
    }
    if (d->t > DR_PEND && pk < DR_QUIET)
        dr_off(d);
}

/* -------------------------------------------------------- noise voices --- */
#define DR_CP_GAP 441u                           /* the sawtooth: a burst every 10 ms, */
#define DR_CP_N 3u                               /* this many fast ones, then a slower last one */
#define DR_CP_REV 9000                           /* the reverb envelope, Q15 */
#define DR_CP_G 10445                            /* out, Q12 */
static __attribute__((noinline)) void dr_cp(dr8_t *d, int32_t *acc, uint32_t n)
{
    int32_t e0, de = dre_next(&d->e[0], &e0), pk = 0;
    const drf_t filter={(int16_t)d->f,(int16_t)d->f2,(int16_t)d->q,DRF_CP.k};
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t y, w;
        if (d->t <= DR_CP_N * DR_CP_GAP && d->t == (uint32_t)d->s[3]) {   /* the next burst */
            d->s[2] = (d->v * 151) >> 10;         /* Q15 of the trigger level (13.5 V -> 1.0) */
            d->s[3] += (int32_t)DR_CP_GAP;
        }
        d->s[2] -= (d->s[2] >> (d->t < DR_CP_N * DR_CP_GAP ? 7 : 8)) + (d->s[2] > 0);   /* 2.9 ms, the last 5.8 ms */
        w = d->s[2] + ((DRE_AT(e0, de, i) * DR_CP_REV) >> 15);
        y = (((drf_bp(&filter, dr_nz[i], d->s) * w) >> 15) * DR_CP_G) >> 12;
        acc[i] += y;
        if (y > pk || -y > pk)
            pk = y < 0 ? -y : y;
        d->t++;
    }
    if (d->t > DR_CP_N * DR_CP_GAP && d->s[2] < 8 && d->e[0].e < DRE_LOW)
        dr_off(d);
}

#define DR_MA_RISE 662u                          /* 15 ms */
#define DR_MA_G 23900                            /* out, Q15 */
static __attribute__((noinline)) void dr_ma(dr8_t *d, int32_t *acc, uint32_t n)
{
    int32_t step = ((d->v * 151) >> 10) / (int32_t)DR_MA_RISE, pk = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t y;
        if (d->t < DR_MA_RISE)
            d->s[2] += step;
        else
            d->s[2] -= (d->s[2] >> 7) + (d->s[2] > 0);   /* 2.9 ms */
        y = (((drf_hp(&DRF_MA, dr_nz[i], d->s) * d->s[2]) >> 15) * DR_MA_G) >> 15;
        acc[i] += y;
        if (y > pk || -y > pk)
            pk = y < 0 ? -y : y;
        d->t++;
    }
    if (d->t > DR_MA_RISE && d->s[2] < 8)
        dr_off(d);
}

/* -------------------------------------------------------- metal voices --- */
#define DR_DUTY 2060725309u                      /* 47.98 % (the HD14584 at 5 V) */
static const uint32_t DR_OSC[6] = {              /* phase increments: 205.3, 369.6, 304.4, 522.7, */
    19994485u, 35995916u, 29645987u, 50906562u, 77913239u, 52591436u,   /* 800 (#5), 540 Hz (#6) */
};
static uint32_t dr_inc[6];
/* Block setup stays outside the six-oscillator sample loop. */
static __attribute__((noinline)) void dr_metal_tune(const dr8_t *d)
{
    uint32_t ratio=(uint32_t)pow2_q16(d->pitch);
    for(uint32_t k=0;k<6;k++)dr_inc[k]=d->pitch?(uint32_t)(((uint64_t)DR_OSC[k]*ratio)>>16):DR_OSC[k];
}
#define DR_MV 6                                  /* the swing VCAs' input gain (x) */
/* the oscillators and what the lane's voice takes from them: the cowbell's two, band 1 (3.44 kHz, the cymbal)
 * and band 2 (7.1 kHz, cymbal and hats) through the clipping of its swing VCAs */
static __attribute__((noinline)) void dr_metal(dr8_t *d, uint32_t n)
{
    uint32_t i, k, ph[6];
    for (k = 0; k < 6u; k++)
        ph[k] = d->ph[k];
    for (i = 0; i < n; i++) {                    /* the six; their sum in dr_m1 */
        int32_t sum = 0;
        for (k = 0; k < 6u; k++) {
            ph[k] += dr_inc[k];
            sum += ph[k] < DR_DUTY ? 2048 : -2048;
        }
        dr_cbx[i] = (ph[4] < DR_DUTY ? 8192 : -8192) + (ph[5] < DR_DUTY ? 8192 : -8192);
        dr_m1[i] = sum;
    }
    for (k = 0; k < 6u; k++)
        d->ph[k] = ph[k];
    if ((DR_BANDS >> d->ins) & 1u)
        for (i = 0; i < n; i++)
            dr_m2[i] = softclip(clamp(drf_bp(&DRF_M2, dr_m1[i], d->m2) * DR_MV, -200000, 200000));
    if (d->ins == DR_CY)
        for (i = 0; i < n; i++)
            dr_m1[i] = drf_bp(&DRF_M1, dr_m1[i], d->m1);
}

#define DR_CB_G 26000
#define DR_CB_FAST 24576                         /* the two envelopes: 50 ms and 500 ms, Q15 */
static __attribute__((noinline)) void dr_cb(dr8_t *d, int32_t *acc, uint32_t n)
{
    int32_t e0, de = dre_next(&d->e[0], &e0), t0, dt = dre_next(&d->e[1], &t0);
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t y = softclip(clamp(drf_bp(&DRF_CB, dr_cbx[i], d->x), -200000, 200000));
        int32_t w = ((DRE_AT(e0, de, i) * DR_CB_FAST) >> 15) + ((DRE_AT(t0, dt, i) * (32767 - DR_CB_FAST)) >> 15);
        acc[i] += (((y * w) >> 15) * DR_CB_G) >> 15;
    }
    d->t += n;
    if (d->e[0].e < DRE_LOW && d->e[1].e < DRE_LOW)
        dr_off(d);
}

#define DR_CY_G 50000
static __attribute__((noinline)) void dr_cy(dr8_t *d, const int16_t *p, int32_t *acc, uint32_t n)
{
    int32_t a0, da, b0, db;
    int32_t *s = d->x;
    uint32_t i;
    d->e[0].k = dr_blk(DR_TAU(dr_exp(650u, p[P_E3] - 64, 50)));   /* DECY: 270 ms .. 1.5 s, 650 ms */
    da = dre_next(&d->e[0], &a0);
    db = dre_next(&d->e[1], &b0);
    for (i = 0; i < n; i++) {
        int32_t a = softclip(clamp(dr_m1[i] * DR_MV, -200000, 200000));
        int32_t b = dr_m2[i], y;
        a = drf_hp(&DRF_CYA, (a * DRE_AT(a0, da, i)) >> 15, s);
        b = drf_hp(&DRF_CYB, (b * DRE_AT(b0, db, i)) >> 15, s + 2);
        y = (a * 5 + b * 4) >> 3;
        acc[i] += ((y - s[4]) * DR_CY_G) >> 15;  /* the level buffer: + 6 dB / octave */
        s[4] = y;
    }
    d->t += n;
    if (d->e[0].e < DRE_LOW && d->e[1].e < DRE_LOW)
        dr_off(d);
}

#define DR_HAT_G 27000
static __attribute__((noinline)) void dr_hat(dr8_t *d, const int16_t *p, int32_t *acc, uint32_t n)   /* open or closed: band 2, a VCA, a high-pass */
{
    int32_t e0, de;
    uint32_t i;
    if (d->ins == DR_OH)                          /* DECY: 76 .. 690 ms, 230 ms; a closed hat's choke */
        d->e[0].k = d->choke ? dr_blk(64u) : dr_blk(DR_TAU(dr_exp(230u, p[P_E3] - 64, 40)));
    de = dre_next(&d->e[0], &e0);
    for (i = 0; i < n; i++) {
        int32_t y = (dr_m2[i] * (DRE_AT(e0, de, i) >> 1)) >> 14;
        acc[i] += (drf_hp(&DRF_HAT, y, d->x) * DR_HAT_G) >> 15;
    }
    d->t += n;
    if (d->e[0].e < DRE_LOW)
        dr_off(d);
}

/* ---------------------------------------------------------- the hits --- */
/* the 808's instrument for a note (the other kits' drum role where its map has none) */
static uint32_t dr8_ins(uint32_t note, uint32_t role)
{
    uint32_t n = note >= 35u && note <= 81u ? note : 36u + (note + 120u - 36u) % 12u, k = DR_GM[n - 35u];
    return k == DR_X ? DR_OF_DVT[role % DVT_COUNT] : k;
}

/* lane d struck: instrument ins at velocity vel (its trigger level), TUNE for the voices tuned at the hit. Another
 * instrument than the one ringing starts from rest */
static void dr8_hit(dr8_t *d, uint32_t ins, uint32_t vel, const int16_t *p)
{
    int32_t tune = (p[P_E1] - 64) * 3 + d->pitch, lv;
    if (!d->nst)
        d->nst = 0x2F6E2B1 + (int32_t)(ins * 0x9E3779B9u);
    if (d->ins != ins)
        dr_off(d);
    lv = DR_LVL(vel);
    d->ins = (uint8_t)ins;
    d->t = 0;
    d->v = (int32_t)vel * 1742;                  /* 13.5 V at 127 */
    switch (ins) {
    case DR_SD:
        d->f = dr_tuned(DR_F(173.3), tune);
        d->q = dr_damp(d->f, dr8_tau(d,DR_MS(18)));         /* Q 10 */
        d->f2 = dr_tuned(DR_F(336.0), tune);
        d->q2 = dr_damp(d->f2, dr8_tau(d,DR_MS(9)));        /* Q 9.9 */
        dre_hit(&d->e[0], lv, dr8_tau(d,DR_MS(45)));
        break;
    case DR_LT: case DR_MT: case DR_HT: case DR_LC: case DR_MC: case DR_HC: {
        uint32_t tau = dr8_tau(d,DR_TAU(DR_TOM[ins - DR_LT].ms));
        dre_hit(&d->e[0], lv, tau);
        dre_hit(&d->e[1], lv, tau * 7u / 10u);
        break;
    }
    case DR_RS:
        d->f = dr_tuned(DR_F(455), tune);
        d->q = dr_damp(d->f, dr8_tau(d,DR_MS(12)));
        d->f2 = dr_tuned(DR_F(1667), tune);
        d->q2 = dr_damp(d->f2, dr8_tau(d,DR_MS(6)));
        dre_hit(&d->e[0], lv, dr8_tau(d,DR_TAU(12)));
        break;
    case DR_CL:
        d->f = dr_tuned(DR_F(2500), tune);
        d->q = dr_damp(d->f, dr8_tau(d,DR_MS(9)));
        break;
    case DR_CP: {
        drf_t filter=dr8_filter(&DRF_CP,d->pitch);d->f=filter.a1;d->f2=filter.a2;d->q=filter.a3;
        d->s[2] = 0;
        d->s[3] = 0;                              /* the first burst now */
        dre_hit(&d->e[0], lv, dr8_tau(d,DR_MS(25)));
        break;
    }
    case DR_MA:
        d->s[2] = 0;
        break;
    case DR_CB:
        dre_hit(&d->e[0], lv, dr8_tau(d,DR_TAU(50)));
        dre_hit(&d->e[1], lv, dr8_tau(d,DR_TAU(500)));
        break;
    case DR_CY:
        dre_hit(&d->e[0], lv, 64u);              /* (dr_cy: DECY) */
        dre_hit(&d->e[1], lv, dr8_tau(d,DR_MS(40)));
        break;
    case DR_OH:
        d->choke = 0;
        dre_hit(&d->e[0], lv, 64u);              /* (dr_hat: DECY) */
        break;
    case DR_CH:
        dre_hit(&d->e[0], lv * 3 / 2, dr8_tau(d,DR_TAU(50)));   /* (its level knob above the open hat's) */
        break;
    }
    d->on = 1;
}

/* the lane's block into y (n <= CTL): its circuit, the knobs of the part (p) as they are now, at DRUM's level */
static __attribute__((noinline)) void dr8_run(dr8_t *d, const int16_t *p, int32_t *y, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        y[i] = 0;
    if (!d->on)
        return;
    if ((DR_NOISY >> d->ins) & 1u)
        for (i = 0; i < n; i++)
            dr_nz[i] = (int32_t)(noise32(&d->nst) >> 17) - 16384;
    if ((DR_METAL >> d->ins) & 1u) {
        dr_metal_tune(d);
        dr_metal(d, n);
    }
    switch (d->ins) {
    case DR_BD: dr_bd(d, p, y, n); break;
    case DR_SD: dr_sd(d, p, y, n); break;
    case DR_RS: dr_rs(d, y, n); break;
    case DR_CL: dr_cl(d, y, n); break;
    case DR_CP: dr_cp(d, y, n); break;
    case DR_MA: dr_ma(d, y, n); break;
    case DR_CB: dr_cb(d, y, n); break;
    case DR_CY: dr_cy(d, p, y, n); break;
    case DR_OH: case DR_CH: dr_hat(d, p, y, n); break;
    default: dr_tom(d, p, y, n); break;
    }
    for (i = 0; i < n; i++)
        y[i] = (y[i] * DR8_OUT) >> 12;
    if (d->t > DR_LIFE)
        dr_off(d);
}
