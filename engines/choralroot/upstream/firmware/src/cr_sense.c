/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
 * After sense.c of Velo (fm1-velocity) and MONK-1 (fm1-monk) by Charles Vestal, GPL-3.0-only: the state machine,
 * the driven samples and the log-scale map are theirs, restated in integers (docs/VELOCITY.md). */
/* Velocity from the key's contact resistance (hal/fm1_input.h: the analog rows PA7 / PA8, 20 of the 27 note keys).
 *
 * Each analog key is sampled every ~2.2 ms, pulled up: 1023 open, below SN_TOUCH when the carbon pill has started to
 * conduct, below SN_PRESS seated. A state machine per key runs on those samples: OPEN, TOUCH, DOWN, LIFT (above
 * PRESS), OPEN again after two samples above SN_RELEASE. Once a key has been touched its every other sample is taken
 * with the row driven weakly high (DRIVE 2: the tick asks cr_sense_drive_hook): that reading, less the diode's and
 * the 595's SN_DBASE, is the pill's resistance. The dome snaps, so every press lands within a sample or two; what
 * tells a hard hit from a soft one is how deep the key sits just after landing: the velocity is the lowest driven
 * reading in the first SN_WAIT driven samples after the touch, on a log scale (conductive rubber's resistance falls
 * as a power of the force): t = log2(r / r_floor) / log2(r_ceil / r_floor), velocity = 1 + 126 (1 - t)^0.7
 * (SN_CURVE, Velo's 830 presses: quartiles 45 / 63 / 80 at that curve). The floor (450) is a firm landing at DRIVE 2,
 * the ceiling (896) the lightest press that seats; Velo's per-key calibration is not here (its open item).
 *
 * The landing lands in cr_sense_land[key] (the driven reading; 0: none yet for this press) at the second pulled-up
 * sample after the touch (a driven one before each: four samples, 8.8 ms after the touch sample;
 * tests/cr_sense_test.c); the UI (cr_ui.c cu_scan_keys) holds a root's debounced press until it is there, maps it
 * to the velocity (cr_sense_take: the log map runs in the main loop, so TIMER5's tick has no loop and no divide in
 * it) and clears it. The digital debounce stays the truth for pressed / released; this only adds the velocity.
 * cr_sense_sample and the hooks run in TIMER5's tick; the rest is the main loop's (bytes and halfwords: no locking). */
#define SN_NKEYS 27u
/* the keys with the converter: bit k (0 = F3 .. 19 = C5: rows PA8 and PA7) */
#define CR_SENSE_KEYS 0x000FFFFFu
#define SN_TOUCH 900u
#define SN_PRESS 400u
#define SN_RELEASE 600u
#define SN_WAIT 2u                     /* driven samples after the touch the velocity is read over */
#define SN_DBASE 185u                  /* the driven reading with no pill resistance: the diode and the 595 */
#define SN_FLOOR 450u                  /* a firm landing at DRIVE 2 (velocity 127) */
#define SN_CEIL 896u                   /* the lightest press that seats (velocity 1): Velo's LIGHTEST */
#define SN_DRIVE 2u                    /* fm1_an.drive: the row driven at the datasheet's second-weakest level */
enum { SN_OPEN, SN_TOUCH_ST, SN_DOWN, SN_LIFT };

typedef struct {
    uint16_t v;                        /* the last pulled-up reading */
    uint16_t d, dmin, dland;           /* driven: the last reading, the lowest this press, the landing (the velocity's) */
    uint8_t st, since, up, dnext, dn;  /* state; pulled samples since the touch; samples above RELEASE; drive the next
                                        * sample; driven samples since the touch */
    uint8_t vel;                       /* the last press's velocity */
} sn_key_t;
static volatile sn_key_t sn_key[SN_NKEYS];
static volatile uint16_t cr_sense_land[SN_NKEYS];  /* the landing of the press in progress, once known; the UI clears it */
static volatile uint32_t sn_presses, sn_grazes;    /* console `inp` */

