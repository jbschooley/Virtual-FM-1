/* SPDX-License-Identifier: GPL-3.0-only */
/* Prophet prototype: native patches and five voices, with two provisional
 * filter characters. Stored engine ID 19 is independent of legacy ANALOG.
 * MELODEE_PROPHET_PROTOTYPE additionally aliases ID 0 for measurement rigs. */
#include "prophet_patch.c"
#include "melodee_prophet_factory.h"   /* Sequential's 200 v1.03 programs (tools/gen_prophet_factory.py) */
#define P5_POLY 5u
typedef struct { int32_t value; uint8_t stage; } p5_env_t;
typedef struct {
    uint32_t phase[2];
    int32_t z[4], triangle, noise, sync_tail;
    int32_t drift, filter_drift, amp_drift, env_drift;
    int32_t filter_prev, amp_prev, glide_pitch, last_pcm;
    int32_t coeff_cut, coeff_k, coeff_g[4], coeff_inv;
    uint32_t coeff_valid;
    p5_env_t filter, amp;
} p5_voice_t;
typedef struct { p5_voice_t voice[P5_POLY]; uint32_t lfo; int32_t lfo_value, last_pitch, mod_noise; int16_t wheel[CTL * P5_OVERSAMPLE]; uint16_t wheel_pitch[CTL * P5_OVERSAMPLE]; uint8_t value[55]; } p5_part_t;
static p5_part_t *p5_part(uint32_t part);
static p5_patch_t p5_patch[NPART] __attribute__((section(".pool")));
static uint8_t p5_ready[NPART];
static p5_patch_t *p5_patch_of(const track_t *t)
{
    uint32_t k = (uint32_t)(t - trk) % NPART;
    if (!p5_ready[k]) { p5_patch_init(&p5_patch[k]); p5_ready[k] = 1; }
    return &p5_patch[k];
}
static inline __attribute__((always_inline)) p5_voice_t *p5_voice(track_t *t, voice_t *v)
{
    p5_part_t *part = p5_part((uint32_t)(t - trk));
    return part ? &part->voice[(uint32_t)(v - t->v) % P5_POLY] : 0;
}
/* Forced-inline local helpers keep RAM code from branching back to XIP.
 * Arithmetic matches dsp.c; the common renderer remains unchanged. */
static inline __attribute__((always_inline)) int32_t p5_fast_mulq15(int32_t a, int32_t b) { return (a * b) >> 15; }
static inline __attribute__((always_inline)) int32_t p5_fast_clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
/* Normalize before division, retaining at least 16 significant bits. Unlike
 * dividing the denominator by 32768 first, this stays bounded at low rates. */
static inline __attribute__((always_inline)) uint32_t p5_fraction_q15(uint32_t distance,uint32_t inc)
{
    uint32_t shift=(uint32_t)__builtin_clz(inc);if(shift>15u)shift=15u;
    uint32_t value=(distance<<shift)/(inc>>(15u-shift));
    return value>32768u?32768u:value;
}
static inline __attribute__((always_inline)) int32_t p5_blep_fraction(uint32_t distance,uint32_t inc)
{
    /* At high rates the short division loses at most six Q15 correction
     * units. Low rates require normalization to avoid the earlier overflow. */
    if(inc>=(1u<<27)){
        uint32_t x=distance/(inc>>15);
        return (int32_t)(x>32768u?32768u:x);
    }
    return (int32_t)p5_fraction_q15(distance,inc);
}
static inline __attribute__((always_inline)) int32_t p5_fast_blep(uint32_t ph, uint32_t inc)
{
    int32_t x;
    if (!inc)
        return 0;
    if (ph < inc) {
        x = p5_blep_fraction(ph,inc);                /* 0..32768 */
        return x + x - ((x * x) >> 15) - 32768;
    }
    if (ph > 0xFFFFFFFFu - inc) {
        x = -p5_blep_fraction(0xFFFFFFFFu-ph,inc);
        return ((x * x) >> 15) + x + x + 32768;
    }
    return 0;
}
static inline __attribute__((always_inline)) int32_t p5_fast_osc_saw(uint32_t ph, uint32_t inc)
{
    return ((int32_t)(ph >> 16) - 32768) - p5_fast_blep(ph, inc);
}
static inline __attribute__((always_inline)) int32_t p5_fast_osc_pulse(uint32_t ph, uint32_t inc, uint32_t pw)
{
    return (p5_fast_osc_saw(ph, inc) - p5_fast_osc_saw(ph + pw, inc)) >> 1;
}
static inline __attribute__((always_inline)) int32_t p5_fast_softclip(int32_t x)
{
    int32_t a = x < 0 ? -x : x, i = (a + 16) >> 5;
    int32_t y = P5_TANH_Q15[i > 2048 ? 2048 : i];
    return x < 0 ? -y : y;
}
static inline __attribute__((always_inline)) uint32_t p5_fast_noise32(int32_t *st)
{
    uint32_t s = (uint32_t)*st;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    *st = (int32_t)s;
    return s;
}
static inline __attribute__((always_inline)) int32_t p5_fast_amp_at(const vmod_t *m, uint32_t i)
{
    int32_t x = (m->amp1 - m->amp0) * (int32_t)i;
    return m->amp0 + ((x + ((x >> 31) & (CTL - 1))) >> CTL_LOG2);
}
static inline __attribute__((always_inline)) int32_t p5_fast_voice_amp(int32_t s, const vmod_t *m, uint32_t i)
{
    return p5_fast_mulq15(p5_fast_mulq15(s, p5_fast_amp_at(m, i)), VOICE_FS);
}

