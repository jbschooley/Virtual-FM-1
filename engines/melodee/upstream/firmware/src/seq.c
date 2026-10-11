/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Keyboard, scale, arpeggiator, sequencer and transport.
 * Everything here runs in the audio ISR, once per CTL-sample block, and
 * ends in trk_note_on / trk_note_off: engines never see where a note came from.
 * Four tracks, one transport: every track's pattern loops on its own LEN / DIV /
 * SWING / GATE (polymeter). The keys play the selected track; MIDI IN by GLO > SYSTEM ROUT (G_ROUTE):
 * CH1-4 (0) channels 1..4 play parts 1..4, any other channel the selected track; SEL (1) every channel
 * the selected track. Their CC1 (mod wheel), CC11 (expression) and channel aftertouch go to the same
 * track's modulation matrix (mod.c; the selected track: the one selected then).
 * A note into an armed track (song.rec) while
 * the transport runs preserves its original onset and independent held length
 * (recording.c). The STEP overview uses nearby steps and ties; optional P_RECQ
 * snaps playback only, leaving the original performance intact. With the ARP on, the notes the arp
 * plays are recorded, not the keys held (what plays back is what was heard). A key or MIDI note plays a
 * chord when the track's CHRD is on (chord.c): its notes go through the same input as so many keys. */
#define KB_SILENT 255u
static volatile uint8_t kb_asleep;
static uint32_t kb_prev;
static uint8_t kb_note[27], kb_trk[27];  /* per key: the note it started and on which track */
static uint8_t last_note = 60;
/* Stage's played notes, after scale/chord mapping. The ISR owns the held bits per
 * track; each track's last complete voicing survives release, including a tap between UI frames. */
static uint32_t live_held[NTRK][4], live_last[NTRK][4];
static volatile uint8_t transport_req;   /* 1 start, 2 stop, 3 restart (from the UI) */
/* audio ISR -> the UI's STEP entry (ui_input.c seq_midi_events): the edges of the notes MIDI plays, with the track and
 * the pitch they went to (after the scale layout and the chord), so the UI never guesses after a change */
#define STEP_MIDI_Q 64u
static uint32_t step_midi_q[STEP_MIDI_Q];
static volatile uint32_t step_midi_w, step_midi_r;
static volatile uint8_t step_midi_overflow;
static void step_midi_edge(uint32_t track, uint32_t pitch, uint32_t on, uint32_t ch, uint32_t src)
{
    uint32_t w = step_midi_w;
    if (!song.seq_mode)
        return;
    if (w - step_midi_r >= STEP_MIDI_Q) {
        step_midi_overflow = 1;
        return;
    }
    step_midi_q[w % STEP_MIDI_Q] = (track << 8) | pitch | (on << 10) | (ch << 12) | (src << 16);
    RING_PUBLISH();
    step_midi_w = w + 1u;
}
static volatile uint8_t panic_req;       /* bit per track: release every sounding note (preset / engine change) */
static volatile uint8_t midi_hint;       /* MIDI IN played track n - 1, which is not the selected one (the UI says so) */

static uint32_t trk_index(const track_t *t) { return (uint32_t)(t - trk); }

static uint32_t trk_midi_ch(uint32_t i) { return i % NTRK; }   /* MIDI channel 0..15 of track i (keys -> MIDI out) */

static int drum_track(const track_t *t) { return ENGINES[eng_idx(t->eng_req)] == &ENG_DRUM; }

static void live_note_on(const track_t *t, uint32_t note)
{
    uint32_t i, k = trk_index(t);
    if (drum_track(t))
        return;
    live_held[k][note >> 5] |= 1u << (note & 31u);
    for (i = 0; i < 4u; i++)
        live_last[k][i] = live_held[k][i];
}

/* the 27 keys from F: black or white, and the key's place among the keys of its colour (white 0..15, black
 * 0..10). The DRUM grid: white keys are steps, black keys 1..8 lanes, 9 ACC, 10 / 11 the page */
static int key_black(uint32_t k) { return (int)((0x54Au >> ((k + 5u) % 12u)) & 1u); }
static uint32_t key_place(uint32_t k)
{
    uint32_t i, n = 0;
    for (i = 0; i < k; i++)
        n += (uint32_t)(key_black(i) == key_black(k));
    return n;
}
enum { GK_ACC = NLANE, GK_PGDN, GK_PGUP };       /* black keys 9..11 on the grid */

static uint32_t scale_mask(const track_t *t)
{
    const micro_scale_t *s = micro_scale(t);
    if (s) { /* approximate pitch classes for the keyboard LEDs only */
        uint32_t mask = 0;
        for (uint32_t i = 0; i < s->count; i++)
            mask |= 1u << ((s->pitch[i] + 5000) / 10000 % 12);
        return mask;
    }
    return SCALE_MASK[clamp(t->p[P_SCALE], 0, SCALE_LEGACY - 1)];
}

static uint32_t scale_count(const track_t *t)
{
    if (micro_scale(t)) return micro_scale(t)->count;
    uint32_t mask = scale_mask(t), count = 0;
    while (mask) {
        count += mask & 1u;
        mask >>= 1;
    }
    return count;
}

static int32_t mpc_degree(const track_t *t) { return clamp(t->p[P_MPCDEG], 1, (int32_t)scale_count(t)); }

/* scale degree `degree` from C4 (0 = the ROOT above C4) in the track's scale, + offset semitones and TRN. strict: a
 * note off the MIDI range is silent (else clamped). Scales of 5, 6, 8 or 12 notes repeat no degree */
static uint32_t scale_degree_map(const track_t *t, int32_t degree, int32_t offset, int strict)
{
    if (micro_active(t)) return micro_degree_map(t, degree, offset, strict);
    uint32_t mask = scale_mask(t), i;
    int32_t count = (int32_t)scale_count(t), oct, n;
    oct = degree / count;
    degree %= count;
    if (degree < 0) {
        degree += count;
        oct--;
    }
    for (i = 0; i < 12u; i++)
        if ((mask >> i) & 1u) {
            if (!degree)
                break;
            degree--;
        }
    n = 60 + t->p[P_ROOT] + 12 * oct + (int32_t)i + offset + t->p[P_TRANS];
    if (strict && (n < 0 || n > 127))
        return KB_SILENT;
    return (uint32_t)clamp(n, 0, 127);
}

/* note n (a key's or a MIDI note) through the WHITE / ALL layouts. WHITE (and MPC on the keys): the white keys walk
 * the scale from C4 = ROOT, the black keys are silent. ALL: every key the next degree of the scale */
static uint32_t scale_map(const track_t *t, int32_t n, int32_t offset)
{
    static const int8_t DEGREE[12] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};
    int32_t degree;
    if (t->p[P_QUANT] == Q_ALL)
        return scale_degree_map(t, n - 60, offset, 1);
    if (t->p[P_QUANT] == Q_WHITE || t->p[P_QUANT] == Q_MPC) {
        if ((degree = DEGREE[n % 12]) < 0)
            return KB_SILENT;
        return scale_degree_map(t, degree + (n / 12 - 5) * 7, offset, 0);
    }
    return (uint32_t)clamp(n + offset + t->p[P_TRANS], 0, 127);
}

