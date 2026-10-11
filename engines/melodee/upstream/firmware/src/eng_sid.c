/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SID: the Commodore MOS 6581 / 8580, a whole chip per note.
 *
 * Each note plays its own SID, clocked as in a PAL C64 (985248 Hz) and modelled at the register level as reSID
 * models it (Dag Lem's reSID 0.16 and 1.0, GPL-2.0-or-later; its measured curves and ladders: tools/gen_tables.py
 * sid_tables):
 *  - oscillators: 24-bit accumulators (the top of our 32-bit phases) stepped by the 16-bit frequency register
 *    (the pitch is rounded to it: 0.06 Hz steps, nothing above 3.85 kHz); sawtooth, triangle (ring modulation
 *    swaps in the source's inverted MSB), pulse against the 12-bit width, the 23-bit noise register (taps 22
 *    and 17, clocked by each rise of accumulator bit 19, read out from bits 20 18 14 11 9 5 2 0), hard sync on
 *    the source's MSB rising; combined waveforms as reSID sampled them from a real 6581 and 8580 (assets/resid),
 *    and on the 6581 their pull on the accumulator's MSB when the sawtooth is among them;
 *  - the DACs: 12-bit waveform and 8-bit envelope R-2R ladders, the 6581's with 2R / R = 2.20 and no termination
 *    (uneven steps, 0x7FF above 0x800), its waveform zero at 0x380 (a DC offset riding on the envelope: the
 *    6581's thump through the C64's coupling capacitor); the 8580's ideal, zero at 0x800;
 *  - the envelope generator: the 15-bit rate counter and its 16 periods, the exponential counter in decay and
 *    release (every period down to 0x5D, then every 2, 4, 8, 16, 30), sustain in 16 levels (it only decays to
 *    it), the counter frozen at zero, the ADSR delay of a period set below the counter (it wraps at 2^15 first);
 *  - the filter: the measured cutoff curve of each revision on the 11-bit FC, resonance in 4 bits, LP / BP / HP
 *    summed as the chip sums them (BP against LP and HP), op-amps that saturate (the 6581's early);
 *  - the C64's output stage: a 16 kHz RC low pass and the 16 Hz coupling capacitor.
 * At 44.1 kHz the sawtooth and pulse edges and the sync resets are band-limited (2-point polyBLEP with the DAC's
 * own step heights, one sample late); triangle, ring flips, noise and combined waveforms are sampled as they are.
 *
 * The track's ADSR picks the nearest of the chip's 16 rates (SID_ADSR_RATE) and 16 sustain levels; ENV DEST
 * reads the envelope counter as a player routine reads ENV3 (voice.c). A new note restarts the attack from the
 * current level with the rate counter cleared, as a player's hard restart leaves it. Velocity scales the voice.
 * STACK sets up the three voices: unison stacks, or SYNC (voice 2 synced to a silent voice 1) and RING (voice
 * 1's triangle ring-modulated by a silent voice; on the chip voice 3); there SHP modulation sweeps voice 2's
 * interval (the synced voice, the ring modulator) instead of the pulse width.
 *
 * Engine ID 3 was LOFI: its projects keep the ID and load as SID. */

static const char *const N_SID_MODEL[] = {"6581", "8580"};
static const char *const N_SID_WAVE[] = {"TRI", "SAW", "PULSE", "NOISE", "TRI+SAW", "TRI+PLS", "SAW+PLS", "T+S+P"};
static const char *const N_SID_STACK[] = {"ONE", "DETUNE", "OCT", "FIFTH", "MAJOR", "SYNC", "RING", "SUB"};
static const char *const N_SID_MODE[] = {"LP", "BP", "HP", "NOTCH", "LP+BP", "BP+HP", "ALL", "OFF"};

enum { SID_6581, SID_8580 };
enum { SID_TRI, SID_SAW, SID_PULSE, SID_NOISE, SID_TRI_SAW, SID_TRI_PULSE, SID_SAW_PULSE, SID_TRI_SAW_PULSE };
enum { SID_ONE, SID_DETUNE, SID_OCT, SID_FIFTH, SID_MAJOR, SID_SYNC, SID_RING, SID_SUB };
enum { SID_LP, SID_BP, SID_HP, SID_NOTCH, SID_LP_BP, SID_BP_HP, SID_ALL, SID_OFF };
enum { SID_ATTACK, SID_DECAY_SUSTAIN, SID_RELEASE };

#define SID_POLY 4
/* cycles per envelope step of each 4-bit rate (reSID: the rate counter's comparison values) */
static const uint16_t SID_RATE[16] = {9, 32, 63, 95, 149, 220, 267, 313, 392, 977, 1954, 3126, 3907, 11720, 19532, 31251};
static const uint8_t SID_MODE_BITS[8] = {1, 2, 4, 5, 3, 6, 7, 0};   /* MODE -> $D418 bits 4..6: LP 1, BP 2, HP 4 */
static const int16_t SID_ZERO[2] = {0x380 * 4, 0x800 * 4};            /* the waveform DAC's zero (x4, as SID_WDAC) */
static const int32_t SID_KNEE[2] = {20000, 40000};                    /* where the filter's op-amps saturate */

typedef struct { int16_t cents[3]; uint8_t nosc, heard; } sid_stack_t;   /* heard: bit k = voice k + 1 mixed */
static const sid_stack_t SID_STACKS[8] = {
    [SID_ONE] = {{0, 0, 0}, 1, 1},
    [SID_DETUNE] = {{0, -8, 8}, 3, 7},
    [SID_OCT] = {{0, 1200, -1200}, 3, 7},
    [SID_FIFTH] = {{0, 700, 1200}, 3, 7},
    [SID_MAJOR] = {{0, 400, 700}, 3, 7},
    [SID_SYNC] = {{0, 1900, 0}, 2, 2},       /* voice 2 synced to voice 1 (its waveform off) */
    [SID_RING] = {{0, 1586, 0}, 2, 1},       /* voice 1 ring-modulated by voice 2 at 5:2 (on the chip: voice 3) */
    [SID_SUB] = {{0, -1200, -2400}, 3, 7},
};

typedef struct {
    uint32_t noise[3];                       /* the noise registers of voices 1..3 (23 bits) */
    int32_t ic1, ic2;                        /* filter integrators */
    int32_t xlp, xhp;                        /* the C64's output stage: RC low pass, coupling capacitor (Q8) */
    int32_t held;                            /* the oscillator mix one sample late (an edge corrects both sides) */
    uint16_t rate_counter, cycle_frac;       /* envelope: the 15-bit rate counter; SID cycles left over (Q16) */
    uint8_t env, env_prev, state, exp_counter, exp_period, hold_zero;
    uint8_t quiet;                           /* the filter and output stage have settled (the voice may end) */
} sid_voice_t;
typedef struct {
    sid_voice_t v[SID_POLY];
    uint32_t wave_key;                       /* the combined waveform unpacked in wave[] (0: none yet) */
    uint8_t wave[4096];                      /* its top 8 output bits by accumulator >> 12 (sid_table) */
} sid_part_t;
static sid_part_t *sid_part(uint32_t part);   /* engines.c: shared runtime pool */
static sid_voice_t *sid_voice(track_t *t, voice_t *v)
{
    return &sid_part((uint32_t)(t - trk) % NPART)->v[(uint32_t)(v - t->v) % SID_POLY];
}

static uint32_t sid_noise_clock(uint32_t sr)
{
    return ((sr << 1) | (((sr >> 22) ^ (sr >> 17)) & 1u)) & 0x7fffffu;
}

/* the noise register as the waveform DAC sees it: bits 20 18 14 11 9 5 2 0 -> 11..4, the low four 0 */
static uint32_t sid_noise_bits(uint32_t sr)
{
    return ((sr & (1u << 20)) >> 9) | ((sr & (1u << 18)) >> 8) |
           ((sr & (1u << 14)) >> 5) | ((sr & (1u << 11)) >> 3) |
           ((sr & (1u << 9)) >> 2) | ((sr & (1u << 5)) << 1) |
           ((sr & (1u << 2)) << 3) | ((sr & 1u) << 4);
}

/* one clock for each rise of accumulator bit 19 (bit 27 of ph) from ph to ph + inc (inc < 2^29: up to two) */
static uint32_t sid_noise_advance(uint32_t sr, uint32_t ph, uint32_t inc)
{
    uint32_t clocks = (((ph ^ 0x08000000u) & 0x0fffffffu) + inc) >> 28;
    while (clocks--)
        sr = sid_noise_clock(sr);
    return sr;
}

/* triangle: the accumulator folded by msb's top bit (its own MSB; RING: XOR the source's inverted), 11 bits, bit 0
 * low */
static inline uint32_t sid_triangle(uint32_t ph, uint32_t msb)
{
    return ((ph ^ (uint32_t)((int32_t)msb >> 31)) >> 19) & 0xffeu;
}

/* WAVE -> the control register's waveform bits 4..7 as 0..3: TRI, SAW, PULSE, NOISE */
static const uint8_t SID_WAVE_BITS[8] = {1, 2, 4, 8, 3, 5, 6, 7};

/* the 12-bit output of waveform bits wb (no noise) at accumulator ph; x: ph with RING's MSB (reSID 1.0: XOR NOT the
 * source's MSB, without the sawtooth), the triangle's fold and a combined table's index; pt: the pulse width << 20
 * (the comparator's threshold); wt: the combined waveform's samples (sid_table), the pulse level ANDed on */
static inline uint32_t sid_wave_out(uint32_t wb, const uint8_t *wt, uint32_t ph, uint32_t x, uint32_t pt)
{
    uint32_t pulse = (uint32_t)-(int32_t)(ph >= pt) & 0xfffu;
    switch (wb) {
    case 1: return sid_triangle(ph, x);
    case 2: return ph >> 20;
    case 4: return pulse;
    default: return ((uint32_t)wt[x >> 20] << 4) & (wb & 4u ? pulse : 0xfffu);
    }
}

/* the 12-bit waveform output of WAVE (wt: as sid_table gives it for a combined one) */
static uint32_t sid_wave12(uint32_t wave, const uint8_t *wt, uint32_t ph, uint32_t x, uint32_t pt, uint32_t noise)
{
    uint32_t b = SID_WAVE_BITS[wave & 7u];
    return b & 8u ? sid_noise_bits(noise) : sid_wave_out(b, wt, ph, x, pt);
}

/* reSID's samples as tools/gen_tables.py sid_lz packs them: c < 0x80: c + 1 bytes follow; c >= 0x80: (c & 0x7F)
 * + 3 bytes copied from d + 1 back, d in the next two bytes */
static void sid_unpack(uint8_t *o, const uint8_t *b, uint32_t n)
{
    uint8_t *e = o + n;
    while (o < e) {
        uint32_t c = *b++;
        if (c < 0x80u) {
            for (c++; c; c--)
                *o++ = *b++;
        } else {
            const uint8_t *src = o - (b[0] | (uint32_t)b[1] << 8) - 1;
            b += 2;
            for (c = (c & 0x7fu) + 3u; c; c--)
                *o++ = *src++;
        }
    }
}

/* the part's samples of combined waveform bits wb on MODEL, unpacked when either changes; 0 for a single one */
static const uint8_t *sid_table(track_t *t, uint32_t model, uint32_t wb)
{
    sid_part_t *sp;
    uint32_t key = 0x100u | model << 3 | wb;
    if (wb != 3u && (wb < 5u || wb > 7u))
        return 0;
    sp = sid_part((uint32_t)(t - trk) % NPART);
    if (sp->wave_key != key) {
        sid_unpack(sp->wave, SID_WAVE_LZ + SID_WAVE_AT[model][wb == 3u ? 0u : wb - 4u], sizeof sp->wave);
        sp->wave_key = key;
    }
    return sp->wave;
}

static inline int32_t sid_dac(const int16_t (*dac)[64], uint32_t w)   /* the waveform DAC, x4 */
{
    return dac[0][w & 63u] + dac[1][w >> 6];
}

static int32_t sid_edac(uint32_t model, uint32_t env)                   /* the envelope DAC, Q15 */
{
    return SID_EDAC[model][0][env & 15u] + SID_EDAC[model][1][env >> 4];
}

/* the fraction of a sample (Q16) a distance dist < inc covers: dist / inc, through rcp = 2^32 / (inc >> sh) with
 * inc >> sh in 2^15..2^16 (no 64-bit division; 15 bits wherever the pitch is) */
static inline uint32_t sid_frac(uint32_t dist, uint32_t rcp, uint32_t sh)
{
    return (uint32_t)(((uint64_t)(dist >> sh) * rcp) >> 16);
}

/* a step of height h, d (Q16) of a sample before this one: the 2-point polyBLEP residuals of the sample before
 * (*pre += h d^2 / 2) and of this one (*now -= h (1 - d)^2 / 2) */
static inline void sid_blep(int32_t h, uint32_t d, int32_t *pre, int32_t *now)
{
    uint32_t u = 65535u - d;
    *pre += (h * (int32_t)((d * d) >> 17)) >> 16;
    *now -= (h * (int32_t)((u * u) >> 17)) >> 16;
}

/* ------------------------------------------------------------------------------------- envelope --- */
static uint32_t sid_period(const int16_t *p, uint32_t state)
{
    uint32_t r = SID_ADSR_RATE[p[state == SID_ATTACK ? P_ATK : state == SID_DECAY_SUSTAIN ? P_DEC : P_REL] & 127];
    return SID_RATE[state == SID_ATTACK ? r & 15u : r >> 4];
}

static uint32_t sid_sustain(const int16_t *p)   /* the 4-bit sustain as the counter level: n * 0x11 */
{
    return (uint32_t)(clamp(p[P_SUS], 0, 127) * 15 + 63) / 127u * 17u;
}

/* delta cycles of the envelope generator (reSID 0.16 EnvelopeGenerator::clock) */
static void sid_env_clock(sid_voice_t *s, const int16_t *p, uint32_t delta)
{
    uint32_t period = sid_period(p, s->state), sustain = sid_sustain(p);
    int32_t step = (int32_t)period - (int32_t)s->rate_counter;
    if (step <= 0)                                     /* the ADSR delay: the counter is past the period, it */
        step += 0x7fff;                                /* counts on to its wrap first */
    if (delta >= (uint32_t)step && (s->hold_zero || (s->state == SID_DECAY_SUSTAIN && s->env == sustain))) {
        uint32_t rest = delta - (uint32_t)step, n = 1u + rest / period;   /* nothing but the counters moves: */
        s->rate_counter = (uint16_t)(rest % period);   /* where they are after delta cycles, at once */
        s->exp_counter = s->state == SID_ATTACK ? 0u : (uint8_t)((s->exp_counter + n) % s->exp_period);
        return;
    }
    while (delta) {
        if (delta < (uint32_t)step) {
            s->rate_counter = (uint16_t)(s->rate_counter + delta);
            if (s->rate_counter & 0x8000u)              /* (a 15-bit LFSR on the chip: period 0x7FFF) */
                s->rate_counter = (uint16_t)((s->rate_counter + 1u) & 0x7fffu);
            return;
        }
        s->rate_counter = 0;
        delta -= (uint32_t)step;
        step = (int32_t)period;
        if (s->state != SID_ATTACK && ++s->exp_counter != s->exp_period)
            continue;                                  /* (the attack does not wait for the exponential counter) */
        s->exp_counter = 0;
        if (s->hold_zero)
            continue;
        if (s->state == SID_ATTACK) {
            if (++s->env == 0xffu) {                   /* (from 0xFF it wraps to 0 and freezes, as the chip) */
                s->state = SID_DECAY_SUSTAIN;
                step = (int32_t)(period = sid_period(p, SID_DECAY_SUSTAIN));
            }
        } else if (s->state == SID_RELEASE || s->env != sustain)
            s->env--;
        switch (s->env) {                              /* the exponential counter's period */
        case 0xff: s->exp_period = 1; break;
        case 0x5d: s->exp_period = 2; break;
        case 0x36: s->exp_period = 4; break;
        case 0x1a: s->exp_period = 8; break;
        case 0x0e: s->exp_period = 16; break;
        case 0x06: s->exp_period = 30; break;
        case 0x00: s->exp_period = 1; s->hold_zero = 1; break;
        default: break;
        }
    }
}

static void sid_note_on(track_t *t, voice_t *v)
{
    sid_voice_t *s = sid_voice(t, v);
    if (!voice_was) {                                  /* a free voice: a chip after reset, accumulators at 0 */
        uint32_t k;
        for (k = 0; k < 3u; k++) {
            v->ph[k] = 0;
            s->noise[k] = 0x7fffff;
        }
        s->ic1 = s->ic2 = s->xlp = s->xhp = s->held = 0;
        s->env = s->env_prev = 0;
        s->exp_counter = 0;
        s->exp_period = 1;
        s->cycle_frac = 0;
    }                                                  /* sounding: the attack goes on from the current level */
    s->state = SID_ATTACK;                             /* the gate, after a hard restart's cleared rate counter */
    s->hold_zero = 0;
    s->rate_counter = 0;
    s->quiet = 0;
}

/* once per control tick before the render (engine_t.done): this tick's cycles of the envelope, then the gate
 * (a note released within its first tick still gets that tick of attack); the voice ends once released to zero
 * and its filter and output stage have settled */
static int sid_done(track_t *t, voice_t *v)
{
    sid_voice_t *s = sid_voice(t, v);
    uint32_t c = s->cycle_frac + SID_CYC_Q16;
    s->env_prev = s->env;
    s->cycle_frac = (uint16_t)c;
    sid_env_clock(s, t->p, c >> 16);
    if (!v->gate && s->state != SID_RELEASE)
        s->state = SID_RELEASE;
    return s->state == SID_RELEASE && !s->env && !s->env_prev && s->quiet;
}

/* the envelope counter (ENV3), Q15: voice.c's ENV source and ENV DEST */
static int32_t sid_env_q15(track_t *t, voice_t *v)
{
    uint32_t e = sid_voice(t, v)->env;
    return (int32_t)(e << 7 | e >> 1);
}

/* --------------------------------------------------------------------------------------- render --- */
static void sid_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    sid_voice_t *s = sid_voice(t, v);
    uint32_t model = p[P_E0] == SID_8580, wave = (uint32_t)p[P_E1] & 7u, stack = (uint32_t)p[P_E2] & 7u;
    const sid_stack_t *st = &SID_STACKS[stack];
    const int16_t (*dac)[64] = SID_WDAC[model];
    const uint16_t *gt = SID_FC_G[model];
    uint32_t nosc = st->nosc, heard = st->heard, bits = SID_MODE_BITS[p[P_E6] & 7], wb = SID_WAVE_BITS[wave];
    uint32_t sync = stack == SID_SYNC, ring = stack == SID_RING, blep = wave == SID_SAW || wave == SID_PULSE;
    uint32_t rmask = ring && !(wb & 2u) ? 0x80000000u : 0u;   /* (RING does nothing with the sawtooth on) */
    uint32_t noisy = wb & 8u, pull = !model && (wb & 2u) && (wb & 5u);   /* (6581, the sawtooth combined) */
    const uint8_t *wt = sid_table(t, model, wb);
    uint32_t ph[3], inc[3] = {0, 0, 0}, rcp[3], rsh[3], noise[3], nb[3], hl[3], nh = 0, pt, i, k;
    int32_t full = dac[0][63] + dac[1][63], zero = SID_ZERO[model] * (heard == 7u ? 3 : 1), knee = SID_KNEE[model];
    int32_t shp = m->shape - (64 << 8), norm = heard == 7u ? 10923 : 32767, vel = v->vel * 258;
    int32_t held = s->held, ic1 = s->ic1, ic2 = s->ic2, xlp = s->xlp, xhp = s->xhp, o = 0;
    int32_t fcr, g, kd, a0, a1, gin, gout, sl, sb, sh;
    tsvf_t fc;
    /* the registers this block: frequencies (16 bits), pulse width (12), FC (11), RES (4) */
    for (k = 0; k < 3u; k++) {
        ph[k] = v->ph[k];
        noise[k] = s->noise[k];
        nb[k] = sid_noise_bits(noise[k]);
        if (heard >> k & 1u)
            hl[nh++] = k;                              /* the voices mixed */
    }
    for (k = 0; k < nosc; k++) {
        int32_t c = st->cents[k] + (k == 1u && (sync || ring) ? (shp * 75) >> 10 : 0);   /* SHP: +-24 st */
        uint32_t fn = (uint32_t)(((uint64_t)cents_inc(m->pitch16, c, m->fine) * SID_FREQ_R32 + 0x80000000u) >> 32);
        inc[k] = (uint32_t)(((uint64_t)(fn > 0xffffu ? 0xffffu : fn) * SID_FREQ_K16) >> 16);
        for (rsh[k] = 0; inc[k] >> rsh[k] >= 65536u; rsh[k]++)
            ;
        rcp[k] = inc[k] ? 0xffffffffu / (inc[k] >> rsh[k]) : 0u;   /* (sid_frac) */
    }
    pt = (uint32_t)clamp(p[P_E3] * 32 + (sync || ring ? 0 : shp / 32), 0, 4095) << 20;
    fcr = clamp(p[P_E4] * 16 + (p[P_E4] >> 3) + (m->cutoff >> 4), 0, 2047);
    g = gt[fcr >> 4] + (((gt[(fcr >> 4) + 1] - gt[fcr >> 4]) * (fcr & 15)) >> 4);
    kd = SID_RES_K[model][clamp(p[P_E5], 0, 15)];
    tsvf_coef_gk(&fc, g, kd);
    sl = bits & 1u ? 1 : 0;                            /* the chip's sum: LP and HP against BP */
    sb = bits & 2u ? -1 : 0;
    sh = bits & 4u ? 1 : 0;
    gin = 4096 + p[P_E7] * 64;                         /* DRIVE: into the filter x1..x3, out x1..x1/2 */
    gout = (4096 << 12) / (4096 + p[P_E7] * 32);
    a0 = mulq15(mulq15(sid_edac(model, s->env_prev), norm), vel);   /* the envelope DAC over the block */
    a1 = mulq15(mulq15(sid_edac(model, s->env), norm), vel);
    for (i = 0; i < n; i++) {
        int32_t xn = 0, pre = 0, now = 0, x, f;
        uint32_t reset = 0, j;
        ph[0] += inc[0];                               /* (a voice the stack leaves out: inc 0) */
        ph[1] += inc[1];
        ph[2] += inc[2];
        if (sync && ph[0] - 0x80000000u < inc[0]) {   /* voice 1's MSB rose: voice 2 restarts from 0 */
            uint32_t d = sid_frac(ph[0] - 0x80000000u, rcp[0], rsh[0]);
            uint32_t after = (uint32_t)(((uint64_t)inc[1] * d) >> 16), at = ph[1] - after;
            sid_blep(sid_dac(dac, sid_wave12(wave, wt, 0, 0, pt, noise[1])) -
                     sid_dac(dac, sid_wave12(wave, wt, at, at, pt, noise[1])), d, &pre, &now);
            ph[1] = after;
            reset = 2u;
        }
        for (j = 0; j < nh; j++) {
            uint32_t q = ph[k = hl[j]], w;
            if (noisy) {                               /* the noise register: clocked by accumulator bit 19 */
                if (((((q - inc[k]) ^ 0x08000000u) & 0x0fffffffu) + inc[k]) >> 28) {
                    noise[k] = sid_noise_advance(noise[k], q - inc[k], inc[k]);
                    nb[k] = sid_noise_bits(noise[k]);
                }
                w = nb[k];
            } else {
                w = sid_wave_out(wb, wt, q, q ^ (~ph[1] & rmask), pt);
                if (pull)                              /* output bit 11 low pulls the accumulator's MSB low */
                    ph[k] = q &= ((w & 0x800u) << 20) | 0x7fffffffu;
            }
            xn += sid_dac(dac, w);
            if (blep && !(reset >> k & 1u)) {          /* its steps band-limited (not when sync just restarted it) */
                if (q < inc[k] && (wave == SID_SAW || pt))   /* the accumulator wrapped: saw and pulse fall */
                    sid_blep(-full, sid_frac(q, rcp[k], rsh[k]), &pre, &now);
                if (wave == SID_PULSE && pt && q - pt < inc[k])   /* the comparator: the pulse rises */
                    sid_blep(full, sid_frac(q - pt, rcp[k], rsh[k]), &pre, &now);
            }
        }
        x = held + pre;                                /* the sample before, its edges band-limited */
        held = xn - zero + now;
        x = (x * (a0 + (((a1 - a0) * (int32_t)i) >> CTL_LOG2))) >> 15;
        if (bits) {                                    /* the filter: a TPT state variable filter */
            int32_t in = (x * gin) >> 12, v3 = in - ic2, bp, lp, hp;
            bp = soft_knee((fc.a1 * ic1 + fc.a2 * v3) >> 13, knee);
            lp = soft_knee(ic2 + ((fc.a2 * ic1 + fc.a3 * v3) >> 13), knee);
            ic1 = clamp(2 * bp - ic1, -150000, 150000);
            ic2 = clamp(2 * lp - ic2, -150000, 150000);
            hp = in - lp - ((bp * kd) >> 12);
            f = ((lp * sl + bp * sb + hp * sh) * gout) >> 12;
        } else                                         /* OFF: no voice routed to the filter */
            f = (soft_knee((x * gin) >> 12, knee) * gout) >> 12;
        xlp += (int32_t)(((int64_t)(clamp(f, -131071, 131071) - xlp) * SID_XLP_A) >> 15);
        xhp += (int32_t)(((int64_t)((xlp << 8) - xhp) * SID_XHP_A) >> 16);
        o = clamp(xlp - (xhp >> 8), -32767, 32767);
        out[i] += voice_amp(o << 1, m, i);             /* (one voice peaks near half scale) */
    }
    for (k = 0; k < 3u; k++) {
        v->ph[k] = ph[k];
        s->noise[k] = noise[k];
    }
    s->held = held;
    s->ic1 = ic1; s->ic2 = ic2; s->xlp = xlp; s->xhp = xhp;
    /* settled: what is left is a tail below -50 dB or the integrators' rounding residue (a few LSB of DC) */
    s->quiet = o > -32 && o < 32 && xlp > -128 && xlp < 128 && ic1 > -256 && ic1 < 256 && ic2 > -256 && ic2 < 256;
}

/* name, MODEL WAVE STACK PW CUT RES MODE DRIVE, ATK DEC SUS REL (the track's: SID_ADSR_RATE), ENV -> FILTER,
 * mono, sends */
#define SIDP(n, mo, wa, st, pw, cu, re, md, dr, a, d, s, r, fe, mi, di, ch, de, rv) \
    {n, {mo, wa, st, pw, cu, re, md, dr}, {a, d, s, r}, fe, mi, FX(di, ch, de, rv)}

static const preset_t SID_PRESETS[] = {
    SIDP("INIT SID",    0, 1, 0, 64, 127,  0, 0,  0,   0, 36, 127, 36,   0, 0,  0,  0, 0,  0),
    SIDP("BREADBOX",    0, 2, 1, 40,  60,  6, 0, 30,   0, 78, 100, 57,  16, 0,  0, 10, 0,  8),
    SIDP("RUBBER BASS", 0, 2, 7, 30,  36, 10, 0, 40,   0, 63,  60, 36,  30, 1,  0,  0, 0,  0),
    SIDP("ARP BASS",    0, 2, 0, 48,  34, 12, 0, 30,   0, 57,  40,  0,  36, 1,  0,  0, 0,  0),
    SIDP("SUB PULSE",   1, 2, 7, 64,  14,  4, 0,  0,   0, 69, 100, 36,  12, 1,  0,  0, 0,  0),
    SIDP("SYNC BASS",   0, 1, 5, 64,  44,  8, 0, 40,   0, 63,  90, 36,  16, 1,  0,  0, 0,  0),
    SIDP("SAW BASS",    1, 1, 2, 64,  10,  9, 0, 10,   0, 63,  84, 36,  30, 1,  0,  0, 0,  0),
    SIDP("DIRTY BASS",  0, 1, 1, 64,  38, 11, 0, 90,   0, 69,  70, 36,  24, 1,  0,  0, 0,  0),
    SIDP("LEAD 6581",   0, 1, 1, 64,  60,  9, 0, 40,   0, 69, 110, 57,  10, 1,  0, 14, 0, 10),
    SIDP("RING LEAD",   1, 0, 6, 64, 127,  0, 7,  0,   0, 69, 110, 57,   0, 1,  0, 10, 0, 10),
    SIDP("SYNC LEAD",   1, 1, 5, 64, 100,  6, 0,  0,   0, 69, 110, 57,   0, 1,  0, 12, 0, 10),
    SIDP("PULSE LEAD",  0, 2, 1, 26,  70,  8, 0, 50,   0, 63, 110, 49,   8, 1,  0,  8, 0,  8),
    SIDP("GAME HERO",   0, 2, 3, 50, 127,  0, 7,  0,   0, 57, 100, 49,   0, 1,  0, 10, 0, 10),
    SIDP("LASER LEAD",  1, 6, 5, 72, 100,  6, 0,  0,   0, 63, 100, 36,   0, 1,  0,  0, 0, 12),
    SIDP("CHIP SOLO",   0, 5, 1, 40, 127,  0, 7,  0,   0, 69, 110, 57,   0, 1,  0, 16, 0, 14),
    SIDP("8580 LEAD",   1, 4, 1, 64, 110,  4, 0,  0,   0, 69, 110, 57,   0, 1,  0, 14, 0, 12),
    SIDP("PWM PAD",     0, 2, 1, 46,  66,  6, 0, 20,  85, 85, 100, 90,   0, 0,  0, 30, 0, 30),
    SIDP("SID STRINGS", 1, 1, 1, 64,  60,  5, 0,  0,  90, 85, 110, 97,   0, 0,  0, 40, 0, 34),
    SIDP("DREAM CHIP",  0, 0, 4, 64, 127,  0, 7,  0,  70, 85, 100, 90,   0, 0,  0, 36, 0, 40),
    SIDP("8580 PAD",    1, 4, 1, 64,  70,  6, 4,  0,  85, 90, 104, 97,   0, 0,  0, 40, 0, 38),
    SIDP("TRI ORGAN",   1, 0, 2, 64, 127,  0, 7,  0,   0, 36, 127, 36,   0, 0,  0, 28, 0, 18),
    SIDP("GAME ORGAN",  0, 2, 3, 64, 127,  0, 7,  0,   0, 36, 118, 36,   0, 0,  0, 24, 0, 16),
    SIDP("C64 KEYS",    1, 6, 0, 58,  70,  4, 0, 20,   0, 85,  60, 63,  20, 0,  0, 18, 0, 12),
    SIDP("SID CLAV",    1, 2, 0, 20,  40, 10, 1, 30,   0, 57,  20, 49,  20, 0,  0, 12, 0,  4),
    SIDP("ARP PLUCK",   0, 1, 4, 64,  50, 12, 0, 40,   0, 57,   0, 49,  34, 0,  0,  8, 0,  8),
    SIDP("COIN PLUCK",  1, 2, 2, 32, 127,  0, 7,  0,   0, 63,   0, 57,   0, 0,  0, 10, 0,  8),
    SIDP("BELL RING",   1, 0, 6, 64, 127,  0, 7,  0,   0, 97,   0, 97,   0, 0,  0, 20, 0, 34),
    SIDP("SID BRASS",   0, 1, 3, 64,  46,  8, 0, 40,  49, 78, 100, 63,  30, 0,  0, 18, 0, 16),
    SIDP("FILTER HIT",  0, 1, 0, 64,  40, 15, 1, 70,   0, 63,   0, 57,  50, 0,  0,  0, 0, 18),
    SIDP("LASER FX",    1, 2, 5, 82,  96,  8, 0,  0,   0, 78,  40, 69,   0, 1,  0,  0, 0, 30),
    SIDP("NOISE SWEEP", 0, 3, 0, 64,  40, 12, 1, 60,  63, 97,  40, 97,  50, 0,  0,  0, 0, 36),
    SIDP("DATA STORM",  1, 3, 1, 64,  80, 10, 0, 40,   0, 85,  60, 78,   0, 0,  0, 18, 0, 28),
};
#undef SIDP

static const engine_t ENG_SID = {
    .name = "SID",
    .page_title = {"CHIP", "FILTER"},
    .edit = {
        {"MODEL", F_ENUM, 0, 1, 0, N_SID_MODEL, 0},
        {"WAVE", F_ENUM, 0, 7, 1, N_SID_WAVE, 0},
        {"STACK", F_ENUM, 0, 7, 0, N_SID_STACK, 0},
        {"PW", F_PCT, 0, 127, 64, 0, 0},
        {"CUT", F_PCT, 0, 127, 127, 0, 0},
        {"RES", F_INT, 0, 15, 0, 0, 0},
        {"MODE", F_ENUM, 0, 7, 0, N_SID_MODE, 0},
        {"DRIVE", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = SID_PRESETS,
    .npresets = NELEM(SID_PRESETS),
    .note_on = sid_note_on,
    .render = sid_render,
    .knob = {P_E1, P_E3, P_E4, P_E5},
    .poly = SID_POLY,
    .ownenv = 1,                 /* the chip's envelope generator: it ends the voice (sid_done) */
    .done = sid_done,
};
