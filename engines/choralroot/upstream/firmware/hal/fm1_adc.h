/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * The non-blocking reads and the CON word: Velo (fm1-velocity) / MONK-1 (fm1-monk) by Charles Vestal, GPL-3.0-only */
/* FM-1 SARADC: PB6 = MASTER pot (ch4), PB1 = battery divider (ch3), and two of the key matrix rows: PA7 (ch0) and
 * PA8 (ch1), the carbon pills of 20 note keys (docs/VELOCITY.md). 10 bit.
 *
 *   fm1_adc_init()               the pot and battery pins analog inputs, no pulls
 *   fm1_adc_read(ch)             blocking, 0..1023, -1 on timeout (the SDK's clock settings): before TIMER5 starts
 *   fm1_adc_start(ch, con)       start one conversion; con = FM1_ADC_CON(div, delay)
 *   fm1_adc_done()               the conversion finished
 *   fm1_adc_take()               the result, and the converter idle again
 *   fm1_adc_abort()              give up on a conversion
 *
 * One converter: once the scan runs (fm1_input.h fm1_an.con set) its tick owns it: it samples the rows between two
 * ticks and the pot and the battery in two of the slots, and the main loop reads fm1_an.master / fm1_an.batt
 * instead of calling fm1_adc_read.
 *
 * CON: bits 0-2 the clock divider (the SDK uses 6: "div 96"), bit 3 set, bit 4 enable, bit 5 interrupt enable, bit 6
 * start / clear pending, bit 7 pending, bits 8-11 the channel, bits 12-15 the start delay in units of 8 ADC clocks
 * (the SDK uses 15). Velo measured: only divider 7 returns real values; delay 2 (~60 us) is clean and fits a tick,
 * delay 0 leaves the sampling capacitor short, delay 4 overruns the tick. */
#pragma once
#include <stdint.h>
#include "fm1_gpio.h"

#ifndef FM1_ADC_CON_R                          /* (tests/input_test.c: the converter as memory) */
#define FM1_ADC_CON_R (*(volatile uint32_t *)0x13100u)
#define FM1_ADC_RES (*(volatile uint32_t *)0x13104u)
#define FM1_WLA_CON0 (*(volatile uint32_t *)0x11900u)
#endif
enum { FM1_ADC_ROW_PA7 = 0, FM1_ADC_ROW_PA8 = 1, FM1_ADC_BATT = 3, FM1_ADC_MASTER = 4 };
#define FM1_ADC_CON(div, delay) ((uint32_t)((div) & 7u) | 8u | ((uint32_t)((delay) & 15u) << 12))
#define FM1_ADC_CON_SDK FM1_ADC_CON(6, 15)
#define FM1_ADC_CON_SCAN FM1_ADC_CON(7, 2)     /* the scan's: fits between two ticks (Velo) */

/* Both are only ever analog inputs: PB1 held low for 8 s would reset the
 * chip (isd_config). */
static void fm1_adc_init(void)
{
    uint32_t m = (1u << 6) | (1u << 1);
    FM1_PR(FM1_PB, FM1_DIE) &= ~m;
    FM1_PR(FM1_PB, FM1_PU) &= ~m;
    FM1_PR(FM1_PB, FM1_PD) &= ~m;
    FM1_PR(FM1_PB, FM1_DIR) |= m;
}

static void fm1_adc_start(uint32_t ch, uint32_t con)
{
    FM1_ADC_CON_R = 0;
    if (FM1_WLA_CON0 & (1u << 14))
        FM1_WLA_CON0 &= ~(1u << 14);
    FM1_ADC_CON_R = (con & 0xF00Fu) | 0x40u | ((ch & 0xFu) << 8);
    FM1_ADC_CON_R |= 0x10u;
    FM1_ADC_CON_R |= 0x40u;
}
static inline int fm1_adc_done(void) { return (FM1_ADC_CON_R & 0x80u) != 0; }
static inline uint32_t fm1_adc_take(void)
{
    uint32_t v = FM1_ADC_RES & 0x3FFu;
    FM1_ADC_CON_R = 0x40u;
    FM1_ADC_CON_R = 0;
    return v;
}
static inline void fm1_adc_abort(void)
{
    FM1_ADC_CON_R = 0x40u;
    FM1_ADC_CON_R = 0;
}

static int32_t fm1_adc_read(uint32_t ch)
{
    uint32_t t;
    fm1_adc_start(ch, FM1_ADC_CON_SDK);
    for (t = 0; t < 20000u; t++)
        if (fm1_adc_done())
            break;
    if (t == 20000u) {
        fm1_adc_abort();
        return -1;
    }
    return (int32_t)fm1_adc_take();
}
