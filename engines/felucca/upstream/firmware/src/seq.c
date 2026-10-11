/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Keyboard, scale, arpeggiator, sequencer and transport.
 * Everything here runs in the audio ISR, once per CTL-sample block, and
 * ends in trk_note_on / trk_note_off: engines never see where a note came from.
 * Four tracks, one transport: every track's pattern loops on its own LEN / DIV /
 * SWING / GATE (polymeter). The keys play the selected track; MIDI IN by MENU > MIDI > MIDI IN (G_ROUTE):
 * CH1-4 (0) channels 1..4 play parts 1..4, channels 5..16 are ignored (midi_control.c midi_event); SEL (1)
 * every channel the selected track; 1.2: CH5-8, CH9-12, CH13-16 as CH1-4 on those four channels (midi_base). Their CC1 (mod wheel), CC11 (expression) and channel aftertouch go to
 * the same track's modulation matrix (mod.c; the selected track: the one selected then).
 * A note into an armed track (song.rec) while
 * the transport runs is recorded into its pattern, quantised to its (swung) steps, with its
 * held length as TIE steps (rec_note, rec_hold, rec_release). With the ARP on, the notes the arp
 * plays are recorded, not the keys held (what plays back is what was heard). A key or MIDI note plays a
 * chord when the track's CHRD is on (chord.c): its notes go through the same input as so many keys. */
static const uint16_t SCALE_MASK[] = {
    0xFFF,                                   /* CHR */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11),   /* MAJ */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10),   /* MIN */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 10),   /* DOR */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 10),   /* MIX */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 7) | (1 << 9),                          /* PEN */
    (1 << 0) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 10),                         /* MPEN */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 11),   /* HARM */
    (1 << 0) | (1 << 1) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10),   /* PHRY */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 6) | (1 << 7) | (1 << 9) | (1 << 11),   /* LYD */
    (1 << 0) | (1 << 1) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 8) | (1 << 10),   /* LOC */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11),   /* MEL (ascending) */
    (1 << 0) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 10),              /* BLUES (minor) */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 6) | (1 << 8) | (1 << 10),              /* WHOLE */
    (1 << 0) | (1 << 1) | (1 << 3) | (1 << 4) | (1 << 6) | (1 << 7) | (1 << 9) | (1 << 10), /* DIMHW */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 8) | (1 << 9) | (1 << 11), /* DIMWH */
};

#define KB_SILENT 255u
static uint32_t kb_prev;
static volatile uint8_t kb_boot_hold;    /* #137: set from power-on to the end of the splash (main.c): the keys sound nothing,
                                          * and one held across its end stays silent until it is let go and pressed again */
static uint8_t kb_note[27], kb_trk[27];  /* per key: the note it started and on which track */
static uint8_t in_key;                   /* the key (+ 1) whose notes go into input_on now (key_on), 0: none (MIDI IN) */
/* #191: where each track's held[i] came from, so the arp maps it again as it plays (arp_list: TRN, ROOT, SCALE, QNT as
 * they are now). held_key: the key + 1 (bits 0..4) and the octave buttons then + 3 (bits 5..7); 0: not a key's own
 * mapping (MIDI IN, the DRUM grid, an engine's own key map), held[i] plays as it is. held_dt: held[i] above the key's
 * note (a CHRD chord's other notes). Out of track_t: its size stays as the audio ISR's trk[] indexing wants it */
static uint8_t held_key[NTRK][16];
static int8_t held_dt[NTRK][16];
static volatile uint8_t kb_asleep;       /* MENU > SCREEN OFF (ui.c): the screen dark or waking, a key pressed now only
                                          * wakes it (silent, no layer, its release nothing) */
static uint8_t last_note = 60;
static volatile uint8_t transport_req;   /* 1 start, 2 stop, 3 restart (from the UI) */
static volatile uint8_t panic_req;       /* bit per track: release every sounding note (preset / engine change) */
static volatile uint8_t midi_hint;       /* MIDI IN played track n - 1, which is not the selected one (the UI says so) */

static uint32_t trk_index(const track_t *t) { return (uint32_t)(t - trk); }

/* MENU > MIDI > MIDI IN (G_ROUTE, up to 1.1.5 GLO > SYSTEM ROUT): the first channel (0..15) of the block of four that
 * plays tracks 1..4, -1 with SEL. Stored
 * 0 CH1-4, 1 SEL, then (1.2, Discussion #143) 2 CH5-8, 3 CH9-12, 4 CH13-16 */
static int32_t midi_base(void)
{
    uint32_t r = (uint32_t)song.g[G_ROUTE];
    return r == 1u ? -1 : r ? (int32_t)(r - 1u) * 4 : 0;
}
/* MIDI channel 0..15 of track i (keys -> MIDI out): its channel in ROUT's block (SEL, CH1-4: 1..4) */
static uint32_t trk_midi_ch(uint32_t i) { return (midi_base() > 0 ? (uint32_t)midi_base() : 0u) + i % NTRK; }

static int drum_track(const track_t *t) { return ENGINES[eng_idx(t->eng_req)] == &ENG_DRUM; }

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
/* the DRUM grid: key k's lane note (black keys 1..8), KB_SILENT for the rest. Out of line (1.5): called from the audio
 * ISR's key loop only on a key change; inlined there, its % 12 and key_place's loop sat in the ISR's static cost
 * (tests/target_budget.py) for a path that runs once per key press */
static __attribute__((noinline)) uint8_t grid_key_note(uint32_t k)
{
    return key_black(k) && key_place(k) < NLANE ? DRUM_LANE_NOTE[key_place(k)] : KB_SILENT;
}
/* GLO > SONG (song.grid 3): key k's slot, the white keys A3 B3 C4 D4 (white places 2..5: keys 4 6 7 9 from F) A..D; -1 = a
 * key that plays. A table, not key_place (a loop); the audio ISR's key loop reads SONG_KEYS, the same keys */
#define SONG_KEYS (1u << 4 | 1u << 6 | 1u << 7 | 1u << 9)
static int32_t song_key(uint32_t k)
{
    static const int8_t S[10] = {-1, -1, -1, -1, 0, -1, 1, 2, -1, 3};
    return k < 10u ? S[k] : -1;
}

static uint32_t scale_mask(const track_t *t)
{
    return SCALE_MASK[clamp(t->p[P_SCALE], 0, sizeof SCALE_MASK / sizeof SCALE_MASK[0] - 1)];
}

/* note n moved down onto the track's scale (ROOT, SCALE): QNT SNAP's keys and QNT SEQ's sequenced notes */
static int32_t scale_snap(const track_t *t, int32_t n)
{
    uint32_t mask = scale_mask(t), guard = 12;
    while (guard-- && !((mask >> (uint32_t)((n - t->p[P_ROOT] + 120) % 12)) & 1u))
        n--;
    return n;
}

