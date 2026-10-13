/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Melodee UI input: LEDs, knobs and buttons, SEQ step entry, panel setup. */
#include "ui_name.c"                                    /* NAME: naming user presets and projects */
#include "ui_new.c"                                     /* NEW SONG: key, tempo, roles */
/* ----------------------------------------------------------- LEDs --- */
/* The LED picture is built off-line and copied one byte per column: clearing
 * and relighting would let the 10 kHz scan catch the dark gap and flicker. */
static uint8_t led_pos[41];                        /* (col << 3) | row bit, 0xFF = none */

static void led_pos_init(void)
{
    uint32_t id, p, r;
    for (id = 0; id < 41u; id++) {
        led_pos[id] = 0xFF;
        for (p = 0; p < FM1_NCOL; p++)
            for (r = 1; r < 5u; r++)
                if (FM1_KEYMAP[r][p] == (int8_t)id)
                    led_pos[id] = (uint8_t)((p << 3) | r);
    }
}

static void led_put(uint8_t *nl, uint32_t id, int on)
{
    uint8_t q = led_pos[id];
    if (q != 0xFF && on)
        nl[q >> 3] |= (uint8_t)(1u << (q & 7u));
}
static int led_get(const uint8_t *nl, uint32_t id)
{
    uint8_t q = led_pos[id];
    return q != 0xFF && ((nl[q >> 3] >> (q & 7u)) & 1u);
}

static const uint8_t FAM_BTN[FAM_COUNT] = {B_HOME, B_ENV, B_LFO, B_SCL, B_EDIT, B_GLO, B_SAVE,
                                           B_ARP, B_SEQ, B_GLO, B_FX};   /* GLO: mixer + global settings; REC is transport */

static uint32_t cur_fam(void)                       /* (an engine's own envelope / LFO page: ENV / LFO, ui.c native_page) */
{
    return ui.home ? FAM_HOME : native_page(ui.page, FAM_ENV) ? FAM_ENV : native_page(ui.page, FAM_LFO) ? FAM_LFO :
           cur_page()->fam;
}

static int layer_set_open(void);                       /* (ui_layer.c) */
/* OCT- / OCT+ shift the octave on Stage (HOME) only; everywhere else, and over Stage's menu, dialogs and popups,
 * they are Esc / Enter (docs/design/README.md, Navigation) */
static int oct_nav(void)
{
    return designer_on() || !ui.home || ui.menu || ui.confirm || name_on() || new_on() || layer_set_open() || pop.on || smap.on;
}
static int oct_enter_ok(void)                           /* OCT+ would do something here */
{
    return list_on() || slot_kind() || motion_page() || (!ui.home && cur_page()->graph == GR_SCALE_PICKER) || page_sheet() || pop.on || smap.on || (!ui.home && cur_page()->graph == GR_BROWSE) || ptc_on();
}

/* the OCT LEDs, bit 0 OCT-, bit 1 OCT+. In the dialogs, the menu and on action pages OCT- (back) is
 * lit and OCT+ blinks while it would do something; off Stage OCT- lit, OCT+ lit where it enters; on Stage
 * they show the octave shift */
static uint32_t oct_leds(void)
{
    if (designer_on() && !name_on() && !ui.confirm) return 3u;
    uint32_t blink = ((fm1_ms / 250u) & 1u) == 0u;
    if ((name_on() || new_on()) && !ui.confirm && !ui.menu)   /* NAME, NEW SONG: OCT- cancels, OCT+ (blinking) goes on */
        return 1u | (blink ? 2u : 0u);
    if (layer_set_open())                               /* a SET layer: OCT- puts back (UNDO), OCT+ nothing */
        return 1u;
    if (ui.confirm || ui.menu || act_cols())
        return 1u | (blink && (ui.confirm || (ui.menu ? ui.menu == 1u : act_ready())) ? 2u : 0u);
    if (oct_nav())
        return 1u | (oct_enter_ok() ? 2u : 0u);
    return (song.octave < 0 ? 1u : 0u) | (song.octave > 0 ? 2u : 0u);
}

/* the key LEDs on the grid, bit k = key k: the white keys show where the selected lane hits on the page
 * shown (its accents while ACC is held), the step playing inverted (a light walks over them); the black keys
 * the lane selected, ACC while held, the page keys while there is more than one page */
static uint32_t grid_leds(void)
{
    const track_t *t = TSEL;
    uint32_t k, m = 0, len = (uint32_t)t->p[P_SLEN], b = 1u << ui.lane, acc = (uint32_t)black_held(GK_ACC);
    uint32_t ph = song.playing && t->seq_idx < len && t->seq_idx / 16u == ui.bank ? t->seq_idx % 16u : 0xFFu;
    for (k = 0; k < 27u; k++) {
        uint32_t p = key_place(k), on;
        if (!key_black(k)) {
            uint32_t i = ui.bank * 16u + p;
            on = i < len && ((acc ? step_accents(&seq_steps(t)[i]) : step_lanes(&seq_steps(t)[i])) & b) != 0u;
            on ^= (uint32_t)(p == ph);
        } else {
            on = p < NLANE ? p == ui.lane : p == GK_ACC ? acc : len > 16u;
        }
        m |= on << k;
    }
    return m;
}

/* an ARP playing on any part flashes the ARP button on the beat, the bar's first beat longer: 1 lit, 0 dark,
 * 2 no ARP playing */
static uint32_t arp_led(void)
{
    uint32_t k, on = 0, b = seq_beat_samples();
    for (k = 0; k < NPART; k++)
        on |= trk[k].p[P_AMODE] && trk[k].nheld;
    return !on ? 2u : beat_pos < (beat_n ? b / 6u : b / 2u);
}

static const uint8_t PAT_KEY[NPAT] = {0, 2, 4, 6, 7, 9, 11, 12};
static int pattern_keys_on(void)
{
    return !ui.menu && !ui.confirm && !name_on() && !ui.ly && !ui.uboot &&
        (fm1_in.buttons & (1u << panel.btn[B_SEQ]));
}
/* the pattern keys (SEQ held), as the keys show things everywhere: bright the pattern playing (and the one held to copy),
 * dim the others that hold something, breathing the one waiting for the bar; the empty ones dark */
static uint32_t pattern_leds(uint32_t *dim, uint32_t *breath)
{
    uint32_t m = 1u << PAT_KEY[TSEL->pattern], b;
    *dim = *breath = 0;
    for (b = 0; b < NPAT; b++)
        if (b != TSEL->pattern && pattern_used(song.sel, b))
            *dim |= 1u << PAT_KEY[b];
    if (TSEL->pattern_next < NPAT) *breath = 1u << PAT_KEY[TSEL->pattern_next];
    if (ui.pat_key) m |= 1u << PAT_KEY[ui.pat_key - 1u];
    *dim &= ~(m | *breath);
    *breath &= ~m;
    return m;
}
static void pattern_keys(uint32_t notes)
{
    if (ui.pat_key && ui.pat_track != song.sel) ui.pat_key = ui.pat_copy = 0;
    if (pattern_keys_on()) {
        for (uint32_t b = 0; b < NPAT; b++) if (notes & (1u << PAT_KEY[b])) {
            ui.seq_t0 |= 2u;
            if (!ui.pat_key) { ui.pat_key = (uint8_t)(b + 1u); ui.pat_track = song.sel; ui.pat_copy = 0; }
            else if (ui.pat_key != b + 1u) {
                int rc = pattern_copy(TSEL, ui.pat_key - 1u, b);
                ui.pat_copy = 1;
                ui_message(rc == 2 ? "PATTERN DATA FULL" : rc ? "STOP TO COPY" : "PATTERN COPIED");
                ui.force = 1;
            }
        }
    }
    if (ui.pat_key && (!pattern_keys_on() || !(fm1_in.notes & (1u << PAT_KEY[ui.pat_key - 1u])))) {
        if (!ui.pat_copy) {
            uint32_t bank = ui.pat_key - 1u;
            if (pattern_request(TSEL, bank)) ui_message("STOP SONG TO SWITCH");
            else { char b[12] = "PATTERN "; b[8] = (char)('1' + bank); b[9] = 0; ui_message(b); }
            ui.entry_open = 0; cursor_set(0); ui.force = 1;
        }
        ui.pat_key = ui.pat_copy = 0;
    }
}
/* key k of track t as the keys play now (LIGHTS): bright while it sounds (pressed, or its note held on the track by
 * a key or MIDI), dim when it plays something (a kit's or the slices' own map: every key; QNT OFF: the notes in the
 * scale; the layouts: the keys not silent), else dark */
enum { KL_OFF, KL_DIM, KL_ON };
static uint32_t play_key_led(const track_t *t, uint32_t k)
{
    const engine_t *e = ENGINES[eng_idx(t->eng_req)];
    uint32_t n = kb_map(t, k), ti = trk_index(t);
    if (((fm1_in.notes >> k) & 1u) || (n < 128u && ((live_held[ti][n >> 5] >> (n & 31u)) & 1u)))
        return KL_ON;
    if (n >= 128u)
        return KL_OFF;
    if (e->keys && e->keys(t, k) >= 0)
        return KL_DIM;
    if (t->p[P_QUANT] == Q_OFF)
        return (scale_mask(t) >> ((n + 120u - (uint32_t)t->p[P_ROOT]) % 12u)) & 1u ? KL_DIM : KL_OFF;
    return KL_DIM;
}

static const uint8_t LIGHTS_MASK[LIGHTS_N] = {0, 7, 3, 1, 0};   /* the dim keys' duty: -, 1/8, 1/4, 1/2, all */
#define BTN_DIM_MASK 3u                                         /* idle buttons: 1/4 duty each scan */
/* The lights' grammar, keys and buttons alike: dark nothing there, dim something there, bright happening now, breathing
 * waiting (a pattern for the bar, REC armed before the transport runs, PLAY counting in). Breathing: the third dim plane,
 * its level swept full .. 1/8 .. dark .. 1/8 .. full over a second (BREATH, 125 ms a step; 0xFF: dark) */
static const uint8_t BREATH[8] = {0, 1, 3, 7, 0xFF, 7, 3, 1};
static uint32_t breath_on(void) { return BREATH[(fm1_ms / 125u) & 7u] != 0xFFu; }

static void ui_leds(void)
{
    uint8_t nl[FM1_NCOL] = {0}, nd[FM1_NCOL] = {0}, nb[FM1_NCOL] = {0}, nw[FM1_NCOL] = {0};
    uint32_t k, c, play, lights = settings_lights % LIGHTS_N, pdim = 0, pbr = 0;
    uint32_t fam = cur_fam();
    uint32_t armed = song.rec != 0u, rolling = song.playing != 0u, counting = (uint32_t)seq_counting();
    uint32_t recording_now = (song.rec & ~song.rec_wait) && rolling && !counting && !chain.running;
    static uint8_t ready;
    if (!ready) {
        led_pos_init();
        ready = 1;
    }
    if (!ui.layer || FAM_BTN[fam] != layer_btn())
        led_put(nl, panel.btn[FAM_BTN[fam]], 1);
    if ((k = arp_led()) != 2u && (!ui.layer || layer_btn() != B_ARP))
        led_put(nl, panel.btn[B_ARP], FAM_BTN[fam] == B_ARP ? !k : (int)k);   /* (on ARP's page: dark flashes) */
    if (ui.layer)                                       /* the layer's button blinks while its map is up */
        led_put(nl, panel.btn[layer_btn()], ((fm1_ms / 250u) & 1u) == 0u);
    led_put(nl, panel.btn[B_PLAY], rolling && !counting); /* steady transport state, independent of audio block rate; */
    led_put(nw, panel.btn[B_PLAY], counting);           /* counting in: waiting */
    led_put(nl, panel.btn[B_REC], recording_now);
    led_put(nw, panel.btn[B_REC], armed && !recording_now);
    k = oct_leds();
    led_put(nl, panel.btn[B_OCTDN], (int)(k & 1u));
    led_put(nl, panel.btn[B_OCTUP], (int)(k >> 1));
    play = !pattern_keys_on() && !(name_on() && !ui.menu) && !ui.layer && !grid_on();
    c = pattern_keys_on() ? pattern_leds(&pdim, &pbr) : name_on() && !ui.menu ? name_leds() : ui.layer ? layer_leds() :
        grid_on() ? grid_leds() : fm1_in.notes & ~kb_layer;   /* the patterns, NAME's keys, the map, the grid, the keys held */
#if MELODEE_SLICE
    if (!ui.layer && !ui.menu && !name_on() && slice_page_on())
        c |= slice_leds();                              /* SLICES: and the keys of the selected slice */
#endif
    for (k = 0; k < 27u; k++) {
        uint32_t lv = play ? play_key_led(TSEL, k) : KL_OFF;   /* playing: the layout, MIDI's notes too (LIGHTS) */
        led_put(nl, 14u + k, (int)(((c >> k) & 1u) || lv == KL_ON));
        led_put(nd, 14u + k, lv == KL_DIM && lights != LIGHTS_OFF);
        led_put(nb, 14u + k, (int)((pdim >> k) & 1u));  /* the patterns holding something: selectable, so lit with
                                                         * LIGHTS OFF too (the buttons' plane) */
        led_put(nw, 14u + k, (int)((pbr >> k) & 1u));
    }
    for (k = 0; lights != LIGHTS_OFF && k < NB; k++)   /* the buttons glow when idle (one breathing: not under it) */
        led_put(nb, panel.btn[k], !led_get(nw, panel.btn[k]));
    fm1_led_dim_mask[0] = LIGHTS_MASK[lights];
    fm1_led_dim_mask[1] = BTN_DIM_MASK;
    fm1_led_dim_mask[2] = BREATH[(fm1_ms / 125u) & 7u];
    for (c = 0; c < FM1_NCOL; c++) {
        fm1_led_dim[0][c] = (uint8_t)(nd[c] | nl[c]);   /* first: a key going dim <-> bright never goes dark */
        fm1_led_dim[1][c] = nb[c];
        fm1_led_dim[2][c] = breath_on() ? nw[c] : 0u;
        fm1_led[c] = nl[c];
    }
}