static uint32_t kb_map(const track_t *t, uint32_t k)
{
    const engine_t *e = ENGINES[eng_idx(t->eng_req)];   /* (the engine it switches to) */
    int32_t n;
    if (e->keys && (n = e->keys(t, k)) >= 0)           /* the engine's own key map (GM kit, slices) */
        return (uint32_t)n;
    n = 53 + (int32_t)k;
    if (t->p[P_QUANT] == Q_SNAP) {                     /* SNAP: every key, rounded down to the scale */
        if (micro_active(t)) {
            uint32_t note = micro_snap(t, n + t->p[P_TRANS]);
            return note == KB_SILENT ? note : micro_degree_map(t, (int32_t)note - 60, 12 * song.octave, 1);
        }
        uint32_t mask = scale_mask(t), guard = 12;
        n += 12 * song.octave + t->p[P_TRANS];
        while (guard-- && !((mask >> (uint32_t)((n - t->p[P_ROOT] + 120) % 12)) & 1u))
            n--;
        return (uint32_t)clamp(n, 0, 127);
    }
    return scale_map(t, n, 12 * song.octave);
}

/* a MIDI note into track t: WHITE and ALL map it as the keys (without the octave buttons), MPC plays the 16 pads of
 * an MPC's Bank H (MIDI 20..35, H01..H16 of MPC Sample's default map; any other note is silent, kits too) as
 * successive degrees of the scale, pad H02 on degree DEG. Kits and slices keep their own note map. KB_SILENT: none */
static uint32_t midi_map(const track_t *t, uint32_t note)
{
    const engine_t *e = ENGINES[eng_idx(t->eng_req)];
    if (t->p[P_QUANT] == Q_MPC && (note < 20u || note > 35u))
        return KB_SILENT;
    if (e->keys && e->keys(t, 0) >= 0)
        return note;
    if (t->p[P_QUANT] == Q_MPC)
        return scale_degree_map(t, (int32_t)note - 21 + mpc_degree(t) - 1, 12 * song.octave, 1);
    if (t->p[P_QUANT] == Q_WHITE || t->p[P_QUANT] == Q_ALL)
        return scale_map(t, (int32_t)note, 0);
    if (micro_active(t) && t->p[P_QUANT] == Q_SNAP)
        return micro_snap(t, (int32_t)note + t->p[P_TRANS]);
    return note;
}

#include "chord.c"

/* ------------------------------------------------------------- arp --- */
static void arp_add(track_t *t, uint32_t note)
{
    uint32_t i;
    if (t->p[P_AHOLD] && t->arp_phys == 0u)
        t->nheld = 0;                               /* new chord replaces the latched one */
    t->arp_phys++;                                  /* every key-down: arp_remove counts every key-up */
    for (i = 0; i < t->nheld; i++)
        if (t->held[i] == note)
            return;                                 /* repeated note-on: not a new note */
    if (t->nheld < 16u)
        t->held[t->nheld++] = (uint8_t)note;
    if (t->nheld == 1u) {
        t->arp_pos = 0xFFFFFFF;                     /* fire on this block */
        t->arp_idx = 0xFFFFFFFFu;
    }
}

static void arp_remove(track_t *t, uint32_t note)
{
    uint32_t i, k = 0;
    if (t->arp_phys)
        t->arp_phys--;
    if (t->p[P_AHOLD])
        return;
    for (i = 0; i < t->nheld; i++)
        if (t->held[i] != note)
            t->held[k++] = t->held[i];
    t->nheld = (uint8_t)k;
}

static void rec_note(track_t *t, uint32_t note, uint32_t vel);
static void rec_release(track_t *t, uint32_t note);
static volatile uint32_t seq_erase_request, seq_erase_generation;
static uint32_t seq_erase_transport;
static uint8_t seq_erase_seen[NTRK];
static volatile uint16_t seq_erase_button;
static int seq_erase_active(const track_t *t);
static int rec_on(const track_t *t) { return ((song.rec >> trk_index(t)) & 1u) && song.playing && !chain.running; }

#include "motion.c"

static void arp_off(track_t *t)                  /* the sounding arp note ends (a recorded one too) */
{
    if (!recording_owns(t, t->arp_note)) trk_note_off(t, t->arp_note);
    if (rec_on(t))
        rec_release(t, t->arp_note);
    t->arp_note = 0;
}

/* n and gate_n: sample time (INT), musical units (MIDI); a stopped external
 * transport uses real time converted to musical units for a tapped note's gate. */
static volatile uint32_t beat_pos, beat_n;      /* clock units into the beat, beat of the bar (0..3) */
static void arp_step(track_t *t, uint32_t n, uint32_t gate_n)
{
    uint32_t period, cnt, list[64], len = 0, i, j, o;
    int32_t sw;
    if (t->arp_note) {
        if (t->arp_off <= gate_n)
            arp_off(t);
        else
            t->arp_off -= gate_n;
    }
    if (!t->p[P_AMODE] || !t->nheld) {
        if (!t->nheld && t->arp_note)
            arp_off(t);
        return;
    }
    period = seq_div_samples((uint32_t)t->p[P_ARATE]);
    sw = t->p[P_ASWING] * (int32_t)period / 250;
    t->arp_pos += n;
    if (t->arp_pos < period + (uint32_t)((t->arp_idx & 1u) ? sw : -sw) && t->arp_pos != 0xFFFFFFF + n)
        return;
    if (t->arp_pos >= 0xFFFFFFFu)
        t->arp_pos = 0;
    else if (song.g[G_CLOCK])
        t->arp_pos -= period + (uint32_t)((t->arp_idx & 1u) ? sw : -sw);
    else
        t->arp_pos = 0;
    /* build the note list: held notes (sorted or as played) over OCT octaves */
    for (i = 0; i < t->nheld; i++)
        list[i] = t->held[i];
    cnt = t->nheld;
    if (!t->p[P_AORDER])
        for (i = 1; i < cnt; i++)
            for (j = i; j > 0 && list[j - 1] > list[j]; j--) {
                uint32_t x = list[j];
                list[j] = list[j - 1];
                list[j - 1] = x;
            }
    for (o = 0; o < (uint32_t)t->p[P_AOCT]; o++)
        for (i = 0; i < cnt && len < 64u; i++)
            if (micro_active(t)) {
                uint32_t note = micro_degree_map(t, (int32_t)list[i] - 60 + (int32_t)scale_note_period(t) * (int32_t)o, 0, 1);
                if (note < 128u) list[len++] = note;
            } else {
                list[len++] = clamp((int32_t)list[i] + 12 * (int32_t)o, 0, 127);
            }
    if (!len) return;
    if (t->p[P_AMODE] == 6) { list[0] = t->held[t->nheld - 1u]; len = 1; } /* REPEAT: last played note, no octave traversal */
    t->arp_idx++;
    switch (t->p[P_AMODE]) {
    case 2:
        j = len - 1u - t->arp_idx % len;
        break;
    case 3: {
        uint32_t cyc = len > 1u ? 2u * len - 2u : 1u, k = t->arp_idx % cyc;
        j = k < len ? k : cyc - k;
        break;
    }
    case 4:
        j = rng() % len;
        break;
    default:
        j = t->arp_idx % len;
        break;
    }
    if (t->arp_note)
        arp_off(t);
    if ((uint32_t)(rng() & 127u) <= (uint32_t)t->p[P_APROB]) {
        t->arp_note = (uint8_t)list[j];
        t->arp_off = period * (uint32_t)t->p[P_AGATE] / 128u;
        if (rec_on(t))
            rec_note(t, t->arp_note, 100);           /* recording: what the arp plays */
        trk_note_on(t, t->arp_note, 100);
    }
}