enum { QN_OFF, QN_SNAP, QN_WHITE, QN_SEQ };      /* P_QUANT */

/* the engine's own key map (DRUM's GM map, slices) for key k, -1 none */
static int32_t kb_engine(const track_t *t, uint32_t k)
{
    const engine_t *e = ENGINES[eng_idx(t->eng_req)];   /* (the engine it switches to) */
    return e->keys ? e->keys(t, k) : -1;
}

/* key k's note with the octave buttons at oct: QNT, ROOT, SCALE, TRN (KB_SILENT: a black key under WHITE). The keys
 * (kb_map) and the arp's latched keys (arp_list, #191: as the keys play now, the octave they were pressed in) */
static uint32_t kb_pitch(const track_t *t, uint32_t k, int32_t oct)
{
    static const int8_t DEGREE[12] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};
    int32_t n = 53 + (int32_t)k;
    if (t->p[P_QUANT] == QN_SNAP || t->p[P_QUANT] == QN_SEQ)   /* SNAP (and SEQ): every key, rounded down */
        return (uint32_t)clamp(scale_snap(t, n + 12 * oct + t->p[P_TRANS]), 0, 127);
    if (t->p[P_QUANT] == QN_WHITE) {                    /* WHITE: white keys walk the scale, black keys are silent */
        uint32_t mask = scale_mask(t), i;
        int32_t count = 0, degree = DEGREE[n % 12], wo;
        if (degree < 0)
            return KB_SILENT;
        /* C4 is the root. Walk scale degrees on successive white keys, including
         * below C4; scales with 5, 6, 8 or 12 notes still have no duplicated degrees. */
        degree += (n / 12 - 5) * 7;
        for (i = 0; i < 12u; i++)
            count += (mask >> i) & 1u;
        wo = degree / count;
        degree %= count;
        if (degree < 0) {
            degree += count;
            wo--;
        }
        for (i = 0; i < 12u; i++)
            if ((mask >> i) & 1u) {
                if (!degree)
                    break;
                degree--;
            }
        n = 60 + t->p[P_ROOT] + 12 * wo + (int32_t)i;
    }
    return (uint32_t)clamp(n + 12 * oct + t->p[P_TRANS], 0, 127);
}

static uint32_t kb_map(const track_t *t, uint32_t k)
{
    int32_t n = kb_engine(t, k);
    return n >= 0 ? (uint32_t)n : kb_pitch(t, k, song.octave);
}

#include "chord.c"

/* ------------------------------------------------------------- arp --- */
static void arp_add(track_t *t, uint32_t note)
{
    uint32_t i, k = in_key - 1u;
    if (t->p[P_AHOLD] && t->arp_phys == 0u)
        t->nheld = 0;                               /* new chord replaces the latched one */
    t->arp_phys++;                                  /* every key-down: arp_remove counts every key-up */
    for (i = 0; i < t->nheld; i++)
        if (t->held[i] == note)
            return;                                 /* repeated note-on: not a new note */
    if (t->nheld < 16u) {
        /* #191: a key the track's scale keyboard maps (not the DRUM grid, not an engine's own map) is kept as that
         * key, so TRN, ROOT, SCALE and QNT reach the notes the arp holds (arp_list); the octave stays the one it was
         * pressed in. Anything else (MIDI IN) plays as it came, as before */
        uint8_t *hk = held_key[trk_index(t)];
        hk[t->nheld] = in_key && song.grid != 1u && kb_engine(t, k) < 0
                           ? (uint8_t)(in_key | (uint32_t)(song.octave + 3) << 5) : 0u;
        held_dt[trk_index(t)][t->nheld] = (int8_t)(hk[t->nheld] ? (int32_t)note - kb_note[k] : 0);
        t->held[t->nheld++] = (uint8_t)note;
    }
    if (t->nheld == 1u) {
        t->arp_pos = 0xFFFFFFF;                     /* fire on this block */
        t->arp_idx = 0xFFFFFFFFu;
    }
}

static void arp_remove(track_t *t, uint32_t note)
{
    uint32_t i, k = 0;
    uint8_t *hk = held_key[trk_index(t)];
    int8_t *dt = held_dt[trk_index(t)];
    if (t->arp_phys)
        t->arp_phys--;
    if (t->p[P_AHOLD])
        return;
    for (i = 0; i < t->nheld; i++)
        if (t->held[i] != note) {
            hk[k] = hk[i];
            dt[k] = dt[i];
            t->held[k++] = t->held[i];
        }
    t->nheld = (uint8_t)k;
}

#define ND_NONE 0u                      /* track_t nd_late / nd_early: nothing latched (seq_tick; else its seq_pos + 1) */
static void rec_note(track_t *t, uint32_t note, uint32_t vel);
static void rec_release(track_t *t, uint32_t note);
static int rec_on(const track_t *t) { return ((song.rec >> trk_index(t)) & 1u) && song.playing && !chain.running; }

#include "motion.c"

/* the ARP's modes (P_AMODE). Append-only: a stored value keeps its meaning. ORD (1.2, it played as UP before) plays
 * the held notes in the order they were pressed, whatever ARP 2's ORD switch says (that switch orders the list of the
 * other modes). 1.2 (Discussion #95), over the note list (the held notes low to high, or as played,
 * across OCT octaves), C E G held: DNUP G E C E; UP+8 C E G C' (the first note again an octave above the top);
 * CONV C G E (outside in); DIVG E G C (inside out); PINKY C G E G (the top note between the others); THUMB
 * C E C G (the bottom one); WALK a random step up or down the list (bouncing off its ends); CHORD all the held
 * notes together (the first 4), an octave higher each step through OCT */
enum { AM_OFF, AM_UP, AM_DN, AM_UPDN, AM_RND, AM_ORD, AM_REPEAT, AM_DNUP, AM_UP8, AM_CONV, AM_DIVG, AM_PINKY,
       AM_THUMB, AM_WALK, AM_CHORD };

static void arp_forget(track_t *t)               /* (its notes ended otherwise: trk_all_off) */
{
    t->arp_note = t->arp_ch[0] = t->arp_ch[1] = t->arp_ch[2] = 0;
}

/* CHORD's notes. Not inlined: they run once an arp step, and kept out of the audio ISR's loop over the tracks
 * (tests/target_budget.py) */
static __attribute__((noinline)) void arp_chord_off(track_t *t, int rec)   /* the other notes end */
{
    uint32_t i;
    for (i = 0; i < 3u; i++)
        if (t->arp_ch[i]) {
            if (rec)
                rec_release(t, t->arp_ch[i]);
            trk_note_off(t, t->arp_ch[i]);
            t->arp_ch[i] = 0;
        }
}