/* ---------------------------------------------------------- input --- */
/* Knob acceleration, after Felucca 1.4 (#52), with fm1-x0x's curve (tuned on the hardware): a slow turn is one step a
 * detent, and a few clicks are one step each however quick. Only a spin speeds up: from the fourth read of a turn,
 * each under ACC_GAP ms a detent after the one before in the same direction, 3 steps a detent under 45 ms, 5 under
 * 25 ms and 8 under 12 ms, so a quick half turn sweeps 0..127. cap: the most for the value (accel, list_accel); a cap
 * over 8 (the tempo, the long lists) doubles them. MENU > KNOB ACCEL OFF (PREF_ACCEL_OFF) keeps every detent one step.
 * The main loop reads the knobs many times a frame (main.c), so a read holds one detent as a rule; a read of several is
 * timed per detent and counts once (a few detents read at once are not a spin). A pause or a reversal starts a new
 * turn, and the sign is always the detents'. *fast (if asked): this detent followed the previous one within ACC_GAP ms,
 * in the same direction (the browser waits for such a turn to rest).
 * ui.enc_t[role]: bits 0..23 the ms of its last read, bit 24 its direction (+1), 25..31 the reads of its turn */
#define ACC_GAP 60u
#define ACC_SPIN 4u
static int32_t accel_by(uint32_t role, int32_t s, uint32_t cap, uint32_t *fast)
{
    uint32_t now = fm1_ms & 0xFFFFFFu, st = ui.enc_t[role % NE], up = s > 0, run = st >> 25, a, dt, m = 1;
    if (fast)
        *fast = 0;
    if ((PREF_BITS & PREF_ACCEL_OFF) || !s)
        return s;
    a = (uint32_t)(s < 0 ? -s : s);
    dt = ((now - st) & 0xFFFFFFu) / a;                  /* ms per detent of this read */
    if (!st || ((st >> 24) & 1u) != up || dt >= ACC_GAP) {
        run = 1;                                        /* a new turn, or reversed */
    } else {
        if (fast)
            *fast = 1;
        run++;
        if (run >= ACC_SPIN)
            m = dt < 12u ? 8u : dt < 25u ? 5u : dt < 45u ? 3u : 1u;
        if (cap > 8u && m > 1u)
            m *= 2u;
        m = m > cap ? cap : m;
    }
    ui.enc_t[role % NE] = now | up << 24 | (run > 127u ? 127u : run) << 25;
    return s * (int32_t)m;
}
/* a value of range steps: none up to 24 (and a list of names passes 0), x3 at most under 100, x8, x16 over 150 */
static int32_t accel(uint32_t role, int32_t s, int32_t range)
{
    return range <= 24 ? s : accel_by(role, s, range < 100 ? 3u : range > 150 ? 16u : 8u, 0);
}
static int32_t desc_range(const param_desc_t *d)     /* (a list of names: no acceleration) */
{
    return d->fmt == F_ENUM ? 0 : d->max - d->min;
}
/* a list of total entries (the sounds, the scales, the user slots): up to x16 over 256 entries */
static int32_t list_accel(uint32_t role, int32_t s, uint32_t total, uint32_t *fast)
{
    return accel_by(role, s, total > 256u ? 16u : total > 64u ? 8u : total > 16u ? 4u : 1u, fast);
}

/* MIXER page: KNOB 1 LEVEL, 2 PAN, 3 unused, 4 MUTE of the selected track (right = ON, left = OFF: the
 * track itself is ALGORITHM's, on every page). Pattern length stays on SEQ. */
static void tracks_edit(uint32_t slot, int32_t steps)
{
    track_t *t = TSEL;
    int16_t *vp;
    const param_desc_t *d;
    switch (slot) {
    case 3:
        t->p[P_MUTE] = (int16_t)(steps > 0);
        return;
    case 0:
        vp = &t->p[P_LEVEL];
        d = &TP[P_LEVEL];
        break;
    case 2:
        return;
    default:
        vp = &t->p[P_PAN];
        d = &TP[P_PAN];
        break;
    }
    *vp = (int16_t)clamp(*vp + accel(EN_K1 + slot, steps, d->max - d->min), d->min, d->max);
    motion_capture(t, (uint32_t)(vp - t->p), *vp);
}

/* REC arms one pattern pass on the selected track without navigating. During
 * playback it waits for loop zero; another tap cancels or finishes the take.
 * On STEP, armed keys audition/record instead of writing the cursor step. */
static void rec_tap(void)
{
    uint8_t bit = (uint8_t)(1u << song.sel);
    if (!ui.home && cur_page()->graph == GR_SONG && !(song.rec & bit)) {
        ui_message("[SEQ] TO RECORD");
        return;
    }
    fm1_irq_off();
    rec_set(TSEL, !(song.rec & bit));
    fm1_irq_on();
    ui.force = 1;                                     /* also refresh the status on MENU / ABOUT */
    if (song.rec_wait & bit)
        ui_message("WAITING FOR LOOP");
    else if ((song.rec & bit) && grid_on())
        ui_message("LANE KEYS RECORD");               /* (the white keys stay the steps) */
    else if ((song.rec & bit) && !ui.home && cur_page()->graph == GR_ROLL)
        ui_message("KEYS RECORD LIVE");               /* (seq_entry pauses while armed and playing) */
    else if (!(song.rec & bit))
        ui_message("REC OFF");
}

/* REC + PLAY starts an armed track, or queues its next pattern pass if playback
 * already runs. REC's release then does nothing (no tap, no hold). */
static void rec_play(void)
{
    uint8_t bit = (uint8_t)(1u << song.sel);
    ui.rec_t0 |= 2u;
    if (chain_busy()) {
        ui_message("STOP TO RECORD");
        return;
    }
    if (!ui.home && cur_page()->graph == GR_SONG) {
        ui_message("[SEQ] TO RECORD");
        return;
    }
    fm1_irq_off();
    rec_set(TSEL, 1);
    fm1_irq_on();
    if (!song.playing && !seq_counting())
        transport_req = 1;
    ui.force = 1;
    ui_message(song.rec_wait & bit ? "WAITING FOR LOOP" : "RECORDING");
}

/* SAVE + REC (either pressed while the other is held): the project back to its slot (project.c project_quick_save);
 * playing, the transport stops first and the save follows (qsave_poll). Neither button then does its own thing */
static void project_quick_save(void);
static uint8_t qsave_req;
static void qsave_chord(void)
{
    ui.save_t0 |= 2u;
    ui.rec_t0 |= 2u;
    if (transport_busy()) {
        transport_req = 2;
        ui_message("STOPPING TO SAVE");
    }
    qsave_req = 1;
}
static void qsave_poll(void)
{
    if (qsave_req && !transport_busy() && !transport_req) {
        qsave_req = 0;
        project_quick_save();
    }
}

/* REC held: Capture. What the selected track played in its last bars while it was not recording (seq.c cap_ev) into
 * its pattern, as recorded notes (timing, velocity and length as played): an empty pattern takes 1, 2 or 4 bars (16, 32
 * or 64 steps), as many as the notes span, its LEN set to it; a pattern with notes the last LEN steps, over them. The
 * bars end at the bar playing when a note in it was played, else at its start. SAVE held undoes it */
static void capture_take(void)
{
    track_t *t = TSEL;
    uint32_t k = song.sel, n = 0, i, now, first = 0xFFFFu, last = 0, end, w, start, put = 0, full = 0, j = 0, from;
    uint64_t touched = 0;
    int empty;
    cap_ev_t captured[CAP_N];
    uint32_t until;
    if (chain_busy()) { ui_message("STOP SONG TO CAPTURE"); return; }
    if ((song.rec >> k) & 1u) { ui_message("RECORDING"); return; }
    fm1_irq_off();                                      /* the selected track's events of this run: the newest, the oldest */
    now = cap_now(t);
    from = cap_w - cap_from > CAP_N ? cap_w - CAP_N : cap_from;
    until = cap_w;
    memcpy(captured, cap_ev, sizeof captured);
    fm1_irq_on();
    for (i = from; i != until; i++) {
        const cap_ev_t *e = &captured[i % CAP_N];
        uint32_t back = (uint16_t)((now >> 8) - e->step);
        if ((e->tr & 127u) != k || back >= 64u)
            continue;
        n++;
        if (back > last) last = back;
        if (back < first) first = back;
    }
    if (!n) { ui_message("NOTHING TO CAPTURE"); return; }
    {   /* (16-bit step counts: they wrap) */
        uint32_t cur = (now >> 8) & 0xFFFFu, pos = cur & 15u, span;
        end = (uint16_t)(cur - pos + (first <= pos ? 16u : 0u));     /* a bar's start */
        span = (uint16_t)(end - (uint16_t)(cur - last));
        empty = seq_is_empty(t) && !notes_have_recording(t);
        w = empty ? (span <= 16u ? 16u : span <= 32u ? 32u : 64u) : (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP);
        start = (uint16_t)(end - w);
    }
    load_begin(t, UNDO_PAT);
    if (empty)
        t->p[P_SLEN] = (int16_t)w;
    for (i = from; i != until; i++) {
        const cap_ev_t *e = &captured[i % CAP_N];
        uint32_t actual = (uint16_t)(e->step - start), on = (uint32_t)e->on << 8, view, dur, exp;
        recorded_note_t r;
        if ((e->tr & 127u) != k || actual >= w)
            continue;                                   /* (another track's; before the bars, or after them) */
        view = on >= RECORD_UNIT / 2u ? (actual + 1u) % w : actual;
        memset(&r, 0, sizeof r);
        r.on = (uint16_t)on;
        r.note = e->note;
        r.vel = e->vel ? e->vel : 1u;
        dur = cap_len(e, now) << 8; exp = 0;
        while (dur > 65535u && exp < 7u) { dur >>= 1; exp++; }
        r.duration = (uint16_t)dur;
        r.owner = (uint8_t)(recording_owner(t) | (exp << 5));
        r.step = (uint8_t)(actual | (view != actual ? 64u : 0u) | (!view && actual ? 128u : 0u));
        r.length = drum_track(t) ? 1u : 0u;
        fm1_irq_off();
        recording_prune(t);
        /* Capturing an overlapping window again must not stack another voice
         * at an onset already kept in this bank. Distinct pitches and times
         * remain independent events; keep existing edits to the first take. */
        uint32_t existing = recording_head[recording_owner(t)];
        for (; existing < RECORD_MAX; existing = recording_next[existing])
            if (recording_active(t, existing) && recording[existing].note == r.note &&
                (recording[existing].step & 63u) == actual && recording[existing].on == r.on)
                break;
        if (existing < RECORD_MAX) { fm1_irq_on(); continue; }
        for (; j < RECORD_MAX && recording[j].vel && (recording_present(&recording[j]) || recording_run[j].left); j++)
            ;
        if (j == RECORD_MAX) { fm1_irq_on(); full = 1; break; }
        if (recording[j].vel) recording_unlink(j);
        recording_restore_note(t, j, r);
        t->step[view].time = ST_NOTE;
        t->step[view].flags |= SF_RECORDED;
        touched |= 1ull << view;
        put++;
        fm1_irq_on();
    }
    for (i = 0; i < NSTEP; i++)
        if ((touched >> i) & 1u) {
            fm1_irq_off();
            notes_rebuild(t, i);
            fm1_irq_on();
        }
    if (put)
        t->seq_active = 1;
    load_end(t);
    if (!put) { ui_message("NOTHING TO CAPTURE"); return; }
    {
        char b[16];
        fmt_int(b, (int32_t)(w % 16u ? w : w / 16u));
        str_cpy(b + str_len(b), w % 16u ? " STEPS" : w > 16u ? " BARS" : " BAR", 8);
        ui_say(full ? "FULL: " : "CAPTURED ", b);
    }
    ui.force = 1;
}

/* SEQ > SONG TAKE JAM: the patterns played since PLAY (seq.c jam) become the song's rows */
static void jam_take(void)
{
    uint32_t i, k;
    if (chain_busy()) { ui_message("STOP TO EDIT"); return; }
    chain_defaults(&chain_config);
    memset(chain_patterns, 0, sizeof chain_patterns);
    for (i = 0; i < jam.n; i++) {
        for (k = 0; k < NTRK; k++)
            chain_patterns[i][k] = jam.pat[i][k];
        chain_config.row[i].slot = jam.pat[i][0];
        chain_config.row[i].repeat = jam.rep[i];
    }
    chain_config.count = jam.n;
    ui.song_row = 0;
    ui.act = 0;
    ui_message("SONG FROM JAM");
    ui.force = 1;
}

/* Autosave: stopped and untouched for AUTOSAVE_MS (no key, button, knob or MIDI), the project back to its slot when it
 * differs from what is there (project.c project_autosave); once per rest */
#define AUTOSAVE_MS 5000u
static void project_autosave(void);
static void autosave_poll(uint32_t active)
{
    static uint32_t t0, rx;
    static uint8_t done;
#if MELODEE_UART
    uint32_t r = usb.rx_pkts + um.bytes;
#else
    uint32_t r = usb.rx_pkts;
#endif
    if (!settings_autosave || active || r != rx || transport_busy() || ui.menu || ui.confirm || name_on() || new_on() || ui.layer || momentary.active ||
        browse_pending() || qsave_req || designer_on()) {
        t0 = fm1_ms;
        rx = r;
        done = 0;
        return;
    }
    if (!done && fm1_ms - t0 >= AUTOSAVE_MS) {
        done = 1;
        project_autosave();
    }
}

/* live recording into the selected track now: the STEP page's key entry pauses meanwhile */
static int live_rec_sel(void) { return (song.rec >> song.sel) & 1u; }

/* the OCT- / OCT+ dialog (ui_draw.c draws it); trk: the track or the slot it is about */
/* the grid's page down (-1) / up (+1): the cursor to the same place on it (at most the last step) */
static void page_go(int32_t d)
{
    uint32_t len = (uint32_t)TSEL->p[P_SLEN], pages = (len + 15u) / 16u, b;
    if (pages < 2u)
        return;
    b = (ui.bank + pages + (uint32_t)d) % pages;
    cursor_set((int32_t)(b * 16u + ui.cursor % 16u < len ? b * 16u + ui.cursor % 16u : len - 1u));
}