/* Stored knobs are 0..127 (Sequential's factory data uses the whole range,
 * e.g. levels and sustains of 127), not the 0..120 of the MIDI guide. */
static int32_t p5_value(const uint8_t *p,uint32_t k)
{
    return p[k]>127u?127:p[k];
}
static inline __attribute__((always_inline)) int32_t p5_m12(int32_t a, int32_t b) { return (a * b) >> 12; }

#define P5_ENV_END (1 << 14)   /* -60 dB: a release has ended */
/* Attack is a linear ramp; decay and release are exponential, except the
 * Rev 1/2 filter envelope (SSM), which Sequential describes as almost linear. */
static int32_t p5_env_tick(p5_env_t *e, int gate, const uint8_t *p,
                          uint32_t a, uint32_t d, uint32_t s, uint32_t r, int32_t variation, const uint8_t *value, int ssm)
{
    if (!gate && e->stage != 0u) e->stage = 3;
    int32_t target = value[s] * (1 << 24) / 127;
    uint32_t rel = p[P5_RELEASE_ON] ? (uint32_t)value[r] : 16u;     /* release off: fast (3 ms) */
    if (e->stage == 1u) {
        int32_t step = (int32_t)P5_ENV_ATK[value[a]];
        step += (step >> 8) * variation / 128;
        e->value += step;
        if (e->value >= (1 << 24)) { e->value = 1 << 24; e->stage = 2; }
    } else if (ssm && e->stage >= 2u) {
        int32_t step = (int32_t)P5_ENV_SSM[e->stage == 2u ? value[d] : rel];
        step = clamp(step + (step >> 8) * variation / 128, 1, 1 << 24);
        if (e->stage == 3u) target = 0;
        e->value = e->value > target ? (e->value - step > target ? e->value - step : target)
                                     : (e->value + step < target ? e->value + step : target);
        if (e->stage == 3u && e->value < P5_ENV_END) { e->value = 0; e->stage = 0; }
    } else if (e->stage == 2u) {
        uint32_t coeff=P5_ENV_EXP[value[d]];
        coeff=(uint32_t)clamp((int32_t)coeff+((int32_t)coeff>>8)*variation/128,1,65535);
        e->value += mulq16(target - e->value, coeff);
    } else if (e->stage == 3u) {
        uint32_t coeff=P5_ENV_EXP[rel];
        coeff=(uint32_t)clamp((int32_t)coeff+((int32_t)coeff>>8)*variation/128,1,65535);
        e->value -= mulq16(e->value, coeff);
        if (e->value < P5_ENV_END) { e->value = 0; e->stage = 0; }
    }
    return e->value >> 9;
}

