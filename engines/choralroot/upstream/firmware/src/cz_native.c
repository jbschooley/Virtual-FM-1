/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) Devin Acker
 * Modifications Copyright (c) 2026 Kerem Kilic (Ellic Studio)
 * License: LICENSES/BSD-3-Clause-uPD933.txt
 * Native CZ-1 tone playback. Parameter encoding: Casio MIDI specification.
 * Chip envelope rate, phase functions and logarithmic DCA law follow the
 * independently documented uPD933 model by Devin Acker (MAME, BSD-3-Clause).
 * See docs/CZ1_SYSEX.md for sources and remaining calibration limits. */
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
CR_HOT_CZ static __attribute__((noinline)) uint32_t cz_hw_rate(uint32_t raw, uint32_t e)
{
    uint32_t chip = (8u | (raw & 7u)) << ((raw & 127u) >> 3);
    /* uPD933 at 40 kHz; convert its three different fixed-point domains to
     * our Q24 control tick. DCO covers 128 semitones, DCW 1024, DCA 512. */
    uint32_t shift = e == 0u ? 3u : e == 1u ? 2u : 1u;
    return (uint32_t)(((uint64_t)chip * CTL * 40000u / FS) >> shift);
}
static int32_t cz_hw_target(uint32_t raw, uint32_t e)
{
    raw &= 127u;
    if (e == 0u) return (int32_t)((raw & 63u) << (raw & 64u ? 18 : 13));
    return (int32_t)(raw << 17); /* DCW: 8 units; DCA: 4 units */
}
/* The six envelopes of a tone, without the DCA's key follow: the tone alone gives them, so they are built once per
 * tone change (cz_native_render: eng_cz.c cz_pgen) and not per voice and block. */
CR_HOT_CZ static __attribute__((noinline)) void cz_native_defs(const uint8_t *b, cz_env_def_t d[2][3])
{
    for (uint32_t l = 0; l < 2u; l++) for (uint32_t e = 0; e < 3u; e++) {
        cz_env_def_t *p = &d[l][e]; uint32_t base = CZ_ENV_BASE[l][e];
        memset(p, 0, sizeof *p); p->end = b[CZ_ENV_END[l][e]] & 7u; p->sustain = 255;
        for (uint32_t k = 0; k < 8u; k++) {
            uint32_t lev = b[base + 2u*k + 1u];
            p->rate[k] = cz_hw_rate(b[base + 2u*k], e);
            p->level[k] = cz_hw_target(lev, e);
            if ((lev & 128u) && p->sustain == 255u && k < p->end) p->sustain = (uint8_t)k;
        }
    }
}
/* Native DCA key-follow is rate scaling; higher keys run faster.
 * Keep raw key-follow and velocity bytes intact for recalibration.
 * The DCA rates of a voice: the tone's and the note's, so built once per note (and tone change). */
CR_HOT_CZ static __attribute__((noinline)) void cz_native_krate(const uint8_t *b, cz_env_def_t d[2][3], uint32_t vnote, uint32_t kr[2][8])
{
    uint32_t note = vnote > 36u ? vnote - 36u : 0u;
    for (uint32_t l = 0; l < 2u; l++) {
        uint32_t kf = b[16u + l*57u] & 15u;
        for (uint32_t k = 0; k < 8u; k++) {
            uint32_t r = d[l][2].rate[k];
            kr[l][k] = r + (uint32_t)(((uint64_t)r * kf * note) / 96u);
        }
    }
}
CR_HOT_CZ static __attribute__((noinline)) int32_t cz_native_amp(int32_t level, uint32_t atten, uint32_t sense, uint32_t vel)
{
    /* Interpolate the chip's logarithmic amplitude, not a linear percentage. */
    uint32_t x = (uint32_t)clamp(level, 0, 127<<17), i = x >> 17, f = x & 0x1FFFFu;
    int32_t a = CZ_AMP[i];
    if (i < 127u) a += (int32_t)((uint32_t)(CZ_AMP[i+1u]-CZ_AMP[i]) * f >> 17);
    a = a * (int32_t)(15u-atten) / 15;
    return a * (int32_t)(127u*15u - (127u-vel)*sense) / (127*15);
}
/* Block-prepared slopes of the chip's 11-bit phase functions. dq, dstep: the DCW depth in Q16 at the block's start
 * (where the line's last block left it) and its step a sample, when the depth changed (dstep 0: it did not, and the
 * block's pivot and slopes are every sample's). */
