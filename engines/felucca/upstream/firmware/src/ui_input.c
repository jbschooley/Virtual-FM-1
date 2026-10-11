/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca UI input: LEDs, knobs and buttons, SEQ step entry, panel setup. */
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

/* PLAY's second, green LED: not in the key matrix (no key there), found on the hardware at column 8, row PA9 (bit 1) */
#define LED_PLAY_GREEN ((8u << 3) | 1u)
static void led_clear(uint8_t *nl, uint32_t id)
{
    uint8_t q = led_pos[id];
    if (q != 0xFF)
        nl[q >> 3] &= (uint8_t)~(1u << (q & 7u));
}

static const uint8_t FAM_BTN[FAM_COUNT] = {B_HOME, B_ENV, B_LFO, B_FX, B_SCL, B_EDIT, B_GLO, B_SAVE,
                                           B_ARP, B_SEQ, B_HOME};   /* MIXER: HOME's (1.2); REC is transport */

static uint32_t cur_fam(void) { return ui.home ? FAM_HOME : cur_page()->fam; }

static int layer_set_open(void);                       /* (ui_layer.c) */
/* the OCT LEDs, bit 0 OCT- lit, bit 1 OCT+ lit, OCT_BREATH OCT+ breathing (can be pressed: dark .. ~60 %,
 * hal/fm1_input.h fm1_led_breath, #119). In the dialogs and on action pages OCT- (back) is lit and OCT+ breathes
 * while it would do something; the menu: on a value row OCT- and OCT+ both breathe while that way has a value to go to,
 * on CALIBRATION / ABOUT OCT+ breathing alone (opens), in ABOUT OCT- lit (back); elsewhere the octave shift */
#define OCT_BREATH 4u
#define OCT_BREATH_DN 8u                                /* OCT- breathing (the menu: the previous value) */
/* MOMENTARY (1.2, Discussions #72 / #73): LFO held, the knobs' changes are temporary. KNOB 1..4 on HOME and on the
 * sound and FX pages (a value of the selected track or a global sound / FX one, as edit_param sets it; MIXER's LEVEL
 * PAN REV MUTE too, #195; not on the action pages, the lists, STEP, AUTOMATION or SONG, where the knobs pick) change
 * their value as ever, and each value turned is put back as it was when LFO is let go: hold LFO, sweep the filter,
 * let go, it snaps back. OCT+ while
 * LFO is still held KEEPS them (the changes stay, nothing is put back; OCT+ breathes while something would go back;
 * LFO has no LED). LFO's tap (its page) is dropped once any knob turned with it; LFO alone still opens LFO. Temporary
 * turns are not recorded as automation (motion_capture) and take no undo; a sound or project load meanwhile (the
 * PRESETS knob, the editor, a song's section) puts that track's values (and the global ones) back first (mom_back: the
 * undo copy holds the sound as it was, the load's sound stays after). The menu, a dialog, NAME or a layer opening put
 * them back too. Up to MOM_N values at a time ("MOMENTARY FULL" past them: the turn does nothing). LFO held did
 * nothing (it is no layer; OCT- is the octave and a layer's UNDO, so not it), so no gesture changes: LFO + a knob
 * turned opened LFO's page on release before, and edited the page under it for good. The values: ui.c mom */
static int mom_down(void)                              /* LFO held where its turns are momentary */
{
    return ((fm1_in.buttons >> panel.btn[B_LFO]) & 1u) && !ui.menu && !ui.confirm && !name_on() && !ui.layer && !ui.uboot;
}
/* KNOB k turned s with LFO held: 1 = taken (a momentary value, or refused full), 0 = not a momentary value (the turn
 * goes as ever: the column picks, steps, acts) */
static int mom_turn(uint32_t k, int32_t s)
{
    const page_t *pg = cur_page();
    const param_desc_t *d;
    int16_t *vp = 0;
    uint32_t i, g;
    const track_t *t = ui.home ? home_trk(k) : TSEL;     /* (HOME LEVELS: KNOB k is track k's) */
    if (ui.home) {
        d = home_param(k, &vp);
    } else if (pg->scope == SC_TRK && !lock_held()) {  /* MIXER (#195): the selected track's LEVEL PAN REV MUTE */
        static const uint8_t MIX_ID[4] = {P_LEVEL, P_PAN, P_REV, P_MUTE};   /* (tracks_edit's knobs) */
        vp = &TSEL->p[MIX_ID[k & 3u]];
        d = &TP[MIX_ID[k & 3u]];
    } else {
        if (act_cols() || lock_held() || !(pg->scope == SC_TRACK || pg->scope == SC_ENGINE || pg->scope == SC_GLOBAL) ||
            pg->graph == GR_EVENTS || pg->graph == GR_SONG || pg->graph == GR_BROWSE || pg->graph == GR_USER ||
            pg->graph == GR_PATS || (pg->graph == GR_SLICES && k < 2u) || (pg->graph == GR_MOD && k == 0u))
            return 0;
        d = page_desc(pg, k, &vp);
    }
    g = vp && vp >= song.g && vp < song.g + G_COUNT ? (uint32_t)(vp - song.g) : G_COUNT;
    if (!d || !vp || d->max == d->min ||
        !((vp >= t->p && vp < t->p + P_COUNT) || (g <= G_CDEPTH && g != G_CLOCK) || g == G_RTYPE))
        return 0;                                      /* (only the values a sound and the FX are: no CLK, no actions) */
    for (i = 0; i < mom.n && mom.vp[i] != vp; i++)
        ;
    if (i == mom.n) {                                  /* the first turn of this value: what it was */
        if (mom.n >= MOM_N) {
            ui_message("MOMENTARY FULL");
            return 1;
        }
        mom.vp[i] = vp;
        mom.v[i] = *vp;
        mom.n++;
        if (i == 0u)
            ui_message("MOMENTARY  [OCT+] KEEP");
    }
    *vp = (int16_t)param_turn(d, *vp, accel(EN_K1 + k, s, d->fmt == F_ENUM ? 0 : d->max - d->min));
    return 1;
}

static uint32_t oct_leds(void)
{
    if (name_on() && !ui.confirm && !ui.menu)           /* NAME: OCT- cancels, OCT+ (breathing) writes */
        return 1u | OCT_BREATH;
    if (layer_set_open())                               /* a SET layer: OCT- puts back (UNDO), OCT+ nothing */
        return 1u;
    if (ui.menu && !ui.confirm)
        return ui.menu >= 2u ? 1u :                    /* (INFO, ABOUT: OCT- back, lit) */
               ui.menu_sel == MI_LCLEAR ? (ml_count() ? OCT_BREATH : 0u) :   /* LEARN CLEAR: while CCs are learned */
               ui.menu_sel >= MI_VALUES ? OCT_BREATH :  /* CALIBRATION / INFO / ABOUT: OCT+ opens */
               (menu_step(ui.menu_sel, -1) != menu_get(ui.menu_sel) ? OCT_BREATH_DN : 0u) |   /* a value: each */
               (menu_step(ui.menu_sel, 1) != menu_get(ui.menu_sel) ? OCT_BREATH : 0u);         /* way it can go */
    if (ui.confirm || act_cols())
        return 1u | (ui.confirm || act_ready() ? OCT_BREATH : 0u);
    if (mom.n && mom_down())                            /* MOMENTARY: OCT+ keeps the changes */
        return (song.octave < 0 ? 1u : 0u) | OCT_BREATH;
    return (song.octave < 0 ? 1u : 0u) | (song.octave > 0 ? 2u : 0u);
}

/* the key LEDs on the grid, bit k = key k: the white keys show where the selected lane hits on the page
 * shown (its accents while ACC is held), the step playing inverted (a light walks over them); the black keys
 * the lane selected, ACC while held, the page keys while there is more than one page */