static void p5_note_on(track_t *t, voice_t *v)
{
    p5_voice_t *s = p5_voice(t, v);
    const uint8_t *p = p5_patch_of(t)->raw;
    if (!s) return;
    if (!voice_was) {
        memset(s, 0, sizeof *s);
        s->phase[0]=v->ph[0];s->phase[1]=v->ph[1];
        s->triangle = 32767;
        /* Thermal excitation lets resonance start with the oscillator mixer
         * shut, as native self-oscillating factory programs require. */
        if(p[P5_RESONANCE]>=110u && !p[P5_LEVEL_A] && !p[P5_LEVEL_B] && !p[P5_NOISE]){
            /* The physical filter is already oscillating behind the closed
             * VCA. Seed that state when waking an otherwise silent voice. */
            s->z[0]=16384;s->z[2]=-16384;
        }
        s->noise = (int32_t)(0x5A1793u + v->age * 17u + (uint32_t)(v-t->v) * 101u);
    }
    if (!voice_was) {
        p5_part_t *part=p5_part((uint32_t)(t-trk));
        s->glide_pitch=part->last_pitch ? part->last_pitch : v->pitch16;
    }
    p5_part((uint32_t)(t-trk))->last_pitch=v->pitch16;
    s->filter.stage = s->amp.stage = 1;
    /* 0 is the stable Rev 4 position, 127 the loose Rev 1: most factory
     * programs use 0, many 42 (Rev 3), few 127. */
    int32_t vintage = p5_value(p, P5_VINTAGE);
    /* Fixed mismatch for the life of the voice, deterministic per note/slot.
     * This is an initial engineering range, awaiting reference calibration. */
    s->drift = ((int32_t)(p5_fast_noise32(&s->noise) & 255u) - 128) * vintage / 1024;
    s->filter_drift = ((int32_t)(p5_fast_noise32(&s->noise) & 255u) - 128) * vintage / 32;
    s->amp_drift = ((int32_t)(p5_fast_noise32(&s->noise) & 255u) - 128) * vintage / 16;
    s->env_drift = ((int32_t)(p5_fast_noise32(&s->noise) & 255u) - 128) * vintage / 128;
}
static __attribute__((noinline)) int32_t p5_env_source(track_t *t,voice_t *v) {p5_voice_t *s=p5_voice(t,v);return s?s->amp_prev:0;}
static int p5_done(track_t *t, voice_t *v)
{
    p5_voice_t *s = p5_voice(t, v);
    return !s || (!v->gate && s->amp.stage == 0u);
}
/* Triangle is bipolar; saw and square are positive with the same
 * peak-to-peak swing (Q14: triangle +-16384, saw/square 0..32767).
 * Preserve the level of a single selected waveform when combining sources. */
static inline __attribute__((always_inline)) int32_t p5_lfo_wave(uint32_t phase,const uint8_t *p)
{
    int32_t y=0;uint32_t n=0;
    if(p[P5_LFO_SAW]){y+=(int32_t)(phase>>17);n++;}
    if(p[P5_LFO_TRI]){int32_t v=(int32_t)(phase>>17);y+=phase<0x80000000u?v*2-16384:49151-v*2;n++;}
    if(p[P5_LFO_PULSE]){y+=phase<0x80000000u?32767:0;n++;}
    return n==3u ? y/3 : n==2u ? y>>1 : y;
}
/* 2^(x/65536) in Q15 for |x| < 65536 (Q16 octaves): the wheel's pitch ratio. */
static inline __attribute__((always_inline)) uint16_t p5_ratio_q15(int32_t x)
{
    x=p5_fast_clamp(x,-65535,65535);
    uint32_t f=(uint32_t)x&65535u,i=f>>8;
    uint32_t r=P5_EXP2_Q14[i]+(((P5_EXP2_Q14[i+1u]-P5_EXP2_Q14[i])*(f&255u)+128u)>>8);
    return (uint16_t)(x<0?r:r*2u);
}
/* Rev-4 vibrato: a full-amount triangle swings +-235 cents (measured at
 * initial amounts 18..55 against a factory demo, see gen_tables.py). */
#define P5_WHEEL_PITCH_Q14 12834    /* 235/1200 octave per 16384, in Q16 octaves */
/* The Prophet's wheel source is shared across all voices in a part. Calculate
 * each audio-rate sample once, including combined waveforms and noise. */
