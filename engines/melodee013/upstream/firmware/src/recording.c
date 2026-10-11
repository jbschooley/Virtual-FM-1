/* SPDX-License-Identifier: GPL-3.0-only */
/* A bounded performance store shared by the project's 32 banks. Step notes
 * are its editable overview; the original onsets, velocities and independent
 * releases live here. QNT changes only the playback schedule. */
static recorded_note_t recording[RECORD_MAX];
static struct { uint32_t held, left; } recording_run[RECORD_MAX];
static uint8_t recording_flags[RECORD_MAX]; /* 1: live hold; 2: skip upcoming snapped onset; 4: heard this loop */
static uint32_t recording_fraction[NTRK];
/* Index by bank and cache onset arithmetic outside the steady audio tick. */
#define RECORDING_EMPTY8 RECORD_MAX, RECORD_MAX, RECORD_MAX, RECORD_MAX, RECORD_MAX, RECORD_MAX, RECORD_MAX, RECORD_MAX
static uint16_t recording_head[NTRK * NPAT] = {RECORDING_EMPTY8, RECORDING_EMPTY8, RECORDING_EMPTY8, RECORDING_EMPTY8};
_Static_assert(NTRK * NPAT == 32u, "recording head initializer covers every bank");
#undef RECORDING_EMPTY8
static uint16_t recording_next[RECORD_MAX];
static uint32_t recording_due[RECORD_MAX] __attribute__((section(".pool")));
static uint16_t recording_refs[NTRK][128] __attribute__((section(".pool")));
static struct { uint32_t period, key; } recording_schedule[NTRK];
static volatile uint8_t recording_full;
static uint8_t recording_tracks;
static volatile uint32_t recording_generation; /* ISR or UI changed event storage */

static uint32_t recording_owner(const track_t *t) { return trk_index(t) * NPAT + t->pattern; }
static uint32_t recording_prefix(const track_t *t, uint32_t period, uint32_t idx)
{
    uint32_t a = step_samples(t, period, 0), b = step_samples(t, period, 1);
    return (idx / 2u) * (a + b) + (idx & 1u) * a;
}
static uint32_t recording_loop(const track_t *t, uint32_t period)
{
    return recording_prefix(t, period, (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP));
}
static int recording_valid(const recorded_note_t *r)
{
    return !r->vel || (r->vel <= 127u && r->duration && r->note <= 127u && ((r->step & 128u) || (r->step & 63u) + ((r->step & 64u) != 0u) < NSTEP));
}
static uint32_t recording_view(const track_t *t, const recorded_note_t *r)
{
    (void)t;
    return r->step & 128u ? 0u : (r->step & 63u) + ((r->step & 64u) != 0u);
}
static int recording_present(const recorded_note_t *r)
{
    uint32_t owner = r->owner & 31u, k = owner / NPAT, bank = owner % NPAT;
    const step_t *steps = trk[k].pattern == bank ? trk[k].step : pattern_at(k, bank)->step;
    uint32_t view = recording_view(&trk[k], r);
    return view < NSTEP && steps[view].time == ST_NOTE && (steps[view].flags & SF_RECORDED);
}
static int recording_active(const track_t *t, uint32_t i)
{
    const recorded_note_t *r = &recording[i];
    if (!r->vel || (r->owner & 31u) != recording_owner(t) || (r->step & 63u) >= (uint32_t)t->p[P_SLEN]) return 0;
    uint32_t view = recording_view(t, r);
    return t->step[view].time == ST_NOTE && (t->step[view].flags & SF_RECORDED);
}
static int recording_owns(const track_t *t, uint32_t note)
{
    return recording_refs[trk_index(t)][note & 127u] != 0;
}
static void recording_off(track_t *t, uint32_t note)
{
    if (recording_owns(t, note) || t->arp_note == note ||
        (live_held[trk_index(t)][note >> 5] & (1u << (note & 31u)))) return;
    for (uint32_t k = 0; k < t->seq_n; k++) if (t->seq_notes[k] == note) return;
    trk_note_off(t, note);
}
/* Reference counts keep overlapping same-pitch releases constant-time even
 * when a dense take is snapped to one boundary. */
