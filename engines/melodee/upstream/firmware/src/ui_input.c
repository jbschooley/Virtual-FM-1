/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Melodee UI input: LEDs, knobs and buttons, SEQ step entry, panel setup. */
#include "ui_name.c"                                    /* NAME: naming user presets and projects */
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

static const uint8_t FAM_BTN[FAM_COUNT] = {B_HOME, B_ENV, B_LFO, B_FX, B_SCL, B_EDIT, B_GLO, B_SAVE,
                                           B_ARP, B_SEQ, B_GLO};   /* GLO: mixer + global settings; REC is transport */

static uint32_t cur_fam(void) { return ui.home ? FAM_HOME : cur_page()->fam; }

static int layer_set_open(void);                       /* (ui_layer.c) */
/* the OCT LEDs, bit 0 OCT-, bit 1 OCT+. In the dialogs, the menu and on action pages OCT- (back) is
 * lit and OCT+ blinks while it would do something; elsewhere they show the octave shift */
static uint32_t oct_leds(void)
{
    uint32_t blink = ((fm1_ms / 250u) & 1u) == 0u;
    if (name_on() && !ui.confirm && !ui.menu)           /* NAME: OCT- cancels, OCT+ (blinking) writes */
        return 1u | (blink ? 2u : 0u);
    if (layer_set_open())                               /* a SET layer: OCT- puts back (UNDO), OCT+ nothing */
        return 1u;
    if (ui.confirm || ui.menu || act_cols())
        return 1u | (blink && (ui.confirm || (ui.menu ? ui.menu == 1u : act_ready())) ? 2u : 0u);
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
static uint32_t pattern_leds(void)
{
    uint32_t m = 1u << PAT_KEY[TSEL->pattern];
    if (TSEL->pattern_next < NPAT && !(fm1_ms / 180u & 1u)) m |= 1u << PAT_KEY[TSEL->pattern_next];
    if (ui.pat_key) m |= 1u << PAT_KEY[ui.pat_key - 1u];
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

static const uint8_t LIGHTS_MASK[LIGHTS_N] = {0, 7, 3, 1, 0};   /* the dim keys' frames: -, 1/8, 1/4, 1/2, all */
#define BTN_DIM_MASK 3u                                         /* idle buttons: 1/4 of the frames */

static void ui_leds(void)
{
    uint8_t nl[FM1_NCOL] = {0}, nd[FM1_NCOL] = {0}, nb[FM1_NCOL] = {0};
    uint32_t k, c, play, lights = settings_lights % LIGHTS_N;
    uint32_t fam = cur_fam();
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
    led_put(nl, panel.btn[B_PLAY], song.playing != 0u); /* steady transport state, independent of audio block rate */
    led_put(nl, panel.btn[B_REC], song.rec != 0u);
    k = oct_leds();
    led_put(nl, panel.btn[B_OCTDN], (int)(k & 1u));
    led_put(nl, panel.btn[B_OCTUP], (int)(k >> 1));
    play = !pattern_keys_on() && !(name_on() && !ui.menu) && !ui.layer && !grid_on();
    c = pattern_keys_on() ? pattern_leds() : name_on() && !ui.menu ? name_leds() : ui.layer ? layer_leds() : grid_on() ? grid_leds() :
        fm1_in.notes & ~kb_layer;                       /* NAME's keys, the map, the grid, the keys held */
#if MELODEE_SLICE
    if (!ui.layer && !ui.menu && !name_on() && slice_page_on())
        c |= slice_leds();                              /* SLICES: and the keys of the selected slice */
#endif
    for (k = 0; k < 27u; k++) {
        uint32_t lv = play ? play_key_led(TSEL, k) : KL_OFF;   /* playing: the layout, MIDI's notes too (LIGHTS) */
        led_put(nl, 14u + k, (int)(((c >> k) & 1u) || lv == KL_ON));
        led_put(nd, 14u + k, lv == KL_DIM && lights != LIGHTS_OFF);
    }
    for (k = 0; lights != LIGHTS_OFF && k < NB; k++)   /* the buttons glow when idle */
        led_put(nb, panel.btn[k], 1);
    fm1_led_dim_mask[0] = LIGHTS_MASK[lights];
    fm1_led_dim_mask[1] = BTN_DIM_MASK;
    for (c = 0; c < FM1_NCOL; c++) {
        fm1_led_dim[0][c] = (uint8_t)(nd[c] | nl[c]);   /* first: a key going dim <-> bright never goes dark */
        fm1_led_dim[1][c] = nb[c];
        fm1_led[c] = nl[c];
    }
}

/* ---------------------------------------------------------- input --- */
static int32_t accel(uint32_t role, int32_t s, int32_t range)
{
    /* Predictable hardware response: each decoded detent is one value step.
     * Fast turns retain their full signed detent count without time acceleration. */
    (void)role; (void)range;
    return s;
}

/* MIXER page: KNOB 1 LEVEL, 2 PAN, 3 REV send, 4 MUTE of the selected track (right = ON, left = OFF: the
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
        vp = &t->p[P_REV];
        d = &TP[P_REV];
        break;
    default:
        vp = &t->p[P_PAN];
        d = &TP[P_PAN];
        break;
    }
    *vp = (int16_t)clamp(*vp + accel(EN_K1 + slot, steps, d->max - d->min), d->min, d->max);
    motion_capture(t, (uint32_t)(vp - t->p), *vp);
}

/* REC tap on every page: arm / disarm live recording on the selected
 * track without navigating; arming while stopped starts the transport too. On STEP, keys then record live (at the
 * play head) instead of writing the cursor step */
static void rec_tap(void)
{
    uint8_t bit = (uint8_t)(1u << song.sel);
    if (!ui.home && cur_page()->graph == GR_SONG && !(song.rec & bit)) {
        ui_message("[SEQ] TO RECORD");
        return;
    }
    song.rec ^= bit;
    ui.force = 1;                                     /* also refresh the status on MENU / ABOUT */
    if ((song.rec & bit) && !song.playing)
        transport_req = 1;
    if ((song.rec & bit) && grid_on())
        ui_message("LANE KEYS RECORD");               /* (the white keys stay the steps) */
    else if ((song.rec & bit) && !ui.home && cur_page()->graph == GR_ROLL)
        ui_message("KEYS RECORD LIVE");               /* (seq_entry pauses while armed and playing) */
}

/* REC + PLAY (PLAY pressed while REC is held): record on the selected track at once, armed and playing; REC's
 * release then does nothing (no tap, no hold) */
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
    song.rec |= bit;
    if (!song.playing)
        transport_req = 1;
    ui.force = 1;
    ui_message("RECORDING");
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

/* REC held: the MIXER (FAM_TRK), the tracks' arming and mutes at a glance */
static void rec_hold_mixer(void)
{
    ui.page = (uint8_t)page_first(FAM_TRK);
    ui.home = 0;
    page_entered();
}

/* live recording into the selected track now: the STEP page's key entry pauses meanwhile */
static int live_rec_sel(void) { return ((song.rec >> song.sel) & 1u) && (song.playing || transport_req == 1u); }

/* the OCT- / OCT+ dialog (ui_draw.c draws it); trk: the track or the slot it is about */
static void confirm_open(uint32_t kind, uint32_t trk)
{
    ui.confirm = (uint8_t)kind;
    ui.confirm_trk = (uint8_t)trk;
    ui.act = 0;
    ui.force = 1;
}

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
            if (black_held(GK_ACC))
                grid_acc(TSEL, i, ui.lane, 2);
            else
                grid_hit(TSEL, i, ui.lane, 2);
            cursor_set((int32_t)i);
        } else if (p < NLANE) {
            ui.lane = (uint8_t)p;
        } else if (p != GK_ACC) {
            page_go(p == GK_PGUP ? 1 : -1);
        }
    }
}