static __attribute__((section(".dsp_text"))) void p5_mod_samples(p5_part_t *s,const uint8_t *p,uint32_t inc,int32_t amount,int32_t mix)
{
    uint32_t phase=s->lfo;if(!s->mod_noise)s->mod_noise=0x517953;
    for(uint32_t i=0;i<CTL*P5_OVERSAMPLE;i++){
        int32_t noise=(int32_t)(p5_fast_noise32(&s->mod_noise)>>17)-16384;
        int32_t w=p5_fast_mulq15(p5_fast_mulq15(p5_lfo_wave(phase,p),32767-mix)+p5_fast_mulq15(noise,mix),amount);
        s->wheel[i]=(int16_t)w;
        /* Exponential, so a triangle goes equally sharp and flat in cents.
         * Calculate once per part, rather than for each voice. */
        s->wheel_pitch[i]=p5_ratio_q15((w*P5_WHEEL_PITCH_Q14+8192)>>14);
        phase+=inc;
    }
    s->lfo=phase;s->lfo_value=p5_lfo_wave(phase,p);
}
static void p5_update_bend(track_t *t);
static void p5_block(track_t *t)
{
    p5_part_t *s=p5_part((uint32_t)(t-trk));const uint8_t *p=p5_patch_of(t)->raw;
    if(!s)return;p5_update_bend(t);
    /* Raw-to-DSP clamping is part-wide; envelopes and all five voices share it. */
    for(uint32_t k=0;k<55u;k++)s->value[k]=(uint8_t)p5_value(p,k);
    uint32_t inc=P5_LFO_INC[p5_value(p,P5_LFO_RATE)]/P5_OVERSAMPLE;
    int32_t amount=P5_AMOUNT_Q15[clamp(p5_value(p,P5_LFO_INITIAL)+t->mw+(p[P5_PRESS_LFO]?t->at:0),0,127)];
    void (*volatile run)(p5_part_t *,const uint8_t *,uint32_t,int32_t,int32_t)=p5_mod_samples;
    run(s,p,inc,amount,p5_value(p,P5_WHEEL_MIX)*258);
}
static uint32_t p5_cap(const track_t *t)
{
    const uint8_t *p=p5_patch_of(t)->raw;
    return p[P5_UNISON] && t->p[P_VOICE]==V_UNISON ?
        (uint32_t)clamp(p[P5_UNISON_COUNT]?p[P5_UNISON_COUNT]:5,1,5) : 5u;
}
static void p5_legato(track_t *t,voice_t *v)
{
    if(p5_patch_of(t)->raw[P5_RETRIGGER]&1u){p5_voice_t *s=p5_voice(t,v);if(s)s->filter.stage=s->amp.stage=1;}
}
/* Native performance controls become the track's allocator settings at load
 * or native edit. Afterwards Melodee's VOICE/PRIO/DETUNE pages can override. */
static void p5_track_accept(track_t *t)
{
    const uint8_t *p=p5_patch_of(t)->raw;
    t->p[P_VOICE]=p[P5_UNISON]?V_UNISON:V_POLY;
    t->p[P_PRIO]=p[P5_RETRIGGER]<2u?1:0;
    t->p[P_DETUNE]=clamp(p[P5_UNISON_DETUNE],0,7)*127/7;
    t->p[P_ALLOC]=1; /* repeated keys reuse a voice, as the original does */
}

/* Four trapezoidal one-poles with an algebraic feedback solution. Curtis:
 * concentrated input saturation with resonance gain loss. SSI: distributed
 * stage saturation and different resonance compensation. These are separate
 * provisional character models, NOT calibrated chip emulations. Q12 products
 * stay within int32 bounds; state clamps are protection, not the normal knee. */
static inline __attribute__((always_inline)) int32_t p5_filter_core(p5_voice_t *s, int32_t input, int32_t cutoff, int32_t k, int32_t gain, int curtis)
{
    cutoff = p5_fast_clamp(cutoff, 0, 127 << 8);
    int32_t g,g2,g3,g4,reciprocal;
    if(s->coeff_valid && cutoff==s->coeff_cut && k==s->coeff_k){
        g=s->coeff_g[0];g2=s->coeff_g[1];g3=s->coeff_g[2];g4=s->coeff_g[3];reciprocal=s->coeff_inv;
    }else{
        uint32_t i=(uint32_t)cutoff>>8;g=P5_TPT_G[i];
        if(i<127u)g+=(P5_TPT_G[i+1u]-g)*(cutoff&255)>>8;
        g2=p5_m12(g,g);g3=p5_m12(g2,g);g4=p5_m12(g3,g);
        uint32_t d=(uint32_t)p5_m12(k,g4),index=d>>5;reciprocal=P5_INV_DEN[index];
        reciprocal+=((P5_INV_DEN[index+1u]-reciprocal)*(int32_t)(d&31u))>>5;
        s->coeff_g[0]=g;s->coeff_g[1]=g2;s->coeff_g[2]=g3;s->coeff_g[3]=g4;
        s->coeff_cut=cutoff;s->coeff_k=k;s->coeff_inv=reciprocal;s->coeff_valid=1;
    }
    int32_t sigma=p5_m12(4096-g,p5_m12(g3,s->z[0])+p5_m12(g2,s->z[1])+p5_m12(g,s->z[2])+s->z[3]);
    input=p5_fast_mulq15(input,gain);
    int32_t drive=p5_fast_clamp(input-p5_m12(k,sigma),-262143,262143);
    int32_t u=(drive*reciprocal)>>13;
    u = p5_fast_softclip(u);
    /* Fixed four-pole stages: no indexed loop or per-stage mode branch. */
#define P5_STAGE(j, nonlinear) do { \
    int32_t delta=p5_m12(g,u-s->z[j]); \
    int32_t y=s->z[j]+delta; \
    s->z[j]=p5_fast_clamp(y+delta,-60000,60000); \
    u=(nonlinear)?p5_fast_softclip(y):y; \
} while(0)
    if(curtis){P5_STAGE(0,0);P5_STAGE(1,0);P5_STAGE(2,0);P5_STAGE(3,0);}
    else {P5_STAGE(0,1);P5_STAGE(1,1);P5_STAGE(2,1);P5_STAGE(3,1);}