static void arp_tick(track_t *t, uint32_t n) { arp_step(t, n, n); }

/* -------------------------------------------------------- note input --- */
/* the length of step idx in samples: SWING (the track's + the global, at most 100) makes the even steps longer
 * and the odd ones shorter, so every odd step starts late */
static uint32_t step_samples(const track_t *t, uint32_t period, uint32_t idx)
{
    return swing_step_len(t, period, idx);   /* core.h: own + global, at most 100 */
}

#include "recording.c"

/* Capture: every note played (keys, MIDI, a chord's notes) while the transport runs and its track is not recording,
 * with its place: the steps the track has entered since PLAY (cap_step) and the 1/256 of the step, as recording.c
 * keeps them. A ring of 8-byte events (RAM is the audio arena's: kept small); REC held writes the selected track's last
 * bars into its pattern (ui_input.c capture_take). Durations in 1/256 of a step; tr bit 7: still held */
#define CAP_N 128u
typedef struct { uint16_t step, dur; uint8_t on, note, vel, tr; } cap_ev_t;
static cap_ev_t cap_ev[CAP_N];
static uint32_t cap_w, cap_from;                /* events written; the first of this run of the transport */
static uint16_t cap_step[NTRK];                 /* the step playing, counted from PLAY (65535 before the first) */
static uint32_t cap_frac(const track_t *t)      /* RECORD_UNIT fraction of the step playing */
{
    uint32_t span = step_samples(t, seq_div_samples((uint32_t)t->p[P_SDIV]), t->seq_idx);
    uint32_t on = t->seq_pos >= 0x7FFFFFFFu || !span ? 0u : (uint32_t)(((uint64_t)t->seq_pos * RECORD_UNIT) / span);
    return on > 65535u ? 65535u : on;
}
static uint32_t cap_now(const track_t *t)       /* the place now in 1/256 steps (mod 2^24) */
{
    uint32_t k = trk_index(t), st = t->seq_pos >= 0x7FFFFFFFu ? (uint16_t)(cap_step[k] + 1u) : cap_step[k];
    return (st << 8) + (cap_frac(t) >> 8);
}
static uint32_t cap_len(const cap_ev_t *e, uint32_t now)   /* its length (held: up to now), 1/256 steps */
{
    uint32_t d = (e->tr & 128u) ? (now - ((uint32_t)e->step << 8) - e->on) & 0xFFFFFFu : e->dur;
    return d < 1u ? 1u : d > 65535u ? 65535u : d;
}
static void cap_on(const track_t *t, uint32_t note, uint32_t vel)
{
    uint32_t k = trk_index(t);
    cap_ev_t *e = &cap_ev[cap_w % CAP_N];
    if (!song.playing || chain.running || rec_on(t))
        return;
    e->step = t->seq_pos >= 0x7FFFFFFFu ? (uint16_t)(cap_step[k] + 1u) : cap_step[k];
    e->on = (uint8_t)(cap_frac(t) >> 8);
    e->dur = 0;
    e->note = (uint8_t)note;
    e->vel = (uint8_t)vel;
    e->tr = (uint8_t)(k | 128u);
    cap_w++;
}
static void cap_off(const track_t *t, uint32_t note)
{
    uint32_t i, k = trk_index(t);
    for (i = 0; i < 32u && i < cap_w - cap_from && i < CAP_N; i++) {
        cap_ev_t *e = &cap_ev[(cap_w - 1u - i) % CAP_N];
        if (e->tr == (k | 128u) && e->note == note) {
            e->dur = (uint16_t)cap_len(e, cap_now(t));
            e->tr = (uint8_t)k;
            return;
        }
    }
}

/* Jam -> song: while the transport runs (no song), each time track 1 starts its loop the four tracks' patterns are
 * a row: the same as the last row: its repeats + 1 (up to 16), else a new row (up to CHAIN_ROWS). From PLAY on;
 * SEQ > SONG TAKE JAM makes them the song (ui_input.c jam_take) */
static struct { uint8_t n, pat[CHAIN_ROWS][NTRK], rep[CHAIN_ROWS]; } jam;
static void jam_loop(void)
{
    uint32_t r = jam.n, k, same = r != 0u;
    for (k = 0; same && k < NTRK; k++)
        same = jam.pat[r - 1u][k] == trk[k].pattern;
    if (same && jam.rep[r - 1u] < 16u) {
        jam.rep[r - 1u]++;
        return;
    }
    if (r >= CHAIN_ROWS)
        return;
    for (k = 0; k < NTRK; k++)
        jam.pat[r][k] = trk[k].pattern;
    jam.rep[r] = 1;
    jam.n = (uint8_t)(r + 1u);
}

/* The STEP overview rounds to the nearest swung onset, with loop wrap.
 * SEQ > STEP follows playback (ui_input.c). Overdub: a step that
 * holds notes gets this one added (a chord of up to 4; when full, the last note is
 * replaced); MONO / LEGATO / UNISON parts keep one note per step, as step entry does.
 * Held on (synth parts): each further step the sequencer enters while the note is
 * held becomes a TIE (rec_hold), up to the pattern length. Actual held duration
 * sets the last step's fractional gate (rec_finish), independent of track GATE.
 * A note recorded into another step ends the hold before (the step model ties the
 * notes of one step only). */
