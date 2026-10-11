/* SPDX-License-Identifier: GPL-3.0-only
 * The tape delay originates in schwung-space-delay, Copyright (c) 2025 Charles Vestal, MIT licence
 * (github.com/charlesvestal/schwung-space-delay; the notice: LICENSES/MIT-schwung-space-delay.txt). */
/* The delay bus (fx.c fx_buses): fm1-x0x's tape delay (Charles Vestal, github.com/charlesvestal/fm1-x0x,
 * firmware/src/dsp/fxbus.c fx_delay; its TAPE after his schwung-space-delay, its DIGI mode 9W9's er99_dly_tick by
 * athousanddetails, after ER-99 by Matthew Cieplak), in fixed point at half the rate. Melodee's FPU-less build runs no float in the audio ISR, and
 * x0x's 2 s line at 44.1 kHz (176 KB, resident) does not fit the arena: here 32768 samples at 22.05 kHz (1.49 s,
 * 64 KiB), borrowed (RES_DELAY) while anything sounds in it and given back once the line and every state are 0.
 *
 * TYPE (G_DTYPE): DIGI, TAPE, DG-PP, TP-PP (ping-pong). TIME (G_DTIME): a note value at the tempo (the external clock's
 * too); one longer than the line plays half as long, still on the beat (x0x's fx_retime). FDBK, TONE, MIX, WEAR.
 * The send (every other sample, 1 2 1) -> two low cuts (~110 Hz: x0x's 150 Hz HPF, no knob left for it) -> the
 * line (int16, a quarter of Q15: 4x headroom, the loop's sums) <- the loop. The read: between samples (Q12), at a
 * time that glides to its target (~23 ms: a time change bends the pitch, as tape and x0x do; no click).
 * DIGI: the loop = the echo x FDBK (0 .. 0.84, 9W9's 0.85) through a one-pole low-pass (TONE: ~430 Hz .. 7.6 kHz).
 * TAPE: the read swings with wow (0.5 Hz, +-1.5 ms) and flutter (6.1 Hz, +-0.15 ms), parabolic sines, both x
 * (0.1 + 1.9 WEAR); FDBK reaches 1.14 (self-oscillation, held by the saturation); the low-pass darker
 * (x (0.6 - 0.3 WEAR)); then tanh(u g) / g (g = 1 + 1.5 WEAR: a Pade tanh, exactly 1 from u g = 3) and a 27 Hz DC
 * block. The echo is heard before the low-pass (the first repeat bright, the ones after darker), x MIX (0 .. 1.2).
 * PP: the left hears the line at the time, the right at twice it, the loop fed from the right: echoes L R L R; when
 * twice the time does not fit the line, centred. A TYPE change glides the wow / flutter and the ping-pong in and out.
 * Out: the mid added to the wet bus, the side (L - R) / 2 into dl.sd, which mix_block adds to the left and takes
 * from the right (dly_side_mix), as HALL's. */
#define DL_N 32768u                      /* the line, samples at 22.05 kHz: 1.49 s, 64 KiB */
#define DL_MASK (DL_N - 1u)
#define DL_NC (CTL / 2u)                 /* a block at half the rate */
#define DL_WOW 97392u                    /* 0.5 Hz, a sample at 22.05 kHz (2^32 x 0.5 / 22050) */
#define DL_FLUT 1188176u                 /* 6.1 Hz */
#define DL_GLIDE 64                      /* TYPE's glides, Q15 a sample: 23 ms */
static int16_t *dly_buf;
static struct {
    uint32_t w, quiet;                   /* the write index; samples written 0 from a silent send (DL_N: all 0) */
    uint32_t wow, flut;                  /* TAPE's phases */
    int32_t dcur;                        /* the time read, Q12 samples (glides to the target) */
    int32_t lp, le;                      /* the loop's low-pass, its step's remainder */
    int32_t hp, he;                      /* TAPE's DC block (lowcut1) */
    int32_t c1, e1, c2, e2;              /* the send's low cuts */
    int32_t xp;                          /* the send's last odd sample (the 1 2 1) */
    int32_t m, s;                        /* the last mid and side out (half rate) */
    int32_t tw, pw;                      /* TAPE's swing and PP's share, Q15 */
    uint8_t side;                        /* sd holds this block's stereo difference */
    int32_t sd[CTL];
} dl __attribute__((section(".pool")));

