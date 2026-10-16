/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#pragma once
#ifndef MELODEE_BATCH_AUDIO
#define MELODEE_BATCH_AUDIO 1
#endif
/* Eight bounded descriptors, flushed before reuse. Only voice sample work
 * runs on CPU1; events and control preparation remain on CPU0. Each job owns
 * PCM scratch, so two voices never concurrently add to the same destination. */
typedef void (*audio_batch_fn)(const void *,int32_t *);
#define AUDIO_BATCH_BYTES 256u
#define AUDIO_BATCH_JOBS 8u
typedef struct {
    audio_batch_fn run;int32_t *out;uint32_t n, profile_id,guard;
#if MELODEE_AUDIO_PROFILE
    uint32_t elapsed;
#endif
    uint32_t data[AUDIO_BATCH_BYTES/4u] __attribute__((aligned(8)));int32_t pcm[CTL];
} audio_batch_job_t;
static audio_batch_job_t audio_batch_jobs[AUDIO_BATCH_JOBS] __attribute__((section(".audio_scratch"),aligned(32)));
static uint32_t audio_batch_count, audio_batch_pairs;
static uint8_t audio_batch_active;
/* TIMER5 preempts CPU0 only. Partition a group around measured kernel
 * costs and a CPU0 service allowance. Profiling does not change scheduling. */
static const uint32_t audio_batch_cost[AP_COUNT] __attribute__((section(".dsp_tables"),aligned(32)))={
    [AP_PROPHET]=3100,[AP_FM6]=1250,[AP_CZ]=2100,
    [AP_SID]=2000,[AP_808]=1150,[AP_909]=1900
};
#if defined(FM1_IRQ_TARGET) && MELODEE_DUAL_CORE
#define AB_LOAD(p) fm1_aw_load(p)
#define AB_STORE(p,v) fm1_aw_store(p,v)
#elif defined(FM1_IRQ_TARGET)
/* A single-core target consumes descriptors serially; no worker publishes. */
#define AB_LOAD(p) (*(p))
#define AB_STORE(p,v) (*(p)=(v))
#else
#define AB_LOAD(p) __atomic_load_n(p,__ATOMIC_ACQUIRE)
#define AB_STORE(p,v) __atomic_store_n(p,v,__ATOMIC_RELEASE)
#endif
static volatile uint32_t audio_batch_worker_mask,audio_batch_published,audio_batch_closed,audio_batch_invalid;
static uint32_t audio_batch_producer_cost,audio_batch_worker_cost;
static uint8_t audio_batch_started;
static void audio_batch_worker(void *context)
{
    audio_batch_job_t *j=context;uint32_t n=j->n,id=j->profile_id;audio_batch_fn run=j->run;
    if(!n || n>CTL || id>=AP_COUNT || j->guard!=((uint32_t)(uintptr_t)run^n^id^0x913ab267u)){
        AB_STORE(&audio_batch_invalid,1u);return;
    }
    for(uint32_t i=0;i<n;i++)j->pcm[i]=0;
    AP_START(start);run(j->data,j->pcm);
#if MELODEE_AUDIO_PROFILE
    j->elapsed=AUDIO_PROFILE_TICKS()-start;
#endif
}
static void audio_batch_merge(audio_batch_job_t *j)
{
#if MELODEE_AUDIO_PROFILE
    audio_profile_add(j->profile_id,j->elapsed);
#endif
    for(uint32_t i=0;i<j->n;i++)j->out[i]+=j->pcm[i];
}
static void audio_batch_parallel(void *context)
{
    (void)context;uint32_t done=0;
    for(;;){
        uint32_t count=AB_LOAD(&audio_batch_published);
        if(count>AUDIO_BATCH_JOBS){AB_STORE(&audio_batch_invalid,1u);return;}
        while(done<count){
            uint32_t k=done++;
            if(AB_LOAD(&audio_batch_worker_mask)&(1u<<k))audio_batch_worker(&audio_batch_jobs[k]);
        }
        if(AB_LOAD(&audio_batch_closed) && done==AB_LOAD(&audio_batch_published))return;
    }
}
static __attribute__((noinline)) void audio_batch_flush(void)
{
#if MELODEE_DUAL_CORE
    if(audio_batch_started){
            uint32_t mask=AB_LOAD(&audio_batch_worker_mask);
            AB_STORE(&audio_batch_closed,1u);
            for(uint32_t k=0;k<audio_batch_count;k++)
                if(!(mask&(1u<<k)))audio_batch_worker(&audio_batch_jobs[k]);
            int ok=audio_worker_join();audio_batch_pairs++;
            if(AB_LOAD(&audio_batch_invalid)){
                ok=0;
#ifdef FM1_IRQ_TARGET
                fm1_aw_store(&audio_worker.rejected,fm1_aw_load(&audio_worker.rejected)+1u);
                audio_worker_retire();
#else
                audio_worker_online=0;
#endif
            }
            /* State ownership returns at join. Drop a failed worker's
             * partial PCM without replay; merge in original voice order. */
            for(uint32_t k=0;k<audio_batch_count;k++)
                if(ok || !(mask&(1u<<k)))audio_batch_merge(&audio_batch_jobs[k]);
            audio_batch_count=0;audio_batch_started=0;return;
    }
#endif
    for(uint32_t k=0;k<audio_batch_count;k++){
        audio_batch_worker(&audio_batch_jobs[k]);audio_batch_merge(&audio_batch_jobs[k]);
    }
    audio_batch_count=0;
}
static void audio_batch_push(audio_batch_fn run,const void *data,uint32_t bytes,int32_t *out,uint32_t n,uint32_t profile_id)
{
    if(audio_batch_count==AUDIO_BATCH_JOBS)audio_batch_flush();
    uint32_t k=audio_batch_count++;audio_batch_job_t *j=&audio_batch_jobs[k];
    j->run=run;j->out=out;j->n=n;j->profile_id=profile_id;memcpy(j->data,data,bytes);
    j->guard=(uint32_t)(uintptr_t)run^n^profile_id^0x913ab267u;
#if MELODEE_DUAL_CORE
    if(!k){
        AB_STORE(&audio_batch_published,0u);AB_STORE(&audio_batch_worker_mask,0u);AB_STORE(&audio_batch_closed,0u);AB_STORE(&audio_batch_invalid,0u);
        audio_batch_worker_cost=0;audio_batch_producer_cost=2000u;
    }
    uint32_t cost=audio_batch_cost[profile_id];
    if(audio_batch_worker_cost<=audio_batch_producer_cost){
        AB_STORE(&audio_batch_worker_mask,AB_LOAD(&audio_batch_worker_mask)|(1u<<k));audio_batch_worker_cost+=cost;
    }else audio_batch_producer_cost+=cost;
    /* Publish only complete descriptors. CPU1 can render the first track
     * while CPU0 prepares the remaining tracks' controls. Flush closes the
     * stream before join; scratch is reused only after ownership returns. */
    AB_STORE(&audio_batch_published,audio_batch_count);
    if(!k)audio_batch_started=(uint8_t)audio_worker_submit(audio_batch_parallel,0);
#endif
}