static void rec_finish(track_t *t);
static void rec_note(track_t *t, uint32_t note, uint32_t vel)
{
    if (seq_erase_active(t)) return;
    uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u, k;
    uint32_t next = t->seq_pos == 0x7FFFFFFFu;      /* Start and the note in one block: step 0 fires after it */
    uint32_t idx = next ? 0u : t->seq_idx % len;
    step_t *s;
    /* Every recording source rounds to the closest swung onset, including
     * the last-to-first transition. Guard the upcoming live note from
     * being retriggered when that step is entered. */
    if (!next &&
        t->seq_pos >= step_samples(t, seq_div_samples((uint32_t)t->p[P_SDIV]), idx) / 2u) {
        idx = (idx + 1u) % len;
        next = 1;
    }
    if (t->rh_n && t->rh_start != idx)
        rec_finish(t);
    s = &t->step[idx];
    if (!recording_note(t, note, vel, idx)) return;
    if (drum_track(t)) {                            /* DRUM: into the grid, no holds (hits) */
        uint32_t l = drum_lane(note);
        if (s->time != ST_NOTE || (!s->n && !s->hit)) {
            s->n = s->hit = s->acc = 0;
            s->flags = 0;
            s->vel = 0;
        }
        s->time = ST_NOTE;
        if (note == DRUM_LANE_NOTE[l]) {            /* a lane's note: its hit */
            s->hit |= (uint8_t)(1u << l);
            if (vel > 110)
                s->acc |= (uint8_t)(1u << l);
        } else {                                    /* another GM drum (a low tom, a crash): a note */
            for (k = 0; k < s->n && s->note[k] != note; k++)
                ;
            if (k == s->n) {
                if (s->n < 4u)
                    s->n++;
                s->note[s->n - 1u] = (uint8_t)note;
            }
            if (vel > 110)
                s->flags |= SF_ACCENT;
        }
        if (vel > s->vel)
            s->vel = (uint8_t)vel;
        t->seq_active = 1;
        if (next) {                                 /* it sounds now: step 0 must not trigger it again */
            if (t->rskip_idx != idx)
                t->rskip_n = 0;
            t->rskip_idx = (uint8_t)idx;
            if (t->rskip_n < NLANE)
                t->rskip[t->rskip_n++] = (uint8_t)note;
        }
        s->flags |= SF_RECORDED;
        return;
    }
    if ((!t->rh_n || t->rh_start != idx) && step_gate(s))
        for (k = 1; k < len && t->step[(idx + k) % len].time == ST_TIE; k++) {
            step_t *tail = &t->step[(idx + k) % len];
            memset(tail, 0, sizeof *tail);
            tail->time = ST_REST;
        }
    if (s->time != ST_NOTE || !s->n || trk_vmode(t) != V_POLY) {
        s->n = 0;                                   /* a fresh step */
        s->flags = 0;
        s->vel = 0;
        s->hit = s->acc = 0;
    }
    for (k = 0; k < s->n && s->note[k] != note; k++)
        ;
    if (k == s->n) {
        if (s->n < 4u)
            s->n++;
        s->note[s->n - 1u] = (uint8_t)note;
    }
    s->time = ST_NOTE;
    if (!t->rh_n || t->rh_start != idx)
        s->flags &= (uint8_t)~SF_ACCENT;
    if ((!t->rh_n || t->rh_start != idx) && s->n == 1u)
        s->vel = (uint8_t)vel;
    else if (vel > s->vel)
        s->vel = (uint8_t)vel;
    s->probability = 0;
    t->seq_active = 1;
    if (!s->hit)
        s->acc = 255;                              /* retain the held gate until its actual release */
    if (next) {                                     /* it sounds now: the step must not trigger it again */
        if (t->rskip_idx != idx)
            t->rskip_n = 0;
        t->rskip_idx = (uint8_t)idx;
        if (t->rskip_n < 4u)
            t->rskip[t->rskip_n++] = (uint8_t)note;
    }
    if (!t->rh_n || t->rh_start != idx) {           /* a new hold (one in another step ends) */
        t->rh_n = 0;
        t->rh_start = (uint8_t)idx;
        t->rh_ties = 0;
        t->rh_elapsed = 0;
    }
    for (k = 0; k < t->rh_n && t->rh_note[k] != note; k++)
        ;
    if (k == t->rh_n && t->rh_n < 4u)
        t->rh_note[t->rh_n++] = (uint8_t)note;      /* a chord: held until its last key is up */
    s->flags |= SF_RECORDED;
}

/* the sequencer enters step idx (before playing it): a recorded note still held ties into it */
static void rec_hold(track_t *t, uint32_t idx, uint32_t len)
{
    step_t *s;
    uint32_t k;
    if (!t->rh_n)
        return;
    if (!((song.rec >> trk_index(t)) & 1u) || t->rh_ties + 1u >= len) {
        t->rh_n = 0;                                /* disarmed, or the whole pattern is this note */
        return;
    }
    if (idx == t->rh_start)
        return;                                     /* never over the note's onset */
    s = &t->step[idx];
    if (s->flags & SF_RECORDED) return;             /* another recorded onset keeps its own release */
    t->rh_bak = *s;
    t->rh_last = (uint8_t)idx;
    t->rh_ties++;
    for (k = 0; k < 4u; k++)
        s->note[k] = 0;
    s->n = 0;
    s->time = ST_TIE;
    s->flags = 0;
    s->vel = 0;
    s->hit = 0;
    s->acc = 255;
}

/* Onsets snap to the nearest step, but the duration comes from elapsed input
 * time. Shifting the onset must shift its release by the same amount. Store
 * the final gate in the unused lane-accent byte of a note/tie without hits. */
static void rec_finish(track_t *t)
{
    uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u;
    uint32_t idx = t->rh_start, duration = t->rh_elapsed, count = 1;
    uint32_t period = seq_div_samples((uint32_t)t->p[P_SDIV]), span;
    if (!t->rh_n && !t->rh_elapsed)
        return;
    span = step_samples(t, period, idx);
    while (duration > span && count < len) {
        duration -= span;
        idx = (idx + 1u) % len;
        count++;
        span = step_samples(t, period, idx);
    }
    if (t->rh_ties && t->rh_last != idx && t->rh_last == (idx + 1u) % len)
        t->step[t->rh_last] = t->rh_bak;
    for (uint32_t i = 1; i < count; i++) {
        step_t *s = &t->step[(t->rh_start + i) % len];
        if (s->flags & SF_RECORDED) continue;
        memset(s, 0, sizeof *s);
        s->time = ST_TIE;
        s->acc = 255;
    }
    if (!t->step[idx].hit)
        t->step[idx].acc = (uint8_t)clamp((int32_t)((uint64_t)duration * 255u / span), 1, 255);
    t->rh_elapsed = 0;
}

/* a key of a recorded note is up: the hold ends with the last one */
static void rec_release(track_t *t, uint32_t note)
{
    recording_release(t, note);
    uint32_t i, k = 0;
    for (i = 0; i < t->rh_n; i++)
        if (t->rh_note[i] != note)
            t->rh_note[k++] = t->rh_note[i];
    if (k == t->rh_n)
        return;
    if (!k)
        rec_finish(t);
    if ((t->rh_n = (uint8_t)k))
        return;                                     /* not one of them, or others still held */
}

static void countin_note(track_t *t,uint32_t note,uint32_t vel);
static void input_on(track_t *t, uint32_t note, uint32_t vel)
{
    countin_note(t,note,vel);
    last_note = (uint8_t)note;
    live_note_on(t, note);
    cap_on(t, note, vel);
    if (rec_on(t) && !t->p[P_AMODE])               /* (ARP on: arp_tick records its notes) */
        rec_note(t, note, vel);
    if (t->p[P_AMODE])
        arp_add(t, note);
    else
        trk_note_on(t, note, vel);
}

