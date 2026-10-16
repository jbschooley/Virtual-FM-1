/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* AC79 EQ, synchronous mono float cascade. Register/row layout follows the
 * V1.2.1 SDK eq.c: 0x1e000 CON0..FLT_ADR, 0x1e800 eight-word rows.
 * No SDK scheduler, IRQ handler, allocation, or unbounded wait in audio.
 * Opt-in until the FM-1 has been auditioned and its DMA latency measured. */
#pragma once
#ifndef MELODEE_HW_EQ
#define MELODEE_HW_EQ 0
#endif
#if MELODEE_HW_EQ && (defined(FM1_IRQ_TARGET) || defined(FM1_EQ_TEST))
#ifndef FM1_EQ_TEST
#define FE_READ(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define FE_WRITE(a,v) (*(volatile uint32_t *)(uintptr_t)(a) = (v))
#define FE_SYNC() __asm__ volatile("csync; ssync" ::: "memory")
#endif
static uint32_t fm1_eq_fault;
static inline __attribute__((always_inline)) int fm1_eq_run(float row[4][8], const float *in, float *out, uint32_t n)
{
    if (fm1_eq_fault || !n || n > 32u || (FE_READ(0x1e000u) & 0x01000000u)) return 0;
    FE_WRITE(0x1e000u, 0);
    for (uint32_t k=0;k<32u;k++) {
        union { float f; uint32_t u; } v; v.f=((float *)row)[k];
        FE_WRITE(0x1e800u+4u*k,v.u);
    }
    FE_WRITE(0x1e01cu,0);              /* no external coefficient rows */
    FE_WRITE(0x1e004u,(n*4u)<<16 | n*4u);
    FE_WRITE(0x1e008u,0);              /* one input, one output channel */
    FE_WRITE(0x1e00cu,4u<<16 | 4u);    /* float sample strides */
    FE_WRITE(0x1e010u,(uint32_t)(uintptr_t)in);
    FE_WRITE(0x1e014u,(uint32_t)(uintptr_t)out);
    FE_WRITE(0x1e018u,n-1u);
    FE_SYNC();
    FE_WRITE(0x1e000u,(3u<<16) | 0xa0u | 0x40000000u | 3u);
    for (uint32_t spins=0;spins<8192u;spins++) {
        if (FE_READ(0x1e000u)&0x80000000u) {
            FE_SYNC();
            for(uint32_t k=0;k<32u;k++) {
                union { float f; uint32_t u; } v;v.u=FE_READ(0x1e800u+4u*k);((float *)row)[k]=v.f;
            }
            FE_WRITE(0x1e000u,0x40000000u);
            return 1;
        }
    }
    FE_WRITE(0x1e000u,0);FE_SYNC();fm1_eq_fault=1;return 0;
}
#else
static int fm1_eq_run(float row[4][8], const float *in, float *out, uint32_t n)
{ (void)row;(void)in;(void)out;(void)n;return 0; }
#endif
