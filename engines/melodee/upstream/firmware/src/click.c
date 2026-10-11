/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Adapted from Felucca's metronome (1.1). Quarter notes in 4/4, with an
 * accented downbeat. A decaying sine at 1760/1320 Hz sounds only at the DAC,
 * after effects, MASTER and USB capture, with its own level. Preferences are device
 * settings; seq.c requests the regular and count-in beats. */
enum { CLICK_OFF, CLICK_REC, CLICK_ON };
static volatile uint8_t click_req;          /* seq.c: 1 a beat, 2 the bar's first; the next block starts it */
/* the magic circle (s, c: a sine and its quadrature) at k = 2 sin(pi f / FS), Q15; env Q15, decaying 1/256 a sample */
#define CLICK_K_DOWN 8195                    /* 1760 Hz */
#define CLICK_K_BEAT 6154                    /* 1320 Hz */
#define CLICK_AMP 16384                      /* the circle's amplitude */
static const uint16_t CLICK_GAIN[3] = {1024, 2048, 4096};   /* LOW MID HIGH, Q12: the downbeat's peak -18 / -12 / -6 dB
                                                             * of DAC full scale (Q15), independent of MASTER */
static struct { int32_t s, c, k, env, g; } clk;

/* add the click to one block of DAC samples (stereo Q15, after the master): out of line, a loop only while it sounds */
static __attribute__((noinline)) void click_render(int32_t *out, uint32_t n)
{
    uint32_t i;
    int32_t s, c, k, env, g;
    if (click_req) {
        uint32_t down = click_req == 2u;
        clk.s = 0;                                  /* (from 0: no step at the start) */
        clk.c = CLICK_AMP;
        clk.k = down ? CLICK_K_DOWN : CLICK_K_BEAT;
        clk.env = 32767;
        clk.g = CLICK_GAIN[settings_click_level % 3u] >> (down ? 0 : 1);   /* Q12: the downbeat x1, a beat x0.5 (-6 dB) */
        click_req = 0;
    }
    if (clk.env <= 0)
        return;
    s = clk.s; c = clk.c; k = clk.k; env = clk.env;
    g = clk.g;                                      /* CLICK LEVEL alone: mix MASTER may even be zero */
    for (i = 0; i < n; i++) {
        int32_t v, l, r;
        s += (k * c) >> 15;
        c -= (k * s) >> 15;
        v = (((s * env) >> 15) * g) >> 12;
        env -= (env >> 8) + 1;
        if (env < 0)
            env = 0;
        l = out[2u * i] + v;
        r = out[2u * i + 1u] + v;
        out[2u * i] = l > 32767 ? 32767 : l < -32767 ? -32767 : l;
        out[2u * i + 1u] = r > 32767 ? 32767 : r < -32767 ? -32767 : r;
    }
    clk.s = s; clk.c = c; clk.env = env;
}