static int midi_note_held(const track_t *t, uint32_t note);
static int midi_local_held(const track_t *t, uint32_t note);
static void input_off(track_t *t, uint32_t note)
{
    if (midi_note_held(t, note))
        return;
    live_held[trk_index(t)][note >> 5] &= ~(1u << (note & 31u));
    cap_off(t, note);
    rec_release(t, note);
    arp_remove(t, note);                            /* both: the note may have started in the */
    if (!recording_owns(t, note)) trk_note_off(t, note); /* other mode (ARP switched while held) */
}

/* the effect of key k in the FX layer (PF_N none): the first PF_M1 white keys (F3 .. A4) in order, black keys
 * 1..4 (F#3 G#3 A#3 C#4) the track mutes */
static uint32_t perf_key(uint32_t k)
{
    uint32_t p = key_place(k);
    return !key_black(k) ? (p < PF_KEYS ? perf_map[p] : PF_N) : p < NTRK ? PF_M1 + p : PF_N;
}

static uint8_t kb_fx[27];
static __attribute__((noinline)) void perf_key_press(uint32_t k, int down)
{
    uint32_t e, j;
    if (down) {
        e = perf_key(k);
        kb_fx[k] = (uint8_t)(e + 1u);
        perf_press(e, 1);
        return;
    }
    if (!kb_fx[k])
        return;
    e = kb_fx[k] - 1u;
    kb_fx[k] = 0;
    for (j = 0; j < 27u; j++)
        if (kb_fx[j] == e + 1u)
            return;
    perf_press(e, 0);
}

/* the map changed (ui_layer.c: PRESETS with a key held, EDIT, a settings load): a white key held takes its new
 * effect at once (the old one let go); FX LATCH: a latched effect of the key's becomes the new one */
static __attribute__((noinline)) void perf_key_remap(void)
{
    uint32_t k, e, o;
    perf_remap = 0;
    for (k = 0; k < 27u; k++) {
        if (!kb_fx[k] || key_black(k) || (o = kb_fx[k] - 1u) == (e = perf_key(k)))
            continue;
        if (!perf_latch_on) {
            perf_key_press(k, 0);
            perf_key_press(k, 1);
            continue;
        }
        kb_fx[k] = (uint8_t)(e + 1u);
        if (o < PF_N && ((perf_latched >> o) & 1u)) {
            perf_latched &= ~PF_BIT(o);
            if (e < PF_N) {
                perf_latched |= PF_BIT(e);
                perf_ord[e] = ++perf_seq;
            }
        }
    }
}


/* key k plays kb_note[k] on track t: its chord (chord.c; the note alone with CHRD OFF). A note another key
 * holds already sounds: it is not started again (nor sent to MIDI OUT); the key keeps its notes */
static void key_on(uint32_t k, track_t *t)
{
    uint32_t n = chord_build(t, kb_note[k], kb_chord[k]), i, mc = trk_midi_ch(trk_index(t));
    kb_chn[k] = 0;
    for (i = 0; i < n; i++) {
        uint32_t x = kb_chord[k][i];
        if (midi_local_held(t, x))
            continue;
        input_on(t, x, 100);
        midi_out_event(0x09u | (0x90u | mc) << 8 | x << 16 | 100u << 24);
    }
    kb_chn[k] = (uint8_t)n;
    last_note = kb_note[k];                         /* (step entry, the SAMPLE zone: the key's note) */
}

/* key k is up: the notes it started end, but those another key still holds */
static void key_off(uint32_t k, track_t *t)
{
    uint32_t n = kb_chn[k], i, mc = trk_midi_ch(trk_index(t));
    kb_chn[k] = 0;
    for (i = 0; i < n; i++) {
        uint32_t x = kb_chord[k][i];
        if (midi_local_held(t, x))
            continue;
        input_off(t, x);
        midi_out_event(0x08u | (0x80u | mc) << 8 | x << 16);
    }
}

#ifdef FM1_INPUT_LAT
/* the press latency stats (hal/fm1_input.h fm1_in_stat, console `inp`): from the first scan that saw
 * key k closed to its note-on here, and to the note's first sample leaving the DMA (kb_out_tick:
 * audio.c, the TIMER4 tick at which the block being rendered starts to play) */
static uint32_t kb_out_tick;
static __attribute__((noinline)) void kb_lat(uint32_t k)
{
    uint32_t t0 = fm1_in.note_t0[k], d = fm1_ticks() - t0, o = kb_out_tick - t0;
    fm1_in_stat.kb_n++;
    fm1_in_stat.kb_sum += d;
    fm1_in_stat.dac_sum += o;
    if (d > fm1_in_stat.kb_max)
        fm1_in_stat.kb_max = d;
    if (o > fm1_in_stat.dac_max)
        fm1_in_stat.dac_max = o;
}
#endif

static void keyboard_block(void)
{
    uint32_t cur = fm1_in.notes, ch, k;
    if (perf_remap) perf_key_remap();
    ch = cur ^ kb_prev;                           /* keys also sound while entering steps */
    if (!ch)
        return;
    for (k = 0; k < 27u; k++) {
        if (!((ch >> k) & 1u))
            continue;
        if ((cur >> k) & 1u) {                    /* the selected track; the key-up goes to the same one */
            kb_trk[k] = song.sel;
            if (kb_asleep) { kb_note[k]=KB_SILENT; continue; }
            if ((fm1_in.buttons & kb_mask) || kb_lock) {       /* a layer's button held: the key is the layer's (ui_layer.c), */
                kb_note[k] = KB_SILENT;           /* with FX an effect (perform.c) */
                kb_layer |= 1u << k;
                if ((fm1_in.buttons & perf_mask) || (kb_lock & 2u))
                    perf_key_press(k, 1);
                continue;
            }
            if (song.grid == 2u)                  /* NAME (ui_name.c): every key types, none sounds */
                kb_note[k] = KB_SILENT;
            else if (song.grid)                   /* the DRUM grid: a lane key plays its lane, the rest are the UI's */
                kb_note[k] = key_black(k) && key_place(k) < NLANE ? DRUM_LANE_NOTE[key_place(k)] : KB_SILENT;
            else
                kb_note[k] = (uint8_t)kb_map(&trk[kb_trk[k]], k);
            if (kb_note[k] == KB_SILENT)
                continue;
            key_on(k, &trk[kb_trk[k]]);
#ifdef FM1_INPUT_LAT
            kb_lat(k);
#endif
        } else {
            if ((kb_layer >> k) & 1u) {
                kb_layer &= ~(1u << k);
                perf_key_press(k, 0);
                continue;
            }
            if (kb_note[k] == KB_SILENT)
                continue;
            key_off(k, &trk[kb_trk[k] % NTRK]);
        }
    }
    kb_prev = cur;
}