typedef struct { uint32_t wave[2], window, dcw, pivot, k0, k1, dq, dstep; } cz_hw_pd_t;
/* pk: the voice's pivot and slopes of this line's last block: the two divides only when the DCW depth changed.
 * The depth of the last block (1024 - pk[0]; pk[0] 0: a new voice, none) is where this block's ramp starts: the depth
 * is the envelope's of a control block, and a step of it at a block's edge is a step of the phase function, so of
 * the sample (a pop on every fast DCW attack of a low note: 0.19, docs/CZ1.md "Pops"). The chip's envelopes step at
 * its sample rate (cz_hw_rate). */
CR_HOT_CZ static __attribute__((noinline)) void cz_native_pd(cz_hw_pd_t *p, uint32_t word, uint32_t dcw, uint32_t *pk, uint32_t n)
{
    int32_t was = pk[0] ? (int32_t)(1024u - pk[0]) : (int32_t)dcw, mv = (int32_t)dcw - was;
    p->wave[0] = (word >> 13) & 7u;
    p->wave[1] = word & 512u ? (word >> 10) & 7u : p->wave[0];
    p->window = (word >> 6) & 7u; p->dcw = dcw;
    p->pivot = 1024u - dcw;
    p->dq = (uint32_t)was << 16;
    /* (a whole block: the divide by n as the shift it is; after n steps dq is dcw << 16 then) */
    p->dstep = !mv ? 0u : n == CTL ? (uint32_t)(mv * (65536 >> CTL_LOG2)) : (uint32_t)(mv * 65536 / (int32_t)n);
    if (pk[0] != p->pivot) {
        pk[0] = p->pivot;
        pk[1] = (1024u << 16) / p->pivot;
        pk[2] = (1024u << 16) / (2048u - p->pivot);
    }
    p->k0 = pk[1]; p->k1 = pk[2];
}
/* One sample of a line: the chip's phase function (wave) and window at pos, the top 11 bits of the phase. The
 * carrier is the generated CZ_CARRIER[phase] = (pd_cos((phase << 5) + (phase >> 6)) + 32767) >> 1 (tools/
 * gen_tables.py: the cosine's interpolation done there, for the 2048 phases there are). wave and win are constants
 * where a loop is built for one of each (cz_native_run): the switches are the compiler's then, not a sample's. */
static inline __attribute__((always_inline)) int32_t cz_native_wave(uint32_t wave, uint32_t win, uint32_t pos, uint32_t pivot,
                                                                    uint32_t k0, uint32_t k1, uint32_t dcw)
{
    uint32_t phase = 0, window = 0;
    switch (wave) {
    case 0: phase = pos < pivot ? pos*k0 >> 16 : 1024u + ((pos-pivot)*k1 >> 16); break;
    case 1: phase = (pos & 1023u) < pivot ? ((pos & 1023u)*k0 >> 16) : 1023u; phase |= pos & 1024u; break;
    case 2: phase = pos < pivot*2u ? pos*k0 >> 16 : 2047u; break;
    case 3: return 0;                                    /* undocumented silent wave */
    case 4: phase = pos < pivot ? pos*k0 >> 15 : (pos-pivot)*k1 >> 15; break;
    case 5: phase = pos < 1024u ? pos : pos < pivot+1024u ? 1024u+((pos&1023u)*k0 >> 16) : 2047u; break;
    case 6: phase = pos + ((pos*dcw) >> 6); break;
    default: phase = (pos&1023u) < pivot ? (pos&1023u)*k0 >> 16 : 2047u; break;
    }
    phase &= 2047u;
    switch (win) {
    case 0: break;
    case 1: window = pos; break;
    case 2: window = (pos&1023u)*2u; if (pos < 1024u) window ^= 2046u; break;
    case 3: if (pos >= 1024u) window = (pos&1023u)*2u; break;
    case 4: window = pos < 1024u ? pos*2u : 2047u; break;
    default: window = (1023u ^ (pos&1023u))*2u; break;
    }
    return (((int32_t)CZ_CARRIER[phase] * (int32_t)(2048u-window)) >> 10) - 32767;
}
/* A line over a block: its phase, the cycle's toggle (which of its two waves plays), its DC filter, the DCA's ramp. */
typedef struct { uint32_t ph, tg, inc; int32_t dc, amp, step; } cz_line_t;
/* A line's samples from i on until its phase wraps (the next cycle may play the other wave) or the block ends, into
 * o; returns where it stopped. Built once for each wave and window (cz_native_line). */
