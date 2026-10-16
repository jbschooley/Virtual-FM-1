/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* KIT SID: a drum track played as a C64 tune's drum routine plays it, on one MOS 6581 for the whole track
 * (eng_sid.c's model of it: oscillators, the noise register, the DACs, the envelope generator, the cutoff curve
 * and resonance, the C64's output stage).
 *  - The chip's three voices take the hits. A lane is one drum at a time (the two hats one lane: a closed hat
 *    chokes the open one): its drum still sounding fades out over 5.8 ms, about the chip's fastest release (a
 *    player's hard restart; switching the release rate instead would wait out the ADSR delay, up to 33 ms),
 *    while the hit starts on a silent voice; with none, it takes the voice furthest into its fade, else the
 *    oldest hit's, after a 2.9 ms fade. Never a cut: the 6581's DC offset would make it a hard step through
 *    the C64's coupling capacitor. A hit restarts its oscillator (the test bit) and its envelope from zero.
 *  - Each drum is a frame program as trackers write them: a PAL frame (19656 cycles, 50.12 Hz) at a time a
 *    waveform, a frequency and its slide each frame, the gate; frames count from the hit. Its ADSR runs on the
 *    chip's envelope generator; the gate closes after the drum's frames (or a recorded hit's length).
 *  - One filter, the chip's: a low pass for the drums routed to it (KICK, SNARE, TOMS, CLAP); TONE is its
 *    cutoff, RES its resonance.
 *  - DIGI ON: KICK and SNARE play on the master volume register ($D418), 4 bits at 7882 Hz (a digi player's
 *    NMI timer), from the samples tools/gen_tables.py synthesizes (no recording). The volume rests at 8; the
 *    6581's DC offset under it makes the sound (reSID 0.16: each voice's 0x800 * 0xFF and the mixer's
 *    -0xFFF * 0xFF / 18), so the voices ringing meanwhile follow the same volume, as on a C64.
 * Velocity, ACC, the lane levels and LEVEL scale a drum's envelope (a digi's steps, still in 4 bits). The chip
 * runs at half scale (three voices and a digi fit under its clamps) and leaves through a soft knee. */
#define DSK_NCH 3
#define DSK_STEPS 5
#define DSK_GATE 0x01u
#define DSK_NOISE 0x80u
#define DSK_DC (3 * 8192 - 910)                /* the 6581's DC under the volume (DAC x 4 by envelope Q15) */
#define DSK_FADE 8                             /* blocks a lane's ringing drum fades over (5.8 ms) */
#define DSK_STEAL 4                            /* .. a voice another drum takes, before that hit (2.9 ms) */
#define DSK_REST 8                             /* DIGI: the master volume between samples */

typedef struct { uint8_t ctrl, frames; uint16_t freq; int16_t slide; } dsk_step_t;
typedef struct {
    uint8_t adsr[4];                           /* ATK DEC SUS REL on the track's scale (SID_ADSR_RATE) */
    uint8_t gate, route, nstep, loop;          /* frames the gate stays open; to the filter; steps; looped to */
    uint8_t snap;                              /* step 0 is the click SNAP sets (1..3 frames, 0: none) */
    uint16_t pw;                               /* pulse width (12 bits) */
    dsk_step_t s[DSK_STEPS];                   /* frames 0: to the end; freq 0: the one before; slide a frame */
} dsk_drum_t;