#undef P5_STAGE
    return p5_fast_softclip(u);
}

static int32_t p5_filter(p5_voice_t *s,int32_t input,int32_t cutoff,int32_t resonance,int curtis)
{
    return p5_filter_core(s,input,cutoff,resonance*(curtis?19200:18800)/127,
                          32767-resonance*(curtis?100:45),curtis);
}

static inline __attribute__((always_inline)) int32_t p5_wave(uint32_t phase, uint32_t inc, uint32_t pw, int saw, int pulse, int tri, int32_t *triangle)
{
    /* Simultaneous waveforms share their discontinuity correction. */
    int32_t y=0, a=p5_fast_osc_saw(phase,inc), square=0;
    if(saw)y+=a>>1;
    if(pulse){square=(a-p5_fast_osc_saw(phase+pw,inc))>>1;y+=square;}
    if(tri){
        if(!pulse || pw!=0x80000000u)square=(a-p5_fast_osc_saw(phase+0x80000000u,inc))>>1;
        *triangle=p5_fast_clamp(p5_fast_mulq15(*triangle,32767)+
            (int32_t)(((int64_t)square*2*(int32_t)(inc>>15))>>15),-40000,40000);
        y+=*triangle>>1;
    }
    return y;
}

/* Normalize the fraction to a 32-bit divide. Both numerator and denominator
 * retain at least 16 significant bits; error is at most one Q15 unit. */
static inline __attribute__((always_inline)) uint32_t p5_sync_fraction(uint32_t distance,uint32_t inc)
{
    return p5_fraction_q15(distance,inc);
}
static inline __attribute__((always_inline)) uint32_t p5_pw(int32_t value)
{
    /* Avoid degenerate pulse widths; signed modulation is applied before p5_fast_clamp. */
    return (uint32_t)p5_fast_clamp(value, 1024, 31743) << 17;
}
/* Poly-Mod to oscillator A frequency is exponential, as on the CEM3340:
 * base * 2^(x/4096), x in Q12 octaves. 32-bit halves only (no 64-bit shifts). */