static inline __attribute__((always_inline)) uint32_t cz_native_run(const cz_hw_pd_t *b, cz_line_t *s, int32_t *o, uint32_t i,
                                                                    uint32_t n, uint32_t wave, uint32_t win, uint32_t mov)
{
    uint32_t ph = s->ph, old, inc = s->inc, pivot = b->pivot, k0 = b->k0, k1 = b->k1, dcw = b->dcw;
    uint32_t dq = b->dq+b->dstep*i, dstep = b->dstep;
    int32_t dc = s->dc, amp = s->amp, step = s->step;
    do {
        if (mov) {                                     /* the depth of this sample, its pivot and slopes (CZ_RECIP) */
            dq += dstep; dcw = dq>>16; pivot = 1024u-dcw; k0 = CZ_RECIP[pivot]; k1 = CZ_RECIP[2048u-pivot];
        }
        int32_t raw = cz_native_wave(wave,win,ph>>21,pivot,k0,k1,dcw);
        old = ph; ph += inc;
        dc += raw-(dc>>10); raw = (raw-(dc>>10))>>1;
        o[i] = mulq15(raw,amp+((step*(int32_t)i)>>8));
        i++;
    } while (ph >= old && i < n);
    if (ph < old) s->tg ^= 1u;
    s->ph = ph; s->dc = dc;
    return i;
}
/* A line's block: the wave of the cycle and the window pick the loop, once a cycle and not once a sample (windows
 * 5..7 are one; the silent wave has no window). 43 loops: about 9 KB of flash for a sample's two switches.
 * Twice: cz_native_line for a block whose DCW depth is the last block's (the usual one: a held note at its sustain),
 * cz_native_line_mv for one whose depth changed, ramped a sample at a time (mov). */
#define CZ_RUN(w,k) case (w)*6u+(k): i = cz_native_run(b,s,o,i,n,w,k,CZ_MOV); break;
#define CZ_RUNS(w) CZ_RUN(w,0u) CZ_RUN(w,1u) CZ_RUN(w,2u) CZ_RUN(w,3u) CZ_RUN(w,4u) CZ_RUN(w,5u)
#define CZ_LINE_BODY \
    uint32_t win = b->window > 5u ? 5u : b->window; \
    for (uint32_t i = 0; i < n; ) { \
        uint32_t wave = b->wave[s->tg & 1u]; \
        switch (wave == 3u ? 3u*6u : wave*6u+win) { \
        CZ_RUNS(0u) CZ_RUNS(1u) CZ_RUNS(2u) CZ_RUN(3u,0u) CZ_RUNS(4u) CZ_RUNS(5u) CZ_RUNS(6u) \
        CZ_RUN(7u,0u) CZ_RUN(7u,1u) CZ_RUN(7u,2u) CZ_RUN(7u,3u) CZ_RUN(7u,4u) \
        default: i = cz_native_run(b,s,o,i,n,7u,5u,CZ_MOV); break;   /* (the last: no value of the switch leaves i) */ \
        } \
    }