/* the frequency register of f Hz (PAL); a voice's control register $D404: waveform bits 4..7, the gate */
#define DSK_HZ(f) ((uint16_t)((f) * 17.0284 + 0.5))
#define D_TRI 0x11u
#define D_PUL 0x41u
#define D_NOI 0x81u
#define D_OFF(c) ((c) & 0xFEu)
#define D_HOLD 0xFFu                           /* (loop: the last step holds) */
/* in the 808's sound order (DRUM_SOUND_NAME; RIDE is its BELL, CRASH its cymbal) */
static const dsk_drum_t DSK_DRUMS[16] = {
    /* KICK: a noise click, a pulse frame, then a triangle sweeping down */
    {{0, 72, 0, 30}, 8, 1, 4, D_HOLD, 1, 0x800, {{D_NOI, 1, 0xA000, 0}, {D_PUL, 1, DSK_HZ(170), 0},
        {D_TRI, 1, DSK_HZ(118), 0}, {D_TRI, 0, DSK_HZ(84), -DSK_HZ(7)}}},
    /* SNARE: a click, two pulse frames of body, a noise tail growing darker */
    {{0, 70, 0, 50}, 6, 1, 4, D_HOLD, 1, 0x600, {{D_NOI, 1, 0xC800, 0}, {D_PUL, 1, DSK_HZ(230), 0},
        {D_PUL, 1, DSK_HZ(190), 0}, {D_NOI, 0, 0xB000, -0x200}}},
    /* TOM L, M, H: a short click, then a falling triangle */
    {{0, 76, 0, 60}, 8, 1, 2, D_HOLD, 1, 0x800, {{D_NOI, 1, 0x6000, 0}, {D_TRI, 0, DSK_HZ(110), -DSK_HZ(3)}}},
    {{0, 76, 0, 60}, 8, 1, 2, D_HOLD, 1, 0x800, {{D_NOI, 1, 0x7000, 0}, {D_TRI, 0, DSK_HZ(150), -DSK_HZ(4)}}},
    {{0, 74, 0, 60}, 8, 1, 2, D_HOLD, 1, 0x800, {{D_NOI, 1, 0x8000, 0}, {D_TRI, 0, DSK_HZ(205), -DSK_HZ(5)}}},
    /* RIM: a narrow pulse blip into a high triangle */
    {{0, 42, 0, 30}, 1, 0, 2, D_HOLD, 0, 0x300, {{D_PUL, 1, DSK_HZ(1700), 0}, {D_TRI, 0, DSK_HZ(820), 0}}},
    /* CLAP: three noise bursts, the gate closed between them, then the tail */
    {{0, 62, 0, 40}, 10, 1, 5, D_HOLD, 0, 0x800, {{D_NOI, 1, 0x4800, 0}, {D_OFF(D_NOI), 1, 0, 0},
        {D_NOI, 1, 0, 0}, {D_OFF(D_NOI), 1, 0, 0}, {D_NOI, 0, 0x4400, 0}}},
    /* HAT C, HAT O: bright noise */
    {{0, 44, 0, 34}, 1, 0, 1, D_HOLD, 0, 0x800, {{D_NOI, 0, 0xF000, 0}}},
    {{0, 80, 0, 74}, 14, 0, 1, D_HOLD, 0, 0x800, {{D_NOI, 0, 0xF000, 0}}},
    /* CYMBAL: a brighter first frame, then a long wash */
    {{0, 100, 0, 96}, 30, 0, 2, D_HOLD, 0, 0x800, {{D_NOI, 1, 0xFFFF, 0}, {D_NOI, 0, 0xD800, -0x40}}},
    /* BELL: two pulse tones a frame each, looped (a C64 arpeggio) */
    {{0, 66, 0, 56}, 6, 0, 2, 0, 0, 0x800, {{D_PUL, 1, DSK_HZ(810), 0}, {D_PUL, 1, DSK_HZ(540), 0}}},
    /* CONGA L, M, H: a triangle that drops a little after its first frame */
    {{0, 66, 0, 50}, 4, 0, 2, D_HOLD, 0, 0x800, {{D_TRI, 1, DSK_HZ(230), 0}, {D_TRI, 0, DSK_HZ(190), 0}}},
    {{0, 64, 0, 50}, 4, 0, 2, D_HOLD, 0, 0x800, {{D_TRI, 1, DSK_HZ(330), 0}, {D_TRI, 0, DSK_HZ(280), 0}}},
    {{0, 62, 0, 50}, 4, 0, 2, D_HOLD, 0, 0x800, {{D_TRI, 1, DSK_HZ(440), 0}, {D_TRI, 0, DSK_HZ(380), 0}}},
    /* CLAVE: a short high triangle */
    {{0, 40, 0, 30}, 1, 0, 1, D_HOLD, 0, 0x800, {{D_TRI, 0, DSK_HZ(2500), 0}}},
    /* MARACAS: noise with a slower attack */
    {{24, 42, 0, 34}, 1, 0, 1, D_HOLD, 0, 0x800, {{D_NOI, 0, 0xFFFF, 0}}},
};

