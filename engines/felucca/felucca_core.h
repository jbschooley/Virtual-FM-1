/* felucca_core.h -- the API of a compiled firmware core (felucca_core.c, and sloop_core.c and
 * melodee_core.c behind the same API).
 *
 * Each firmware is compiled once, its functions prefixed (FEL_PREFIX: fel_, slp_, mel_; FEL(init) is
 * fel_init). Its state is not in the program's variables but in a block each instance owns
 * (engines/felucca/fel_state.py rewrites the compiled core that way): state_size() bytes, set up
 * by state_init(), and bind()-ed on the calling thread before any other call, which then works on
 * that state. FELUCCA_API(X) lists the functions for code that builds a table of them.
 * Not thread-safe per state: one instance's calls are made one at a time (FeluccaEngine's lock). */
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
    X(void, bind, (void *state))                                                 \
    X(uint64_t, state_size, (void))                                              \
    X(int, state_init, (void *state))                                            \
    X(void, init, (void))                                                        \
    X(const char *, version, (void))                                             \
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
    X(uint32_t, engines_shown, (void))                                           \
    X(uint32_t, engine_shown, (uint32_t n))                                      \
    X(uint32_t, fm6_engine, (void))                                              \
    X(void, fm6_patch_get, (uint32_t track, uint8_t *v155))                      \
    X(void, fm6_patch_set, (uint32_t track, const uint8_t *v155))                \
    X(void, midi, (uint32_t usbMidiPacket))                                      \
    X(void, render, (int32_t *interleavedStereo, uint32_t frames))               \
    X(void, service, (void))                                                     \
    X(int, service_ready, (void))                                                \
    X(uint32_t, nbuttons, (void))                                                \
    X(const char *, button_name, (uint32_t b))                                   \
    X(uint32_t, nknobs, (void))                                                  \
    X(const char *, knob_name, (uint32_t k))                                     \
    X(void, button, (uint32_t b, int down))                                      \
    X(void, key, (uint32_t k, int down))                                         \
    X(void, knob, (uint32_t role, int32_t steps))                                \
    X(void, draw, (uint16_t *screen240x240))                                     \
    X(uint32_t, midi_out, (uint32_t *usbMidiPackets, uint32_t max))              \
    X(void, transport, (int play))                                               \
    X(uint32_t, selected, (void))                                                \
    X(void, select, (uint32_t track))                                            \
    X(int, playing, (void))                                                      \
    X(int32_t, step_of, (uint32_t track))                                        \
    X(uint32_t, armed, (void))                                                   \
    X(uint32_t, scale_mask, (uint32_t track))                                    \
    X(uint32_t, leds, (uint8_t *out, uint32_t max))                              \
    X(uint32_t, nlanes, (uint32_t track))                                        \
    X(const char *, lane_name, (uint32_t track, uint32_t lane))                  \
    X(int32_t, arr_do, (uint32_t op, uint32_t arg))                              \
    X(uint32_t, arr_state, (uint8_t *out, uint32_t max))                         \
    X(int32_t, arr_chain, (const uint8_t *entries, uint32_t count, int loop))    \
    X(uint32_t, object_max, (void))                                              \
    X(int32_t, object_get, (uint32_t id, uint8_t *out, uint32_t max))            \
    X(uint32_t, object_put, (uint32_t id, const uint8_t *data, uint32_t len))

#endif

/* the declarations of one copy: include felucca_core_api.h with FEL_PREFIX defined */
