/* SPDX-License-Identifier: GPL-3.0-only */
/* X0X drum part: the 9W9 TR-909 engine (Charles Vestal, GPL-3.0; itself grown out
 * of ER-99 by Matthew Cieplak, GPL-3.0), ported to the FM-1.
 *
 * Circuit-modelled BD, SD, LT/MT/HT, RS, CP from x0x/9W9. Melodee synthesizes
 * CH/OH, CR and RD: these are approximations, with no recorded buffers. The
 * sends and the master stage use Melodee's existing mixer.
 *
 * Mono, 44.1 kHz, float, no libm, no allocation. Idle voices cost nothing:
 * every voice has 9W9's mute countdown, and the block loop runs a voice only for
 * the samples it is still sounding.
 *
 * Threading: drum909_trigger / drum909_set are called between render blocks
 * (the same thread, or with the audio ISR held off); none of them loops over
 * more than a few dozen operations. */
#pragma once
#include <stdint.h>
#include "x0x_param.h"
#include "drum909_dsp.h"

enum { D9_BD, D9_SD, D9_LT, D9_MT, D9_HT, D9_RS, D9_CP, D9_CH, D9_OH, D9_CR, D9_RD, D9_NUM, D9_KIT = D9_NUM };

#define D9_MAX_PARAMS 9          /* most params any voice (or the kit) has (X0X: + PAN) */
#define D9_NZ_HIST 256           /* noise history kept across blocks (filter warm-up) */
#define D9_NZ_BUF (D9_NZ_HIST + 2 * CTL)

/* BD and SD: 9W9's er99_bt_t */
typedef struct {
    /* panel values (engineering units) */
    float tune, sweep_depth, sweep_time, decay, attack, click_tone, level;
    float tune2, osc2_mix, snappy, noise_decay, noise_hp, amp_hold, pitch_mod;
    float drive;
    int32_t dist_type;
    d9_shape_t shape;
    /* runtime */
    uint32_t ph, ph2;
    d9_env_t pitch, amp, click_env, noise_env;
    d9_biquad_t click_lp, noise_hpf, noise_lpf, dc_block;
    int32_t noise_hold, noise_gated, amp_hold_left, bd_phold, impulse, mute;
    int32_t click_stale, click_warm;   /* skipped click filter: see d9_render_bd */
    float bd_df, bd_mult, bd_base, out_gain;
    float crush_st[2];
} d9_bt_t;

/* LT/MT/HT: 9W9's er99_tom_t (three VCOs); reads the panel from a d9_bt_t */
typedef struct {
    uint32_t ph[3];
    d9_env_t env[3], pitch, noise_env;
    d9_biquad_t noise_bp, dc_block;
    float out_gain, noise_level;
    float crush_st[2];
    int32_t mute;
    int32_t stick_stale, stick_warm;   /* skipped stick filter: see d9_render_tom */
} d9_tom_t;

typedef struct {
    float tune, tune2, res, decay, noise_mix, drive, level, accent;
    int32_t dist_type;
    d9_shape_t shape;
    d9_biquad_t bp1, bp2, hp;
    d9_env_t amp;
    float crush_st[2];
    int32_t impulse, mute;
} d9_rim_t;

typedef struct {
    float tune, res, spread, burst_decay, tail_decay, tail_level, drive, level, accent;
    int32_t dist_type;
    d9_shape_t shape;
    d9_biquad_t bp, hp;
    d9_env_t burst, tail;
    float crush_st[2];
    float next_pulse;
    int32_t pulse_index, mute;
} d9_clap_t;

typedef struct {
    float decay, volume, pitch, drive;
    int32_t dist_type;
    d9_shape_t shape;
    /* Melodee: synthesized metal, no recorded cymbal buffers. */
    uint32_t ph[6], noise;
    d9_biquad_t bp, hp;
    int32_t playing, kind;
    d9_env_t out;
    float crush_st[2];
    int32_t mute;
} d9_smp_t;

struct drum909 {
    d9_bt_t bt[5];                /* BD SD LT MT HT panels; BD and SD voices */
    d9_tom_t tom[3];              /* LT MT HT voices */
    d9_rim_t rim;
    d9_clap_t clap;
    d9_smp_t smp[4];              /* CH OH CR RD */
    float send_rev[D9_NUM], send_dly[D9_NUM];
    float pan_l[D9_NUM], pan_r[D9_NUM];   /* X0X: each voice's pan (x0x_pan_gains) */
    float pan_lc[D9_NUM], pan_rc[D9_NUM]; /* where each voice's pan gains are now, gliding to pan_l/r */
    float pan_dl[D9_NUM], pan_dr[D9_NUM]; /* this block's glide per sample */
    float accent, vel_depth;      /* kit */
    uint32_t noise;
    float nz_buf[D9_NZ_BUF];      /* noise, linear; the block plus >= D9_NZ_HIST before it */
    int32_t nz_pos;               /* where the next block's noise starts */
    uint8_t pots[D9_NUM + 1][D9_MAX_PARAMS];
};
typedef struct drum909 drum909_t;

void drum909_init(drum909_t *d);
void drum909_trigger(drum909_t *d, int voice, float vel);
void drum909_render(drum909_t *d, float *dry, float *rev, float *dly, int n);
/* X0X stereo: each voice placed by its PAN into dry_l / dry_r (sends mono); all centred, both are
 * drum909_render's dry exactly */
void drum909_render_st(drum909_t *d, float *dry_l, float *dry_r, float *rev, float *dly, int n);
void drum909_pan_settle(drum909_t *d);   /* every pan to its target at once (the kit is silent: no zipper) */
int drum909_nparams(int voice);
const x0x_param_t *drum909_param(int voice, int i);
void drum909_set(drum909_t *d, int voice, int i, int value);
int drum909_get(const drum909_t *d, int voice, int i);

/* true while any voice is still sounding (for a caller that wants to skip work) */
int drum909_active(const drum909_t *d);