static void step_edit(uint32_t slot, int32_t steps)
{
    if (cur_page()->graph == GR_NOTES) {
        if (slot == 0u) cursor_set(ui.cursor + steps);
        else if (slot == 1u) notes_cycle(steps);
        else if (slot == 3u) ui.note_zoom = (uint8_t)clamp((int32_t)ui.note_zoom + steps, 0, 4);
        ui.force = 1;
        return;
    }
    step_t *st = &TSEL->step[ui.cursor];
    uint32_t i;
    if (drum_track(TSEL)) {
        grid_edit(slot, steps);
        return;
    }
    switch (slot) {
    case 0:                                               /* STEP cursor; SELECT shapes a held note */
        if (!ui.entry_open)
            cursor_set(ui.cursor + steps);
        break;
    case 1:                                               /* NOTE: transpose the step */
        st->flags &= (uint8_t)~SF_RECORDED;
        if (!st->n) {
            st->note[0] = last_note;
            st->n = 1;
            st->time = ST_NOTE;
            break;
        }
        for (i = 0; i < st->n; i++)
            st->note[i] = (uint8_t)clamp(st->note[i] + steps, 1, 127);
        st->time = ST_NOTE;
        last_note = st->note[0];
        break;
    case 2:                                               /* TIME: NOTE / TIE / REST (length: PRESETS) */
        st->flags &= (uint8_t)~SF_RECORDED;
        st->time = (uint8_t)clamp((int32_t)st->time + (steps > 0 ? 1 : -1), ST_NOTE, ST_REST);
        if (!st->hit && st->time == ST_REST) st->acc = 0;
        break;
    default: {                                            /* FLAG: - / ACC / SLD / A+S */
        st->flags &= (uint8_t)~SF_RECORDED;
        uint32_t f = (st->flags & SF_ACCENT ? 1u : 0u) | (st->flags & SF_SLIDE ? 2u : 0u);
        f = (uint32_t)clamp((int32_t)f + (steps > 0 ? 1 : -1), 0, 3);
        st->flags = (uint8_t)((st->flags & ~(SF_ACCENT | SF_SLIDE)) | (f & 1u ? SF_ACCENT : 0u) | (f & 2u ? SF_SLIDE : 0u));
        break;
    }
    }
}

