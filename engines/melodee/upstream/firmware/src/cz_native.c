/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) Devin Acker
 * Modifications Copyright (c) 2026 Kerem Kilic (Ellic Studio)
 * License: LICENSES/BSD-3-Clause-uPD933.txt
 * Native CZ-1 tone playback. Parameter encoding: Casio MIDI specification.
 * Chip envelope rate, phase functions and logarithmic DCA law follow the
 * independently documented uPD933 model by Devin Acker (MAME, BSD-3-Clause).
 * See docs/CZ1_SYSEX.md for sources and remaining calibration limits. */
#include "x0x/fastmath.h"

static const uint16_t CZ_AMP[128] = {
    0,4,5,5,5,6,6,7,7,8,8,9,9,10,11,12,
    12,13,14,15,16,18,19,20,22,23,25,27,29,31,33,36,
    38,41,44,47,51,54,58,63,67,72,77,83,89,96,103,110,
    118,127,136,146,157,168,180,194,208,223,239,257,275,296,317,340,
    365,392,421,451,484,520,558,598,642,689,739,793,851,914,980,1052,
    1129,1212,1300,1395,1497,1606,1724,1850,1985,2130,2286,2453,2632,2824,3031,3252,
    3490,3745,4019,4313,4628,4966,5329,5718,6136,6585,7066,7582,8136,8731,9369,10054,
    10789,11577,12423,13331,14305,15351,16473,17676,18968,20355,21842,23438,25151,26990,28962,31079
};
typedef struct { float rate[8], level[8]; uint8_t sustain, end; } cz_native_def_t;
/* Native control domains: DCO semitones, DCW 11-bit phase units, DCA 0..127.
 * Preserve the uPD933's rate and target laws, using fractional control time. */
static float cz_hw_rate(uint32_t raw,uint32_t e)
{
    uint32_t chip=(8u|(raw&7u))<<((raw&127u)>>3);
    float scale=e==0u ? 1.0f/(8.0f*131072.0f) : e==1u ? 1.0f/(4.0f*16384.0f) : 1.0f/(2.0f*131072.0f);
    return (float)chip*((float)CTL*40000.0f/FS)*scale;
}
static float cz_hw_target(uint32_t raw,uint32_t e)
{
    raw&=127u;
    if(e==0u)return (float)(raw&63u)*(raw&64u?2.0f:0.0625f);
    return (float)raw*(e==1u?8.0f:1.0f);
}
static __attribute__((noinline)) float cz_native_env_tick(cz_native_env_t *e,const cz_native_def_t *d,uint32_t gate)
{
    float left=1.0f;
    if(e->gate && !gate && d->sustain<=d->end && e->stage<=d->sustain)e->stage=d->sustain+1u;
    e->gate=(uint8_t)gate;
    for(uint32_t k=0;k<8u && e->stage<=d->end;k++){
        uint32_t st=e->stage;float rate=d->rate[st],target=st==d->end?0.0f:d->level[st];
        float delta=target-e->level,dist=fm_fabsf(delta),step=rate*left;
        if(dist>step){e->level+=delta<0.0f?-step:step;break;}
        e->level=target;if(gate && st==d->sustain)break;e->stage++;
        if(dist>0.0f && rate>0.0f)left=fm_maxf(0.0f,left-dist/rate);
        if(left<=0.0f)break;
    }
    return e->level;
}
static void cz_native_note_on(track_t *t,voice_t *v)
{
    cz_voice_t *c=cz_voice(t,v);
    float dc[2]={c->native_dc[0],c->native_dc[1]};
    phase_note_on(t,v); /* common phase/noise reset; zero has the same float representation */
    if(voice_was){c->native_dc[0]=dc[0];c->native_dc[1]=dc[1];}
    for(uint32_t l=0;l<2u;l++)for(uint32_t e=0;e<3u;e++)c->native_eg[l][e].gate=1;
}
static __attribute__((noinline)) void cz_native_defs(track_t *t, voice_t *v, cz_native_def_t d[2][3])
{
    const uint8_t *b = cz_patch[(uint32_t)(t - trk) % NTRK].raw;
    for (uint32_t l = 0; l < 2u; l++) for (uint32_t e = 0; e < 3u; e++) {
        cz_native_def_t *p = &d[l][e]; uint32_t base = CZ_ENV_BASE[l][e];
        memset(p, 0, sizeof *p); p->end = b[CZ_ENV_END[l][e]] & 7u; p->sustain = 255;
        for (uint32_t k = 0; k < 8u; k++) {
            uint32_t lev = b[base + 2u*k + 1u];
            p->rate[k] = cz_hw_rate(b[base + 2u*k], e);
            p->level[k] = cz_hw_target(lev, e);
            if ((lev & 128u) && p->sustain == 255u && k < p->end) p->sustain = (uint8_t)k;
            /* Native DCA key-follow is rate scaling; higher keys run faster.
             * Keep raw key-follow and velocity bytes intact for recalibration. */
            if (e == 2u) {
                uint32_t kf = b[16u + l*57u] & 15u;
                uint32_t note = v->note > 36u ? v->note - 36u : 0u;
                p->rate[k] *= 1.0f+(float)(kf*note)*(1.0f/96.0f);
            }
        }
    }
}
static __attribute__((noinline)) float cz_native_amp(float level,uint32_t atten,uint32_t sense,uint32_t vel)
{
    float x=fm_clampf(level,0.0f,127.0f);uint32_t i=(uint32_t)x;
    float a=(float)CZ_AMP[i];
    if(i<127u)a+=(float)(CZ_AMP[i+1u]-CZ_AMP[i])*(x-(float)i);
    return a*(1.0f/32768.0f)*(float)(15u-atten)*(1.0f/15.0f)*
        (1.0f-(float)((127u-vel)*sense)*(1.0f/(127.0f*15.0f)));
}
/* Continuous evaluation of the chip's phase functions, with fp32 slopes.
 * uint32 phase retains tuning precision; no 11-bit truncation between samples. */