/* the grid's knobs: 1 STEP (the cursor), 2 LANE, 3 HIT and 4 ACC of the lane at the cursor (right on, left off) */
static void grid_edit(uint32_t slot, int32_t steps)
{
    if (slot == 0u)
        cursor_set(ui.cursor + steps);
    else if (slot == 1u)
        ui.lane = (uint8_t)clamp((int32_t)ui.lane + (steps > 0 ? 1 : -1), 0, NLANE - 1);
    else if (slot == 2u)
        grid_hit(TSEL, ui.cursor, ui.lane, steps > 0);
    else
        grid_acc(TSEL, ui.cursor, ui.lane, steps > 0);
}

/* the keys on the grid (presses): a white key toggles the selected lane at its step of the page (its accent
 * while ACC is held) and puts the cursor there; a lane key selects the lane (seq.c plays it); the page keys */
static void drum_hit_key(uint32_t accent);
static void grid_keys(uint32_t pressed)
{
    uint32_t k, len = (uint32_t)TSEL->p[P_SLEN];
    for (k = 0; k < 27u; k++) {
        uint32_t p = key_place(k);
        if (!((pressed >> k) & 1u))
            continue;
        if (!key_black(k)) {
            uint32_t i = ui.bank * 16u + p;
            if (chain_busy()) { ui_message("STOP TO EDIT"); continue; }
            if (i >= len)
                continue;                               /* past LEN: no step there */
            if(cur_page()->scope==SC_DRUMHIT){cursor_set((int32_t)i);drum_hit_key(black_held(GK_ACC));continue;}
            if (black_held(GK_ACC))
                grid_acc(TSEL, i, ui.lane, 2);
            else
                grid_hit(TSEL, i, ui.lane, 2);
            cursor_set((int32_t)i);
        } else if (p < NLANE) {
            ui.lane = (uint8_t)p;
            if(cur_page()->scope==SC_DRUMHIT){ui.drum_sound=(uint8_t)drum_sound_of(TSEL,DRUM_LANE_NOTE[p]);ui.note_pick=0;}
        } else if (p != GK_ACC) {
            page_go(p == GK_PGUP ? 1 : -1);
        }
    }
}

static int live_rec_sel(void);
static void notes_event_edit(uint32_t slot, int32_t delta);
static void step_move_edit(int32_t delta);
/* The sound page edits a kit voice; HIT edits only one event, retaining its instrument. */
static uint32_t drum_hit_selected(void)
{
    uint32_t selected=ui.note_pick?ui.note_pick-1u:RECORD_MAX, first=RECORD_MAX;
    for(uint32_t i=recording_head[recording_owner(TSEL)];i<RECORD_MAX;i=recording_next[i]){
        if(!recording_active(TSEL,i) || recording_view(TSEL,&recording[i])!=ui.cursor || drum_sound_of(TSEL,recording[i].note)!=ui.drum_sound%drum_sound_count(TSEL))continue;
        if(i==selected)return i;
        if(first==RECORD_MAX || recording[i].on<recording[first].on)first=i;
    }
    return first;
}
static void drum_sound_edit(uint32_t slot,int32_t delta)
{
    ui.drum_sound%=drum_sound_count(TSEL);
    uint32_t field=cur_page()->id[slot];
    if(!field){ui.drum_sound=(uint8_t)clamp(ui.drum_sound+delta,0,drum_sound_count(TSEL)-1);ui.force=1;return;}
    if(field==255)return;
    fm1_irq_off();
    if(field==4){int8_t *p=&drum_patch[song.sel].c[ui.drum_sound][3];*p=(int8_t)clamp(*p-delta,0,127);}
    else {int8_t *p=&drum_patch[song.sel].c[ui.drum_sound][field-1];*p=(int8_t)clamp(*p+delta,field==1?-24:-64,field==1?24:63);}
    fm1_irq_on();ui.force=1;
}
static void drum_hit_jog(int32_t delta);
static void drum_hit_edit(uint32_t slot,int32_t delta)
{
    ui.drum_sound%=drum_sound_count(TSEL);
    if(!slot){drum_hit_jog(delta);return;}         /* KNOB 1: the hits one by one, the steps */
    if(slot==1){ui.drum_sound=(uint8_t)clamp(ui.drum_sound+delta,0,drum_sound_count(TSEL)-1);ui.lane=(uint8_t)drum_lane(drum_sound_note(TSEL,ui.drum_sound));ui.note_pick=0;ui.force=1;notes_preview();return;}
    if(chain_busy() || live_rec_sel()){ui_message("STOP TO EDIT");return;}
    step_history_end();step_history_finish();fm1_irq_off();step_history_sync_locked();
    uint32_t i=drum_hit_selected(), created=i>=RECORD_MAX;
    recorded_note_t before={0},after;
    if(!created)before=recording_snapshot(i);
    else {
        /* A new edited hit replaces its identical manual/grid hit; all other
         * voices in this step retain their original velocity and playback. */
        for(i=0;i<RECORD_MAX && recording[i].vel;i++);
        if(i==RECORD_MAX){fm1_irq_on();ui_message("RECORDING FULL");return;}
    }
    note_assign(&after,&before);
    if(created){after.note=(uint8_t)drum_sound_note(TSEL,ui.drum_sound);after.vel=96;after.owner=(uint8_t)(recording_owner(TSEL)|(1u<<5));after.duration=32768;after.step=ui.cursor;
        const step_t *st=&TSEL->step[ui.cursor];for(uint32_t j=0;j<st->n;j++)if(drum_sound_of(TSEL,st->note[j])==ui.drum_sound){after.note=st->note[j];break;}after.vel=(uint8_t)(step_accents(st)&(1u<<drum_lane(after.note))?127:st->vel?st->vel:96);}
    if(slot==2)after.pitch=(int8_t)clamp(after.pitch+delta,-24,24);
    else {
        int32_t duration=(int32_t)after.duration*(1u<<(after.owner>>5));
        duration+=clamp(delta,-128,128)*(int32_t)(RECORD_UNIT/16u);
        after.length=duration>0;duration=clamp(duration,1,128*RECORD_UNIT);
        uint32_t exp=0;while(duration>65535&&exp<7u){duration>>=1;exp++;}
        after.duration=(uint16_t)duration;after.owner=(uint8_t)((after.owner&31u)|(exp<<5));
    }
    if(!created && !memcmp(&before,&after,sizeof before)){fm1_irq_on();return;}
    note_assign(&step_history.removed,&before);note_assign(&step_history.replacement,&after);step_history.removed_index=(uint16_t)i;step_history.has_removed=1;
    if(!created)recording_remove(TSEL,i);
    recording_restore_note(TSEL,i,after);
    step_t *st=&TSEL->step[ui.cursor];st->time=ST_NOTE;st->flags|=SF_RECORDED;
    uint32_t lane=drum_lane(after.note);
    if(after.note==DRUM_LANE_NOTE[lane])st->hit|=(uint8_t)(1u<<lane);
    else {uint32_t j;for(j=0;j<st->n && st->note[j]!=after.note;j++){}if(j==st->n && st->n<4u)st->note[st->n++]=after.note;}
    step_history.recording_gen=recording_generation;
    ui.note_pick=(uint16_t)(i+1);note_assign(&ui.note_identity,&after);ui.note_generation=recording_generation;
    fm1_irq_on();ui.force=1;notes_preview();
}

static void drum_hit_jog(int32_t delta)
{
    notes_step_jog(delta);uint32_t i=notes_selected(TSEL);
    if(i<RECORD_MAX){ui.drum_sound=(uint8_t)drum_sound_of(TSEL,recording[i].note);ui.lane=(uint8_t)drum_lane(recording[i].note);}
    notes_preview();ui.force=1;
}
static void drum_hit_delete(void)
{
    if(chain_busy() || live_rec_sel()){ui_message("STOP TO EDIT");return;}
    step_history_end();step_history_finish();fm1_irq_off();step_history_sync_locked();
    uint32_t i=drum_hit_selected(),note=drum_sound_note(TSEL,ui.drum_sound%drum_sound_count(TSEL));
    if(i>=RECORD_MAX)for(uint32_t j=0;j<TSEL->step[ui.cursor].n;j++)if(drum_sound_of(TSEL,TSEL->step[ui.cursor].note[j])==ui.drum_sound%drum_sound_count(TSEL)){note=TSEL->step[ui.cursor].note[j];break;}
    if(i<RECORD_MAX){note=recording[i].note;step_history.removed=recording_snapshot(i);memset(&step_history.replacement,0,sizeof step_history.replacement);step_history.removed_index=(uint16_t)i;step_history.has_removed=1;recording_remove(TSEL,i);}
    uint32_t remain=0,same=0;
    for(uint32_t j=recording_head[recording_owner(TSEL)];j<RECORD_MAX;j=recording_next[j])if(recording_active(TSEL,j) && recording_view(TSEL,&recording[j])==ui.cursor){remain++;same|=recording[j].note==note;}
    step_t *st=&TSEL->step[ui.cursor];
    if(!same){uint32_t lane=drum_lane(note);if(note==DRUM_LANE_NOTE[lane]){st->hit&=(uint8_t)~(1u<<lane);st->acc&=(uint8_t)~(1u<<lane);}
        for(uint32_t j=0;j<st->n;j++)if(st->note[j]==note){for(uint32_t k=j;k+1<st->n;k++)st->note[k]=st->note[k+1];st->n--;break;}}
    if(!remain)st->flags&=(uint8_t)~SF_RECORDED;
    if(!st->n && !st->hit && !remain)step_clear(st);
    step_history.recording_gen=recording_generation;ui.note_pick=0;ui.note_generation=recording_generation;fm1_irq_on();ui.force=1;ui_message("HIT CLEARED");
}
static void drum_hit_key(uint32_t accent)
{
    if(chain_busy() || live_rec_sel()){ui_message("STOP TO EDIT");return;}
    uint32_t note=drum_sound_note(TSEL,ui.drum_sound%drum_sound_count(TSEL)),lane=drum_lane(note),present=drum_hit_selected()<RECORD_MAX;
    const step_t *st=&TSEL->step[ui.cursor];present|=note==DRUM_LANE_NOTE[lane] && ((st->hit>>lane)&1u);
    for(uint32_t j=0;j<st->n;j++)present|=drum_sound_of(TSEL,st->note[j])==ui.drum_sound%drum_sound_count(TSEL);
    if(present && !accent){drum_hit_delete();return;}
    drum_hit_edit(2,0);
    if(accent){uint32_t i=drum_hit_selected();if(i<RECORD_MAX){fm1_irq_off();recording[i].vel=127;note_assign(&step_history.replacement,&recording[i]);TSEL->step[ui.cursor].acc|=(uint8_t)(1u<<lane);fm1_irq_on();}}
    notes_preview();
}

static void step_edit(uint32_t slot, int32_t steps)
{
    if (slot == 0u) {                                   /* KNOB 1: the notes one by one, the steps (empty ones too) */
        if (!ui.entry_open) {
            if (cur_page()->graph == GR_ROLL && !grid_on()) {if(bulk.on||ui.note_move||ui.note_copy)step_move_edit(steps);else notes_step_jog(steps);}
            else cursor_set(ui.cursor + steps);
        }
        return;
    }
    if (live_rec_sel()) { ui_message("STOP RECORDING"); return; }
    if (notes_selected(TSEL) < RECORD_MAX) {
        uint32_t fine = slot == 2u ? ui.step_mods & (1u << panel.btn[B_ENV]) : 0u;
        ui.step_used |= (uint16_t)fine;
        notes_event_edit(fine ? 4u : slot, steps);
        return;
    }
    if (grid_on()) { grid_edit(slot, steps); return; }
    if(slot==2u){uint32_t fine=ui.step_mods&(1u<<panel.btn[B_ENV]);ui.step_used|=(uint16_t)fine;note_manual_length(steps,fine?RECORD_UNIT/16u:RECORD_UNIT/4u);return;}
    fm1_irq_off();
    uint32_t at = notes_manual_start(TSEL);
    if (at >= NSTEP && (slot != 1u || (TSEL->step[ui.cursor].flags & SF_RECORDED))) { fm1_irq_on(); ui_message("SELECT A NOTE"); return; }
    step_t *st = &TSEL->step[at < NSTEP ? at : ui.cursor];
    if (slot == 1u) {
        if (!st->n) {
            if (st->flags & SF_RECORDED) { fm1_irq_on(); ui_message("SELECT A NOTE"); return; }
            st->note[0] = last_note; st->n = 1; st->time = ST_NOTE;
        } else {
            uint32_t j = notes_manual_slot(st);
            st->note[j] = (uint8_t)clamp((int32_t)st->note[j] + steps, 1, 127);
        }
        last_note = st->note[notes_manual_slot(st)];
    } else if (slot == 2u) {
        ui.step_used |= (uint16_t)(ui.step_mods & (1u << panel.btn[B_ENV]));
        step_note_resize(TSEL, at, (int32_t)step_note_length(TSEL, at) + steps);
    } else if (ui.step_mods & (1u << panel.btn[B_ENV])) {
        ui.step_used |= (uint16_t)(1u << panel.btn[B_ENV]);
        if (steps > 0) st->flags |= SF_SLIDE; else st->flags &= (uint8_t)~SF_SLIDE;
    } else {
        st->vel = (uint8_t)clamp((int32_t)(st->flags & SF_ACCENT ? 127 : st->vel ? st->vel : 96) + steps, 1, 127);
        st->flags &= (uint8_t)~SF_ACCENT;
    }
    fm1_irq_on();
}

/* REC armed, stopped: a knob that would record its moves says the transport has to run (motion lands on the step
 * playing) */