typedef struct {                               /* a hit: its drum at the hit's pitch, level, ADSR, SNAP, length */
    int32_t tune, gain, length;
    uint8_t adsr[4], sound, lane, level, snap;
} dsk_next_t;
typedef struct {
    sid_voice_t e;                             /* its envelope generator (sid_env_clock); e.env_prev: a tick ago */
    uint32_t ph, inc, noise, nb, pt, age;      /* accumulator (<< 8), its step, the noise register, its bits */
    int32_t frame, tune, gain, length;         /* cycles to the next frame (Q16); the hit's pitch (Q16), level
                                                * (Q15); a recorded hit's samples to its release (0: none) */
    uint16_t freq;                             /* the frequency register */
    int16_t slide;
    uint8_t adsr[4], ctrl, sound, lane, step, left, gate_left, snap, level;
    uint8_t restart;                           /* a hit waits (next) for the fade to end */
    uint8_t fade, fade_len;                    /* the fade out: blocks left + 1 (dsk_render ramps fade .. fade - 1) */
    dsk_next_t next;
} dsk_ch_t;
struct dsk_part {                              /* (dsk_part_t: eng_drum.c) */
    dsk_ch_t ch[DSK_NCH];
    int32_t ic1, ic2, xlp, xhp;                /* the filter's integrators, the C64's output stage */
    uint32_t dpos, dstep, age;                 /* DIGI: position (Q16 samples), its step a sample (Q16) */
    int32_t dgain;                             /* the digi's level (Q15) */
    uint16_t cycle_frac, dlen;                 /* SID cycles left over (Q16); the digi's samples */
    uint8_t ready, quiet, digi, dsmp, dlane, dlevel;
};
_Static_assert(sizeof(dsk_part_t) < sizeof(drum909_part_t) && sizeof(dsk_part_t) != sizeof(drum_lane_t) * DV_NLANE,
               "the SID kit's state: within the DRUM pool, its own size (resource_get keys on it)");
static dsk_part_t *dsk_part(uint32_t part);   /* engines.c: the part's runtime pool */
static uint32_t dsk_bytes(void) { return sizeof(dsk_part_t); }

static int dsk_done(const dsk_ch_t *c) { return c->e.state == SID_RELEASE && !c->e.env; }

static dsk_part_t *dsk_of(const track_t *t)
{
    dsk_part_t *K = dsk_part((uint32_t)(t - trk));
    if (K && !K->ready) {                      /* a chip after reset: voices released at zero */
        for (uint32_t k = 0; k < DSK_NCH; k++) {
            K->ch[k].e.state = SID_RELEASE;
            K->ch[k].e.hold_zero = 1;
            K->ch[k].e.exp_period = 1;
            K->ch[k].noise = 0x7fffff;
            K->ch[k].nb = sid_noise_bits(0x7fffff);
            K->ch[k].lane = 0xFF;
        }
        K->quiet = K->ready = 1;
    }
    return K;
}

static void dsk_gate(dsk_ch_t *c, uint32_t on)
{
    if (on && !(c->ctrl & DSK_GATE)) {         /* the gate opens: the attack, the rate counter cleared */
        c->e.state = SID_ATTACK;
        c->e.hold_zero = 0;
        c->e.rate_counter = 0;
    } else if (!on && (c->ctrl & DSK_GATE))
        c->e.state = SID_RELEASE;
}

