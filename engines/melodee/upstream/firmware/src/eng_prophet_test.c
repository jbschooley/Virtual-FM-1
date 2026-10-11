/* SPDX-License-Identifier: GPL-3.0-only */
/* Prophet prototype: native patches and five (experimental dual-core: eight) voices, with two provisional
 * filter characters. Stored engine ID 19 is independent of legacy ANALOG.
 * MELODEE_PROPHET_PROTOTYPE additionally aliases ID 0 for measurement rigs. */
#include "x0x/fastmath.h"
#include "prophet_patch.c"
#include "melodee_prophet_factory.h"   /* Sequential's 200 v1.03 programs (tools/gen_prophet_factory.py) */
#if MELODEE_DUAL_CORE
#define P5_POLY 8u
#else
#define P5_POLY 5u
#endif
static uint32_t p5_poly(void)
{
#if MELODEE_DUAL_CORE
    if (audio_worker_online) return P5_POLY;
#endif
    return 5u;
}
typedef struct { float value; uint8_t stage; } p5_env_t;
typedef struct {
    uint32_t phase[2];
    float z[4], triangle, sync_tail;
    int32_t noise;
    int32_t drift, filter_drift, amp_drift, env_drift;
    float filter_prev, amp_prev;
    int32_t glide_pitch, last_pcm;
    float coeff_cut, coeff_k, coeff_g[4], coeff_inv;
    uint32_t coeff_valid;
    p5_env_t filter, amp;
} p5_voice_t;
typedef struct { p5_voice_t voice[P5_POLY]; uint32_t lfo; int32_t lfo_value, last_pitch, mod_noise; float wheel[CTL * P5_OVERSAMPLE], wheel_pitch[CTL * P5_OVERSAMPLE]; uint8_t value[55]; } p5_part_t;
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
/* Integer phase counters retain low-frequency tuning precision. Everything
 * after phase conversion is normalized fp32, including BLEP and saturation. */
static inline __attribute__((always_inline)) int32_t p5_fast_clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline __attribute__((always_inline)) float p5_fast_blep(uint32_t ph, uint32_t inc)
{
    if (!inc) return 0.0f;
    if (ph < inc) {
        float x = (float)ph / (float)inc;
        return x + x - x * x - 1.0f;
    }
    if (ph > 0xFFFFFFFFu - inc) {
        float x = -(float)(0xFFFFFFFFu - ph) / (float)inc;
        return x * x + x + x + 1.0f;
    }
    return 0.0f;
}
static inline __attribute__((always_inline)) float p5_fast_osc_saw(uint32_t ph, uint32_t inc)
{
    return (float)ph * (1.0f / 2147483648.0f) - 1.0f - p5_fast_blep(ph, inc);
}
/* A float table keeps the seven SSI saturation evaluations inexpensive.
 * Linear interpolation has <6e-6 error and no integer amplitude steps. */
