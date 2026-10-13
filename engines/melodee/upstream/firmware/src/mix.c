/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Engine output -> optional stereo insert -> track level -> master.
 * The bypass retains the original level/pan/spread path exactly. */
#include "fx.c"
static int32_t part_buf[CTL], mix_l[CTL], mix_r[CTL];
static uint8_t fx_send_on;
static uint32_t fx_extra_gain[FX_SLOTS-1u];
static __attribute__((noinline,section(".audio_text"))) void mix_extra_sends(uint32_t n)
{
    for(uint32_t i=0;i<n;i++){
        int32_t l=mix_l[i]-fx_bus_sum_l[i],r=mix_r[i]-fx_bus_sum_r[i];
        for(uint32_t slot=0;slot<FX_SLOTS-1u;slot++)if(fx_extra_gain[slot]){
            fx_send_extra_l[slot][i]+=fx_scale(l,fx_extra_gain[slot]);fx_send_extra_r[slot][i]+=fx_scale(r,fx_extra_gain[slot]);
        }
    }
}
static inline __attribute__((always_inline)) void mix_add(int32_t l,int32_t r,uint32_t i,uint32_t send)
{
    mix_l[i]+=l;mix_r[i]+=r;if(send){fx_send_l[i]+=fx_scale(l,send);fx_send_r[i]+=fx_scale(r,send);}
}
static volatile uint32_t kb_mask, kb_layer;
static volatile uint8_t kb_lock;
#define LIM_T 18000
static int32_t lim_env = LIM_T;
static int32_t dc_l, dc_r, dce_l, dce_r;
static inline __attribute__((always_inline)) int32_t output_clip(int32_t x)
{
    int32_t a = x < 0 ? -x : x, i = a >> 8, y;
    if (i >= 256) y = TANH_Q15[256];
    else y = TANH_Q15[i] + (((TANH_Q15[i + 1] - TANH_Q15[i]) * (a & 255)) >> 8);
    return x < 0 ? -y : y;
}

/* DC blocker (~2 Hz), always on: a leaky integrator of the input (Q6 state) subtracted from it.
 * The >> 12 step keeps its remainder (error feedback, 0..4095) and adds it to the next one, so no
 * part of the step is lost: the state follows the input exactly, down to 0 after the sound stops.
 * (A rounded step of (x - dc) / 4096 would stop moving at |x - dc| < 2048 and leave an offset of up
 * to +-31 at the output after silence.) */
static inline __attribute__((always_inline)) int32_t dc_block(int32_t x, int32_t *dc, int32_t *err)
{
    int32_t e = x * 64 - *dc + *err, d = e >> 12;
    *err = e - d * 4096;
    *dc += d;
    return x - ((*dc + 32) >> 6);
}

static __attribute__((noinline,section(".dsp_text"))) void master_out(int32_t *l, int32_t *r)
{
    int32_t al, ar, a;
    *l = dc_block(*l, &dc_l, &dce_l);
    *r = dc_block(*r, &dc_r, &dce_r);
    
    al = *l < 0 ? -*l : *l;
    ar = *r < 0 ? -*r : *r;
    a = al > ar ? al : ar;
    if (a > lim_env)
        lim_env += (a - lim_env) >> 2;
    else if (lim_env > LIM_T)
        lim_env -= ((lim_env - LIM_T) >> 12) + 1;
    if (lim_env > LIM_T) {
        int32_t g = (int32_t)(((uint32_t)LIM_T << 15) / (uint32_t)lim_env);   /* < 32768 */
        *l = ((*l >> 4) * g) >> 11;                      /* >> 4 first: |l| may be far above Q15 */
        *r = ((*r >> 4) * g) >> 11;
    }
    *l = output_clip(*l);
    *r = output_clip(*r);
}