static void dsk_freq(dsk_ch_t *c, int32_t f)
{
    c->freq = (uint16_t)clamp(f, 0, 0xffff);
    c->inc = (uint32_t)(((uint64_t)c->freq * SID_FREQ_K16) >> 16);
}

/* step k of the voice's drum: its waveform and gate (closed once the drum's gate frames are over), frequency,
 * slide (at the hit's pitch) */
static void dsk_load(dsk_ch_t *c, uint32_t k)
{
    const dsk_drum_t *d = &DSK_DRUMS[c->sound];
    const dsk_step_t *s = &d->s[k];
    uint32_t gate = (s->ctrl & DSK_GATE) && c->gate_left;
    c->step = (uint8_t)k;
    c->left = d->snap && !k ? c->snap : s->frames;
    if (s->freq)
        dsk_freq(c, (int32_t)(((uint64_t)s->freq * (uint32_t)c->tune) >> 16));
    c->slide = (int16_t)(((int64_t)s->slide * c->tune) >> 16);
    dsk_gate(c, gate);
    c->ctrl = (uint8_t)((s->ctrl & ~DSK_GATE) | gate);
}

/* a frame of the voice's program */
static void dsk_frame(dsk_ch_t *c)
{
    const dsk_drum_t *d = &DSK_DRUMS[c->sound];
    if (c->gate_left && !--c->gate_left) {
        dsk_gate(c, 0);
        c->ctrl &= (uint8_t)~DSK_GATE;
    }
    if (c->left && !--c->left) {               /* the step is over: the next, the loop, or the last holds */
        if (c->step + 1u < d->nstep)
            dsk_load(c, c->step + 1u);
        else if (d->loop < d->nstep)
            dsk_load(c, d->loop);
    } else if (c->slide)
        dsk_freq(c, c->freq + c->slide);
}