static void recording_end(track_t *t, uint32_t i)
{
    if (!recording_run[i].left) return;
    recording_run[i].left = 0;
    uint16_t *count = &recording_refs[trk_index(t)][recording[i].note];
    if (*count) --*count;
    recording_off(t, recording[i].note);
}
static void recording_reindex(void)
{
    recording_generation++;
    for (uint32_t k = 0; k < NTRK * NPAT; k++) recording_head[k] = RECORD_MAX;
    recording_tracks = 0;
    memset(recording_refs, 0, sizeof recording_refs);
    for (uint32_t i = RECORD_MAX; i-- > 0;) if (recording[i].vel) {
        uint32_t owner = recording[i].owner & 31u;
        recording_next[i] = recording_head[owner]; recording_head[owner] = (uint16_t)i;
        recording_tracks |= (uint8_t)(1u << (owner / NPAT));
        if (recording_run[i].left && trk[owner / NPAT].pattern == owner % NPAT) recording_refs[owner / NPAT][recording[i].note]++;
    }
    memset(recording_schedule, 0, sizeof recording_schedule);
}
static void recording_unlink(uint32_t i)
{
    if (!recording[i].vel) return;
    uint16_t *link = &recording_head[recording[i].owner & 31u];
    while (*link < RECORD_MAX && *link != i) link = &recording_next[*link];
    if (*link == i) *link = recording_next[i];
}
static void recording_reset(void)
{
    memset(recording, 0, sizeof recording);
    memset(recording_run, 0, sizeof recording_run);
    memset(recording_flags, 0, sizeof recording_flags);
    memset(recording_fraction, 0, sizeof recording_fraction);
    recording_full = 0;
    recording_reindex();
}
static recorded_note_t recording_snapshot(uint32_t i)
{
    recorded_note_t r = recording[i];
    if (recording_flags[i] & 1u) {
        uint32_t held = recording_run[i].held, exponent = 0;
        while (held > 65535u && exponent < 7u) { held >>= 1; exponent++; }
        r.duration = (uint16_t)clamp((int32_t)held, 1, 65535);
        r.owner = (r.owner & 31u) | (exponent << 5);
    }
    return r;
}
static void recording_finish(uint32_t i)
{
    recording[i] = recording_snapshot(i);
    recording_generation++;
    recording_flags[i] &= (uint8_t)~1u;
}
static void recording_stop(track_t *t)
{
    uint32_t owner = recording_owner(t);
    for (uint32_t i = recording_head[owner]; i < RECORD_MAX; i = recording_next[i]) {
        if ((recording_flags[i] & 1u)) recording_finish(i);
        recording_end(t, i);
        recording_flags[i] &= (uint8_t)~6u;
    }
}
/* Onsets are fractions within their original swung step. Durations are
 * nominal-step fractions with a compact exponent. Optional quantization finds the nearest swung grid boundary,
 * considering the end of the loop as another copy of zero. */
