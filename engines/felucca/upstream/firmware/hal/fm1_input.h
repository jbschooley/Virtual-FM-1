/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM-1 input HAL: key/button/encoder matrix and LEDs.
 *
 * One 11-column x 6-row diode matrix behind a 2x74HC595 chain (PA4 SER, PA3
 * SRCLK, PA1 RCLK), bit-banged and polled; rows PA0, PA5..PA8, PB7 with
 * pull-ups (low = closed). LED lines PH6/PH9/PA9/PA10 light the LED on the
 * same column, row PA7/PA8/PA5/PA6 respectively.
 *
 *   fm1_input_init();
 *   polled:  for (;;) { fm1_input_scan(); ... }
 *   IRQ:     call fm1_input_tick() from a ~10 kHz timer ISR; it advances one
 *            column per call (stock-style pipeline: rows are sampled one tick
 *            after the column was latched, the LEDs stay lit in between), debounces
 *            that column's keys at once and the encoders every FM1_NCOL ticks
 *            (a frame). The main loop then only
 *            reads fm1_in.notes / buttons and takes edges/steps with
 *            fm1_input_edges() / fm1_enc_take(), which are IRQ-safe.
 *
 * fm1_input_scan() runs one full frame (11 columns, ~0.6 ms) and calls
 * FM1_INPUT_IDLE() while it waits.
 * Keys/buttons: asymmetric debounce. A press counts after FM1_DEB_PRESS frames closed in a row
 * (1.1-2.2 ms from the contact with the TIMER5 scan): the matrix has diodes and no ghosting, so a closed sample is a
 * closed key; two in a row keep one stray sample from playing a note. A release needs
 * FM1_DEB_RELEASE frames open in a row (~9 ms): a contact bouncing open on the way down, or
 * chattering on the way up, never ends the note early or plays it twice. A key still bouncing
 * when it is let go (open, closed, open..) holds its note until it has been open for that long.
 * Encoders: stock quadrature decoder (tables 0x2814/0x4182, sign flipped so
 * + = clockwise on the hardware), plus detent counting. An FM-1
 * detent is one full quadrature cycle (4 transitions, M0g) and the knob rests in
 * one state: the one seen at power-on (relearned after FM1_REST_FRAMES still
 * elsewhere). Steps are emitted on arriving back at it, the net transitions
 * rounded to whole cycles (>= 2 counts one: a lost transition or two is
 * forgiven; a two-state jump counts on in the direction of travel, from the
 * detent the way of a click in the last FM1_ENC_GO frames). So one click = one
 * step, bounce and back-and-forth cancel out. Each frame's state counts (#126:
 * the stock filter took a state only when two frames in a row saw it, and a
 * turn faster than ~110 clicks/s, ~60 with the transitions bunched, lost whole
 * clicks; a flick counted next to nothing); counted up to ~300 clicks/s.
 * fm1_enc_take() returns the steps.
 * LEDs: set fm1_led[col] (packed row bits, bit1 PA5..bit4 PA8); they are lit
 * while that column is selected. fm1_led_key/btn helpers address them by id.
 * A dim glow (fm1_input_tick only): fm1_led_dim[col] are lit for a short pulse at the end of their column's
 * tick, on every frame (~910 Hz, no flicker): ~1/30 of a lit LED (~95 us) at FM1_LED_DIM_NS 3.2 us (the eye
 * is logarithmic: 1/4 and 1/6 read as nearly lit, #35). No wait: the pulse rides on the 595 shift of the next
 * column. Its 16 bits go out while the 595 still drives column p (its outputs change only at the latch), so
 * the lines go lit | dim of p before the shift and dark after its first fm1__dim_k bits, then the rest
 * shifts and latches with the lines dark as before (the read-modify-write edges, 04d7180): nothing reaches
 * another column, and the key read (before it, the lines dark) is unchanged. The shift is the same code on
 * every column, so the pulse is the same width on each; TIMER4 measures it on every pulse and fm1__dim_k
 * follows FM1_LED_DIM_NS (one bit up or down a tick: the widths straddle the target by a bit's time). Only
 * if the whole shift were shorter than the pulse would the rest be waited (console `inp`: dim_pulse_ns,
 * dim_bits). An LED in both is fully lit. FM1_LED_DIM_DIV > 1 also skips frames (keep >= 200 Hz).
 * Two glows (MENU > LEDS): fm1_led_dim_level(0) FM1_LED_DIM_NS (DIM HI, the default), (1) FM1_LED_DIM_LO_NS
 * (DIM LO, ~1/60). The tick reads the target from fm1__dim_t (TIMER4 ticks, set here only, never divided):
 * both are shorter than the shift (~3-5 us measured, dim_pulse_ns 3125 at 14 of 16 bits), so neither waits.
 * Levels: a column has one pulse, so per LED there are three: dark, the glow, lit; the pulse cannot grow towards
 * lit without a wait in the ISR (~95 us). But a second set can end inside the pulse, and a frame can be lit or
 * not: a breath (#119). fm1_led_breath[col] breathe, dark .. ~60 % of lit and back, all in step
 * (FM1_LED_BREATH_FRAMES ~1.1 s). The frame's level B (of a lit LED, /65536) is a smoothstep squared x the peak
 * (FM1_LED_BREATH_PK, /256; DIM LO FM1_LED_BREATH_PK_LO ~30 %: fm1_led_dim_level sets both). Two ranges, one curve:
 *   B <= the glow G (the pulse's share of a lit frame, FM1_LED_DIM_NS / FM1_LED_TICK_US, ~1/31): lit with the
 *     pulse and dark again after the first kb of its k bits (one more line write in the shift), kb = k x B / G,
 *     dithered over 8 frames to 1/8 bit (~114 Hz, a ripple of one bit, ~1/400 of lit);
 *   B > G: whole frames lit as a lit LED (its tick and its pulse), the others the full glow; which ones a first
 *     order sigma-delta picks (one accumulator for all, lit share s = (B - G) / (1 - G)): the lit frames as evenly
 *     spread as can be, a dark run between two at most ~1 / s frames (input_test: at the peak 1 frame; over half
 *     the peak <= 3 frames, ~300 Hz and up; over a quarter <= 8, ~100 Hz). Only the bottom of the range, just over
 *     the glow (s < ~0.1, ~0.1 s of each fade, ~0.15 s with DIM LO), is slower than ~100 Hz: a lit frame every
 *     10..40 frames over the glow. No finer step exists without a wait: a frame of a column is lit or not.
 * Both from constants: no division in the ISR. While any LED breathes every pulse has that write (the same width
 * on every column; fm1__dim_k measures it with the rest); with none the tick is as before (the same writes). A
 * breath starting from none starts at its peak (shown at once). An LED lit or in the glow does not breathe.
 * Cost: the level once a frame (11 bytes ORed, a few multiplies, an add), a multiply and one line write a pulse,
 * an AND / OR on the two lit writes.
 * A steady mid level (1.1, the DRUM grid's beats): fm1_led_mid[col] are lit as a lit LED on 1 frame in N, in step
 * (fm1__mid_ph, counted when column 0 comes round), and are dark in the others unless also in fm1_led_dim (the glow
 * then: the caller sets both, ui_input.c ui_leds). N = FM1_LED_MID_N (8: ~12.5 % + the glow ~3 %, a lit frame every
 * ~8.8 ms, ~114 Hz) or with DIM LO FM1_LED_MID_N_LO (12: ~8 % + ~1.6 %, ~76 Hz); fm1_led_dim_level sets it. No breath,
 * no sigma-delta: a fixed frame count, the same frames for every column. An LED lit does not need it; one breathing
 * and mid is lit on the mid frames as well.
 * The power-on sweep (1.1, hal/fm1_led_anim.h: a light running over the keys, then the buttons): a level per LED,
 * 0..64 /64 of lit, as whole lit frames, each LED its own first order sigma-delta (lit when its sum passes 64: the
 * lit frames as evenly spread as can be, a dark run at most 64 / level frames, <= 16 (~18 ms, ~57 Hz) from the
 * lowest, 4 /64), the glow under them (an LED in both is lit) and the glow alone for the faint end. fm1_led_anim_start
 * (main.c, at boot) hands it fm1_led and fm1_led_dim; once a frame the tick that latched column 0 writes them for
 * the next frame (~41 levels: a table, a multiply, a shift each) after it lit column 0, and lights column 0 again
 * with them; after FM1_ANIM_FRAMES (~0.70 s), or at once when a key or a button is down, the last picture (the idle
 * glow or dark) stays and fm1_led_anim_on() goes 0: the UI writes the LEDs from then (ui_input.c ui_leds). Idle:
 * one byte tested a frame (in the branch the frame end already takes), the writes as before.
 */
#pragma once
#include <stdint.h>
#include "fm1_time.h"
#include "fm1_gpio.h"
#include "fm1_led_anim.h"

#ifndef FM1_INPUT_IDLE
#define FM1_INPUT_IDLE() ((void)0)
#endif
#ifndef FM1_INPUT_NOW
#define FM1_INPUT_NOW() fm1_ticks()   /* the press stats' clock (input_test.c: simulated) */
#endif
#ifndef FM1_LED_US
#define FM1_LED_US 40u           /* LED on-time per column (brightness vs scan rate) */
#endif
#define FM1_DEB_PRESS 2u          /* frames closed in a row: a press (a frame = 11 ticks, ~1.1 ms) */
#define FM1_DEB_RELEASE 8u        /* frames open in a row: a release (~9 ms) */
#define FM1_INPUT_LAT 1           /* the press latency stats below (seq.c, console `inp`) */
#define FM1_SETTLE_US 10u
#ifndef FM1_LED_DIM_NS
#define FM1_LED_DIM_NS 3200u      /* a dim LED's pulse per frame (ns; a lit one ~95 us): ~1/30 the brightness */
#endif
#ifndef FM1_LED_DIM_LO_NS
#define FM1_LED_DIM_LO_NS 1600u   /* the darker glow (MENU > LEDS DIM LO): ~1/60 */
#endif
#ifndef FM1_LED_DIM_DIV
#define FM1_LED_DIM_DIV 1u        /* a dim LED: the pulse on 1 frame in DIV (1: every frame, ~910 Hz) */
#endif
#define FM1_LED_BREATH_FRAMES 1024u   /* a breath: frames dark -> the peak -> dark (~1.13 s; a power of 2) */
#ifndef FM1_LED_BREATH_PK
#define FM1_LED_BREATH_PK 154u        /* the breath's peak, /256 of lit: ~60 % (DIM HI, OFF, INV) */
#endif
#ifndef FM1_LED_BREATH_PK_LO
#define FM1_LED_BREATH_PK_LO 77u      /* with DIM LO: ~30 % */
#endif
#ifndef FM1_LED_MID_N
#define FM1_LED_MID_N 8u              /* the steady mid level: lit 1 frame in N (DIM HI, OFF, INV) */
#endif
#ifndef FM1_LED_MID_N_LO
#define FM1_LED_MID_N_LO 12u          /* with DIM LO */
#endif
#ifndef FM1_LED_TICK_US
#define FM1_LED_TICK_US 100u          /* fm1_input_tick's period (TIMER5, 10 kHz): a lit LED's time a frame */
#endif
#ifndef FM1_LED_TRACE
#define FM1_LED_TRACE(rowmask) ((void)0)   /* input_test.c: every write of the LED lines */
#endif
#ifndef FM1_SR_LATCH_TRACE
#define FM1_SR_LATCH_TRACE() ((void)0)     /* input_test.c: the 595 latch */
#endif
#define FM1_REST_FRAMES 900u      /* ~1 s still off the detent state: that is the detent (power-on) */
#define FM1_ENC_GO 20u            /* a 2-state jump from the detent goes on the way of a click this recent (~22 ms) */
#define FM1_NCOL 11u
#define FM1_NKEY 41u              /* ids: 0..13 buttons, 14..40 note keys */
#define FM1_NENC 7u


/* key id at (physical column, packed row bit), -1 = none */
static const int8_t FM1_KEYMAP[6][FM1_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},          /* PA0: encoders */
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},          /* PA5 */
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},          /* PA6 */
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},          /* PA7 */
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},          /* PA8 */
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},          /* PB7: encoder 6 */
};
/* encoder i: A at (col, row bit), B at (col, row bit) */
static const uint8_t FM1_ENC[FM1_NENC][4] = {
    {0, 0, 1, 0}, {2, 0, 3, 0}, {8, 1, 9, 1}, {8, 0, 9, 0}, {6, 0, 7, 0}, {4, 0, 5, 0}, {0, 5, 1, 5},
};
enum { FM1_BTN_OCT_DOWN = 0, FM1_BTN_OCT_UP = 1 };


