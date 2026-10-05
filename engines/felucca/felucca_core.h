/* felucca_core.h -- the API of one compiled copy of Felucca (felucca_core.c).
 *
 * Every copy's functions carry its prefix: with FEL_PREFIX=fel3_, FEL(init) is
 * fel3_init. FELUCCA_API(X) lists them for code that builds a table of copies.
 * Not thread-safe: one copy is driven by one plugin instance, from its audio thread. */
#ifndef FELUCCA_CORE_H
#define FELUCCA_CORE_H

#include <stdint.h>

#define FEL_CAT2(a, b) a##b
#define FEL_CAT(a, b) FEL_CAT2(a, b)
#define FEL(name) FEL_CAT(FEL_PREFIX, name)

typedef struct {
    const char *label;          /* as on the device */
    const char *unit;           /* may be null */
    uint8_t fmt;                /* Felucca's F_* display format; F_ENUM (8) has names */
    int16_t min, max, def;
    const char *const *names;   /* nnames value names, from min */
    uint32_t nnames;
} fel_desc_t;

/* X(return type, name, parameter list) for every function of a copy */
#define FELUCCA_API(X)                                                           \
    X(void, restore, (void))                                                     \
    X(uint32_t, state_bytes, (void))                                             \
    X(void, init, (void))                                                        \
    X(uint32_t, ctl, (void))                                                     \
    X(uint32_t, rate, (void))                                                    \
    X(uint32_t, ntracks, (void))                                                 \
    X(uint32_t, nparts, (void))                                                  \
    X(uint32_t, nengines, (void))                                                \
    X(uint32_t, pcount, (void))                                                  \
    X(uint32_t, pe0, (void))                                                     \
    X(uint32_t, gcount, (void))                                                  \
    X(uint32_t, nsteps, (void))                                                  \
    X(const char *, engine_name, (uint32_t e))                                   \
    X(const char *, engine_page, (uint32_t e, uint32_t page))                    \
    X(uint32_t, npresets, (uint32_t e))                                          \
    X(const char *, preset_name, (uint32_t e, uint32_t i))                       \
    X(int, param_desc, (uint32_t track, uint32_t id, fel_desc_t *out))           \
    X(int, global_desc, (uint32_t id, fel_desc_t *out))                          \
    X(int32_t, param_get, (uint32_t track, uint32_t id))                         \
    X(void, param_set, (uint32_t track, uint32_t id, int32_t v))                 \
    X(int32_t, global_get, (uint32_t id))                                        \
    X(void, global_set, (uint32_t id, int32_t v))                                \
    X(uint32_t, engine_of, (uint32_t track))                                     \
    X(uint32_t, preset_of, (uint32_t track))                                     \
    X(void, set_engine, (uint32_t track, uint32_t e))                            \
    X(void, apply_preset, (uint32_t track, uint32_t pi))                         \
    X(void, midi, (uint32_t usbMidiPacket))                                      \
    X(void, render, (int32_t *interleavedStereo, uint32_t frames))

#endif

/* the declarations of one copy: include felucca_core_api.h with FEL_PREFIX defined */