static void dsk_start(dsk_ch_t *c);
static void dsk_fade(dsk_ch_t *c, uint32_t len)   /* (a fade under way goes on) */
{
    if (!c->fade) {
        c->fade = (uint8_t)(len + 1u);
        c->fade_len = (uint8_t)len;
    }
}
/* a hit on note at velocity vel: DIGI's KICK and SNARE on $D418, any other drum on a voice */
static void dsk_hit(track_t *t, uint32_t note, uint32_t vel)
{
    dsk_part_t *K = dsk_of(t);
    int32_t st, tune;
    uint32_t role, lane, sound, k, best = DSK_NCH;
    const int8_t *pc;
    if (!K)
        return;
    role = drum_gm(t->p, note, &st);
    lane = DV_TYPE_LANE[role];
    sound = drum_sound_of(t, note) & 15u;
    pc = drum_patch[t - trk].c[sound];
    tune = clamp((st + pc[0] + drum_event_pitch) * 16 + (t->p[P_E1] - 64) * 3, -576, 576);
    K->quiet = 0;
    if (t->p[P_E6] && sound <= D9_SD) {
        K->dsmp = (uint8_t)sound;
        K->dpos = 0;
        K->dlen = DSK_DIGI_LEN[sound];
        K->dstep = tune ? (uint32_t)(((uint64_t)DSK_DIGI_STEP * dr_pow2(tune)) >> 16) : DSK_DIGI_STEP;
        K->dgain = dr8_level(vel, t->p[P_E5]);
        K->dlane = (uint8_t)lane;
        K->dlevel = (uint8_t)(127 - pc[3]);
        K->digi = 1;
        return;
    }
    for (k = 0; k < DSK_NCH; k++) {            /* the lane's drum (the hats': one) */
        dsk_ch_t *v = &K->ch[k];
        uint32_t l = v->restart ? v->next.lane : v->lane;
        if (l != lane && (lane - DV_HATC > 1u || l - DV_HATC > 1u))
            continue;
        if (v->restart)                        /* waiting: this hit instead */
            best = k;
        else if (!dsk_done(v))                 /* sounding: it fades out */
            dsk_fade(v, DSK_FADE);
    }
    for (k = 0; k < DSK_NCH && best == DSK_NCH; k++)   /* a silent voice */
        if (dsk_done(&K->ch[k]) && !K->ch[k].e.env_prev && !K->ch[k].fade)
            best = k;
    if (best == DSK_NCH)                       /* else the one furthest into its fade */
        for (k = 0; k < DSK_NCH; k++)
            if (K->ch[k].fade && !K->ch[k].restart && (best == DSK_NCH || K->ch[k].fade < K->ch[best].fade))
                best = k;
    if (best == DSK_NCH)                       /* else the oldest hit */
        for (best = 0, k = 1; k < DSK_NCH; k++)
            if (K->ch[k].age < K->ch[best].age)
                best = k;
    dsk_ch_t *c = &K->ch[best];
    dsk_next_t *h = &c->next;
    const dsk_drum_t *d = &DSK_DRUMS[sound];
    int32_t dec = (t->p[P_E3] - 64) / 2 + pc[1] / 2, snap = clamp(t->p[P_E4] + pc[2], 0, 191);
    h->sound = (uint8_t)sound;
    h->tune = tune ? (int32_t)dr_pow2(tune) : 65536;   /* (untuned: the program's registers exactly) */
    h->gain = dr8_level(vel, t->p[P_E5]);
    h->level = (uint8_t)(127 - pc[3]);
    h->adsr[0] = d->adsr[0];
    h->adsr[1] = (uint8_t)clamp(d->adsr[1] + dec, 0, 127);
    h->adsr[2] = d->adsr[2];
    h->adsr[3] = (uint8_t)clamp(d->adsr[3] + dec, 0, 127);
    h->snap = (uint8_t)((snap + 32) / 64);
    h->length = (int32_t)drum_event_length;
    h->lane = (uint8_t)lane;
    c->age = ++K->age;
    if (dsk_done(c) && !c->e.env_prev && !c->fade) {
        dsk_start(c);                          /* silent: at once */
    } else {
        c->restart = 1;                        /* sounding: after its fade */
        dsk_fade(c, DSK_STEAL);
    }
}

/* the voice's waiting hit starts */
static void dsk_start(dsk_ch_t *c)
{
    const dsk_next_t *h = &c->next;
    const dsk_drum_t *d = &DSK_DRUMS[h->sound];
    c->sound = h->sound;
    c->lane = h->lane;
    c->tune = h->tune;
    c->gain = h->gain;
    c->level = h->level;
    memcpy(c->adsr, h->adsr, sizeof c->adsr);
    c->snap = h->snap;
    c->length = h->length;
    c->restart = c->fade = 0;
    c->gate_left = d->gate;
    c->ph = 0;                                 /* the test bit: the accumulator from 0 */
    c->e.env = c->e.env_prev = 0;              /* the envelope from zero, as a hard restart leaves it */
    c->e.exp_counter = 0;
    c->e.exp_period = 1;
    c->e.state = SID_RELEASE;
    c->ctrl = 0;
    c->frame = (int32_t)SID_FRAME_Q16;
    dsk_load(c, d->snap && !c->snap ? 1u : 0u);
}

