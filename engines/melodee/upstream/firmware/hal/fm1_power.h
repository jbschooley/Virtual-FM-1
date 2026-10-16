/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* FM-1 supply startup, matching the stock V15 analog setup, and read-only
 * diagnostics for MELODEE_CORE1_TEST builds (editor command 79).
 * Register order / values: stock app 0x02004F90..0x0200509A, identified against
 * the AC79 SDK __hw_power_init. See BUILDING.md, "The stock supply setup".
 * Call with IRQs off and CPU1 held, after application RAM is initialized.
 *
 *   fm1_power_get(rail)   P33 read (IRQs off: P33 access is not reentrant)
 *   fm1_clock_regs(v)     SYS_DIV, CLK_CON0..3 (SDK WL82.h JL_CLOCK)
 *   fm1_cpu_ticks()       TIMER4 ticks for 4000 passes of 64 dependent adds
 *
 * Levels (SDK p33.h): SYSVDD, the core and SRAM rail (isd_config "DVDD"),
 * 0.93 V + 30 mV a step (15: 1.38 V); VDC14 ("DCDC14") 1.25 V + 50 mV a step
 * (7: 1.60 V); VDDIO 2.8 V + 100 mV a step (4: 3.2 V).
 */
#pragma once
#include <stdint.h>
#ifndef FM1_POWER_HOST_TEST
#include "fm1_sys.h"
#include "fm1_time.h"

/* Stock delay(1000), not 1000 us: keep its nop / increment / branch loop.
 * The memory clobber also keeps each delay between its P33 operations. */
static inline __attribute__((always_inline)) void fm1_power_settle(void)
{
    int32_t n = -1000;
    __asm__ volatile("goto 2f\n1:\n\tnop\n\t%0 += 1\n2:\n\tif (%0 != 0) goto 1b"
                     : "+r"(n) :: "memory");
}
#endif

static uint8_t fm1_power_ready;

/* Same field replacement as P33_CON_SET. Never turn a timed-out read's dummy
 * byte into an analog-register write. A failed transfer stops the sequence. */
static int fm1_power_field(uint32_t reg, uint32_t start, uint32_t width, uint32_t value)
{
    uint32_t timeouts = fm1_p33_timeouts;
    uint32_t mask = ((1u << width) - 1u) << start;
    uint8_t old = fm1_p33_read(reg);
    if (fm1_p33_timeouts != timeouts)
        return 0;
    fm1_p33_write(reg, (uint8_t)((old & ~mask) | ((value << start) & mask)));
    return fm1_p33_timeouts == timeouts;
}

/* Both builds use the stock supply setup. This is a fixed boot sequence, not
 * a runtime voltage ladder. CPU1 remains held if any transfer or check fails.
 * The stock's unused reads (6, 5, 5) are retained in their original position. */
static int fm1_power_init(void)
{
    uint32_t timeouts = fm1_p33_timeouts;
    uint8_t mode, io, vdc14, sysvdd;
#define FM1_POWER_STEP(op) do { op; if (fm1_p33_timeouts != timeouts) return 0; } while (0)
#define FM1_POWER_FIELD(reg, start, width, value) \
    do { if (!fm1_power_field(reg, start, width, value)) return 0; } while (0)
    FM1_POWER_FIELD(0x00u, 2, 1, 1);             /* LDO13 on during handover */
    fm1_power_settle();
    FM1_POWER_FIELD(0x00u, 3, 1, 1);             /* DCDC13 on */
    fm1_power_settle();
    FM1_POWER_FIELD(0x00u, 2, 1, 0);             /* LDO13 off */
    fm1_power_settle();
    FM1_POWER_STEP(fm1_p33_write(0x01u, 0x20));
    FM1_POWER_STEP(fm1_p33_write(0x05u, 0x40));
    FM1_POWER_STEP(fm1_p33_write(0x06u, 0x0C));
    FM1_POWER_STEP(fm1_p33_write(0x07u, 0xA5));
    FM1_POWER_STEP(fm1_p33_write(0x08u, 0x96));
    FM1_POWER_FIELD(0x09u, 4, 1, 1);             /* SYSVDD drive */
    FM1_POWER_FIELD(0x05u, 0, 3, 4);             /* VDDIOM 3.2 V */
    FM1_POWER_FIELD(0x06u, 0, 3, 7);             /* VDC14 1.60 V */
    FM1_POWER_FIELD(0x05u, 4, 2, 3);             /* VDDIOW 3.2 V */
    FM1_POWER_STEP((void)fm1_p33_read(0x06u));
    FM1_POWER_STEP((void)fm1_p33_read(0x05u));
    FM1_POWER_STEP((void)fm1_p33_read(0x05u));
    FM1_POWER_STEP(fm1_p33_or(0x00u, 0x80));      /* RC_250k_EN */
    FM1_POWER_FIELD(0x16u, 0, 6, 0);             /* analog keep */
    FM1_POWER_STEP(fm1_p33_or(0x02u, 0x40));      /* VDDOK_DIS */
    FM1_POWER_STEP(fm1_p33_and(0x17u, 0xDF));
    FM1_POWER_FIELD(0x09u, 0, 4, 15);            /* SYSVDD 1.38 V, last */
#undef FM1_POWER_FIELD
#undef FM1_POWER_STEP
    /* Additional read-only verification; no retry or speculative rollback. */
    mode = fm1_p33_read(0x00u);
    io = fm1_p33_read(0x05u);
    vdc14 = fm1_p33_read(0x06u);
    sysvdd = fm1_p33_read(0x09u);
    return fm1_p33_timeouts == timeouts && (mode & 0x0Cu) == 0x08u &&
           (io & 0x37u) == 0x34u && (vdc14 & 7u) == 7u && (sysvdd & 15u) == 15u;
}

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