static __attribute__((section(".dsp_text"))) uint32_t p5_inc(uint32_t base, int32_t x)
{
    if (!x) return base;
    x = p5_fast_clamp(x, -8 * 4096, 6 * 4096);
    int32_t oct = x >> 12;
    uint32_t f = (uint32_t)x & 4095u, i = f >> 4;
    uint32_t r = P5_EXP2_Q14[i] + (((P5_EXP2_Q14[i + 1u] - P5_EXP2_Q14[i]) * (f & 15u)) >> 4);
    uint32_t y = (base >> 14) * r + (((base & 16383u) * r) >> 14);   /* base * 1..2, < 2^32 */
    if (oct < 0) return (y >> -oct) ? y >> -oct : 1u;
    return y > (1932735280u >> oct) ? 1932735280u : y << oct;
}
static inline __attribute__((always_inline)) uint32_t p5_wheel_inc(uint32_t base,uint16_t ratio)
{
    /* Base is capped below 2^31; both partial products fit uint32. */
    uint32_t inc=(base>>15)*ratio+(((base&32767u)*ratio)>>15);
    return inc>1932735280u?1932735280u:(uint32_t)inc;
}
/* Summed native waveforms can exceed Q15 before the mixer saturation. */
static inline __attribute__((always_inline)) int32_t p5_wave_gain(int32_t wave,int32_t gain)
{
    /* gain is 0..32766. Split the signed waveform into whole/fractional Q15
     * parts for an exact product without overflow or a target 64-bit helper. */
    return (wave>>15)*gain+(((wave&32767)*gain)>>15);
}
typedef struct { int32_t f0, a0, f1, a1, ia, ib, cut, k, filter_gain, filter_velocity, amp_velocity, env_amount, pa_env, pa_b, pf_env, pf_b, pw_env, pw_b, pwm_a, pwm_b, la, lb, ln; const int16_t *wheel; const uint16_t *wheel_pitch; } p5_render_params_t;
static __attribute__((section(".dsp_text"))) void p5_samples(p5_voice_t *restrict s, const uint8_t *restrict p, int32_t *restrict out, uint32_t n, const vmod_t *m, const p5_render_params_t *c)
{
    int32_t f0=c->f0;
    int32_t a0=c->a0;
    int32_t f1=c->f1;
    int32_t a1=c->a1;
    int32_t ia=c->ia;
    int32_t ib=c->ib;
    int32_t cut=c->cut;
    int32_t k=c->k, filter_gain=c->filter_gain;
    int32_t filter_velocity=c->filter_velocity;
    int32_t amp_velocity=c->amp_velocity;
    int32_t env_amount=c->env_amount;
    int32_t pa_env=c->pa_env, pa_b=c->pa_b, pf_env=c->pf_env, pf_b=c->pf_b, pw_env=c->pw_env, pw_b=c->pw_b;
    int32_t pwm_a=c->pwm_a;
    int32_t pwm_b=c->pwm_b;
    int32_t la=c->la;
    int32_t lb=c->lb;
    int32_t ln=c->ln;
    int32_t last=0;
    for (uint32_t i=0; i<n; i++) {
        int32_t fe=f0+(((f1-f0)*(int32_t)i)>>CTL_LOG2);
        int32_t ae=a0+(((a1-a0)*(int32_t)i)>>CTL_LOG2);
        int32_t sum=0;
        for (uint32_t os=0; os<P5_OVERSAMPLE; os++) {
            int32_t noise=(int32_t)(p5_fast_noise32(&s->noise)>>17)-16384;
            int32_t lfo=c->wheel[i*P5_OVERSAMPLE+os];
            uint16_t wheel_pitch=c->wheel_pitch[i*P5_OVERSAMPLE+os];
            uint32_t db=p[P5_WHEEL_FREQ_B]?p5_wheel_inc(ib,wheel_pitch):(uint32_t)ib;
            int32_t b=p5_wave(s->phase[1],db,p5_pw(pwm_b+(p[P5_WHEEL_PW_B]?lfo:0)),p[P5_SAW_B],p[P5_PULSE_B],p[P5_TRI_B],&s->triangle);
            int32_t bn=b*2;   /* one full oscillator B wave: +-32768 */
            uint32_t da=p[P5_POLY_FREQ]?p5_inc(ia,p5_fast_mulq15(fe,pa_env)+p5_wave_gain(bn,pa_b)):(uint32_t)ia;
            if(p[P5_WHEEL_FREQ_A])da=p5_wheel_inc(da,wheel_pitch);
            uint32_t pw=p5_pw(pwm_a+(p[P5_POLY_PW]?p5_fast_mulq15(fe,pw_env)+p5_wave_gain(b,pw_b):0)+(p[P5_WHEEL_PW_A]?lfo:0));
            int32_t unused=0;
            int32_t a=p5_wave(s->phase[0],da,pw,p[P5_SAW_A],p[P5_PULSE_A],0,&unused)+s->sync_tail;
            s->sync_tail=0;
            uint32_t nb=s->phase[1]+db, na=s->phase[0]+da;
            if (p[P5_SYNC] && nb<s->phase[1] && db) {
                uint32_t frac=p5_sync_fraction(0u-s->phase[1],db);
                uint32_t at=s->phase[0]+(uint32_t)(((uint64_t)da*frac)>>15);
                int32_t before=p5_wave(at,da,pw,p[P5_SAW_A],p[P5_PULSE_A],0,&unused);
                int32_t after=p5_wave(0,da,pw,p[P5_SAW_A],p[P5_PULSE_A],0,&unused);
                int32_t jump=after-before, remain=32768-(int32_t)frac;
                /* Fractional discontinuity correction over this/next sub-sample. */
                a += p5_fast_mulq15((jump>>1),p5_fast_mulq15(remain,remain));
                s->sync_tail=-p5_fast_mulq15((jump>>1),p5_fast_mulq15((int32_t)frac,(int32_t)frac));
                na=(uint32_t)(((uint64_t)da*(uint32_t)remain)>>15);
            }
            s->phase[0]=na; s->phase[1]=nb;
            int32_t x=p5_wave_gain(a,la)+p5_wave_gain(b,lb)+p5_fast_mulq15(noise,ln);
            int32_t fc=cut+p5_fast_mulq15(p5_fast_mulq15(fe,filter_velocity),env_amount);
            if (p[P5_POLY_FILTER]) fc+=p5_fast_mulq15(fe,pf_env)+p5_wave_gain(bn,pf_b);
            if (p[P5_WHEEL_FILTER]) fc+=(lfo*7)>>3;   /* +-4 octaves at full amount */
            sum+=p5_filter_core(s,p5_fast_softclip(x),fc,k,filter_gain,!!p[P5_FILTER_REV]);
        }
        #if P5_OVERSAMPLE == 2
        sum >>= 1;
        #endif
        int32_t gain=p5_fast_mulq15(p5_fast_mulq15(ae,amp_velocity),32767+s->amp_drift);
        last=p5_fast_voice_amp(p5_fast_mulq15(sum,gain),m,i)*2;
        out[i]+=last;
    }
    s->last_pcm=last;
}
/* XIP stalls dominated the first hardware run. This bounded hot path uses
 * the existing RAMTEXT region; startup copies it before audio starts. */
