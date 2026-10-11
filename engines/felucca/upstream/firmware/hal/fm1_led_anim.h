/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The power-on LED sweep (Felucca 1.1, Discussion #135): its picture, frame by frame. Pure (no hardware): the
 * driver (hal/fm1_input.h fm1_led_anim_start, from the TIMER5 scan) and the browser emulator (web/emu) show it.
 *
 * A soft light runs over the keys from left to right, a bright head with a fading tail, then the buttons swell up
 * and settle. In frames of the scan (11 ticks of 100 us, 1.1 ms; ~909 a second), FM1_ANIM_FRAMES in all (~0.70 s):
 *   keys      frames 0..FM1_ANIM_SWEEP (~0.56 s): the head from 1.5 half white keys left of F3 to 8 right of G5,
 *             at a steady ~75 half white keys a second (F3 to G5, 30 of them, in ~0.43 s). The keys in the order of
 *             their place on the keyboard (FM1_ANIM_X, in half white keys: a black key between its two whites);
 *   buttons   frames FM1_ANIM_BTN0..FM1_ANIM_FRAMES (~0.39 .. 0.70 s): together, from dark up to a quarter of lit
 *             (FM1_ANIM_BTN_PK, ~0.55 s) and down to the glow, a level the eye sees fall evenly (an exponential).
 * A key's brightness is 2^-e of lit, e = 0.75 per half white key behind the head (the tail: ~59 % of lit one key
 * back, ~35 % two, ~21, ~12, ~7 %, then the glow: 4..6 keys), 2.5 ahead (a soft front over ~2 keys). Levels, /64
 * of lit (FM1_ANIM_FULL): from 4 /64 up (~6 %) lit frames, fewer down to the glow (the glow under them as well);
 * below that the glow (hal/fm1_input.h fm1_led_dim, ~1/30 of lit) down to ~2 % of lit, dark under it. With `glow`
 * (MENU > LEDS DIM HI / DIM LO: the idle glow) the keys the head has passed keep the glow, the buttons settle on it:
 * the last frame is the idle picture's glow; without (LEDS OFF / INV) all goes dark. A frame at or past
 * FM1_ANIM_FRAMES is that last picture. */
#pragma once
#include <stdint.h>

#define FM1_ANIM_FRAMES 640u      /* the whole sweep, frames (~0.70 s) */
#define FM1_ANIM_SWEEP 512u       /* the head's run (a power of 2) */
#define FM1_ANIM_H0 (-24)         /* the head at frame 0 and from FM1_ANIM_SWEEP: 1/16 half white key from F3 */
#define FM1_ANIM_H1 608
#define FM1_ANIM_BTN0 352u        /* the buttons: up from here, the peak, down until FM1_ANIM_FRAMES */
#define FM1_ANIM_BTN_PK 496u
#define FM1_ANIM_FULL 64u         /* a level: lit frames /64 */
#define FM1_ANIM_GLOW 0x80u       /* a level's flag: the glow under it */
#define FM1_ANIM_NBTN 14u         /* stock ids 0..13 buttons, 14..40 the keys F3..G5 */
#define FM1_ANIM_NKEY 27u

/* the 27 keys from F3: their place left to right, in half white keys (a black key half way between its whites) */
static const uint8_t FM1_ANIM_X[FM1_ANIM_NKEY] = {0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 14, 15, 16, 17, 18, 19,
                                                  20, 22, 23, 24, 25, 26, 28, 29, 30};
static const uint16_t FM1_ANIM_POW[16] = {1024, 981, 939, 899, 861, 825, 790, 756,   /* 2^(-i/16) x 1024 */
                                          724, 693, 664, 636, 609, 583, 558, 535};

/* 2^(-e/16) of lit, /1024 (e >= 0) */
static uint32_t fm1_anim_bright(int32_t e)
{
    return e >= 176 ? 0u : (uint32_t)FM1_ANIM_POW[e & 15] >> (e >> 4);
}
/* a brightness /1024 as a level: lit frames /64 with the glow under them; under 4 /64 the glow (from ~2 %, or
 * always with `floor`), else dark */