#define CZ_MOV 0u
CR_HOT_CZ static __attribute__((noinline)) void cz_native_line(const cz_hw_pd_t *b, cz_line_t *s, int32_t *o, uint32_t n)
{
    CZ_LINE_BODY
}
#undef CZ_MOV
#define CZ_MOV 1u
CR_HOT_CZ static __attribute__((noinline)) void cz_native_line_mv(const cz_hw_pd_t *b, cz_line_t *s, int32_t *o, uint32_t n)
{
    CZ_LINE_BODY
}
#undef CZ_MOV
#undef CZ_LINE_BODY
#undef CZ_RUNS
#undef CZ_RUN
/* Line 2 with NOISE: the generator moves its increment every sample, so its loop keeps a sample's switches. */
CR_HOT_CZ static __attribute__((noinline)) void cz_native_noise(const cz_hw_pd_t *b, cz_line_t *s, uint32_t *noise, int32_t *o, uint32_t n)
{
    uint32_t ph = s->ph, tg = s->tg, nz = *noise, pivot = b->pivot, k0 = b->k0, k1 = b->k1, dcw = b->dcw;
    uint32_t dq = b->dq, dstep = b->dstep;
    int32_t dc = s->dc;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t old = ph, delta = s->inc;
        nz ^= nz<<13; nz ^= nz>>17; nz ^= nz<<5;
        if (nz&1u) delta = delta > 0x1428A2F9u ? 0x7FFFFFFFu : (delta>>8)*1625u;
        if (dstep) {                                   /* (the depth changed: this sample's, as cz_native_run) */
            dq += dstep; dcw = dq>>16; pivot = 1024u-dcw; k0 = CZ_RECIP[pivot]; k1 = CZ_RECIP[2048u-pivot];
        }
        int32_t raw = cz_native_wave(b->wave[tg&1u],b->window,ph>>21,pivot,k0,k1,dcw); ph += delta;
        if (ph < old) tg ^= 1u;
        dc += raw-(dc>>10); raw = (raw-(dc>>10))>>1;
        o[i] = mulq15(raw,s->amp+((s->step*(int32_t)i)>>8));
    }
    s->ph = ph; s->tg = tg; s->dc = dc; *noise = nz;
}
CR_HOT_CZ static __attribute__((noinline)) int32_t cz_native_vibrato(cz_voice_t *c, const uint8_t *b)
{
    uint32_t delay = (uint32_t)b[6] | (uint32_t)b[7] << 8, inc = (uint32_t)b[9] | (uint32_t)b[10] << 8;
    /* Approximate control clock; needs verification against a CZ-1. */
    c->vib_ticks++;
    if (c->vib_ticks < delay * FS / (200u*CTL)) return 0;
    c->vib_phase += inc * (uint32_t)((uint64_t)CTL*200u*65536u/FS);
    int32_t x = (int32_t)(c->vib_phase >> 16), wave;
    if (b[4] & 32u) wave = 32767-x;
    else if (b[4] & 8u) wave = x < 32768 ? x*2-32768 : 98303-x*2;
    else if (b[4] & 4u) wave = x-32768;
    else wave = x < 32768 ? -32768 : 32767;
    uint32_t depth = (uint32_t)b[12] | (uint32_t)b[13] << 8;
    /* 1/16-semitone pitch, toward zero: DEPTH 0 is machine depth 1 (Casio p. 85), which must not bend
     * the negative half of the wave down a step */
    return (wave * (int32_t)depth) / (1 << 19);
}
#define CZ_VOICE_GAIN 2              /* a voice's sum into the part (below); 4 until 0.19 */
CR_HOT_CZ static __attribute__((noinline)) void cz_native_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    uint32_t tr = (uint32_t)(t-trk)%NTRK, gen = cz_pgen[tr];
    const uint8_t *b = cz_patch[tr].raw;
    cz_voice_t *c = cz_voice(t, v); cz_defs_t *D = cz_defs((uint32_t)(t-trk)); cz_hw_pd_t pd[2];
    uint32_t inc[2];
    int32_t amp[2][2], step[2], line[2][CTL];
    uint32_t ls = b[0]&3u, first = ls == 1u ? 1u : 0u, last = ls >= 2u ? 1u : first;
    if (!c) return;                                    /* (a part without engine state: silent) */
    int32_t vib = cz_native_vibrato(c,b), oct = (b[0]>>2) == 1u ? 192 : (b[0]>>2) == 2u ? -192 : 0;
    if (D->gen != gen) { cz_native_defs(b,D->d); D->gen = gen; }          /* the tone changed */
    if (c->kgen != gen || c->knote != v->note) {                         /* .. or this is another note */
        cz_native_krate(b,D->d,v->note,c->krate); c->kgen = gen; c->knote = v->note;
    }
    for (uint32_t l=0;l<2u;l++) {
        uint32_t src = ls == 2u ? 0u : l, off = src*57u;             /* (1+1': line 1's definitions for both) */
        const cz_env_def_t *defs = D->d[src];
        int32_t level = c->eg[l][2].level;
        int32_t pitch = cz_env_tick(&c->eg[l][0],&defs[0],defs[0].rate,v->gate);
        int32_t depth = cz_env_tick(&c->eg[l][1],&defs[1],defs[1].rate,v->gate);
        cz_env_tick(&c->eg[l][2],&defs[2],c->krate[src],v->gate);
        /* A line the tone does not play: its envelopes run on as they did (a LINE change under a held note finds
         * them where they would be), its level, pitch and phase function are nobody's. */
        if (l < first || l > last) { c->ag[l] = 0; continue; }
        uint32_t av = 15u-(b[20u+off]>>4), al = b[16u+off]>>4;
        /* The block's gain runs from where the line's last block ended (ag: that gain + 1) to this block's. While
         * the velocity and the tone's level bytes are what they were that is the DCA curve at the level before the
         * tick, as it always was (one curve a block now, not two while the envelope moves). A note struck again at
         * another velocity on a voice that sounds (cz_note_on keeps ag), MONO's legato raising it, a LEVEL or V.AMP
         * turned under a held note: the curve of the same level is another gain, and from one sample to the next it
         * was a step of the voice's whole sample (0.19, docs/CZ1.md "Pops": the click of a chord struck again). */
        amp[l][1] = cz_native_amp(c->eg[l][2].level,al,av,v->vel);
        amp[l][0] = c->ag[l] ? c->ag[l]-1 : c->eg[l][2].level == level ? amp[l][1] : cz_native_amp(level,al,av,v->vel);
        c->ag[l] = amp[l][1]+1;
        step[l] = (amp[l][1]-amp[l][0])*256;
        /* (a whole block: the divide by n as the shift it is, towards zero) */
        step[l] = n == CTL ? (step[l]+((step[l]>>31)&(int32_t)(CTL-1u)))>>CTL_LOG2 : step[l]/(int32_t)n;
        uint32_t pv = 15u-(b[54u+off]>>4), wv = 15u-(b[37u+off]>>4);
        /* (no velocity sensitivity: the factor is 65536, the product the value itself) */
        if (pv) pitch = mulq16(pitch, (127u*15u-(127u-v->vel)*pv)*65536u/(127u*15u));
        if (wv) depth = mulq16(depth, (127u*15u-(127u-v->vel)*wv)*65536u/(127u*15u));
        uint32_t kf = b[18u+off], kfnote = v->note > 36u ? v->note-36u : 0u;
        /* (no key follow: depth * 96 / 96 is depth while the product fits, so no divide) */
        if (kf*kfnote || depth > 0x7FFFFFFF/96 || depth < -(0x7FFFFFFF/96)) depth = depth*96/(int32_t)(96u+kf*kfnote);
        int32_t det = l ? ((int32_t)b[3]*16+(b[2]>>2)*16/64)*(b[1] ? -1 : 1) : 0;
        int32_t nt = clamp(m->pitch16+oct+vib+(pitch>>13)+det,0,2047);
        inc[l] = pitch_inc((uint32_t)nt);
        inc[l] += (uint32_t)((int32_t)(inc[l]>>12)*m->fine);
        uint32_t word = (uint32_t)b[14u+off]<<8 | b[15u+off];
        uint32_t dep = (uint32_t)clamp((depth>>14)+((m->cutoff+m->shape-(64<<8))>>5),0,1023);
        cz_native_pd(&pd[l],word,dep,c->pk[l],n);
    }
    v->s[4] = c->eg[first][2].level >> 9;
    uint32_t modulation = (b[15]>>3)&7u;
    if (n > CTL) n = CTL;                              /* (a block: line[]) */
    for (uint32_t l=first;l<=last;l++) {               /* each line's block on its own: a line reads no other */
        cz_line_t s = {v->ph[l],(uint32_t)v->s[l],inc[l],v->s[2u+l],amp[l][0],step[l]};
        if (l && modulation==3u) cz_native_noise(&pd[l],&s,&c->noise,line[l],n);
        else if (pd[l].dstep) cz_native_line_mv(&pd[l],&s,line[l],n);
        else cz_native_line(&pd[l],&s,line[l],n);
        v->ph[l] = s.ph; v->s[l] = (int32_t)s.tg; v->s[2u+l] = s.dc;
    }
    /* The voice into the part: a line is half scale ((raw-dc)>>1), and such an engine adds its voice << 1 (dsp.c
     * voice_amp). Until 0.19 this was * 4: CZ-1 sat 6 dB above the other engines (docs/CZ1.md "Level"); every sample
     * a voice adds is exactly half of what it was (the factor is the last thing done to it). */
    if (first==last) for (uint32_t i=0;i<n;i++) out[i]+=voice_amp(line[first][i],m,i)*CZ_VOICE_GAIN;
    else if (modulation==4u) for (uint32_t i=0;i<n;i++) out[i]+=voice_amp(mulq15(line[0][i],line[1][i])*2,m,i)*CZ_VOICE_GAIN;
    else for (uint32_t i=0;i<n;i++) out[i]+=voice_amp((line[0][i]+line[1][i])>>1,m,i)*CZ_VOICE_GAIN;
#ifdef CZ_PROBE                                         /* (host only: tests/cz_pops.c looks at the voice's block) */
    CZ_PROBE(t,v,first,last,amp,pd,line,n,modulation,m);
#endif
}