static void motion_unplayed(void)
{
    if (((song.rec >> song.sel) & 1u) && !song.playing && !chain_busy())
        ui_message("PLAY TO RECORD MOTION");
}
/* an edit of the engine's own patch (its bytes before: old, under motion_guard): its motion (motion.c) */
static void native_motion(const uint8_t *old)
{
    if (!designer_on() && !momentary.active && motion_native_edited(TSEL, old))
        motion_unplayed();
}
static void edit_param(uint32_t slot, int32_t steps)
{
    int16_t *vp;
    const page_t *pg = cur_page();
    const param_desc_t *d;
    int32_t v;
    if (pg->graph == GR_SLOTS && slot == 0u && (ui.proj_new || (steps > 0 && song.g[G_SLOT] == PROJ_TMPL))) {
        ui.proj_new = steps > 0;                        /* past TMPL: NEW (SLOT itself stays) */
        if (!ui.proj_new && ui.act == 4u) ui.act = 0;
        return;
    }
    if (pg->graph == GR_PATGRID) {                      /* PATTERNS: KNOB k queues track k's (ui_stage.c) */
        patgrid_edit(slot, steps);
        return;
    }
    if (pg->graph == GR_SONG) {                       /* KNOB 1 the section, 2 the track, 3 its pattern there, 4 the
                                                       * section's repeats (TAKE JAM: REC held) */
        if (slot == 0u) {
            ui.song_row = (uint8_t)clamp((int32_t)ui.song_row + steps, 0,
                chain_config.count < CHAIN_ROWS ? chain_config.count : CHAIN_ROWS - 1u);
            return;
        }
        if (slot == 1u) {
            track_select((uint32_t)clamp((int32_t)song.sel + (steps > 0 ? 1 : -1), 0, NTRK - 1));
            return;
        }
        if (chain_busy()) { ui_message("STOP TO EDIT"); return; }
        if (slot == 3u && ui.song_row >= chain_config.count)
            return;                                 /* (repeats of the column to add: none yet; KNOB 3 adds it) */
        if (ui.song_row >= chain_config.count) {
            chain_row_t *r = &chain_config.row[ui.song_row];
            r->slot = 0;
            if (ui.song_row) memcpy(chain_patterns[ui.song_row], chain_patterns[ui.song_row - 1u], NTRK);
            else for (uint32_t k = 0; k < NTRK; k++) chain_patterns[ui.song_row][k] = trk[k].pattern;
            r->repeat = 1;
            chain_config.count = ui.song_row + 1u;
            if (slot == 2u) return;
        }
        if (slot == 2u)
            chain_patterns[ui.song_row][song.sel] = (uint8_t)clamp((int32_t)chain_patterns[ui.song_row][song.sel] + steps, 0, NPAT - 1u);
        chain_config.row[ui.song_row].slot = chain_patterns[ui.song_row][0];
        if (slot == 3u)
            chain_config.row[ui.song_row].repeat = (uint8_t)clamp((int32_t)chain_config.row[ui.song_row].repeat + steps, 1, 16);
        return;
    }
    if (chain_busy() && (pg->scope == SC_STEP || pg->graph == GR_STEPS ||
        0)) {
        ui_message("STOP TO EDIT"); return;
    }
    if(pg->scope==SC_DRUM){drum_sound_edit(slot,steps);return;}
    if(pg->scope==SC_DRUMHIT){drum_hit_edit(slot,steps);return;}
    if (pg->scope == SC_STEP) {
        step_edit(slot, steps);
        return;
    }
    if (pg->scope == SC_TRK) {
        tracks_edit(slot, steps);
        return;
    }
    if (pg->graph == GR_SCALE_PICKER) {
        scale_picker_edit(slot, slot == 1u ? list_accel(EN_K1 + slot, steps, scale_picker_count(), 0) : steps);
        return;
    }
    if (scale_settings_page(pg) && slot == 1u) {
        scale_picker_mark(steps > 0);
        return;
    }
    if (pg->graph == GR_BROWSE) {   /* the browser's two columns: KNOB 1 LIST (ALL FAV RECENT, the categories), 2 the
                                     * sounds in it (as PRESETS); 3 FAV, 4 the next / previous engine */
        if (slot == 1u) {
            browse_turn(EN_K2, steps);
            return;
        }
        browse_commit();                                  /* (the others act on the sound shown: load it first) */
        if (slot == 3u) {
            select_engine(eng_step(TSEL->eng_req, steps));
        } else if (slot == 2u) {
            preset_mark(steps > 0);
        } else if (slot == 0u) {                          /* ALL FAV RECENT, the categories (no wrap) */
            uint32_t m = (uint32_t)clamp((int32_t)list_mode() + (steps > 0 ? 1 : -1), 0, (int32_t)LM_N - 1);
            if (m != list_mode()) {
                list_set(m);
                ui.force = 1;
                settings_save();
            }
        }
        return;
    }
#if MELODEE_SLICE
    if (pg->graph == GR_SLICES && slot < 2u) {           /* SLICES: KNOB 1 the marker, 2 moves it (ui_slice.c) */
        if (slice_page_ok())                              /* (the engine changed before ui_draw left the page) */
            slice_knob(slot, steps);
        return;
    }
#endif
    if ((act_cols() >> slot) & 1u) {                      /* an action's knob picks it (right) or drops it (left); */
        ui.act = steps > 0 ? (uint8_t)(slot + 1u) : ui.act == slot + 1u ? 0u : ui.act;   /* (OCT+ does it: act_do) */
        return;
    }
    if (pg->graph == GR_USER) {                           /* KNOB 1 the slot */
        if (slot == 0u)
            ui.uslot = (uint8_t)clamp((int32_t)ui.uslot + list_accel(EN_K1, steps, user_limit(), 0), 0, user_limit() - 1);
        return;
    }
    if (pg->graph == GR_MOD && slot == 1u) {             /* MOD: KNOB 2 the route; 1 its SRC, 3 DST, 4 AMT */
        mod_ui_slot = (uint8_t)clamp((int32_t)mod_ui_slot + (steps > 0 ? 1 : -1), 0, 3);
        return;
    }
    d = page_desc(pg, slot, &vp);
    if (!d || !vp || d->max == d->min)
        return;
    v = enum_step(d, *vp, clamp(*vp + accel(EN_K1 + slot, steps, desc_range(d)), d->min, d->max));
    *vp = (int16_t)v;
    if(pg->scope==SC_P5){
        uint8_t old[MO_NATIVE_N];uint32_t f=motion_guard();
        motion_native_peek(TSEL,old);p5_edit_value(TSEL,pg->id[slot],p5_panel_raw(TSEL,pg->id[slot],v));native_motion(old);
        motion_unguard(f);return;
    }
    if(pg->scope==SC_P5STORE){p5_store_slot=(int16_t)v;return;}
    if (pg->scope == SC_CZ1) {                           /* (a copy of the tone's value: written back there) */
        uint8_t raw[CZ_BYTES], old[MO_NATIVE_N];
        uint32_t tr = song.sel % NTRK, f = motion_guard();
        motion_native_peek(TSEL, old);
        if (cz_ed_put(tr, pg->id[slot], (uint32_t)v, raw)) {
            if (!momentary.active) cz_compare_take(tr);
            memcpy(cz_patch[tr].raw, raw, 128u);
            native_motion(old);
        }
        motion_unguard(f);
        return;
    }
    if (pg->scope == SC_FM6 || pg->scope == SC_FMOP) {   /* (a copy of FM6's value: written back there) */
        uint8_t old[MO_NATIVE_N];
        uint32_t f = motion_guard();
        motion_native_peek(TSEL, old);
        fm6_page_put(pg, slot, v);
        native_motion(old);
        motion_unguard(f);
        return;
    }
    if (pg->scope == SC_GLOBAL && pg->id[slot] == G_BOOT) {   /* BOOT: the device's (settings), saved */
        settings_boot = (uint8_t)v;
        settings_save();
        return;
    }
    if (pg->scope == SC_GLOBAL && pg->id[slot] == G_A4) {
        settings_save();
        return;
    }
    if (pg->scope == SC_GLOBAL && pg->id[slot] == G_DRUMCH) { /* DRUM: the device's too */
        settings_drumch = (uint8_t)v;
        settings_save();
        return;
    }
    if ((pg->scope != SC_GLOBAL || pg->id[slot]==G_TRK_SEND) && !momentary.active && !designer_on()) {
        motion_capture(TSEL, (uint32_t)(vp - TSEL->p), *vp);
        if (motion_param((uint32_t)(vp - TSEL->p)))
            motion_unplayed();
    }
    if (pg->scope == SC_TRACK && scale_shared((uint32_t)(vp - TSEL->p)))
        scale_share(TSEL);
}

/* CZ TOOLS 1 > 2 / 2 > 1: line src's wave, window, key follow, level, velocity and envelopes over the other
 * line (Casio's 57-byte line block); MOD stays line 1's. Undoable as a sound load */
static void cz_line_copy(track_t *t, uint32_t src)
{
    uint8_t *b = cz_patch[trk_index(t)].raw;
    uint32_t s = src ? 71u : 14u, d = src ? 14u : 71u, mod = b[d + 1u] & 0x38u;
    cz_compare_take(trk_index(t));
    load_begin(t, UNDO_SOUND);
    fm1_irq_off();
    memcpy(b + d, b + s, 57u);
    b[d + 1u] = (uint8_t)((b[d + 1u] & ~0x38u) | mod);
    fm1_irq_on();
    load_end(t);
}

/* CZ TOOLS COMP: the tone before the first edit and the edited one change places; 0 = nothing edited */
static int cz_compare_swap(uint32_t tr)
{
    cz_patch_t x = cz_patch[tr % NTRK];
    if (cz_compare_tr != tr % NTRK + 1u)
        return 0;
    fm1_irq_off();
    cz_patch[tr % NTRK] = cz_compare;
    fm1_irq_on();
    cz_compare = x;
    return 1;
}

/* OCT+ on an action page: the picked action. A load stays picked (browse and load again); the others
 * are dropped once done. Flash writes only while stopped; over the user's data: the dialog */
static void act_do(void)
{
    uint32_t c = act_col(), id, k = (uint32_t)song.g[G_SLOT] - 1u;
    if (!c--)
        return;
    if(cur_page()->scope==SC_P5STORE){
        uint32_t slot=(uint32_t)p5_store_slot-1u;
        if(c==1u){if(transport_busy())ui_message("STOP TO SAVE");else if(native_used(ENGI_PROPHET,slot))confirm_open(CF_OVR_USER,slot);else name_open(NK_USER_SAVE,slot);}
        else if(c==2u)p5_send(song.sel);
        else {load_begin(TSEL,UNDO_SOUND);panic_req|=1u<<song.sel;fm1_irq_off();p5_patch_init(p5_patch_of(TSEL));p5_track_accept(TSEL);fm1_irq_on();load_end(TSEL);ui_message("PROPHET INIT");}
        ui.act=0;return;
    }
    if (cur_page()->graph == GR_CZTOOLS) {               /* CZ-1: NAME, copy a line over the other, COMPARE */
        uint32_t tr = song.sel % NTRK;
        if (chain_busy()) { ui_message("STOP TO EDIT"); return; }
        if (c == 0u) {
            name_open(NK_CZ_NAME, tr);
        } else if (c < 3u) {
            cz_line_copy(TSEL, c == 1u ? 0u : 1u);
            ui_message(c == 1u ? "LINE 1 > 2" : "LINE 2 > 1");
        } else {
            ui_message(cz_compare_swap(tr) ? "COMPARE / EDIT" : "NO EDITS");
        }
        ui.act = 0;
        ui.force = 1;
        return;
    }
    if (cur_page()->graph == GR_SONG) {
        if (song.playing || seq_counting() || chain_busy()) transport_req = 2;
        else chain_play_ui();
        return;
    }
#if MELODEE_SLICE
    if (cur_page()->graph == GR_SLICES) {                 /* SPLIT / JOIN: stays picked (split again, join again) */
        if (slice_page_ok())
            slice_act(c);
        return;
    }
#endif
    if (cur_page()->graph == GR_FMSTORE) {                /* FM6: 1 STORE (into fm6_bslot), 2 SEND, 3 INIT */
        if (c == 1u) {
            if (transport_busy()) ui_message("STOP TO SAVE");
            else if (native_used(ENGI_FM6,fm6_bslot) || native_old(ENGI_FM6,fm6_bslot)) confirm_open(CF_OVR_USER, fm6_bslot);
            else fm6_store(fm6_bslot);
        }
        else if (c == 2u)
            fm6_send();
        else if (c == 3u)
            fm6_init_voice();
        ui.act = 0;
        return;
    }
    if (cur_page()->graph == GR_USER) {                   /* 1 LOAD, 2 ERASE, 3 SAVE (the NAME screen first) */
        if (c > 1u)
            ui.act = 0;
        ui.uslot %= user_limit();
        if (c == 2u && (user_used(ui.uslot) || user_old(ui.uslot)) && !transport_busy())
            confirm_open(CF_ERASE_USER, ui.uslot);        /* ERASE: the dialog first */
        else if (c != 3u)
            user_ui_named(c - 1u, ui.uslot, 0);
        else if (transport_busy())
            ui_message("STOP TO SAVE");
        else if (user_used(ui.uslot) || user_old(ui.uslot))
            confirm_open(CF_OVR_USER, ui.uslot);
        else
            name_open(NK_USER_SAVE, ui.uslot);
        return;
    }
    id = cur_page()->id[c & 3u];
    if (id != G_LOAD)
        ui.act = 0;
    if (id == G_LOAD && ui.proj_new) {                  /* NEW: a new song (unsaved changes: the dialog first) */
        if (transport_busy()) ui_message("STOP FIRST");
        else if (project_dirty()) confirm_open(CF_NEW_SONG, 0);
        else new_open();
        ui.act = 0;
        return;
    }
    if (song.g[G_SLOT] == PROJ_TMPL && (id == G_LOAD || id == G_SAVE)) {   /* the template: no name, no dialog */
        if (id == G_LOAD)
            template_load();
        else
            if (template_old()) confirm_open(CF_OLD_TEMPLATE, 0); else template_save();
        return;
    }
    switch (id) {
    case G_LOAD:
        project_load(k);
        break;
    case G_SAVE:                                          /* the NAME screen writes it */
        if (transport_busy())
            ui_message("STOP TO SAVE");
        else if (project_used(k) || project_old(k))     /* (an old record too: only replaced when asked) */
            confirm_open(CF_OVR_PROJ, k);
        else
            name_open(NK_PROJ_SAVE, k);
        break;
    case G_CLRSEQ:
        if (chain_busy()) { ui_message("STOP TO EDIT"); break; }
        confirm_open(CF_CLEAR_SEQ, song.sel);
        break;
    default:                                              /* G_INITSND */
        sound_init_of(TSEL, TSEL->eng_req);               /* the engine's INIT (the steps stay) */
        ui_message("SOUND INIT");
        ui.force = 1;
        break;
    }
}

/* OCT- / OCT+ as Esc / Enter (oct_nav): on release, and only a press that began there; both down together
 * (UPDATE MODE, main.c) is no tap. Bit 0 OCT-, bit 1 OCT+ */
