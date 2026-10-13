/* SPDX-License-Identifier: GPL-3.0-only */
/* EDIT held during live sequence recording erases the selected bank at audio
 * step boundaries. The UI publishes a scoped request; physical release, track
 * changes, disarm and transport stop disable it without waiting for a UI frame. */
static int seq_erase_active(const track_t *t)
{
    uint32_t request = seq_erase_request, buttons = fm1_in.buttons;
    return (request >> 31) && rec_on(t) && song.seq_mode && song.sel == trk_index(t) &&
           (request & 31u) == recording_owner(t) && seq_erase_generation == t->pattern_gen &&
           (buttons & seq_erase_button) && !(buttons & ~((request >> 16) & 0x3FFFu));
}
static void seq_erase_clear(step_t *st)
{
    memset(st, 0, sizeof *st);
    st->time = ST_REST;
}

/* Rebuild all affected rounded overview cells in one pass. A late hit can
 * share its rounded cell with an unvisited interval, including across zero. */
static void seq_erase_rebuild(track_t *t, uint64_t touched)
{
    if (!touched) return;
    uint64_t found = 0;
    for (uint32_t s = 0; s < NSTEP; s++) if ((touched >> s) & 1u) {
        step_t *st = &t->step[s];
        st->n = st->hit = st->acc = st->vel = 0;
        st->flags &= (uint8_t)~SF_ACCENT;
    }
    for (uint32_t i = recording_head[recording_owner(t)]; i < RECORD_MAX; i = recording_next[i]) {
        if (!recording_active(t, i)) continue;
        const recorded_note_t *r = &recording[i];
        uint32_t view = recording_view(t, r);
        if (!((touched >> view) & 1u)) continue;
        found |= (uint64_t)1u << view;
        step_t *st = &t->step[view];
        if (r->vel > st->vel) st->vel = r->vel;
        uint32_t lane = drum_lane(r->note);
        if (drum_track(t) && r->note == DRUM_LANE_NOTE[lane]) {
            st->hit |= (uint8_t)(1u << lane);
            if (r->vel > 110u) st->acc |= (uint8_t)(1u << lane);
        } else {
            uint32_t j;
            for (j = 0; j < st->n && st->note[j] != r->note; j++);
            if (j == st->n && st->n < 4u) st->note[st->n++] = r->note;
        }
        if (st->vel > 110u) st->flags |= SF_ACCENT;
    }
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP);
    for (uint32_t s = 0; s < NSTEP; s++) if (((touched & ~found) >> s) & 1u) {
        seq_erase_clear(&t->step[s]);
        for (uint32_t n = 1; s < len && n < len; n++) {
            step_t *tail = &t->step[(s + n) % len];
            if (tail->time != ST_TIE) break;
            seq_erase_clear(tail);
        }
    }
}

static void seq_erase_pass(track_t *t, uint32_t step, int entered)
{
    uint32_t tr = trk_index(t);
    if (!seq_erase_active(t)) { seq_erase_seen[tr] = 0; return; }
    if (t->seq_pos >= 0x7FFFFFFFu || (seq_erase_seen[tr] && !entered)) return;
    int starting = !seq_erase_seen[tr];
    seq_erase_seen[tr] = 1;
    uint64_t touched = 0;
    for (uint32_t i = recording_head[recording_owner(t)]; i < RECORD_MAX;) {
        uint32_t next = recording_next[i];
        if (recording_active(t, i) && ((recording[i].step & 63u) == step ||
            (starting && (recording_run[i].left || (recording_flags[i] & 1u))))) {
            touched |= (uint64_t)1u << recording_view(t, &recording[i]);
            recording_remove(t, i);
        }
        i = next;
    }
    seq_erase_rebuild(t, touched);
    /* A manual tied chord is one note: remove its onset and its whole tail. */
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), source = step;
    for (uint32_t n = 0; n < len && t->step[source].time == ST_TIE; n++) source = (source + len - 1u) % len;
    if (!(t->step[source].flags & SF_RECORDED)) {
        if (t->step[source].time == ST_NOTE && (t->step[source].n || t->step[source].hit)) step = source;
        seq_erase_clear(&t->step[step]);
        for (uint32_t n = 1; n < len && t->step[(step + n) % len].time == ST_TIE; n++)
            seq_erase_clear(&t->step[(step + n) % len]);
    }
    seq_release(t);
    /* A key already held must not recreate erased ties on its eventual release. */
    t->rh_n = t->rh_ties = t->rskip_n = 0;
    t->rh_elapsed = 0;
}