static uint32_t grid_leds(void)
{
    const track_t *t = TSEL;
    uint32_t k, m = 0, len = (uint32_t)t->p[P_SLEN], b = detail_on() ? 0xFFu : 1u << ui.lane;   /* (DETAIL: any hit) */
    uint32_t acc = !detail_on() && black_held(GK_ACC);
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

/* the DRUM grid's beats, bit k = key k (ui_leds: a steady brighter glow than the other steps', hal/fm1_input.h
 * fm1_led_mid): the first step of each group of four on the page shown, steps 1 5 9 13 of 16 ("5111 5111 5111 5111"),
 * within LEN, where grid_leds shows no hit (the lane's, its accents while ACC is held) and not the playhead. Always
 * groups of four from the pattern's first step, whatever LEN or DIV (a 3/4 or a DIV 1/8 pattern too): the steps are
 * counted, not the time */
static uint32_t grid_beats(void)
{
    const track_t *t = TSEL;
    uint32_t k, m = 0, len = (uint32_t)t->p[P_SLEN], b = detail_on() ? 0xFFu : 1u << ui.lane;
    uint32_t acc = !detail_on() && black_held(GK_ACC);
    uint32_t ph = song.playing && t->seq_idx < len && t->seq_idx / 16u == ui.bank ? t->seq_idx % 16u : 0xFFu;
    for (k = 0; k < 27u; k++) {
        uint32_t p = key_place(k), i = ui.bank * 16u + p;
        if (key_black(k) || p % 4u || i >= len || p == ph)
            continue;
        m |= (uint32_t)(((acc ? step_accents(&seq_steps(t)[i]) : step_lanes(&seq_steps(t)[i])) & b) == 0u) << k;
    }
    return m;
}

/* the DRUM grid's keys that do something, bit k = key k (they glow with the idle LEDs, ui_leds): the page's steps
 * within LEN, the lane keys, ACC, the page keys while there is more than one page */
static uint32_t grid_glow(void)
{
    uint32_t k, m = 0, len = (uint32_t)TSEL->p[P_SLEN];
    for (k = 0; k < 27u; k++) {
        uint32_t p = key_place(k);
        m |= (uint32_t)(!key_black(k) ? ui.bank * 16u + p < len : p < NLANE || p == GK_ACC || len > 16u) << k;
    }
    return m;
}

/* an ARP playing on any part flashes the ARP button on the beat, the bar's first beat longer: 1 lit, 0 dark,
 * 2 no ARP playing */
static uint32_t arp_led(void)
{
    uint32_t k, on = 0, b = beat_samples();
    for (k = 0; k < NPART; k++)
        on |= trk[k].p[P_AMODE] && trk[k].nheld;
    return !on ? 2u : beat_pos < (beat_n ? b / 6u : b / 2u);
}

/* Discussion #81: the keys of the notes MIDI IN (USB and TRS, routed by ROUT) holds on track t, bit k = key k: as
 * play_leds, where the keys play that note at the octave now, the lowest key that gives it; a note no key plays is
 * not shown. Read from midi_control.c's own state (midi_note_held: the notes each channel holds for the track, the
 * pedal's too, and the tones of MIDI chords): no state here; nothing to do while MIDI holds none of the track's */
static uint32_t midi_leds(const track_t *t)
{
    uint32_t k, note, m = 0, seen[4] = {0, 0, 0, 0};
    if (!midi_owners[trk_index(t)])
        return 0;
    for (k = 0; k < 27u; k++) {
        note = kb_map(t, k);
        if (note > 127u || ((seen[note >> 5] >> (note & 31u)) & 1u))
            continue;                                   /* (silent, or a lower key gives it) */
        seen[note >> 5] |= 1u << (note & 31u);
        m |= (uint32_t)midi_note_held(t, note) << k;
    }
    return m;
}

/* the keys of the notes the selected track's sequencer and ARP sound now (#38), bit k = key k: where the keys
 * play that note (kb_map: the octave, TRN, QNT, an engine's own map), the lowest key that gives it (QNT SNAP
 * rounds the keys above down onto it); a note no key plays is not shown. A snapshot of the ISR's seq_notes /
 * arp_note (arp_ch): no state of its own, nothing to do while nothing sounds */
static uint32_t play_leds(void)
{
    const track_t *t = TSEL;
    uint8_t s[4 + NLANE + 4];
    uint32_t n = t->seq_n < 4u + NLANE ? t->seq_n : 4u + NLANE, i, k, note, used = 0, m = 0, hit;
    for (i = 0; i < n; i++)
        s[i] = t->seq_notes[i];
    if (t->arp_note)
        s[n++] = t->arp_note;
    for (i = 0; i < 3u; i++)                            /* (the ARP's CHORD) */
        if (t->arp_ch[i])
            s[n++] = t->arp_ch[i];
    for (k = 0; n && k < 27u; k++) {
        note = kb_map(t, k);
        for (i = 0, hit = 0; i < n; i++)
            if (s[i] == note && !((used >> i) & 1u)) {
                used |= 1u << i;
                hit = 1;
            }
        m |= hit << k;
    }
    return m | midi_leds(t);
}

/* Discussion #89: on SEQ > STEP (the piano roll; the DRUM grid has its own map) while stopped, the keys of the
 * notes stored in the cursor step, bit k = key k: as play_leds, where the keys play that note now (kb_map: the
 * octave, TRN, QNT), the lowest key that gives it; a note no key plays is not shown, nor a REST step's. Playing,
 * the keys show what plays (play_leds) instead. Read from the step: no state of its own */
static uint32_t step_leds(void)
{
    const track_t *t = TSEL;
    const step_t *st;
    uint32_t i, k, note, used = 0, m = 0;
    if (ui.home || ui.menu || ui.confirm || !song.seq_mode || cur_page()->graph != GR_ROLL || song.playing ||
        chain_busy() || ui.cursor >= NSTEP)
        return 0;
    st = &t->step[ui.cursor];
    if (!st->n || st->time == ST_REST)
        return 0;
    for (k = 0; k < 27u; k++) {
        note = kb_map(t, k);
        for (i = 0; i < st->n && i < 4u; i++)
            if (st->note[i] == note && !((used >> i) & 1u)) {
                used |= 1u << i;
                m |= 1u << k;
            }
    }
    return m;
}

/* 1: the keys show a map of their own (NAME, a layer's map: SCL's scale, FX; the DRUM grid, SLICES), lit or
 * dark; 0: the keys held and the notes playing, over the idle glow */
static int keys_own(void)
{
    return (name_on() && !ui.menu) || ui.layer || grid_on()
#if FELUCCA_SLICE
           || (!ui.menu && !name_on() && slice_page_on())
#endif
        ;
}

/* Discussion #127 (1.2): MENU > SCALE LEDS ON, the keys of the selected track's scale (ROOT, SCALE), bit k = key k, as
 * the keys play now: the note a key gives with TRN (QNT SNAP / SEQ: the key's own note; the others round down onto
 * the scale); QNT WHITE every white key (they walk the scale; the black ones are silent). *root: the keys of the
 * scale's root. None where the engine maps the keys itself (DRUM's GM map, the slices): 0 there, and OFF */
static uint32_t scale_leds(uint32_t *root)
{
    const track_t *t = TSEL;
    const engine_t *e = ENGINES[eng_idx(t->eng_req)];
    uint32_t k, m = 0, r = 0, mask = scale_mask(t), note;
    int32_t pc;
    *root = 0;
    if (!(ui_rec_prefs & (PREF_SCALE_LEDS >> 8)))
        return 0;
    for (k = 0; k < 27u; k++) {
        if (e->keys && e->keys(t, k) >= 0)
            continue;
        if (t->p[P_QUANT] == QN_WHITE) {
            if ((note = kb_map(t, k)) > 127u)
                continue;                               /* (a black key: silent) */
            pc = (int32_t)note - t->p[P_TRANS];         /* (WHITE walks the scale before TRN) */
        } else {
            pc = 53 + (int32_t)k + t->p[P_TRANS];
        }
        pc = ((pc - t->p[P_ROOT]) % 12 + 12) % 12;
        m |= ((mask >> pc) & 1u) << k;
        r |= (uint32_t)(pc == 0) << k;
    }
    *root = r & m;
    return m;
}

static uint32_t song_leds(void);
/* MENU > ANIM IDLE (1.2, Discussion #135): after IDLE_LEDS_MS without panel input (ui.c scrn.idle: a button or key
 * pressed or held, a knob turned; what SCREEN OFF counts) the LEDs play the idle animation (hal/fm1_led_anim.h
 * fm1_idle_level: a soft light drifting over the keys and back, the buttons breathing with it; the scan draws it,
 * hal/fm1_input.h fm1_led_idle_start). Only with nothing going on: stopped (no count-in, no song, nothing armed or
 * waiting to start), no layer (held or locked), menu, dialog, NAME or UPDATE countdown, no key held or lit by the
 * selected track (MIDI IN notes), not with ANIM OFF. Any input ends it at once and is not swallowed (as SCREEN OFF's
 * wake would be): a key or a button down ends it in the scan before its next frame (the press acts as ever), a knob
 * here (the turn acts as ever); so does the transport starting (MIDI Start, the editor) or anything above. 1 = it has
 * the LEDs (ui_leds writes none). A dialog or the count-in count as input, as for SCREEN OFF (ui.c scr_frame) */
#define IDLE_LEDS_MS 60000u
static int idle_leds(void)
{
    int due = (ui_idle & 1u) && !(ui_prefs & PREF_ANIM_OFF) && !song.playing && !seq_counting() && !chain_busy() &&
              !song.rec && !transport_req && !ui.layer && !ui.lock && !ui.menu && !ui.confirm && !ui.uboot && !name_on() &&
              !(fm1_in.notes | fm1_in.buttons) && !panel_moved && !play_leds() && fm1_ms - scrn.idle >= IDLE_LEDS_MS;
    if (!due) {
        if (fm1_led_idle_on())
            fm1_led_anim_stop();
        return 0;
    }
    if (!fm1_led_anim_on())                            /* (not over the power-on sweep) */
        fm1_led_idle_start(settings_leds == LEDS_DIM || settings_leds == LEDS_DIM_LO);
    return fm1_led_idle_on();
}
/* the key LEDs, bit k = key k: NAME's keys, the layer's map, the DRUM grid, else the keys held and the notes
 * the selected track's sequencer, ARP and MIDI IN play, on STEP while stopped the cursor step's notes (#89; and
 * on SLICES the keys of the selected slice; on SONG the key of the cell's slot, 1.2) */
static uint32_t key_leds(uint32_t *br)                 /* (*br: a layer's keys that breathe, layer_leds) */
{
    uint32_t c;
    if (br)
        *br = 0;
    c = name_on() && !ui.menu ? name_leds() : ui.layer ? layer_leds(br) : grid_on() ? grid_leds() :
                 (fm1_in.notes & ~kb_layer) | play_leds() | step_leds() | (song_keys_on() ? song_leds() : 0u);
#if FELUCCA_SLICE
    if (!ui.layer && !ui.menu && !name_on() && slice_page_on())
        c |= slice_leds();                              /* SLICES: and the keys of the selected slice */
#endif
    return c;
}

/* The LEDs: lit = active (the page's family, PLAY / REC running, the keys held or playing, a map's keys), the ARP
 * beat flashes, the ones that can be pressed breathe (#119: the open layer's button, OCT+ when it would act, a
 * layer's keys; dark .. ~60 % of lit and back, hal/fm1_input.h fm1_led_breath, in every mode: never a hard blink in
 * a dark room; DIM LO up to ~30 %, DIM HI OFF INV ~60 %; with LEDS OFF it is the only glow; INV leaves it as it is,
 * between the dark active ones and the lit idle ones), every other button and key glows dim (#35: the
 * buttons of the black FM-1 can be found in the dark; hal/fm1_input.h fm1_led_dim, a short pulse each frame).
 * MENU > LEDS: DIM HI (default) that glow, DIM LO a darker one (fm1_led_dim_level), OFF no glow (as 1.0); INV
 * turns it around, as the stock firmware: the idle ones fully lit, the active ones dark, no glow (the ARP beat:
 * lit / dark). The keys' own maps (keys_own) stay lit or dark in every mode: their dark keys read as dark; only the
 * DRUM grid's keys that do something (grid_glow) glow under it, so STEP on a DRUM track is never a dark
 * keyboard (the steps of an empty pattern); on it the first step of each four (grid_beats: 1 5 9 13 of a page, where
 * no hit shows) glows steadier and brighter, so a bar's beats can be counted: DIM HI / DIM LO the mid level
 * (hal/fm1_input.h fm1_led_mid: lit 1 frame in 8, DIM LO in 12, over the glow), OFF and INV the glow alone (no
 * other key glows there). 1.2, MENU > SCALE LEDS ON (scale_leds; not over a map of the keys' own, nor on a DRUM or
 * SLICE track): the keys of the selected track's scale glow and the others are dark, the root's at the mid level, in
 * DIM HI, DIM LO and OFF alike (asked for: OFF only takes the other glow away); INV turns it around as it does the
 * rest: the root's keys lit, the scale's at the mid level, the others glowing; the keys held and playing as ever
 * (lit, INV dark). Each picture is built off-line and copied one byte per column, the
 * glow first: an LED going from lit to dim never has a dark frame */
static void ui_leds(void)
{
    uint8_t nl[FM1_NCOL] = {0}, nd[FM1_NCOL] = {0}, own[FM1_NCOL] = {0}, og[FM1_NCOL] = {0}, nb[FM1_NCOL] = {0};
    uint8_t nm[FM1_NCOL] = {0}, sg[FM1_NCOL] = {0}, sm[FM1_NCOL] = {0};
    uint32_t k, c, g, br, beat, sc, sr = 0;
    uint32_t fam = cur_fam(), mode = settings_leds;
    int keys_map = keys_own();
    static uint8_t ready;
    if (idle_leds() || fm1_led_anim_on())              /* the idle animation, the power-on sweep have them (main.c */
        return;                                         /* boot_leds) */
    if (!ready) {
        led_pos_init();
        ready = 1;
    }
    if (!ui.layer || FAM_BTN[fam] != layer_btn())
        led_put(nl, panel.btn[FAM_BTN[fam]], 1);
    if ((k = arp_led()) != 2u && (!ui.layer || layer_btn() != B_ARP))
        led_put(nl, panel.btn[B_ARP], FAM_BTN[fam] == B_ARP ? !k : (int)k);   /* (on ARP's page: dark flashes) */
    if (!ui.layer && (perf_latched || perf_k[0] || perf_k[1] || perf_k[2] || perf_k[3]))
        led_put(nl, panel.btn[B_FX], 1);                /* FX LATCH: lit while an effect or a macro is on */
    if (ui.layer)                                       /* the layer's button breathes while its map is up */
        led_put(nb, panel.btn[layer_btn()], 1);
    led_put(nl, panel.btn[B_REC], song.rec != 0u && ui.layer != LAYER_REC);   /* (its layer: it breathes) */
    k = oct_leds();
    led_put(nl, panel.btn[B_OCTDN], (int)(k & 1u));
    led_put(nl, panel.btn[B_OCTUP], (int)((k >> 1) & 1u));
    led_put(nb, panel.btn[B_OCTUP], (int)(k & OCT_BREATH));
    led_put(nb, panel.btn[B_OCTDN], (k & OCT_BREATH_DN) != 0u);
    c = key_leds(&br);
    g = !keys_map ? 0u : grid_on() && !ui.layer && !name_on() ? grid_glow() : 0u;   /* (key_leds: the grid's map) */
    beat = g ? grid_beats() & ~c : 0u;                  /* (the grid's empty beat starts: brighter than the glow) */
    sc = keys_map ? 0u : scale_leds(&sr);               /* SCALE LEDS: the scale's keys glow, the root's brighter */
    for (k = 0; k < 27u; k++) {
        uint32_t in = (sc >> k) & 1u, rt = (sr >> k) & 1u, idle = !((c >> k) & 1u);
        led_put(nm, 14u + k, (int)((beat >> k) & 1u));
        led_put(keys_map ? own : nl, 14u + k, (int)((c >> k) & 1u));
        led_put(keys_map ? og : nd, 14u + k, keys_map ? (int)((g >> k) & 1u) : !sc || (mode == LEDS_INV && rt));
        led_put(nb, 14u + k, (int)((br >> k) & 1u));
        if (!sc)
            continue;
        led_put(sg, 14u + k, mode == LEDS_INV ? !in && idle : (int)in);   /* (INV: the active ones dark) */
        led_put(sm, 14u + k, mode == LEDS_INV ? in && !rt && idle : (int)rt);
    }
    for (k = 0; k < NB; k++)
        led_put(nd, panel.btn[k], 1);
    if (song.playing || seq_counting())                 /* playing: PLAY's green, its own LED dark in every mode */
        led_clear(nd, panel.btn[B_PLAY]);
    for (c = 0; c < FM1_NCOL; c++) {
        if (mode == LEDS_INV)                           /* INV: the active ones dark, the rest lit */
            nl[c] = (uint8_t)(nd[c] & ~nl[c]);
        nd[c] = mode == LEDS_DIM || mode == LEDS_DIM_LO ? (uint8_t)(nd[c] | og[c]) : nm[c];   /* OFF, INV: no glow but */
        if (mode != LEDS_DIM && mode != LEDS_DIM_LO)    /* the grid's beats (the one level above dark there); DIM HI */
            nm[c] = 0;                                  /* / LO: the beats at the mid level over the glow */
        nd[c] |= sg[c];                                 /* SCALE LEDS: in every mode (INV: inverted above) */
        nm[c] |= sm[c];
        nm[c] &= (uint8_t)~(nb[c] | own[c]);
        nl[c] |= own[c];
        nl[c] &= (uint8_t)~nb[c];                       /* breathing: neither lit (INV's idle) nor the glow */
        nd[c] &= (uint8_t)~nb[c];
    }
    if (song.playing || (seq_counting() && cin_pos < click_beat_len() / 4u))   /* counting in: green on each beat */
        nl[LED_PLAY_GREEN >> 3] |= (uint8_t)(1u << (LED_PLAY_GREEN & 7u));
    fm1_led_dim_level(mode == LEDS_DIM_LO);
    for (c = 0; c < FM1_NCOL; c++) {
        fm1_led_dim[c] = nd[c];
        fm1_led_breath[c] = nb[c];
        fm1_led_mid[c] = nm[c];
    }
    for (c = 0; c < FM1_NCOL; c++)
        fm1_led[c] = nl[c];
}

/* ---------------------------------------------------------- input --- */
/* Predictable hardware response (#23): each decoded detent is one value step; a fast turn keeps its full signed
 * detent count. MENU > KNOB ACCEL ON (#52, OFF by default) multiplies a fast turn of a wide value (range > 32, not a
 * list of names; the FX and GLO layers' knobs too, #126) by 2..4, by up to 8 over a range above 64. The main loop
 * reads the knobs many times a frame (main.c), so a read holds one detent as a rule: the speed is the time per
 * detent, ACC_RATE / ms -> 25 ms x2, 16 ms x3, 12 ms x4, 10 ms x5 .. 6 ms or less x8 (a flick: 12 detents in 120 ms
 * move a -100..100 FX macro ~50). Only the longer of this read's and the previous read's time counts, and only while
 * the turn goes on (both under ACC_GAP ms) in one direction: a slow turn, the first two detents of a turn, a single
 * quick detent (a bounce) and a reversal are one step per detent, and the sign is always the detents'.
 * ui.enc_t[role]: bits 0..23 the ms of its last read, bit 24 its direction (+1), 25..31 its ms per detent (127 slow) */
#define ACC_GAP 40u
#define ACC_RATE 50u
static int32_t accel(uint32_t role, int32_t s, int32_t range)
{
    uint32_t now = fm1_ms & 0xFFFFFFu, st = ui.enc_t[role], up = s > 0, pi = st >> 25, a, i, m = 1;
    if (!(ui_prefs & PREF_ACCEL) || range <= 32 || !s)
        return s;
    a = (uint32_t)(s < 0 ? -s : s);
    i = ((now - st) & 0xFFFFFFu) / a;                   /* ms per detent of this read */
    if (!st || ((st >> 24) & 1u) != up || i >= ACC_GAP)
        i = 127u;                                       /* a new turn, or reversed */
    else if (pi < ACC_GAP) {
        m = ACC_RATE / (i > pi ? i : pi ? pi : 1u);
        a = range > 64 ? 8u : 4u;                       /* (the cap) */
        m = m < 1u ? 1u : m > a ? a : m;
    }
    ui.enc_t[role] = now | up << 24 | (i ? i : 1u) << 25;
    return s * (int32_t)m;
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
        ml_knob(t, P_MUTE);                             /* (MIDI LEARN: picked) */
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
    ml_knob(t, (uint32_t)(vp - t->p));
}

/* SEQ > STEP's piano roll (not the DRUM grid, whose white keys stay its steps): where step recording writes */
static int roll_on(void)
{
    return !ui.home && !ui.menu && !ui.confirm && song.seq_mode && cur_page()->graph == GR_ROLL && !drum_track(TSEL);
}

/* SONG: + ADD selected (song_row == count < CHAIN_ROWS): a section there, copying the one before it (the first: every
 * track on A, x1) */
static void song_add(void)
{
    uint32_t n = chain_config.count;
    if (n >= CHAIN_ROWS)
        return;
    chain_config.row[n] = n ? chain_config.row[n - 1u] : chain_row_of(0, 1);
    chain_config.count = (uint8_t)(n + 1u);
    ui.song_row = (uint8_t)n;
}
/* SONG's keys (song.grid 3): a press of A3 B3 C4 D4 (seq.c song_key) sets the selected cell's slot A..D (on + ADD it
 * adds the section first: a copy of the one before, the first with every track on that slot) */
static void song_keys(uint32_t notes)
{
    uint32_t k;
    for (k = 0; k < 27u; k++) {
        int32_t s = (notes >> k) & 1u ? song_key(k) : -1;
        if (s < 0)
            continue;
        if (chain_busy()) { ui_message("STOP TO EDIT"); return; }
        if (ui.song_row >= chain_config.count) {
            if (ui.song_row >= CHAIN_ROWS)
                return;
            song_add();
            if (chain_config.count == 1u)
                chain_config.row[0] = chain_row_of((uint32_t)s, 1);
        }
        chain_config.row[ui.song_row].slot[ui.song_trk % NTRK] = (uint8_t)s;
        ui.force = 1;
    }
}
/* the key LEDs of SONG: the key of the selected cell's slot (none for "-" and on + ADD) */
static uint32_t song_leds(void)
{
    uint32_t k, s;
    if (ui.song_row >= chain_config.count || (s = chain_config.row[ui.song_row].slot[ui.song_trk % NTRK]) >= 4u)
        return 0;
    for (k = 0; k < 27u; k++)
        if (song_key(k) == (int32_t)s)
            return 1u << k;
    return 0;
}

/* REC tap on every page: arm / disarm recording on the selected track without navigating; arming while stopped
 * starts the transport too (live recording), except on STEP's piano roll (1.2, Discussion #133): there it arms step
 * recording and the transport stays stopped: the keys write the cursor step (seq_entry), PLAY then records live.
 * Armed and playing, STEP's keys record live (at the play head) instead of writing the cursor step */
static int rec_tap(void)                                /* 1: the arming changed */
{
    uint8_t bit = (uint8_t)(1u << song.sel);
    if (chain_busy()) {
        ui_message("STOP TO RECORD");
        return 0;
    }
    if (!ui.home && cur_page()->graph == GR_SONG && !(song.rec & bit)) {
        ui_message("[SEQ] TO RECORD");
        return 0;
    }
    song.rec ^= bit;
    ui.force = 1;                                     /* also refresh the status on MENU / ABOUT */
    if ((song.rec & bit) && !song.playing && !seq_counting() && roll_on()) {
        ui_message(ui_prefs2 & (PREF_CHORD_ADD >> 16) ? "KEYS ADD TO STEP" : "KEYS WRITE STEPS");   /* step recording:
                                                         * stopped, at the cursor (CHORD ENTRY ADD: adding) */
        return 1;
    }
    if ((song.rec & bit) && !song.playing && !seq_counting())
        transport_req = 1;
    if ((song.rec & bit) && grid_on())
        ui_message("LANE KEYS RECORD");               /* (the white keys stay the steps) */
    else if ((song.rec & bit) && !ui.home && cur_page()->graph == GR_ROLL)
        ui_message("KEYS RECORD LIVE");               /* (seq_entry pauses while armed and playing) */
    return 1;
}

/* live recording into the selected track now: the STEP page's key entry pauses meanwhile */
static int live_rec_sel(void)
{
    return ((song.rec >> song.sel) & 1u) && (song.playing || transport_req == 1u || seq_counting());
}

/* the OCT- / OCT+ dialog (ui_draw.c draws it); trk: the track or the slot it is about */
static void confirm_open(uint32_t kind, uint32_t trk)
{
    ui.confirm = (uint8_t)kind;
    ui.confirm_trk = (uint8_t)trk;
    ui.act = 0;
    ui.force = 1;
}

/* MENU > CONTROL > STEP PREVIEW ON (1.2, Discussion #169): on SEQ > STEP (the roll and the DRUM grid) with the transport
 * stopped, a move of the cursor by a knob (KNOB 1, PRESETS) or the grid's page keys sounds the step it lands on once,
 * on the selected track (seq.c seq_audition). Not while playing, counting in or in a song; step recording moving on
 * after its keys (seq_entry) does not; a grid key on a step neither (it edits the step, a lane key plays the lane).
 * OFF (the default): the cursor moves silently, as before */
static void cursor_move(int32_t c)
{
    cursor_set(c);
    if ((ui_prefs2 & (PREF_PREVIEW >> 16)) && !song.playing && !seq_counting() && transport_req != 1u && !chain_busy() &&
        !ui.home && song.seq_mode && cur_page()->graph == GR_ROLL)
        aud_req = (uint16_t)((uint32_t)ui.cursor << 8 | (song.sel + 1u));
}

/* the grid's page down (-1) / up (+1): the cursor to the same place on it (at most the last step) */
static void page_go(int32_t d)
{
    uint32_t len = (uint32_t)TSEL->p[P_SLEN], pages = (len + 15u) / 16u, b;
    if (pages < 2u)
        return;
    b = (ui.bank + pages + (uint32_t)d) % pages;
    cursor_move((int32_t)(b * 16u + ui.cursor % 16u < len ? b * 16u + ui.cursor % 16u : len - 1u));
}

/* the grid's knobs: 1 STEP (the cursor), 2 LANE, 3 HIT and 4 ACC of the lane at the cursor (right on, left off) */
static void grid_edit(uint32_t slot, int32_t steps)
{
    if (slot == 0u)
        cursor_move(ui.cursor + steps);
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
            if (detail_on()) {                          /* DETAIL (1.5.1): the key picks the step, no hit toggled */
                cursor_set((int32_t)i);
                continue;
            }
            {   /* a hit (an accent) there goes when the key is let go, unless it was held for a lock (lock_keys) */
                const step_t *st = &TSEL->step[i];
                uint64_t b = (uint64_t)1 << i;
                uint32_t acc = (uint32_t)black_held(GK_ACC);
                if (((acc ? step_accents(st) : step_lanes(st)) >> ui.lane) & 1u) {
                    ui.plk_rm |= b;
                    ui.plk_acc = acc ? ui.plk_acc | b : ui.plk_acc & ~b;
                    ui.plk_lane = ui.lane;
                    ui.plk_trk = song.sel;
                } else if (acc) {
                    grid_acc(TSEL, i, ui.lane, 1);
                } else {
                    grid_hit(TSEL, i, ui.lane, 1);
                }
            }
            cursor_set((int32_t)i);
        } else if (p < NLANE) {
            ui.lane = (uint8_t)p;
        } else if (p != GK_ACC) {
            page_go(p == GK_PGUP ? 1 : -1);
        }
    }
}

