/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include "scales.h"

/* Microtonal notes use the existing 7-bit note identity as a scale-degree address:
 * 60 is degree zero at C4 + ROOT + TRN. This keeps independent releases, chords,
 * recordings and projects intact even when two degrees lie within one semitone.
 * Pitch tables are hundredths of a cent; their last entry is the repeat period. */
static const micro_scale_t *micro_scale(const track_t *t)
{
    int32_t s = t->p[P_SCALE];
    return s >= (int32_t)SCALE_LEGACY && s < (int32_t)SCALE_TOTAL ? &MICRO_SCALE[s - SCALE_LEGACY] : 0;
}
static int micro_active(const track_t *t)
{
    return t->p[P_QUANT] != Q_OFF && t->eng_req != 10u && micro_scale(t) != 0;
}
static int32_t micro_floor(int32_t n, int32_t d)
{
    int32_t q = n / d;
    return q - (n % d < 0);
}
static int32_t micro_pitch(const track_t *t, int32_t note)
{
    const micro_scale_t *s = micro_scale(t);
    int32_t d = note - 60, cycle = micro_floor(d, s->count);
    return (60 + t->p[P_ROOT] + t->p[P_TRANS]) * 10000 +
           cycle * s->pitch[s->count] + s->pitch[d - cycle * s->count];
}
static uint32_t micro_degree_map(const track_t *t, int32_t degree, int32_t offset, int strict)
{
    const micro_scale_t *s = micro_scale(t);
    int32_t note = 60 + degree + offset / 12 * s->count;
    int32_t pitch = micro_pitch(t, note);
    if (note < 0 || note > 127 || (strict && (pitch < 0 || pitch > 1270000)))
        return 255u;
    return (uint32_t)note;
}
static uint32_t micro_snap(const track_t *t, int32_t pitch)
{
    const micro_scale_t *s = micro_scale(t);
    int32_t relative = (pitch - 60 - t->p[P_ROOT] - t->p[P_TRANS]) * 10000;
    int32_t cycle = micro_floor(relative, s->pitch[s->count]), degree = 0;
    relative -= cycle * s->pitch[s->count];
    while (degree + 1 < s->count && s->pitch[degree + 1] <= relative)
        degree++;
    return micro_degree_map(t, cycle * s->count + degree, 0, 1);
}
static uint32_t scale_note_period(const track_t *t)
{
    return micro_active(t) ? micro_scale(t)->count : 12u;
}