#include "click.c"
static volatile uint32_t cin_left;
static uint32_t cin_pos, cin_total;
static uint8_t cin_note[NTRK][8],cin_vel[NTRK][8],cin_n[NTRK];
static void countin_note(track_t *t,uint32_t note,uint32_t vel)
{
    if(cin_left!=1u || cin_pos<beat_samples()/2u)return;
    uint32_t tr=trk_index(t),i;for(i=0;i<cin_n[tr] && cin_note[tr][i]!=note;i++);
    if(i<8u){cin_note[tr][i]=note;cin_vel[tr][i]=vel;if(i==cin_n[tr])cin_n[tr]++;}
}
static int seq_counting(void) { return cin_left != 0; }
static int click_wanted(void) { return settings_click == CLICK_ON || (settings_click == CLICK_REC && song.rec); }
/* Preview has its own bounded lifetime and never enters the recording or MIDI-out paths. */
static volatile uint8_t audition_request;
static uint8_t audition_track, audition_count, audition_notes[12], audition_vel[12];
static uint8_t audition_active, audition_owner, audition_n, audition_played[12];
static uint32_t audition_left, audition_gate;
static int32_t audition_pitch;
static void audition_stop(void)
{
    track_t *t = &trk[audition_owner % NTRK];
    for (uint32_t i=0;i<audition_n;i++) {
        uint32_t note=audition_played[i];
        if (!(live_held[trk_index(t)][note>>5] & (1u<<(note&31u))) && t->arp_note!=note && !recording_owns(t,note)) trk_note_off(t,note);
    }
    audition_active=audition_n=0;
}
static __attribute__((noinline)) void audition_tick(uint32_t n)
{
    if (song.playing || seq_counting()) { audition_request=0; if(audition_active)audition_stop(); return; }
    if (audition_request) {
        audition_stop(); audition_owner=audition_track; audition_left=FS/6u;
        track_t *t=&trk[audition_owner % NTRK];
        drum_event_pitch=audition_pitch;drum_event_length=audition_gate;
        for(uint32_t i=0;i<audition_count;i++) {
            uint32_t note=audition_notes[i];
            if ((live_held[trk_index(t)][note>>5] & (1u<<(note&31u))) || t->arp_note==note) continue;
            audition_played[audition_n++]=(uint8_t)note; trk_note_on(t,note,audition_vel[i]);
        }
        drum_event_pitch=0;drum_event_length=0;
        audition_active=1; audition_request=0;
    }
    if(audition_active) { if(n>=audition_left)audition_stop(); else audition_left-=n; }
}
/* -------------------------------------------------------- sequencer --- */
static void seq_start(void)
{
    uint32_t i;
    seq_erase_request = 0;
    seq_erase_transport++;
    memset(seq_erase_seen, 0, sizeof seq_erase_seen);
    motion_begin();
    chain_start();
    for (i = 0; i < NTRK; i++) {                   /* every track from its step 0, together */
        track_t *t = &trk[i];
        if (t->p[P_LTRIG] && t->p[P_LSYNC]) t->lfo_ph = (uint32_t)t->p[P_LPHASE] << 25;
        t->seq_idx = (uint16_t)(t->p[P_SLEN] - 1);
        t->seq_pos = 0x7FFFFFFF;                   /* step 0 fires on the first block */
        t->rskip_n = 0;
        t->rh_n = 0;
        cap_step[i] = 0xFFFFu;                     /* (capture: step 0 is the first) */
    }
    cap_from = cap_w;                              /* capture and the jam log: this run */
    jam.n = 0;
    song.tick = 0;
    beat_pos = 0; beat_n = 0;                      /* the ARP LED's beat from the top too */
    song.playing = 1;
    if (click_wanted()) click_req=2;
    slicer_start();                                /* slicer.c: its step 0 with the sequencer's */
    perf_start();                                  /* perform.c: its 1/16 grid too */
}

static void seq_release(track_t *t)
{
    uint32_t i;
    for (i = 0; i < t->seq_n; i++)
        if (!(live_held[trk_index(t)][t->seq_notes[i] >> 5] & (1u << (t->seq_notes[i] & 31u))) &&
            t->arp_note != t->seq_notes[i] && !recording_owns(t, t->seq_notes[i]))
            trk_note_off(t, t->seq_notes[i]);
    t->seq_n = 0;
    t->seq_hold = 0;
    t->slide_glide = 0;                             /* live MONO / LEG keys must not glide after it */
}

#include "seq_erase.c"

static void seq_stop(void)
{
    cin_left=0; click_req=0; clk.env=0; audition_request=0; if(audition_active)audition_stop();
    uint32_t i;
    seq_erase_request = 0;
    seq_erase_transport++;
    song.playing = 0;
    if (song.g[G_CLOCK])                           /* external clock: the arp stops with the transport, */
        for (i = 0; i < NPART; i++)                /* its sounding note too (HOLD keeps the latched chord) */
            if (trk[i].arp_note)
                arp_off(&trk[i]);
    for (i = 0; i < NTRK; i++) {
        recording_stop(&trk[i]);
        if (trk[i].rh_n)
            rec_finish(&trk[i]);
        seq_release(&trk[i]);
        trk[i].rh_n = 0;                           /* a recorded note held over the stop: as far as it got */
    }
    chain_stop();
    for (i = 0; i < NTRK; i++) if (trk[i].pattern_next < NPAT) pattern_switch(&trk[i], trk[i].pattern_next);
    motion_end();
}

/* play one step: TIE extends, REST releases, NOTE (re)triggers its notes, then its lane hits (their GM
 * notes, DRUM_LANE_NOTE; an accented hit at 127); a SLIDE on the previous step makes this one legato with a
 * glide (acid style). skip: bit k = note k, bit 8 + l = the hit of lane l already sounds from live
 * recording (not triggered, not released here). */