/* ------------------------------------------------- parameter locks --- */
/* 1.1 (Discussions #75, #53): on SEQ > STEP, hold a step and turn KNOB 1..4: the step gets a lock of the parameter
 * that knob edits on the sound page shown last (ui.plk_src: HOME's four, ENV, LFO, FX, EDIT 1 / 2, OP ENV, ..; the
 * cards show those four while a step is held, a locked value bright, the sound's own DIM). The step held: on the
 * DRUM grid its white keys (several at once: each gets the lock; a press on a hit takes it away only when let go,
 * and not at all when it was held for a lock), on the roll the step being entered (its note keys held). [EDIT]
 * tapped meanwhile clears the held steps' locks (EDIT alone still clears the step, its locks with it). Not while a
 * song plays (STOP TO EDIT). A lock is a motion record (motion.c): AUTOMATION's PLAY plays it, CLEAR clears it */
static uint64_t lock_held(void)
{
    const track_t *t = TSEL;
    uint32_t k, len = (uint32_t)t->p[P_SLEN], keys = fm1_in.notes & ~kb_layer;
    uint64_t m = 0;
    if (ui.home || ui.menu || ui.confirm || ui.layer || name_on() || !song.seq_mode || cur_page()->graph != GR_ROLL ||
        !keys || detail_on())                           /* (DETAIL: its knobs are the step's own fields, sd_turn) */
        return 0;
    if (!grid_on())                                     /* the roll: the step being entered */
        return ui.entry_open && !live_rec_sel() && ui.cursor < len ? (uint64_t)1 << ui.cursor : 0u;
    for (k = 0; k < 27u; k++)
        if (((keys >> k) & 1u) && !key_black(k) && ui.bank * 16u + key_place(k) < len)
            m |= (uint64_t)1 << (ui.bank * 16u + key_place(k));
    return m;
}

