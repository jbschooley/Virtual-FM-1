/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#pragma once
#include <stdint.h>
#ifndef MELODEE_AUDIO_PROFILE
#define MELODEE_AUDIO_PROFILE 0
#endif
/* Inclusive TIMER4 spans on CPU0; nested spans must not be added together.
 * CPU1 owns its mailbox statistics, sampled only after completion. */
enum {
    AP_HALF, AP_TIMER5, AP_INPUT, AP_MIDI_RX, AP_USB, AP_EVENTS, AP_ALLOC,
    AP_ATTACK, AP_ENVELOPE, AP_MODULATION, AP_TRACK_CONTROL, AP_PROPHET,
    AP_FM6_CONTROL, AP_FM6, AP_CZ, AP_SID, AP_808, AP_909,
    AP_MIX, AP_FX, AP_OUTPUT, AP_COUNT
};
#if MELODEE_AUDIO_PROFILE
#ifndef AUDIO_PROFILE_TICKS
#ifdef FM1_IRQ_TARGET
#define AUDIO_PROFILE_TICKS() fm1_ticks()
#else
#define AUDIO_PROFILE_TICKS() 0u
#endif
#endif
typedef struct { uint32_t ticks, calls, max_ticks; } audio_profile_span_t;
static audio_profile_span_t audio_profile[AP_COUNT];
static __attribute__((noinline)) void audio_profile_add(uint32_t id, uint32_t ticks)
{
    audio_profile_span_t *s=&audio_profile[id];
    s->ticks+=ticks;s->calls++;
    if(ticks>s->max_ticks)s->max_ticks=ticks;
}
#define AP_START(name) uint32_t name=AUDIO_PROFILE_TICKS()
#define AP_END(id,name) audio_profile_add((id),AUDIO_PROFILE_TICKS()-(name))
#else
#define AP_START(name) ((void)0)
#define AP_END(id,name) ((void)0)
#endif