/* once per control tick (drum_amp): each voice's frames and envelope; 0 once the chip has gone quiet */
static __attribute__((noinline)) int dsk_tick(track_t *t)
{
    dsk_part_t *K = dsk_of(t);
    uint32_t c, cycles, k, busy = 0;
    int16_t q[P_COUNT];                        /* (the ADSR, where sid_env_clock reads a track's) */
    if (!K)
        return 0;
    c = K->cycle_frac + SID_CYC_Q16;
    K->cycle_frac = (uint16_t)c;
    cycles = c >> 16;
    for (k = 0; k < DSK_NCH; k++) {
        dsk_ch_t *v = &K->ch[k];
        v->e.env_prev = v->e.env;
        if (v->fade && !--v->fade) {           /* faded out: the hit waiting, or silence */
            if (v->restart) {
                dsk_start(v);
            } else {
                v->e.env = v->e.env_prev = 0;
                v->e.state = SID_RELEASE;
                v->e.hold_zero = 1;
                v->ctrl &= (uint8_t)~DSK_GATE;
                v->gate_left = 0;
            }
        }
        if (dsk_done(v))
            continue;
        if (v->length > 0 && (v->length -= CTL) <= 0) {   /* a recorded hit's length: the release */
            v->gate_left = 0;
            dsk_gate(v, 0);
            v->ctrl &= (uint8_t)~DSK_GATE;
        }
        if ((v->frame -= (int32_t)SID_CYC_Q16) <= 0) {
            v->frame += (int32_t)SID_FRAME_Q16;
            dsk_frame(v);
        }
        q[P_ATK] = v->adsr[0];
        q[P_DEC] = v->adsr[1];
        q[P_SUS] = v->adsr[2];
        q[P_REL] = v->adsr[3];
        sid_env_clock(&v->e, q, cycles);
        busy |= !dsk_done(v) || v->e.env_prev || v->fade;
    }
    return busy || K->digi || !K->quiet;
}

/* a lane level (P_LN0..) and a drum's LEVEL as one gain (Q15), as the 808's lanes take them */
static int32_t dsk_level(const int16_t *p, uint32_t lane, uint32_t level)
{
    int32_t lv = clamp(p[P_LN0 + (lane & (DV_NLANE - 1u))], 0, 127) * (int32_t)level / 127;
    return lv * lv * 32767 / (127 * 127);
}