static uint8_t oct_eat;                                 /* OCT taps not to come (held for a sheet, used in a combo) */
static uint8_t oct_deferred;                            /* OCT+ pressed on Stage (its sheet when held): the octave on
                                                         * release */
static uint32_t oct_taps(uint32_t pressed, int here)
{
    static uint8_t down, chord;
    uint32_t dn = panel.btn[B_OCTDN], up = panel.btn[B_OCTUP];
    uint32_t now = ((fm1_in.buttons >> dn) & 1u) | ((fm1_in.buttons >> up) & 1u) << 1, tap;
    if (here)
        down |= (uint8_t)(((pressed >> dn) & 1u) | ((pressed >> up) & 1u) << 1);
    if (now == 3u)
        chord = 1;
    tap = down & ~now;
    down &= (uint8_t)now;
    if (chord) {
        tap = 0;
        chord = now != 0u;
    }
    tap &= ~(uint32_t)oct_eat;
    oct_eat &= (uint8_t)now;
    return tap;
}

/* SEQ step entry, acid style: the keys pressed together (POLY: up to 4 notes, MONO:
 * the last one) become the cursor step; releasing all keys moves on. With CHRD on a key
 * writes what it sounds, as live recording does: POLY its chord, MONO the chord's root */
static int step_page(void) { return !ui.home && (cur_page()->scope == SC_STEP || cur_page()->scope==SC_DRUMHIT); }   /* SEQ > NOTES, DRUM HIT */
/* ENV / SCL / EDIT held on STEP: their note gestures (SELECT resizes, moves; EDIT + OCT-/+ undoes, redoes; EDIT tapped
 * deletes), armed and playing too. EDIT's quick layer: off the synth STEP */
static int step_modifier_context(void)
{
    return step_page() && (!drum_track(TSEL) || !grid_on() || (cur_page()->graph == GR_ROLL && live_rec_sel())) &&
           !ui.menu && !ui.confirm && !ui.ly && !name_on();
}
static uint32_t step_modifier_mask(void)                /* (EDIT: NOTES only) */
{
    if (grid_on() && live_rec_sel()) return 1u << panel.btn[B_EDIT];
    return (1u << panel.btn[B_ENV]) | (1u << panel.btn[B_SCL]) |
           (cur_page()->graph == GR_ROLL ? 1u << panel.btn[B_EDIT] : 0u);
}

/* An EDIT chord is not a delete tap. Remember it across page/track changes
 * and through release-frame encoder detents, just like undo/redo chords. */
static void step_edit_combo(void)
{
    ui.step_used |= (uint16_t)(ui.step_mods & (1u << panel.btn[B_EDIT]));
}

static void step_length_edit(int32_t delta)
{
    if (cur_page()->graph == GR_ROLL && notes_selected(TSEL) < RECORD_MAX) { notes_event_edit(2, delta); return; }
    uint32_t at, n = 0;
    fm1_irq_off();
    at = cur_page()->graph == GR_ROLL ? notes_manual_start(TSEL) : step_note_start(TSEL, ui.cursor);
    if (at < NSTEP)
        n = step_note_resize(TSEL, at, (int32_t)step_note_length(TSEL, at) + delta);
    fm1_irq_on();
    if (n) {
        char b[8];
        fmt_int(b, (int32_t)n);
        ui_say("LEN ", b);
    } else {
        ui_message("SELECT A NOTE");
    }
    ui.force = 1;
}

static void step_move_edit(int32_t delta)
{
    if(bulk.on || ui.note_move || ui.note_copy || (ui.step_mods&(1u<<panel.btn[B_ENV]))) {
        note_move_snapped(delta,RECORD_UNIT/4u,ui.note_copy);ui.note_copy=0;return;
    }
    if (cur_page()->graph == GR_ROLL && notes_selected(TSEL) < RECORD_MAX) { notes_event_edit(0, delta); return; }
    uint32_t at,to;fm1_irq_off();at=cur_page()->graph==GR_ROLL?notes_manual_start(TSEL):step_note_start(TSEL,ui.cursor);
    to=step_note_move(TSEL,at,delta);fm1_irq_on();
    if(to<NSTEP){uint8_t entry=ui.entry_open;cursor_set((int32_t)to);ui.entry_open=entry;ui_message(to==at?"NEXT NOTE":"NOTE MOVED");}
    else ui_message("SELECT A NOTE");ui.force=1;
}

/* Edit one event atomically; neighbours and original fractional timing survive. */
static void notes_event_edit(uint32_t slot, int32_t delta)
{
    if (chain_busy()) { ui_message("STOP TO EDIT"); return; }
    if (live_rec_sel()) { ui_message("STOP RECORDING"); return; }
    step_history_end(); step_history_finish();
    fm1_irq_off();
    step_history_sync_locked();
    uint32_t i = notes_selected(TSEL);
    if (i >= RECORD_MAX) { fm1_irq_on(); return; }
    recorded_note_t before = recording_snapshot(i), after;
    note_assign(&after,&before);
    uint32_t oldview = recording_view(TSEL, &before);
    if (slot == 0u) {
        uint32_t len = step_pattern_len(TSEL);
        int32_t actual = ((int32_t)(before.step & 63u) + delta) % (int32_t)len;
        if (actual < 0) actual += len;
        uint32_t view = (actual + ((before.step & 64u) != 0u)) % len;
        if (notes_manual_occupied(TSEL, view)) {
            fm1_irq_on(); ui_message("STEP OCCUPIED"); return;
        }
        after.step = (uint8_t)(actual | (before.step & 64u) | (!view && actual ? 128u : 0u));
    } else if (slot == 1u) {
        if(drum_track(TSEL))after.pitch=(int8_t)clamp(before.pitch+delta,-24,24);
        else after.note=(uint8_t)clamp((int32_t)before.note+delta,1,127);
    }
    else if (slot == 2u || slot == 4u) {
        int32_t duration = (int32_t)before.duration * (1u << (before.owner >> 5));
        uint32_t quantum=slot==4u?RECORD_UNIT/16u:RECORD_UNIT/4u;
        duration=clamp(note_snap(duration,delta,quantum),(int32_t)quantum,128*RECORD_UNIT);
        uint32_t exp=0;while(duration>65535&&exp<7u){duration>>=1;exp++;}
        after.duration=(uint16_t)duration;after.owner=(uint8_t)((after.owner&31u)|(exp<<5));
        if(drum_track(TSEL))after.length=1;
    } else after.vel = (uint8_t)clamp((int32_t)before.vel + delta, 1, 127);
    if (!memcmp(&before, &after, sizeof before)) { fm1_irq_on(); return; }
    step_history.state[step_history_index()].selection = ui.note_pick;
    step_history.state[step_history_index()].cursor = ui.cursor;
    note_assign(&step_history.removed,&before); note_assign(&step_history.replacement,&after);
    step_history.removed_index = (uint16_t)i; step_history.has_removed = 1;
    recording_remove(TSEL, i); recording_restore_note(TSEL, i, after);
    uint32_t view = recording_view(TSEL, &after);
    TSEL->step[view].time = ST_NOTE; TSEL->step[view].flags |= SF_RECORDED;
    notes_rebuild(TSEL, oldview);
    if (view != oldview) notes_rebuild(TSEL, view);
    step_history.recording_gen = recording_generation;
    if (slot == 0u) cursor_set(after.step & 63u);
    ui.note_pick = (uint16_t)(i + 1u); note_assign(&ui.note_identity,&after);
    ui.note_generation = recording_generation;
    fm1_irq_on(); ui.force = 1;
}

static void notes_delete_edit(void)
{
    if (chain_busy()) { ui_message("STOP TO EDIT"); return; }
    if (live_rec_sel()) { ui_message("STOP RECORDING"); return; }
    step_history_end();
    step_history_finish();
    fm1_irq_off();
    step_history_sync_locked(); /* a recorder ISR could have run since the frame's first sync */
    uint32_t i = notes_selected(TSEL);
    if (i >= RECORD_MAX) {
        fm1_irq_on();
        ui_message("NO RECORDED NOTE");
        return;
    }
    /* One event delta per history entry keeps undo bounded without copying
     * all 1,024 events for each of the eight edits. */
    step_history.state[step_history_index()].selection = ui.note_pick;
    step_history.state[step_history_index()].cursor = ui.cursor;
    step_history.removed = recording_snapshot(i);
    step_history.removed_index = (uint16_t)i;
    step_history.has_removed = 1;
    memset(&step_history.replacement, 0, sizeof step_history.replacement);
    uint32_t view = recording_view(TSEL, &recording[i]);
    notes_cycle(1);
    recording_remove(TSEL, i);
    notes_rebuild(TSEL, view);
    step_history.recording_gen = recording_generation; /* accept only this protected UI mutation */
    ui.note_generation = recording_generation;
    if (ui.note_pick == i + 1u) ui.note_pick = 0;
    notes_selected(TSEL);
    fm1_irq_on();
    ui_message("NOTE DELETED");
    ui.force = 1;
}

static void step_delete_edit(void)
{
    if(cur_page()->graph==GR_ROLL&&bulk.on){notes_bulk_delete();return;}
    if (cur_page()->graph == GR_ROLL && notes_selected(TSEL) < RECORD_MAX) { notes_delete_edit(); return; }
    if (cur_page()->graph == GR_ROLL) {
        uint32_t start = notes_manual_start(TSEL);
        if (start < NSTEP && TSEL->step[start].n > 1u) {
            fm1_irq_off();
            step_t *st = &TSEL->step[start];
            uint32_t j = notes_manual_slot(st);
            for (; j + 1u < st->n; j++) st->note[j] = st->note[j + 1u];
            st->n--; ui.note_slot = 0; fm1_irq_on(); ui_message("NOTE DELETED"); return;
        }
        if (TSEL->step[ui.cursor].flags & SF_RECORDED) { ui_message("SELECT A NOTE"); return; }
    }
    uint32_t at;
    step_history_end();                              /* deletion is separate from a held entry */
    step_history_finish();
    fm1_irq_off();
    at = step_note_start(TSEL, ui.cursor);
    step_delete(TSEL, ui.cursor);
    fm1_irq_on();
    cursor_set((int32_t)(at < NSTEP ? at : ui.cursor) + 1);
    ui_message(at < NSTEP ? "NOTE CLEARED" : "STEP CLEARED");
}

/* the cursor step takes notes n[0..cnt) (a key's chord, or what a MIDI note played): the first of an entry starts
 * the step afresh. MONO / LEGATO / UNISON (not a kit): one note, the first */
static void seq_entry_notes(const uint8_t *n, uint32_t cnt)
{
    track_t *t = TSEL;
    step_t *st = &t->step[ui.cursor];
    uint32_t i, j;
    if (!cnt)
        return;
    if (!ui.entry_open) {
        step_history.cursor_before = ui.cursor;
        ui.entry_open = 1;
        if (!settings_chord_add) st->n = 0;
        st->flags &= (uint8_t)~SF_RECORDED;
        st->time = ST_NOTE;
    }
    if (t->p[P_VOICE] && !ENGINES[t->engine]->oneshot) {   /* (drums: hits stack as a chord) */
        st->note[0] = n[0];
        st->n = 1;
    } else {
        for (i = 0; i < cnt; i++) {
            for (j = 0; j < st->n && st->note[j] != n[i]; j++)
                ;
            if (j < st->n && settings_chord_add) {
                for(uint32_t k=j+1;k<st->n;k++)st->note[k-1]=st->note[k]; st->n--;
            } else if (j == st->n) {
                if(st->n<4u)st->note[st->n++]=n[i]; else ui_message("CHORD FULL");
            }
        }
    }
}

/* Complete entry after knobs, so the final SELECT detent still edits the note being released. */
static void seq_entry_finish(void)
{
    uint32_t held = fm1_in.notes & ~kb_layer, k;
    for (k = 0; k < 27u; k++)
        if ((held >> k) & 1u && kb_map(TSEL, k) == KB_SILENT)
            held &= ~(1u << k);
    if (ui.entry_open && !held && !step_midi_held) {
        uint32_t n = step_note_length(TSEL, ui.cursor);
        if(settings_chord_add)ui.entry_open=0; else cursor_set(ui.cursor + (n ? n : 1u));
        step_history_end();                          /* two MIDI taps in one UI frame remain separate edits */
    }
}

static void seq_midi_events(int accept)
{
    if (!accept) {
        seq_midi_reset();
        return;
    }
    if (step_midi_overflow) {
        seq_midi_reset();
        if (accept && ui.entry_open) {
            ui.entry_open = 0;
            ui_message("MIDI INPUT BUSY");
        }
        return;
    }
    while (step_midi_r != step_midi_w) {
        uint32_t e;
        RING_PUBLISH();
        e = step_midi_q[step_midi_r % STEP_MIDI_Q];
        step_midi_r++;
        if (!accept || ((e >> 8) & 3u) != song.sel)
            continue;
        uint32_t ch = (e >> 12) & 15u, src = (e >> 16) & 127u, bit = 1u << (src % 32u);
        uint32_t *keys = &step_midi_keys[ch][src / 32u];
        if (e & (1u << 11)) {                            /* all off */
            step_midi_held = 0;
            memset(step_midi_keys, 0, sizeof step_midi_keys);
            seq_entry_finish();
        } else if (e & (1u << 10)) {
            if (!step_midi_held)
                seq_entry_finish();                  /* another tap starts a separate entry, even in one frame */
            uint8_t note = (uint8_t)(e & 127u);
            if (!(*keys & bit))
                step_midi_held++;
            *keys |= bit;
            seq_entry_notes(&note, 1);
            last_note = note;
        } else if (*keys & bit) {
            *keys &= ~bit;
            step_midi_held--;
        }
    }
}

/* the keys pressed together (POLY: up to 4, MONO: the first) and the MIDI notes held become the cursor step; SELECT
 * sets its length while they are held; when the last is let go the cursor moves past the note */
static void seq_entry(uint32_t pressed)
{
    track_t *t = TSEL;
    uint32_t k;
    for (k = 0; k < 27u; k++) {
        uint8_t ch[CHORD_MAX];
        int32_t r;
        uint16_t mask;
        uint32_t note;
        if (!((pressed >> k) & 1u))
            continue;
        note = kb_map(t, k);
        if (note == KB_SILENT)
            continue;
        seq_entry_notes(ch, chord_make(t, note, ch, &r, &mask));   /* (CHRD OFF, a kit: the note alone) */
        last_note = (uint8_t)note;
    }
    seq_midi_events(1);
}