/* log2 of x (1..1023) in Q8: the leading bit plus a 64-entry fraction table */
static const uint8_t SN_LOG2F[65] = {0, 6, 11, 17, 22, 28, 33, 38, 44, 49, 54, 59, 63, 68, 73, 78, 82, 87, 92, 96, 100,
    105, 109, 113, 118, 122, 126, 130, 134, 138, 142, 146, 150, 154, 157, 161, 165, 169, 172, 176, 179, 183, 186, 190,
    193, 197, 200, 203, 207, 210, 213, 216, 220, 223, 226, 229, 232, 235, 238, 241, 244, 247, 250, 253, 255};
static uint32_t sn_log2(uint32_t x)
{
    uint32_t n = 0, f;
    if (x < 1u)
        x = 1u;
    while ((x >> (n + 1u)) != 0u)
        n++;
    f = ((x << 6) >> n) & 63u;                     /* the mantissa's top six bits */
    return (n << 8) + SN_LOG2F[f];
}
/* velocity = 1 + 126 (1 - t)^c at t = i / 64, c = 0.7 x 2^(0.2 k) for Options > Velocity Curve k = -3 .. +3 (the
 * table k + 3; fm1-ctl's Curve, x 2 at +5: here x 1.5 at +3): higher, you play harder to reach 127; lower, softer
 * playing reaches it. Generated: round(1 + 126 (1 - i / 64)^c) */
static const uint8_t SN_CURVES[7][65] = {
    /* -3: 0.462 */ {127, 126, 125, 124, 123, 122, 121, 120, 119, 118, 117, 116, 115, 114, 113, 112, 111, 110, 109, 108, 107, 106, 105, 104, 102, 101, 100, 99, 98, 96, 95, 94, 92, 91, 90, 88, 87, 86, 84, 83, 81, 80, 78, 76, 75, 73, 71, 69, 67, 65, 63, 61, 59, 57, 54, 52, 49, 46, 43, 40, 36, 32, 26, 19, 1},
    /* -2: 0.531 */ {127, 126, 125, 124, 123, 122, 121, 119, 118, 117, 116, 115, 114, 113, 112, 110, 109, 108, 107, 106, 104, 103, 102, 100, 99, 98, 97, 95, 94, 92, 91, 90, 88, 87, 85, 84, 82, 81, 79, 78, 76, 74, 73, 71, 69, 67, 65, 63, 61, 59, 57, 55, 53, 51, 48, 46, 43, 40, 37, 34, 30, 26, 21, 15, 1},
    /* -1: 0.609 */ {127, 126, 125, 123, 122, 121, 120, 118, 117, 116, 115, 113, 112, 111, 109, 108, 107, 105, 104, 103, 101, 100, 98, 97, 96, 94, 93, 91, 90, 88, 87, 85, 84, 82, 80, 79, 77, 75, 74, 72, 70, 69, 67, 65, 63, 61, 59, 57, 55, 53, 51, 49, 46, 44, 42, 39, 36, 34, 31, 28, 24, 21, 16, 11, 1},
    /* +0: 0.700 */ {127, 126, 124, 123, 121, 120, 119, 117, 116, 114, 113, 111, 110, 108, 107, 106, 104, 103, 101, 99, 98, 96, 95, 93, 92, 90, 88, 87, 85, 84, 82, 80, 79, 77, 75, 73, 72, 70, 68, 66, 64, 63, 61, 59, 57, 55, 53, 51, 49, 47, 44, 42, 40, 38, 35, 33, 30, 28, 25, 22, 19, 16, 12, 8, 1},
    /* +1: 0.804 */ {127, 125, 124, 122, 121, 119, 117, 116, 114, 113, 111, 109, 108, 106, 104, 103, 101, 99, 98, 96, 94, 93, 91, 89, 87, 86, 84, 82, 80, 79, 77, 75, 73, 71, 70, 68, 66, 64, 62, 60, 58, 56, 54, 52, 50, 48, 46, 44, 42, 40, 38, 36, 34, 32, 29, 27, 25, 22, 20, 17, 15, 12, 9, 5, 1},
    /* +2: 0.924 */ {127, 125, 123, 122, 120, 118, 116, 114, 112, 111, 109, 107, 105, 103, 101, 99, 98, 96, 94, 92, 90, 88, 86, 85, 83, 81, 79, 77, 75, 73, 71, 69, 67, 66, 64, 62, 60, 58, 56, 54, 52, 50, 48, 46, 44, 42, 40, 38, 36, 34, 32, 30, 28, 26, 24, 22, 19, 17, 15, 13, 11, 8, 6, 4, 1},
    /* +3: 1.061 */ {127, 125, 123, 121, 119, 117, 115, 112, 110, 108, 106, 104, 102, 100, 98, 96, 94, 92, 90, 88, 86, 84, 82, 80, 78, 75, 73, 71, 69, 67, 65, 63, 61, 59, 57, 55, 53, 51, 49, 47, 46, 44, 42, 40, 38, 36, 34, 32, 30, 28, 26, 24, 22, 20, 19, 17, 15, 13, 11, 9, 8, 6, 4, 3, 1},
};
static volatile uint8_t cr_touch_curve = 3;        /* Options > Velocity Curve + 3 (the UI writes it) */
static uint8_t sn_velocity(uint32_t d)
{
    uint32_t l0 = sn_log2(SN_FLOOR - SN_DBASE), l1 = sn_log2(SN_CEIL - SN_DBASE), l, i;
    if (d <= SN_FLOOR)
        return 127;
    if (d >= SN_CEIL)
        return 1;
    l = sn_log2(d - SN_DBASE);
    i = ((l - l0) * 64u + (l1 - l0) / 2u) / (l1 - l0);
    return SN_CURVES[cr_touch_curve > 6u ? 3u : cr_touch_curve][i > 64u ? 64u : i];
}