static volatile struct {
    uint32_t notes;              /* debounced: bit n = note key n (0 = F3 .. 26 = G5) */
    uint32_t buttons;            /* debounced: bit i = button i (0..13) */
    uint32_t pressed, released;  /* button edges since the last fm1_input_edges() */
    uint32_t notes_pressed;      /* note-key press edges since the last fm1_input_note_edges() */
    uint8_t raw[FM1_NCOL];       /* last frame, packed rows, 1 = closed */
    uint8_t cnt[FM1_NKEY];       /* frames in a row against the debounced state */
    uint32_t note_t0[27];        /* TIMER4 tick of the first scan that saw note key n closed */
    uint8_t enc_prev[FM1_NENC], enc_last[FM1_NENC];
    uint8_t enc_rest[FM1_NENC];  /* the detent state (0..3) */
    uint16_t enc_still[FM1_NENC]; /* frames since the last state change */
    int8_t enc_sub[FM1_NENC];    /* net transitions since the last rest state */
    int16_t enc_steps[FM1_NENC]; /* + = clockwise */
    int8_t enc_dir[FM1_NENC];    /* the last click's direction, and its frame */
    uint32_t enc_dfr[FM1_NENC];
    uint32_t frames;
} fm1_in;
static uint8_t fm1_led[FM1_NCOL];
static uint8_t fm1_led_dim[FM1_NCOL];
static uint8_t fm1_led_breath[FM1_NCOL];   /* breathing LEDs (see top): dark .. ~60 % of lit, in step */
static uint8_t fm1_led_mid[FM1_NCOL];      /* a steady mid level (see top): lit 1 frame in FM1_LED_MID_N */