/* each pass, before the grid's keys: a hit pressed on goes once its key is let go (plk_rm, grid_keys) */
static void lock_keys(void)
{
    uint64_t gone = ui.plk_rm & ~lock_held();
    uint32_t i;
    for (i = 0; gone && i < NSTEP; i++)
        if ((gone >> i) & 1u) {
            if ((ui.plk_acc >> i) & 1u)
                grid_acc(&trk[ui.plk_trk % NTRK], i, ui.plk_lane, 0);
            else
                grid_hit(&trk[ui.plk_trk % NTRK], i, ui.plk_lane, 0);
        }
    ui.plk_rm &= ~gone;
}

/* KNOB k turned s with the steps `held` held: each gets (or changes) its lock of lock_id(k), from its lock or the
 * sound's own value */
static void lock_turn(uint32_t k, int32_t s, uint64_t held)
{
    track_t *t = TSEL;
    uint32_t id = lock_id(k), i;
    const param_desc_t *d;
    if (chain_busy()) {
        ui_message("STOP TO EDIT");
        return;
    }
    if (id == 0xFFu)
        return;                                         /* (an empty card: nothing to lock) */
    ui.plk_rm &= ~held;                                 /* (held for a lock: its hit stays) */
    d = track_desc(t, id);
    s = accel(EN_K1 + k, s, d->fmt == F_ENUM ? 0 : d->max - d->min);
    motion_undo_take(t, 0x100u | k);                    /* (1.1.5: SAVE held takes the locks back) */
    for (i = 0; i < NSTEP; i++) {
        int16_t v;
        if (!((held >> i) & 1u))
            continue;
        if (!motion_lock_get(t, i, id, &v))
            v = motion_base_value(t, id);
        if (motion_set_lock(t, i, id, (int16_t)param_turn(d, v, s)) == 2)
            break;                                      /* (full: ui_notices says so) */
    }
    motion_undo_done(t);
}