/* HOME / REC / SAVE: tap on release, hold 0.7 s fires once. t0 = press time | 1,
 * bit 1 = fired (or swallowed: then the release is no tap either) */
enum { BT_NONE, BT_TAP, BT_HOLD };
static uint32_t btn_hold(uint32_t *t0, uint32_t label, uint32_t now, int hold_ok)
{
    uint32_t tap;
    if ((fm1_in.buttons >> panel.btn[label]) & 1u) {
        if (!*t0)
            *t0 = (now | 1u) & ~2u;
        else if (hold_ok && !(*t0 & 2u) && now - (*t0 & ~3u) > 700u * 1000u * FM1_TICKS_PER_US) {
            *t0 |= 2u;
            return BT_HOLD;
        }
        return BT_NONE;
    }
    tap = *t0 && !(*t0 & 2u);
    *t0 = 0;
    return tap ? BT_TAP : BT_NONE;
}

/* the quick layers: ui_layer.c (included after this file) */
static void layer_masks(void);
static void layer_lock_input(uint32_t pressed);
static void layer_arm(uint32_t pressed, uint32_t now);
static uint32_t layer_held(void);
static uint32_t layer_gesture(uint32_t now, uint32_t combo);
static void layer_show(void);
static void layer_tap(uint32_t l);
static void layer_keys(uint32_t keys);
static void layer_knob(uint32_t k, int32_t s);
static int layer_play(void);
static int layer_set_open(void);
static uint32_t layer_oct(uint32_t pressed, uint32_t oct);
static int layer_allowed(void);
static uint32_t ly_bit(uint32_t l);

/* a page button let go (they act on release; a layer's own button: layer_gesture) */
static void page_tap(uint32_t b)
{
    uint32_t f;
    if (b == B_EDIT && ui.erase_gesture) return;
    if (b == B_EDIT && smap.on) {                       /* the map open: EDIT closes it */
        smap_close();
        return;
    }
    if (b == B_EDIT && sec_on()) {                      /* an EDIT page of an engine in sections: its map */
        smap_open();
        return;
    }
    if(b==B_EDIT && !ui.home && cur_page()->scope==SC_DRUMHIT){drum_hit_delete();return;}
    if (b == B_GLO) {
        open_global();                                  /* MIXER -> GLOBAL -> SYSTEM -> MIXER */
        return;
    }
    if (b == B_EDIT && song.seq_mode && !ui.home && cur_page()->graph == GR_ROLL) {   /* the DRUM grid: EDIT clears
                                                         * the step (synth STEP: a step modifier, ui_input) */
        if (chain_busy()) { ui_message("STOP TO EDIT"); return; }
        if (live_rec_sel() && notes_have_recording(TSEL)) { ui_message("STOP RECORDING"); return; }
        fm1_irq_off();
        if (drum_track(TSEL))
            step_clear(&TSEL->step[ui.cursor]);
        else
            step_delete(TSEL, ui.cursor);
        fm1_irq_on();
        cursor_set(ui.cursor + 1);
        ui_message("STEP CLEARED");
        return;
    }
    if (b == B_EDIT && !ui.home && (cur_page()->graph == GR_USER || cur_page()->graph == GR_SLOTS)) {
        name_rename();                                  /* SAVE > USER / PROJECT: EDIT renames the slot */
        return;
    }
    for (f = FAM_HOME + 1u; f < FAM_COUNT; f++)
        if (FAM_BTN[f] == b) {
            open_family(f);
            return;
        }
}

/* messages of things that happened elsewhere (a load, the editor, MIDI in): after this frame's own */
static void ui_notices(void)
{
    static uint32_t midi_t, midi_last;
    if (ui.msg_t && str_eq(ui.msg, "WAITING FOR LOOP") && !(song.rec_wait & (1u << song.sel)))
        ui_message(rec_on(TSEL) ? "RECORDING" : "REC OFF");
    if (motion_full) { motion_full = 0; ui_message("MOTION FULL"); }
    if (recording_quant_blocked) { recording_quant_blocked = 0; ui_message("QNT STEP OCCUPIED"); }
    if (midi_hint) {                                    /* MIDI notes into a track that is not selected */
        uint32_t h = midi_hint;
        midi_hint = 0;
        if (!ui.msg_t && (h != midi_last || fm1_ms - midi_t > 4000u)) {
            char b[4] = {'T', (char)('0' + h), 0, 0};
            ui_say("MIDI IN -> ", b);
            midi_last = h;
            midi_t = fm1_ms;
        }
    }
}

/* SELECT or KNOB 1 turned on STEP with ENV held: the cursor's note longer / shorter; SCL held: it moves (ties, chord,
 * velocity and all); a key or MIDI note held while entering: its length. 0: none of them held (the turn is the
 * cursor's or the pages') */
static int step_gesture(int32_t s)
{
    uint32_t env = 1u << panel.btn[B_ENV], scl = 1u << panel.btn[B_SCL];
    if (!step_modifier_context() || !((ui.step_mods & (env | scl)) || ui.entry_open))
        return 0;
    ui.step_used |= (uint16_t)(ui.step_mods & (env | scl));
    if (chain_busy()) {
        ui_message("STOP TO EDIT");
    } else if (ui.step_mods & scl) {
        ui.step_move = 1;
        step_move_edit(s);
    } else if (ui.step_mods & env) {
        ui.step_move = 1;
        step_length_edit(s);
    } else {
        step_length_edit(s);
    }
    ui.force = 1;
    return 1;
}

/* Capture one erase gesture and publish its scope to the audio sequencer. */
static void seq_erase_update(uint32_t pressed)
{
    uint32_t ed = 1u << panel.btn[B_EDIT], rec = 1u << panel.btn[B_REC];
    int context = !ui.home && cur_page()->graph == GR_ROLL && !ui.menu && !ui.confirm &&
                  !name_on() && !ui.ly && live_rec_sel() && !chain_busy();
    if ((pressed & ed) && context) {
        ui.erase_gesture = 1;
        ui.erase_owner = (uint8_t)(recording_owner(TSEL) + 1u);
        ui.erase_generation = TSEL->pattern_gen;
        ui.erase_transport = seq_erase_transport;
        fm1_irq_off();
        seq_erase_seen[song.sel] = 0;
        fm1_irq_on();
    }
    if (ui.erase_gesture) {
        ui.step_used |= (uint16_t)ed; /* release is never an additional delete tap */
        if (!context || ui.erase_owner != recording_owner(TSEL) + 1u || ui.erase_generation != TSEL->pattern_gen || ui.erase_transport != seq_erase_transport)
            ui.erase_owner = 0;
    }
    uint32_t request = ui.erase_owner && context && (fm1_in.buttons & ed) ?
        0x80000000u | ((ed | rec) << 16) | (ui.erase_owner - 1u) : 0u;
    fm1_irq_off();
    seq_erase_button = (uint16_t)ed;
    seq_erase_generation = ui.erase_generation;
    seq_erase_request = request;
    fm1_irq_on();
    if (!(fm1_in.buttons & ed)) { ui.erase_gesture = ui.erase_owner = 0; }
}

/* KNOB k turned s on the page shown (the sound designer's pages too): what it edits there, its card hot */
static void page_knob(uint32_t k, int32_t s)
{
    const page_t *pg = cur_page();
    int16_t *hv;
    if (ui.home || pg->scope == SC_STEP || pg->scope == SC_TRK || ((pg->scope==SC_DRUM || pg->scope==SC_DRUMHIT) && pg->id[k]!=255) || page_desc(pg, k, &hv) ||
        (pg->graph == GR_USER && k == 0u) || (pg->graph == GR_MOD && k == 1u)
        || pg->graph == GR_SONG || pg->graph == GR_PATGRID || pg->graph == GR_SCALE_PICKER || (scale_settings_page(pg) && k == 1u)
        || (pg->graph == GR_SLICES && k < 2u)) {       /* (not an empty column) */
        ui.hot_col = (uint8_t)k;
        ui.hot_t = 40;
    }
    if (list_on()) {                                    /* a list page: KNOB 2 the row, KNOB 1 its value (ui_list.c) */
        list_knob(k, s);
        return;
    }
    if (slot_kind()) {                                  /* a slot page: KNOB 1 / 2 the slot (ui_slots.c) */
        if (k < 2u)
            edit_param(0, s);
        return;
    }
    if (motion_page()) {                                /* MOTION: KNOB 2 the lane, KNOB 1 Play (ui_motion.c) */
        motion_knob(k, s);
        return;
    }
    momentary_take(k);
    if (ui.home) {                                      /* Stage: the selected track's engine's four, as its pages; they
                                                         * drop in */
        stage_knob_touch();
        page_over = stage_page();
        edit_param(k, s);
        page_over = 0;
    } else {
        edit_param(k, s);
        pick_touch(k);                                  /* a name from a long list: its picker (ui_popup.c) */
    }
}

static int bulk_context(void)
{
    return !ui.home&&!ui.menu&&!ui.confirm&&!ui.layer&&!pop.on&&!name_on()&&!sx_import.count&&
        ((slot_kind()&&slot_kind()!=SK_PROJECT)||(cur_page()->graph==GR_ROLL&&!grid_on()));
}
static int bulk_jog(int32_t delta)
{
    if(!bulk_context()||!(fm1_in.buttons&(1u<<panel.btn[B_OCTUP])))return 0;
    uint32_t slots=slot_kind(),cur=slots?slot_cur(slots):ui.cursor;
    if(!bulk.on||bulk.on==2u){bulk.anchor=(uint16_t)cur;bulk.engine=(uint8_t)library_engine();bulk.track=song.sel;bulk.pattern=TSEL->pattern;bulk.generation=TSEL->pattern_gen;}
    bulk.on=1;uint32_t limit=slots?user_limit():step_pattern_len(TSEL);
    uint32_t to=(uint32_t)clamp((int32_t)cur+delta,0,(int32_t)limit-1);
    if(slots==SK_FM6)fm6_bslot=(uint8_t)to;else if(slots==SK_P5)p5_store_slot=(uint16_t)(to+1u);else if(slots)ui.uslot=(uint8_t)to;else cursor_set((int32_t)to);
    bulk.end=(uint16_t)to;ui.bulk_hold=0;oct_eat|=2u;ui.oct_t0|=2u;ui.force=1;ui_message(slots?"PRESETS SELECTED":"Moving..");return 1;
}
static void notes_bulk_copy(void){note_copy_arm();}
static const sheet_row_t SHEET_NOTE_BULK[]={{"Move: KNOB 1",0,0,note_move_arm,0},{"Copy: KNOB 1",0,0,notes_bulk_copy,0},{"Quantize",nt_quantize_v,nt_quantize_grid,nt_quantize,0},{"Delete selected",0,0,notes_bulk_delete,0},{"Clear selection",0,0,bulk_clear,0}};
static void notes_bulk_sheet_open(void){sheet_open("Selected notes","",SHEET_NOTE_BULK,NELEM(SHEET_NOTE_BULK));}

/* Confirmed writes are infrequent; keep their dispatch out of every input frame. */
static void ui_confirm_accept(void)
{
    uint32_t kind = ui.confirm;
    ui.confirm = 0;
    ui.force = 1;
    if(kind==CF_OLD_SETTINGS){settings_old_accept();settings_save();
    } else if(kind==CF_OLD_TEMPLATE){settings_old_accept();template_save();
    } else if(kind==CF_IMPORT){preset_old_allow(1);sysex_commit();preset_old_allow(0);
    } else if(kind==CF_BULK_ERASE){bulk_erase();
    } else if (kind == CF_OVR_PROJ) {                  /* overwrite: the NAME screen writes it */
        name_open(NK_PROJ_SAVE, ui.confirm_trk & 3u);
    } else if (kind == CF_OVR_USER) {
        name_open(NK_USER_SAVE, ui.confirm_trk);
    } else if (kind == CF_ERASE_USER) {
        user_ui_named(1u, ui.confirm_trk, 0);
    } else if (kind == CF_ERASE_PROJ) {
        project_erase(ui.confirm_trk & 3u);
    } else if (kind == CF_CLEAR_MOTION) {
        track_t *t = &trk[ui.confirm_trk % NTRK];
        if (!chain_busy()) { load_begin(t, UNDO_PAT); motion_clear(t); load_end(t); ui_message("MOTION CLEARED"); }
    } else if (kind == CF_DEL_ROW) {
        uint32_t r = ui.confirm_trk;
        if (!chain_busy() && r < chain_config.count) {
            for (; r + 1u < chain_config.count; r++) {
                chain_config.row[r] = chain_config.row[r + 1u];
                memcpy(chain_patterns[r], chain_patterns[r + 1u], NTRK);
            }
            chain_config.count--;
            if (ui.song_row > chain_config.count) ui.song_row = chain_config.count;
            ui_message("ROW DELETED");
        }
    } else if (kind == CF_TAKE_JAM) {
        jam_take();
    } else if (kind == CF_NEW_SONG) {
        new_open();
    } else if (kind == CF_CLEAR_SONG) {
        if (!chain_busy()) { chain_defaults(&chain_config); memset(chain_patterns, 0, sizeof chain_patterns); ui.song_row = 0; ui_message("SONG CLEARED"); }
    } else if (kind == CF_PASTE_PAT) {          /* PATTERNS' Copy to over a pattern in use */
        if (ui.ptc_on && ui.ptc_trk == ui.confirm_trk)
            ptc_paste(1);
    } else if (kind == CF_DISCARD_DESIGN) {    /* the sound designer's original back */
        if (designer_on() && designer_quit) { designer_quit(1); ui_message("SOUND RESTORED"); }
    } else if (kind == CF_INIT_SOUND) {
        if (!chain_busy()) { sound_init_of(TSEL, TSEL->eng_req); ui_message("SOUND INIT"); }
    } else {
        track_t *t = &trk[ui.confirm_trk % NTRK];
        if(recording_head[recording_owner(t)]<RECORD_MAX){
            if(!note_clear_pattern(t))return;
        }else{
            load_begin(t, UNDO_PAT);
            track_defaults_steps(t);
            load_end(t);
        }
        t->nheld = 0;                           /* and the latched arp chord */
        t->arp_phys = 0;
        if (kind == CF_CLEAR_TRK) {
            char b[12] = "1 CLEARED";
            b[0] = (char)('1' + ui.confirm_trk);
            ui_say("TRACK ", b);
        } else {
            ui_message(kind == CF_DEL_PAT ? "PATTERN DELETED" : "PATTERN CLEARED");
        }
    }
}