static uint32_t recording_raw_on(const track_t *t, const recorded_note_t *r, uint32_t period)
{
    uint32_t loop = recording_loop(t, period);
    uint32_t span = step_samples(t, period, r->step & 63u);
    return (recording_prefix(t, period, r->step & 63u) + (uint32_t)(((uint64_t)r->on * span + RECORD_UNIT / 2u) / RECORD_UNIT)) % loop;
}
static uint32_t recording_on(const track_t *t, const recorded_note_t *r, uint32_t period)
{
    uint32_t loop = recording_loop(t, period), at = recording_raw_on(t, r, period);
    uint32_t q = (uint32_t)t->p[P_RECQ];
    if (!q) return at;
    uint32_t grid = seq_div_samples(q - 1u), a = step_samples(t, grid, 0), b = step_samples(t, grid, 1);
    uint32_t base = at / (a + b) * (a + b), best = 0, distance = at;
    uint32_t candidates[] = {base, base + a, base + a + b, loop};
    for (uint32_t k = 0; k < NELEM(candidates); k++) {
        uint32_t v = candidates[k];
        if (v > loop) continue;
        uint32_t d = v > at ? v - at : at - v;
        if (d < distance || (d == distance && v > best)) { best = v; distance = d; }
    }
    return best == loop ? 0u : best;
}
static void recording_sync(track_t *t, uint32_t period)
{
    uint32_t tr = trk_index(t), key = (uint32_t)t->p[P_RECQ] | (uint32_t)t->p[P_SSWING] << 4 |
        (uint32_t)song.g[G_SWING] << 11 | (uint32_t)t->p[P_SLEN] << 18 | (uint32_t)t->pattern << 25;
    if (recording_schedule[tr].period == period && recording_schedule[tr].key == key) return;
    recording_schedule[tr].period = period; recording_schedule[tr].key = key;
    for (uint32_t i = recording_head[recording_owner(t)]; i < RECORD_MAX; i = recording_next[i])
        recording_due[i] = recording_on(t, &recording[i], period);
}
static int recording_note(track_t *t, uint32_t note, uint32_t vel, uint32_t step)
{
    uint32_t owner = recording_owner(t), period = seq_div_samples((uint32_t)t->p[P_SDIV]);
    uint32_t at = t->seq_pos >= 0x7FFFFFFFu ? 0u : recording_prefix(t, period, t->seq_idx) + t->seq_pos;
    uint32_t actual = t->seq_pos >= 0x7FFFFFFFu ? 0u : t->seq_idx;
    uint32_t span = step_samples(t, period, actual), pos = t->seq_pos >= 0x7FFFFFFFu ? 0u : t->seq_pos;
    uint32_t on = (uint32_t)(((uint64_t)pos * RECORD_UNIT + span / 2u) / span), free = RECORD_MAX;
    if (on > 65535u) on = 65535u;
    for (uint32_t i = 0; i < RECORD_MAX; i++) {
        recorded_note_t *r = &recording[i];
        if ((r->owner & 31u) == owner && (recording_flags[i] & 1u) && r->note == note) recording_finish(i);
        /* Repeated passes replace the same pitch at the same captured time.
         * Distinct hits in one step remain separate events. */
        uint32_t same_time = r->on == on;
        if (r->vel && (r->owner & 31u) == owner && r->note == note && (r->step & 63u) == actual && same_time) free = i;
        else if (free == RECORD_MAX && (!r->vel || (!recording_present(r) && !recording_run[i].left && !(recording_flags[i] & 1u)))) free = i;
    }
    if (free == RECORD_MAX) { recording_full = 1; return 0; }
    if (recording_run[free].left) recording_end(&trk[(recording[free].owner & 31u) / NPAT], free);
    recording_unlink(free);
    recorded_note_t *r = &recording[free];
    *r = (recorded_note_t){(uint16_t)on, 1, (uint8_t)note, (uint8_t)vel, (uint8_t)owner, (uint8_t)(actual | (step != actual ? 64u : 0u) | (step == 0u && actual ? 128u : 0u))};
    recording_next[free] = recording_head[owner]; recording_head[owner] = (uint16_t)free;
    recording_schedule[trk_index(t)].period = 0;
    memset(&recording_run[free], 0, sizeof recording_run[free]);
    recording_flags[free] = 5;
    uint32_t target = recording_on(t, r, period);
    if (t->p[P_RECQ] && (target > at || (!target && at > recording_loop(t, period) / 2u))) recording_flags[free] |= 2u;
    t->step[step].flags |= SF_RECORDED;
    recording_tracks |= (uint8_t)(1u << trk_index(t));
    recording_generation++;
    return 1;
}

/* The UI calls these with audio interrupts paused. Remove only this event,
 * ending its reference-counted voice; history keeps its original bytes. */
static void recording_remove(track_t *t, uint32_t i)
{
    recording_end(t, i);
    recording_unlink(i);
    memset(&recording[i], 0, sizeof recording[i]);
    memset(&recording_run[i], 0, sizeof recording_run[i]);
    recording_flags[i] = 0;
    recording_schedule[trk_index(t)].period = 0;
    recording_generation++;
}
static void recording_restore_note(track_t *t, uint32_t i, recorded_note_t r)
{
    recording[i] = r;
    memset(&recording_run[i], 0, sizeof recording_run[i]);
    recording_flags[i] = 0;
    recording_next[i] = recording_head[recording_owner(t)];
    recording_head[recording_owner(t)] = (uint16_t)i;
    recording_tracks |= (uint8_t)(1u << trk_index(t));
    recording_schedule[trk_index(t)].period = 0;
    recording_generation++;
}

/* Copy recordings atomically with a bank. Check capacity before mutating
 * either the destination's steps, automation or performance. */
