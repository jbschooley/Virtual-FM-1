/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#ifndef MELODEE_STEP_SNAPSHOT_H
#define MELODEE_STEP_SNAPSHOT_H
/* Main-loop history only. Preserve every step field, including unused note
 * bytes and n on rests/ties. Three bounded control fields share one byte;
 * all other bytes remain exact. No variable-size allocation or audio decode. */
typedef struct {
    uint8_t note[4], meta, vel, hit, acc, probability;
} step_snapshot_t;
_Static_assert(sizeof(step_snapshot_t) == 9u, "compact history step");
_Static_assert(ST_REST < 4u && (SF_ACCENT | SF_SLIDE | SF_RECORDED) < 8u,
               "history control fields fit one byte");
static void step_snapshot_put(step_snapshot_t *out, const step_t *in)
{
    for (uint32_t i = 0; i < 4u; i++) out->note[i] = in->note[i];
    out->meta = (uint8_t)(in->n | (in->time << 3) | (in->flags << 5));
    out->vel = in->vel; out->hit = in->hit; out->acc = in->acc; out->probability = in->probability;
}
static step_t step_snapshot_get(const step_snapshot_t *in)
{
    step_t out;
    for (uint32_t i = 0; i < 4u; i++) out.note[i] = in->note[i];
    out.n = in->meta & 7u; out.time = (in->meta >> 3) & 3u; out.flags = in->meta >> 5;
    out.vel = in->vel; out.hit = in->hit; out.acc = in->acc; out.probability = in->probability;
    return out;
}
static void step_snapshot_save(step_snapshot_t *out, const step_t *in)
{
    for (uint32_t i = 0; i < NSTEP; i++) step_snapshot_put(&out[i], &in[i]);
}
static void step_snapshot_load(step_t *out, const step_snapshot_t *in)
{
    for (uint32_t i = 0; i < NSTEP; i++) out[i] = step_snapshot_get(&in[i]);
}
static int step_snapshot_equal(const step_snapshot_t *a, const step_t *b)
{
    for (uint32_t i = 0; i < NSTEP; i++) {
        step_t s = step_snapshot_get(&a[i]);
        if (memcmp(&s, &b[i], sizeof s)) return 0;
    }
    return 1;
}
#endif
