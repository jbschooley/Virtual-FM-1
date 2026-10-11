/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Sparse, bounded step automation. Main-loop writes publish under the same
 * interrupt guard as parameter batches. The ISR only scans 64 fixed records.
 * p[] is the sounding value; motion_base_value() is the patch's saved value. */
static motion_store_t motion;
/* Main-loop sound design temporarily auditions the base tone. The saved motion
 * enable bits and event pool remain unchanged. */
static uint8_t motion_design_pause;
static int recording_copy(track_t *t, uint32_t source, uint32_t dest, int apply);
static int16_t motion_base[NTRK][P_COUNT];
static uint32_t motion_active[NTRK][(P_COUNT + 31u) / 32u];
static uint8_t motion_base_valid, motion_full;
/* The engines' own patches move too (what the Prophet's, the CZ-1's and FM6's pages edit, Stage's knobs on the Prophet
 * and the CZ-1): an event's param MO_NATIVE + i names byte i of the track's engine's patch (the Prophet's program, the
 * CZ-1's tone, FM6's voice), its value the engine it was recorded on << 8 | the byte (MO_TAG_*: another engine there
 * leaves it). While motion moves a byte, the patch's own is in motion_nbase (its bit in motion_nactive) */
#define MO_NATIVE 112u
#define MO_NATIVE_N 144u                                /* (FM6: the voice up to TRANSPOSE; the CZ-1: 128; P5: 88) */
enum { MO_TAG_NONE, MO_TAG_P5, MO_TAG_CZ, MO_TAG_FM6 };
_Static_assert(P_COUNT <= MO_NATIVE && MO_NATIVE + MO_NATIVE_N <= 256u, "motion: the native ids after the track's");
static uint8_t motion_nbase[NTRK][MO_NATIVE_N] __attribute__((section(".pool")));   /* (read only under its bit) */
static uint32_t motion_nactive[NTRK][(MO_NATIVE_N + 31u) / 32u];
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
static uint32_t motion_native_tag(const track_t *t)
{
    uint32_t e = t->eng_req;
    return e == ENGI_PROPHET ? MO_TAG_P5 : e == ENGI_CZ ? MO_TAG_CZ : e == ENGI_FM6 ? MO_TAG_FM6 : MO_TAG_NONE;
}
/* byte i of the patch of the track's engine, 0: none motion moves (the Prophet's voice assignment, the CZ-1's line
 * select, FM6's TRANSPOSE and name: what changes the voices themselves) */
