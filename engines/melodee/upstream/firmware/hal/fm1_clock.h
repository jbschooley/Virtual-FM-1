/* SPDX-License-Identifier: GPL-3.0-only */
/* The SYS clock, read only. Melodee never writes the PLL: it runs at the clock the boot loader (SPL) sets.
 * Live FM-1 readings in PR #27 (@johnskiller, with @charlesvestal's register reads) identify PLL2_CON1 (0x119AC) as
 * the SYS VCO multiplier: FBDS = multiplier - 2. The SPL's 268 means 24 MHz / 12 * 270 = 540 MHz, with CLK_CON2
 * selecting 2/3 = 360 (HSB 180, LSB 60, SFC 90).
 * 1.3.0 to 1.3.3 (and 1.4.0-rc.1/2) offered a boot-time retune to 320 MHz (multiplier 240, OVERCLOCK). On some FM-1s that retune
 * failed before the LCD and USB came up and the boot guard entered UBOOT; it is gone. A soft reset keeps the PLL, so
 * after an update from such a session the FM-1 can still run at 320 MHz (or at an unreleased test's 396, multiplier
 * 297) until its next power-off: fm1_clock_mult reads which, for the voice budget (main.c). */
#pragma once
#include <stdint.h>
#ifndef FM1_CLOCK_HOST_TEST
#include "fm1_flash.h"                         /* (RAMFN, FL_FAR: the files after this one have them from here) */
#define FCLK_READ(a) (*(volatile uint32_t *)(uintptr_t)(a))
#endif

#define FCLK_PLL2_CON1 0x119ACu

#define FCLK_MULT_320 240u
#define FCLK_MULT_360 270u
#define FCLK_MULT_396 297u
/* the SYS clock of a multiplier: 2 MHz a step, 2/3 of the VCO */
static inline uint32_t fm1_clock_mhz_of(uint32_t mult) { return mult * 4u / 3u; }

/* The multiplier running now (FCLK_MULT_*), 0 for one this file does not know */
static inline uint32_t fm1_clock_mult(void)
{
    uint32_t m = (FCLK_READ(FCLK_PLL2_CON1) & 0xFFFu) + 2u;
    return m == FCLK_MULT_320 || m == FCLK_MULT_360 || m == FCLK_MULT_396 ? m : 0u;
}