/* length of one division (N_DIV order) in samples at the song tempo */
static const uint8_t DIV_DEN[6] = {1, 2, 4, 8, 3, 6};    /* original IDs stay fixed; slow rates append */
static uint8_t midi_sync_active;                 /* armed by an incoming Start/Continue/Stop, never a clock pulse */
static uint32_t midi_beat_samples;                    /* zero until an external clock has a measured tempo */
static uint32_t beat_samples(void)
{
    return midi_sync_active && midi_beat_samples ? midi_beat_samples : (uint32_t)FS * 60u / (uint32_t)song.g[G_BPM];
}
static uint32_t div_samples(uint32_t div)
{
    uint32_t quarter = beat_samples();
    return div < 6u ? quarter / DIV_DEN[div] : div < 10u ? quarter << (div - 5u) : quarter / DIV_DEN[div % 6u];
}

/* External sequencing uses musical units, independent of the measured tempo.
 * 24 clocks/beat and every division divide exactly; tempo estimates only set
 * LFO timing and the display, never move an already recorded step. */
#define MIDI_BEAT_UNITS 24576u
static uint32_t seq_beat_samples(void)
{
    return midi_sync_active ? MIDI_BEAT_UNITS : beat_samples();
}
static uint32_t seq_div_samples(uint32_t div)
{
    uint32_t quarter = seq_beat_samples();
    return div < 6u ? quarter / DIV_DEN[div] : div < 10u ? quarter << (div - 5u) : quarter / DIV_DEN[div % 6u];
}


static void events_block(uint32_t n);
#if MELODEE_USB_AUDIO
static int ua_capture_active(void);
static uint8_t track_capture_on;
static int32_t track_capture[CTL * NTRK];
#endif

