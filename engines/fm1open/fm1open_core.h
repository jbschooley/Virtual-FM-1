/* fm1open_core.h -- the API of the FM1 Open core (fm1open_core.c): M-VAVE's FM1 Open V12, as its
 * browser preview builds it, compiled once with its state per instance (fel_state.py, as the
 * Felucca family's cores; see engines/felucca/felucca_core.h for bind() and the state block).
 * Not thread-safe per state: one instance's calls are made one at a time (Fm1OpenEngine's lock). */
#ifndef FM1OPEN_CORE_H
#define FM1OPEN_CORE_H

#include <stdint.h>

#define FEL_CAT2(a, b) a##b
#define FEL_CAT(a, b) FEL_CAT2(a, b)
#define FEL(name) FEL_CAT(FEL_PREFIX, name)

#ifdef __cplusplus
extern "C" {
#endif

enum {
    FO_BLOCK_FRAMES = 128,          /* one render: 128 stereo frames at 44.1 kHz (fm1_block.h) */
    FO_SCREEN = 240,                /* 240 x 240, RGB565 in native byte order */
    FO_PROJECT_BYTES = 4064,        /* an .fm1 project (GROOVE_PROJECT_BYTES) */
    FO_STATE_BYTES = 225,           /* sim_state: views, LEDs at 168..208 */
    FO_PROJECTS_BYTES = 69632,      /* the flash before the sample bank (GROOVE_SAMPLE_OFFSET): projects, settings */
};

#define FM1OPEN_API(X)                                                                            \
    X(void, bind, (void *state))                                                                  \
    X(uint64_t, state_size, (void))                                                               \
    X(int, state_init, (void *state))                                                             \
    X(void, fo_boot, (int clear))                       /* power on; clear: empty flash, factory samples */ \
    X(const float *, fo_render, (void))                 /* the next block, interleaved L R, FO_BLOCK_FRAMES */ \
    X(int, fo_midi, (const uint8_t *bytes, unsigned n)) /* a channel message (1..3 bytes) or SysEx */ \
    X(void, fo_control, (unsigned kind, unsigned index, int value)) /* 0 button, 1 key, 2 encoder, 3 knob */ \
    X(void, fo_release, (void))                         /* every button and key up */            \
    X(const uint16_t *, fo_pixels, (void))                                                        \
    X(uint32_t, fo_frame_revision, (void))                                                        \
    X(const uint8_t *, fo_state, (void))                /* FO_STATE_BYTES */                     \
    X(uint8_t *, fo_flash, (void))                                                                \
    X(uint32_t, fo_flash_size, (void))                                                            \
    X(uint32_t, fo_flash_revision, (void))                                                        \
    X(int, fo_export, (uint8_t *out))                   /* the project playing, FO_PROJECT_BYTES; 0 ok */ \
    X(int, fo_import, (const uint8_t *in, unsigned n))  /* into the project playing; 0 ok */     \
    X(int, fo_factory_project, (unsigned index, uint8_t *out)) /* a demo project's bytes; 0 ok */ \
    X(const char *, fo_factory_name, (unsigned index))  /* null past the last */                 \
    X(const char *, fo_project_name, (void))                                                      \
    X(int, fo_preset, (unsigned slot))                  /* as PRESETS on HOME */                 \
    X(uint32_t, fo_heap_used, (void))                   /* its heap's high-water mark, bytes */

#ifndef FO_NAMES_PROBE
#define FO_DECLARE(ret, name, params) ret FEL(name) params;
FM1OPEN_API(FO_DECLARE)
#undef FO_DECLARE
#endif

#ifdef __cplusplus
}
#endif
#endif