static void sn_driven(volatile sn_key_t *k, uint32_t v)
{
    k->dnext = 0;
    if (k->st == SN_OPEN)                          /* released meanwhile */
        return;
    k->d = (uint16_t)v;
    if (k->st == SN_TOUCH_ST && k->dn < 255u)
        k->dn++;
    if (k->st != SN_LIFT && v < k->dmin)
        k->dmin = (uint16_t)v;
}

static void cr_sense_sample(uint32_t key, uint32_t v, uint32_t driven)
{
    volatile sn_key_t *k;
    if (key >= SN_NKEYS)
        return;
    k = &sn_key[key];
    if (driven) {
        sn_driven(k, v);
        return;
    }
    k->v = (uint16_t)v;
    if (k->st == SN_OPEN && v < SN_TOUCH) {
        k->st = SN_TOUCH_ST;
        k->since = 0;
        k->dn = 0;
        k->dmin = 1023;
    }
    if (k->st == SN_TOUCH_ST) {
        if (v >= SN_TOUCH) {                       /* gone before the velocity sample: a graze */
            k->st = SN_OPEN;
            sn_grazes++;
        } else if (k->dn >= SN_WAIT && v < SN_PRESS) {   /* seated: the landing is the lowest driven reading since */
            k->st = SN_DOWN;
            k->dland = k->dmin;
            cr_sense_land[key] = (uint16_t)(k->dmin ? k->dmin : 1u);
            sn_presses++;
        } else if (k->since < 255u) {
            k->since++;
        }
    } else if (k->st == SN_DOWN) {
        if (v >= SN_PRESS) {
            k->st = SN_LIFT;
            k->up = 0;
        }
    }
    if (k->st == SN_LIFT) {
        if (v < SN_PRESS)
            k->st = SN_DOWN;
        else if (v < SN_RELEASE)
            k->up = 0;
        else if (++k->up >= 2u)
            k->st = SN_OPEN;
    }
    k->dnext = (uint8_t)(k->st != SN_OPEN);        /* a held key: the next sample driven */
}

/* main loop: the velocity of key k's press in progress, 0 while its landing is not known yet (the UI clears the landing
 * when it takes the velocity) */
static uint32_t cr_sense_take(uint32_t k)
{
    uint32_t d = k < SN_NKEYS ? cr_sense_land[k] : 0u;
    if (!d)
        return 0;
    sn_key[k].vel = sn_velocity(d);
    return sn_key[k].vel;
}