static uint32_t fm1_anim_q(uint32_t b, uint32_t floor)
{
    uint32_t l = (b + 8u) >> 4;
    if (l >= 4u)
        return (l > FM1_ANIM_FULL ? FM1_ANIM_FULL : l) | FM1_ANIM_GLOW;
    return b >= 20u || floor ? FM1_ANIM_GLOW : 0u;
}
/* LED `id` (stock id: 0..13 a button, 14..40 a key) at frame f: its level (0..64 | FM1_ANIM_GLOW) */
static uint32_t fm1_anim_level(uint32_t f, uint32_t id, uint32_t glow)
{
    int32_t e, d;
    if (f >= FM1_ANIM_FRAMES)
        return glow ? FM1_ANIM_GLOW : 0u;
    if (id < FM1_ANIM_NBTN) {
        if (f < FM1_ANIM_BTN0)
            return 0;
        e = f < FM1_ANIM_BTN_PK ? 96 - (int32_t)(f - FM1_ANIM_BTN0) * 64 / (int32_t)(FM1_ANIM_BTN_PK - FM1_ANIM_BTN0)
                                : 32 + (int32_t)(f - FM1_ANIM_BTN_PK) * 48 / (int32_t)(FM1_ANIM_FRAMES - FM1_ANIM_BTN_PK);
        return fm1_anim_q(fm1_anim_bright(e), glow && f >= FM1_ANIM_BTN_PK);   /* 1/64 .. 1/4 .. 1/32 */
    }
    if (id >= FM1_ANIM_NBTN + FM1_ANIM_NKEY)
        return 0;
    d = FM1_ANIM_H0 + (int32_t)((uint32_t)(FM1_ANIM_H1 - FM1_ANIM_H0) * (f < FM1_ANIM_SWEEP ? f : FM1_ANIM_SWEEP) /
                                FM1_ANIM_SWEEP) - 16 * (int32_t)FM1_ANIM_X[id - FM1_ANIM_NBTN];   /* behind the head */
    e = d >= 0 ? (d * 3) >> 2 : (-d * 5) >> 1;    /* (no division: shifts) */
    return fm1_anim_q(fm1_anim_bright(e), glow && d > 0);
}

/* The idle animation (1.2, Discussion #135; MENU > ANIM IDLE, src/ui_input.c idle_leds): while the FM-1 sits idle a
 * soft light drifts over the keys from F3 to G5 and back, and the buttons breathe with it. Endless, in cycles of
 * FM1_IDLE_FRAMES frames (~9.0 s): the light's place eases from 0 to 30 half white keys in the first half and back in
 * the second (a smoothstep of a triangle, as the breath: it slows down at either end, ~4.5 s a way); a key's
 * brightness 2^-(e / 16) of lit, e = 24 + 16 per half white key from the light (~35 % of lit on it, half that a half
 * white key away: two or three keys softly lit, never a bright one). The buttons, all together, from 1/64 of lit (the
 * light at F3) up to 1/8 (at G5) and down again. With `glow` (MENU > LEDS DIM HI / DIM LO) every LED keeps the glow
 * under it; without (OFF / INV) the rest is dark. The same levels as the power-on sweep (fm1_anim_q: lit frames /64,
 * FM1_ANIM_GLOW): the scan draws them the same way. No division: shifts */
#define FM1_IDLE_FRAMES 8192u    /* a cycle (a power of 2), frames (~9.0 s) */
#define FM1_IDLE_E0 24           /* the light: 2^-1.5 of lit at its place */
static uint32_t fm1_idle_level(uint32_t f, uint32_t id, uint32_t glow)
{
    uint32_t x = f & (FM1_IDLE_FRAMES - 1u);
    int32_t d;
    x = (x & (FM1_IDLE_FRAMES / 2u) ? FM1_IDLE_FRAMES - 1u - x : x) >> 4;   /* a triangle 0..255 */
    x = x * x * (768u - 2u * x) >> 16;                 /* smoothstep, 0..255 */
    if (id < FM1_ANIM_NBTN)
        return fm1_anim_q(fm1_anim_bright(96 - (int32_t)(x * 48u >> 8)), glow);   /* 1/64 .. 1/8 */
    if (id >= FM1_ANIM_NBTN + FM1_ANIM_NKEY)
        return 0;
    d = (int32_t)(x * 480u >> 8) - 16 * (int32_t)FM1_ANIM_X[id - FM1_ANIM_NBTN];   /* 1/16 half white keys */
    return fm1_anim_q(fm1_anim_bright(FM1_IDLE_E0 + (d < 0 ? -d : d)), glow);
}