/* scan diagnostics (console `inp`, read and cleared by the main loop): the gap between ticks
 * = the on-time of the column lit in it, in TIMER4 ticks (24 MHz) */
#define FM1_GAP_BINS 8u                /* < 0.15, 0.3, 0.6, 1.2, 2.4, 4.8, 9.6 ms, longer */
static volatile struct {
    uint32_t last, gap_max, gap_hist[FM1_GAP_BINS];
    uint32_t on[FM1_NCOL], on_max[FM1_NCOL];
    uint32_t cost_sum, cost_max, ticks;
    uint32_t enc_moves[FM1_NENC], enc_lost[FM1_NENC];   /* accepted transitions, 2-state jumps */
    uint32_t press_n, press_sum, press_max;   /* note keys: first closed scan -> debounced press (ticks) */
    /* seq.c keyboard_block, from the same first closed scan: to the note-on in the render, and to
     * the note's first sample leaving the I2S DMA (the half it renders starts one half later) */
    uint32_t kb_n, kb_sum, kb_max, dac_sum, dac_max;
    uint32_t dim_n, dim_sum;     /* the dim pulses and their widths (TIMER4 ticks) */
} fm1_in_stat;

static void fm1__led_lines(uint32_t rowmask)       /* row bit 1 PA9, 2 PA10, 3 PH6, 4 PH9 */
{
    FM1_LED_TRACE(rowmask);
    uint32_t a = FM1_PR(FM1_PA, FM1_OUT) & ~((1u << 9) | (1u << 10));
    uint32_t h = FM1_PR(FM1_PH, FM1_OUT) & ~((1u << 6) | (1u << 9));
    FM1_PR(FM1_PA, FM1_OUT) = a | (rowmask & 2u) << 8 | (rowmask & 4u) << 8;
    FM1_PR(FM1_PH, FM1_OUT) = h | (rowmask & 8u) << 3 | (rowmask & 16u) << 5;
}

