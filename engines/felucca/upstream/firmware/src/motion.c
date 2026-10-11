/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Sparse, bounded step automation. Main-loop writes publish under the same
 * interrupt guard as parameter batches. The ISR only scans MOTION_MAX fixed records (128 since 1.2, FUN10; 64 before).
 * p[] is the sounding value; motion_base_value() is the patch's saved value.
 * Two kinds of record share the MOTION_MAX (core.h MOTION_LOCK): an automation event sets its value at its step and the value
 * holds (to the next event, the loop's restart, a stop); a parameter lock (1.1, Discussions #75 / #53: hold a step,
 * turn a knob) sets its value at its step only when the step plays (its chance passed) and the next step puts back
 * what would sound without it: the latest automation event of the pass up to that step, else the sound's own value
 * (motion_unlock). A lock lasts the whole step, its RATCH parts too; swing only moves the step's start. */
static motion_store_t motion;
static int16_t motion_base[NTRK][P_COUNT];
static uint32_t motion_active[NTRK][(P_COUNT + 31u) / 32u];
static uint32_t motion_locked[NTRK][(P_COUNT + 31u) / 32u];   /* the ids a lock holds now (put back at the next step) */
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
    return id < P_COUNT && (id <= P_REL || (id >= P_ED_FLT && id <= P_LD_AMP) ||
        (id >= P_DIST && id <= P_REV) || id == P_GLIDE || id == P_PAN ||
        id == P_DETUNE || (id >= P_FM1_ATK && id <= P_FM4_LEVEL) || (id >= P_LN0 && id <= P_LN7) ||
        id == P_SPRD || (id >= P_ITYPE && id <= P_IMIX) || id >= P_E0);   /* (not the chord keys, nor 1.2's LFO SYNC TRIG
                                        * POL and QUANTIZE, nor 1.5's filter TYPE and ENV SYNC: settings; the DRUM lane
                                        * levels, SPREAD (#148: continuous, as PAN), 1.5's INSERT (TYPE too: a lock
                                        * switches it for a step, fx.c track_insert fades over), E0..E7) */
}
static int motion_valid(const motion_store_t *m)
{
    uint32_t i, j;
    if (m->count > MOTION_MAX || (m->on & ~((1u << NTRK) - 1u))) return 0;
    for (i = 0; i < m->count; i++) {
        const motion_event_t *e = &m->event[i];
        if (!motion_param(MOTION_ID(e)) || e->value < -64 || e->value > 127) return 0;
        for (j = 0; j < i; j++)
            if (m->event[j].place == e->place && MOTION_ID(&m->event[j]) == MOTION_ID(e)) return 0;
    }
    return 1;
}
static int motion_enabled(const track_t *t) { return (motion.on >> trk_index(t)) & 1u; }
static uint32_t motion_count(const track_t *t)
{
    uint32_t i, n = 0, k = trk_index(t);
    for (i = 0; i < motion.count; i++) n += (motion.event[i].place >> 6) == k;
    return n;
}
/* track t's parameter locks (of motion_count) */
static uint32_t motion_lock_count(const track_t *t)
{
    uint32_t i, n = 0, k = trk_index(t);
    for (i = 0; i < motion.count; i++) n += (motion.event[i].place >> 6) == k && (motion.event[i].param & MOTION_LOCK);
    return n;
}
/* the steps of track k that hold a lock, bit s = step s (the roll and the grid mark them) */
static uint64_t motion_lock_steps(uint32_t k)
{
    uint32_t i;
    uint64_t m = 0;
    for (i = 0; i < motion.count; i++)
        if ((motion.event[i].place >> 6) == k && (motion.event[i].param & MOTION_LOCK))
            m |= (uint64_t)1 << (motion.event[i].place & 63u);
    return m;
}
/* the lock of track t on step / id: 1 and its value in *v, 0 = none */
static int motion_lock_get(const track_t *t, uint32_t step, uint32_t id, int16_t *v)
{
    uint32_t i, place = trk_index(t) << 6 | step;
    for (i = 0; i < motion.count; i++)
        if (motion.event[i].place == place && motion.event[i].param == (id | MOTION_LOCK)) {
            *v = motion.event[i].value;
            return 1;
        }
    return 0;
}
/* the parameters track k's MOTION will change (PLAY ON, at least one event): a bit per P_* id in m[].
 * Main loop only (the ISR never writes the events); at most MOTION_MAX records, once per card redraw. */
