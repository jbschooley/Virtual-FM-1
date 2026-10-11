/* SPDX-License-Identifier: GPL-3.0-only */
/* FM-1 supply rails and clock tree, read only, for MELODEE_CORE1_TEST builds
 * (editor command 79). Melodee never changes a rail: they stay where the boot
 * loader sets them (BUILDING.md, "Units whose CPU1 misreads SRAM").
 *
 *   fm1_power_get(rail)   P33 read (IRQs off: P33 access is not reentrant)
 *   fm1_clock_regs(v)     SYS_DIV, CLK_CON0..3 (SDK WL82.h JL_CLOCK)
 *   fm1_cpu_ticks()       TIMER4 ticks for 4000 passes of 64 dependent adds
 *
 * Levels (SDK p33.h): SYSVDD, the core and SRAM rail (isd_config "DVDD"),
 * 0.93 V + 30 mV a step (11: 1.26 V); VDC14 ("DCDC14") 1.25 V + 50 mV a step
 * (3: 1.40 V); VDDIO 2.8 V + 100 mV a step.
 */
#pragma once
#include <stdint.h>
#include "fm1_sys.h"
#include "fm1_time.h"

#if MELODEE_CORE1_TEST
enum { FM1_RAIL_SYSVDD, FM1_RAIL_VDC14, FM1_RAIL_VDDIO, FM1_RAILS };
static const uint8_t fm1_rail_reg[FM1_RAILS] = { 0x09, 0x06, 0x05 };   /* P3_ANA_CON9, 6, 5 */
static const uint8_t fm1_rail_max[FM1_RAILS] = { 15, 7, 7 };            /* field masks [3:0], [2:0], [2:0] */

static uint32_t fm1_power_get(uint32_t rail)
{
    return fm1_p33_read(fm1_rail_reg[rail]) & fm1_rail_max[rail];
}

static void fm1_clock_regs(uint32_t *v)   /* 5 words */
{
    uint32_t i;
    for (i = 0; i < 5u; i++)
        v[i] = *(volatile uint32_t *)(0x10008u + 4u * i);
}

static __attribute__((noinline)) uint32_t fm1_cpu_adds(uint32_t passes)
{
    uint32_t x = 0;
    while (passes--)
        __asm__ volatile(".rept 64\n\t%0 = %0 + 1\n\t.endr" : "+r"(x));
    return x;
}

static uint32_t fm1_cpu_ticks(void)          /* IRQs off: TIMER4 ticks for 4000 x 64 adds */
{
    uint32_t t;
    fm1_cpu_adds(4);
    t = fm1_ticks();
    fm1_cpu_adds(4000);
    return fm1_ticks() - t;
}
#endif