/* bits i0 .. i1-1 of w (msb first) into the 595; the outputs stay as they are until fm1__sr_latch */
static void fm1__sr_bits(uint32_t w, uint32_t i0, uint32_t i1)
{
    uint32_t i;
    /* read-modify-write per edge on purpose: write-only edges were too short for the
     * 595 on the board and latched the neighbouring column (LEDs and keys copied one column over) */
    for (i = i0; i < i1; i++) {
        if (w & (0x8000u >> i))
            FM1_PR(FM1_PA, FM1_OUT) |= 1u << 4;
        else
            FM1_PR(FM1_PA, FM1_OUT) &= ~(1u << 4);
        FM1_PR(FM1_PA, FM1_OUT) |= 1u << 3;
        FM1_PR(FM1_PA, FM1_OUT) &= ~(1u << 3);
    }
}
static void fm1__sr_latch(void)
{
    FM1_SR_LATCH_TRACE();
    FM1_PR(FM1_PA, FM1_OUT) |= 1u << 1;
    FM1_PR(FM1_PA, FM1_OUT) &= ~(1u << 1);
}
static void fm1__sr_word(uint32_t w)
{
    fm1__sr_bits(w, 0, 16u);
    fm1__sr_latch();
}

static uint32_t fm1__rows(void)
{
    uint32_t a = FM1_PR(FM1_PA, FM1_IN), b = FM1_PR(FM1_PB, FM1_IN);
    return (~((a & 1u) | ((a >> 4) & 0x1Eu) | ((b >> 2) & 0x20u))) & 0x3Fu;
}

static void fm1__wait(uint32_t us)
{
    uint32_t t0 = fm1_ticks(), span = us * FM1_TICKS_PER_US;
    while ((uint32_t)(fm1_ticks() - t0) < span)
        FM1_INPUT_IDLE();
}

static void fm1_input_init(void)
{
    static const uint8_t LEDP[4][2] = {{FM1_PH, 6}, {FM1_PH, 9}, {FM1_PA, 9}, {FM1_PA, 10}};
    const uint32_t rows_a = (1u << 0) | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 8);
    const uint32_t sr = (1u << 1) | (1u << 3) | (1u << 4), row_b = 1u << 7;
    uint32_t i;
    for (i = 0; i < 4u; i++) {
        uint32_t p = LEDP[i][0], m = 1u << LEDP[i][1];
        FM1_PR(p, FM1_DIE) |= m;
        FM1_PR(p, FM1_OUT) &= ~m;
        FM1_PR(p, FM1_DIR) &= ~m;
        FM1_PR(p, FM1_HD0) |= m;
        FM1_PR(p, FM1_HD) |= m;
    }
    FM1_PR(FM1_PA, FM1_DIE) |= rows_a;
    FM1_PR(FM1_PA, FM1_DIR) |= rows_a;
    FM1_PR(FM1_PA, FM1_PD) &= ~rows_a;
    FM1_PR(FM1_PA, FM1_PU) |= rows_a;
    FM1_PR(FM1_PB, FM1_DIE) |= row_b;
    FM1_PR(FM1_PB, FM1_DIR) |= row_b;
    FM1_PR(FM1_PB, FM1_PD) &= ~row_b;
    FM1_PR(FM1_PB, FM1_PU) |= row_b;
    FM1_PR(FM1_PA, FM1_DIE) |= sr;
    FM1_PR(FM1_PA, FM1_PU) &= ~sr;
    FM1_PR(FM1_PA, FM1_PD) &= ~sr;
    FM1_PR(FM1_PA, FM1_OUT) &= ~sr;
    FM1_PR(FM1_PA, FM1_DIR) &= ~sr;
    fm1__sr_word(0xFFFFu);
    for (i = 0; i < FM1_NENC; i++)
        fm1_in.enc_prev[i] = fm1_in.enc_last[i] = 0xFF;   /* seeded by the first frame */
}