static uint32_t motion_mask(uint32_t k, uint32_t m[(P_COUNT + 31u) / 32u])
{
    uint32_t i, any = 0;
    memset(m, 0, ((P_COUNT + 31u) / 32u) * sizeof m[0]);
    if (k >= NTRK || !((motion.on >> k) & 1u)) return 0;
    for (i = 0; i < motion.count; i++) {
        const motion_event_t *e = &motion.event[i];
        if ((e->place >> 6) != k || MOTION_ID(e) >= P_COUNT) continue;
        m[MOTION_ID(e) / 32u] |= 1u << (MOTION_ID(e) % 32u);
        any = 1;
    }
    return any;
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
    memset(motion_locked[k], 0, sizeof motion_locked[k]);
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
        if ((motion.event[i].place >> 6) != k) motion.event[n++] = motion.event[i];
    memset(motion.event + n, 0, (MOTION_MAX - n) * sizeof motion.event[0]);
    motion.count = (uint8_t)n;
    motion.on &= (uint8_t)~(1u << k);
    motion_rebase(t);
    motion_full = 0;
    motion_unguard(f);
}
static void motion_reset(track_t *t) { motion_clear(t); }
/* a record of kind 0 (automation) or MOTION_LOCK on track t's step / id: a new one, or the one there (of either kind)
 * becomes this. 0 ok, 1 invalid, 2 the MOTION_MAX are used (motion_full: "AUTOMATION FULL") */
static int motion_put(track_t *t, uint32_t step, uint32_t id, int16_t value, uint32_t kind)
{
    uint32_t k = trk_index(t), i, f;
    const param_desc_t *d;
    if (k >= NTRK || step >= NSTEP || !motion_param(id)) return 1;
    d = param_desc_of(eng_idx(t->eng_req), id);
    if (value < d->min || value > d->max || value < -64 || value > 127) return 1;
    value = (int16_t)param_fit(d, value);                    /* (a retired KIT: the kit it plays) */
    f = motion_guard();
    for (i = 0; i < motion.count; i++)
        if (motion.event[i].place == (k << 6 | step) && MOTION_ID(&motion.event[i]) == id) break;
    if (i == MOTION_MAX) { motion_full = 1; motion_unguard(f); return 2; }
    motion.event[i].place = (uint8_t)(k << 6 | step);
    motion.event[i].param = (uint8_t)(id | kind);
    motion.event[i].value = (int8_t)value;
    RING_PUBLISH();
    if (i == motion.count) motion.count++;
    motion.on |= (uint8_t)(1u << k);
    motion_unguard(f);
    return 0;
}
static int motion_set_event(track_t *t, uint32_t step, uint32_t id, int16_t value) { return motion_put(t, step, id, value, 0); }
static int motion_set_lock(track_t *t, uint32_t step, uint32_t id, int16_t value)
{
    return motion_put(t, step, id, value, MOTION_LOCK);
}
static void motion_delete_event(track_t *t, uint32_t step, uint32_t id)   /* (either kind) */
{
    uint32_t f = motion_guard(), place = trk_index(t) << 6 | step, i, n = 0;
    motion_restore(t);
    for (i = 0; i < motion.count; i++)
        if (motion.event[i].place != place || MOTION_ID(&motion.event[i]) != id) motion.event[n++] = motion.event[i];
    memset(motion.event + n, 0, (MOTION_MAX - n) * sizeof motion.event[0]);
    motion.count = (uint8_t)n;
    motion_unguard(f);
}
/* the locks of track t's step (NSTEP and up: of every step); their automation events stay. A lock sounding now goes
 * back at the next step as always (motion_locked). The locks it removed */