/* the time's note value in samples at 44.1 kHz: N_DDIV (params.c), fx.c div_samples' ten, then 1/16. 1/8. 1/4. 4T */
static uint32_t dly_samples(void)
{
    uint32_t v = (uint32_t)song.g[G_DTIME], q = beat_samples();
    if (v < 10u)
        return div_samples(v);
    return v == 10u ? q * 3u / 8u : v == 11u ? q * 3u / 4u : v == 12u ? q * 3u / 2u : q * 2u / 3u;
}
/* .. at half the rate, Q12; longer than the line: halved till it fits (on the beat still) */
static int32_t dly_target(void)
{
    uint32_t s = dly_samples() >> 1;
    while (s > DL_N - 128u)
        s >>= 1;
    return (int32_t)((s < 16u ? 16u : s) << 12);
}
/* x0x's parabolic sine of a turn, 4 x (1 - |x|), Q15 */
static inline int32_t dl_psin(uint32_t ph)
{
    int32_t x = (int32_t)ph >> 16;
    return (x * (32768 - (x < 0 ? -x : x))) >> 13;
}
/* tanh, Q12 in and out: x (27 + x^2) / (27 + 9 x^2) (within 2.4 %), exactly 1 and flat from |x| = 3 */
static inline int32_t dl_tanh(int32_t x)
{
    int32_t x2;
    x = clamp(x, -3 * 4096, 3 * 4096);
    x2 = (x * x) >> 12;
    return x * (110592 + x2) / (110592 + 9 * x2);
}
/* the line d (Q12 samples) behind the write index w, between samples */
static inline int32_t dl_read(const int16_t *b, uint32_t w, int32_t d)
{
    uint32_t rp = (w << 12) - (uint32_t)d, i0 = (rp >> 12) & DL_MASK;
    int32_t f = (int32_t)(rp & 4095u), a = b[i0];
    return a + (((b[(i0 + 1u) & DL_MASK] - a) * f) >> 12);
}
/* m / 2^sh rounded towards zero (a feedback product: a floor would keep a -1 going round the loop for ever) */
static inline int32_t dl_tz(int32_t m, uint32_t sh) { return (m + ((m >> 31) & ((1 << sh) - 1))) >> sh; }
static inline int32_t dl_glide(int32_t v, int32_t to)
{
    return v + clamp(to - v, -DL_GLIDE, DL_GLIDE);
}

/* n samples of the time's glide, the clocks and the glides (the idle line: nothing else would change) */
static void dly_clocks(int32_t target, int32_t tt, int32_t pt, uint32_t n)
{
    uint32_t i;
    dl.w = (dl.w + n) & DL_MASK;
    dl.wow += DL_WOW * n;
    dl.flut += DL_FLUT * n;
    for (i = 0; i < n && dl.dcur != target; i++) {
        int32_t d = (target - dl.dcur) >> 9;
        dl.dcur += d ? d : target - dl.dcur;
    }
    dl.tw = tt;
    dl.pw = pt;
}

