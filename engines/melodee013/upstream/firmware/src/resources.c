/* SPDX-License-Identifier: GPL-3.0-only */
/* Bounded audio working-memory arena. Only the audio owner (or boot/host
 * setup before audio starts) mutates it; main-loop migration reserves/releases
 * its two temporary handles with audio interrupts excluded. Thirteen fixed handles, no heap,
 * relocation, or searches proportional to the number of voices/samples.
 * The linker leaves the rest of POOL available after permanent buffers. */
enum { RES_ENGINE0, RES_SLICER0 = RES_ENGINE0 + NPART,
       RES_PERFORM = RES_SLICER0 + NTRK, RES_CHORUS, RES_REVERB, RES_LEGACY0, RES_LEGACY1, RES_COUNT };
static struct { uint32_t off, size; } resource[RES_COUNT];
static uint32_t resource_peak, resource_failures;
#ifdef FM1_IRQ_TARGET
extern uint8_t _resource_start[], _resource_end[];
#define RESOURCE_BASE _resource_start
#define RESOURCE_CAPACITY ((uint32_t)(_resource_end - _resource_start))
#else
static uint32_t resource_host[32768];
#define RESOURCE_BASE ((uint8_t *)resource_host)
#define RESOURCE_CAPACITY ((uint32_t)sizeof resource_host)
#endif
static uint32_t resource_used(void)
{
    uint32_t n = 0;
    for (uint32_t k = 0; k < RES_COUNT; k++) n += resource[k].size;
    return n;
}
static void resource_release(uint32_t id) { resource[id].size = 0; }
static __attribute__((noinline)) void *resource_allocate(uint32_t id, uint32_t bytes)
{
    uint32_t size = (bytes + 3u) & ~3u, off = 0;
    if (bytes > RESOURCE_CAPACITY) { resource_failures++; return 0; }
    if (!size) { resource_release(id); return 0; }
    /* Each collision advances past a live block; at most RES_COUNT passes. */
    for (uint32_t pass = 0; pass <= RES_COUNT; pass++) {
        uint32_t next = off;
        for (uint32_t k = 0; k < RES_COUNT; k++) {
            if (k == id || !resource[k].size) continue;
            uint32_t end = resource[k].off + resource[k].size;
            if (off < end && off + size > resource[k].off && end > next) next = end;
        }
        if (next == off) {
            if (off > RESOURCE_CAPACITY || size > RESOURCE_CAPACITY - off) break;
            resource[id].off = off; resource[id].size = size;
            void *p = RESOURCE_BASE + off;
            uint32_t *words=p;
            for (uint32_t j=0;j<size/4u;j++)words[j]=0;
            uint32_t used = resource_used();
            if (used > resource_peak) resource_peak = used;
            return p;
        }
        off = next;
    }
    resource_failures++;
    return 0;                          /* leave an existing allocation intact */
}

/* Keep the already allocated path cheap in every engine accessor. */
static inline void *resource_get(uint32_t id, uint32_t bytes)
{
    uint32_t size=(bytes+3u)&~3u;
    if(size && size==resource[id].size)return RESOURCE_BASE+resource[id].off;
    return resource_allocate(id,bytes);
}