static uint32_t motion_clear_locks(track_t *t, uint32_t step)
{
    uint32_t f = motion_guard(), k = trk_index(t), i, n = 0, gone;
    for (i = 0; i < motion.count; i++) {
        const motion_event_t *e = &motion.event[i];
        if ((e->place >> 6) == k && (e->param & MOTION_LOCK) && (step >= NSTEP || (e->place & 63u) == step)) continue;
        motion.event[n++] = *e;
    }
    gone = motion.count - n;
    memset(motion.event + n, 0, (MOTION_MAX - n) * sizeof motion.event[0]);
    motion.count = (uint8_t)n;
    motion_unguard(f);
    return gone;
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
    period = div_samples((uint32_t)t->p[P_SDIV]);
    f = motion_guard();
    idx = t->seq_idx % len;
    if (t->seq_pos < 0x7FFFFFFFu && t->seq_pos > step_samples(t, period, idx) / 2u) idx = (idx + 1u) % len;
    /* Mark the immediate knob value transient too, so a stop before its next
     * quantized step still restores the original patch. */
    if ((motion_base_valid >> k) & 1u) motion_active[k][id / 32u] |= 1u << (id % 32u);
    motion_unguard(f);
    return motion_set_event(t, idx, id, value);
}
/* step `step` starts: the locks of the step before go back to what sounds without them: the latest automation event
 * of track t's id at a step up to this one in this pass (motion_restore at step 0 starts each pass), else the sound's
 * own value */
static __attribute__((noinline)) void motion_unlock(track_t *t, uint32_t step, const motion_store_t *m)
{
    uint32_t k = trk_index(t), id, i;
    for (id = 0; id < P_COUNT; id++) {
        int32_t best = -1;
        int16_t v;
        if (!((motion_locked[k][id / 32u] >> (id % 32u)) & 1u))
            continue;
        v = motion_base[k][id];
        for (i = 0; i < m->count; i++) {
            const motion_event_t *e = &m->event[i];
            int32_t s = (int32_t)(e->place & 63u);
            if ((e->place >> 6) != k || e->param != id || s > (int32_t)step || s <= best)
                continue;                               /* (e->param == id: automation only, no MOTION_LOCK) */
            best = s;
            v = (int16_t)param_fit(param_desc_of(eng_idx(t->eng_req), id), e->value);
        }
        t->p[id] = v;
    }
    memset(motion_locked[k], 0, sizeof motion_locked[k]);
}
/* step `step` of track t starts (seq.c seq_tick); play: it plays (its chance passed): its locks apply */
static __attribute__((noinline)) void motion_step(track_t *t, uint32_t step, const motion_store_t *m, uint32_t play)
{
    uint32_t k = trk_index(t), i, w, any = 0;
    if (!((m->on >> k) & 1u)) return;
    if (!((motion_base_valid >> k) & 1u)) {
        memcpy(motion_base[k], t->p, sizeof t->p);
        motion_base_valid |= (uint8_t)(1u << k);
    }
    for (w = 0; w < (P_COUNT + 31u) / 32u; w++)
        any |= motion_locked[k][w];
    if (!step) motion_restore(t);
    else if (any) motion_unlock(t, step, m);
    for (i = 0; i < m->count; i++) {
        const motion_event_t *e = &m->event[i];
        uint32_t id = MOTION_ID(e);
        if (e->place != (k << 6 | step) || ((e->param & MOTION_LOCK) && !play)) continue;
        const param_desc_t *d = param_desc_of(eng_idx(t->eng_req), id);
        t->p[id] = (int16_t)param_fit(d, e->value);
        motion_active[k][id / 32u] |= 1u << (id % 32u);
        if (e->param & MOTION_LOCK)
            motion_locked[k][id / 32u] |= 1u << (id % 32u);
    }
}
static void motion_snapshot_track(track_t *t, motion_store_t *out)
{
    uint32_t f = motion_guard(), k = trk_index(t), i;
    memset(out, 0, sizeof *out);
    out->on = motion.on & (uint8_t)(1u << k);
    for (i = 0; i < motion.count; i++)
        if ((motion.event[i].place >> 6) == k) out->event[out->count++] = motion.event[i];
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
    motion.count += in->count;
    motion.on |= in->on & (uint8_t)(1u << k);
    motion_unguard(f);
    return 0;
}