static __attribute__((noinline)) void seq_step(track_t *t, const step_t *s, uint32_t period, uint32_t skip)
{
    if (s->flags & SF_RECORDED)
        for (uint32_t r = recording_head[recording_owner(t)]; r < RECORD_MAX; r = recording_next[r]) if (recording_active(t, r) && recording_view(t, &recording[r]) == t->seq_idx) {
            for (uint32_t k = 0; k < s->n; k++) if (s->note[k] == recording[r].note) skip |= 1u << k;
            for (uint32_t k = 0; k < NLANE; k++) if (DRUM_LANE_NOTE[k] == recording[r].note) skip |= 1u << (8u + k);
        }
    uint32_t i, j, gate = step_gate(s) ? period * step_gate(s) / 255u : period * (uint32_t)t->p[P_SGATE] / 128u;
    uint32_t vel = (s->flags & SF_ACCENT) ? 127u : (s->vel ? s->vel : 96u);
    uint32_t slide_in = t->seq_hold && t->seq_n;
    uint32_t len = t->p[P_SLEN] ? (uint32_t)t->p[P_SLEN] : 1u;
    uint32_t next_tie = seq_steps(t)[(t->seq_idx + 1u) % len].time == ST_TIE;
    uint8_t nn[4 + NLANE], vv[4 + NLANE];           /* the notes it plays: its notes, then its hits */
    uint32_t m = 0, sk = 0;
    if (step_chance(s) < 100u && rng() % 100u >= step_chance(s)) {
        seq_release(t);
        return;
    }
    if (s->time == ST_TIE) {
        if (t->seq_n) {
            t->seq_off = step_gate(s) ? gate : gate + period / 2u;
            t->seq_hold = (s->flags & SF_SLIDE) != 0 || next_tie;   /* chains hold at any GATE / swing */
        }
        return;
    }
    if (s->time == ST_REST || (!s->n && !s->hit)) {
        seq_release(t);
        return;
    }
    for (i = 0; i < s->n && i < 4u; i++, m++) {
        nn[m] = s->note[i];
        vv[m] = (uint8_t)vel;
        sk |= ((skip >> i) & 1u) << m;
    }
    for (i = 0; i < NLANE; i++)
        if ((s->hit >> i) & 1u) {
            nn[m] = DRUM_LANE_NOTE[i];
            vv[m] = (uint8_t)((s->acc >> i) & 1u ? 127u : vel);
            sk |= ((skip >> (8u + i)) & 1u) << m;
            m++;
        }
    t->slide_glide = (uint8_t)slide_in;
    if (!slide_in)
        seq_release(t);
    for (i = 0; i < m; i++)
        if (!((sk >> i) & 1u))
            trk_note_on(t, nn[i], vv[i]);
    if (slide_in)                                   /* release what is not held over */
        for (i = 0; i < t->seq_n; i++) {
            for (j = 0; j < m && nn[j] != t->seq_notes[i]; j++)
                ;
            if (j == m)
                trk_note_off(t, t->seq_notes[i]);
        }
    t->seq_n = 0;
    for (i = 0; i < m; i++)
        if (!((sk >> i) & 1u))
            t->seq_notes[t->seq_n++] = nn[i];
    t->seq_off = gate;
    t->seq_hold = (s->flags & SF_SLIDE) != 0 || next_tie;   /* next step a TIE: keep the notes to it */
}

static void seq_grid_tick(track_t *t, uint32_t n)
{
    uint32_t period, len;
    if (t->seq_n && !t->seq_hold) {
        if (t->seq_off <= n)
            seq_release(t);
        else
            t->seq_off -= n;
    }
    if (!song.playing)
        return;
    if (t->rh_n)
        t->rh_elapsed += n;
    period = seq_div_samples((uint32_t)t->p[P_SDIV]);
    len = (uint32_t)t->p[P_SLEN];
    t->seq_pos += n;
    for (;;) {
        uint32_t cur_len = step_samples(t, period, t->seq_idx);
        if (t->seq_pos < cur_len && t->seq_pos != 0x7FFFFFFFu + n)
            break;
        t->seq_pos = t->seq_pos >= 0x7FFFFFFFu ? (chain.running ? chain.carry : 0u) : t->seq_pos - cur_len;
        t->seq_idx = (uint16_t)((t->seq_idx + 1u) % (len ? len : 1u));
        if (!t->seq_idx && !chain.running && t->pattern_next < NPAT) {
            pattern_switch(t, t->pattern_next);
            t->seq_idx = 0;
            period = seq_div_samples((uint32_t)t->p[P_SDIV]);
            len = (uint32_t)t->p[P_SLEN];
        }
        cap_step[trk_index(t)]++;
        if (!t->seq_idx && t == &trk[0] && !chain.running)
            jam_loop();
        motion_step(t, t->seq_idx, &motion);
        seq_erase_pass(t, t->seq_idx, 1);
        if (!seq_erase_active(t)) rec_hold(t, t->seq_idx, len ? len : 1u);
        {
            const step_t *s = &seq_steps(t)[t->seq_idx];
            uint32_t skip = 0, i, k;
            if (t->rskip_n && t->rskip_idx == t->seq_idx) {
                for (k = 0; k < t->rskip_n; k++) {
                    for (i = 0; i < s->n; i++)
                        if (s->note[i] == t->rskip[k])
                            skip |= 1u << i;
                    for (i = 0; i < NLANE; i++)
                        if (((s->hit >> i) & 1u) && DRUM_LANE_NOTE[i] == t->rskip[k])
                            skip |= 1u << (8u + i);
                }
                t->rskip_n = 0;
            }
            seq_step(t, s, step_gate(s) ? step_samples(t, period, t->seq_idx) : period, skip);
        }
    }
}

/* Split at pattern boundaries so raw playback follows bank changes and fires
 * loop zero once. MIDI batches may cross several boundaries in one call. */
static void seq_tick(track_t *t, uint32_t n)
{
    seq_erase_pass(t, t->seq_idx, 0);
    if (!(recording_tracks & (1u << trk_index(t)))) { seq_grid_tick(t, n); return; }
    if (!song.playing) { seq_grid_tick(t, n); return; }
    if (t->seq_pos >= 0x7FFFFFFFu) {
        seq_grid_tick(t, n);
        recording_zero(t);
        return;
    }
    while (n) {
        uint32_t period = seq_div_samples((uint32_t)t->p[P_SDIV]);
        uint32_t span = step_samples(t, period, t->seq_idx);
        uint32_t chunk = t->seq_pos < span ? span - t->seq_pos : 1u;
        if (chunk > n) chunk = n;
        uint32_t before = t->seq_idx;
        recording_tick(t, chunk);
        seq_grid_tick(t, chunk);
        if (!t->seq_idx && (before || t->p[P_SLEN] == 1) && t->seq_pos < chunk) recording_zero(t);
        n -= chunk;
    }
}

/* MIDI in: the track a channel plays (0..15): G_ROUTE CH1-4 (0) channels 1..4 their parts, SEL (1) none */
/* the track MIDI channel ch (0..15) plays: the DRUM channel the first DRUM track (none: the selected one), then ROUT
 * CH1-4 channels 1..4 tracks 1..4, every other channel the selected track */
static track_t *midi_track(uint32_t ch)
{
    uint32_t k;
    if (settings_drumch && ch + 1u == settings_drumch) {
        for (k = 0; k < NPART; k++)
            if (drum_track(&trk[k]))
                return &trk[k];
        return TSEL;
    }
    return ch < NPART && !song.g[G_ROUTE] ? &trk[ch] : TSEL;
}

#include "midi_control.c"
#include "midi_clock.c"

static void seq_advance(uint32_t n)
{
    uint32_t i, beat = seq_beat_samples();
    chain_tick(n);
    for (i = 0; i < NTRK; i++)
        seq_tick(&trk[i], n);
    for (i = 0; i < NPART; i++)
        arp_step(&trk[i], n, n);
    beat_pos += n;
    while (beat_pos >= beat) {
        beat_pos -= beat;
        beat_n = (beat_n + 1u) & 3u;
        if(song.playing && click_wanted())click_req=beat_n?1:2;
    }
}