typedef struct {
    int16_t level, pan;
    uint8_t valid;
    int32_t lvl, gl, gr;
} mix_cache_t;
static mix_cache_t mix_cache[NTRK];
static __attribute__((noinline)) void mix_cache_update(const track_t *t, mix_cache_t *mc)
{
    mc->level=t->p[P_LEVEL];mc->pan=t->p[P_PAN];mc->valid=1;
    mc->lvl=LEVEL_Q12[mc->level & 127];
    mc->gl=4096-(mc->pan>0?mc->pan*64:0);mc->gr=4096+(mc->pan<0?mc->pan*64:0);
}
static __attribute__((noinline)) void mix_spread(track_t *t, int32_t *b, uint32_t n)
{
    uint32_t send=fx_send_on?fx_send_gain(t):0u;
    int32_t *d=spread_buf;
    int32_t lvl=LEVEL_Q12[t->p[P_LEVEL] & 127],pan=t->p[P_PAN],s=(t->p[P_SPRD]+1)>>1;
    int32_t pa=clamp(pan-s,-64,63),pb=clamp(pan+s,-64,63);
    int32_t gla=4096-(pa>0?pa*64:0),gra=4096+(pa<0?pa*64:0);
    int32_t glb=4096-(pb>0?pb*64:0),grb=4096+(pb<0?pb*64:0);
    int32_t glm=(gla+glb)>>1,grm=(gra+grb)>>1,gld=(gla-glb)>>1,grd=(gra-grb)>>1,pk=t->peak;
    for(uint32_t i=0;i<n;i++){
        int32_t x=clamp(((clamp(b[i],-884000,884000)>>2)*lvl)>>10,-524287,524287);
        int32_t y=clamp(((clamp((d[i]<<1)-b[i],-884000,884000)>>2)*lvl)>>10,-524287,524287);
        int32_t a=x<0?-x:x;
#if MELODEE_USB_AUDIO
        if(track_capture_on)track_capture[i*NTRK+(uint32_t)(t-trk)]=x;
#endif
        if(a>pk)pk=a;
        mix_add(((x*glm)>>12)+((y*gld)>>12),((x*grm)>>12)+((y*grd)>>12),i,send);
    }
    t->peak=pk;
}
static inline __attribute__((always_inline)) int32_t mix_clamp(int32_t x,int32_t lo,int32_t hi)
{ return x<lo?lo:x>hi?hi:x; }
static __attribute__((noinline,section(".dsp_text"))) void mix_mono_samples(track_t *t,int32_t *b,uint32_t n,const mix_cache_t *mc)
{
    uint32_t ti=(uint32_t)(t-trk);
    uint32_t send=fx_send_on?fx_send_gain(t):0u;
    int32_t lvl=mc->lvl,gl=mc->gl,gr=mc->gr,pk=t->peak;
    for(uint32_t i=0;i<n;i++){
        int32_t x=mix_clamp(((mix_clamp(b[i],-884000,884000)>>2)*lvl)>>10,-524287,524287),a=x<0?-x:x;
#if MELODEE_USB_AUDIO
        if(track_capture_on)track_capture[i*NTRK+ti]=x;
#endif
        if(a>pk)pk=a;
        mix_add((x*gl)>>12,(x*gr)>>12,i,send);
    }
    t->peak=pk;
}
static int32_t solo_gain[NPART]={32768,32768,32768,32768};
static void mix_solo(track_t *t,int32_t *b,uint32_t n)
{
    uint32_t ti=(uint32_t)(t-trk);
    int32_t from=solo_gain[ti],to=!track_solo || ((track_solo>>ti)&1u)?32768:0;
    if(from==32768 && to==32768)return;
    for(uint32_t i=0;i<n;i++){
        from += clamp(to-from,-256,256); /* 128 samples, same GLO solo ramp as before */
        b[i]=mulq16(b[i],(uint32_t)from<<1);
        if(spread_on)spread_buf[i]=mulq16(spread_buf[i],(uint32_t)from<<1);
    }
    solo_gain[ti]=from;
}
static __attribute__((noinline,section(".dsp_text"))) void mix_fx_samples(track_t *t,uint32_t n)
{
    uint32_t ti=(uint32_t)(t-trk);int32_t lvl=LEVEL_Q12[t->p[P_LEVEL]&127],pk=t->peak;
    uint32_t send=fx_send_on?fx_send_gain(t):0u;
    int32_t gain=solo_gain[ti],to=!t->p[P_MUTE] && (!track_solo || ((track_solo>>ti)&1u))?32768:0;
    for(uint32_t i=0;i<n;i++){
        gain+=mix_clamp(to-gain,-256,256);
        int32_t l=fx_scale(fx_left[i],(uint32_t)gain*2u),r=fx_scale(fx_right[i],(uint32_t)gain*2u);
        l=mix_clamp(fx_div_pow2((l>>2)*lvl,10),-524287,524287);r=mix_clamp(fx_div_pow2((r>>2)*lvl,10),-524287,524287);
        mix_add(l,r,i,send);int32_t a=l<0?-l:l,c=r<0?-r:r;if(c>a)a=c;if(a>pk)pk=a;
#if MELODEE_USB_AUDIO
        if(track_capture_on)track_capture[i*NTRK+ti]=fx_div_pow2(l+r,1);
#endif
    }
    solo_gain[ti]=gain;t->peak=pk;
}
static void mix_part(track_t *t,uint32_t n)
{
    uint32_t extra=0;
    if(fx_send_on && fx_bus_separate)for(uint32_t slot=0;slot<FX_SLOTS-1u;slot++){
        uint32_t amount=(uint32_t)t->p[P_FXSEND2+slot];fx_extra_gain[slot]=(amount+(amount==127u))*512u;extra|=amount;
    }
    /* Reuse the return workspace until the worker starts. The difference is
     * this track's exact post-fader/pan/solo contribution; CHAIN's sample loops
     * stay unchanged and zero extra sends need no extra mixing pass. */
    if(extra){memcpy(fx_bus_sum_l,mix_l,n*sizeof *mix_l);memcpy(fx_bus_sum_r,mix_r,n*sizeof *mix_r);}
    mod_begin(t);
    spread_on=t->p[P_SPRD]>0 && (trk_vmode(t)==V_POLY || trk_vmode(t)==V_UNISON);
    int rendered=track_render_audio(t,part_buf,n,0);
#if defined(MELODEE_FX_PROFILE) && MELODEE_FX_PROFILE && defined(FM1_IRQ_TARGET)
    if(fx_profile_control>>30u && fx_profile_track((uint32_t)(t-trk))){for(uint32_t i=0;i<n;i++)part_buf[i]=((i&8u)?-8000:8000)+(int32_t)i*97;rendered=1;spread_on=0;}
#endif
    if(rendered || fx_active(t)){
        if(!rendered){memset(part_buf,0,n*sizeof *part_buf);if(spread_on)memset(spread_buf,0,n*sizeof *spread_buf);}
        if(fx_process(t,part_buf,n)){
            void (*volatile run)(track_t *,uint32_t)=mix_fx_samples;run(t,n);
        }else{
            mix_solo(t,part_buf,n);
            if(spread_on)mix_spread(t,part_buf,n);
            else{
            mix_cache_t *mc=&mix_cache[(uint32_t)(t-trk)];
            if(!mc->valid || mc->level!=t->p[P_LEVEL] || mc->pan!=t->p[P_PAN])mix_cache_update(t,mc);
            void (*volatile mix)(track_t *,int32_t *,uint32_t,const mix_cache_t *)=mix_mono_samples;
            mix(t,part_buf,n,mc);
            }
        }
    }
    if(mod.on)mod_end(t);
    if(extra){void (*volatile run)(uint32_t)=mix_extra_sends;run(n);}
}
static __attribute__((noinline,section(".dsp_text"))) void master_samples(int32_t *out,uint32_t n)
{
    int32_t gain=fx_bus_mix,target=fx_bus_target;
    for(uint32_t i=0;i<n;i++){
        if(gain!=target)gain+=mix_clamp(target-gain,-128,128);
        int32_t l=mix_l[i],r=mix_r[i];
        if(fx_bus_result){uint32_t ret=(uint32_t)gain*2u;
            l+=((fx_scale(fx_left[i],ret)>>2)*(int32_t)song.master_q12)>>10;
            r+=((fx_scale(fx_right[i],ret)>>2)*(int32_t)song.master_q12)>>10;}
        master_out(&l,&r);out[2u*i]=l;out[2u*i+1u]=r;
    }
    fx_bus_mix=gain;
}
static void mix_block(int32_t *out,uint32_t n)
{
#if defined(MELODEE_FX_PROFILE) && MELODEE_FX_PROFILE && defined(FM1_IRQ_TARGET)
    fx_profile_poll();
#endif
#if MELODEE_USB_AUDIO
    track_capture_on=(uint8_t)ua_capture_active();
    if(track_capture_on)memset(track_capture,0,n*NTRK*sizeof *track_capture);
#endif
    memset(mix_l,0,n*sizeof *mix_l);memset(mix_r,0,n*sizeof *mix_r);
    events_block(n);
    fx_bus_select();fx_send_on=(uint8_t)fx_bus_active();if(fx_send_on){memset(fx_send_l,0,n*sizeof *fx_send_l);memset(fx_send_r,0,n*sizeof *fx_send_r);
        if(fx_bus_separate){memset(fx_send_extra_l,0,sizeof fx_send_extra_l);memset(fx_send_extra_r,0,sizeof fx_send_extra_r);}}
    for(uint32_t i=0;i<NPART;i++)mix_part(&trk[i],n);
    fx_bus_start(n);
    /* Dry gain overlaps the shared wet processing; limiter sees their sum. */
    for(uint32_t i=0;i<n;i++){mix_l[i]=((mix_l[i]>>2)*(int32_t)song.master_q12)>>10;mix_r[i]=((mix_r[i]>>2)*(int32_t)song.master_q12)>>10;}
    fx_bus_join();
    void (*volatile run)(int32_t *,uint32_t)=master_samples;
    run(out,n);
}