static uint8_t *motion_native_at(track_t *t, uint32_t i)
{
    uint32_t k = trk_index(t);
    switch (motion_native_tag(t)) {
#if MELODEE_PROPHET
    case MO_TAG_P5:
        return i < NELEM(P5_PANEL) && P5_PANEL[i].label && i != P5_UNISON && i != P5_UNISON_COUNT &&
               i != P5_UNISON_DETUNE && i != P5_RETRIGGER ? &p5_patch_of(t)->raw[i] : 0;
#endif
    case MO_TAG_CZ:
        return i && i < 128u ? &cz_patch[k].raw[i] : 0;
    case MO_TAG_FM6:
        return i < FP_TRNSP ? &fm6_patch[k][i] : 0;
    default:
        return 0;
    }
}
static int motion_native_ok(uint32_t id, int32_t value)    /* (a store's event: in range, an engine's) */
{
    uint32_t i = id - MO_NATIVE, tag = (uint32_t)value >> 8, v = (uint32_t)value & 255u;
    if (id < MO_NATIVE || id >= MO_NATIVE + MO_NATIVE_N || value < 0) return 0;
    if (tag == MO_TAG_P5) {
#if MELODEE_PROPHET
        return i < NELEM(P5_PANEL) && P5_PANEL[i].label && i != P5_UNISON && i != P5_UNISON_COUNT &&
            i != P5_UNISON_DETUNE && i != P5_RETRIGGER && v >= (uint32_t)P5_PANEL[i].min && v <= (uint32_t)P5_PANEL[i].max;
#else
        return 0;
#endif
    }
    if (tag == MO_TAG_CZ) {
        if (!i || i >= 128u) return 0;
        if (i == 1u) return v <= 1u;
        if (i == 2u) return !(v & 3u);
        if (i == 3u) return v <= 47u;
        if (i == 16u || i == 73u) return (v & 15u) <= 9u && (v >> 4) <= 14u;
        if (i == 18u || i == 75u) return v <= 9u;
        for (uint32_t l = 0; l < 2u; l++)
            for (uint32_t e = 0; e < 3u; e++)
                if (i == CZ_ENV_END[l][e]) return !(v & 8u);
        return 1;
    }
    if (tag == MO_TAG_FM6) {
        static const uint8_t opmax[FP_OP] = {99,99,99,99,99,99,99,99,99,99,99,3,3,7,3,7,99,1,31,99,14};
        static const uint8_t globalmax[FP_TRNSP - FP_PR1] = {99,99,99,99,99,99,99,99,31,7,1,99,99,99,99,1,5,7};
        return i < FP_TRNSP && v <= (i < FP_PR1 ? opmax[i % FP_OP] : globalmax[i - FP_PR1]);
    }
    return 0;
}
static int motion_nbit(uint32_t k, uint32_t i) { return (motion_nactive[k][i / 32u] >> (i % 32u)) & 1u; }
/* byte i to v (motion playing): the patch's own kept first */
static void motion_native_set(track_t *t, uint32_t i, uint32_t v)
{
    uint32_t k = trk_index(t);
    uint8_t *b = motion_native_at(t, i);
    if (!b)
        return;
    if (!motion_nbit(k, i)) {
        motion_nbase[k][i] = *b;
        motion_nactive[k][i / 32u] |= 1u << (i % 32u);
    }
    if (*b != (uint8_t)v) {
        *b = (uint8_t)v;
        if (motion_native_tag(t) == MO_TAG_FM6)
            fm6_pgen[k]++;                              /* (its operators figured again) */
    }
}
static void motion_native_restore(track_t *t)           /* the patch's own bytes back */
{
    uint32_t k = trk_index(t), i, w, any = 0;
    for (w = 0; w < NELEM(motion_nactive[k]); w++)
        any |= motion_nactive[k][w];
    if (!any)
        return;
    for (i = 0; i < MO_NATIVE_N; i++)
        if (motion_nbit(k, i)) {
            uint8_t *b = motion_native_at(t, i);
            if (b)
                *b = motion_nbase[k][i];
        }
    memset(motion_nactive[k], 0, sizeof motion_nactive[k]);
    if (motion_native_tag(t) == MO_TAG_FM6)
        fm6_pgen[k]++;
}
/* Copy the saved patch base over a snapshot of its sounding bytes. */
static void motion_native_snapshot(const track_t *t, uint8_t *raw, uint32_t tag, uint32_t n)
{
    uint32_t k = trk_index(t);
    if (motion_native_tag(t) != tag) return;
    for (uint32_t i = 0; i < n && i < MO_NATIVE_N; i++)
        if (motion_nbit(k, i)) raw[i] = motion_nbase[k][i];
}
static int motion_param(uint32_t id)
{
    /* Sound only: transport, routing, voice allocation and discrete engine
     * changes never become automation. FX sends and continuous mix are safe. */
    return id != P_RECQ && id < P_COUNT && (id <= P_REL || (id >= P_ED_FLT && id <= P_LD_AMP) ||
        (id >= P_DIST && id <= P_REV) || id == P_GLIDE || id == P_PAN ||
        id == P_DETUNE || id == P_SPRD || (id >= P_LN0 && id <= P_LN7) || (id >= P_FM1_ATK && id <= P_FM4_LEVEL) || id >= P_E0);   /* (not the chord keys) */
}
static int motion_valid(const motion_store_t *m)
{
    uint32_t i, j;
    if (m->rsv[0] > 1u || m->count > MOTION_MAX || (m->on & ~((1u << NTRK) - 1u))) return 0;
    for (i = 0; i < m->count; i++) {
        const motion_event_t *e = &m->event[i];
        if (e->param >= MO_NATIVE ? !motion_native_ok(e->param, e->value) :
            (!motion_param(e->param) && e->param != P_RECQ) || e->value < -64 || e->value > 127) return 0;
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
static int motion_has_lanes(const track_t *t)
{
    uint32_t k = trk_index(t), tag = motion_native_tag(t);
    for (uint32_t i = 0; i < motion.count; i++) {
        const motion_event_t *e = &motion.event[i];
        if ((e->place >> 6) == k && motion_pattern[i] == t->pattern && e->param != P_RECQ &&
            (e->param < MO_NATIVE || (uint32_t)e->value >> 8 == tag)) return 1;
    }
    return 0;
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
    motion_native_restore(t);
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
    if (k >= NTRK || step >= NSTEP) return 1;
    if (id >= MO_NATIVE) {
        if (!motion_native_ok(id, value)) return 1;
    } else {
        if (!motion_param(id)) return 1;
        d = param_desc_of(eng_idx(t->eng_req), id);
        if (value < d->min || value > d->max || value < -64 || value > 127) return 1;
    }
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
/* one lane (MOTION's sheet): every event of id on the track's pattern; none left there: the track's motion off */
static void motion_clear_param(track_t *t, uint32_t id)
{
    uint32_t f = motion_guard(), k = trk_index(t), i, n = 0, remaining = 0;
    motion_restore(t);
    for (i = 0; i < motion.count; i++)
        if (motion_pattern[i] != t->pattern || (motion.event[i].place >> 6) != k || motion.event[i].param != id) {
            motion_pattern[n] = motion_pattern[i]; motion.event[n++] = motion.event[i];
        }
    memset(motion.event + n, 0, (MOTION_MAX - n) * sizeof motion.event[0]);
    motion.count = (uint8_t)n;
    for (i = 0; i < n; i++) remaining |= (motion.event[i].place >> 6) == k;
    if (!remaining) motion.on &= (uint8_t)~(1u << k);
    motion_rebase(t);
    motion_full = 0;
    motion_unguard(f);
}
static uint32_t motion_rec_step(track_t *t)             /* the step a move recorded now lands on (the nearer one) */
{
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), period = seq_div_samples((uint32_t)t->p[P_SDIV]);
    uint32_t idx = t->seq_idx % len;
    if (t->seq_pos < 0x7FFFFFFFu && t->seq_pos > step_samples(t, period, idx) / 2u) idx = (idx + 1u) % len;
    return idx;
}
static int motion_capture(track_t *t, uint32_t id, int16_t value)
{
    uint32_t k = trk_index(t), idx, f;
    if (!motion_param(id)) return 0;
    if (!rec_on(t) || t != TSEL) {
        /* A live edit becomes a new base, even when an earlier motion value is
         * currently sounding. Subsequent recorded events can still override it. */
        f = motion_guard();
        if ((motion_base_valid >> k) & 1u) motion_base[k][id] = value;
        motion_unguard(f);
        return 0;
    }
    f = motion_guard();
    idx = motion_rec_step(t);
    /* Mark the immediate knob value transient too, so a stop before its next
     * quantized step still restores the original patch. */
    if ((motion_base_valid >> k) & 1u) motion_active[k][id / 32u] |= 1u << (id % 32u);
    motion_unguard(f);
    return motion_set_event(t, idx, id, value);
}
/* the engine's own patch: its bytes before (motion_native_peek), then an edit; each byte it changed is motion while
 * recording (the patch's own kept to come back at the stop), else, where motion moves it, the patch's own value.
 * The caller holds motion_guard from the peek on (the sequencer moves bytes too). The bytes changed that motion can
 * move: their number */
static void motion_native_peek(track_t *t, uint8_t *old)
{
    uint32_t i;
    for (i = 0; i < MO_NATIVE_N; i++) {
        const uint8_t *b = motion_native_at(t, i);
        old[i] = b ? *b : 0;
    }
}
static uint32_t motion_native_edited(track_t *t, const uint8_t *old)
{
    uint32_t k = trk_index(t), i, idx = 0, rec = rec_on(t) && t == TSEL, tag = motion_native_tag(t), n = 0;
    if (rec) {
        uint32_t extra = 0;
        idx = motion_rec_step(t);
        for (i = 0; i < MO_NATIVE_N; i++) {
            const uint8_t *b = motion_native_at(t, i);
            if (!b || *b == old[i]) continue;
            uint32_t j;
            for (j = 0; j < motion.count; j++)
                if (motion_pattern[j] == t->pattern && motion.event[j].place == (k << 6 | idx) && motion.event[j].param == MO_NATIVE + i) break;
            extra += j == motion.count;
        }
        /* A multi-byte CZ edit must never record only half its patch change. */
        if (motion.count + extra > MOTION_MAX) { motion_full = 1; rec = 0; }
    }
    for (i = 0; i < MO_NATIVE_N; i++) {
        const uint8_t *b = motion_native_at(t, i);
        if (!b || *b == old[i])
            continue;
        n++;
        if (rec) {
            if (!motion_nbit(k, i)) {                   /* (a stop before its step still brings the patch's back) */
                motion_nbase[k][i] = old[i];
                motion_nactive[k][i / 32u] |= 1u << (i % 32u);
            }
            (void)motion_set_event(t, idx, MO_NATIVE + i, (int16_t)(tag << 8 | *b));
        } else if (motion_nbit(k, i)) {
            motion_nbase[k][i] = *b;
        }
    }
    return n;
}
static __attribute__((noinline)) void motion_step(track_t *t, uint32_t step, const motion_store_t *m)
{
    uint32_t k = trk_index(t), i;
    if((motion_design_pause>>k)&1u)return;
    if (!((m->on >> k) & 1u)) return;
    if (!((motion_base_valid >> k) & 1u)) {
        memcpy(motion_base[k], t->p, sizeof t->p);
        motion_base_valid |= (uint8_t)(1u << k);
    }
    if (!step) motion_restore(t);
    for (i = 0; i < m->count; i++) {
        const motion_event_t *e = &m->event[i];
        if (motion_pattern[i] != t->pattern || e->place != (k << 6 | step)) continue;
        if (e->param == P_RECQ) continue; /* old inert FX automation stays inert */
        if (e->param >= MO_NATIVE) {      /* the engine's own patch: its engine's events only */
            if ((uint32_t)e->value >> 8 == motion_native_tag(t)) motion_native_set(t, e->param - MO_NATIVE, (uint32_t)e->value & 255u);
            continue;
        }
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
