/* SPDX-License-Identifier: GPL-3.0-only */
/* AC79 cache RAM, following SDK V1.2.1 sdk_ld_sfc.c / cache_way_config.
 * Keep all 8 instruction ways; retain data way 7 for both cores and DMA.
 * The other seven 4 KiB data ways map at 0x01F28000..0x01F2F000.
 * Boot only, IRQs off, CPU1 held, before any cache-bank allocation. Never
 * change this configuration at runtime or place code/stacks in cache RAM.
 * Disabled by default pending physical FM-1 validation. */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"

enum { FM1_CACHE_DISABLED, FM1_CACHE_READY, FM1_CACHE_RETRY,
       FM1_CACHE_EXTERNAL_MEMORY, FM1_CACHE_TIMEOUT, FM1_CACHE_WAY_FAILED,
       FM1_CACHE_MEMORY_FAILED };
static struct {
    uint32_t status, con_before, data_before, instruction_before;
    uint32_t con_after, data_after, instruction_after, failure_address, test_ticks;
} fm1_cache;

#if MELODEE_CACHE_RAM
#ifndef FM1_CACHE_HOST_TEST
#define FC_READ(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define FC_WRITE(a,v) (*(volatile uint32_t *)(uintptr_t)(a) = (v))
#define FC_SYNC() __asm__ volatile("csync; ssync" ::: "memory")
#endif
#define FC_CON 0x01EEE008u
#define FC_DATA 0x01EEE00Cu
#define FC_INSTRUCTION 0x01EEE010u
#define FC_TICKS 0x00010804u
#define FC_BEGIN 0x01F28000u
#define FC_END 0x01F2F000u

/* Must remain wholly in SRAM: the instruction cache is temporarily off.
 * Volatile word loops avoid compiler-generated memset calls into XIP. */
#ifdef FM1_CACHE_HOST_TEST
#define FC_BOOT_FN static
#else
#define FC_BOOT_FN static __attribute__((noinline, section(".ram_text.cache")))
#endif
FC_BOOT_FN void fm1_cache_init(uint32_t retry)
{
    uint32_t start, spins, a, pass, want, got, dc, ic, con;
    if (retry) { fm1_cache.status = FM1_CACHE_RETRY; return; }
    con = FC_READ(FC_CON); dc = FC_READ(FC_DATA); ic = FC_READ(FC_INSTRUCTION);
    fm1_cache.con_before = con; fm1_cache.data_before = dc; fm1_cache.instruction_before = ic;
    /* Avoid discarding dirty cached SDRAM/PSRAM: this path supports internal
     * SRAM-only FM-1 firmware. Unsupported boot state keeps the SRAM arena. */
    if ((FC_READ(0x00040400u) & 0x800u) || (FC_READ(0x00040500u) & 1u)) {
        fm1_cache.status = FM1_CACHE_EXTERNAL_MEMORY; return;
    }
    start = FC_READ(FC_TICKS);
    for (spins = 0; !(FC_READ(FC_CON) & 0x4000u); spins++) {
        if (spins >= 100000u || (uint32_t)(FC_READ(FC_TICKS)-start) >= 240000u) {
            fm1_cache.status = FM1_CACHE_TIMEOUT; return;
        }
    }
    FC_WRITE(FC_CON, con & ~0x300u); FC_SYNC();
    FC_WRITE(FC_DATA, (dc & 0xFF000000u) | 0x00808080u);
    FC_WRITE(FC_INSTRUCTION, (ic & 0xFFFF0000u) | 0xFFFFu);
    /* SDK tag/valid metadata, for CPU0, CPU1 and peripheral data cache. */
    for (a = 0x01F00000u; a < 0x01F04000u; a += 4u) FC_WRITE(a, 0);
    for (a = 0x01F08000u; a < 0x01F0A200u; a += 4u) FC_WRITE(a, 0);
    for (a = 0x01F0B000u; a < 0x01F0B200u; a += 4u) FC_WRITE(a, 0);
    FC_SYNC();
    FC_WRITE(FC_CON, (con & ~0x300u) | 0x100u); FC_SYNC();
    fm1_cache.con_after = FC_READ(FC_CON);
    fm1_cache.data_after = FC_READ(FC_DATA);
    fm1_cache.instruction_after = FC_READ(FC_INSTRUCTION);
    if (fm1_cache.data_after != ((dc & 0xFF000000u) | 0x00808080u) ||
        fm1_cache.instruction_after != ((ic & 0xFFFF0000u) | 0xFFFFu) ||
        (fm1_cache.con_after & 0x300u) != 0x100u) {
        fm1_cache.status = FM1_CACHE_WAY_FAILED;
    } else {
        /* Whole-bank address patterns and complements detect aliased ways,
         * stuck data/address bits and retention between separate passes. */
        for (pass = 0; pass < 2u; pass++) {
            for (a = FC_BEGIN; a < FC_END; a += 4u) {
                want = a ^ 0xA55A3CC3u;
                FC_WRITE(a, pass ? ~want : want);
            }
            FC_SYNC();
            for (a = FC_BEGIN; a < FC_END; a += 4u) {
                want = a ^ 0xA55A3CC3u; got = FC_READ(a);
                if (got != (pass ? ~want : want)) {
                    fm1_cache.status = FM1_CACHE_MEMORY_FAILED;
                    fm1_cache.failure_address = a;
                    goto failed;
                }
            }
        }
        for (a = FC_BEGIN; a < FC_END; a += 4u) FC_WRITE(a, 0);
        FC_SYNC();
        fm1_cache.test_ticks = FC_READ(FC_TICKS)-start;
        fm1_cache.status = FM1_CACHE_READY;
        return;
    }
failed:
    /* No allocations are live yet. Restore the inherited way masks and
     * enable state before returning to XIP; failed banks remain unavailable. */
    FC_WRITE(FC_CON, FC_READ(FC_CON) & ~0x300u); FC_SYNC();
    FC_WRITE(FC_DATA, dc); FC_WRITE(FC_INSTRUCTION, ic); FC_SYNC();
    FC_WRITE(FC_CON, con); FC_SYNC();
    fm1_cache.con_after = FC_READ(FC_CON);
    fm1_cache.data_after = FC_READ(FC_DATA);
    fm1_cache.instruction_after = FC_READ(FC_INSTRUCTION);
    fm1_cache.test_ticks = FC_READ(FC_TICKS)-start;
}
#undef FC_READ
#undef FC_WRITE
#undef FC_SYNC
#undef FC_BOOT_FN
#endif