static void p5_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    p5_voice_t *s=p5_voice(t,v);
    p5_part_t *part=p5_part((uint32_t)(t-trk)%NPART);
    const uint8_t *p=p5_patch_of(t)->raw;
    if (!s || !part) return;
    /* A budget victim is already inactive when the common renderer delivers
     * its final fade block. Fade its last PCM sample to zero instead of running
     * an extra complete Prophet voice alongside the replacement track. This
     * keeps the first sample continuous and ends exactly at zero. Engine-switch
     * fades and active native releases continue through the full synth. */
    if(!v->active && !m->amp1){
        if(n>1u){
            int32_t step=s->last_pcm/(int32_t)(n-1u);
            for(uint32_t i=0;i<n-1u;i++)out[i]+=s->last_pcm-step*(int32_t)i;
        }
        s->last_pcm=0;return;
    }
    const uint8_t *q=part->value;
    int32_t f0=s->filter_prev, a0=s->amp_prev;
    int curtis=!!p[P5_FILTER_REV];
    int32_t f1=p5_env_tick(&s->filter,v->gate,p,P5_ATTACK_FILTER,P5_DECAY_FILTER,P5_SUSTAIN_FILTER,P5_RELEASE_FILTER,s->env_drift,q,!curtis);
    int32_t a1=p5_env_tick(&s->amp,v->gate,p,P5_ATTACK_AMP,P5_DECAY_AMP,P5_SUSTAIN_AMP,P5_RELEASE_AMP,s->env_drift,q,0);
    s->filter_prev=f1; s->amp_prev=a1;
    int32_t glide=0;
    if(p[P5_GLIDE]){
        int32_t target=v->pitch16,d=target-s->glide_pitch;
        int32_t step=(int32_t)(ENV_LIN[q[P5_GLIDE]]>>14);if(step<1)step=1;
        s->glide_pitch+=clamp(d,-step,step);glide=s->glide_pitch-v->pitch_cur;
    }else s->glide_pitch=v->pitch16;
    /* Keyed coarse tuning has semitone detents, with adjacent raw values
     * selecting the same detent (24/25 are both the C1 anchor). Its four
     * octaves end at raw 96. B's keyboard-off knob instead spans nine octaves
     * continuously. Only the rendered pitch is quantized; patch bytes stay exact. */
    int32_t coarse_a=(clamp(q[P5_FREQ_A],0,96)/2-12)*16;
    int32_t coarse_b=(clamp(q[P5_FREQ_B],0,96)/2-12)*16;
    int32_t pitch_a = m->pitch16 + glide + coarse_a + t->p[P_E2]*16;
    int32_t pitch_b = (p[P5_KEY_B] ? m->pitch16+glide+coarse_b : 12*16+q[P5_FREQ_B]*108*16/127) + t->p[P_E3]*16;
    uint32_t ia = cents_inc(pitch_a, s->drift, m->fine) / P5_OVERSAMPLE;
    uint32_t ib = cents_inc(pitch_b, q[P5_FINE_B]*100/128-s->drift, m->fine) / P5_OVERSAMPLE;
    if (p[P5_LOW_B]) ib >>= 10;
    int32_t filter_velocity=p[P5_VEL_FILTER] ? v->mvel*258 : 32767;
    int32_t amp_velocity=p[P5_VEL_AMP] ? v->mvel*258 : 32767;
    /* Cutoff in Q8 steps of the coefficient table (14.02 per octave, 30 Hz
     * at 0). Fitted to Rev-4 recordings: the Curtis knob puts the lowest C
     * (MIDI 36) near 870 Hz * 2^((raw-67)/15.2); SSI sits ~0.7 octave higher
     * below the middle of the knob, converging at the top. */
    int32_t c0=q[P5_CUTOFF];
    int32_t cut=1618+c0*236+(curtis?0:(127-c0>66?66:127-c0)*38) + m->cutoff + t->p[P_E0]*256 + s->filter_drift;
    /* Keyboard tracking pivots on the lowest C, where the keyboard CV is 0 V:
     * full tracking doubles Hz per keyboard octave; half tracks sqrt(2). */
    cut += (m->pitch16-36*16) * p5_fast_clamp(p[P5_KEY_FILTER],0,2)*P5_KEYTRACK_HALF_Q7/128;
    if(p[P5_PRESS_FILTER])cut+=t->at*96;
    int32_t resonance=p5_fast_clamp(q[P5_RESONANCE]+t->p[P_E1],0,127);
    int32_t k=resonance*(curtis?19200:18800)/127;
    int32_t filter_gain=32767-resonance*(curtis?100:45);
    int32_t env_amount=q[P5_ENV_FILTER]*165;     /* 0.046 octave per step (Rev-4 recordings) */
    /* Poly-Mod. Oscillator A (Q12 octaves): filter envelope 2 octaves at 96,
     * oscillator B +-1.1 octave at 127. Filter (Q8 steps): 8 octaves at 127. */
    int32_t pa_env=q[P5_POLY_ENV]*85, pa_b=q[P5_POLY_B]*35;
    int32_t pf_env=q[P5_POLY_ENV]*226, pf_b=q[P5_POLY_B]*226;
    int32_t pw_env=q[P5_POLY_ENV]*64, pw_b=q[P5_POLY_B]*258;
    int32_t width_mod=m->shape-(64<<8);
    int32_t pwm_a=q[P5_PW_A]*258+t->p[P_E4]*256+width_mod, pwm_b=q[P5_PW_B]*258+t->p[P_E5]*256+width_mod;
    int32_t la=clamp(q[P5_LEVEL_A]+t->p[P_E6],0,127)*258, lb=clamp(q[P5_LEVEL_B]+t->p[P_E7],0,127)*258, ln=q[P5_NOISE]*258;
    const p5_render_params_t c={f0,a0,f1,a1,ia,ib,cut,k,filter_gain,filter_velocity,amp_velocity,env_amount,pa_env,pa_b,pf_env,pf_b,pw_env,pw_b,pwm_a,pwm_b,la,lb,ln,part->wheel,part->wheel_pitch};
    /* An indirect call crosses the XIP/RAM distance beyond direct branch range. */
    void (*volatile run)(p5_voice_t *,const uint8_t *,int32_t *,uint32_t,const vmod_t *,const p5_render_params_t *)=p5_samples;
    run(s,p,out,n,m,&c);
}