#ifndef CR_SENSE_NO_HOOKS                        /* (the emulator feeds cr_sense_sample itself: tools/emu/emu_fw.c) */
/* the tick's hooks (hal/fm1_input.h): the key at (col, rowbit) */
static void cr_sense_hook(uint32_t col, uint32_t rowbit, uint32_t v, uint32_t driven)
{
    int id = FM1_KEYMAP[rowbit][col];
    if (id >= 14)
        cr_sense_sample((uint32_t)id - 14u, v, driven);
}
static int cr_sense_drive_hook(uint32_t col, uint32_t rowbit)
{
    int id = FM1_KEYMAP[rowbit][col];
    return id >= 14 && sn_key[id - 14].dnext;
}
static int cr_sense_led_ok_hook(uint32_t col, uint32_t rowbit)   /* the key is open: its column's LEDs may be lit */
{
    int id = FM1_KEYMAP[rowbit][col];
    return cr_touch_mode == CR_TOUCH_FIXED || id < 14 || sn_key[id - 14].st == SN_OPEN;
}
#endif

/* Aftertouch (MONK-1's ui.c pressure(), in integers): pressing harder than the key RESTS. A key goes on settling
 * after it lands, so each held key's rest follows its driven reading down for SN_P_SETTLE ms, then creeps after it,
 * and comes up at once when the key is eased. The lean is the pill's resistance below its rest in octaves (Q8:
 * 256 = one octave; the reading less SN_DBASE is the resistance): SN_P_DEAD is noise, SN_P_SPAN (Aftertouch Sens:
 * Low 0.63, Mid 0.46, High 0.29 octave) is 127. Smoothed over ~30 ms. cr_sense_pressure_poll runs in the main loop
 * every 5 ms (cr_ui.c cu_touch_poll) and leaves each held key's pressure in cr_sense_at[]. */
#define SN_P_SETTLE 250u
#define SN_P_DEAD 10u
static const uint16_t SN_P_SPAN[3] = {118, 161, 74};   /* Mid, Low, High */
static uint16_t sn_p_base16[SN_NKEYS];                 /* the rest, x16 */
static uint16_t sn_p_s[SN_NKEYS];                      /* the smoothed pressure, Q8 */
static uint32_t sn_p_t0[SN_NKEYS];
static uint8_t sn_p_on[SN_NKEYS];
static volatile uint8_t cr_sense_at[SN_NKEYS];         /* each key's pressure 0..127 (0 when up) */
static void cr_sense_pressure_poll(uint32_t now_ms, uint32_t sens)
{
    uint32_t k, span = SN_P_SPAN[sens > 2u ? 0u : sens];
    for (k = 0; k < SN_NKEYS; k++) {
        uint32_t d, d16, lean, lb, ld, x;
        if (!((CR_SENSE_KEYS >> k) & 1u))
            continue;
        if (sn_key[k].st != SN_DOWN || !sn_key[k].dn) {
            sn_p_on[k] = 0;
            sn_p_s[k] = 0;
            cr_sense_at[k] = 0;
            continue;
        }
        d = sn_key[k].d > SN_DBASE + 1u ? sn_key[k].d - SN_DBASE : 1u;
        d16 = d << 4;
        if (!sn_p_on[k]) {
            sn_p_on[k] = 1;
            sn_p_base16[k] = (uint16_t)d16;
            sn_p_t0[k] = now_ms;
        }
        if (d16 > sn_p_base16[k]) {
            sn_p_base16[k] = (uint16_t)d16;                /* eased off: that is the rest now */
        } else if (now_ms - sn_p_t0[k] < SN_P_SETTLE) {
            sn_p_base16[k] = (uint16_t)(sn_p_base16[k] - (sn_p_base16[k] - d16) / 4u);
        } else if (sn_p_base16[k] > d16) {
            uint32_t step = (sn_p_base16[k] - d16) >> 10;
            sn_p_base16[k] = (uint16_t)(sn_p_base16[k] - (step ? step : 1u));
        }
        lb = sn_log2(sn_p_base16[k] >> 4);
        ld = sn_log2(d);
        lean = lb > ld ? lb - ld : 0u;
        x = lean > SN_P_DEAD ? ((lean - SN_P_DEAD) * 127u + span / 2u) / span : 0u;
        if (x > 127u)
            x = 127u;
        sn_p_s[k] = (uint16_t)(sn_p_s[k] + (((int32_t)(x << 8) - (int32_t)sn_p_s[k]) * 11) / 64);
        cr_sense_at[k] = (uint8_t)((sn_p_s[k] + 128u) >> 8);
    }
}