static inline __attribute__((always_inline)) float p5_fast_softclip(float x)
{
    float a=fm_fabsf(x)*128.0f;
    if(a>=1024.0f)return x<0.0f?-1.0f:1.0f;
    uint32_t i=(uint32_t)a;
    float y=P5_TANH[i]+(P5_TANH[i+1u]-P5_TANH[i])*(a-(float)i);
    return x<0.0f?-y:y;
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
/* Stored knobs are 0..127; native patch bytes remain exact. */
static int32_t p5_value(const uint8_t *p,uint32_t k) { return p[k]>127u?127:p[k]; }
#define P5_ENV_END (1.0f / 1024.0f)   /* about -60 dB */
/* Linear attack, exponential decay/release; the SSM filter envelope uses
 * its measured linear decay. Float avoids an integer floor in quiet tails. */
static float p5_env_tick(p5_env_t *e, int gate, const uint8_t *p,
                        uint32_t a, uint32_t d, uint32_t s, uint32_t r, int32_t variation, const uint8_t *value, int ssm)
{
    if (!gate && e->stage != 0u) e->stage = 3;
    float target = (float)value[s] * (1.0f / 127.0f);
    float scale = 1.0f + (float)variation * (1.0f / 32768.0f);
    uint32_t rel = p[P5_RELEASE_ON] ? value[r] : 16u;
    if (e->stage == 1u) {
        e->value += P5_ENV_ATK[value[a]] * scale;
        if (e->value >= 1.0f) { e->value = 1.0f; e->stage = 2; }
    } else if (ssm && e->stage >= 2u) {
        float step = P5_ENV_SSM[e->stage == 2u ? value[d] : rel] * scale;
        if (e->stage == 3u) target = 0.0f;
        e->value += fm_clampf(target - e->value, -step, step);
    } else if (e->stage == 2u) {
        e->value += (target - e->value) * fm_minf(P5_ENV_EXP[value[d]] * scale, 1.0f);
    } else if (e->stage == 3u) {
        e->value -= e->value * fm_minf(P5_ENV_EXP[rel] * scale, 1.0f);
    }
    if (e->stage == 3u && e->value < P5_ENV_END) { e->value = 0.0f; e->stage = 0; }
    return e->value;
}

static void p5_note_on(track_t *t, voice_t *v)
{
    p5_voice_t *s = p5_voice(t, v);
    const uint8_t *p = p5_patch_of(t)->raw;
    if (!s) return;
    if (!voice_was) {
        memset(s, 0, sizeof *s);
        s->phase[0]=v->ph[0];s->phase[1]=v->ph[1];
        s->triangle = 1.0f;
        /* Thermal excitation lets resonance start with the oscillator mixer
         * shut, as native self-oscillating factory programs require. */
        if(p[P5_RESONANCE]>=110u && !p[P5_LEVEL_A] && !p[P5_LEVEL_B] && !p[P5_NOISE]){
            /* The physical filter is already oscillating behind the closed
             * VCA. Seed that state when waking an otherwise silent voice. */
            s->z[0]=0.5f;s->z[2]=-0.5f;
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
    /* The oscillators free-run, so a key finds a Lo Freq B anywhere in its
     * cycle. Starting each new voice at the bottom of the ramp instead held
     * Pickle Pincher's filter shut (Poly-Mod B 127) for every fresh note. */
    if (!voice_was && p[P5_LOW_B]) s->phase[1] = p5_fast_noise32(&s->noise);
}
static __attribute__((noinline)) int32_t p5_env_source(track_t *t,voice_t *v) {p5_voice_t *s=p5_voice(t,v);return s?(int32_t)(s->amp_prev*32768.0f):0;}
static int p5_done(track_t *t, voice_t *v)
{
    p5_voice_t *s = p5_voice(t, v);
    return !s || (!v->gate && s->amp.stage == 0u);
}
/* The native modulation sources: triangle +-0.5, saw/pulse 0..1. */
static inline __attribute__((always_inline)) float p5_lfo_wave(uint32_t phase,const uint8_t *p)
{
    float pos=(float)phase*(1.0f/4294967296.0f), y=0.0f;uint32_t n=0;
    if(p[P5_LFO_SAW]){y+=pos;n++;}
    if(p[P5_LFO_TRI]){y+=phase<0x80000000u?pos*2.0f-0.5f:1.5f-pos*2.0f;n++;}
    if(p[P5_LFO_PULSE]){y+=phase<0x80000000u?1.0f:0.0f;n++;}
    return n ? y/(float)n : 0.0f;
}
/* Float interpolation of one octave, with an exact power-of-two scale.
 * Audio-rate Poly-Mod and the shared wheel avoid repeating a polynomial.
 * Callers constrain x to [-8,6]; interpolation error is below 1 ppm. */
static inline __attribute__((always_inline)) float p5_pitch_ratio(float x)
{
    int32_t octave=(int32_t)x;if((float)octave>x)octave--;
    float pos=(x-(float)octave)*512.0f;uint32_t i=(uint32_t)pos;
    float ratio=P5_EXP2[i]+(P5_EXP2[i+1u]-P5_EXP2[i])*(pos-(float)i);
    fm_bits_t scale;scale.u=(uint32_t)(octave+127)<<23;
    return ratio*scale.f;
}
/* Rev-4 full-amount triangle swings +-235 cents. Shared by every voice. */
static __attribute__((section(".dsp_text"))) void p5_mod_samples(p5_part_t *s,const uint8_t *p,uint32_t inc,float amount,float mix)
{
    uint32_t phase=s->lfo;if(!s->mod_noise)s->mod_noise=0x517953;
    for(uint32_t i=0;i<CTL*P5_OVERSAMPLE;i++){
        float noise=(float)(int32_t)(p5_fast_noise32(&s->mod_noise)>>8)*(1.0f/16777216.0f)-0.5f;
        float w=(p5_lfo_wave(phase,p)*(1.0f-mix)+noise*mix)*amount;
        s->wheel[i]=w;
        s->wheel_pitch[i]=p5_pitch_ratio(w*(470.0f/1200.0f));
        phase+=inc;
    }
    s->lfo=phase;s->lfo_value=(int32_t)(p5_lfo_wave(phase,p)*32768.0f);
}
static void p5_update_bend(track_t *t);
static void p5_block(track_t *t)
{
    p5_part_t *s=p5_part((uint32_t)(t-trk));const uint8_t *p=p5_patch_of(t)->raw;
    if(!s)return;p5_update_bend(t);
    /* Raw-to-DSP clamping is part-wide; envelopes and all voices share it. */
    for(uint32_t k=0;k<55u;k++)s->value[k]=(uint8_t)p5_value(p,k);
    uint32_t inc=P5_LFO_INC[p5_value(p,P5_LFO_RATE)]/P5_OVERSAMPLE;
    float amount=P5_AMOUNT[clamp(p5_value(p,P5_LFO_INITIAL)+t->mw+(p[P5_PRESS_LFO]?t->at:0),0,127)];
    void (*volatile run)(p5_part_t *,const uint8_t *,uint32_t,float,float)=p5_mod_samples;
    run(s,p,inc,amount,(float)p5_value(p,P5_WHEEL_MIX)*(1.0f/127.0f));
}
static uint32_t p5_cap(const track_t *t)
{
    const uint8_t *p=p5_patch_of(t)->raw;
    return p[P5_UNISON] && t->p[P_VOICE]==V_UNISON ?
        (uint32_t)clamp(p[P5_UNISON_COUNT]?p[P5_UNISON_COUNT]:5,1,(int32_t)p5_poly()) : p5_poly();
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

/* Four trapezoidal one-poles with an algebraic feedback solution. Curtis
 * saturates the input; SSI saturates each stage. These remain provisional
 * character models. Coefficients, feedback and stage memory are all fp32. */
static inline __attribute__((always_inline)) float p5_filter_core(p5_voice_t *s, float input, float cutoff, float k, float gain, int curtis)
{
    cutoff=fm_clampf(cutoff,0.0f,127.0f);
    float g,g2,g3,g4,reciprocal;
    if(s->coeff_valid && cutoff==s->coeff_cut && k==s->coeff_k){
        g=s->coeff_g[0];g2=s->coeff_g[1];g3=s->coeff_g[2];g4=s->coeff_g[3];reciprocal=s->coeff_inv;
    }else{
        uint32_t i=(uint32_t)cutoff;g=P5_TPT_G[i];
        if(i<127u)g+=(P5_TPT_G[i+1u]-g)*(cutoff-(float)i);
        g2=g*g;g3=g2*g;g4=g3*g;
        reciprocal=1.0f/(1.0f+k*g4);
        s->coeff_g[0]=g;s->coeff_g[1]=g2;s->coeff_g[2]=g3;s->coeff_g[3]=g4;
        s->coeff_cut=cutoff;s->coeff_k=k;s->coeff_inv=reciprocal;s->coeff_valid=1;
    }
    float sigma=(1.0f-g)*(g3*s->z[0]+g2*s->z[1]+g*s->z[2]+s->z[3]);
    float u=p5_fast_softclip((input*gain-k*sigma)*reciprocal);
#define P5_STAGE(j, clip) do { \
    float delta=g*(u-s->z[j]); \
    float y=s->z[j]+delta; \
    s->z[j]=fm_clampf(y+delta,-1.83105469f,1.83105469f); \
    if(fm_fabsf(s->z[j])<1e-20f)s->z[j]=0.0f; \
    u=clip(y); \
} while(0)
#define P5_LINEAR(y) (y)
    if(curtis){P5_STAGE(0,P5_LINEAR);P5_STAGE(1,P5_LINEAR);P5_STAGE(2,P5_LINEAR);P5_STAGE(3,P5_LINEAR);}
    else {P5_STAGE(0,p5_fast_softclip);P5_STAGE(1,p5_fast_softclip);P5_STAGE(2,p5_fast_softclip);P5_STAGE(3,p5_fast_softclip);}
#undef P5_LINEAR
#undef P5_STAGE
    return p5_fast_softclip(u);
}
static float p5_filter(p5_voice_t *s,float input,float cutoff,float resonance,int curtis)
{
    return p5_filter_core(s,input,cutoff,resonance*(curtis?19200.0f:18800.0f)/(127.0f*4096.0f),
                         1.0f-resonance*(curtis?100.0f:45.0f)/32768.0f,curtis);
}
static inline __attribute__((always_inline)) float p5_wave(uint32_t phase, uint32_t inc, uint32_t pw, int saw, int pulse, int tri, float *triangle)
{
    float y=0.0f,a=p5_fast_osc_saw(phase,inc),square=0.0f;
    if(saw)y+=a*0.5f;
    if(pulse){square=(a-p5_fast_osc_saw(phase+pw,inc))*0.5f;y+=square;}
    if(tri){
        if(!pulse || pw!=0x80000000u)square=(a-p5_fast_osc_saw(phase+0x80000000u,inc))*0.5f;
        *triangle=fm_clampf(*triangle*(32767.0f/32768.0f)+square*(float)inc*(8.0f/4294967296.0f),-1.220703125f,1.220703125f);
        y+=*triangle*0.5f;
    }
    return y;
}
static inline __attribute__((always_inline)) float p5_sync_fraction(uint32_t distance,uint32_t inc)
{
    return inc ? fm_minf((float)distance/(float)inc,1.0f) : 0.0f;
}
static inline __attribute__((always_inline)) uint32_t p5_pw(float value)
{
    return (uint32_t)(fm_clampf(value,0.03125f,31743.0f/32768.0f)*4294967296.0f);
}
/* Exponential Poly-Mod, x in octaves; cap below Nyquist before conversion. */
static inline __attribute__((always_inline)) uint32_t p5_inc(uint32_t base,float x)
{
    if(!x)return base;
    float y=(float)base*p5_pitch_ratio(fm_clampf(x,-8.0f,6.0f));
    return (uint32_t)fm_clampf(y,1.0f,1932735232.0f);
}
static inline __attribute__((always_inline)) uint32_t p5_wheel_inc(uint32_t base,float ratio)
{
    if(ratio==1.0f)return base;
    return (uint32_t)fm_minf((float)base*ratio,1932735232.0f);
}
typedef struct {
    float f0,a0,f1,a1,cut,k,filter_gain,filter_velocity,amp_velocity,env_amount;
    float pa_env,pa_b,pf_env,pf_b,pw_env,pw_b,pwm_a,pwm_b,la,lb,ln;
    uint32_t ia,ib;
    const float *wheel,*wheel_pitch;
} p5_render_params_t;
static __attribute__((section(".dsp_text"))) void p5_samples(p5_voice_t *restrict s,const uint8_t *restrict p,int32_t *restrict out,uint32_t n,const vmod_t *m,const p5_render_params_t *c)
{
    int32_t last=0;
    for(uint32_t i=0;i<n;i++){
        float time=(float)i*(1.0f/CTL);
        float fe=c->f0+(c->f1-c->f0)*time,ae=c->a0+(c->a1-c->a0)*time,sum=0.0f;
        for(uint32_t os=0;os<P5_OVERSAMPLE;os++){
            float noise=(float)(p5_fast_noise32(&s->noise)>>8)*(1.0f/16777216.0f)-0.5f;
            float lfo=c->wheel[i*P5_OVERSAMPLE+os],wheel_pitch=c->wheel_pitch[i*P5_OVERSAMPLE+os];
            uint32_t db=p[P5_WHEEL_FREQ_B]?p5_wheel_inc(c->ib,wheel_pitch):c->ib;
            float b=p5_wave(s->phase[1],db,p5_pw(c->pwm_b+(p[P5_WHEEL_PW_B]?lfo:0.0f)),p[P5_SAW_B],p[P5_PULSE_B],p[P5_TRI_B],&s->triangle);
            float bn=b*2.0f;
            uint32_t da=p[P5_POLY_FREQ]?p5_inc(c->ia,fe*c->pa_env+bn*c->pa_b):c->ia;
            if(p[P5_WHEEL_FREQ_A])da=p5_wheel_inc(da,wheel_pitch);
            uint32_t pw=p5_pw(c->pwm_a+(p[P5_POLY_PW]?fe*c->pw_env+b*c->pw_b:0.0f)+(p[P5_WHEEL_PW_A]?lfo:0.0f));
            float unused=0.0f;
            float a=p5_wave(s->phase[0],da,pw,p[P5_SAW_A],p[P5_PULSE_A],0,&unused)+s->sync_tail;
            s->sync_tail=0.0f;
            uint32_t nb=s->phase[1]+db,na=s->phase[0]+da;
            if(p[P5_SYNC] && nb<s->phase[1] && db){
                float frac=p5_sync_fraction(0u-s->phase[1],db),remain=1.0f-frac;
                uint32_t at=s->phase[0]+(uint32_t)((float)da*frac);
                float before=p5_wave(at,da,pw,p[P5_SAW_A],p[P5_PULSE_A],0,&unused);
                float after=p5_wave(0,da,pw,p[P5_SAW_A],p[P5_PULSE_A],0,&unused);
                float jump=(after-before)*0.5f;
                a+=jump*remain*remain;s->sync_tail=-jump*frac*frac;
                na=(uint32_t)((float)da*remain);
            }
            s->phase[0]=na;s->phase[1]=nb;
            float x=a*c->la+b*c->lb+noise*c->ln;
            float fc=c->cut+fe*c->filter_velocity*c->env_amount;
            if(p[P5_POLY_FILTER])fc+=fe*c->pf_env+bn*c->pf_b;
            if(p[P5_WHEEL_FILTER])fc+=lfo*112.0f;
            sum+=p5_filter_core(s,p5_fast_softclip(x),fc,c->k,c->filter_gain,!!p[P5_FILTER_REV]);
        }
        sum*=1.0f/P5_OVERSAMPLE;
        float gain=ae*c->amp_velocity*(1.0f+(float)s->amp_drift/32768.0f);
        float amp=((float)m->amp0+(float)(m->amp1-m->amp0)*time)*(1.0f/32768.0f);
        last=(int32_t)(sum*gain*amp*(2.0f*VOICE_FS));out[i]+=last;
    }
    s->last_pcm=last;
}

#if MELODEE_DUAL_CORE
/* The sample kernel owns only its voice and private PCM. Part-wide wheel
 * buffers and patch bytes remain read-only until post joins the worker. */
static struct {
    p5_voice_t *voice;
    const uint8_t *patch;
    p5_render_params_t params;
    vmod_t modulation;
    uint32_t n;
    int32_t pcm[CTL];
} p5_job;
static int p5_pending;
static uint32_t p5_pairs;
static int32_t *p5_pending_out;
static void p5_worker_kernel(void *unused)
{
    (void)unused;
    for(uint32_t i=0;i<p5_job.n;i++)p5_job.pcm[i]=0;
    void (*volatile run)(p5_voice_t *,const uint8_t *,int32_t *,uint32_t,const vmod_t *,const p5_render_params_t *)=p5_samples;
    run(p5_job.voice,p5_job.patch,p5_job.pcm,p5_job.n,&p5_job.modulation,&p5_job.params);
}
static void p5_join(void)
{
    if(!p5_pending)return;
    p5_pending=0;
    if(!audio_worker_join())return;   /* CPU1 lost: this voice's block is dropped */
    for(uint32_t i=0;i<p5_job.n;i++)p5_pending_out[i]+=p5_job.pcm[i];
}
static void p5_post(track_t *t,int32_t *out,uint32_t n,uint32_t nr)
{
    (void)t;(void)out;(void)n;(void)nr;
    p5_join();
}
#endif

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
#if MELODEE_DUAL_CORE
        p5_join();
#endif
        if(n>1u){
            int32_t step=s->last_pcm/(int32_t)(n-1u);
            for(uint32_t i=0;i<n-1u;i++)out[i]+=s->last_pcm-step*(int32_t)i;
        }
        s->last_pcm=0;return;
    }
    const uint8_t *q=part->value;
    float f0=s->filter_prev, a0=s->amp_prev;
    int curtis=!!p[P5_FILTER_REV];
    float f1=p5_env_tick(&s->filter,v->gate,p,P5_ATTACK_FILTER,P5_DECAY_FILTER,P5_SUSTAIN_FILTER,P5_RELEASE_FILTER,s->env_drift,q,!curtis);
    float a1=p5_env_tick(&s->amp,v->gate,p,P5_ATTACK_AMP,P5_DECAY_AMP,P5_SUSTAIN_AMP,P5_RELEASE_AMP,s->env_drift,q,0);
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
    /* Lo Freq drops B seven octaves: Pickle Pincher's keyboard-off B (raw 48,
     * 173 Hz) moves its filter at the demo's 1.35 Hz (ten octaves: 0.17 Hz). */
    if (p[P5_LOW_B]) ib >>= 7;
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
    const p5_render_params_t c={f0,a0,f1,a1,
        (float)cut/256.0f,(float)k/4096.0f,(float)filter_gain/32768.0f,
        (float)filter_velocity/32768.0f,(float)amp_velocity/32768.0f,(float)env_amount/256.0f,
        (float)pa_env/4096.0f,(float)pa_b/4096.0f,(float)pf_env/256.0f,(float)pf_b/256.0f,
        (float)pw_env/32768.0f,(float)pw_b/32768.0f,(float)pwm_a/32768.0f,(float)pwm_b/32768.0f,
        (float)la/32768.0f,(float)lb/32768.0f,(float)ln/32768.0f,
        ia,ib,part->wheel,part->wheel_pitch};
    /* An indirect call crosses the XIP/RAM distance beyond direct branch range. */
    void (*volatile run)(p5_voice_t *,const uint8_t *,int32_t *,uint32_t,const vmod_t *,const p5_render_params_t *)=p5_samples;
#if MELODEE_DUAL_CORE
    uint32_t next=(uint32_t)(v-t->v)+1u;
    while(next<NVOICE && !t->v[next].active)next++;
    if(!p5_pending && audio_worker_online && n<=CTL && next<NVOICE){
        p5_job.voice=s;p5_job.patch=p;p5_job.params=c;p5_job.modulation=*m;p5_job.n=n;
        if(audio_worker_submit(p5_worker_kernel,0)){
            p5_pending=1;p5_pending_out=out;return;
        }
    }
    if(p5_pending){
        /* Preserve serial voice addition order, including any earlier voices
         * already in out. The producer's scratch never aliases worker PCM. */
        int32_t pcm[CTL]={0};
        run(s,p,pcm,n,m,&c);
        p5_pairs++;
        p5_join();
        for(uint32_t i=0;i<n;i++)out[i]+=pcm[i];
        return;
    }
#endif
    run(s,p,out,n,m,&c);
}

/* INIT, then Sequential's factory programs in their order (111..588 on the
 * Prophet: preset k is program k-1 of the v1.03 bank). Dry, as the Prophet. */
#define P5_FACTORY_PRESET(n) {n,{0,0,0,0,0,0,0,0},{0,0,127,0},0,0,FX(0,0,0,0)},
static const preset_t P5_TEST_PRESETS[] = {
    {"INIT PROPHET",{0,0,0,0,0,0,0,0},{0,0,127,0},0,0,FX(0,0,0,0)},
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
/* Serial hardware measurements justify three units per voice. The opt-in
 * paired path provisionally charges two: eight voices fill the same sixteen
 * units, so adding another track still steals voices. Validate on hardware. */
static uint32_t p5_units(const track_t *t) { (void)t; return p5_poly()>5u?2u:3u; }
static const engine_t ENG_P5_TEST = {
    .name="PROPHET", .page_title={"FILTER/TUNE","PW/MIX"},
    .edit={{"CUT",F_INT,-63,63,0,0,0},{"RES",F_INT,-63,63,0,0,0},
           {"TUNA",F_SEMI,-24,24,0,0,0},{"TUNB",F_SEMI,-24,24,0,0,0},
           {"PWA",F_INT,-63,63,0,0,0},{"PWB",F_INT,-63,63,0,0,0},
           {"MIXA",F_INT,-63,63,0,0,0},{"MIXB",F_INT,-63,63,0,0,0}},
    .presets=P5_TEST_PRESETS,.npresets=NELEM(P5_TEST_PRESETS),.poly=P5_POLY,.ownenv=1,
    .note_on=p5_note_on,.render=p5_render,.done=p5_done,.block=p5_block,.units=p5_units,.cap=p5_cap,.legato=p5_legato,
#if MELODEE_DUAL_CORE
    .post=p5_post,
#endif
    .knob={P_E0,P_E1,P_CHOR,P_REV}
};
