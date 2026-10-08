/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* A note's length on SEQ > STEP (ui_input.c): its onset step and the TIE steps after it, as the sequencer plays
 * them: no project or protocol changes. ENV + SELECT resizes it, SCL + SELECT moves it, FX or EDIT deletes it with its
 * ties. Hold keys or MIDI notes, turn SELECT, then release to advance past the note. Wraps over the pattern's loop (LEN). UI only;
 * the caller keeps an edit whole for the audio ISR (seq_undo.c). A note: a NOTE step with notes (ui.c step_on) */

static uint32_t step_pattern_len(const track_t *t)
{
    return (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP);
}

/* Resolve any tied step to its onset, including across the loop. A rest or
 * an orphan tie has no onset (NSTEP). */
static uint32_t step_note_start(const track_t *t, uint32_t at)
{
    uint32_t len = step_pattern_len(t), i;
    if (at >= len)
        return NSTEP;
    for (i = 0; i < len; i++) {
        const step_t *st = &t->step[at];
        if (step_on(st))
            return at;
        if (st->time != ST_TIE)
            break;
        at = (at + len - 1u) % len;
    }
    return NSTEP;
}

static uint32_t step_note_length(const track_t *t, uint32_t start)
{
    uint32_t len = step_pattern_len(t), n = 1;
    if (start >= len || !step_on(&t->step[start]))
        return 0;
    while (n < len && t->step[(start + n) % len].time == ST_TIE)
        n++;
    return n;
}

/* Delete the whole selected note, even when the cursor is on its tie tail.
 * An orphan tie still clears its remaining tail; a rest clears just itself. */
static void step_delete(track_t *t, uint32_t at)
{
    uint32_t len = step_pattern_len(t), start = step_note_start(t, at), i;
    if (at >= len)
        return;
    if (start < NSTEP)
        at = start;
    step_clear(&t->step[at]);
    for (i = 1; i < len; i++) {
        step_t *st = &t->step[(at + i) % len];
        if (st->time != ST_TIE)
            break;
        step_clear(st);
    }
}

/* Move a complete note through free space, with loop wrap. Walk each requested
 * step so a fast turn cannot jump over another note. Preserve every step's
 * data, including chord, velocity and flags. The caller makes the edit atomic. */
static uint32_t step_note_move(track_t *t, uint32_t start, int32_t delta)
{
    uint32_t len = step_pattern_len(t), n = step_note_length(t, start), target = start, i;
    step_t saved[NSTEP];
    int32_t dir = delta > 0 ? 1 : -1;
    if (!n)
        return NSTEP;
    delta = clamp(delta, -(int32_t)len, (int32_t)len);
    while (delta) {
        uint32_t next = (target + len + dir) % len;
        uint32_t edge = dir > 0 ? (next + n - 1u) % len : next;
        uint32_t after = (next + n) % len;
        /* Only one new step enters the span on each move. Source steps will
         * be cleared. Keep the search linear while audio interrupts pause. */
        if (((edge + len - start) % len >= n && (step_on(&t->step[edge]) || t->step[edge].time == ST_TIE)) ||
            ((after + len - start) % len >= n && t->step[after].time == ST_TIE))
            break;
        target = next;
        delta -= dir;
    }
    if (target == start)
        return start;
    for (i = 0; i < n; i++)
        saved[i] = t->step[(start + i) % len];
    saved[0].flags &= (uint8_t)~SF_RECORDED;
    for (i = 0; i < n; i++)
        step_clear(&t->step[(start + i) % len]);
    for (i = 0; i < n; i++)
        t->step[(target + i) % len] = saved[i];
    return target;
}

/* Extend through empty steps, stopping before existing notes. Shrinking only
 * clears this note's old tail. Never join a separate orphan tie chain. */
static uint32_t step_note_resize(track_t *t, uint32_t start, int32_t wanted)
{
    uint32_t len = step_pattern_len(t), old = step_note_length(t, start), n, limit, i;
    if (!old)
        return 0;
    limit = old;
    while (limit < len) {
        const step_t *st = &t->step[(start + limit) % len];
        if (step_on(st) || st->time == ST_TIE || t->step[(start + limit + 1u) % len].time == ST_TIE)
            break;
        limit++;
    }
    n = (uint32_t)clamp(wanted, 1, (int32_t)limit);
    t->step[start].flags &= (uint8_t)~SF_RECORDED;
    for (i = n; i < old; i++)
        step_clear(&t->step[(start + i) % len]);
    for (i = old; i < n; i++) {
        step_t *st = &t->step[(start + i) % len];
        step_clear(st);
        st->time = ST_TIE;
    }
    return n;
}