static void fm1__key(uint32_t id, uint32_t closed)
{
    volatile uint8_t *c = &fm1_in.cnt[id];
    uint32_t note = id >= 14u, bit = note ? 1u << (id - 14u) : 1u << id;
    uint32_t on = ((note ? fm1_in.notes : fm1_in.buttons) & bit) != 0u;
    if (closed == on) {                            /* agrees with the state: start over */
        *c = 0;
        return;
    }
    if (!on && note && *c == 0u)
        fm1_in.note_t0[id - 14u] = FM1_INPUT_NOW();
    if (++*c < (on ? FM1_DEB_RELEASE : FM1_DEB_PRESS))
        return;
    *c = 0;
    if (note) {
        if (!on) {
            uint32_t d = FM1_INPUT_NOW() - fm1_in.note_t0[id - 14u];
            fm1_in.notes_pressed |= bit;
            fm1_in_stat.press_n++;
            fm1_in_stat.press_sum += d;
            if (d > fm1_in_stat.press_max)
                fm1_in_stat.press_max = d;
        }
        fm1_in.notes ^= bit;
    } else {
        fm1_in.buttons ^= bit;
        if (!on)
            fm1_in.pressed |= bit;
        else
            fm1_in.released |= bit;
    }
}

static void fm1__keys(uint32_t p)                  /* the keys of column p, just read */
{
    uint32_t r, raw = fm1_in.raw[p];
    for (r = 1; r < 5u; r++)
        if (FM1_KEYMAP[r][p] >= 0)
            fm1__key((uint32_t)FM1_KEYMAP[r][p], (raw >> r) & 1u);
}

static void fm1__frame(void);

static void fm1_input_scan(void)
{
    uint32_t p;
    for (p = 0; p < FM1_NCOL; p++) {
        fm1__led_lines(0);
        fm1__sr_word(0xFFFFu ^ (1u << p) ^ (p < 2u ? 1u << (11u + p) : 0u));
        fm1__wait(FM1_SETTLE_US);
        fm1_in.raw[p] = (uint8_t)fm1__rows();
        fm1__keys(p);
        fm1__led_lines(fm1_led[p]);
        fm1__wait(FM1_LED_US);
    }
    fm1__led_lines(0);
    fm1__frame();
}

static void fm1__frame(void)
{
    uint32_t e;
    for (e = 0; e < FM1_NENC; e++) {               /* stock SOFT2 decoder + detents */
        const uint8_t *m = FM1_ENC[e];
        uint32_t cur = ((fm1_in.raw[m[0]] >> m[1]) & 1u) << 1 | ((fm1_in.raw[m[2]] >> m[3]) & 1u);
        uint32_t idx;
        volatile int8_t *sub = &fm1_in.enc_sub[e];
        if (cur != fm1_in.enc_last[e]) {           /* (#126: no 2-sample filter, every frame's state counts) */
            fm1_in.enc_last[e] = (uint8_t)cur;
            fm1_in.enc_still[e] = 0;
        }
        if (fm1_in.enc_prev[e] == 0xFF) {          /* first frame: the knob rests here */
            fm1_in.enc_prev[e] = (uint8_t)cur;
            fm1_in.enc_rest[e] = (uint8_t)cur;
        }
        if (fm1_in.enc_still[e] < 0xFFFFu && ++fm1_in.enc_still[e] == FM1_REST_FRAMES &&
            cur != fm1_in.enc_rest[e]) {
            /* parked a long time off the detent state (held at power-on): that is the detent.
             * Never a second state: a knob held mid-click taught the complement of the detent as
             * one more rest and every click then counted twice (#23); short mid-click pauses of a
             * slow turn taught the mid states and the knob went dead */
            fm1_in.enc_rest[e] = (uint8_t)cur;
            *sub = 0;
        }
        if (cur == fm1_in.enc_prev[e])
            continue;
        idx = (uint32_t)fm1_in.enc_prev[e] << 2 | cur;
        if ((0x4182u >> idx) & 1u) {
            (*sub)++;
            fm1_in_stat.enc_moves[e]++;
        } else if ((0x2814u >> idx) & 1u) {
            (*sub)--;
            fm1_in_stat.enc_moves[e]++;
        } else {                                   /* two states in one sample: a fast turn, the way it */
            int32_t d = *sub > 0 ? 1 : *sub < 0 ? -1 : /* was going (on the detent: a click of the last */
                        (uint32_t)(fm1_in.frames - fm1_in.enc_dfr[e]) <= FM1_ENC_GO ? fm1_in.enc_dir[e] : 0;
            fm1_in_stat.enc_lost[e]++;             /* FM1_ENC_GO frames, else nothing) */
            *sub = (int8_t)(*sub + 2 * d);
        }
        fm1_in.enc_prev[e] = (uint8_t)cur;
        if (*sub > 100 || *sub < -100)
            *sub = 0;                              /* (never off the detent that long) */
        if (cur == fm1_in.enc_rest[e]) {          /* back on the detent: whole cycles, a lost transition */
            int32_t n = *sub < 0 ? -*sub : *sub;   /* or two forgiven (one click = 4 transitions) */
            n = n >= 2 ? (n + 2) / 4 : 0;
            fm1_in.enc_steps[e] = (int16_t)(fm1_in.enc_steps[e] + (*sub < 0 ? -n : n));
            if (n) {
                fm1_in.enc_dir[e] = (int8_t)(*sub < 0 ? -1 : 1);
                fm1_in.enc_dfr[e] = fm1_in.frames;
            }
            *sub = 0;
        }
    }
    fm1_in.frames++;
}