static __attribute__((noinline)) void arp_chord(track_t *t, const uint32_t *list, uint32_t n)
{
    uint32_t i, nt, top = 0, o = t->arp_idx % (uint32_t)(t->p[P_AOCT] > 0 ? t->p[P_AOCT] : 1);
    if (n > 4u)
        n = 4u;
    for (i = 0; i < n; i++) {                       /* the held notes (sorted or as played), o octaves up */
        nt = (uint32_t)clamp((int32_t)list[i] + 12 * (int32_t)o, 0, 127);
        if (nt == 127u && top++)
            continue;                               /* (notes clamped onto the top one: once) */
        if (!t->arp_note)
            t->arp_note = (uint8_t)nt;
        else
            t->arp_ch[i - 1u] = (uint8_t)nt;
        if (rec_on(t))
            rec_note(t, nt, 100);
        trk_note_on(t, nt, 100);
    }
}

static void arp_silence(track_t *t)              /* the sounding arp note(s) end */
{
    if (t->arp_ch[0] | t->arp_ch[1] | t->arp_ch[2])
        arp_chord_off(t, 0);
    trk_note_off(t, t->arp_note);
    t->arp_note = 0;
}

static void arp_off(track_t *t)                  /* the sounding arp note(s) end (a recorded one too) */
{
    if (t->arp_ch[0] | t->arp_ch[1] | t->arp_ch[2])
        arp_chord_off(t, rec_on(t));
    trk_note_off(t, t->arp_note);
    if (rec_on(t))
        rec_release(t, t->arp_note);
    t->arp_note = 0;
}

/* the place in the note list (len notes) the step arp_idx plays (not inlined, as arp_chord); UP+8 adds its note */
static __attribute__((noinline)) uint32_t arp_pick(track_t *t, uint32_t *list, uint32_t len)
{
    uint32_t i = t->arp_idx, m = (uint32_t)t->p[P_AMODE], cyc, k, w;
    if (m == AM_UP8 && len < 64u)                   /* UP+8: the first note again over the top */
        list[len++] = (uint32_t)clamp((int32_t)list[0] + 12 * t->p[P_AOCT], 0, 127);
    cyc = len > 1u ? 2u * len - 2u : 1u;
    k = i % cyc;
    switch (m) {
    case AM_DN:
        return len - 1u - i % len;
    case AM_UPDN:
        return k < len ? k : cyc - k;
    case AM_DNUP:
        return k < len ? len - 1u - k : k - (len - 1u);
    case AM_RND:
        return rng() % len;
    case AM_CONV:
    case AM_DIVG:
        k = i % len;
        if (m == AM_DIVG)
            k = len - 1u - k;
        return (k & 1u) ? len - 1u - k / 2u : k / 2u;
    case AM_PINKY:
        return len < 2u ? 0u : (k & 1u) ? len - 1u : k / 2u;
    case AM_THUMB:
        return len < 2u ? 0u : (k & 1u) ? 1u + k / 2u : 0u;
    case AM_WALK:
        w = t->arp_walk;
        if (!i)
            w = 0;                                  /* (from the bottom) */
        else if (w >= len)
            w = len - 1u;                           /* (notes let go) */
        else if (len > 1u)
            w = !w ? 1u : w == len - 1u ? w - 1u : (rng() >> 7) & 1u ? w + 1u : w - 1u;
        t->arp_walk = (uint8_t)w;
        return w;
    default:
        return i % len;
    }
}

/* n: samples the arp advances by (the external clock's while it runs, 0 stopped); gate_n: samples its
 * gate counts down by (real time when the external transport stops: a tapped or latched note still ends) */
static volatile uint32_t beat_pos, beat_n;      /* samples into the beat, the beat of the bar (0..3) */

/* the metronome (click.c) and the count-in (1.1, Discussion #131). The click follows the song tempo's quarter notes
 * (BPM, or the external clock's 24 pulses), 4 to the bar, the first accented, whatever a track's DIV or SWING: a beat is
 * four 1/16 steps as the sequencer counts them (click_beat_len: 4 x div_samples(1/16), which may be a few samples
 * shorter than beat_samples, so the click never drifts from the 1/16 tracks it is recorded against). clk_pos and
 * clk_step count the 1/16 steps as seq_tick counts a 1/16 track's (and midi_clock_pulse rescales clk_pos with them when
 * an external clock's tempo moves): CLK_START at seq_start clicks the first beat in the block step 0 starts in, and
 * beat k in the block a 1/16 track's step 4k starts in (every 4th step; step 0 of 16, the bar's first, accented).
 * The count-in: with MENU > COUNT-IN on, PLAY from stop with a track armed (REC while stopped starts PLAY too) on
 * the internal clock counts cin_total beats first (1 or 2 bars, clicked whatever CLICK is), then
 * starts the sequencer at step 1 exactly that many beats later (song.playing stays 0 until then: nothing plays or
 * records; the keys sound). A note into an armed track in the count-in's last eighth (the second half of its last
 * beat) is recorded onto step 1 when it starts (cin_keep, cin_flush), held on or let go as it was. An external clock
 * never counts in (its Start starts). STOP (seq_stop) cancels it. */
#define CLK_START 0xFFFFFFFFu
static uint32_t clk_pos = CLK_START, clk_step;  /* samples into the 1/16 step, the step of the bar (0..15) */
static volatile uint8_t cin_left, cin_total;    /* the count-in's beats still to come (0: none), of how many */
static volatile uint32_t cin_pos;               /* samples into its beat playing */
static uint8_t cin_flush;                       /* the count-in ended in this block: record what was kept */
static uint8_t cin_note[NTRK][4], cin_vel[NTRK][4], cin_n[NTRK], cin_up[NTRK];   /* kept notes; cin_up bit k: let go */
static int seq_counting(void) { return cin_left != 0u; }
static uint32_t click_beat_len(void) { return 4u * div_samples(2); }   /* DIV 2: 1/16 */
static int click_wanted(void)                  /* CLICK REC: while a track is armed (not in a song: no recording) */
{
    return click_mode == CLICK_ON || (click_mode == CLICK_REC && song.rec && !chain.running);
}
/* the arp's note list (list, its length returned; 0: nothing to play): the held notes (sorted or as played) over OCT
 * octaves, *cnt of them. #191: the notes as they play now, a key's mapped again through kb_pitch (TRN, ROOT, SCALE,
 * QNT now, the octave it was pressed in; a chord's note as far above it), the rest (MIDI IN) as they came; each once
 * (two keys may meet on one note now), a key silent now (a black key, QNT WHITE since) left out. With nothing changed
 * since the keys were pressed, exactly the list of held[] as before */