#include "ui_designer_input.c"
static void ui_input(void)
{
    uint32_t pressed = fm1_input_edges(0), notes = fm1_input_note_edges(), now = fm1_ticks(), id, b, k;
    if(ui.import_refresh){pop.on=0;brw.on=0;ui.import_refresh=0;ui.force=1;}
    if (scr_input(pressed, notes)) return;
    if (designer_on() && !name_on() && !ui.confirm && designer_input(pressed, notes)) return;
    if (notes)
        browse_commit();                              /* a key played while browsing: hear the sound shown */
    browse_poll();
    layer_lock_input(pressed);
    if(momentary.active && (pressed&(1u<<panel.btn[B_OCTUP]))){
        momentary.active=momentary.count=momentary.native=0;pressed&=~(1u<<panel.btn[B_OCTUP]);ui_message("KEPT");
    }
    uint32_t bank_notes = notes;
    uint32_t home = btn_hold(&ui.home_t0, B_HOME, now, 1);
    uint32_t rec = btn_hold(&ui.rec_t0, B_REC, now, !ui.menu && !ui.confirm && !name_on());   /* held: MIXER */
    uint32_t seq = btn_hold(&ui.seq_t0, B_SEQ, now, !ui.menu && !ui.confirm);
    uint32_t save = btn_hold(&ui.save_t0, B_SAVE, now, !ui.menu && !ui.confirm);   /* held: UNDO (ui.c undo_swap) */
    uint32_t octup = btn_hold(&ui.oct_t0, B_OCTUP, now, page_sheet() != 0u);   /* held: the page's sheet */
    uint32_t oct;
    if (octup == BT_HOLD) {
        oct_eat |= 2u;                                  /* (its release is no OCT+) */
        oct_deferred = 0;
        if(bulk_context())ui.bulk_hold=1;else page_sheet_open();
    } else if (octup == BT_TAP && oct_deferred) {
        song.octave += song.octave < 3 ? 1 : 0;         /* (OCT+ pressed on a page with a sheet: on release) */
    }
    if (octup != BT_NONE || !((fm1_in.buttons >> panel.btn[B_OCTUP]) & 1u))
        oct_deferred = 0;
    if(ui.bulk_hold && !(fm1_in.buttons&(1u<<panel.btn[B_OCTUP]))){
        if(bulk_context()){if(bulk.on&&!slot_kind())notes_bulk_sheet_open();else if(!bulk.on)page_sheet_open();}ui.bulk_hold=0;
    }
    oct = oct_taps(pressed, oct_nav());
    uint32_t lay, knob_layer, combo = 0, lytap, lkeys;
    int32_t s, ks[4] = {0, 0, 0, 0};
    seq_erase_update(pressed);
    if (step_modifier_context()) {
        ui.step_mods |= (uint16_t)(pressed & step_modifier_mask());
        uint32_t ed = 1u << panel.btn[B_EDIT];
        if ((ui.step_mods & ed) && ((pressed & ~ed) || notes || step_midi_r != step_midi_w)) step_edit_combo();
    } else {
        ui.step_mods = ui.step_used = ui.step_oct_used = ui.step_move = 0;
    }
    oct = layer_oct(pressed, oct);                      /* (a SET layer's OCT-: put back) */
    layer_arm(pressed, now);
    layer_masks();                                      /* seq.c: keys pressed with a layer's button are its own */
    lay = layer_held();
    /* The armed layer owns the release frame too. Read each knob once: the scan ISR can publish another
     * detent after a take, which must wait for the next frame rather than edit the underlying page. */
    knob_layer = ui.ly && layer_allowed();
    if (knob_layer)
        for (k = 0; k < 4u; k++)
            ks[k] = panel_enc(EN_K1 + k);
    {   /* a key pressed with the button: a combo (its edge, or the ISR already took it); then not the grid's or a step's */
        static uint32_t kb_seen;
        lkeys = kb_layer & ~kb_seen;
        combo = lay && (notes || lkeys);
        kb_seen = kb_layer;
    }
    if (lay)
        lkeys |= notes & ~fm1_in.notes;                 /* (tapped and let go already) */
    notes &= ~kb_layer;
    if (knob_layer)
        for (k = 0; k < 4u; k++)
            if (ks[k]) combo = 1;
    if (lay) {                                          /* a layer's button held: keys, knobs and buttons are combos */
        combo |= (pressed & ~ly_bit(ui.ly)) != 0u;
        notes = 0;                                      /* (the keys are the layer's, not the grid's or a step's) */
        if (pressed & (1u << panel.btn[B_SAVE]))       /* no UNDO, no page */
            ui.save_t0 |= 2u;
        if (pressed & (1u << panel.btn[B_HOME]))       /* no menu, no HOME */
            ui.home_t0 |= 2u;
        if (pressed & (1u << panel.btn[B_SEQ]))
            ui.seq_t0 |= 2u;
        s=panel_enc(EN_PRESET);
        panel_enc(EN_ALGO);                             /* (another track: OCT- puts back the layer's track only) */
    }
    lytap = layer_gesture(now, combo);
    layer_masks();
    layer_show();
    layer_keys(lkeys);
    for (k = 0; k < 4u; k++)
        if (lay && ks[k]) {
            layer_knob(k, ks[k]);
            ui.hot_col = (uint8_t)k;
            ui.hot_t = 40;
        }
    song.grid = (uint8_t)keys_mode();                 /* (the menu, a dialog: the keys play again; NAME: silent) */
    if (!step_page() || drum_track(TSEL) || ui.menu || ui.confirm || name_on() || lay || live_rec_sel() || chain_busy())
        seq_midi_events(0);                         /* discard events during modal/layer input too */
    if (ui.seen_pattern_gen != TSEL->pattern_gen) {
        if (ui.seen_pattern_gen) { seq_midi_reset(); ui.entry_open = ui.step_move = ui.note_move = ui.note_copy = 0; cursor_set(0); ui.force = 1; }
        ui.seen_pattern_gen = TSEL->pattern_gen;
    }
    if(bulk.on && (ui.home || slot_kind()==SK_PROJECT || bulk.track!=song.sel || bulk.pattern!=TSEL->pattern || bulk.engine!=library_engine() ||
       (!slot_kind() && (cur_page()->graph!=GR_ROLL || (bulk.on==1u?bulk.generation!=TSEL->pattern_gen:bulk.generation!=recording_generation)))))bulk_clear();
    step_history_sync();                                /* (seq_undo.c: a change from elsewhere: a fresh history) */
    if (home == BT_HOLD) {                              /* HOME held: open the menu, or leave it */
        if (ui.menu) {
        momentary_restore();
            menu_close();
        } else {
            ui.menu = 1;
            ui.menu_sel = 0;
            ui.confirm = 0;                             /* (a clear dialog is cancelled, NAME too) */
            name_close();
            new_close();
            ui.force = 1;
            song.seq_mode = 0;
        }
    }
    if (ui.menu || ui.confirm || name_on()) {
        /* REC does nothing in the menu, a dialog or NAME (no transport start there) */
    } else if (chain_busy() && rec == BT_TAP) {
        ui_message("STOP TO RECORD");
    } else if (rec == BT_TAP) {
        rec_tap();
    } else if (rec == BT_HOLD) {
        if (!ui.home && cur_page()->graph == GR_SONG)    /* SONG: TAKE JAM (the jam's rows as the song) */
            ps_jam();
        else
            capture_take();
    }
    if (ui.menu) {
        momentary_restore();                                      /* HOME / SAVE / REC taps do nothing here */
        if (ui.save_t0)
            ui.save_t0 |= 2u;
        ui.pg_down = 0;
        if (!ui.home_t0)
            menu_input(oct);
        return;
    }
    if (name_on() && !ui.confirm) {                     /* NAME: the keys type, KNOB 1 / 2, OCT+ / OCT- (ui_name.c); */
        if (ui.save_t0)                                 /* SAVE does nothing */
            ui.save_t0 |= 2u;
        if (((pressed >> panel.btn[B_PLAY]) & 1u) && (song.playing || seq_counting() || chain_busy()))
            transport_req = 2;                          /* PLAY stops a transport started meanwhile (MIDI Start, the
                                                         * editor) so the name can be saved; it never starts one */
        ui.pg_down = 0;
        name_input(notes, oct);
        return;
    }
    if (new_on() && !ui.confirm) {                      /* NEW SONG: its knobs, OCT+ / OCT-, HOME cancels (ui_new.c) */
        if (ui.save_t0)
            ui.save_t0 |= 2u;
        ui.pg_down = 0;
        new_input(oct, home == BT_TAP);
        return;
    }
    if (save == BT_HOLD && chain_busy())
        ui_message("STOP TO UNDO");
    else if (save == BT_HOLD && !(step_page() && step_history_apply(0)))   /* SEQ > STEP: its last edit first, */
        undo_swap();                                                       /* else the last sound / pattern load */
    else if (save == BT_TAP && !ui.confirm)             /* SAVE acts on release (a hold is the undo) */
        open_family(FAM_SAVE);
    if (ui.confirm) {                                   /* OCT- cancels, OCT+ does it; nothing else reacts */
        if (oct & 2u) {
            ui_confirm_accept();
        } else if (oct & 1u) {
            ui.confirm = 0;
            ui.force = 1;
        }
        ui.pg_down = 0;
        enc_drop();
        return;
    }
    if (smap.on) {                                      /* the map: KNOB 1 / 2, OCT+ / OCT- (ui_sections.c); EDIT */
        int32_t k1 = panel_enc(EN_K1), k2 = panel_enc(EN_K2);   /* closes it too (page_tap) */
        smap_input(k1, k2, oct);
        if (lytap)
            layer_tap(lytap);
        b = ui.pg_down & ~fm1_in.buttons;
        ui.pg_down &= (uint16_t)~b;
        for (id = 0; b; id++, b >>= 1)
            if (b & 1u)
                page_tap(panel_btn_of(id));
        enc_drop();
        return;
    }
    if (pop.on == POP_SHEET || pop.on == POP_LIST) {   /* an action sheet, a value's list: KNOB 1 / 2, OCT+ / OCT-
                                                         * (ui_popup.c) */
        int32_t k1 = panel_enc(EN_K1), k2 = panel_enc(EN_K2);
        if (pop.on == POP_LIST) vlist_input(k2, oct);
        else sheet_input(k1, k2, oct);
        ui.pg_down = 0;
        enc_drop();
        step_history_end();                             /* (a note's sheet: its CHANCE, LENGTH, ... edits undo too) */
        return;
    }
    if (lytap)                                          /* a layer's button acts on release (held: the layer) */
        layer_tap(lytap);
    if (seq == BT_HOLD) {                               /* SEQ held: PATTERNS, the song under it (SONG: SELECT) */
        for (k = 0; k < NPAGES; k++) if (PAGES[k].graph == GR_PATGRID) break;
        ui.home = 0; ui.page = (uint8_t)k; ui.fam_last[FAM_SEQ] = ui.page; page_entered();
    } else if (seq == BT_TAP) {
        open_family(FAM_SEQ);
    }
    if (home == BT_TAP)                                 /* HOME acts on release: a hold opens the menu */
        go_home();
    cursor_fix();                                       /* LEN may have changed (knob, editor, load) */
    if (step_page() && live_rec_sel() && !ui.step_mods) { /* armed: follow recording
                                                         * (not while ENV / SCL are held: a note edit) */
        uint32_t idx = !song.playing || TSEL->seq_pos == 0x7FFFFFFFu ? 0u : TSEL->seq_idx;
        if (ui.cursor != idx)
            cursor_set((int32_t)idx);
    }
    {   /* SAVE + REC: the quick save */
        uint32_t sv = 1u << panel.btn[B_SAVE], rc = 1u << panel.btn[B_REC];
        if (((pressed & rc) && (fm1_in.buttons & sv)) || ((pressed & sv) && (fm1_in.buttons & rc)))
            qsave_chord();
        qsave_poll();
    }
    for (id = 0; id < 14u; id++) {
        if (!((pressed >> id) & 1u))
            continue;
        if ((ui.step_mods >> id) & 1u)
            continue;
        b = panel_btn_of(id);
        switch (b) {
        case B_PLAY:
            if ((fm1_in.buttons >> panel.btn[B_REC]) & 1u) {   /* REC + PLAY: record now */
                rec_play();
                break;
            }
            if (layer_play())                           /* (GLO held: RESTART) */
                break;
            if (song.playing || seq_counting() || chain_busy())
                transport_req = 2;
            else if (!ui.home && cur_page()->graph == GR_SONG)
                chain_play_ui();
            else
                transport_req = 1;
            break;
        case B_SEQ:
        case B_REC:                                     /* tap / hold: above */
        case B_SAVE:
        case B_GLO:
        case B_SCL:
        case B_EDIT:
        case B_HOME:
            break;
        case B_OCTDN:
        case B_OCTUP: {
            uint32_t both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]), bit = b == B_OCTUP ? 2u : 1u;
            if (pop.on == POP_PICK) {                   /* a picker: either closes it (ui_popup.c) */
                pop_close();
                oct_eat |= (uint8_t)bit;
                break;
            }
            if (act_cols() || layer_set_open() || list_on())   /* action, list pages: enter / back (below); SET layers:
                                                                 * OCT- */
                break;
            if (step_page() && ((fm1_in.buttons >> panel.btn[B_SAVE]) & 1u)) {   /* SAVE held: undo further / redo */
                ui.save_t0 |= 2u;                       /* (no page, no other undo when SAVE is let go) */
                ui.oct_t0 |= 2u;                        /* (OCT+: no tap, no sheet) */
                ui.step_oct_used |= (uint8_t)bit;
                oct_eat |= (uint8_t)bit;                /* (its release: no Esc / Enter) */
                if (chain_busy())
                    ui_message("STOP TO UNDO");
                else if (!step_history_apply(b == B_OCTUP))
                    ui_message(b == B_OCTUP ? "NOTHING TO REDO" : "NOTHING TO UNDO");
                break;
            }
            if ((fm1_in.buttons >> panel.btn[B_SAVE]) & 1u) {   /* SAVE held elsewhere: OCT- undoes a load further, */
                ui.save_t0 |= 2u;                       /* OCT+ redoes (no page, no undo when SAVE is let go) */
                ui.oct_t0 |= 2u;
                oct_eat |= (uint8_t)bit;
                if (chain_busy())
                    ui_message("STOP TO UNDO");
                else
                    undo_step(b == B_OCTUP);
                break;
            }
            if (!ui.home && cur_page()->graph == GR_BROWSE) {   /* the browser: OCT- back, OCT+ keep (ui_browser.c) */
                browser_key(b == B_OCTUP);
                oct_eat |= (uint8_t)bit;
                break;
            }
            if (oct_nav())                              /* off Stage: Esc / Enter, on release (below) */
                break;
            if ((fm1_in.buttons & both) == both) {
                song.octave = 0;
                ui.oct_t0 |= 2u;                        /* (OCT+ then neither a tap nor a sheet) */
                oct_deferred = 0;
            } else if (b == B_OCTUP && page_sheet() == 1u) {
                oct_deferred = 1;                       /* Stage: OCT+ on release, held: the sound's sheet */
            } else {
                song.octave += b == B_OCTDN ? (song.octave > -3 ? -1 : 0) : (song.octave < 3 ? 1 : 0);
            }
            break;
        }
        default:                                        /* page buttons (GLO SCL ENV LFO EDIT ARP): when let go */
            if (!lay)                                   /* (with a layer held: swallowed) */
                ui.pg_down |= (uint16_t)(1u << id);
            break;
        }
    }
    if(momentary.active)ui.pg_down&=(uint16_t)~(1u<<panel.btn[B_LFO]);
    b = ui.pg_down & ~fm1_in.buttons;
    ui.pg_down &= (uint16_t)~b;
    for (id = 0; b; id++, b >>= 1)
        if (b & 1u)
            page_tap(panel_btn_of(id));
    if(bulk.on && !slot_kind() && !ui.home && cur_page()->graph==GR_ROLL && (oct&2u)){
        notes_bulk_sheet_open();oct=0;
    } else if((bulk.on||ui.note_move||ui.note_copy)&&(oct&1u)){bulk_clear();oct=0;}
    if (list_on() && (oct & 2u)) {                      /* a list page: OCT+ the row's list, OCT- Stage */
        list_enter();
    } else if (list_on() && (oct & 1u)) {
        go_home();
    } else if (act_cols() && (oct & 2u)) {              /* action pages: OCT+ does the picked action, */
        act_do();
    } else if (act_cols() && (oct & 1u)) {              /* OCT- drops it, or (none picked) goes HOME */
        if (ui.act)
            ui.act = 0;
        else
            go_home();
    } else if (oct_nav() && !ui.layer && (oct & 2u)) {  /* any other page: OCT+ Enter (a slot's sheet, the page's), */
        if (ptc_on())                                   /* PATTERNS' Copy to: there */
            ptc_paste(0);
        else if (slot_kind())
            slot_enter();
        else if (motion_page())
            motion_enter();
        else if (cur_page()->graph == GR_SCALE_PICKER)  /* SCALES: done, SCL's list */
            scales_back();
        else if (step_enter())
            ;
        else if (page_sheet())
            page_sheet_open();
    } else if (oct_nav() && !ui.layer && (oct & 1u)) {  /* OCT- Esc: SCALES back to SCL's list, else Stage */
        uint32_t p = page_titled("SCL");
        if (str_eq(cur_page()->title, "SCALES") && p < NPAGES) {
            ui.page = (uint8_t)p;
            page_entered();
        } else if (ptc_on()) {                          /* PATTERNS' Copy to: left, the page stays */
            ui.ptc_on = 0;
            ui.force = 1;
        } else if (str_eq(cur_page()->title, "PATTERNS") && patterns_queued()) {   /* PATTERNS: the queue first */
            uint32_t k, f = motion_guard();
            for (k = 0; k < NTRK; k++)
                trk[k].pattern_next = 0xFFu;
            motion_unguard(f);
            ui_message("QUEUE CLEARED");
            ui.force = 1;
        } else {
            go_home();
        }
    }
    song.grid = (uint8_t)keys_mode();                 /* (seq.c: the keys are the grid's) */
