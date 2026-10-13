/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>

/* Carried across soft resets (a crash, the watchdog, an update), lost at
 * power-off; nothing goes to flash on a unit that may be unstable. Once CPU1
 * was retired, or CPU0 crashed with CPU1 working (its data can go bad too,
 * where no check reaches), every boot until power-off renders on one core. */
#define FM1_CORE1_BAR_MAGIC 0x52414231u      /* "1BAR" */
typedef struct { uint32_t magic, barred; } fm1_core1_bar_t;
/* Not static, as fm1_crash: the compiler must not assume a zero start. */
fm1_core1_bar_t fm1_core1_bar __attribute__((section(".noinit")));
static inline int fm1_core1_barred(void)
{
    return fm1_core1_bar.magic == FM1_CORE1_BAR_MAGIC && fm1_core1_bar.barred == 1u;
}
static inline void fm1_core1_bar_set(void)
{
    fm1_core1_bar.magic = FM1_CORE1_BAR_MAGIC;
    fm1_core1_bar.barred = 1;
}
