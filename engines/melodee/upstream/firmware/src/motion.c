/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Sparse, bounded step automation. Main-loop writes publish under the same
 * interrupt guard as parameter batches. The ISR only scans 64 fixed records.
 * p[] is the sounding value; motion_base_value() is the patch's saved value. */
static motion_store_t motion;
static int recording_copy(track_t *t, uint32_t source, uint32_t dest, int apply);
static int16_t motion_base[NTRK][P_COUNT];
static uint32_t motion_active[NTRK][(P_COUNT + 31u) / 32u];
static uint8_t motion_base_valid, motion_full;
static uint32_t motion_guard(void)
{
#if defined(FM1_IRQ_TARGET)
    uint32_t f = fm1_icfg();
    fm1_irq_off();
    return f;
#else
    return 0;
#endif
}
static void motion_unguard(uint32_t f)
{
#if defined(FM1_IRQ_TARGET)
    fm1_icfg_set(f);
#else
    (void)f;
#endif
}
static int motion_param(uint32_t id)
{
    /* Sound only: transport, routing, voice allocation and discrete engine
     * changes never become automation. FX sends and continuous mix are safe. */
    return id != P_RECQ && id != P_DLY && id < P_COUNT && (id <= P_REL || (id >= P_ED_FLT && id <= P_LD_AMP) ||
        (id >= P_DIST && id <= P_REV) || id == P_GLIDE || id == P_PAN ||
        id == P_DETUNE || (id >= P_FM1_ATK && id <= P_FM4_LEVEL) || id >= P_E0);   /* (not the chord keys) */
}
static int motion_valid(const motion_store_t *m)
{
    uint32_t i, j;
    if (m->rsv[0] > 1u || m->count > MOTION_MAX || (m->on & ~((1u << NTRK) - 1u))) return 0;
    for (i = 0; i < m->count; i++) {
        const motion_event_t *e = &m->event[i];
        if ((!motion_param(e->param) && e->param != P_RECQ && e->param != P_DLY) || e->value < -64 || e->value > 127) return 0;
        if (!m->rsv[0]) for (j = 0; j < i; j++)
            if (m->event[j].place == e->place && m->event[j].param == e->param) return 0;
    }
    return 1;
}
static int motion_enabled(const track_t *t) { return (motion.on >> trk_index(t)) & 1u; }
static uint32_t motion_count(const track_t *t)
{
    uint32_t i, n = 0, k = trk_index(t);
    for (i = 0; i < motion.count; i++) n += (motion.event[i].place >> 6) == k && motion_pattern[i] == t->pattern;
    return n;
}
static int16_t motion_base_value(const track_t *t, uint32_t id)
{
    uint32_t k = trk_index(t);
    return (motion_active[k][id / 32u] >> (id % 32u)) & 1u ? motion_base[k][id] : t->p[id];
}
static void motion_restore(track_t *t)
{
    uint32_t k = trk_index(t), id, f = motion_guard();
    for (id = 0; id < P_COUNT; id++)
        if ((motion_active[k][id / 32u] >> (id % 32u)) & 1u) t->p[id] = motion_base[k][id];
    memset(motion_active[k], 0, sizeof motion_active[k]);
    motion_unguard(f);
}
static void motion_rebase(track_t *t)
{
    uint32_t k = trk_index(t), f = motion_guard();
    motion_restore(t);
    memcpy(motion_base[k], t->p, sizeof t->p);
    motion_base_valid = (uint8_t)((motion_base_valid & ~(1u << k)) | (song.playing ? 1u << k : 0u));
    motion_unguard(f);
}
static void motion_begin(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++) {
        motion_restore(&trk[k]);
        memcpy(motion_base[k], trk[k].p, sizeof trk[k].p);
    }
    motion_base_valid = (1u << NTRK) - 1u;
}
static void motion_end(void)
{
    for (uint32_t k = 0; k < NTRK; k++) motion_restore(&trk[k]);
    motion_base_valid = 0;
}
static void motion_set_enabled(track_t *t, uint32_t on)
{
    uint32_t f = motion_guard(), b = 1u << trk_index(t);
    motion.on = (uint8_t)(on ? motion.on | b : motion.on & ~b);
    if (!on) motion_restore(t);
    motion_unguard(f);
}
static void motion_clear(track_t *t)
{
    uint32_t f = motion_guard(), k = trk_index(t), i, n = 0;
    motion_restore(t);
    for (i = 0; i < motion.count; i++)
        if ((motion.event[i].place >> 6) != k || motion_pattern[i] != t->pattern) {
            motion_pattern[n] = motion_pattern[i]; motion.event[n++] = motion.event[i];
        }
    memset(motion.event + n, 0, (MOTION_MAX - n) * sizeof motion.event[0]);
    motion.count = (uint8_t)n;
    uint32_t remaining = 0;
    for (i = 0; i < n; i++) remaining |= (motion.event[i].place >> 6) == k;
    if (!remaining) motion.on &= (uint8_t)~(1u << k);
    motion_rebase(t);
    motion_full = 0;
    motion_unguard(f);
}
static void motion_reset(track_t *t) { motion_clear(t); }
static int motion_set_event(track_t *t, uint32_t step, uint32_t id, int16_t value)
{
    uint32_t k = trk_index(t), i, f;
    const param_desc_t *d;
    if (k >= NTRK || step >= NSTEP || !motion_param(id)) return 1;
    d = param_desc_of(eng_idx(t->eng_req), id);
    if (value < d->min || value > d->max || value < -64 || value > 127) return 1;
    f = motion_guard();
    for (i = 0; i < motion.count; i++)
        if (motion_pattern[i] == t->pattern && motion.event[i].place == (k << 6 | step) && motion.event[i].param == id) break;
    if (i == MOTION_MAX) { motion_full = 1; motion_unguard(f); return 2; }
    if (t->pattern) motion.rsv[0] = 1;
    motion_pattern[i] = t->pattern;
    motion.event[i].place = (uint8_t)(k << 6 | step);
    motion.event[i].param = (uint8_t)id;
    motion.event[i].value = value;
    RING_PUBLISH();
    if (i == motion.count) motion.count++;
    motion.on |= (uint8_t)(1u << k);
    motion_unguard(f);
    return 0;
}
static void motion_delete_event(track_t *t, uint32_t step, uint32_t id)
{
    uint32_t f = motion_guard(), place = trk_index(t) << 6 | step, i, n = 0;
    motion_restore(t);
    for (i = 0; i < motion.count; i++)
        if (motion_pattern[i] != t->pattern || motion.event[i].place != place || motion.event[i].param != id) {
            motion_pattern[n] = motion_pattern[i]; motion.event[n++] = motion.event[i];
        }
    memset(motion.event + n, 0, (MOTION_MAX - n) * sizeof motion.event[0]);
    motion.count = (uint8_t)n;
    motion_unguard(f);
}
static int motion_capture(track_t *t, uint32_t id, int16_t value)
{
    uint32_t k = trk_index(t), idx, len, period, f;
    if (!motion_param(id)) return 0;
    if (!rec_on(t) || t != TSEL) {
        /* A live edit becomes a new base, even when an earlier motion value is
         * currently sounding. Subsequent recorded events can still override it. */
        f = motion_guard();
        if ((motion_base_valid >> k) & 1u) motion_base[k][id] = value;
        motion_unguard(f);
        return 0;
    }
    len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP);
    period = seq_div_samples((uint32_t)t->p[P_SDIV]);
    f = motion_guard();
    idx = t->seq_idx % len;
    if (t->seq_pos < 0x7FFFFFFFu && t->seq_pos > step_samples(t, period, idx) / 2u) idx = (idx + 1u) % len;
    /* Mark the immediate knob value transient too, so a stop before its next
     * quantized step still restores the original patch. */
    if ((motion_base_valid >> k) & 1u) motion_active[k][id / 32u] |= 1u << (id % 32u);
    motion_unguard(f);
    return motion_set_event(t, idx, id, value);
}
static __attribute__((noinline)) void motion_step(track_t *t, uint32_t step, const motion_store_t *m)
{
    uint32_t k = trk_index(t), i;
    if (!((m->on >> k) & 1u)) return;
    if (!((motion_base_valid >> k) & 1u)) {
        memcpy(motion_base[k], t->p, sizeof t->p);
        motion_base_valid |= (uint8_t)(1u << k);
    }
    if (!step) motion_restore(t);
    for (i = 0; i < m->count; i++) {
        const motion_event_t *e = &m->event[i];
        if (motion_pattern[i] != t->pattern || e->place != (k << 6 | step)) continue;
        if (e->param == P_RECQ || e->param == P_DLY) continue; /* old inert FX automation stays inert */
        const param_desc_t *d = param_desc_of(eng_idx(t->eng_req), e->param);
        t->p[e->param] = (int16_t)clamp(e->value, d->min, d->max);
        motion_active[k][e->param / 32u] |= 1u << (e->param % 32u);
    }
}
static void motion_snapshot_track(track_t *t, motion_store_t *out)
{
    uint32_t f = motion_guard(), k = trk_index(t), i;
    memset(out, 0, sizeof *out);
    out->on = motion.on & (uint8_t)(1u << k);
    for (i = 0; i < motion.count; i++)
        if ((motion.event[i].place >> 6) == k && motion_pattern[i] == t->pattern) out->event[out->count++] = motion.event[i];
    motion_unguard(f);
}
static int motion_replace_track(track_t *t, const motion_store_t *in)
{
    uint32_t k = trk_index(t), n = motion.count - motion_count(t), f;
    if (!motion_valid(in) || n + in->count > MOTION_MAX) return 2;
    for (uint32_t i = 0; i < in->count; i++) if ((in->event[i].place >> 6) != k) return 1;
    f = motion_guard();
    motion_clear(t);
    memcpy(motion.event + motion.count, in->event, in->count * sizeof in->event[0]);
    memset(motion_pattern + motion.count, t->pattern, in->count);
    motion.count += in->count;
    if (t->pattern && in->count) motion.rsv[0] = 1;
    motion.on |= in->on & (uint8_t)(1u << k);
    motion_unguard(f);
    return 0;
}