typedef struct { uint32_t wave[2],window; float dcw,pivot,k0,k1; } cz_hw_pd_t;
static __attribute__((noinline)) void cz_native_pd(cz_hw_pd_t *p,uint32_t word,float dcw)
{
    p->wave[0]=(word>>13)&7u;p->wave[1]=word&512u?(word>>10)&7u:p->wave[0];
    p->window=(word>>6)&7u;p->dcw=fm_clampf(dcw,0.0f,1023.0f);
    p->pivot=1024.0f-p->dcw;p->k0=1024.0f/p->pivot;p->k1=1024.0f/(2048.0f-p->pivot);
}
static __attribute__((noinline)) float cz_native_wave(const cz_hw_pd_t *b,uint32_t ph,uint32_t toggle)
{
    float pos=(float)ph*(2048.0f/4294967296.0f),half=pos>=1024.0f?pos-1024.0f:pos;
    float pivot=b->pivot,phase=0.0f,window=0.0f;
    switch(b->wave[toggle&1u]){
    case 0:phase=pos<pivot?pos*b->k0:1024.0f+(pos-pivot)*b->k1;break;
    case 1:phase=(half<pivot?half*b->k0:1024.0f)+(pos>=1024.0f?1024.0f:0.0f);break;
    case 2:phase=pos<pivot*2.0f?pos*b->k0:2048.0f;break;
    case 3:return 0.0f;
    case 4:phase=pos<pivot?pos*b->k0*2.0f:(pos-pivot)*b->k1*2.0f;break;
    case 5:phase=pos<1024.0f?pos:pos<pivot+1024.0f?1024.0f+half*b->k0:2048.0f;break;
    case 6:phase=pos*(1.0f+b->dcw*(1.0f/64.0f));break;
    default:phase=half<pivot?half*b->k0:2048.0f;break;
    }
    phase-=(float)(uint32_t)(phase*(1.0f/2048.0f))*2048.0f;
    switch(b->window){
    case 0:break;
    case 1:window=pos;break;
    case 2:window=pos<1024.0f?2048.0f-half*2.0f:half*2.0f;break;
    case 3:if(pos>=1024.0f)window=half*2.0f;break;
    case 4:window=pos<1024.0f?pos*2.0f:2048.0f;break;
    default:window=(1024.0f-half)*2.0f;break;
    }
    /* The normalized sine table is shared with FM6. Interpolate its cosine
     * quadrant in float: no chip-phase or PCM truncation before the mixer. */
    float x=phase*0.5f+256.0f;uint32_t i=(uint32_t)x;float f=x-(float)i;
    i&=1023u;
    float carrier=1.0f-(FM6_SIN[i]+(FM6_SIN[i+1u]-FM6_SIN[i])*f);
    return carrier*(1.0f-window*(1.0f/2048.0f))-1.0f;
}
static __attribute__((noinline)) float cz_native_vibrato(cz_voice_t *c, const uint8_t *b)
{
    uint32_t delay = (uint32_t)b[6] | (uint32_t)b[7] << 8, inc = (uint32_t)b[9] | (uint32_t)b[10] << 8;
    /* Approximate control clock; needs verification against a CZ-1. */
    c->vib_ticks++;
    if (c->vib_ticks < (uint32_t)((float)delay*FS/(200.0f*CTL))) return 0;
    c->vib_phase += (uint32_t)((float)inc*((float)CTL*200.0f*65536.0f/FS));
    int32_t x = (int32_t)(c->vib_phase >> 16), wave;
    if (b[4] & 32u) wave = 32767-x;
    else if (b[4] & 8u) wave = x < 32768 ? x*2-32768 : 98303-x*2;
    else if (b[4] & 4u) wave = x-32768;
    else wave = x < 32768 ? -32768 : 32767;
    uint32_t depth = (uint32_t)b[12] | (uint32_t)b[13] << 8;
    /* DEPTH 0 is encoded as machine depth 1 (Casio p. 85): keep it neutral. */
    return depth<=1u ? 0.0f : (float)wave*(float)depth*(1.0f/8388608.0f);
}
static __attribute__((noinline)) uint32_t cz_native_inc(float note,int32_t fine)
{
    float hz=(float)tuning_a4*fm_exp2f((note-69.0f)*(1.0f/12.0f));
    return (uint32_t)fm_clampf(hz*(4294967296.0f/FS)*(1.0f+(float)fine*(1.0f/4096.0f)),0.0f,2147483520.0f);
}
static __attribute__((noinline)) void cz_native_render(track_t *t,voice_t *v,int32_t *out,uint32_t n,const vmod_t *m)
{
    if(!n)return;
    const uint8_t *b=cz_patch[(uint32_t)(t-trk)%NTRK].raw;
    cz_voice_t *c=cz_voice(t,v);cz_native_def_t defs[2][3];cz_hw_pd_t pd[2];
    uint32_t inc[2],ph[2]={v->ph[0],v->ph[1]},tg[2]={(uint32_t)v->s[0],(uint32_t)v->s[1]};
    float amp[2][2],step[2];
    uint32_t ls=b[0]&3u,first=ls==1u?1u:0u,last=ls>=2u?1u:first;
    float vib=cz_native_vibrato(c,b),oct=(b[0]>>2)==1u?12.0f:(b[0]>>2)==2u?-12.0f:0.0f;
    cz_native_defs(t,v,defs);
    for(uint32_t l=0;l<2u;l++){
        uint32_t src=ls==2u?0u:l,off=src*57u;
        if(src!=l)memcpy(defs[l],defs[src],sizeof defs[l]);
        uint32_t av=15u-(b[20u+off]>>4),al=b[16u+off]>>4;
        amp[l][0]=cz_native_amp(c->native_eg[l][2].level,al,av,v->vel);
        float pitch=cz_native_env_tick(&c->native_eg[l][0],&defs[l][0],v->gate);
        float depth=cz_native_env_tick(&c->native_eg[l][1],&defs[l][1],v->gate);
        cz_native_env_tick(&c->native_eg[l][2],&defs[l][2],v->gate);
        amp[l][1]=cz_native_amp(c->native_eg[l][2].level,al,av,v->vel);step[l]=(amp[l][1]-amp[l][0])/(float)n;
        uint32_t pv=15u-(b[54u+off]>>4),wv=15u-(b[37u+off]>>4);
        pitch*=1.0f-(float)((127u-v->vel)*pv)*(1.0f/(127.0f*15.0f));
        depth*=1.0f-(float)((127u-v->vel)*wv)*(1.0f/(127.0f*15.0f));
        uint32_t kf=b[18u+off],kfnote=v->note>36u?v->note-36u:0u;
        depth*=96.0f/(float)(96u+kf*kfnote);
        float det=l?((float)b[3]+(float)(b[2]>>2)*(1.0f/64.0f))*(b[1]?-1.0f:1.0f):0.0f;
        float nt=fm_clampf((float)m->pitch16*(1.0f/16.0f)+oct+vib+pitch+det,0.0f,2047.0f/16.0f);
        inc[l]=cz_native_inc(nt,m->fine);
        uint32_t word=(uint32_t)b[14u+off]<<8|b[15u+off];
        float dep=depth+(float)(m->cutoff+m->shape-(64<<8))*(1.0f/32.0f);
        cz_native_pd(&pd[l],word,dep);
    }
    v->s[4]=(int32_t)(c->native_eg[first][2].level*256.0f);
    uint32_t modulation=(b[15]>>3)&7u;
    for(uint32_t i=0;i<n;i++){
        float line[2]={0.0f,0.0f};
        for(uint32_t l=first;l<=last;l++){
            uint32_t old=ph[l],delta=inc[l];
            if(l && modulation==3u){
                c->noise^=c->noise<<13;c->noise^=c->noise>>17;c->noise^=c->noise<<5;
                if(c->noise&1u)delta=(uint32_t)fm_minf((float)delta*(1625.0f/256.0f),2147483520.0f);
            }
            float raw=cz_native_wave(&pd[l],ph[l],tg[l]);ph[l]+=delta;
            if(ph[l]<old)tg[l]^=1u;
            c->native_dc[l]+=(raw-c->native_dc[l])*(1.0f/1024.0f);
            raw=(raw-c->native_dc[l])*0.5f;
            line[l]=raw*(amp[l][0]+step[l]*(float)i);
        }
        float sample=first==last?line[first]:modulation==4u?line[0]*line[1]*2.0f:(line[0]+line[1])*0.5f;
        float gain=((float)m->amp0+(float)(m->amp1-m->amp0)*(float)i*(1.0f/CTL))*(1.0f/32768.0f);
        out[i]+=(int32_t)(sample*gain*(4.0f*VOICE_FS));
    }
    v->ph[0]=ph[0];v->ph[1]=ph[1];v->s[0]=(int32_t)tg[0];v->s[1]=(int32_t)tg[1];
}
