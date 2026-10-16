/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef FM1_FM_PATCH_H
#define FM1_FM_PATCH_H
#include "engine.h"
enum { FM1_FM_PROGRAMS = 16 };
/* Expanded DX7 voices; MIDI programs 0..15, panel positions 1..16. */
FM1_DSP_CODE void groove_fm_program(uint8_t patch[156], unsigned program);
const char *groove_fm_program_name(unsigned program);
#endif