/* one column per call, from a timer ISR (see top) */
#define FM1__DIM_T(ns) (((ns) * FM1_TICKS_PER_US + 500u) / 1000u)   /* a pulse in TIMER4 ticks */
static uint8_t fm1__tick_col, fm1__dim_k = 16u;   /* the bits of the shift the dim pulse spans (0..16) */
static uint16_t fm1__dim_t = FM1__DIM_T(FM1_LED_DIM_NS);   /* the pulse the tick aims at (fm1_led_dim_level) */
/* the breath's constants for each glow (see top): its peak (/256 of lit), the glow G (of a lit frame, /65536),
 * 2^24 / G (the pulse's share: B x it >> 16, 0..256), 2^30 / (65536 - G) (the lit share: (B - G) x it >> 14) */
#define FM1__BR_G(ns) ((ns) * 65536u / (FM1_LED_TICK_US * 1000u * FM1_LED_DIM_DIV))
#define FM1__BR(pk, ns) {(pk), FM1__BR_G(ns), (1u << 24) / FM1__BR_G(ns), (1u << 30) / (65536u - FM1__BR_G(ns))}
static const uint32_t FM1__BR_K[2][4] = {FM1__BR(FM1_LED_BREATH_PK, FM1_LED_DIM_NS),
                                         FM1__BR(FM1_LED_BREATH_PK_LO, FM1_LED_DIM_LO_NS)};
static uint8_t fm1__br_sel;                       /* FM1__BR_K[it]: 0 DIM HI (OFF, INV), 1 DIM LO */
static uint8_t fm1__mid_n = FM1_LED_MID_N, fm1__mid_ph;   /* the mid level: 1 frame lit in fm1__mid_n; this frame's */
/* the glow (main loop, any time; fm1__dim_k follows it in a few frames): 0 FM1_LED_DIM_NS, 1 FM1_LED_DIM_LO_NS;
 * and the breath's peak with it: 0 FM1_LED_BREATH_PK, 1 FM1_LED_BREATH_PK_LO */
static void fm1_led_dim_level(uint32_t lo)
{
    fm1__dim_t = (uint16_t)(lo ? FM1__DIM_T(FM1_LED_DIM_LO_NS) : FM1__DIM_T(FM1_LED_DIM_NS));
    fm1__br_sel = lo != 0u;
    fm1__mid_n = (uint8_t)(lo ? FM1_LED_MID_N_LO : FM1_LED_MID_N);
}
/* column c's lit lines this frame: lit, a breath's lit frame (not dim), the mid level's lit frame */
#define FM1__LIT(c) (fm1_led[c] | (fm1__br_lit ? fm1_led_breath[c] & ~fm1_led_dim[c] : 0u) | \
                     (!fm1__mid_ph ? fm1_led_mid[c] : 0u))
#if FM1_LED_DIM_DIV > 1
static uint8_t fm1__dim_ph;
#endif
static uint16_t fm1__br_ph;                       /* the breath: frames into it (0 and 1023 dark, 512 the peak) */
static uint16_t fm1__br_lv, fm1__br_acc;          /* this frame: the pulse's share 0..256; the sigma-delta */
static uint8_t fm1__br_on, fm1__br_d, fm1__br_lit;   /* this frame: any LED breathing, the dither, lit */
static void fm1__breath_frame(void)               /* (a new frame, before column 0's pulse) */
{
    const uint32_t *q = FM1__BR_K[fm1__br_sel];
    uint32_t c, any = 0, x, d;
    for (c = 0; c < FM1_NCOL; c++)
        any |= fm1_led_breath[c];
    x = !any ? 0u : fm1__br_on ? (fm1__br_ph + 1u) & (FM1_LED_BREATH_FRAMES - 1u) : FM1_LED_BREATH_FRAMES / 2u;
    fm1__br_on = any != 0u;
    fm1__br_ph = (uint16_t)x;
    d = x & 7u;                                    /* the dither: 0..7 bit-reversed, x 32 */
    fm1__br_d = (uint8_t)(((d & 1u) << 7) | ((d & 2u) << 5) | ((d & 4u) << 3));
    x = x * 512u / FM1_LED_BREATH_FRAMES;          /* 0..511 */
    x = x & 256u ? 511u - x : x;                   /* a triangle 0..255 */
    x = x * x * (768u - 2u * x) >> 16;             /* smoothstep, 0..255 */
    x = x * x * q[0] >> 8;                         /* squared (the eye), x the peak: B, of lit /65536 */
    if (x <= q[1]) {                               /* up to the glow: a share of the pulse */
        fm1__br_lv = (uint16_t)(x * q[2] >> 16);
        fm1__br_lit = 0;
    } else {                                       /* over it: lit frames among full glows */
        x = fm1__br_acc + ((x - q[1]) * q[3] >> 14);
        fm1__br_lit = x >= 65536u;
        fm1__br_acc = (uint16_t)x;                 /* (- 65536 when lit) */
        fm1__br_lv = 256u;
    }
}
/* the power-on sweep (hal/fm1_led_anim.h; see top): while it runs (fm1__an_f = its frame + 1) it owns fm1_led and
 * fm1_led_dim (the UI leaves them alone: fm1_led_anim_on), once a frame from the scan, at the end of the tick that
 * lights column 0 (after the lit write: no on-time lost; column 0 written again with the new frame's). Not inlined:
 * the tick's own code (its registers, its stack) stays as it was, a call in a branch never taken once it is over */