/* ---- 1.1.5: SEQ > AUTOMATION (ui_events.c ev_*): the records of a track as a list, edited one by one ---- */
/* the record of track t on step / id (either kind): its index, -1 none */
static int32_t motion_find(const track_t *t, uint32_t step, uint32_t id)
{
    uint32_t i, place = trk_index(t) << 6 | step;
    for (i = 0; i < motion.count; i++)
        if (motion.event[i].place == place && MOTION_ID(&motion.event[i]) == id)
            return (int32_t)i;
    return -1;
}
/* the indices of track k's records in list order (by step, then by id): n of them into idx[MOTION_MAX] */
static uint32_t motion_rows(uint32_t k, uint8_t *idx)
{
    uint32_t i, j, n = 0;
    for (i = 0; i < motion.count; i++) {
        const motion_event_t *e = &motion.event[i];
        uint32_t key = (uint32_t)(e->place & 63u) << 7 | MOTION_ID(e);
        if ((e->place >> 6) != k)
            continue;
        for (j = n; j > 0u; j--) {                       /* (insertion: at most MOTION_MAX) */
            const motion_event_t *o = &motion.event[idx[j - 1u]];
            if (((uint32_t)(o->place & 63u) << 7 | MOTION_ID(o)) <= key)
                break;
            idx[j] = idx[j - 1u];
        }
        idx[j] = (uint8_t)i;
        n++;
    }
    return n;
}
/* record i of track t moves to step / id with value v (its kind kept): 0 ok, 1 invalid or another record is there.
 * What it set sounds until the next step (motion_restore puts back the sound's values, as a delete does) */
static int motion_move(track_t *t, uint32_t i, uint32_t step, uint32_t id, int16_t v)
{
    const param_desc_t *d;
    int32_t at;
    uint32_t f;
    if (i >= motion.count || (motion.event[i].place >> 6) != trk_index(t) || step >= NSTEP || !motion_param(id))
        return 1;
    at = motion_find(t, step, id);
    if (at >= 0 && (uint32_t)at != i)
        return 1;
    d = param_desc_of(eng_idx(t->eng_req), id);
    if (v < d->min || v > d->max || v < -64 || v > 127)
        return 1;
    f = motion_guard();
    if (motion.event[i].place != (trk_index(t) << 6 | step) || MOTION_ID(&motion.event[i]) != id)
        motion_restore(t);                              /* (a value alone: what sounds stays) */
    motion.event[i].place = (uint8_t)(trk_index(t) << 6 | step);
    motion.event[i].param = (uint8_t)((motion.event[i].param & MOTION_LOCK) | id);
    motion.event[i].value = (int8_t)param_fit(d, v);
    motion_unguard(f);
    return 0;
}
/* an id whose meaning is the engine's own: EDIT's E1..E8, DIGITAL's operators, DRUM's lane levels */
static int motion_engine_id(uint32_t id)
{
    return (id >= P_E0 && id <= P_E7) || (id >= P_FM1_ATK && id <= P_FM4_LEVEL) || (id >= P_LN0 && id <= P_LN7);
}
/* a sound load put engine `to` where `from` was: when they differ, track t's records on the engine's own ids go (their
 * meaning changed); the ones on the common parameters (LEVEL, ENV, LFO, FILTER ENV, the sends, GLIDE, PAN, DETUNE)
 * stay, both kinds. The records it removed */
static uint32_t motion_sound_loaded(track_t *t, uint32_t from, uint32_t to)
{
    uint32_t f, k = trk_index(t), i, n = 0, gone;
    if (eng_idx(from) == eng_idx(to))
        return 0;
    f = motion_guard();
    for (i = 0; i < motion.count; i++) {
        const motion_event_t *e = &motion.event[i];
        if ((e->place >> 6) == k && motion_engine_id(MOTION_ID(e)))
            continue;
        motion.event[n++] = *e;
    }
    gone = motion.count - n;
    memset(motion.event + n, 0, (MOTION_MAX - n) * sizeof motion.event[0]);
    motion.count = (uint8_t)n;
    motion_unguard(f);
    return gone;
}