static uint32_t arp_list(const track_t *t, uint32_t *list, uint32_t *cnt)
{
    uint32_t i, j, n = 0, x, s, last = 0, len = 0, o;
    const uint8_t *hk = held_key[trk_index(t)];
    const int8_t *dt = held_dt[trk_index(t)];
    for (i = 0; i < t->nheld; i++) {
        x = t->held[i];
        if ((s = hk[i]) != 0u) {
            x = kb_pitch(t, (s & 31u) - 1u, (int32_t)(s >> 5) - 3);
            if (x == KB_SILENT)
                continue;
            x = (uint32_t)clamp((int32_t)x + dt[i], 0, 127);
        }
        last = x;
        for (j = 0; j < n && list[j] != x; j++)
            ;
        if (j == n)
            list[n++] = x;
    }
    *cnt = n;
    if (!t->p[P_AORDER] && t->p[P_AMODE] != AM_ORD)   /* (ORD: as pressed, held[] keeps that order) */
        for (i = 1; i < n; i++)
            for (j = i; j > 0 && list[j - 1] > list[j]; j--) {
                x = list[j];
                list[j] = list[j - 1];
                list[j - 1] = x;
            }
    for (o = 0; o < (uint32_t)t->p[P_AOCT]; o++)
        for (i = 0; i < n && len < 64u; i++)
            list[len++] = clamp((int32_t)list[i] + 12 * (int32_t)o, 0, 127);
    if (n && t->p[P_AMODE] == AM_REPEAT) { list[0] = last; len = 1; } /* REPEAT: last played note, no octave traversal */
    return len;
}

/* an arp step starts (period: its length): the next note of the list. Not inlined: once a step, kept out of the audio
 * ISR's loop over the tracks (tests/target_budget.py) */
static __attribute__((noinline)) void arp_fire(track_t *t, uint32_t period)
{
    uint32_t cnt, list[64], len, j;
    if (!(len = arp_list(t, list, &cnt))) {        /* (every key silent now: QNT WHITE's black keys) */
        if (t->arp_note)
            arp_off(t);
        return;
    }
    t->arp_idx++;
    j = t->p[P_AMODE] == AM_CHORD ? 0u : arp_pick(t, list, len);
    if (t->arp_note)
        arp_off(t);
    if ((uint32_t)(rng() & 127u) <= (uint32_t)t->p[P_APROB]) {
        t->arp_off = period * (uint32_t)t->p[P_AGATE] / 128u;
        if (t->p[P_AMODE] == AM_CHORD) {            /* CHORD: the first 4 notes, an octave up each step */
            arp_chord(t, list, cnt);
            return;
        }
        t->arp_note = (uint8_t)list[j];
        if (rec_on(t))
            rec_note(t, t->arp_note, 100);           /* recording: what the arp plays */
        trk_note_on(t, t->arp_note, 100);
    }
}

static void arp_step(track_t *t, uint32_t n, uint32_t gate_n)
{
    uint32_t period;
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
    period = div_samples((uint32_t)t->p[P_ARATE]);
    sw = t->p[P_ASWING] * (int32_t)period / 250;
    t->arp_pos += n;
    if (t->arp_pos < period + (uint32_t)((t->arp_idx & 1u) ? sw : -sw) && t->arp_pos != 0xFFFFFFF + n)
        return;
    t->arp_pos = 0;
    arp_fire(t, period);
}

static void arp_tick(track_t *t, uint32_t n) { arp_step(t, n, n); }

/* -------------------------------------------------------- note input --- */
/* the length of step idx in samples: SWING (the track's + the global, at most 100) makes the even steps longer
 * and the odd ones shorter, so every odd step starts late */
static uint32_t step_samples(const track_t *t, uint32_t period, uint32_t idx)
{
    return swing_step_len(t, period, idx);   /* core.h: own + global, at most 100 */
}

/* NUDGE (1.2, #162): a note `off` samples from the start of the step it goes into, in 1/NUDGE_DIV of the step (period),
 * rounded (step_set_nudge keeps -8..+7): the first note of a fresh step sets it; QUANTIZE (P_SQNT) decides whether it
 * plays (not inlined: its divide stays out of the audio ISR's loops) */
static __attribute__((noinline)) int32_t rec_nudge(int32_t off, uint32_t period)
{
    return (off * NUDGE_DIV + (off < 0 ? -(int32_t)period / 2 : (int32_t)period / 2)) / (int32_t)(period ? period : 1u);
}

/* live recording: the note goes into the nearest step, as swung (the one playing, or
 * the next one when it is past the middle of the playing one). Overdub: a step that
 * holds notes gets this one added (a chord of up to 4; when full, the last note is
 * replaced); MONO / LEGATO / UNISON parts keep one note per step, as step entry does.
 * Held on (synth parts): each further step the sequencer enters while the note is
 * held becomes a TIE (rec_hold), up to the pattern length; a release before the middle
 * of the last one puts that step back (rec_release), so a short note stays one step.
 * A note recorded into another step ends the hold before (the step model ties the
 * notes of one step only). */