/* everything that happens between two rendered blocks */
static void events_block(uint32_t n)
{
    uint32_t i, pr;
    uint32_t clock_mode = (uint32_t)song.g[G_CLOCK];
    if (midi_clock.mode != clock_mode) {
        uint32_t internal_beat = (uint32_t)FS * 60u / (uint32_t)song.g[G_BPM];
        uint32_t old_beat = midi_clock.mode ? MIDI_BEAT_UNITS : internal_beat;
        uint32_t new_beat = clock_mode ? MIDI_BEAT_UNITS : internal_beat;
        song.g[G_CLOCK] = (int16_t)midi_clock.mode;
        seq_stop();
        song.g[G_CLOCK] = (int16_t)clock_mode;
        /* Continue and a latched ARP preserve their fractional phase when
         * crossing between sample time and MIDI musical units. */
        for (i = 0; i < NTRK; i++) {
            track_t *t = &trk[i];
            if (t->seq_pos < 0x7FFFFFFFu)
                t->seq_pos = (uint32_t)((uint64_t)t->seq_pos * new_beat / old_beat);
            if (t->arp_pos < 0xFFFFFFFu)
                t->arp_pos = (uint32_t)((uint64_t)t->arp_pos * new_beat / old_beat);
            t->arp_off = (uint32_t)((uint64_t)t->arp_off * new_beat / old_beat);
        }
        beat_pos = (uint32_t)((uint64_t)beat_pos * new_beat / old_beat);
        memset(&midi_clock, 0, sizeof midi_clock);
        midi_clock.mode = (uint8_t)clock_mode;
        midi_beat_samples = 0;
    }
    if (transport_req == 1u) {
        if (clock_mode)
            midi_clock_transport(0xFAu, fm1_ms);
        else if(settings_countin && song.rec && !chain.armed && !chain.running) {
            seq_stop(); memset(cin_n,0,sizeof cin_n);cin_total=cin_left=settings_countin*4u; cin_pos=0; click_req=2;
        } else seq_start();
        transport_req = 0;
    } else if (transport_req == 2u) {
        seq_stop();
        transport_req = 0;
    } else if (transport_req == 3u) {                 /* GLO + PLAY: from the top without stopping (INT clock) */
        if (!clock_mode) {
            seq_stop();
            seq_start();
        }
        transport_req = 0;
    }
    pr = panic_req;
    panic_req = 0;
    if (midi_in_overflow) {                            /* a lost note-off must never leave a held note */
        mi_r = mi_w;
        memset(midi_sel_on, 0, sizeof midi_sel_on);
        memset(midi_ch, 0, sizeof midi_ch);
        memset(midi_owners, 0, sizeof midi_owners);
        memset(mchord, 0, sizeof mchord);
        midi_hint = 0;
        for (i = 0; i < NTRK; i++) {
            trk[i].rh_n = trk[i].rskip_n = 0;
            trk[i].seq_n = trk[i].seq_hold = trk[i].slide_glide = 0;
        }
        pr |= (1u << NTRK) - 1u;
        RING_PUBLISH();
        midi_in_overflow = 0;
    }
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        if ((pr >> i) & 1u) {
            midi_forget_track(i);
            recording_stop(t);
            t->rh_n = t->rskip_n = 0;
            trk_all_off(t);
            t->nheld = 0;
            t->arp_phys = 0;
            t->arp_note = 0;
        }
        engine_block(t);                              /* engine switch: fade, then switch (voice.c) */
        /* ARP turned off, or HOLD released with no key down: drop the latched chord */
        if ((t->armp && !t->p[P_AMODE]) || (t->aholdp && !t->p[P_AHOLD] && !t->arp_phys)) {
            t->nheld = 0;
            if (!t->p[P_AMODE])
                t->arp_phys = 0;
            if (t->arp_note) {
                trk_note_off(t, t->arp_note);
                t->arp_note = 0;
            }
        }
        t->armp = t->p[P_AMODE];
        t->aholdp = t->p[P_AHOLD];
    }
    audition_tick(n);
    keyboard_block();
    while (mi_r != mi_w) {                            /* USB-MIDI (and TRS) in */
        uint32_t at = mi_r % MQ, pkt = midi_in_q[at], status = (pkt >> 8) & 0xFFu;
        uint32_t st = status & 0xF0u, ch = status & 0x0Fu;
        uint32_t d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        uint32_t source = midi_in_source[at] ? midi_in_source[at] : 1u, ms = midi_in_ms[at];
        uint32_t time = midi_in_time[at];
        if (midi_render_timed && (int32_t)(time - midi_render_time) > 0)
            break;
        mi_r++;
        if (status >= 0xF8u) {
            if (clock_mode == source) {
                if (status == 0xF8u)
                    midi_clock_pulse(ms, time);
                else {
                    if (song.playing)
                        seq_advance(midi_clock_advance(time));
                    midi_clock_transport(status, ms);
                }
                if (song.playing)
                    seq_advance(midi_clock_advance(time));
            }
        } else {
            if (clock_mode && song.playing)
                seq_advance(midi_clock_advance(time));
            midi_event(st, ch, d1, d2);
        }
    }
    if (clock_mode) {
        if (song.playing) {
            uint32_t last = midi_clock.have_pulse ? midi_clock.last_ms : midi_clock.start_ms;
            if (fm1_ms - last > 500u) {
                seq_stop();                         /* cable loss must not leave a running held note */
                midi_clock.tempo_valid = 0;
            } else
                seq_advance(midi_clock_advance(midi_render_timed ? midi_render_time : MIDI_TIME_NOW()));
        }
        if (!song.playing)
            for (i = 0; i < NPART; i++)
                arp_step(&trk[i], 0, (uint32_t)((uint64_t)n * MIDI_BEAT_UNITS / beat_samples()));
    } else {
        uint32_t left=n;
        if(cin_left) {
            uint32_t beat=beat_samples();if(cin_pos>=beat)cin_pos=beat-1u;
            while(cin_left && left) {
                uint32_t take=beat-cin_pos; if(take>left)take=left;
                cin_pos+=take; left-=take;
                if(cin_pos>=beat) {
                    cin_pos=0; cin_left--;
                    if(cin_left)click_req=((cin_total-cin_left)&3u)?1:2;
                    else { seq_start(); seq_advance(0);
                        for(uint32_t tr=0;tr<NTRK;tr++)if(rec_on(&trk[tr]) && !trk[tr].p[P_AMODE])
                          for(uint32_t i=0;i<cin_n[tr];i++) { uint32_t note=cin_note[tr][i];
                            if(live_held[tr][note>>5]&(1u<<(note&31u)))rec_note(&trk[tr],note,cin_vel[tr][i]); }
                        if(!click_wanted())click_req=2; }
                }
            }
        }
        if(left)seq_advance(left);
    }
    if (song.playing)
        song.tick++;
}