static int pattern_request(track_t *t, uint32_t bank)
{
    if (bank >= NPAT) return 1;
    uint32_t f = motion_guard();
    if (chain.running || chain.armed) { motion_unguard(f); return 1; }
    if (song.playing) t->pattern_next = bank == t->pattern ? 0xff : (uint8_t)bank;
    else if (bank != t->pattern) pattern_switch(t, bank);
    motion_unguard(f);
    return 0;
}
static int pattern_copy(track_t *t, uint32_t source, uint32_t dest)
{
    uint32_t k = trk_index(t), i, n = 0, f, extra = 0;
    if (source >= NPAT || dest >= NPAT) return 1;
    if (source == dest) return 0;
    f = motion_guard();
    if (chain.running || chain.armed || (song.playing && dest == t->pattern)) { motion_unguard(f); return 1; }
    for (i = 0; i < motion.count; i++) if ((motion.event[i].place >> 6) == k) {
        if (motion_pattern[i] == source) extra++;
        if (motion_pattern[i] == dest) n++;
    }
    if (motion.count - n + extra > MOTION_MAX) { motion_unguard(f); return 2; }
    if (recording_copy(t, source, dest, 0)) { motion_unguard(f); return 2; }
    pattern_commit(t);
    recording_copy(t, source, dest, 1);
    *pattern_at(k, dest) = *pattern_at(k, source);
    n = 0;
    for (i = 0; i < motion.count; i++) if ((motion.event[i].place >> 6) != k || motion_pattern[i] != dest) {
        motion_pattern[n] = motion_pattern[i]; motion.event[n++] = motion.event[i];
    }
    motion.count = (uint8_t)n;
    motion.rsv[0] = 1;
    for (i = 0; i < n; i++) if ((motion.event[i].place >> 6) == k && motion_pattern[i] == source) {
        motion.event[motion.count] = motion.event[i]; motion_pattern[motion.count++] = (uint8_t)dest;
    }
    if (dest == t->pattern) pattern_apply(t, dest);
    motion_unguard(f);
    return 0;
}