/* the chip, one block: the voices (routed: through the filter), the master volume (DIGI), the output stage */
static __attribute__((noinline)) void dsk_render(track_t *t, int32_t *out, uint32_t n, const vmod_t *m,
                                                 const int16_t *p)
{
    dsk_part_t *K = dsk_part((uint32_t)(t - trk) % NPART);
    const int16_t (*dac)[64] = SID_WDAC[SID_6581];
    const uint16_t *gt = SID_FC_G[SID_6581];
    int32_t a0[DSK_NCH], a1[DSK_NCH], zero = SID_ZERO[SID_6581], knee = SID_KNEE[SID_6581];
    int32_t ic1, ic2, xlp, xhp, o = 0, fcr, g, kd, dg = 0, rest = p[P_E6] ? DSK_REST : 15, rcp;
    uint32_t i, k, live = 0, route = 0, dpos, dend;
    tsvf_t fc;
    if (!K || !K->ready)
        return;
    for (k = 0; k < DSK_NCH; k++) {
        const dsk_ch_t *c = &K->ch[k];
        int32_t lv;
        if (dsk_done(c) && !c->e.env_prev && !c->fade)
            continue;
        lv = mulq15(c->gain, dsk_level(p, c->lane, c->level)) >> 1;   /* (half scale) */
        a0[k] = mulq15(sid_edac(SID_6581, c->e.env_prev), lv);
        a1[k] = mulq15(sid_edac(SID_6581, c->e.env), lv);
        if (c->fade) {                         /* fading out: fade / len .. (fade - 1) / len this block */
            a0[k] = mulq15(a0[k], c->fade * 32767 / c->fade_len);
            a1[k] = mulq15(a1[k], (c->fade - 1) * 32767 / c->fade_len);
        }
        live |= 1u << k;
        route |= (uint32_t)DSK_DRUMS[c->sound].route << k;
    }
    fcr = clamp(p[P_E2] * 16 + (p[P_E2] >> 3), 0, 2047);   /* TONE: FC; RES: the resonance's 4 bits */
    g = gt[fcr >> 4] + (((gt[(fcr >> 4) + 1] - gt[fcr >> 4]) * (fcr & 15)) >> 4);
    kd = SID_RES_K[SID_6581][clamp(p[P_E7], 0, 127) >> 3];
    tsvf_coef_gk(&fc, g, kd);
    if (K->digi)
        dg = mulq15(K->dgain, dsk_level(p, K->dlane, K->dlevel));
    rcp = 65536 / rest;
    ic1 = K->ic1; ic2 = K->ic2; xlp = K->xlp; xhp = K->xhp;
    dpos = K->dpos;
    dend = (uint32_t)K->dlen << 16;
    for (i = 0; i < n; i++) {
        int32_t f = 0, d = 0, s;
        for (k = 0; k < DSK_NCH; k++) {
            dsk_ch_t *c = &K->ch[k];
            uint32_t q, w, wb;
            int32_t x;
            if (!(live >> k & 1u))
                continue;
            q = c->ph + c->inc;
            if (c->ctrl & DSK_NOISE) {             /* the noise register: clocked by accumulator bit 19 */
                if ((((c->ph ^ 0x08000000u) & 0x0fffffffu) + c->inc) >> 28) {
                    c->noise = sid_noise_advance(c->noise, c->ph, c->inc);
                    c->nb = sid_noise_bits(c->noise);
                }
                w = c->nb;
            } else {
                wb = (c->ctrl >> 4) & 7u;
                wb &= -wb;                         /* (one waveform: the programs combine none) */
                w = wb ? sid_wave_out(wb, 0, q, q, c->pt) : 0u;
            }
            c->ph = q;
            x = ((sid_dac(dac, w) - zero) * (a0[k] + (((a1[k] - a0[k]) * (int32_t)i) >> CTL_LOG2))) >> 15;
            if (route >> k & 1u)
                f += x;
            else
                d += x;
        }
        {                                          /* the filter: the SID engine's TPT state variable, LP */
            int32_t v3 = f - ic2, bp, lp;
            bp = soft_knee((fc.a1 * ic1 + fc.a2 * v3) >> 13, knee);
            lp = soft_knee(ic2 + ((fc.a2 * ic1 + fc.a3 * v3) >> 13), knee);
            ic1 = clamp(2 * bp - ic1, -150000, 150000);
            ic2 = clamp(2 * lp - ic2, -150000, 150000);
            s = lp + d;
        }
        if (K->digi) {                             /* $D418: the sample's step, the DC riding on it */
            uint32_t at = dpos >> 16, nib = (DSK_DIGI[DSK_DIGI_AT[K->dsmp] + (at >> 1)] >> ((at & 1u) << 2)) & 15u;
            int32_t vol = clamp(rest + ((((int32_t)nib - 8) * dg + 16384) >> 15), 0, 15);
            s = (int32_t)(((int64_t)(s * vol + (DSK_DC >> 2) * (vol - rest)) * rcp) >> 16);   /* (the DC at
                                                                       * half of half scale: a digi as loud as a drum) */
            if ((dpos += K->dstep) >= dend)
                K->digi = 0;
        }
        xlp += (int32_t)(((int64_t)(clamp(s, -131071, 131071) - xlp) * SID_XLP_A) >> 15);
        xhp += (int32_t)(((int64_t)((xlp << 8) - xhp) * SID_XHP_A) >> 16);
        o = clamp(xlp - (xhp >> 8), -32767, 32767);
        out[i] += voice_amp(soft_knee(o * 3, 24000), m, i) << 1;   /* (x3: the kits' level from half scale; doubled
                                                                       * after voice_amp, whose Q15 product holds
                                                                       * 16 bits: the knee's top, 40383, x 2 would wrap) */
    }
    K->ic1 = ic1; K->ic2 = ic2; K->xlp = xlp; K->xhp = xhp;
    K->dpos = dpos;
    K->quiet = !live && !K->digi && o > -32 && o < 32 && xlp > -128 && xlp < 128 && ic1 > -256 && ic1 < 256 &&
               ic2 > -256 && ic2 < 256;
}