/* INIT, then Sequential's factory programs in their order (111..588 on the
 * Prophet: preset k is program k-1 of the v1.03 bank). Dry, as the Prophet. */
#define P5_FACTORY_PRESET(n) {n,{0,0,0,0,0,0,0,0},{0,0,127,0},0,0,FX(0,0,0,0),PAT(1)},
static const preset_t P5_TEST_PRESETS[] = {
    {"INIT PROPHET",{0,0,0,0,0,0,0,0},{0,0,127,0},0,0,FX(0,0,0,0),PAT(1)},
    P5_FACTORY_PRESETS(P5_FACTORY_PRESET)
};
_Static_assert(NELEM(P5_TEST_PRESETS)==P5_FACTORY_N+1u, "Prophet factory presets");
/* A factory preset's native program; preset 0 is INIT. */
static void p5_preset_loaded(track_t *t,uint32_t pi)
{
    p5_patch_t *p=p5_patch_of(t);
    if(pi && pi<=P5_FACTORY_N)*p=P5_FACTORY[pi-1u];else p5_patch_init(p);
    p5_track_accept(t);
}
/* Hardware: five heavy Prophet voices fit; five plus three other engines do
 * not. Charge three of sixteen units before allocation, using the existing
 * per-engine budget hook. Three Prophet + three regular voices cost fifteen. */
static uint32_t p5_units(const track_t *t) { (void)t; return 3u; }
static const engine_t ENG_P5_TEST = {
    .name="PROPHET", .page_title={"FILTER/TUNE","PW/MIX"},
    .edit={{"CUT",F_INT,-63,63,0,0,0},{"RES",F_INT,-63,63,0,0,0},
           {"TUNA",F_SEMI,-24,24,0,0,0},{"TUNB",F_SEMI,-24,24,0,0,0},
           {"PWA",F_INT,-63,63,0,0,0},{"PWB",F_INT,-63,63,0,0,0},
           {"MIXA",F_INT,-63,63,0,0,0},{"MIXB",F_INT,-63,63,0,0,0}},
    .presets=P5_TEST_PRESETS,.npresets=NELEM(P5_TEST_PRESETS),.poly=P5_POLY,.ownenv=1,
    .note_on=p5_note_on,.render=p5_render,.done=p5_done,.block=p5_block,.units=p5_units,.cap=p5_cap,.legato=p5_legato,
    .knob={P_E0,P_E1,P_CHOR,P_REV}
};
