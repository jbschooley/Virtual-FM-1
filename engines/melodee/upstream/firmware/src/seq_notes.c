/* SPDX-License-Identifier: GPL-3.0-only */
/* NOTES uses the original onset interval, not the rounded STEP overview.
 * Selection is ordered by onset, pitch, then storage index: repeated pitches
 * and independent chord notes are individually reachable. No audio mutation. */
static int notes_in_step(const track_t *t, uint32_t i)
{
    return i < RECORD_MAX && recording_active(t, i) && (recording[i].step & 63u) == ui.cursor;
}
static int notes_before(uint32_t a, uint32_t b)
{
    if (b >= RECORD_MAX) return 1;
    const recorded_note_t *x = &recording[a], *y = &recording[b];
    return x->on != y->on ? x->on < y->on : x->note != y->note ? x->note < y->note : a < b;
}
static uint32_t notes_selected(const track_t *t)
{
    uint32_t chosen = ui.note_pick ? ui.note_pick - 1u : RECORD_MAX;
    if (ui.note_track != trk_index(t) || ui.note_pattern_gen != t->pattern_gen ||
        (ui.note_generation != recording_generation &&
         (chosen >= RECORD_MAX || memcmp(&ui.note_identity, &recording[chosen], sizeof ui.note_identity)))) {
        ui.note_pick = 0;
        chosen = RECORD_MAX;
    }
    ui.note_track = (uint8_t)trk_index(t);
    ui.note_pattern_gen = t->pattern_gen;
    ui.note_generation = recording_generation;
    if (!notes_in_step(t, chosen)) {
        chosen = RECORD_MAX;
        for (uint32_t i = recording_head[recording_owner(t)]; i < RECORD_MAX; i = recording_next[i])
            if (notes_in_step(t, i) && notes_before(i, chosen)) chosen = i;
        ui.note_pick = chosen < RECORD_MAX ? (uint16_t)(chosen + 1u) : 0;
    }
    if (chosen < RECORD_MAX) ui.note_identity = recording[chosen];
    return chosen;
}
static uint32_t notes_rank(const track_t *t, uint32_t chosen, uint32_t *count)
{
    uint32_t rank = 0;
    *count = 0;
    for (uint32_t i = recording_head[recording_owner(t)]; i < RECORD_MAX; i = recording_next[i])
        if (notes_in_step(t, i)) {
            (*count)++;
            if (chosen < RECORD_MAX && notes_before(i, chosen)) rank++;
        }
    return chosen < RECORD_MAX ? rank + 1u : 0u;
}
static void notes_cycle(int32_t delta)
{
    uint32_t chosen = notes_selected(TSEL);
    if (chosen >= RECORD_MAX) return;
    delta = clamp(delta, -16, 16);
    while (delta) {
        uint32_t next = RECORD_MAX;
        for (uint32_t i = recording_head[recording_owner(TSEL)]; i < RECORD_MAX; i = recording_next[i]) {
            if (!notes_in_step(TSEL, i)) continue;
            if (delta > 0 ? notes_before(chosen, i) : notes_before(i, chosen))
                if (next == RECORD_MAX || (delta > 0 ? notes_before(i, next) : notes_before(next, i))) next = i;
        }
        if (next == RECORD_MAX) break;
        chosen = next;
        delta += delta > 0 ? -1 : 1;
    }
    ui.note_pick = (uint16_t)(chosen + 1u);
}
static uint32_t notes_span(void) { return 16u >> ui.note_zoom; }
static uint32_t notes_base(void) { return ui.cursor / notes_span() * notes_span(); }

/* Rebuild only the deleted event's rounded overview cell. Other events in
 * that cell keep their timing and their playback ownership. Emptying it
 * clears its tie tail as ordinary STEP deletion does. */
static void notes_rebuild(track_t *t, uint32_t view)
{
    step_t *st = &t->step[view];
    uint32_t count = 0;
    st->n = st->hit = st->acc = st->vel = 0;
    for (uint32_t i = recording_head[recording_owner(t)]; i < RECORD_MAX; i = recording_next[i]) {
        const recorded_note_t *r = &recording[i];
        if (!recording_active(t, i) || recording_view(t, r) != view) continue;
        count++;
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
    }
    if (!count) step_delete(t, view);
    else if (st->vel > 110u) st->flags |= SF_ACCENT;
    else st->flags &= (uint8_t)~SF_ACCENT;
}