static int recording_copy(track_t *t, uint32_t source, uint32_t dest, int apply)
{
    uint32_t src = trk_index(t) * NPAT + source, dst = trk_index(t) * NPAT + dest, count = 0, free = 0;
    for (uint32_t i = 0; i < RECORD_MAX; i++) {
        if (recording[i].vel && (recording[i].owner & 31u) == src) count++;
        if (!recording[i].vel || (recording[i].owner & 31u) == dst) free++;
    }
    if (count > free) return 2;
    if (!apply) return 0;
    for (uint32_t i = 0; i < RECORD_MAX; i++) if ((recording[i].owner & 31u) == dst) {
        recording_end(t, i);
        memset(&recording[i], 0, sizeof recording[i]);
        memset(&recording_run[i], 0, sizeof recording_run[i]); recording_flags[i] = 0;
    }
    for (uint32_t i = 0; i < RECORD_MAX; i++) if (recording[i].vel && (recording[i].owner & 31u) == src)
        for (uint32_t j = 0; j < RECORD_MAX; j++) if (!recording[j].vel) {
            recording[j] = recording_snapshot(i); recording[j].owner = (recording[j].owner & 224u) | dst;
            memset(&recording_run[j], 0, sizeof recording_run[j]); recording_flags[j] = 0;
            break;
        }
    recording_reindex();
    return 0;
}
static void recording_release(track_t *t, uint32_t note)
{
    for (uint32_t i = recording_head[recording_owner(t)]; i < RECORD_MAX; i = recording_next[i])
        if (recording[i].note == note && (recording_flags[i] & 1u))
            recording_finish(i);
}
static void recording_fire(track_t *t, uint32_t i, uint32_t period, uint32_t late)
{
    recorded_note_t *r = &recording[i];
    if (recording_flags[i] & 4u) return;
    recording_flags[i] |= 4u;
    if ((recording_flags[i] & 2u)) { recording_flags[i] &= (uint8_t)~2u; return; }
    if ((recording_flags[i] & 1u)) return;
    uint32_t chance = step_chance(&t->step[recording_view(t, r)]);
    if (chance < 100u && rng() % 100u >= chance) return;
    uint32_t gate = (uint32_t)(((uint64_t)r->duration << (r->owner >> 5)) * period / RECORD_UNIT);
    if (!gate) gate = 1;
    trk_note_on(t, r->note, r->vel);
    uint32_t was = recording_run[i].left != 0;
    recording_run[i].left = gate > late ? gate - late : 0u;
    if (recording_run[i].left && !was) recording_refs[trk_index(t)][r->note]++;
    if (!recording_run[i].left) {
        if (was && recording_refs[trk_index(t)][r->note]) recording_refs[trk_index(t)][r->note]--;
        recording_off(t, r->note);
    }
}
static void recording_zero(track_t *t)
{
    uint32_t period = seq_div_samples((uint32_t)t->p[P_SDIV]);
    uint32_t phase = recording_prefix(t, period, t->seq_idx) + t->seq_pos;
    recording_sync(t, period);
    for (uint32_t i = recording_head[recording_owner(t)]; i < RECORD_MAX; i = recording_next[i]) {
        recording_flags[i] &= (uint8_t)~4u;
        if (recording_active(t, i)) {
            uint32_t on = recording_due[i];
            if (on <= phase) recording_fire(t, i, period, phase - on);
        }
    }
}
static void recording_tick(track_t *t, uint32_t n)
{
    uint32_t period = seq_div_samples((uint32_t)t->p[P_SDIV]), tr = trk_index(t);
    uint32_t a = recording_prefix(t, period, t->seq_idx) + t->seq_pos, b = a + n;
    uint64_t ticks = (uint64_t)n * RECORD_UNIT + recording_fraction[tr];
    recording_fraction[tr] = (uint32_t)(ticks % period);
    uint32_t elapsed = (uint32_t)(ticks / period);
    recording_sync(t, period);
    for (uint32_t i = recording_head[recording_owner(t)]; i < RECORD_MAX; i = recording_next[i]) {
        if ((recording_flags[i] & 1u)) recording_run[i].held = (uint32_t)clamp((int32_t)(recording_run[i].held + elapsed), 0, 8388607);
        if (recording_run[i].left) {
            if (!recording_active(t, i) || recording_run[i].left <= n) {
                recording_end(t, i);
            } else recording_run[i].left -= n;
        }
        if (!(recording_flags[i] & 4u) && recording_active(t, i)) {
            uint32_t on = recording_due[i];
            if (on > a && on <= b) recording_fire(t, i, period, b - on);
        }
    }
}