static void edit_param(uint32_t slot, int32_t steps)
{
    int16_t *vp;
    const page_t *pg = cur_page();
    const param_desc_t *d;
    int32_t v;
    if (pg->graph == GR_CHANCE) {
        if (slot == 0u) cursor_set(ui.cursor + steps);
        else if (slot == 1u) {
            if (chain_busy()) { ui_message("STOP TO EDIT"); return; }
            step_t *st = &TSEL->step[ui.cursor];
            step_set_chance(st, (uint32_t)clamp((int32_t)step_chance(st) + steps, 0, 100));
        }
        return;
    }
    if (pg->graph == GR_MOTION) {
        if (chain_busy()) { ui_message("STOP TO EDIT"); return; }
        if (slot == 0u) motion_set_enabled(TSEL, steps > 0);
        else if (slot == 3u) ui.act = steps > 0 ? 4u : 0u;
        return;
    }
    if (pg->graph == GR_SONG) {
        if (slot == 0u) {
            ui.song_row = (uint8_t)clamp((int32_t)ui.song_row + steps, 0,
                chain_config.count < CHAIN_ROWS ? chain_config.count : CHAIN_ROWS - 1u);
            return;
        }
        if (chain_busy()) { ui_message("STOP TO EDIT"); return; }
        if (slot == 3u) return;               /* PLAY is a button; no duplicate row-count knob */
        if (ui.song_row >= chain_config.count) {
            chain_row_t *r = &chain_config.row[ui.song_row];
            r->slot = 0;
            if (ui.song_row) memcpy(chain_patterns[ui.song_row], chain_patterns[ui.song_row - 1u], NTRK);
            else for (uint32_t k = 0; k < NTRK; k++) chain_patterns[ui.song_row][k] = trk[k].pattern;
            r->repeat = 1;
            chain_config.count = ui.song_row + 1u;
            if (slot == 1u) return;
        }
        if (slot == 1u)
            chain_patterns[ui.song_row][song.sel] = (uint8_t)clamp((int32_t)chain_patterns[ui.song_row][song.sel] + steps, 0, NPAT - 1u);
        chain_config.row[ui.song_row].slot = chain_patterns[ui.song_row][0];
        if (slot == 2u)
            chain_config.row[ui.song_row].repeat = (uint8_t)clamp((int32_t)chain_config.row[ui.song_row].repeat + steps, 1, 16);
        return;
    }
    if (chain_busy() && (pg->scope == SC_STEP || pg->graph == GR_STEPS ||
        0)) {
        ui_message("STOP TO EDIT"); return;
    }
    if (pg->scope == SC_STEP) {
        step_edit(slot, steps);
        return;
    }
    if (pg->scope == SC_TRK) {
        tracks_edit(slot, steps);
        return;
    }
    if (pg->graph == GR_SCALE_PICKER) {
        scale_picker_edit(slot, steps);
        return;
    }
    if (scale_settings_page(pg) && slot == 1u) {
        scale_picker_mark(steps > 0);
        return;
    }
    if (pg->graph == GR_BROWSE) {                         /* KNOB 1: one preset, KNOB 2: the next / previous engine */
        if (slot == 0u) {
            preset_step(steps);
        } else if (slot == 1u) {
            select_engine(eng_step(TSEL->eng_req, steps));
        } else if (slot == 2u) {
            preset_mark(steps > 0);
        } else if (slot == 3u && favorites.filter != (uint32_t)(steps > 0)) {
            favorites.filter = steps > 0;
            ui.force = 1;
            settings_save();
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
        if (pg->graph != GR_PATS)                         /* OCT+ does it (act_do) */
            ui.act = steps > 0 ? (uint8_t)(slot + 1u) : ui.act == slot + 1u ? 0u : ui.act;
        return;
    }
    if (pg->graph == GR_USER) {                           /* KNOB 1 the slot */
        if (slot == 0u)
            ui.uslot = (uint8_t)clamp((int32_t)ui.uslot + steps, 0, user_limit() - 1);
        return;
    }
    if (pg->graph == GR_PATS) {                          /* KNOB 1 the pattern */
        if (slot == 0u)
            ui.ppick = (uint8_t)clamp((int32_t)pat_pick() + steps, 0, (int32_t)pat_count() - 1);
        return;
    }
    if (pg->graph == GR_MOD && slot == 0u) {             /* MOD: KNOB 1 the slot, 2..4 its SRC DST AMT */
        mod_ui_slot = (uint8_t)clamp((int32_t)mod_ui_slot + (steps > 0 ? 1 : -1), 0, 3);
        return;
    }
    d = page_desc(pg, slot, &vp);
    if (!d || !vp || d->max == d->min)
        return;
    v = enum_step(d, *vp, clamp(*vp + accel(EN_K1 + slot, steps, d->max - d->min), d->min, d->max));
    *vp = (int16_t)v;
    if (pg->scope == SC_CZ1) {                           /* (a copy of the tone's value: written back there) */
        uint8_t raw[CZ_BYTES];
        uint32_t tr = song.sel % NTRK;
        if (cz_ed_put(tr, pg->id[slot], (uint32_t)v, raw)) {
            cz_compare_take(tr);
            fm1_irq_off();
            memcpy(cz_patch[tr].raw, raw, 128u);
            fm1_irq_on();
        }
        return;
    }
    if (pg->scope == SC_FM6 || pg->scope == SC_FMOP) {   /* (a copy of FM6's value: written back there) */
        fm6_page_put(pg, slot, v);
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
    if (pg->scope != SC_GLOBAL) motion_capture(TSEL, (uint32_t)(vp - TSEL->p), *vp);
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
    if (cur_page()->graph == GR_MOTION) {
        if (chain_busy()) { ui_message("STOP TO EDIT"); return; }
        confirm_open(CF_CLEAR_MOTION, song.sel);
        return;
    }
    if (cur_page()->graph == GR_TOOLS) {
        if (chain_busy()) { ui_message("STOP TO EDIT"); return; }
        if (!act_ready()) {                               /* nothing there to clear or delete */
            ui_message(c == 2u ? "NOTHING TO DELETE" : "NOTHING TO CLEAR");
            return;
        }
        confirm_open(c == 0u ? CF_CLEAR_SEQ : c == 1u ? CF_INIT_SOUND :
                     c == 2u ? CF_DEL_ROW : CF_CLEAR_SONG, c == 2u ? ui.song_row : song.sel);
        return;
    }
    if (cur_page()->graph == GR_SONG) {
        if (song.playing || chain_busy()) transport_req = 2;
        else chain_play_ui();
        return;
    }
    if (cur_page()->graph == GR_PATS) {
        if (chain_busy()) { ui_message("STOP TO EDIT"); return; }
        if (pat_needs_confirm(TSEL))                      /* the user's steps: the dialog */
            confirm_open(CF_LOAD_PAT, song.sel);
        else                                              /* empty, or a pattern loaded and untouched */
            pat_load_ui(TSEL, pat_pick());
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
            else if (native_used(ENGI_FM6,fm6_bslot)) confirm_open(CF_OVR_USER, fm6_bslot);
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
        if (c == 2u && user_used(ui.uslot) && !transport_busy())
            confirm_open(CF_ERASE_USER, ui.uslot);        /* ERASE: the dialog first */
        else if (c != 3u)
            user_ui_named(c - 1u, ui.uslot, 0);
        else if (transport_busy())
            ui_message("STOP TO SAVE");
        else if (user_used(ui.uslot))
            confirm_open(CF_OVR_USER, ui.uslot);
        else
            name_open(NK_USER_SAVE, ui.uslot);
        return;
    }
    id = cur_page()->id[c & 3u];
    if (id != G_LOAD)
        ui.act = 0;
    if (song.g[G_SLOT] == PROJ_TMPL && (id == G_LOAD || id == G_SAVE)) {   /* the template: no name, no dialog */
        if (id == G_LOAD)
            template_load();
        else
            template_save();
        return;
    }
    switch (id) {
    case G_LOAD:
        project_load(k);
        break;
    case G_SAVE:                                          /* the NAME screen writes it */
        if (transport_busy())
            ui_message("STOP TO SAVE");
        else if (project_used(k))
            confirm_open(CF_OVR_PROJ, k);
        else
            name_open(NK_PROJ_SAVE, k);
        break;
    case G_CLRSEQ:
        if (chain_busy()) { ui_message("STOP TO EDIT"); break; }
        confirm_open(CF_CLEAR_SEQ, song.sel);
        break;
    default:                                              /* G_INITSND */
        set_engine(TSEL->eng_req);                        /* engine defaults + its first preset (the steps stay) */
        ui_message("SOUND INIT");
        ui.force = 1;
        break;
    }
}

/* OCT- / OCT+ where they answer (the dialogs, the menu, action pages): on release, and only a press
 * that began there; both down together (UPDATE MODE, main.c) is no tap. Bit 0 OCT-, bit 1 OCT+ */
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
    return tap;
}

/* SEQ step entry, acid style: the keys pressed together (POLY: up to 4 notes, MONO:
 * the last one) become the cursor step; releasing all keys moves on. With CHRD on a key
 * writes what it sounds, as live recording does: POLY its chord, MONO the chord's root */
static int step_page(void) { return !ui.home && cur_page()->scope == SC_STEP; }   /* SEQ > STEP, CHANCE */
/* ENV / SCL / EDIT held on STEP: their note gestures (SELECT resizes, moves; EDIT + OCT-/+ undoes, redoes; EDIT tapped
 * deletes), armed and playing too. EDIT's quick layer: off the synth STEP */
static int step_modifier_context(void)
{
    return step_page() && (!drum_track(TSEL) || cur_page()->graph == GR_NOTES) &&
           !ui.menu && !ui.confirm && !ui.ly && !name_on();
}
/* .. and OCT taps move the cursor, but not while recording live: the keys played need the octave buttons then */
static int step_oct_context(void) { return step_modifier_context() && !live_rec_sel(); }
static uint32_t step_modifier_mask(void)                /* (EDIT: STEP only, not CHANCE) */
{
    if (cur_page()->graph == GR_NOTES) return 1u << panel.btn[B_EDIT];
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
    uint32_t at, n = 0;
    fm1_irq_off();
    at = step_note_start(TSEL, ui.cursor);
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
    uint32_t at, to;
    fm1_irq_off();
    at = step_note_start(TSEL, ui.cursor);
    to = step_note_move(TSEL, at, delta);
    fm1_irq_on();
    if (to < NSTEP) {
        uint8_t entry = ui.entry_open;
        cursor_set((int32_t)to);
        ui.entry_open = entry;
        ui_message(to == at ? "NEXT NOTE" : "NOTE MOVED");
    } else {
        ui_message("SELECT A NOTE");
    }
    ui.force = 1;
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
    if (cur_page()->graph == GR_NOTES) { notes_delete_edit(); return; }
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
        st->n = 0;
        st->flags &= (uint8_t)~SF_RECORDED;
        st->time = ST_NOTE;
    }
    if (t->p[P_VOICE] && !ENGINES[t->engine]->oneshot) {   /* (drums: hits stack as a chord) */
        st->note[0] = n[0];
        st->n = 1;
    } else {
        for (i = 0; i < cnt && st->n < 4u; i++) {
            for (j = 0; j < st->n && st->note[j] != n[i]; j++)
                ;
            if (j == st->n)
                st->note[st->n++] = n[i];
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
        cursor_set(ui.cursor + (n ? n : 1u));
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
    if (b == B_GLO) {
        open_global();                                  /* MIXER -> GLOBAL -> SYSTEM -> MIXER */
        return;
    }
    if (b == B_EDIT && song.seq_mode && !ui.home && cur_page()->graph == GR_ROLL) {   /* the DRUM grid: EDIT clears
                                                         * the step (synth STEP: a step modifier, ui_input) */
        if (chain_busy()) { ui_message("STOP TO EDIT"); return; }
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
    if (motion_full) { motion_full = 0; ui_message("MOTION FULL"); }
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
    } else if (ui.step_mods & env) {
        ui.step_move = 1;
        step_length_edit(s);
    } else if (ui.step_mods & scl) {
        ui.step_move = 1;
        step_move_edit(s);
    } else {
        step_length_edit(s);
    }
    ui.force = 1;
    return 1;
}

static void ui_input(void)
{
    uint32_t pressed = fm1_input_edges(0), notes = fm1_input_note_edges(), now = fm1_ticks(), id, b, k;
    uint32_t bank_notes = notes;
    uint32_t home = btn_hold(&ui.home_t0, B_HOME, now, 1);
    uint32_t rec = btn_hold(&ui.rec_t0, B_REC, now, !ui.menu && !ui.confirm && !name_on());   /* held: MIXER */
    uint32_t seq = btn_hold(&ui.seq_t0, B_SEQ, now, !ui.menu && !ui.confirm);
    uint32_t save = btn_hold(&ui.save_t0, B_SAVE, now, !ui.menu && !ui.confirm);   /* held: UNDO (ui.c undo_swap) */
    uint32_t oct = oct_taps(pressed, ui.menu || ui.confirm || act_cols() || name_on() || layer_set_open() || step_oct_context());
    uint32_t lay, combo = 0, lytap, lkeys;
    int32_t s, ks[4] = {0, 0, 0, 0};
    if (step_modifier_context()) {
        ui.step_mods |= (uint16_t)(pressed & step_modifier_mask());
        uint32_t ed = 1u << panel.btn[B_EDIT];
        if ((ui.step_mods & ed) && ((pressed & ~ed) || notes || step_midi_r != step_midi_w)) step_edit_combo();
    } else {
        ui.step_mods = ui.step_used = ui.step_oct_used = ui.step_move = 0;
    }
    fm6_poll();                                         /* FM6: PTCH turned -> its patch */
#if !MELODEE_FM4
    for (k = 0; k < NTRK; k++)                          /* a DIGITAL sound any other way (the paths convert it */
        if (trk[k].eng_req == ENGI_DIGITAL)             /* already): FM6 (fm4_convert.c) */
            fm4_track(&trk[k]);
#endif
    oct = layer_oct(pressed, oct);                      /* (a SET layer's OCT-: put back) */
    layer_arm(pressed, now);
    layer_masks();                                      /* seq.c: keys pressed with a layer's button are its own */
    lay = layer_held();
    if (!layer_allowed())
        perf_kill = 1;                                  /* (effects off until their keys are let go) */
    else if (!kb_layer)
        perf_kill = 0;
    {   /* a key pressed with the button: a combo (its edge, or the ISR already took it); then not the grid's or a step's */
        static uint32_t kb_seen;
        lkeys = kb_layer & ~kb_seen;
        combo = lay && (notes || lkeys);
        kb_seen = kb_layer;
    }
    if (lay)
        lkeys |= notes & ~fm1_in.notes;                 /* (tapped and let go already) */
    notes &= ~kb_layer;
    if (lay) {                                          /* a layer's button held: keys, knobs and buttons are combos */
        combo |= (pressed & ~ly_bit(ui.ly)) != 0u;
        notes = 0;                                      /* (the keys are the layer's, not the grid's or a step's) */
        if (pressed & (1u << panel.btn[B_SAVE]))       /* no UNDO, no page */
            ui.save_t0 |= 2u;
        if (pressed & (1u << panel.btn[B_HOME]))       /* no menu, no HOME */
            ui.home_t0 |= 2u;
        if (pressed & (1u << panel.btn[B_SEQ]))
            ui.seq_t0 |= 2u;
        for (k = 0; k < 4u; k++)                        /* KNOB 1..4: the layer's (ui_layer.c layer_knob) */
            if ((ks[k] = panel_enc(EN_K1 + k)) != 0)
                combo = 1;
        panel_enc(EN_PRESET);                           /* (a stray turn would load another sound) */
        panel_enc(EN_ALGO);                             /* (another track: OCT- puts back the layer's track only) */
    }
    lytap = layer_gesture(now, combo);
    layer_show();
    layer_keys(lkeys);
    for (k = 0; k < 4u; k++)
        if (ks[k]) {
            layer_knob(k, ks[k]);
            ui.hot_col = (uint8_t)k;
            ui.hot_t = 40;
        }
    song.grid = (uint8_t)keys_mode();                 /* (the menu, a dialog: the keys play again; NAME: silent) */
    if (!step_page() || drum_track(TSEL) || ui.menu || ui.confirm || name_on() || lay || live_rec_sel() || chain_busy())
        seq_midi_events(0);                         /* discard events during modal/layer input too */
    if (ui.seen_pattern_gen != TSEL->pattern_gen) {
        if (ui.seen_pattern_gen) { seq_midi_reset(); ui.entry_open = ui.step_move = 0; cursor_set(0); ui.force = 1; }
        ui.seen_pattern_gen = TSEL->pattern_gen;
    }
    step_history_sync();                                /* (seq_undo.c: a change from elsewhere: a fresh history) */
    if (home == BT_HOLD) {                              /* HOME held: open the menu, or leave it */
        if (ui.menu) {
            menu_close();
        } else {
            ui.menu = 1;
            ui.menu_sel = 0;
            ui.confirm = 0;                             /* (a clear dialog is cancelled, NAME too) */
            name_close();
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
        rec_hold_mixer();
    }
    if (ui.menu) {                                      /* HOME / SAVE / REC taps do nothing here */
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
        if (((pressed >> panel.btn[B_PLAY]) & 1u) && (song.playing || chain_busy()))
            transport_req = 2;                          /* PLAY stops a transport started meanwhile (MIDI Start, the
                                                         * editor) so the name can be saved; it never starts one */
        ui.pg_down = 0;
        name_input(notes, oct);
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
            uint32_t kind = ui.confirm;
            ui.confirm = 0;
            ui.force = 1;
            if (kind == CF_OVR_PROJ) {                  /* overwrite: the NAME screen writes it */
                name_open(NK_PROJ_SAVE, ui.confirm_trk & 3u);
            } else if (kind == CF_OVR_USER) {
                name_open(NK_USER_SAVE, ui.confirm_trk);
            } else if (kind == CF_ERASE_USER) {
                user_ui_named(1u, ui.confirm_trk, 0);
            } else if (kind == CF_LOAD_PAT) {
                pat_load_ui(&trk[ui.confirm_trk % NTRK], pat_pick());
                str_cpy(ui.msg2, "[SAVE] HOLD TO UNDO", sizeof ui.msg2);
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
            } else if (kind == CF_CLEAR_SONG) {
                if (!chain_busy()) { chain_defaults(&chain_config); memset(chain_patterns, 0, sizeof chain_patterns); ui.song_row = 0; ui_message("SONG CLEARED"); }
            } else if (kind == CF_INIT_SOUND) {
                if (!chain_busy()) { set_engine(TSEL->eng_req); ui_message("SOUND INIT"); }
            } else {
                track_t *t = &trk[ui.confirm_trk % NTRK];
                load_begin(t, UNDO_PAT);
                track_defaults_steps(t);
                load_end(t);
                t->nheld = 0;                           /* and the latched arp chord */
                t->arp_phys = 0;
                if (kind == CF_CLEAR_TRK) {
                    char b[12] = "1 CLEARED";
                    b[0] = (char)('1' + ui.confirm_trk);
                    ui_say("TRACK ", b);
                } else {
                    ui_message("PATTERN CLEARED");
                }
            }
        } else if (oct & 1u) {
            ui.confirm = 0;
            ui.force = 1;
        }
        ui.pg_down = 0;
        enc_drop();
        return;
    }
    if (lytap)                                          /* a layer's button acts on release (held: the layer) */
        layer_tap(lytap);
    if (seq == BT_HOLD) {
        for (k = 0; k < NPAGES; k++) if (PAGES[k].graph == GR_SONG) break;
        ui.home = 0; ui.page = (uint8_t)k; page_entered();
    } else if (seq == BT_TAP) {
        open_family(FAM_SEQ);
    }
    if (home == BT_TAP)                                 /* HOME acts on release: a hold opens the menu */
        go_home();
    cursor_fix();                                       /* LEN may have changed (knob, editor, load) */
    if (step_page() && cur_page()->graph != GR_NOTES && live_rec_sel() && !ui.step_mods) { /* armed: follow recording
                                                         * (not while ENV / SCL / FX are held: a note edit) */
        uint32_t idx = TSEL->seq_pos == 0x7FFFFFFFu ? 0u : TSEL->seq_idx;
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
            if (song.playing || chain_busy())
                transport_req = 2;
            else if (!ui.home && cur_page()->graph == GR_SONG)
                chain_play_ui();
            else
                transport_req = 1;
            break;
        case B_SEQ:
        case B_REC:                                     /* tap / hold: above */
        case B_SAVE:
        case B_FX:                                      /* the layers' buttons: ui_layer.c */
        case B_GLO:
        case B_SCL:
        case B_EDIT:
        case B_HOME:
            break;
        case B_OCTDN:
        case B_OCTUP: {
            uint32_t both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
            if (act_cols() || layer_set_open())         /* action pages: enter / back (below); SET layers: OCT- */
                break;
            if (step_page() && ((fm1_in.buttons >> panel.btn[B_SAVE]) & 1u)) {   /* SAVE held: undo further / redo */
                ui.save_t0 |= 2u;                       /* (no page, no other undo when SAVE is let go) */
                ui.step_oct_used |= b == B_OCTUP ? 2u : 1u;
                if (chain_busy())
                    ui_message("STOP TO UNDO");
                else if (!step_history_apply(b == B_OCTUP))
                    ui_message(b == B_OCTUP ? "NOTHING TO REDO" : "NOTHING TO UNDO");
                break;
            }
            if (step_oct_context())                    /* STEP OCT taps: the cursor; EDIT consumes them below */
                break;
            if ((fm1_in.buttons & both) == both)
                song.octave = 0;
            else
                song.octave += b == B_OCTDN ? (song.octave > -3 ? -1 : 0) : (song.octave < 3 ? 1 : 0);
            break;
        }
        default:                                        /* page buttons (GLO SCL ENV LFO EDIT ARP): when let go */
            if (!lay)                                   /* (with FX held: swallowed) */
                ui.pg_down |= (uint16_t)(1u << id);
            break;
        }
    }
    b = ui.pg_down & ~fm1_in.buttons;
    ui.pg_down &= (uint16_t)~b;
    for (id = 0; b; id++, b >>= 1)
        if (b & 1u)
            page_tap(panel_btn_of(id));
    if (act_cols() && (oct & 2u)) {                     /* action pages: OCT+ does the picked action, */
        act_do();
    } else if (act_cols() && (oct & 1u)) {              /* OCT- drops it, or (none picked) goes HOME */
        if (cur_page()->graph != GR_PATS && ui.act)
            ui.act = 0;
        else
            go_home();
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
        grid_keys(notes);
    } else if (song.seq_mode && cur_page()->graph == GR_ROLL) {   /* STEP (not CHANCE: its knobs only) */
        if (live_rec_sel()) {                           /* armed and playing: the keys and MIDI record live, */
            ui.entry_open = 0;                          /* not into the cursor step too */
            seq_midi_events(0);
        } else if (!chain_busy()) {
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
        scale_picker_step(s);
    } else if (s && (ui.home || cur_page()->graph == GR_BROWSE)) {
        /* PRESETS browses the selected part's sounds (all engines, then user presets) on HOME and the
         * PRESETS page only (never the steps); elsewhere (TRACKS too, where one records) a stray turn
         * would throw away the sound being edited */
        preset_step(s);                                  /* past the factory ones: user presets */
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
        if (pattern_keys_on()) {
            ui.seq_t0 |= 2u;
            uint32_t from = TSEL->pattern_next < NPAT ? TSEL->pattern_next : TSEL->pattern;
            if (pattern_request(TSEL, (uint32_t)clamp((int32_t)from + s, 0, NPAT - 1u))) ui_message("STOP SONG TO SWITCH");
            ui.force = 1;
        } else if (step_gesture(s)) {                   /* ENV / SCL / a key held: the note's length, its place */
        } else if (!ui.home) {                          /* the section's pages (BPM: SEQ > TEMPO; the STEP cursor:
                                                         * KNOB 1) */
            page_scroll(s);
        }
    }
    for (k = 0; k < 4u; k++) {
        const page_t *pg = cur_page();
        int16_t *hv;
        if ((s = panel_enc(EN_K1 + k)) == 0)
            continue;
        step_edit_combo();
        if (k == 0u && pg->graph == GR_ROLL && step_gesture(s))   /* KNOB 1 too, as SELECT (ENV / SCL / a key held) */
            continue;
        if (ui.home || pg->scope == SC_STEP || pg->scope == SC_TRK || page_desc(pg, k, &hv) ||
            ((pg->graph == GR_USER || pg->graph == GR_MOD || pg->graph == GR_PATS) && k == 0u)
            || pg->graph == GR_SONG || pg->graph == GR_SCALE_PICKER || (scale_settings_page(pg) && k == 1u)
            || (pg->graph == GR_SLICES && k < 2u)) {   /* (not an empty column) */
            ui.hot_col = (uint8_t)k;
            ui.hot_t = 40;
        }
        if (ui.home) {
            int16_t *vp;
            const param_desc_t *d = home_param(k, &vp);
            *vp = (int16_t)enum_step(d, *vp, clamp(*vp + accel(EN_K1 + k, s, d->max - d->min), d->min, d->max));
            motion_capture(TSEL, (uint32_t)(vp - TSEL->p), *vp);
        } else {
            edit_param(k, s);
        }
    }
    if (step_modifier_context()) {
        uint32_t ed = 1u << panel.btn[B_EDIT], octmask = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
        uint32_t combo_oct = (ui.step_mods & ed) ? fm1_in.buttons & octmask : 0;
        if (combo_oct && ((pressed & octmask) || (pressed & ed))) {   /* EDIT + OCT-: undo, OCT+: redo */
            uint32_t dir = ((combo_oct >> panel.btn[B_OCTDN]) & 1u) | (((combo_oct >> panel.btn[B_OCTUP]) & 1u) << 1);
            ui.step_used |= (uint16_t)ed;
            ui.step_oct_used |= (uint8_t)dir;
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
        if (step_oct_context()) {
            if (oct & ~ui.step_oct_used) cursor_set(ui.cursor + ((oct & 2u) ? 1 : -1));
            ui.step_oct_used &= (uint8_t)(((fm1_in.buttons >> panel.btn[B_OCTDN]) & 1u) |
                                         (((fm1_in.buttons >> panel.btn[B_OCTUP]) & 1u) << 1));
        }
        ui.step_move = (ui.step_mods & ui.step_used) != 0u;
    }
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
