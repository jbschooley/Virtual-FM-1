/* SPDX-License-Identifier: GPL-3.0-only */
/* Bounded audio working-memory arena. Only the audio owner (or boot/host
 * setup before audio starts) mutates it; main-loop migration reserves/releases
 * its two temporary handles with audio interrupts excluded. Fourteen fixed handles, no heap,
 * relocation, or searches proportional to the number of voices/samples.
 * Linker-derived SRAM tails are separate banks: an allocation never spans a
 * code/data/retained-state boundary. Small banks first preserve the large
 * contiguous POOL bank for delay/reverb/performance buffers. */
enum { RES_ENGINE0, RES_SLICER0 = RES_ENGINE0 + NPART,
       RES_PERFORM = RES_SLICER0 + NTRK, RES_CHORUS, RES_REVERB, RES_DELAY, RES_LEGACY0, RES_LEGACY1, RES_COUNT };
static struct { void *ptr; uint32_t size; } resource[RES_COUNT];
static uint32_t resource_peak, resource_failures;
#ifdef FM1_IRQ_TARGET
extern uint8_t _resource_start[], _resource_end[];
extern uint8_t _resource_low_start[], _resource_low_end[];
extern uint8_t _resource_data_start[], _resource_data_end[];
extern uint8_t _resource_retained_start[], _resource_retained_end[];
extern uint8_t _resource_cache_start[], _resource_cache_end[];
#if MELODEE_CACHE_RAM
#define RESOURCE_CACHE_AVAILABLE (fm1_cache.status == FM1_CACHE_READY)
#else
#define RESOURCE_CACHE_AVAILABLE 0
#endif
#define RESOURCE_MAIN_CAPACITY ((uint32_t)((uintptr_t)_resource_end - (uintptr_t)_resource_start))
#else
static uint32_t resource_host[40960];
static uint32_t resource_host_low[5120], resource_host_data[512], resource_host_retained[1024];
static uint32_t resource_host_cache[7168], resource_host_cache_enabled;
#define RESOURCE_CACHE_AVAILABLE resource_host_cache_enabled
#define _resource_cache_start ((uint8_t *)resource_host_cache)
#define _resource_cache_end ((uint8_t *)(resource_host_cache + 7168))
#define _resource_start ((uint8_t *)resource_host)
#define _resource_end ((uint8_t *)(resource_host + 40960))
#define _resource_low_start ((uint8_t *)resource_host_low)
#define _resource_low_end ((uint8_t *)(resource_host_low + 5120))
#define _resource_data_start ((uint8_t *)resource_host_data)
#define _resource_data_end ((uint8_t *)(resource_host_data + 512))
#define _resource_retained_start ((uint8_t *)resource_host_retained)
#define _resource_retained_end ((uint8_t *)(resource_host_retained + 1024))
#define RESOURCE_MAIN_CAPACITY ((uint32_t)sizeof resource_host)
#endif
/* The cache RAM's first UI_CACHE_BYTES are the UI's (ui.c: the undo levels), the rest the audio's: the audio's worst
 * case fits the SRAM banks alone (the linker's budget), so the cache RAM only ever adds to it */
#define UI_CACHE_BYTES 14336u
static uint8_t *ui_cache(void) { return RESOURCE_CACHE_AVAILABLE ? _resource_cache_start : 0; }
static const struct { uint8_t *start, *end; } resource_banks[] = {
    { _resource_data_start, _resource_data_end },
    { _resource_retained_start, _resource_retained_end },
    { _resource_low_start, _resource_low_end },
    { _resource_cache_start + UI_CACHE_BYTES, _resource_cache_end },
    { _resource_start, _resource_end }
};
#define RESOURCE_BANKS ((uint32_t)(sizeof resource_banks / sizeof resource_banks[0]))
enum { RESOURCE_CACHE_BANK = 3 };
#define RESOURCE_CAPACITY (RESOURCE_MAIN_CAPACITY + \
    (uint32_t)((uintptr_t)_resource_low_end - (uintptr_t)_resource_low_start) + \
    (uint32_t)((uintptr_t)_resource_data_end - (uintptr_t)_resource_data_start) + \
    (uint32_t)((uintptr_t)_resource_retained_end - (uintptr_t)_resource_retained_start) + \
    (RESOURCE_CACHE_AVAILABLE ? (uint32_t)((uintptr_t)_resource_cache_end - (uintptr_t)_resource_cache_start) - UI_CACHE_BYTES : 0u))
static uint32_t resource_used(void)
{
    uint32_t n = 0;
    for (uint32_t k = 0; k < RES_COUNT; k++) n += resource[k].size;
    return n;
}
static void resource_release(uint32_t id) { resource[id].size = 0; }
static __attribute__((noinline)) void *resource_allocate(uint32_t id, uint32_t bytes)
{
    if (bytes > RESOURCE_CAPACITY) { resource_failures++; return 0; }
    uint32_t size = (bytes + 3u) & ~3u;
    if (!size) { resource_release(id); return 0; }
    for (uint32_t bank = 0; bank < RESOURCE_BANKS; bank++) {
        if (bank == RESOURCE_CACHE_BANK && !RESOURCE_CACHE_AVAILABLE) continue;
        uintptr_t at = (uintptr_t)resource_banks[bank].start;
        uintptr_t limit = (uintptr_t)resource_banks[bank].end;
        /* Each collision advances past a live block; at most RES_COUNT passes.
         * Integer addresses permit distinct host arrays and disjoint SRAM banks. */
        for (uint32_t pass = 0; pass <= RES_COUNT; pass++) {
            if (at > limit || size > limit - at) break;
            uintptr_t next = at;
            for (uint32_t k = 0; k < RES_COUNT; k++) {
                if (k == id || !resource[k].size) continue;
                uintptr_t begin = (uintptr_t)resource[k].ptr;
                uintptr_t end = begin + resource[k].size;
                if (at < end && at + size > begin && end > next) next = end;
            }
            if (next == at) {
                void *p = (void *)at;
                resource[id].ptr = p; resource[id].size = size;
                uint32_t *words=p;
                for (uint32_t j=0;j<size/4u;j++)words[j]=0;
                uint32_t used = resource_used();
                if (used > resource_peak) resource_peak = used;
                return p;
            }
            at = next;
        }
    }
    resource_failures++;
    return 0;                          /* leave an existing allocation intact */
}

/* Keep the already allocated path cheap in every engine accessor. */
static inline void *resource_get(uint32_t id, uint32_t bytes)
{
    uint32_t size=(bytes+3u)&~3u;
    if(size && size==resource[id].size)return resource[id].ptr;
    return resource_allocate(id,bytes);
}