static void rec_note(track_t *t, uint32_t note, uint32_t vel)
{
    uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u, idx = t->seq_idx % len, k;
    uint32_t period = div_samples((uint32_t)t->p[P_SDIV]), cur = step_samples(t, period, t->seq_idx);
    uint32_t next = t->seq_pos > cur / 2u, sounds;
    int32_t off = rec_nudge(t->seq_pos >= cur ? 0 : next ? -(int32_t)(cur - t->seq_pos) : (int32_t)t->seq_pos, period);
    step_t *s;
    if (next)
        idx = (idx + 1u) % len;
    s = &t->step[idx];
    /* the step's notes play later in this pass (it is the next one, not played early yet, or the one playing whose
     * nudge has not come yet): what sounds now must not play again there (rskip) */
    sounds = next ? t->nd_fired != idx + 1u : t->nd_late != ND_NONE;
    if (drum_track(t)) {                            /* DRUM: into the grid, no holds (hits) */
        uint32_t l = drum_lane(note);
        if (s->time != ST_NOTE || (!s->n && !s->hit)) {
            s->n = s->hit = s->acc = 0;
            s->flags = 0;
            s->vel = 0;
            step_set_nudge(s, off);
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
        if (sounds) {
            if (t->rskip_idx != idx)
                t->rskip_n = 0;
            t->rskip_idx = (uint8_t)idx;
            if (t->rskip_n < NLANE)
                t->rskip[t->rskip_n++] = (uint8_t)note;
        }
        return;
    }
    if (s->time != ST_NOTE || !s->n || trk_vmode(t) != V_POLY) {
        s->n = 0;                                   /* a fresh step */
        s->flags = 0;
        s->vel = 0;
        step_set_nudge(s, off);
    }
    for (k = 0; k < s->n && s->note[k] != note; k++)
        ;
    if (k == s->n) {
        if (s->n < 4u)
            s->n++;
        s->note[s->n - 1u] = (uint8_t)note;
    }
    s->time = ST_NOTE;
    if (vel > 110)
        s->flags |= SF_ACCENT;
    if (vel > s->vel)
        s->vel = (uint8_t)vel;
    t->seq_active = 1;
    if (sounds) {                                   /* it sounds now: the step must not trigger it again */
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
    }
    for (k = 0; k < t->rh_n && t->rh_note[k] != note; k++)
        ;
    if (k == t->rh_n && t->rh_n < 4u)
        t->rh_note[t->rh_n++] = (uint8_t)note;      /* a chord: held until its last key is up */
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
        return;                                     /* (recorded ahead into the step now starting) */
    s = &t->step[idx];
    t->rh_bak = *s;
    t->rh_last = (uint8_t)idx;
    t->rh_ties++;
    for (k = 0; k < 4u; k++)
        s->note[k] = 0;
    s->n = 0;
    s->time = ST_TIE;
    s->flags = 0;
    s->vel = 0;
    s->hit = s->acc = 0;
}

/* a key of a recorded note is up: the hold ends with the last one */
static void rec_release(track_t *t, uint32_t note)
{
    uint32_t i, k = 0;
    for (i = 0; i < t->rh_n; i++)
        if (t->rh_note[i] != note)
            t->rh_note[k++] = t->rh_note[i];
    if (k == t->rh_n || (t->rh_n = (uint8_t)k))
        return;                                     /* not one of them, or others still held */
    if (t->rh_ties && t->seq_idx == t->rh_last &&
        t->seq_pos < step_samples(t, div_samples((uint32_t)t->p[P_SDIV]), t->seq_idx) / 2u)
        t->step[t->rh_last] = t->rh_bak;            /* released early in it: not held into this step */
}

/* a note into armed track t in the count-in's last eighth: kept for step 1 (cin_flush); once each, 4 per track */
static __attribute__((noinline)) void cin_keep(track_t *t, uint32_t note, uint32_t vel)
{
    uint32_t i = trk_index(t), k;
    if (cin_left != 1u || cin_pos < click_beat_len() / 2u || !((song.rec >> i) & 1u) || t->p[P_AMODE])
        return;
    for (k = 0; k < cin_n[i] && cin_note[i][k] != note; k++)
        ;
    if (k == cin_n[i]) {
        if (k == 4u)
            return;
        cin_n[i]++;
    }
    cin_note[i][k] = (uint8_t)note;
    cin_vel[i][k] = (uint8_t)vel;
    cin_up[i] &= (uint8_t)~(1u << k);
}

static void input_on(track_t *t, uint32_t note, uint32_t vel)
{
    last_note = (uint8_t)note;
    if (cin_left)
        cin_keep(t, note, vel);
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
    if (cin_left) {                                 /* a kept note let go before step 1: recorded, then let go */
        uint32_t i = trk_index(t), k;
        for (k = 0; k < cin_n[i]; k++)
            if (cin_note[i][k] == note)
                cin_up[i] |= (uint8_t)(1u << k);
    }
    rec_release(t, note);
    arp_remove(t, note);                            /* both: the note may have started in the */
    trk_note_off(t, note);                          /* other mode (ARP switched while held) */
}

/* the effect of key k in the FX layer (PF_N none): the white keys F3 .. G5 the map's (perf_map: the settings,
 * ui_layer.c; by default 1.1.5's ten, FLANGER, PHASER, REV / ECHO THROW), black keys 1..4 the track mutes */
static uint32_t perf_key(uint32_t k)
{
    uint32_t p = key_place(k);
    return !key_black(k) ? (p < PF_KEYS ? perf_map[p] : PF_N) : p < NTRK ? PF_M1 + p : PF_N;
}

/* a key of the FX layer down (FX held) / up: the effect it holds. kb_fx: the one it took (+ 1, 0 none), so a key
 * remapped while held lets go of what it holds; an effect two keys hold ends with the last of them. noinline: out
 * of keyboard_block's loop (the audio ISR) */
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
    in_key = (uint8_t)(k + 1u);                     /* (the arp keeps the key: arp_add) */
    for (i = 0; i < n; i++) {
        uint32_t x = kb_chord[k][i];
        if (midi_local_held(t, x))
            continue;
        input_on(t, x, 100);
        midi_out_event(0x09u | (0x90u | mc) << 8 | x << 16 | 100u << 24);
    }
    in_key = 0;
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
    uint32_t lay, fx, mute;
    ch = cur ^ kb_prev;                          /* keys also sound while entering steps */
    if (perf_remap)
        perf_key_remap();
    if (!ch)
        return;
    if (kb_boot_hold) {                           /* #137: under the splash; their release is silent too */
        for (k = 0; k < 27u; k++)
            if ((ch & cur) >> k & 1u)
                kb_note[k] = KB_SILENT;
        kb_prev = cur;
        return;
    }
    lay = (fm1_in.buttons & kb_mask) || kb_lock;  /* (#83: kb_lock, a layer locked open with no button held) */
    fx = (fm1_in.buttons & perf_mask) || (kb_lock & 2u);
    mute = song.grid == 2u ? ~0u : song.grid == 3u ? SONG_KEYS : 0u;   /* the keys that never sound here */
    for (k = 0; k < 27u; k++) {
        if (!((ch >> k) & 1u))
            continue;
        if ((cur >> k) & 1u) {                    /* the selected track; the key-up goes to the same one */
            kb_trk[k] = song.sel;
            if (kb_asleep) {                      /* (SCREEN OFF: the wake press is swallowed) */
                kb_note[k] = KB_SILENT;
                continue;
            }
            if (lay) {                            /* a layer's button held (or locked): the key is the layer's */
                kb_note[k] = KB_SILENT;           /* (ui_layer.c), with FX an effect (perform.c) */
                kb_layer |= 1u << k;
                if (fx)
                    perf_key_press(k, 1);
                continue;
            }
            if ((mute >> k) & 1u)                 /* NAME (ui_name.c): every key types, none sounds; SONG: A..D */
                kb_note[k] = KB_SILENT;
            else if (song.grid == 1u)             /* the DRUM grid: a lane key plays its lane, the rest are the UI's */
                kb_note[k] = grid_key_note(k);
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

/* -------------------------------------------------------- sequencer --- */
static void seq_start(void)
{
    uint32_t i;
    motion_begin();
    chain_start();
    for (i = 0; i < NTRK; i++) {                   /* every track from its step 0, together */
        track_t *t = &trk[i];
        t->seq_idx = (uint16_t)(t->p[P_SLEN] - 1);
        t->seq_pos = 0x7FFFFFFF;                   /* step 0 fires on the first block */
        t->rskip_n = 0;
        t->rh_n = 0;
        t->rat_left = 0;
        t->nd_late = t->nd_early = ND_NONE;
        t->nd_fired = 0;
        if (t->p[P_LTRIG] && t->p[P_LSYNC])        /* LFO 2 FREE + SYNC: in time with the bars from PLAY */
            t->lfo_ph = (uint32_t)t->p[P_LPHASE] << 25;
    }
    song.tick = 0;
    beat_pos = 0; beat_n = 0;                      /* the ARP LED's beat from the top too */
    clk_pos = CLK_START;                           /* the metronome's first beat with step 0 */
    song.playing = 1;
    slicer_start();                                /* slicer.c: its step 0 with the sequencer's */
    perf_start();                                  /* perform.c: its 1/16 grid too */
}

static void seq_release(track_t *t)
{
    uint32_t i;
    for (i = 0; i < t->seq_n; i++)
        trk_note_off(t, t->seq_notes[i]);
    t->seq_n = 0;
    t->seq_hold = 0;
    t->slide_glide = 0;                             /* live MONO / LEG keys must not glide after it */
}

static void seq_stop(void)
{
    uint32_t i;
    song.playing = 0;
    cin_left = 0;                                  /* (a count-in: cancelled, nothing recorded) */
    cin_flush = 0;
    memset(cin_n, 0, sizeof cin_n);
    if (song.g[G_CLOCK])                           /* external clock: the arp stops with the transport, */
        for (i = 0; i < NPART; i++)                /* its sounding note too (HOLD keeps the latched chord) */
            if (trk[i].arp_note)
                arp_off(&trk[i]);
    for (i = 0; i < NTRK; i++) {
        seq_release(&trk[i]);
        trk[i].rh_n = 0;                           /* a recorded note held over the stop: as far as it got */
        trk[i].rat_left = 0;
        trk[i].nd_late = trk[i].nd_early = ND_NONE;   /* (a nudged step not played yet: not after the stop) */
        trk[i].nd_fired = 0;
    }
    chain_stop();
    motion_end();
}

/* play one step: TIE extends, REST releases, NOTE (re)triggers its notes, then its lane hits (their GM
 * notes, DRUM_LANE_NOTE; an accented hit at 127); a SLIDE on the previous step makes this one legato with a
 * glide (acid style). skip: bit k = note k, bit 8 + l = the hit of lane l already sounds from live
 * recording (not triggered, not released here); SEQ_REP: a RATCH repeat (seq_ratchet).
 * RATCH x2..x4 (core.h step_ratchet): a NOTE step plays all of it again in equal parts of its swung length, each
 * part retriggered with GATE of the part; the chance is rolled once for the whole step (a failed roll: no part),
 * a slide into it glides into the first part only, and it never slides or ties out (its last part ends at its
 * gate). x1, what every older pattern holds, is the step exactly as before. */
#define SEQ_REP (1u << 16)
#define SEQ_ROLLED (1u << 17)   /* seq_tick rolled the chance already (before the step's parameter locks): it passed */
#define SEQ_MISS (1u << 18)     /* .. it failed: nothing plays */
static __attribute__((noinline)) void seq_step(track_t *t, const step_t *s, uint32_t idx, uint32_t period, uint32_t skip)
{
    uint32_t i, j, gate = period * (uint32_t)t->p[P_SGATE] / 128u;
    uint32_t vel = (s->flags & SF_ACCENT) ? 127u : (s->vel ? s->vel : 96u);
    uint32_t slide_in = t->seq_hold && t->seq_n && !(skip & SEQ_REP);
    uint32_t len = t->p[P_SLEN] ? (uint32_t)t->p[P_SLEN] : 1u;
    uint32_t next_tie = seq_steps(t)[(idx + 1u) % len].time == ST_TIE;
    uint32_t hits = step_ratchet(s);
    uint8_t nn[4 + NLANE], vv[4 + NLANE];           /* the notes it plays: its notes, then its hits */
    uint32_t m = 0, sk = 0;
    /* QNT SEQ: the step's notes (not the lane hits) snap to the scale as they play; never on a drum kit, the slices
     * or another engine that maps the keys itself. seq_notes keeps the notes that sound: their note-offs match */
    const engine_t *e = ENGINES[eng_idx(t->eng_req)];
    uint32_t qseq = t->p[P_QUANT] == QN_SEQ && !(e->keys && e->keys(t, 0) >= 0);
    if (!(skip & SEQ_REP)) {
        t->rat_left = 0;
        if ((skip & SEQ_MISS) ||
            (!(skip & SEQ_ROLLED) && step_chance(s) < 100u && rng() % 100u >= step_chance(s))) {
            seq_release(t);
            return;
        }
    }
    if (s->time == ST_TIE) {
        if (t->seq_n) {
            t->seq_off = gate + period / 2u;
            t->seq_hold = (s->flags & SF_SLIDE) != 0 || next_tie;   /* chains hold at any GATE / swing */
        }
        return;
    }
    if (s->time == ST_REST || (!s->n && !s->hit)) {
        seq_release(t);
        return;
    }
    if (hits > 1u) {                                /* RATCH: the gate of a part, no slide or tie out */
        gate = step_samples(t, period, idx) / hits * (uint32_t)t->p[P_SGATE] / 128u;
        if (!(skip & SEQ_REP))
            t->rat_left = (uint8_t)(hits - 1u);
    }
    for (i = 0; i < s->n && i < 4u; i++) {
        uint32_t x = s->note[i];
        if (qseq) {                                 /* QNT SEQ: onto the scale now; two notes snapping */
            x = (uint32_t)clamp(scale_snap(t, (int32_t)x), 0, 127);   /* together play once */
            for (j = 0; j < m && nn[j] != x; j++)
                ;
            if (j < m)
                continue;
        }
        nn[m] = (uint8_t)x;
        vv[m] = (uint8_t)vel;
        sk |= ((skip >> i) & 1u) << m;
        m++;
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
    t->seq_hold = hits == 1u && ((s->flags & SF_SLIDE) != 0 || next_tie);   /* next step a TIE: keep the notes to it */
}

/* MENU > CONTROL > STEP PREVIEW (1.2, Discussion #169): the UI asks for a step of a track to sound once (aud_req: step
 * << 8 | track + 1; ui_input.c cursor_move); with the transport stopped it plays here as the sequencer would play it,
 * through the track's sound: its notes or hits, accent, QNT SEQ (not its chance, ratchet repeats or locks), for a 1/16
 * at the track's GATE (seq_tick lets it go, stopped too). A SLIDE or TIE after it does not hold it. Nothing is recorded:
 * the step is only read */
static volatile uint16_t aud_req;
static __attribute__((noinline)) void seq_audition(void)
{
    uint32_t r = aud_req, i = (r & 0xFFu) - 1u, idx = r >> 8;
    track_t *t;
    uint16_t at;
    aud_req = 0;
    if (song.playing || cin_left || i >= NTRK || idx >= NSTEP)
        return;
    t = &trk[i];
    at = t->seq_idx;
    t->seq_idx = (uint16_t)idx;
    seq_step(t, &t->step[idx], idx, div_samples(2), SEQ_ROLLED);
    t->seq_idx = at;
    t->seq_hold = 0;
    t->rat_left = 0;
}

/* RATCH: the next part of the playing step once the sequencer is that far into it (seq_step played the first).
 * The step is read again: an edit to fewer parts (or another source in a song) ends the repeats */
static __attribute__((noinline)) void seq_ratchet(track_t *t, uint32_t period)
{
    const step_t *s = &seq_steps(t)[t->seq_idx];
    uint32_t hits = step_ratchet(s);
    if (t->rat_left >= hits) {
        t->rat_left = 0;
        return;
    }
    if (t->seq_pos < (hits - t->rat_left) * (step_samples(t, period, t->seq_idx) / hits))
        return;
    t->rat_left--;
    seq_step(t, s, t->seq_idx, period, SEQ_REP);
}

/* the nudge of step s in 1/NUDGE_DIV of the step (NUDGE, core.h) when it plays off its start: the track's QUANTIZE OFF,
 * a NOTE step that plays something, x1 (a ratcheted step's parts divide the step: on its start); else 0 */
static int32_t seq_nudge(const track_t *t, const step_t *s)
{
    if (t->p[P_SQNT] || s->time != ST_NOTE || (!s->n && !s->hit) || step_ratchet(s) != 1u)
        return 0;
    return step_nudge(s);
}

/* the automation a track plays: its slot's while a song plays (song_chain.c lanes; 0: a silent lane, none) */
static const motion_store_t *chain_motion(const track_t *t)
{
    uint32_t s = chain.lane[trk_index(t)];
    return !chain.running ? &motion : s < 4u ? &chain.source[s].motion : 0;
}

/* step idx plays: its chance (the one roll, as seq_step made it: a step that does not play applies no lock), the
 * automation and the locks before the notes (so a note-on reads them: eng_drum's KIT, ..), the notes live recording
 * already sounds left out (rskip), then its notes. On its start, or (NUDGE) at its start + its nudge */
static __attribute__((noinline)) void seq_play(track_t *t, uint32_t idx, uint32_t period)
{
    const step_t *s = &seq_steps(t)[idx];
    const motion_store_t *m = chain_motion(t);
    uint32_t skip = SEQ_ROLLED, i, k;
    if (step_chance(s) < 100u && rng() % 100u >= step_chance(s))
        skip = SEQ_MISS;
    if (m)
        motion_step(t, idx, m, skip == SEQ_ROLLED);
    if (t->rskip_n && t->rskip_idx == idx) {
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
    seq_step(t, s, idx, period, skip);
}

/* NUDGE (1.2, #162; QUANTIZE OFF): a step plays at its start + nudge x DIV / 16. When a step starts its own late play
 * (nd_late) and the next step's early one (nd_early: that much before the end of this one) are latched; an early
 * play marks the step (nd_fired), whose start then plays nothing. A late play falls before the next start (at most
 * 7/16 of the step, the shortest swung step is 0.6 of it); an early one never before the late one of the step before.
 * Not across the end of a song row's pattern (the next row's pattern is not there yet): such a step plays on its
 * start. QUANTIZE ON (the default), or every nudge 0 (every older pattern): each step plays on its start, as before.
 * seq_nd_due: before step seq_idx's end is checked, the latched plays due by now (seq_pos): this step's late one, then
 * the next step's early one (not inlined: off the audio ISR's loop, and only called with one latched) */
static __attribute__((noinline)) void seq_nd_due(track_t *t, uint32_t period, uint32_t len, uint32_t cur_len, uint32_t n)
{
    if (t->nd_late != ND_NONE && (t->seq_pos >= t->nd_late - 1u || t->seq_pos >= cur_len)) {
        t->nd_late = ND_NONE;                       /* (also when a shorter DIV / LEN left it past the end) */
        seq_play(t, t->seq_idx, period);
    }
    if (t->nd_early != ND_NONE && t->seq_pos >= t->nd_early - 1u && t->seq_pos != 0x7FFFFFFFu + n) {
        uint32_t nx = (t->seq_idx + 1u) % len;
        t->nd_early = ND_NONE;
        t->nd_fired = (uint8_t)(nx + 1u);
        seq_play(t, nx, period);
    }
}

/* NUDGE: step seq_idx has started: it plays now (or played early: nd_fired), or its late play is latched; and the
 * next step's early play (not inlined, as seq_nd_due) */
static __attribute__((noinline)) void seq_nd_start(track_t *t, uint32_t period, uint32_t len)
{
    uint32_t nx = (t->seq_idx + 1u) % len;
    int32_t d;
    t->nd_early = ND_NONE;
    if (t->nd_fired == t->seq_idx + 1u)
        ;                                           /* (it played early) */
    else if ((d = seq_nudge(t, &seq_steps(t)[t->seq_idx])) > 0)
        t->nd_late = (uint32_t)d * period / NUDGE_DIV + 1u;
    else
        seq_play(t, t->seq_idx, period);
    t->nd_fired = 0;
    if ((!chain.running || nx) && (d = seq_nudge(t, &seq_steps(t)[nx])) < 0) {
        uint32_t at = step_samples(t, period, t->seq_idx), back = (uint32_t)-d * period / NUDGE_DIV;
        at = (at > back ? at - back : 0u) + 1u;
        t->nd_early = at < t->nd_late ? t->nd_late : at;   /* (never before this step's own late play) */
    }
}

static void seq_tick(track_t *t, uint32_t n)
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
    period = div_samples((uint32_t)t->p[P_SDIV]);
    len = (uint32_t)t->p[P_SLEN];
    len = len ? len : 1u;
    t->seq_pos += n;
    for (;;) {
        uint32_t cur_len = step_samples(t, period, t->seq_idx);
        if (t->nd_late | t->nd_early)
            seq_nd_due(t, period, len, cur_len, n);
        if (t->seq_pos < cur_len && t->seq_pos != 0x7FFFFFFFu + n)
            break;
        t->seq_pos = t->seq_pos >= 0x7FFFFFFFu ? (chain.running ? chain.carry : 0u) : t->seq_pos - cur_len;
        t->seq_idx = (uint16_t)((t->seq_idx + 1u) % len);
        rec_hold(t, t->seq_idx, len);
        if (t->p[P_SQNT] && !t->nd_fired)
            seq_play(t, t->seq_idx, period);        /* QUANTIZE ON: every step on its start, as before 1.2 */
        else
            seq_nd_start(t, period, len);
    }
    if (t->rat_left)
        seq_ratchet(t, period);
}

/* MIDI in: the track a channel plays (0..15): G_ROUTE CH1-4 (0) channels 1..4 their parts (5..16 never get
 * here: midi_event drops them), SEL (1) every channel the selected track; CH5-8 .. CH13-16 (2..4) as CH1-4 on
 * their four channels (midi_base) */
static uint8_t midi_route;                 /* the G_ROUTE events_block last saw (a change to a block: midi_route_block) */
static int midi_in_block(uint32_t ch)      /* ch is one of ROUT's block (not with SEL) */
{
    int32_t b = midi_base();
    return b >= 0 && ch - (uint32_t)b < NPART;
}
static track_t *midi_track(uint32_t ch) { return midi_in_block(ch) ? &trk[ch - (uint32_t)midi_base()] : TSEL; }

#include "midi_control.c"
#include "midi_clock.c"

/* the count-in: its first beat now (accented) */
static __attribute__((noinline)) void cin_begin(void)
{
    cin_total = cin_left = (uint8_t)(4u * cin_bars);
    cin_pos = 0;
    memset(cin_n, 0, sizeof cin_n);
    click_req = 2;
}
/* n samples on: a beat ends; the last one starts the sequencer (in this block, before seq_tick: step 0 with it) */
static __attribute__((noinline)) void cin_block(uint32_t n)
{
    uint32_t b = click_beat_len();
    cin_pos += n;
    if (cin_pos < b)
        return;
    cin_pos -= b;
    cin_pos = cin_pos < b ? cin_pos : 0u;
    if (--cin_left) {
        click_req = (uint8_t)((cin_total - cin_left) % 4u ? 1u : 2u);
        return;
    }
    seq_start();
    cin_flush = 1;
}
/* the count-in's kept notes onto step 1, which has just started (events_block, after seq_tick): as if played now */
static __attribute__((noinline)) void cin_record(void)
{
    uint32_t i, k;
    cin_flush = 0;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        for (k = 0; k < cin_n[i]; k++)
            if (rec_on(t)) {
                rec_note(t, cin_note[i][k], cin_vel[i][k]);
                if ((cin_up[i] >> k) & 1u)
                    rec_release(t, cin_note[i][k]);
            }
        cin_n[i] = 0;
    }
}
/* the metronome's beat: adv samples on while the transport runs (click.c sounds it in this block) */
static void click_tick(uint32_t adv)
{
    uint32_t p = div_samples(2), beat = 0;
    if (clk_pos == CLK_START) {
        clk_pos = 0;
        clk_step = 0;
        beat = 2;
    } else {
        clk_pos += adv;
        while (clk_pos >= p) {                      /* (as seq_tick: the remainder kept) */
            clk_pos -= p;
            clk_step = (clk_step + 1u) & 15u;
            if (!(clk_step & 3u))
                beat = clk_step ? 1u : 2u;
        }
    }
    if (beat && click_wanted())
        click_req = (uint8_t)beat;
}
/* the beat of the bar the click is on (0..3: 0 the accented first), -1 stopped: the count-in's, else the playing
 * transport's (clk_step, internal or external clock). It moves in the block a beat clicks in (the header's metronome,
 * ui_draw.c, swings on it; CLICK OFF it moves all the same) */
static int32_t click_beat_now(void)
{
    if (cin_left)
        return (int32_t)((uint32_t)(cin_total - cin_left) % 4u);
    if (!song.playing)
        return -1;
    return clk_pos == CLK_START ? 0 : (int32_t)(clk_step >> 2);
}

/* everything that happens between two rendered blocks */
static void events_block(uint32_t n)
{
    uint32_t i, pr, seq_n = n;
    uint32_t clock_mode = (uint32_t)song.g[G_CLOCK];
    if (midi_clock.mode != clock_mode) {
        seq_stop();
        memset(&midi_clock, 0, sizeof midi_clock);
        midi_clock.mode = (uint8_t)clock_mode;
        midi_beat_samples = 0;
    }
    if (cin_left && !clock_mode)
        cin_block(n);
    if (transport_req == 1u) {
        if (clock_mode)
            midi_clock_transport(0xFAu, fm1_ms);
        else if (cin_left)
            ;                                       /* counting in already (the UI's PLAY stops it: seq_counting) */
        else if (cin_bars && song.rec && !song.playing && !chain.armed)
            cin_begin();                            /* recording from stop: the count-in first */
        else
            seq_start();
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
    if (midi_route != (uint8_t)song.g[G_ROUTE]) {     /* ROUT changed: a block lets go of the other channels */
        midi_route = (uint8_t)song.g[G_ROUTE];
        if (midi_route != 1u)
            midi_route_block();
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
            trk[i].seq_n = trk[i].seq_hold = trk[i].slide_glide = trk[i].rat_left = 0;
        }
        pr |= (1u << NTRK) - 1u;
        RING_PUBLISH();
        midi_in_overflow = 0;
    }
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        if ((pr >> i) & 1u) {
            midi_forget_track(i);
            trk_all_off(t);
            t->nheld = 0;
            t->arp_phys = 0;
            arp_forget(t);
        }
        engine_block(t);                              /* engine switch: fade, then switch (voice.c) */
        /* ARP turned off, or HOLD released with no key down: drop the latched chord */
        if ((t->armp && !t->p[P_AMODE]) || (t->aholdp && !t->p[P_AHOLD] && !t->arp_phys)) {
            t->nheld = 0;
            if (!t->p[P_AMODE])
                t->arp_phys = 0;
            if (t->arp_note)
                arp_silence(t);
        }
        t->armp = t->p[P_AMODE];
        t->aholdp = t->p[P_AHOLD];
    }
    keyboard_block();
    while (mi_r != mi_w) {                            /* USB-MIDI (and TRS) in */
        uint32_t at = mi_r % MQ, pkt = midi_in_q[at], status = (pkt >> 8) & 0xFFu;
        uint32_t st = status & 0xF0u, ch = status & 0x0Fu;
        uint32_t d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        uint32_t source = midi_in_source[at] ? midi_in_source[at] : 1u, ms = midi_in_ms[at];
        mi_r++;
        if (status >= 0xF8u) {
            if (clock_mode == source) {
                if (status == 0xF8u)
                    midi_clock_pulse(ms);
                else
                    midi_clock_transport(status, ms);
            }
        } else
            midi_event(st, ch, d1, d2);
    }
    if (clock_mode) {
        seq_n = 0;
        if (song.playing) {
            uint32_t last = midi_clock.have_pulse ? midi_clock.last_ms : midi_clock.start_ms;
            if (fm1_ms - last > 500u) {
                seq_stop();                         /* cable loss must not leave a running held note */
                midi_clock.tempo_valid = 0;
            } else
                seq_n = midi_clock_advance(fm1_ms);
        }
    }
    if (aud_req)
        seq_audition();                               /* (STEP PREVIEW: a step sounds, stopped) */
    chain_tick(seq_n);
    for (i = 0; i < NTRK; i++)
        seq_tick(&trk[i], seq_n);
    if (cin_flush)
        cin_record();
    if (song.playing)
        click_tick(seq_n);
    for (i = 0; i < NPART; i++)
        arp_step(&trk[i], clock_mode ? seq_n : n, clock_mode && song.playing ? seq_n : n);
    beat_pos += clock_mode ? seq_n : n;               /* the beat the ARP LED flashes on (ui_leds) */
    if (beat_pos >= beat_samples()) {
        beat_pos -= beat_samples();
        beat_pos = beat_pos < beat_samples() ? beat_pos : 0u;
        beat_n = (beat_n + 1u) & 3u;
    }
    if (song.playing)
        song.tick++;
}