/* [EDIT] with the steps `held` held: their locks go */
static void lock_clear(uint64_t held)
{
    uint32_t i, n = 0;
    ui.plk_rm &= ~held;
    if (!(motion_lock_steps(song.sel) & held)) {
        ui_message("NO LOCKS");
        return;
    }
    motion_undo_take(TSEL, 0);                          /* (1.1.5: SAVE held brings them back) */
    for (i = 0; i < NSTEP; i++)
        if ((held >> i) & 1u)
            n += motion_clear_locks(TSEL, i);
    motion_undo_done(TSEL);
    ui_message(n ? "LOCKS CLEARED" : "NO LOCKS");
}

/* NUDGE (1.2, #162; core.h step_nudge): the steps `held` move by one sixteenth of a step (-8..+7) a detent: SEQ > STEP
 * with steps held (as for a lock: the grid's step keys, the roll's step being entered) and the PRESETS knob; let go,
 * PRESETS moves the cursor again (MENU > STEP PREVIEW: it sounds). SEQ > AUTOMATION's NUDGE rows (ui_events.c) are the
 * other way. They play with the track's QUANTIZE OFF (AUTOMATION's QUANTIZE row; ON, the default, plays every step on
 * its start: the message says so). One undo per turn (SAVE held), as AUTOMATION's */
static void nudge_turn(uint64_t held, int32_t steps)
{
    uint32_t i;
    int32_t v = 0;
    char b[16] = "NUDGE ";
    if (chain_busy()) {
        ui_message("STOP TO EDIT");
        return;
    }
    ui.plk_rm &= ~held;                                 /* (a grid key held for it: its hit stays) */
    step_undo_take(TSEL, 0x800u);                       /* (a knob turned on: one) */
    for (i = 0; i < NSTEP; i++)
        if ((held >> i) & 1u) {
            v = clamp(step_nudge(&TSEL->step[i]) + (steps > 0 ? 1 : -1), -8, 7);
            step_set_nudge(&TSEL->step[i], v);
        }
    motion_undo_done(TSEL);
    if (TSEL->p[P_SQNT]) {
        ui_message("QUANTIZE IS ON");
        return;
    }
    ev_nudge_fmt(b + 6, v);
    ui_message(b);
}

/* SEQ > DETAIL (1.5.1, Discussion #199; ui.c detail_on): KNOB 1 CHANCE (0..100 %), 2 RATCH (x1..x4), 3 NUDGE (-8..+7
 * sixteenths), 4 VEL (1..127) of the cursor step, the fields SEQ > AUTOMATION lists as rows (ui_events.c ev_sval,
 * ev_sput: one storage, nothing new stored; back at its default a field is no row there). On the DRUM grid the white
 * keys held pick several steps: each moves by the turn. A step that plays nothing takes none (sd_takes: NO NOTE).
 * One undo per knob gesture on a step (SAVE held): walking to the next step starts another */
static uint64_t sd_held(void)                           /* the grid's step keys held on DETAIL (bit = step), 0 none */
{
    uint32_t k, len = (uint32_t)TSEL->p[P_SLEN], keys = fm1_in.notes & ~kb_layer;
    uint64_t m = 0;
    if (!keys || !detail_on() || !grid_on() || ui.layer || name_on())
        return 0;
    for (k = 0; k < 27u; k++)
        if (((keys >> k) & 1u) && !key_black(k) && ui.bank * 16u + key_place(k) < len)
            m |= (uint64_t)1 << (ui.bank * 16u + key_place(k));
    return m;
}
static uint64_t sd_steps(void)
{
    uint64_t m = sd_held();
    return m ? m : (uint64_t)1 << ui.cursor;
}
static void sd_turn(uint32_t slot, int32_t steps)
{
    track_t *t = TSEL;
    uint32_t k = SD_KIND[slot & 3u], i, n = 0;
    uint64_t m = sd_steps();
    int32_t d = k == EVK_CHANCE ? accel(EN_K1 + slot, steps, 100) : k == EVK_VEL ? accel(EN_K1 + slot, steps, 126) : steps;
    for (i = 0; i < NSTEP; i++)
        n += ((m >> i) & 1u) && sd_takes(&t->step[i], k);
    if (!n) {
        ui_message("NO NOTE");
        return;
    }
    step_undo_take(t, 0x4000u | slot << 6 | ui.cursor);   /* (this knob on this step: one undo) */
    for (i = 0; i < NSTEP; i++) {
        int32_t v;
        if (!((m >> i) & 1u) || !sd_takes(&t->step[i], k))
            continue;
        v = ev_sval(&t->step[i], k) + d;
        v = k == EVK_CHANCE ? clamp(v, 0, 100) : k == EVK_RATCH ? clamp(v, 1, 4) : k == EVK_VEL ? clamp(v, 1, 127) :
            clamp(v, -8, 7);
        ev_sput(&t->step[i], k, v);
    }
    motion_undo_done(t);
    if (k == EVK_NUDGE && t->p[P_SQNT])
        ui_message("QUANTIZE IS ON");                   /* (kept: it plays once QUANTIZE is OFF) */
    else if (k == EVK_VEL && (t->step[ui.cursor].flags & SF_ACCENT))
        ui_message("ACC PLAYS 127");                    /* (kept: it plays once the ACC goes) */
}
/* DETAIL's EDIT: the steps' four back to 100 % / x1 / 0 / 96 (their notes stay); one undo */
static void sd_reset(void)
{
    track_t *t = TSEL;
    uint64_t m = sd_steps();
    uint32_t i, k, n = 0;
    for (i = 0; i < NSTEP; i++)
        for (k = 0; k < 4u; k++)
            n += ((m >> i) & 1u) && ev_sval(&t->step[i], SD_KIND[k]) != ev_sdefv(SD_KIND[k]);
    if (!n) {
        ui_message("NOTHING TO RESET");
        return;
    }
    step_undo_take(t, 0);
    for (i = 0; i < NSTEP; i++)
        for (k = 0; (m >> i) & 1u && k < 4u; k++)
            ev_sput(&t->step[i], SD_KIND[k], ev_sdefv(SD_KIND[k]));
    motion_undo_done(t);
    ui_message("STEP RESET");
}