/* one block (n = CTL) of the send in, the mid added to out, the side into dl.sd */
static __attribute__((noinline)) void dly_bus(const int32_t *in, int32_t *out, uint32_t n)
{
    uint32_t i, any = 0, type = (uint32_t)song.g[G_DTYPE] & 3u, tape = type & 1u;
    int32_t target = dly_target(), wear = song.g[G_DWEAR], tc = 1966 + song.g[G_DCOLOR] * 155;   /* Q15 at 44.1 kHz */
    int32_t depth = 410 + wear * 61;                                 /* Q12: 0.1 + 1.9 WEAR */
    int32_t wa = (depth * 2117) >> 9, fa = (depth * 212) >> 9;       /* /8: 1.5 ms, 0.15 ms (Q12 samples) x depth */
    int32_t g = 4096 + wear * 48, ig = (1 << 27) / g;                /* the saturation's drive (Q12), 1 / it (Q15) */
    int32_t fb = song.g[G_DFDBK] * (tape ? 156 : 115);               /* Q14: 0 .. 1.14 / 0 .. 0.84 */
    int32_t lvl = song.g[G_DMIX] * 310;                              /* Q15: 0 .. 1.2 */
    int32_t tt = tape ? 32767 : 0, pt, sany = 0;
    int16_t *b;
    dl.side = 0;
    if (!dl.dcur)                                        /* (the first block: at the time, no glide from 0) */
        dl.dcur = target;
    if (!tape)                                           /* (DIGI has no DC block: its state at rest) */
        dl.hp = dl.he = 0;
    for (i = 0; i < n; i++)
        any |= (uint32_t)in[i];
    /* PP: only while twice the time (and its swing) fits the line */
    pt = type >= 2u && 2 * (target > dl.dcur ? target : dl.dcur) + 2 * (wa + fa) * 8 + (8 << 12) < (int32_t)(DL_N << 12)
         ? 32767 : 0;
    if (!any && (!dly_buf || dl.quiet >= DL_N) && !(dl.lp | dl.hp | dl.c1 | dl.c2 | dl.xp | dl.m | dl.s)) {
        if (dly_buf) {                                   /* silent: every state at its rest, the line all 0: */
            resource_release(RES_DELAY);                 /* given back, the rounding remainders cleared (a new */
            dly_buf = 0;                                 /* send then plays as on a fresh bus) */
            dl.le = dl.he = dl.e1 = dl.e2 = 0;
        }
        dly_clocks(target, tt, pt, n / 2u);
        return;
    }
    if (!dly_buf) {
        if (!(dly_buf = resource_get(RES_DELAY, DL_N * sizeof(int16_t)))) {
            dly_clocks(target, tt, pt, n / 2u);          /* (no memory now: silent) */
            return;
        }
        dl.quiet = 0;                                    /* (a line all 0, its count from now) */
    }
    b = dly_buf;
    if (tape)
        tc = (tc * (19661 - wear * 77)) >> 15;          /* x (0.6 - 0.3 WEAR) */
    tc = (2 * tc - ((tc * tc) >> 15)) >> 1;              /* the same corner at 22.05 kHz, Q14 */
    for (i = 0; i < n / 2u; i++) {
        int32_t x = (dl.xp + 2 * in[2u * i] + in[2u * i + 1u]) >> 4, y, yr, d, e, s, k, mm, ss;
        uint32_t w = dl.w;
        dl.xp = in[2u * i + 1u];
        x = lowcut1(x, &dl.c1, &dl.e1, 5);
        x = lowcut1(x, &dl.c2, &dl.e2, 5);
        d = (target - dl.dcur) >> 9;                    /* the time glides */
        dl.dcur += d ? d : target - dl.dcur;
        dl.wow += DL_WOW;
        dl.flut += DL_FLUT;
        dl.tw = dl_glide(dl.tw, tt);
        dl.pw = dl_glide(dl.pw, pt);
        d = dl.dcur;
        if (dl.tw)                                       /* TAPE: wow and flutter */
            d += ((((wa * dl_psin(dl.wow) + fa * dl_psin(dl.flut)) >> 12) >> 3) * dl.tw) >> 12;
        d = clamp(d, 2 << 12, (int32_t)((DL_N - 4u) << 12));
        y = dl_read(b, w, d);
        yr = dl.pw ? y + (((dl_read(b, w, clamp(2 * d, 2 << 12, (int32_t)((DL_N - 4u) << 12))) - y) * dl.pw) >> 15) : y;
        e = (dl_tz(yr * fb, 14) - dl.lp) * tc + dl.le;  /* the loop's low-pass (from the right: PP) */
        dl.le = e & 16383;
        dl.lp += e >> 14;
        s = dl.lp;
        if (tape) {                                      /* saturation, then the DC block */
            s = (dl_tanh((s * g) >> 13) * ig) >> 14;
            s = lowcut1(s, &dl.hp, &dl.he, 7);
        }
        k = clamp(x + s, -32767, 32767);
        b[w] = (int16_t)k;
        dl.w = (w + 1u) & DL_MASK;
        dl.quiet = k || x ? 0u : dl.quiet < DL_N ? dl.quiet + 1u : DL_N;
        mm = (((y + yr) >> 1) * lvl) >> 13;             /* (a quarter of Q15 back to Q15) */
        ss = (((y - yr) >> 1) * lvl) >> 13;
        out[2u * i] += (dl.m + mm) >> 1;                 /* back to 44.1 kHz, between samples */
        out[2u * i + 1u] += mm;
        dl.sd[2u * i] = (dl.s + ss) >> 1;
        dl.sd[2u * i + 1u] = ss;
        dl.m = mm;
        dl.s = ss;
        sany |= ss | dl.sd[2u * i];
    }
    dl.side = sany != 0;
}

/* after the buses: PP's stereo difference to the left and from the right of the dry mix */
static __attribute__((noinline)) void dly_side_mix(int32_t *l, int32_t *r, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        l[i] += dl.sd[i];
        r[i] -= dl.sd[i];
    }
}