/* 1.2: the idle animation (fm1_led_idle_start; fm1__an_idle) runs the same way, its picture fm1_idle_level, endless
 * (the frame wraps at FM1_IDLE_FRAMES): a key or a button down ends it before its frame is drawn (the LEDs as the
 * last frame left them, for the UI to take over), so does fm1_led_anim_stop (the main loop: a knob turned, the
 * transport started) */
static uint16_t fm1__an_f;
static uint8_t fm1__an_glow, fm1__an_idle, fm1__an_cb[FM1_NKEY], fm1__an_acc[FM1_NKEY];   /* each LED: column << 3 | row,
                                                                                            * its sigma-delta */
static __attribute__((noinline)) void fm1__anim_frame(void)
{
    uint32_t f = fm1__an_f - 1u, i, idle = fm1__an_idle;
    uint8_t l[FM1_NCOL] = {0}, d[FM1_NCOL] = {0};
    if (fm1_in.notes | fm1_in.buttons) {          /* a key or a button down: done, the UI's at once */
        if (idle) {
            fm1__an_f = 0;
            return;
        }
        f = FM1_ANIM_FRAMES;
    }
    for (i = 0; i < FM1_NKEY; i++) {
        uint32_t q = idle ? fm1_idle_level(f, i, fm1__an_glow) : fm1_anim_level(f, i, fm1__an_glow);
        uint32_t cb = fm1__an_cb[i], a = fm1__an_acc[i] + (q & 0x7Fu);
        uint32_t m = 1u << (cb & 7u);
        if (a >= FM1_ANIM_FULL) {                  /* lit frames: q of 64, a first order sigma-delta each */
            a -= FM1_ANIM_FULL;
            l[cb >> 3] |= (uint8_t)m;
        }
        if (q & FM1_ANIM_GLOW)
            d[cb >> 3] |= (uint8_t)m;
        fm1__an_acc[i] = (uint8_t)a;
    }
    if (idle)                                      /* (endless: the frame wraps) */
        f = (f + 1u) & (FM1_IDLE_FRAMES - 1u);
    for (i = 0; i < FM1_NCOL; i++) {
        fm1_led_dim[i] = d[i];
        fm1_led[i] = !idle && f >= FM1_ANIM_FRAMES ? 0u : l[i];
    }
    fm1__an_f = (uint16_t)(idle ? f + 1u : f >= FM1_ANIM_FRAMES ? 0u : f + 2u);   /* (the sweep's last picture stays for
                                                                                   * the UI to take over) */
    fm1__led_lines(FM1__LIT(0u));                  /* column 0, lit a moment ago with the last frame's: this one's */
}
/* start the sweep (main loop, before the scan runs or with it): `glow` the idle glow is on (MENU > LEDS DIM HI / LO);
 * idle: the idle animation (fm1_led_idle_start) */
static void fm1__anim_begin(uint32_t glow, uint32_t idle)
{
    uint32_t i, r, c;
    for (i = 0; i < FM1_NKEY; i++)
        for (r = 1; r < 5u; r++)
            for (c = 0; c < FM1_NCOL; c++)
                if (FM1_KEYMAP[r][c] == (int8_t)i)
                    fm1__an_cb[i] = (uint8_t)(c << 3 | r);
    for (i = 0; i < FM1_NKEY; i++)
        fm1__an_acc[i] = FM1_ANIM_FULL / 2u;
    for (c = 0; c < FM1_NCOL; c++)
        fm1_led[c] = fm1_led_dim[c] = fm1_led_breath[c] = fm1_led_mid[c] = 0;
    fm1__an_glow = glow != 0u;
    fm1__an_idle = idle != 0u;
    fm1__an_f = 1;
}
static void fm1_led_anim_start(uint32_t glow) { fm1__anim_begin(glow, 0); }
static int fm1_led_anim_on(void) { return fm1__an_f != 0u; }
/* 1.2: the idle animation (main loop): it has the LEDs as the sweep does (fm1_led_anim_on) until a key or a button
 * goes down or fm1_led_anim_stop. One core: the scan's frame runs whole before or after these */