static void step_edit(uint32_t slot, int32_t steps)
{
    step_t *st = &TSEL->step[ui.cursor];
    uint32_t i;
    if (detail_on()) {                                    /* DETAIL (1.5.1): CHANCE RATCH NUDGE VEL */
        sd_turn(slot, steps);
        return;
    }
    if (drum_track(TSEL)) {
        grid_edit(slot, steps);
        return;
    }
    switch (slot) {
    case 0:                                               /* STEP: the cursor */
        cursor_move(ui.cursor + steps);
        break;
    case 1:                                               /* NOTE: transpose the step */
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
    case 2:                                               /* TIME; a note's LEN (1.5: ui.c note_set_len) */
        if (step_on(st)) {
            uint32_t was = note_len(TSEL, ui.cursor), n;
            step_undo_take(TSEL, 0x2000u);                /* (a knob turned on: one undo, SAVE held) */
            n = note_set_len(TSEL, ui.cursor, (uint32_t)clamp((int32_t)was + steps, 1, NSTEP));
            motion_undo_done(TSEL);
            if (steps > 0 && n == was)
                ui_message(ui.cursor + n < (uint32_t)clamp(TSEL->p[P_SLEN], 1, NSTEP) ? "NOTE AHEAD" : "END OF PATTERN");
            break;
        }
        st->time = (uint8_t)clamp((int32_t)st->time + (steps > 0 ? 1 : -1), ST_NOTE, ST_REST);
        break;
    default: {                                            /* FLAG: - / ACC / SLD / A+S */
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
    if (pg->graph == GR_EVENTS) {                         /* AUTOMATION (ui_events.c) */
        ev_knob(slot, steps);
        return;
    }
    if (pg->graph == GR_SONG) {                       /* KNOB 1 SECTION, 2 TRACK, 3 PAT (A..D, -), 4 REPS */
        chain_row_t *r;
        if (slot == 0u) {
            ui.song_row = (uint8_t)clamp((int32_t)ui.song_row + steps, 0,
                chain_config.count < CHAIN_ROWS ? chain_config.count : CHAIN_ROWS - 1u);
            return;
        }
        if (slot == 1u) {
            ui.song_trk = (uint8_t)clamp((int32_t)ui.song_trk + steps, 0, NTRK - 1);
            return;
        }
        if (chain_busy()) { ui_message("STOP TO EDIT"); return; }
        if (ui.song_row >= chain_config.count) {     /* + ADD: a turn adds the section (KNOB 3: only that) */
            song_add();
            if (slot == 2u) return;
        }
        r = &chain_config.row[ui.song_row];
        if (slot == 2u)
            r->slot[ui.song_trk % NTRK] = (uint8_t)clamp((int32_t)r->slot[ui.song_trk % NTRK] + steps, 0, CHAIN_SILENT);
        else
            r->repeat = (uint8_t)clamp((int32_t)r->repeat + steps, 1, 16);
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
    if (pg->graph == GR_BROWSE) {                         /* KNOB 1: one preset, KNOB 2: the next / previous engine */
        if (slot == 0u) {
            preset_step(steps);
        } else if (slot == 1u) {
            select_engine(eng_step(TSEL->eng_req, steps));
        } else if (slot == 2u) {
            preset_mark(steps > 0);
        } else if (slot == 3u) {                          /* LIST: ALL FAV BASS .. OTHER (ui.c list_mode), no wrap */
            uint32_t m = (uint32_t)clamp((int32_t)list_mode() + steps, 0, (int32_t)LIST_N - 1);
            if (m != list_mode()) {
                list_set(m);
                ui.force = 1;
                settings_later();                         /* (#197: not on every detent; ui.c) */
            }
        }
        return;
    }
#if FELUCCA_SLICE
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
            ui.uslot = (uint8_t)clamp((int32_t)ui.uslot + steps, 0, UP_SLOTS - 1);
        return;
    }
    if (pg->graph == GR_PATS) {                          /* KNOB 1 the pattern */
        if (slot == 0u)
            ui.ppick = (uint8_t)clamp((int32_t)pat_pick() + steps, 0, (int32_t)pat_count() - 1);
        return;
    }
    if (fop_page(pg->graph)) {                           /* FM6's operators: KNOB 1 OP, 2..4 its bytes (ui_fm6op.c) */
        fop_knob(slot, steps);
        return;
    }
    if (pg->graph == GR_MOD && slot == 0u) {             /* MOD: KNOB 1 the slot, 2..4 its SRC DST AMT */
        mod_ui_slot = (uint8_t)clamp((int32_t)mod_ui_slot + (steps > 0 ? 1 : -1), 0, 3);
        return;
    }
    d = page_desc(pg, slot, &vp);
    if (!d || !vp || d->max == d->min)
        return;
    v = param_turn(d, *vp, accel(EN_K1 + slot, steps, d->fmt == F_ENUM ? 0 : d->max - d->min));
    *vp = (int16_t)v;
    if (pg->scope != SC_GLOBAL) {
        motion_capture(TSEL, (uint32_t)(vp - TSEL->p), *vp);
        ml_knob(TSEL, (uint32_t)(vp - TSEL->p));        /* (MIDI LEARN: picked) */
    }
}

/* OCT+ on an action page: the picked action. A load stays picked (browse and load again); the others
 * are dropped once done. Flash writes only while stopped; over the user's data: the dialog */
static void act_do(void)
{
    uint32_t c = act_col(), id, k = (uint32_t)song.g[G_SLOT] - 1u;
    if (!c--)
        return;
    if (cur_page()->graph == GR_EVENTS) {
        ev_oct();
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
        if (song.playing || chain_busy() || seq_counting()) transport_req = 2;
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
#if FELUCCA_SLICE
    if (cur_page()->graph == GR_SLICES) {                 /* SPLIT / JOIN: stays picked (split again, join again) */
        if (slice_page_ok())
            slice_act(c);
        return;
    }
#endif
    if (cur_page()->graph == GR_USER) {                   /* 1 LOAD, 2 ERASE, 3 SAVE (the NAME screen first) */
        if (c > 1u)
            ui.act = 0;
        if (c == 2u && up_used(ui.uslot) && !transport_busy())
            confirm_open(CF_ERASE_USER, ui.uslot);        /* ERASE: the dialog first */
        else if (c != 3u)
            up_ui(c - 1u, ui.uslot);
        else if (transport_busy())
            ui_message("STOP TO SAVE");
        else if (up_used(ui.uslot))
            confirm_open(CF_OVR_USER, ui.uslot);
        else
            name_open(NK_USER_SAVE, ui.uslot);
        return;
    }
    id = cur_page()->id[c & 3u];
    if (id != G_LOAD)
        ui.act = 0;
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

/* MENU > CONTROL > CHORD ENTRY ADD (1.2, #155: chords with one hand): a key's notes go into the cursor step beside what
 * is there (up to 4), or out of it when its note is there already (a key that adds a chord takes all of it out); MONO
 * keeps one note (another key replaces it). A TIE or REST step becomes a note step; one with no note left a REST, as
 * EDIT leaves it */
static void step_toggle(const track_t *t, step_t *st, const uint8_t *ch, uint32_t n)
{
    uint32_t i, j, k;
    if (st->time != ST_NOTE) {
        st->n = 0;
        st->time = ST_NOTE;
    }
    for (j = 0; j < st->n && st->note[j] != ch[0]; j++)
        ;
    if (j < st->n) {                                   /* there: its notes out */
        for (i = 0; i < n; i++)
            for (j = 0; j < st->n; j++)
                if (st->note[j] == ch[i]) {
                    for (k = j; k + 1u < st->n; k++)
                        st->note[k] = st->note[k + 1u];
                    st->n--;
                    break;
                }
        if (!st->n)
            st->time = ST_REST;
        return;
    }
    if (t->p[P_VOICE] && !ENGINES[t->engine]->oneshot) {   /* MONO / LEG / UNI: one note */
        st->note[0] = ch[0];
        st->n = 1;
        return;
    }
    for (i = 0; i < n && st->n < 4u; i++) {
        for (j = 0; j < st->n && st->note[j] != ch[i]; j++)
            ;
        if (j == st->n)
            st->note[st->n++] = ch[i];
    }
}

/* SEQ step entry = step recording (1.2, Discussion #133: only with the track armed and the transport stopped), acid
 * style: the keys pressed together (POLY: up to 4 notes, MONO: the last one) become the cursor step; releasing all
 * keys moves on one step (wrapping inside LEN). With CHRD on a key writes what it sounds, as live recording does:
 * POLY its chord, MONO the chord's root. MENU > CONTROL > CHORD ENTRY ADD (1.2, #155): each key adds its notes to
 * the cursor step or takes them out (step_toggle), one after another with one hand, and letting go moves nothing: the
 * cursor moves by KNOB 1 (STEP) or PRESETS only, as it always could (EDIT still clears the step and moves on) */
static void seq_entry(uint32_t pressed)
{
    track_t *t = TSEL;
    step_t *st = &t->step[ui.cursor];
    uint32_t k, add = (ui_prefs2 & (PREF_CHORD_ADD >> 16)) != 0u;
    for (k = 0; k < 27u; k++) {
        uint8_t ch[CHORD_MAX];
        int32_t r;
        uint16_t mask;
        uint32_t note, n, i, j;
        if (!((pressed >> k) & 1u))
            continue;
        note = kb_map(t, k);
        if (note == KB_SILENT)
            continue;
        if (!ui.entry_open) {
            ui.entry_open = 1;
            if (!add) {
                st->n = 0;
                st->time = ST_NOTE;
            }
        }
        n = chord_make(t, note, ch, &r, &mask);        /* (CHRD OFF, a kit: the note alone; MONO: the root) */
        if (add)
            step_toggle(t, st, ch, n);
        else if (t->p[P_VOICE] && !ENGINES[t->engine]->oneshot) {   /* (drums: hits stack as a chord) */
            st->note[0] = ch[0];
            st->n = 1;
        } else {
            for (i = 0; i < n && st->n < 4u; i++) {
                for (j = 0; j < st->n && st->note[j] != ch[i]; j++)
                    ;
                if (j == st->n)
                    st->note[st->n++] = ch[i];
            }
        }
        last_note = (uint8_t)note;
    }
    if (ui.entry_open && !(fm1_in.notes & ~kb_layer)) {
        if (add)
            ui.entry_open = 0;                          /* (ADD: the cursor stays) */
        else
            cursor_set(ui.cursor + 1);
    }
}

/* Discussions #92 / #94: the PRESETS knob by page (the menu, a dialog, NAME and the layers: ui_input before this).
 *   the SEQ pages (STEP and the DRUM grid: the step cursor; AUTOMATION: the row) and the list / action pages USER,
 *     PHRASES, PROJECT, SONG: as their KNOB 1 (the SEQ pages: a sound load there would drop the track's motion: never).
 *     1.2: with PATTERN, CHANCE and the old AUTOMATION gone, KNOB 1 is the cursor or the row on every SEQ page, so
 *     the PRESETS knob is no longer a second cursor where KNOB 1 was something else; TOOLS and SLICES: nothing
 *   HOME, PRESETS and every other page (EDIT, ENV, LFO, FX, SCL, ARP, MIXER, CLOCK, ...): the selected track's
 *     sound, one list over every engine, then the used user presets (preset_step). A sound load keeps the steps
 *     (SAVE held undoes it). Up to 1.0.3 it did that on HOME and PRESETS only */
static void presets_turn(int32_t s)
{
    const page_t *pg = cur_page();
    uint32_t g = pg->graph;
    uint64_t held = lock_held();
    if (detail_on()) {                                    /* DETAIL (1.5.1): the cursor (its KNOB 1 is CHANCE) */
        if (chain_busy())
            ui_message("STOP TO EDIT");
        else
            cursor_move(ui.cursor + s);
        return;
    }
    if (held) {                                           /* STEP with steps held: their NUDGE (1.2) */
        nudge_turn(held, s);
        return;
    }
    if (ui.home || g == GR_BROWSE) {
        preset_step(s);
    } else if (g == GR_ROLL || g == GR_USER || g == GR_SLOTS || g == GR_PATS || g == GR_SONG || g == GR_EVENTS) {
        edit_param(0, s);                                 /* KNOB 1's (STEP: STOP TO EDIT while a song plays) */
        ui.hot_col = 0;
        ui.hot_t = 40;
    } else if (g != GR_TOOLS && g != GR_SLICES) {
        preset_step(s);
    }
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
static int layer_knobs_quiet(void);
static uint32_t layer_gesture(uint32_t now, uint32_t combo);
static void layer_show(void);
static void layer_tap(uint32_t l);
static void layer_keys(uint32_t keys);
static void layer_knob(uint32_t k, int32_t s);
static void fx_map_sync(void);
static void fx_assign(int32_t s);
static void fx_default(void);
static int layer_play(void);
static int layer_set_open(void);
static uint32_t layer_oct(uint32_t pressed, uint32_t oct);
static void layer_oct_open(uint32_t pressed);
static int layer_allowed(void);
static uint32_t ly_bit(uint32_t l);
static void layer_lock_input(uint32_t pressed);

/* a page button let go (they act on release; a layer's own button: layer_gesture). 1: it acted (EDIT on STEP, USER,
 * PROJECT, AUTOMATION) instead of opening a page */
static int page_tap(uint32_t b)
{
    uint32_t f;
    if (b == B_EDIT && detail_on()) {                    /* DETAIL (1.5.1): the step's CHANCE RATCH NUDGE VEL back */
        if (chain_busy())
            ui_message("STOP TO EDIT");
        else
            sd_reset();
        return 1;
    }
    if (b == B_EDIT && song.seq_mode && !ui.home && cur_page()->graph == GR_ROLL) {   /* STEP: EDIT clears the step */
        uint64_t held = lock_held();
        if (chain_busy()) { ui_message("STOP TO EDIT"); return 1; }
        if (held) {                                     /* a step held: its locks (the step stays) */
            lock_clear(held);
            return 1;
        }
        step_clear(&TSEL->step[ui.cursor]);
        (void)motion_clear_locks(TSEL, ui.cursor);
        cursor_set(ui.cursor + 1);
        ui_message("STEP CLEARED");
        return 1;
    }
    if (b == B_EDIT && !ui.home && cur_page()->graph == GR_EVENTS) {   /* AUTOMATION: EDIT deletes the row's record */
        ev_delete();
        return 1;
    }
    if (b == B_EDIT && !ui.home && (cur_page()->graph == GR_USER || cur_page()->graph == GR_SLOTS)) {
        name_rename();                                  /* SAVE > USER / PROJECT: EDIT renames the slot */
        return 1;
    }
    for (f = FAM_HOME + 1u; f < FAM_COUNT; f++)        /* (GLO SEQ SAVE ..: HOME is ui_input's, home_tap) */
        if (FAM_BTN[f] == b && b != B_HOME) {
            open_family(f);
            return 0;
        }
    return 0;
}

/* a track whose sound's sample is missing (engines.c snd_missing: an empty user slot, a built-in set this build lacks;
 * it plays a sine): the no-file icon and NO SAMPLE (ui.c MSG_NO_SAMPLE). Once per track and source: at power-on, a sound
 * or project load, SRC / SET turned, a slot erased; again after another source or a project load. One a frame; after
 * a message already showing (LOADED) as the second message. Not on notes: nothing in the audio path */
static void sample_notice(void)
{
    uint32_t k, code;
    for (k = 0; k < NTRK; k++) {
        code = 0;
        if (!snd_missing(&trk[k], &code)) {
            snd_said[k] = 0;
            continue;
        }
        if (snd_said[k] == code)
            continue;
        snd_said[k] = (uint8_t)code;
        if (ui.msg_t)
            str_cpy(ui.msg2, MSG_NO_SAMPLE, sizeof ui.msg2);
        else
            ui_message(MSG_NO_SAMPLE);
        return;
    }
}

/* messages of things that happened elsewhere (a load, the editor, MIDI in): after this frame's own */
static void ui_notices(void)
{
    static uint32_t midi_t, midi_last;
    sample_notice();
    ml_poll();                                          /* (MIDI LEARN: a CC came for the picked parameter) */
    if (motion_full) { motion_full = 0; ui_message("AUTOMATION FULL"); }
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

static void ui_input(void)
{
    uint32_t pressed = fm1_input_edges(0), notes = fm1_input_note_edges(), now = fm1_ticks(), id, b, k;
    uint32_t home, rec, save;
    uint32_t oct;
    uint32_t lay, combo = 0, lytap, lkeys, glo, kq = 0;
    int32_t s, sel = 0, ks[4] = {0, 0, 0, 0};
    static uint32_t lock_ms;                            /* BPM LOCK: the last locked SELECT turn (fm1_ms | 1; 0 none) */
    midi_pc_poll();                                     /* MIDI Program Change (before fm6_poll: a bank voice) */
    fm6_poll();                                         /* FM6: PTCH turned -> its patch */
#if !FELUCCA_FM4
    for (k = 0; k < NTRK; k++)                          /* a DIGITAL sound any other way (the paths convert it */
        if (trk[k].eng_req == ENGI_DIGITAL)             /* already): FM6 (fm4_convert.c) */
            fm4_track(&trk[k]);
#endif
    perf_latch_on = fx_latch & 1u;                      /* (MENU > FX LATCH; a settings load sets it too) */
    fx_map_sync();                                      /* (the FX layer's key map: ui_layer.c, a settings load too) */
    fx_usb_fixed = (ui_prefs & PREF_USB_FIXED) != 0u;   /* (MENU > USB LEVEL: fx.c, audio.c) */
    scope_mix = (ui_prefs2 & (PREF_SCOPE_MIX >> 16)) != 0u;   /* (MENU > SCOPE: fx.c, audio.c) */
    rp_apply();                                         /* (MENU > CLICK, CLICK LEVEL, COUNT-IN: click.c, seq.c) */
    if (!ui.menu)
        usb_serial_apply();                             /* (MENU > USB SERIAL: when the menu has closed) */
    settings_later_poll();                              /* (#197: LIST / FAV written once the view is left or idle) */
    if (scr_input(pressed, notes))                      /* MENU > SCREEN OFF: dark, or the wake gesture: swallowed */
        return;                                         /* (before the buttons' timers: no tap or hold after it) */
    if (mom.n && !mom_down()) {                         /* MOMENTARY: LFO let go (or the menu, a layer ..): back */
        mom_restore();
        ui_message("BACK");
    }
    if (mom.n && ((pressed >> panel.btn[B_OCTUP]) & 1u)) {   /* .. OCT+ with LFO held: kept (no octave) */
        mom.n = 0;
        pressed &= ~(1u << panel.btn[B_OCTUP]);
        ui_message("KEPT");
    }
    home = btn_hold(&ui.home_t0, B_HOME, now, 1);
    rec = btn_hold(&ui.rec_t0, B_REC, now, 0);          /* (a tap; held, the REC layer: ui_layer.c) */
    save = btn_hold(&ui.save_t0, B_SAVE, now, !ui.menu && !ui.confirm);   /* held: UNDO (ui.c undo_swap) */
    layer_lock_input(pressed);                          /* (#83: a button closes a locked layer) */
    layer_arm(pressed, now);
    if (((pressed >> panel.btn[B_REC]) & 1u) && ui.ly == LAYER_REC)
        ui.rec_t0 |= 2u;                                /* (REC is its layer's button now: its tap is layer_tap's) */
    layer_oct_open(pressed);                            /* (OCT± with a SET layer's button down: it opens now) */
    oct = oct_taps(pressed, ui.menu || ui.confirm || act_cols() || name_on() || layer_set_open() || ui.ml.on);
    oct = layer_oct(pressed, oct);                      /* (a SET layer's OCT-: put back) */
    layer_masks();                                      /* seq.c: keys pressed with a layer's button are its own */
    lay = layer_held();
    glo = lay && ui.ly == LAYER_GLO;                    /* GLO held: SELECT is the tempo, BPM LOCK or not (#58) */
    if (!layer_allowed()) {
        perf_kill = 1;                                  /* (effects off until their keys are let go) */
        perf_latched = 0;                               /* (FX LATCH: the latched ones and the macros off) */
        perf_k[0] = perf_k[1] = perf_k[2] = perf_k[3] = 0;
    }
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
        combo |= (pressed & ~ly_bit(ui.ly) &            /* (REC + PLAY: PLAY plays and REC let go soon is still */
                  ~(ui.ly == LAYER_REC ? 1u << panel.btn[B_PLAY] : 0u)) != 0u;   /* a tap, it arms: as before) */
        notes = 0;                                      /* (the keys are the layer's, not the grid's or a step's) */
        if (pressed & (1u << panel.btn[B_SAVE]))       /* no UNDO, no page */
            ui.save_t0 |= 2u;
        if (pressed & (1u << panel.btn[B_HOME]))       /* no menu, no HOME */
            ui.home_t0 |= 2u;
        for (k = 0; k < 4u; k++)                        /* KNOB 1..4: the layer's (ui_layer.c layer_knob) */
            if ((ks[k] = panel_enc(EN_K1 + k)) != 0)
                combo = 1;
        if ((s = panel_enc(EN_PRESET)) != 0 && ui.ly == LAYER_FX) {   /* (a stray turn would load another sound) */
            fx_assign(s);                               /* FX: the white key held takes the next effect */
            combo = 1;
        }
        if (ui.ly == LAYER_FX && ((pressed >> panel.btn[B_EDIT]) & 1u))
            fx_default();                               /* .. EDIT: its default back */
        panel_enc(EN_ALGO);                             /* (another track: OCT- puts back the layer's track only) */
        if (glo && (sel = panel_enc(EN_SELECT)) != 0)   /* GLO + SELECT: the tempo, a combo (OCT- puts it back) */
            combo = 1;
    } else if ((kq = (uint32_t)layer_knobs_quiet()) != 0) {   /* a layer letting go: KNOB 1..4 are nobody's (#39) */
        for (k = 0; k < 4u; k++)
            if (panel_enc(EN_K1 + k) != 0)
                combo = 1;                              /* (with the button let go this frame: no tap) */
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
    if (home == BT_TAP && ui.menu) {                    /* HOME (pressed, or held) closes the menu, from ABOUT too; */
        menu_close();                                   /* the release of the hold that opened it is no tap */
        home = BT_NONE;                                 /* (menu_close went HOME already) */
    } else if (home == BT_HOLD) {                       /* HOME held: open the menu, or leave it */
        if (ui.menu) {
            menu_close();
        } else {
            ui.menu = 1;                                /* (at the tab and row it was left at) */
            ui.confirm = 0;                             /* (a clear dialog is cancelled, NAME too) */
            name_close();
            ui.force = 1;
            song.seq_mode = 0;
        }
    }
    if (rec == BT_TAP && !ui.menu && !ui.confirm && !name_on())   /* REC pressed in another layer (REC alone: */
        rec_tap();                                      /* its layer's tap); nothing in the menu, a dialog or NAME */
    if (ui.menu) {                                      /* SAVE / REC taps do nothing here (HOME: above) */
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
        if (((pressed >> panel.btn[B_PLAY]) & 1u) && (song.playing || chain_busy() || seq_counting()))
            transport_req = 2;                          /* PLAY stops a transport started meanwhile (MIDI Start, the
                                                         * editor) so the name can be saved; it never starts one */
        ui.pg_down = 0;
        name_input(notes, oct);
        return;
    }
    if (save == BT_HOLD && chain_busy())
        ui_message("STOP TO UNDO");
    else if (save == BT_HOLD)                                /* SAVE held: undo the last sound load */
        undo_swap();
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
                up_ui(1u, ui.confirm_trk);
            } else if (kind == CF_LOAD_PAT) {
                pat_load_ui(&trk[ui.confirm_trk % NTRK], pat_pick());
                str_cpy(ui.msg2, "[SAVE] HOLD TO UNDO", sizeof ui.msg2);
            } else if (kind == CF_CLEAR_MOTION) {
                track_t *t = &trk[ui.confirm_trk % NTRK];
                if (!chain_busy()) { load_begin(t, UNDO_PAT); motion_clear(t); load_end(t); ui_message("AUTOMATION CLEARED"); }
            } else if (kind == CF_DEL_ROW) {
                uint32_t r = ui.confirm_trk;
                if (!chain_busy() && r < chain_config.count) {
                    for (; r + 1u < chain_config.count; r++) chain_config.row[r] = chain_config.row[r + 1u];
                    chain_config.count--;
                    if (ui.song_row > chain_config.count) ui.song_row = chain_config.count;
                    ui_message("SECTION DELETED");
                }
            } else if (kind == CF_CLEAR_SONG) {
                if (!chain_busy()) { chain_defaults(&chain_config); ui.song_row = 0; ui_message("SONG CLEARED"); }
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
    if (lytap)                                          /* a layer's button acts on release (held: the layer; SEQ's */
        layer_tap(lytap);                               /* on every page since 1.2, its tap the SEQ pages) */
    if (home == BT_TAP)                                 /* HOME acts on release: a hold opens the menu */
        home_tap();
    cursor_fix();                                       /* LEN may have changed (knob, editor, load) */
    for (id = 0; id < 14u; id++) {
        if (!((pressed >> id) & 1u))
            continue;
        b = panel_btn_of(id);
        switch (b) {
        case B_PLAY:
            if (layer_play())                           /* (GLO held: RESTART) */
                break;
            if (song.playing || chain_busy() || seq_counting())   /* (a count-in: PLAY stops it) */
                transport_req = 2;
            else if (!ui.home && cur_page()->graph == GR_SONG)
                chain_play_ui();
            else
                transport_req = 1;
            break;
        case B_SEQ:
        case B_REC:                                     /* tap: above; held, its layer (ui_layer.c) */
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
            if (act_cols() || layer_set_open() || ui.ml.on)   /* action pages: enter / back (below); SET layers: OCT-; */
                break;                                  /* MIDI LEARN: clear / done (below) */
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
    } else if (ui.ml.on && oct) {                          /* MIDI LEARN: OCT- clears the picked CC, OCT+ done */
        ml_oct(oct);
    }
    song.grid = (uint8_t)keys_mode();                 /* (seq.c: the keys are the grid's) */
#if FELUCCA_SLICE
    if (notes && slice_page_on())                       /* SLICES: a key picks the slice it plays */
        slice_keys_pick(notes);
#endif
    lock_keys();                                        /* (a grid key on a hit let go: the hit goes) */
    if (song.grid == 3u) {
        song_keys(notes);
    } else if (song.grid) {
        grid_keys(notes);
    } else if (song.seq_mode && cur_page()->graph == GR_ROLL) {   /* STEP */
        if (live_rec_sel() || !((song.rec >> song.sel) & 1u))   /* armed and playing: the keys record live, not
                                                         * into the cursor step too; not armed (1.2, #133): they
                                                         * only play, being on STEP writes nothing */
            ui.entry_open = 0;
        else if (!chain_busy())
            seq_entry(notes);                           /* armed and stopped: step recording at the cursor */
        else if (notes)
            ui_message("STOP TO EDIT");
    }

    /* #102: the knobs a layer took above are not read again here. The TIMER5 scan (10 kHz) counts detents while this
     * pass runs, so one landing after the layer's read went to the page as well: KNOB 1..4 edited the layer's control
     * and the page under it (HOME's sound, motion recorded), PRESETS could load a sound. They stay for the next pass */
    kq |= lay;
    if (!lay && (s = panel_enc(EN_PRESET)) != 0)
        presets_turn(s);
    if (!lay && (s = panel_enc(EN_ALGO)) != 0)     /* ALGORITHM: the selected track, on every page */
        track_select((uint32_t)clamp((int32_t)song.sel + (s > 0 ? 1 : -1), 0, NTRK - 1));
    if ((s = glo ? sel : panel_enc(EN_SELECT)) != 0) {   /* SELECT knob = global tempo; */
        if (glo || !(ui_prefs & PREF_BPM_LOCK)) {
            song.g[G_BPM] = (int16_t)clamp(song.g[G_BPM] + accel(EN_SELECT, s, 200), GP[G_BPM].min, GP[G_BPM].max);
            ui.bpm_t = 40;                              /* the header's BPM lights up; no message over the header */
        } else {                                        /* MENU > BPM LOCK ON (#58): only with GLO held (and on HOME >
                                                         * CLOCK, GLO's F4 TAP); a turn burst says so once */
            if (!lock_ms || fm1_ms - lock_ms > 1000u)
                ui_message("BPM LOCKED");
            lock_ms = fm1_ms | 1u;
        }
    }
    for (k = 0; k < 4u && !kq; k++) {
        const page_t *pg = cur_page();
        int16_t *hv;
        uint64_t held;
        if ((s = panel_enc(EN_K1 + k)) == 0)
            continue;
        if (mom_down())                                 /* (LFO + a knob, MOMENTARY or not: LFO's release opens no page) */
            ui.pg_down &= (uint16_t)~(1u << panel.btn[B_LFO]);
        if ((held = lock_held()) != 0u) {               /* a step held on STEP: KNOB k locks (parameter locks) */
            lock_turn(k, s, held);
            ui.hot_col = (uint8_t)k;
            ui.hot_t = 40;
            continue;
        }
        if (mom_down() && mom_turn(k, s)) {             /* MOMENTARY: LFO held (no automation, no undo) */
            ui.hot_col = (uint8_t)k;
            ui.hot_t = 40;
            continue;
        }
        if (ui.home || pg->scope == SC_STEP || pg->scope == SC_TRK || page_desc(pg, k, &hv) ||
            ((pg->graph == GR_USER || pg->graph == GR_MOD || pg->graph == GR_PATS) && k == 0u)
            || pg->graph == GR_SONG || (pg->graph == GR_SLICES && k < 2u) || fop_page(pg->graph)) {   /* (not an empty
                                                                                                          * column) */
            ui.hot_col = (uint8_t)k;
            ui.hot_t = 40;
        }
        if (ui.home) {
            int16_t *vp;
            const param_desc_t *d = home_param(k, &vp);
            *vp = (int16_t)param_turn(d, *vp, accel(EN_K1 + k, s, d->fmt == F_ENUM ? 0 : d->max - d->min));
            motion_capture(home_trk(k), (uint32_t)(vp - home_trk(k)->p), *vp);   /* (LEVELS: that track's, as MIXER) */
            ml_knob(home_trk(k), (uint32_t)(vp - home_trk(k)->p));
        } else {
            edit_param(k, s);
        }
    }
    ui_notices();
}

/* ---------------------------------------------------- panel setup --- */
/* 30 s without input: give up and keep the old table (a stuck key cannot hang the boot). A piano key cancels at
 * any time: the keys are not part of what is taught, so one works whatever the buttons' table says */
#define SETUP_IDLE_MS 30000u
#define SETUP_GIVE_UP(t0) (fm1_ms - (t0) > SETUP_IDLE_MS || fm1_in.notes != 0u)
static void setup_title(void)
{
    lcd_fill(0, 0, 240, 240, T_BG);
    {   /* the title with its icon (the menu row's), centred together; M from y 8 as before */
        const char *t = "HARDWARE CALIBRATION";
        int32_t x = (240 - (16 + 6 + text_w(&AF_M, t))) / 2;
        cv_begin(240, 24, T_BG);
        GFX_HOOK_ALIGN(0, 0, 240, 0, AL_H | AL_N(2), "calibration title centred");
        GFX_HOOK_ALIGN(0, HEAD_MY + AF_M_CAP_Y, 0, HEAD_MY + AF_M_CAP_Y + AF_M_CAP_H, AL_V | AL_PASS,
                       "header icon on its title's line");
        cv_icon_mid(x, 12, 16, ICON_X_DOCTOR, T_THEME, T_BG);
        cv_text(x + 22, HEAD_MY, &AF_M, t, T_TEXT);
        cv_blit(0, 5);
    }
    draw_text_box(0, 32, 240, &AF_S, "TEACH EACH BUTTON AND KNOB", T_MID, 1);
    lcd_fill(16, 56, 208, 1, T_LINE);
    draw_text_box(0, 206, 240, &AF_S, "ANY KEY: CANCEL", T_DIM, 1);
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
        if (SETUP_GIVE_UP(t0))
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
            if (SETUP_GIVE_UP(t0))
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
            if (SETUP_GIVE_UP(t0))
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
