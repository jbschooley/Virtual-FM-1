/* SPDX-License-Identifier: GPL-3.0-only */
/* Single producer (CPU0 audio ISR), single consumer (CPU1). One outstanding
 * job. Completion publishes the result AND returns ownership of voice state.
 * CPU0 must join before events, engine changes, post processing or flash IO.
 * Platform hooks provide acquire/release accesses, time, holding the worker,
 * the count of its faults and the answer to a misread.
 *
 * Some FM-1 units' CPU1 now and then reads shared SRAM wrong (keremimo/melodee#17:
 * 0x00200000 where RAM holds 0). Before, one such read of the mailbox at rest
 * called a null fn: the MELODEE CRASH of an idle unit. The worker now runs only
 * the next request, and only when its check word matches: a misread is counted,
 * never run. A misread (unless the platform answers it: AW_MISREAD, test
 * builds only), a CPU1 fault or a stall retires the worker for the session:
 * CPU1 is held and CPU0 renders every voice again. */
#pragma once
#include <stdint.h>
#ifndef AW_IDLE
#define AW_IDLE() ((void)0)
#endif
#ifndef AW_HOLD
#define AW_HOLD() ((void)0)          /* stop CPU1 for good: on return it touches nothing more */
#endif
#ifndef AW_FAULTS
#define AW_FAULTS() 0u               /* CPU1 faults the platform's exception handler caught */
#endif
#ifndef AW_MISREAD
#define AW_MISREAD(n) 0              /* n misreads so far: 1 answered (keep the worker), 0 retire it */
#endif
#define AW_CHECK(fn, context, request) \
    ((uint32_t)(uintptr_t)(fn) ^ (uint32_t)(uintptr_t)(context) ^ (uint32_t)(request) * 0x9E3779B1u)
typedef void (*audio_worker_fn)(void *);
/* CPU0's words, then CPU1's on a cache line of their own */
static struct {
    AW_WORD request;
    audio_worker_fn fn;
    void *context;
    uint32_t check, max_wait_ticks, timeouts;
    AW_WORD ready __attribute__((aligned(32)));
    AW_WORD complete, rejected;
    uint32_t jobs, max_job_ticks, misread;          /* misread: the last request refused, as CPU1 read it */
} audio_worker __attribute__((aligned(32)));
static int audio_worker_online;

static int audio_worker_submit(audio_worker_fn fn, void *context)
{
    uint32_t request;
    if (!audio_worker_online || (request = AW_LOAD(&audio_worker.request)) != AW_LOAD(&audio_worker.complete))
        return 0;
    request++;
    audio_worker.fn = fn;
    audio_worker.context = context;
    audio_worker.check = AW_CHECK(fn, context, request);
    AW_STORE(&audio_worker.request, request);
    return 1;
}

/* CPU0: hold CPU1 for good and render alone. A job it may have been running
 * is abandoned, never rerun (its voice was partly advanced). */
static void audio_worker_retire(void)
{
    AW_HOLD();
    audio_worker_online = 0;
    AW_STORE(&audio_worker.complete, AW_LOAD(&audio_worker.request));
}

/* CPU0 between blocks (nothing outstanding): retire a worker that faulted or
 * misread past what the platform answers */
static void audio_worker_check(void)
{
    uint32_t misreads;
    if (!audio_worker_online)
        return;
    misreads = AW_LOAD(&audio_worker.rejected);
    if (AW_FAULTS() || (misreads && !AW_MISREAD(misreads)))
        audio_worker_retire();
}

/* 1: the job's results are in. 0: CPU1 faulted or stalled for 10 ms; it is
 * retired and the job's results must be dropped (one block of one voice). */
static int audio_worker_join(void)
{
    uint32_t start = AW_TICKS();
    while (AW_LOAD(&audio_worker.complete) != AW_LOAD(&audio_worker.request)) {
        if (AW_FAULTS() || (uint32_t)(AW_TICKS() - start) >= AW_TIMEOUT_TICKS) {
            if (!AW_FAULTS())
                audio_worker.timeouts++;
            audio_worker_retire();
            return 0;
        }
    }
    uint32_t elapsed = AW_TICKS() - start;
    if (elapsed > audio_worker.max_wait_ticks) audio_worker.max_wait_ticks = elapsed;
    return 1;
}

/* One look at the mailbox (CPU1). The count of finished requests stays in a
 * register: only request, fn, context and check come from SRAM, and a misread
 * of any of them fails the check. */
static inline __attribute__((always_inline)) void audio_worker_poll(uint32_t *done)
{
    uint32_t request = AW_LOAD(&audio_worker.request);
    if (request == *done) { AW_IDLE(); return; }
    if (request == *done + 1u) {
        audio_worker_fn fn = audio_worker.fn;
        void *context = audio_worker.context;
        if (audio_worker.check == AW_CHECK(fn, context, request)) {
            uint32_t start = AW_TICKS();
            fn(context);
            uint32_t elapsed = AW_TICKS() - start;
            audio_worker.jobs++;
            if (elapsed > audio_worker.max_job_ticks) audio_worker.max_job_ticks = elapsed;
            *done = request;
            AW_STORE(&audio_worker.complete, request);
            return;
        }
    }
    audio_worker.misread = request;
    AW_STORE(&audio_worker.rejected, AW_LOAD(&audio_worker.rejected) + 1u);
}

/* This function and its polling helpers must stay in internal RAM on target.
 * It publishes completion only after a callback has returned from XIP. With
 * no job outstanding, CPU0 may turn XIP off for a flash write. Do not use idle:
 * CPU1 has no interrupt to wake it. */
AW_RAM_LOOP void audio_worker_loop(void)
{
    uint32_t done = AW_LOAD(&audio_worker.complete);
    AW_STORE(&audio_worker.ready, 1u);
    for (;;)
        audio_worker_poll(&done);
}
