/* SPDX-License-Identifier: GPL-3.0-only */
/* NOTES uses the original onset interval, not the rounded STEP overview.
 * Selection is ordered by onset, pitch, then storage index: repeated pitches
 * and independent chord notes are individually reachable. Selection never edits note data. */
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
    notes_preview();
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

/* One editor handles both ordinary step notes and precise performance events. */
static int notes_have_recording(const track_t *t)
{
    for (uint32_t i = recording_head[recording_owner(t)]; i < RECORD_MAX; i = recording_next[i])
        if (recording_active(t, i)) return 1;
    return 0;
}
static uint32_t notes_manual_start(const track_t *t)
{
    uint32_t at = step_note_start(t, ui.cursor);
    return at < NSTEP && !(t->step[at].flags & SF_RECORDED) ? at : NSTEP;
}
static uint32_t notes_manual_slot(const step_t *st)
{
    return st->n ? (ui.note_slot < st->n ? ui.note_slot : st->n - 1u) : 0u;
}
/* Jog through every note within an interval, then into the next interval.
 * Empty intervals remain reachable for entry. Cursor movement never edits. */
static void notes_last(void)
{
    uint32_t chosen = RECORD_MAX;
    for (uint32_t i = recording_head[recording_owner(TSEL)]; i < RECORD_MAX; i = recording_next[i])
        if (notes_in_step(TSEL, i) && (chosen == RECORD_MAX || notes_before(chosen, i))) chosen = i;
    ui.note_pick = chosen < RECORD_MAX ? (uint16_t)(chosen + 1u) : 0;
}
/* KNOB 1 on NOTES (SELECT turns the pages, Kerem 2026-10-10): a step's notes one by one, then the next step, an empty
 * one too; back: the step before, on its last note. Moving never edits */
static void notes_step_jog(int32_t delta)
{
    delta = clamp(delta, -64, 64);
    while (delta) {
        int dir = delta > 0 ? 1 : -1;
        uint32_t chosen = notes_selected(TSEL), at = notes_manual_start(TSEL);
        delta -= dir;
        if (chosen < RECORD_MAX) {                      /* recorded notes: the next one in this step */
            notes_cycle(dir);
            if (notes_selected(TSEL) != chosen)
                continue;
        } else if (at < NSTEP && TSEL->step[at].n &&
                   (dir > 0 ? ui.note_slot + 1u < TSEL->step[at].n : ui.note_slot > 0u)) {
            ui.note_slot = (uint8_t)(ui.note_slot + dir);   /* a chord's next note */
            continue;
        }
        cursor_set(ui.cursor + dir);                    /* the next step */
        if (dir < 0) {
            at = notes_manual_start(TSEL);
            notes_last();
            if (at < NSTEP && TSEL->step[at].n)
                ui.note_slot = (uint8_t)(TSEL->step[at].n - 1u);
        }
    }
    notes_preview();
}

static void notes_preview(void)
{
    if(!settings_preview || song.playing || seq_counting() || ui.home || ui.menu || ui.layer || ui.entry_open) return;
    const page_t *pg=cur_page(); if(pg->graph!=GR_ROLL && pg->graph!=GR_STEPS && pg->graph!=GR_DRUMHIT) return;
    uint8_t nn[12],vv[12];uint32_t count=0,chosen=notes_selected(TSEL);
    if(pg->scope==SC_DRUMHIT)chosen=drum_hit_selected();
    int32_t pitch=0;uint32_t gate=0;
    if(chosen<RECORD_MAX) { const recorded_note_t *r=&recording[chosen];nn[count]=r->note;vv[count++]=r->vel;
        if(drum_track(TSEL)){pitch=r->pitch;if(r->length)gate=(uint32_t)(((uint64_t)r->duration<<(r->owner>>5))*seq_div_samples(TSEL->p[P_SDIV])/RECORD_UNIT);}
    }
    else {
        uint32_t at=step_note_start(TSEL,ui.cursor);
        if(at<NSTEP) { const step_t *st=&TSEL->step[at];
            if(pg->graph==GR_ROLL && st->n) { nn[count]=st->note[notes_manual_slot(st)];vv[count++]=st->vel?st->vel:96; }
            else for(uint32_t i=0;i<st->n;i++){nn[count]=st->note[i];vv[count++]=st->vel?st->vel:96;}
            if(drum_track(TSEL))for(uint32_t i=0;i<NLANE;i++)if(step_lanes(st)&(1u<<i)){nn[count]=DRUM_LANE_NOTE[i];vv[count++]=(step_accents(st)&(1u<<i))?127:96;}
        }
    }
    fm1_irq_off(); audition_track=song.sel;audition_count=(uint8_t)count;audition_pitch=pitch;audition_gate=gate;
    memcpy(audition_notes,nn,count);memcpy(audition_vel,vv,count);audition_request=1;fm1_irq_on();
}