static void fm1_led_idle_start(uint32_t glow) { fm1__anim_begin(glow, 1); }
static int fm1_led_idle_on(void) { return fm1__an_f != 0u && fm1__an_idle; }
static void fm1_led_anim_stop(void) { fm1__an_f = 0; }   /* (the LEDs as the last frame left them: the UI writes them) */
static void fm1_input_tick(void)
{
    uint32_t p = fm1__tick_col, n = p + 1u == FM1_NCOL ? 0u : p + 1u;
    uint32_t t0 = fm1_ticks(), g = t0 - fm1_in_stat.last, b = 0;
    uint32_t w = 0xFFFFu ^ (1u << n) ^ (n < 2u ? 1u << (11u + n) : 0u), lit, dim = 0, k = fm1__dim_k;
    uint32_t br = 0, kb = 0;
    fm1__led_lines(0);
    fm1_in_stat.last = t0;
    while (b < FM1_GAP_BINS - 1u && g >= (150u * FM1_TICKS_PER_US << b))
        b++;
    fm1_in_stat.gap_hist[b]++;
    if (g > fm1_in_stat.gap_max)
        fm1_in_stat.gap_max = g;
    fm1_in_stat.on[p] += g;
    if (g > fm1_in_stat.on_max[p])
        fm1_in_stat.on_max[p] = g;
    fm1_in.raw[p] = (uint8_t)fm1__rows();          /* column p has been latched one tick (the lines dark) */
    if (p == 0u)
        fm1__breath_frame();
    lit = FM1__LIT(p);                             /* (a lit frame: as lit) */
#if FM1_LED_DIM_DIV > 1
    if (p == 0u)                                   /* a new frame: the dim LEDs' turn on 1 in FM1_LED_DIM_DIV */
        fm1__dim_ph = (uint8_t)(fm1__dim_ph + 1u >= FM1_LED_DIM_DIV ? 0u : fm1__dim_ph + 1u);
    if (!fm1__dim_ph)
#endif
    {
        dim = fm1_led_dim[p] & ~lit;
        if (fm1__br_on) {                          /* breathing: the first kb of the k bits (kb <= k) */
            kb = (k * fm1__br_lv + fm1__br_d) >> 8;
            br = kb ? fm1_led_breath[p] & ~(lit | dim) : 0u;
        }
    }
    if (dim | br) {                                /* the dim pulse of column p: over the first k bits */
        uint32_t t1, d, T = fm1__dim_t;
        t1 = fm1_ticks();                          /* (before the write: d spans one write and the bits) */
        fm1__led_lines(lit | dim | br);
        if (fm1__br_on) {                          /* (every pulse while any LED breathes: the same width) */
            fm1__sr_bits(w, 0, kb);
            fm1__led_lines(lit | dim);             /* (a shift shorter than the pulse waits after: the breath not) */
            fm1__sr_bits(w, kb, k);
        } else {
            fm1__sr_bits(w, 0, k);                 /* (the 595 still drives column p) */
        }
        d = fm1_ticks() - t1;
        while (k == 16u && d < T)                 /* only a shift shorter than the pulse waits */
            d = fm1_ticks() - t1;
        fm1__led_lines(0);
        fm1__dim_k = (uint8_t)(d > T ? (k ? k - 1u : 0u) : d < T && k < 16u ? k + 1u : k);
        fm1_in_stat.dim_n++;
        fm1_in_stat.dim_sum += d;
    } else {
        k = 0;
    }
    fm1__sr_bits(w, k, 16u);
    fm1__sr_latch();                               /* column n, the lines dark */
    if (n == 0u)                                   /* (a new frame: the mid level's turn, before column 0 lights) */
        fm1__mid_ph = (uint8_t)(fm1__mid_ph + 1u >= fm1__mid_n ? 0u : fm1__mid_ph + 1u);
    fm1__led_lines(FM1__LIT(n));
    fm1__tick_col = (uint8_t)n;
    fm1__keys(p);                                  /* its keys now: no wait for the frame's end */
    if (n == 0u) {
        fm1__frame();
        if (fm1__an_f)                             /* the power-on sweep: the next frame's picture */
            fm1__anim_frame();
    }
    g = fm1_ticks() - t0;
    fm1_in_stat.cost_sum += g;
    fm1_in_stat.ticks++;
    if (g > fm1_in_stat.cost_max)
        fm1_in_stat.cost_max = g;
}

/* main-loop critical section against fm1_input_tick (main loop only: it
 * re-enables interrupts unconditionally) */
static inline uint32_t fm1__lock(void)
{
    __asm__ volatile("cli" ::: "memory");
    return 0;
}
static inline void fm1__unlock(uint32_t v)
{
    (void)v;
    __asm__ volatile("csync\n\tsti" ::: "memory");
}

/* detent steps turned since the last call, + = clockwise */
static int32_t fm1_enc_take(uint32_t e)
{
    uint32_t k = fm1__lock();
    int32_t s = fm1_in.enc_steps[e];
    fm1_in.enc_steps[e] = 0;
    fm1__unlock(k);
    return s;
}

static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t k = fm1__lock();
    uint32_t p = fm1_in.pressed;
    if (released)
        *released = fm1_in.released;
    fm1_in.pressed = fm1_in.released = 0;
    fm1__unlock(k);
    return p;
}

static uint32_t fm1_input_note_edges(void)
{
    uint32_t k = fm1__lock();
    uint32_t p = fm1_in.notes_pressed;
    fm1_in.notes_pressed = 0;
    fm1__unlock(k);
    return p;
}

/* LED of key id (button 0..13 or note key 14..40) */
static void fm1_led_key(uint32_t id, int on)
{
    uint32_t p, r;
    for (p = 0; p < FM1_NCOL; p++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][p] == (int8_t)id) {
                if (on)
                    fm1_led[p] |= (uint8_t)(1u << r);
                else
                    fm1_led[p] &= (uint8_t)~(1u << r);
            }
}