#if MELODEE_SLICE
    if (notes && slice_page_on())                       /* SLICES: a key picks the slice it plays */
        slice_keys_pick(notes);
#endif
    pattern_keys(bank_notes);
    if (pattern_keys_on() || ui.pat_key) {
        seq_midi_events(0);
    } else if (song.grid) {
        if (!seq_erase_active(TSEL)) grid_keys(notes);
        else seq_midi_events(0);
    } else if (song.seq_mode && cur_page()->graph == GR_ROLL) {   /* NOTES */
        if (live_rec_sel()) {                           /* armed and playing: the keys and MIDI record live, */
            ui.entry_open = 0;                          /* not into the cursor step too */
            seq_midi_events(0);
        } else if (!chain_busy() && notes_selected(TSEL) >= RECORD_MAX && !(TSEL->step[ui.cursor].flags & SF_RECORDED)) {
            seq_entry(notes);
        } else {
            seq_midi_events(0);
            if (notes)
                ui_message("STOP TO EDIT");
        }
    } else {
        seq_midi_events(0);                             /* (MIDI enters steps on SEQ > STEP only) */
    }

    s = panel_enc(EN_PRESET);
    if (s) step_edit_combo();
    if (s && !ui.home && cur_page()->graph == GR_SCALE_PICKER) {
        scale_picker_step(list_accel(EN_PRESET, s, scale_picker_count(), 0));
    } else if (s && (ui.home || cur_page()->graph == GR_BROWSE)) {
        /* PRESETS browses the selected part's sounds (all engines, then user presets) on HOME and the
         * PRESETS page only (never the steps); elsewhere (TRACKS too, where one records) a stray turn
         * would throw away the sound being edited */
        browse_turn(EN_PRESET, s);                       /* past the factory ones: user presets */
    } else if (s && !ui.home && cur_page()->graph == GR_ROLL && !grid_on()) {
        ui.note_zoom = (uint8_t)clamp((int32_t)ui.note_zoom + s, 0, 4);
        ui.force = 1;
    } else if (s && sec_on()) {                         /* an engine in sections: the next / previous one (FM6's
                                                         * operators are sections: ui_sections.c) */
        sec_turn(s);
    } else if (s && !ui.home && cur_page()->scope == SC_FMOP) {   /* FM6's operator pages: the operator */
        fm6_opsel = (uint8_t)clamp((int32_t)fm6_opsel + (s > 0 ? 1 : -1), 0, 5);
        ui.force = 1;
    }
    if ((s = panel_enc(EN_ALGO)) != 0) {           /* ALGORITHM: the selected track, on every page */
        step_edit_combo();
        track_select((uint32_t)clamp((int32_t)song.sel + (s > 0 ? 1 : -1), 0, NTRK - 1));
    }
    if ((s = panel_enc(EN_SELECT)) != 0) {          /* SELECT: pages; with SEQ / a step key / ENV / SCL: below */
        step_edit_combo();
        if(bulk_jog(s)) {
        } else if (pattern_keys_on()) {
            ui.seq_t0 |= 2u;
            uint32_t from = TSEL->pattern_next < NPAT ? TSEL->pattern_next : TSEL->pattern;
            if (pattern_request(TSEL, (uint32_t)clamp((int32_t)from + s, 0, NPAT - 1u))) ui_message("STOP SONG TO SWITCH");
            ui.force = 1;
        } else if (step_gesture(s)) {                   /* ENV / SCL / a key held: the note's length, its place */
        } else if (!ui.home) {                          /* the section's pages (BPM: SEQ > TEMPO; the notes, the hits:
                                                         * KNOB 1) */
            page_scroll(s);
        }
    }
    for (k = 0; !knob_layer && k < 4u; k++) {
        if ((s = panel_enc(EN_K1 + k)) == 0)
            continue;
        step_edit_combo();
        if(k<2u && bulk_jog(s))continue;
        if (k == 0u && cur_page()->graph == GR_ROLL && step_gesture(s))   /* KNOB 1 too, as SELECT (ENV / SCL / a key
                                                                         * held) */
            continue;
        page_knob(k, s);
    }
    if (step_modifier_context()) {
        uint32_t ed = 1u << panel.btn[B_EDIT], octmask = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
        uint32_t combo_oct = (ui.step_mods & ed) ? fm1_in.buttons & octmask : 0;
        if (combo_oct && ((pressed & octmask) || (pressed & ed))) {   /* EDIT + OCT-: undo, OCT+: redo */
            uint32_t dir = ((combo_oct >> panel.btn[B_OCTDN]) & 1u) | (((combo_oct >> panel.btn[B_OCTUP]) & 1u) << 1);
            ui.step_used |= (uint16_t)ed;
            ui.step_oct_used |= (uint8_t)dir;
            oct_eat |= (uint8_t)dir;                   /* (their release: no Esc / Enter) */
            if (chain_busy()) ui_message("STOP TO UNDO");
            else if (dir == 3u) ui_message("USE ONE OCT BUTTON");
            else {
                step_history_end();                    /* include an edit received in this same frame */
                if (!step_history_apply(dir == 2u)) ui_message(dir == 2u ? "NOTHING TO REDO" : "NOTHING TO UNDO");
            }
        }
        seq_entry_finish();                             /* final SELECT detent precedes key release */
        uint32_t released = ui.step_mods & ~fm1_in.buttons;
        for (k = 0; k < 14u; k++) if ((released >> k) & 1u) {
            uint32_t bit = 1u << k;
            ui.step_mods &= (uint16_t)~bit;
            if (!(ui.step_used & bit)) {
                b = panel_btn_of(k);
                if (b != B_EDIT)
                    open_family(b == B_ENV ? FAM_ENV : FAM_SCL);
                else if (chain_busy())
                    ui_message("STOP TO EDIT");
                else
                    step_delete_edit();                 /* EDIT tapped: the note with its ties */
            }
            ui.step_used &= (uint16_t)~bit;
        }
        ui.step_oct_used &= (uint8_t)(((fm1_in.buttons >> panel.btn[B_OCTDN]) & 1u) |
                                     (((fm1_in.buttons >> panel.btn[B_OCTUP]) & 1u) << 1));
        ui.step_move = (ui.step_mods & ui.step_used) != 0u;
    }

    if(momentary.active && !(fm1_in.buttons&(1u<<panel.btn[B_LFO])))momentary_restore();
    seq_erase_update(0);
    autosave_poll(pressed || bank_notes || fm1_in.buttons || fm1_in.notes || ui.hot_t);
    if (recording_full) { recording_full = 0; ui_message("RECORDING FULL"); }
    step_history_end();                                 /* (seq_undo.c: this frame's STEP edit) */
    ui_notices();
}

/* ---------------------------------------------------- panel setup --- */
/* 30 s without input: give up and keep the old table (a stuck key cannot hang the boot) */
#define SETUP_IDLE_MS 30000u
static void setup_title(void)
{
    lcd_fill(0, 0, 240, 240, T_BG);
    {   /* the title with its icon (the menu row's), centred together; M from y 8 as before */
        const char *t = "HARDWARE CALIBRATION";
        int32_t x = (240 - (16 + 6 + text_w(&AF_M, t))) / 2;
        cv_begin(240, 24, T_BG);
        cv_icon_on(x, 4, 16, ICON_X_DOCTOR, T_THEME, T_BG);
        cv_text(x + 22, 3, &AF_M, t, T_TEXT);
        cv_blit(0, 5);
    }
    draw_text_box(0, 32, 240, &AF_S, "TEACH EACH BUTTON AND KNOB", T_MID, 1);
    lcd_fill(16, 56, 208, 1, T_LINE);
}
static void setup_show(const char *what, const char *name)     /* "PRESS" / "TURN RIGHT", the control */
{
    draw_text_box(0, 80, 240, &AF_S, what, T_MID, 1);
    draw_text_box(0, 100, 240, &AF_L, name, T_THEME, 1);
}
static void panel_setup(void)
{
    uint32_t i, used = 0, t0 = fm1_ms;
    const panel_t old = panel;
    setup_title();
    while (fm1_in.buttons) {                             /* wait for OCT-/OCT+ release */
        fm1_wdt_feed();
        if (fm1_ms - t0 > SETUP_IDLE_MS)
            goto timeout;
    }
    fm1_input_edges(0);
    for (i = 0; i < NB; i++) {
        uint32_t p = 0, id;
        setup_show("PRESS", B_NAME[i]);
        t0 = fm1_ms;
        while (!(p & ~used)) {
            fm1_wdt_feed();
            p |= fm1_input_edges(0);
            if (fm1_ms - t0 > SETUP_IDLE_MS)
                goto timeout;
        }
        for (id = 0; id < 14u; id++)
            if (((p & ~used) >> id) & 1u)
                break;
        panel.btn[i] = (uint8_t)id;
        used |= 1u << id;
    }
    used = 0;
    for (i = 0; i < NE; i++) {
        uint32_t e;
        int32_t st = 0;
        setup_show("TURN RIGHT", E_NAME[i]);
        for (e = 0; e < 7u; e++)
            fm1_enc_take(e);
        t0 = fm1_ms;
        for (;;) {
            fm1_wdt_feed();
            if (fm1_ms - t0 > SETUP_IDLE_MS)
                goto timeout;
            for (e = 0; e < 7u; e++)
                if (!((used >> e) & 1u) && (st = fm1_enc_take(e)) != 0)
                    break;
            if (e < 7u)
                break;
        }
        panel.enc[i] = (uint8_t)e;
        panel.dir[i] = (int8_t)(st > 0 ? 1 : -1);
        used |= 1u << e;
        fm1_delay_ms(300);
        fm1_enc_take(e);
    }
    panel.magic = PANEL_MAGIC;
    lcd_fill(0, 0, 240, 240, T_BG);
    ui.force = 1;
    return;
timeout:
    panel = old;
    lcd_fill(0, 0, 240, 240, T_BG);
    ui.force = 1;
    ui_message("SETUP CANCELLED");
}
