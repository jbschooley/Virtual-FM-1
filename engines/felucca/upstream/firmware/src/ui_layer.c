/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca quick layers: while a page button is held, the keys and KNOB 1..4
 * are its shortcuts and its map shows over the page. One table (LAYERS) drives the gesture, the keys, the knobs,
 * the LEDs and the overlay:
 *   FX   HOLD  the performance effects (perform.c) and the track mutes while held; KNOB 1..4 its macros. The white
 *              keys' effects are the user's (1.2): a white key held in the layer (FX held or locked) + PRESETS turned
 *              steps that key through NONE and the effects (it holds the new one at once), EDIT puts the key's default
 *              back; kept with the settings (fx_keys) when the layer closes
 *   GLO  SET   black keys 1..4 (F#3 G#3 A#3 C#4) T1..T4 MUTE (latched; lit = sounding), F3..B3 SOLO T1..T4 while
 *              held (HOLD cells, a corner triangle), C4 UNMUTE ALL, D4 MIDI LEARN on / off (1.5, midi_learn.c), F4 TAP
 *              tempo; KNOB 1..4 T1..T4 LEVEL;
 *              GLO + PLAY: from the top without stopping
 *   SCL  SET   any key: its note name is ROOT; KNOB 1..4 ROOT SCL CHRD VOIC (LY_SCL: the SCL page's first two,
 *              the CHORD page's two; QNT TRN stay on SCL); the LEDs show the root lit and the scale's notes breathing
 *   EDIT SET   the white keys from F3: the engines in PRESETS order (one key each, the NENG_SHOWN one can pick:
 *              engines.c eng_vis), the next white key INIT (LY_INIT: E5)
 *              (the dialog); KNOB 1 ENG, 2 No. (the engine's sounds), 3 FAV. Sound loads as on PRESETS: the steps
 *              stay, SAVE held undoes, the editor gets RELOAD; they apply while playing too. 1.4.1: the black keys
 *              the selected track's engine's quick actions (EQA below: DRUM 1..8 its lanes' MUTE; other engines
 *              none yet, the keys dark)
 *   SEQ  SET   (1.2, on every page; up to 1.1.5 SEQ held opened SONG) TOOLS: the white keys from F3 the sequence tools
 *              (ui_tools.c: CLEAR, REVERSE, SHIFT < >, RANDOM, COOK; on a DRUM track BEAT and the lane's CLEAR REVERSE
 *              FILL RANDOM), black keys 1..8 the lane (DRUM); KNOB 1..4 LEN DIV SWING GATE (the PATTERN page's up to
 *              1.1.5: the page is gone, they are here). Each action one undo (SAVE held), OCT- the track as the layer
 *              opened (tl_open)
 *   REC  SET   (1.1.5, on every page; REC's tap still arms / disarms) the white keys from F3: CLEAR (the selected
 *              track's steps and automation, no dialog: one undo, SAVE held), CLICK (OFF REC ON), COUNT-IN (OFF 1 BAR
 *              2 BARS), CLICK LEVEL (LOW MID HIGH), each press the next value; KNOB 1..3 the same three. The settings
 *              are MENU > AUDIO's (ui.c rp_get / rp_put: one store), saved when the layer closes; OCT- puts back the
 *              settings and the track as the layer opened (tl_open). Replaces the old REC held on SEQ (Discussion #91:
 *              the CLEAR Tn SEQUENCE? dialog and its "HOLD: CLEAR" hint)
 * The gesture: let go before HOLD (the menu: 0.3 .. 0.6 s) with nothing else touched: a tap, the button's page.
 * Held past HOLD alone: the map (a peek), letting go does nothing. A key, a knob or a button meanwhile: a combo,
 * the map at once, no tap. Keys pressed with the button down are the layer's (seq.c keyboard_block): silent, no
 * MIDI, never recorded; keys held before stay notes. Only one layer at a time: a second layer button is ignored.
 * PLAY and REC work in every layer (GLO held + PLAY: RESTART; GLO's lock: PLAY as always, #183); SAVE, HOME, SEQ and
 * the other page buttons are swallowed. In SET layers OCT± do not shift the octave: OCT- (on release, not with OCT+)
 * puts back what the layer changed since it opened. A HOLD key's effect lasts until the key is let go, the map with
 * it. No layer in the menu, a dialog, NAME or the UPDATE MODE countdown. After a tap, until the layer has been opened
 * once: "HOLD [GLO] QUICK" (the seen bits are kept with the settings, favorites.c spare byte).
 * The lock (Discussion #83): a double tap (the second press within LY_DTAP_MS of the first tap, both let go before
 * HOLD, nothing else touched) opens the map and keeps it open with no button held (ui.lock): ly_down() counts the lock
 * as the button held, so the keys (seq.c keyboard_block: kb_lock), KNOB 1..4, OCT- act exactly as held (PLAY starts
 * and stops: GLO's RESTART needs GLO held, layer_play). A tap of its button closes it (the press hands it back to the
 * button: held on, a peek; let go, closed; never a tap), so do another page button, HOME, SAVE, SEQ (each then acts as
 * always), the menu, a dialog, NAME. The first tap is not deferred (a single tap opens its page at once, no lag): it
 * opens the page and remembers the page it left (lys.nv); the second tap locks the layer over that page, put back, so
 * a double tap leaves the page as it was. A first tap that acted instead of opening a page (EDIT on STEP: clear the
 * step; on USER / PROJECT: rename) arms no double tap: two quick taps there still act twice. */
#include "ui_tools.c"                                   /* SEQ TOOLS' actions */
enum { LK_HOLD, LK_SET };
typedef struct {
    uint8_t btn, kind, fam;            /* the button, HOLD / SET, the family whose first page KNOB 1..4 edit */
    const char *head;                  /* (FAM_HOME: the layer's own knobs) the header over the map */
    khint_t foot[3];                   /* the footer's key hints (a third with no word: none) */
} layer_t;
static const layer_t LAYERS[LAYER_N] = {
    {0, 0, 0, "", {{0, 0}, {0, 0}, {0, 0}}},
    {B_FX, LK_HOLD, FAM_HOME, "[FX] HOLD", {{KC_KEYS, "EFFECTS"}, {KC_K14, "MACROS"}, {0, 0}}},   /* (LET GO: the header's HOLD) */
    {B_GLO, LK_SET, FAM_HOME, "[GLO] SET", {{KC_PLAY, "RESTART"}, {KC_OCTDN, "UNDO"}, {KC_GLO, "DONE"}}},
    {B_SCL, LK_SET, FAM_SCL, "[SCL] SET", {{KC_KEYS, "ROOT"}, {KC_OCTDN, "UNDO"}, {KC_SCL, "DONE"}}},
    {B_EDIT, LK_SET, FAM_HOME, "[EDIT] SET", {{KC_KEYS, "ENGINE"}, {KC_OCTDN, "UNDO"}, {KC_EDIT, "DONE"}}},
    {B_SEQ, LK_SET, FAM_SEQ, "[SEQ] TOOLS", {{KC_KEYS, "TOOLS"}, {KC_OCTDN, "UNDO"}, {KC_SEQ, "DONE"}}},
    {B_REC, LK_SET, FAM_HOME, "[REC] SET", {{KC_KEYS, "RECORDING"}, {KC_OCTDN, "UNDO"}, {KC_REC, "DONE"}}},
};
static const uint8_t LY_KC[LAYER_N] = {0, KC_FX, KC_GLO, KC_SCL, KC_EDIT, KC_SEQ, KC_REC};
/* SCL's knobs: the key and its chord (cur_page() while the layer edits or draws them: page_over) */
static const page_t LY_SCL = {"SCL", FAM_SCL, SC_TRACK, GR_SCALE, {P_ROOT, P_SCALE, P_CHRD, P_VOIC}};
/* SEQ TOOLS' knobs: LEN DIV SWING GATE (the length the tools work in; up to 1.1.5 the PATTERN page's) */
static const page_t LY_SEQ = {"PATTERN", FAM_SEQ, SC_TRACK, GR_STEPS, {P_SLEN, P_SDIV, P_SSWING, P_SGATE}};
static const page_t *ly_page(uint32_t l) { return l == LAYER_SCL ? &LY_SCL : l == LAYER_SEQ ? &LY_SEQ : 0; }
#define LY_OPEN 2u                     /* ui.ly_t0: the map opened (no tap any more) */
#define LY_COMBO 4u                    /* .. by a combo */
#define LY_DEAD 8u                     /* .. pressed where there is no layer: does nothing */
#define LY_INIT ((uint32_t)NENG_SHOWN) /* EDIT: the white key of INIT, the one after the engines (13: E5), its map cell */
typedef char ly_init_fits[LY_INIT < 16u ? 1 : -1];   /* (a white key: F3 .. G5) */
#define layer_seen (favorites.factory[15][31])   /* bit l: layer l opened once (a byte no engine uses) */
static const khint_t FX_LATCH_FOOT[3] = {{KC_KEYS, "ON / OFF"}, {KC_K14, "MACROS"}, {KC_OCTDN, "ALL OFF"}};
static const khint_t FX_KEY_FOOT[3] = {{KC_PRESETS, "ASSIGN"}, {KC_EDIT, "DEFAULT"}, {KC_K14, "MACROS"}};   /* (a key held) */
static const khint_t GLO_LOCK_FOOT[3] = {{KC_OCTDN, "UNDO"}, {KC_GLO, "DONE"}, {0, 0}};   /* (locked: PLAY plays, #183) */
/* the FX layer's key map (perform.c perf_map_of / perf_map_put: 16 x 5 bits; all 0, every older setting: the default
 * map) in 10 bytes no engine uses, kept with the settings */
#define fx_keys (&favorites.factory[15][14])

static struct {
    uint8_t l, trk, loaded, oct;       /* SET: the layer and the track of the snapshot; EDIT: a sound loaded since
                                        * it opened (SEQ: a tool used); OCT- / OCT+ pressed in a SET layer (bits) */
    uint8_t cook;                      /* SEQ: COOK pressed since it opened */
    uint8_t rp_dirty;                  /* REC: a setting changed (settings_save when the layer lets go); FX: the key map */
    uint8_t fxk;                       /* FX: the white key pressed last in the layer (+ 1; 0 none): PRESETS assigns it */
    uint8_t mute[NPART];               /* EDIT: the DRUM lanes' mutes when it opened (eng_drum.c drum_mute; OCT-) */
    uint32_t eqh;                      /* EDIT: the keys (bits 0..26) of the HOLD quick actions held */
    int16_t v[9];                      /* the values when it opened: GLO mutes, levels, BPM; SCL ROOT..TRN, CHRD VOIC;
                                        * SEQ LEN DIV SWING GATE; REC the settings (ui_rec_prefs) */
    uint32_t solo;                     /* GLO: the keys held that solo */
    uint32_t tap[4];                   /* GLO TAP: the last taps (fm1_ms) */
    uint8_t ntap;
    uint8_t quiet;                     /* #39: a layer closed after it opened: KNOB 1..4 do nothing until quiet_t + */
    uint32_t quiet_t;                  /* LY_QUIET_MS (fm1_ms) */
    uint8_t dt_l, dt_hint, dtap;       /* #83: the layer of the last tap that opened a page (0 none), its hint said; the
                                        * press now armed is that tap's second (a double tap) */
    uint32_t dt_ms;                    /* .. when that tap was let go (fm1_ms) */
    struct { uint8_t home, page, act, seq, fam[FAM_COUNT], rec, go; } nv;   /* .. the page it left (put back by the
                                        * lock); REC: the arming before its tap, the transport that tap started */
} lys;
#define LY_QUIET_MS 250u
#define LY_DTAP_MS 300u                /* #83: the second press of a double tap at most this long after the first tap */

static int layer_allowed(void) { return !ui.menu && !ui.confirm && !ui.uboot && !name_on(); }
static uint32_t ly_bit(uint32_t l) { return 1u << panel.btn[LAYERS[l].btn]; }
static uint32_t ly_down(uint32_t l) { return l && ((fm1_in.buttons & ly_bit(l)) != 0u || ui.lock == l); }   /* (locked: held) */
static uint32_t layer_bits(void)
{
    uint32_t l, m = 0;
    for (l = LAYER_FX; l < LAYER_N; l++)
        m |= ly_bit(l);                                 /* (1.2: every layer on every page, SEQ's too) */
    return m;
}
static uint32_t layer_btn(void) { return LAYERS[ui.layer % LAYER_N].btn; }
static const char *layer_head(void) { return ui.layer == LAYER_FX && perf_latch_on ? "[FX] LATCH" : LAYERS[ui.layer % LAYER_N].head; }
static uint32_t layer_open(void) { return ui.ly && (ui.ly_t0 & LY_OPEN) && ly_down(ui.ly) && layer_allowed() ? ui.ly : 0u; }
static int layer_set_open(void)                 /* (FX LATCH: FX is one too, OCT- turns all off) */
{
    return layer_open() && (LAYERS[layer_open()].kind == LK_SET || (layer_open() == LAYER_FX && perf_latch_on));
}
static uint32_t layer_held(void) { return ui.ly && ly_down(ui.ly) && layer_allowed(); }

/* each pass, before the keys: what the ISR (seq.c keyboard_block) gives to a layer. Armed: its button; none
 * armed: any layer button not already down (one pressed meanwhile is armed below; one held over from before
 * is ignored, its keys stay notes); armed but dead (pressed under a dialog, a menu or NAME that has closed
 * since): nothing, its keys stay notes and FX starts no effect with no map shown; FX's effects only with FX */
static void layer_masks(void)
{
    uint32_t m = !layer_allowed() || (ui.ly && (ui.ly_t0 & LY_DEAD)) ? 0u
               : ui.ly ? ly_bit(ui.ly) : layer_bits() & ~fm1_in.buttons;
    kb_mask = m;
    perf_mask = m & ly_bit(LAYER_FX);
    kb_lock = (uint8_t)(m && ui.lock ? 1u | (ui.lock == LAYER_FX ? 2u : 0u) : 0u);   /* (no button held: the ISR's) */
}

/* a layer button pressed (its edge) while none is armed: it is now, a dead one where no layer opens */
static void layer_arm(uint32_t pressed, uint32_t now)
{
    uint32_t l;
    if (ui.ly)
        return;
    for (l = LAYER_FX; l < LAYER_N; l++)
        if (pressed & ly_bit(l)) {
            ui.ly = (uint8_t)l;
            ui.ly_t0 = (now & ~15u) | 1u | (layer_allowed() ? 0u : LY_DEAD);
            lys.dtap = lys.dt_l == l && fm1_ms - lys.dt_ms <= LY_DTAP_MS;   /* (#83: the second tap of a double tap?) */
            lys.dt_l = 0;
            return;
        }
}

/* the armed layer lets go (its button up, or its lock closed): FX's macros snap back (FX LATCH: they stay); one that
 * opened, or a combo: KNOB 1..4 quiet for a while (#39: the knob still turning is not the page's) */
static void layer_let_go(uint32_t quiet)
{
    if (ui.ly == LAYER_FX && !perf_latch_on)
        perf_k[0] = perf_k[1] = perf_k[2] = perf_k[3] = 0;
    if (lys.rp_dirty) {                                 /* REC: its settings kept, as MENU does when it closes (FX: the map) */
        lys.rp_dirty = 0;
        settings_save();                                /* (deferred while playing) */
    }
    if (quiet) {
        lys.quiet = 1;
        lys.quiet_t = fm1_ms;
    }
    ui.ly_t0 = 0;
    ui.ly = 0;
    ui.lock = 0;
}

/* #83, each pass before the layer's buttons are read: a locked layer closes. Its own button pressed: the lock goes,
 * the button held keeps it open (a peek: let go, it closes, no tap). Another button but PLAY, REC and OCT- / OCT+
 * (another page button, another layer's, HOME, SAVE, SEQ): closed now, and that button acts as always. Any other
 * button pressed between the taps of a double tap: no double tap */
static uint32_t fx_key_held(void);
static void layer_lock_input(uint32_t pressed)
{
    uint32_t keep = 1u << panel.btn[B_PLAY] | 1u << panel.btn[B_REC] | 1u << panel.btn[B_OCTDN] | 1u << panel.btn[B_OCTUP];
    if (ui.lock == LAYER_FX && fx_key_held())           /* (FX's lock, a key held: EDIT puts its default back) */
        keep |= 1u << panel.btn[B_EDIT];
    if (lys.dt_l && (pressed & ~ly_bit(lys.dt_l)))
        lys.dt_l = 0;
    if (!ui.lock)
        return;
    if (pressed & ly_bit(ui.lock))
        ui.lock = 0;
    else if (pressed & ~keep)
        layer_let_go(1);
}
static int layer_locked(void) { return ui.lock && ui.layer == ui.lock; }   /* (the map's header: the lock's mark) */

static void layer_opened(uint32_t l);
/* #83: the second tap of a double tap: the page the first one left comes back, and the layer locks open over it */
static void layer_lock(uint32_t l, uint32_t now)
{
    ui.home = lys.nv.home;
    ui.page = lys.nv.page;
    memcpy(ui.fam_last, lys.nv.fam, sizeof ui.fam_last);
    ui.act = lys.nv.act;
    song.seq_mode = lys.nv.seq;
    ui.entry_open = 0;
    if (lys.dt_hint || l == LAYER_REC)
        ui.msg_t = 0;                                   /* (the first tap's "HOLD [..] QUICK", REC's) */
    if (l == LAYER_REC) {                               /* REC: the arming as before the first tap, and the */
        song.rec = lys.nv.rec;                          /* transport it started stopped again */
        if (lys.nv.go && transport_req == 1u)
            transport_req = 0;
        else if (lys.nv.go && (song.playing || seq_counting()))
            transport_req = 2;
        ui.force = 1;
    }
    ui.lock = (uint8_t)l;
    ui.ly_t0 = (now & ~15u) | 1u | LY_OPEN;
    ui.force = 1;
    layer_opened(l);
}

/* the layer opened: once seen, no more hint; SET: what OCT- will put back */
static void layer_opened(uint32_t l)
{
    uint32_t k;
    if (!((layer_seen >> l) & 1u)) {
        layer_seen |= (uint8_t)(1u << l);
        settings_save();                                /* (deferred while playing) */
    }
    lys.l = (uint8_t)l;
    lys.trk = song.sel;
    lys.loaded = 0;
    lys.cook = 0;
    if (l == LAYER_SEQ || l == LAYER_REC) {             /* the track as it is now (OCT-) */
        memcpy(tl_open.step, TSEL->step, sizeof tl_open.step);
        motion_snapshot_track(TSEL, &tl_open.mo);
    }
    for (k = 0; k < 4u; k++)
        if (l == LAYER_SEQ) {
            lys.v[k] = TSEL->p[P_SLEN + k];
        } else if (l == LAYER_GLO) {
            lys.v[k] = trk[k].p[P_MUTE];
            lys.v[4u + k] = trk[k].p[P_LEVEL];
        } else {
            lys.v[k] = TSEL->p[P_ROOT + k];
            lys.v[4u + k] = k < 2u ? TSEL->p[P_CHRD + k] : 0;
        }
    lys.v[8] = song.g[G_BPM];
    memcpy(lys.mute, drum_mute, sizeof lys.mute);
    if (l == LAYER_REC)
        lys.v[0] = ui_rec_prefs;
}

/* the armed button: 0, or the layer whose button was tapped (let go) */
static uint32_t layer_gesture(uint32_t now, uint32_t combo)
{
    uint32_t *t0 = &ui.ly_t0, l = ui.ly;
    if (!l)
        return 0;
    if (!layer_allowed()) {
        *t0 |= LY_DEAD;
        ui.lock = 0;                                    /* (the menu, a dialog, NAME: the lock closes) */
    }
    if (ly_down(l)) {
        if (!(*t0 & (LY_OPEN | LY_DEAD)) &&
            (combo || now - (*t0 & ~15u) >= (uint32_t)HOLD_MS[settings_hold % 4u] * 1000u * FM1_TICKS_PER_US)) {
            *t0 |= LY_OPEN | (combo ? LY_COMBO : 0u);
            layer_opened(l);
        }
        return 0;
    }
    l = *t0 & (LY_OPEN | LY_DEAD) || combo ? 0u : l;   /* (a knob turned as it was let go: a combo, no tap) */
    if (l && lys.dtap) {                                /* #83: a double tap: locked open (no tap) */
        lys.dtap = 0;
        layer_lock(l, now);
        return 0;
    }
    layer_let_go(*t0 & LY_OPEN || combo);
    return l;
}

/* #39: KNOB 1..4 belong to no page while a layer lets go: the frame its button is let go (the turns read then
 * were made with it held), while its map still shows (its keys held after it), and LY_QUIET_MS after it closed.
 * Their turns are dropped there (ui_input); before, they edited the page under the layer (an ARP page: ARP on) */
static int layer_knobs_quiet(void)
{
    if (lys.quiet && fm1_ms - lys.quiet_t >= LY_QUIET_MS)
        lys.quiet = 0;
    return layer_allowed() && (ui.ly || ui.layer || lys.quiet);
}

/* the map shows while the button is held open, and after it while its keys are still held */
static void layer_show(void)
{
    uint32_t show = layer_allowed() ? (layer_open() ? layer_open() : kb_layer ? ui.layer : 0u) : 0u;
    if (show != ui.layer) {
        ui.layer = (uint8_t)show;
        ui.force = 1;
    }
}

/* a tap: the button's page; the hint until its layer has been opened once (unless the tap said something, or
 * opened NAME: EDIT on USER / PROJECT renames; never over a dialog or the menu) */
static void layer_tap(uint32_t l)
{
    uint8_t m = ui.msg_t;
    int acted;
    lys.nv.home = ui.home;                              /* (#83: the page this tap leaves, for a double tap) */
    lys.nv.page = ui.page;
    lys.nv.act = ui.act;
    lys.nv.seq = song.seq_mode;
    memcpy(lys.nv.fam, ui.fam_last, sizeof lys.nv.fam);
    lys.nv.rec = song.rec;
    if (l == LAYER_REC) {                               /* REC: arms / disarms (no page); said nothing: no double tap */
        uint32_t idle = !song.playing && !seq_counting() && transport_req != 1u;
        acted = !rec_tap();
        lys.nv.go = (uint8_t)(idle && transport_req == 1u);
    } else {
        acted = page_tap(LAYERS[l].btn);
    }
    lys.dt_hint = 0;
    if (!((layer_seen >> l) & 1u) && ui.msg_t == m && !name_on() && !ui.confirm && !ui.menu) {
        ui_say("HOLD [", KC[LY_KC[l]].label);
        str_cpy(ui.msg + str_len(ui.msg), "] QUICK", sizeof ui.msg - str_len(ui.msg));
        ui.msg_t = 90;                                  /* ~1.5 s */
        lys.dt_hint = 1;
    }
    if (!acted && !name_on() && !ui.confirm && !ui.menu) {   /* a page opened: a second tap soon locks the layer */
        lys.dt_l = (uint8_t)l;
        lys.dt_ms = fm1_ms;
    }
}

/* EDIT: a sound load in the layer. The first one takes the undo copy as the track is now (when the layer
 * opened, unless KNOB 2 / 3 had loaded already), so OCT- and SAVE held go back to it */
static uint32_t snd_id(void) { return TSEL->eng_req | (uint32_t)TSEL->preset << 8 | (uint32_t)TSEL->user << 16; }
static void edit_load(uint32_t e, int32_t step)
{
    uint32_t id = snd_id();
    if (!lys.loaded)
        undo.keep = 0;
    if (e < NENGINES) {
        if (e == TSEL->eng_req % NENGINES)
            return;                                     /* its engine already: the sound stays */
        select_engine(e);
        preset_hinted();
    } else {
        eng_list_step(step);
    }
    lys.loaded |= snd_id() != id;
}

/* GLO TAP: from the third tap the tempo of the taps (the last 4); 2 s without one starts over. INT clock only */
static void glo_tap(void)
{
    uint32_t n = lys.ntap, i;
    if (song.g[G_CLOCK]) {
        ui_message("TAP: CLK IS EXT");
        return;
    }
    if (n && fm1_ms - lys.tap[n - 1u] > 2000u)
        n = 0;
    if (n == 4u) {
        for (i = 0; i < 3u; i++)
            lys.tap[i] = lys.tap[i + 1u];
        n = 3;
    }
    lys.tap[n++] = fm1_ms;
    lys.ntap = (uint8_t)n;
    if (n >= 3u && lys.tap[n - 1u] != lys.tap[0]) {
        song.g[G_BPM] = (int16_t)clamp((int32_t)(60000u * (n - 1u) / (lys.tap[n - 1u] - lys.tap[0])),
                                       GP[G_BPM].min, GP[G_BPM].max);
        ui.bpm_t = 40;
    }
}

/* SEQ TOOLS: white key place p's action on the selected track (ui_tools.c), "STOP TO EDIT" while a song plays */
static void tools_key(uint32_t p)
{
    const char *m;
    char n[4];
    if (!tl_cell(TSEL, p))
        return;
    if (chain_busy()) {
        ui_message("STOP TO EDIT");
        return;
    }
    if (!(m = tl_do(TSEL, p, ui.lane)))
        return;
    if (p == TL_COOK && lys.cook < 99u) {               /* "COOKED 3": the third since the layer opened */
        lys.cook++;
        fmt_int(n, lys.cook);
        ui_say("COOKED ", n);
    } else {
        ui_message(m);
    }
    if (!lys.loaded)                                    /* (the first one: how to take it back) */
        str_cpy(ui.msg2, "[SAVE] HOLD TO UNDO", sizeof ui.msg2);
    lys.loaded = 1;
    ui.force = 1;
}

/* REC: CLEAR, the selected track's steps and automation (and ARP's latched chord): one undo (SAVE held), no dialog,
 * as SEQ TOOLS' CLEAR (up to 1.1.4 REC held on SEQ asked first, CF_CLEAR_SEQ). Its own undo copy (not a sound load's) */
static void rec_clear(void)
{
    track_t *t = TSEL;
    if (chain_busy()) {
        ui_message("STOP TO EDIT");
        return;
    }
    if (seq_is_empty(t) && !motion_count(t)) {
        ui_message("NOTHING TO CLEAR");
        return;
    }
    undo.keep = 0;
    load_begin(t, UNDO_PAT);
    track_defaults_steps(t);
    load_end(t);
    t->nheld = 0;
    t->arp_phys = 0;
    ui_message("SEQUENCE CLEARED");
    if (!lys.loaded)                                    /* (the first one: how to take it back) */
        str_cpy(ui.msg2, "[SAVE] HOLD TO UNDO", sizeof ui.msg2);
    lys.loaded = 1;
    ui.force = 1;
}
/* REC's settings, row r (G3 A3 B3, KNOB 1..3): CLICK, COUNT-IN, CLICK LEVEL as MENU > AUDIO steps them (ui.c rp_*) */
static const uint8_t RL_F[3] = {RP_CLICK, RP_COUNTIN, RP_LEVEL};
static const char *const RL_LABEL[3] = {"CLICK", "COUNT", "LEVEL"};   /* (COUNT-IN: too wide for a card or a key cell) */
static const uint8_t RL_ICON[3] = {ICON_TEMPO, ICON_TIME, ICON_LEVEL};
static const char *rl_name(uint32_t r, uint32_t v)
{
    return (r == 0u ? CLICK_N : r == 1u ? COUNTIN_N : CLKLVL_N)[v % 3u];
}
static void rec_set(uint32_t r, uint32_t v)
{
    if (v == rp_get(RL_F[r]))
        return;
    rp_put(RL_F[r], v);
    lys.rp_dirty = 1;
}
static void rec_key(uint32_t p)
{
    uint32_t r = p - 1u, v;
    if (p == 0u) {
        rec_clear();
        return;
    }
    v = (rp_get(RL_F[r]) + 1u) % 3u;                    /* the next value, round */
    rec_set(r, v);
    ui_say(r == 2u ? "CLICK LEVEL " : r ? "COUNT-IN " : "CLICK ", rl_name(r, v));
}

/* FX: the effects' names (the message; NONE: PF_N) and the map's cells' */
static const char *const PF_NAME[PF_NFX + 1] = {"REPEAT 1/8", "REPEAT 1/16", "REPEAT 1/32", "REVERSE", "LPF", "HPF",
    "TAPE STOP", "FREEZE", "OCT UP", "OCT DN", "FLANGER", "PHASER", "REV THROW", "ECHO THROW", "NONE"};
static const char *const PF_CELL[PF_NFX] = {"1/8", "1/16", "1/32", "REV", "LPF", "HPF", "STOP", "FRZ", "OCT+", "OCT-",
    "FLNG", "PHSR", "VERB", "ECHO"};
static const char W_NOTE[16] = {'F', 'G', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'A', 'B', 'C', 'D', 'E', 'F', 'G'};
/* each pass: the map from the settings to the ISR (a change: seq.c makes a key held take its new effect) */
static void fx_map_sync(void)
{
    uint32_t p, e;
    for (p = 0; p < PF_KEYS; p++)
        if (perf_map[p] != (e = perf_map_of(fx_keys, p))) {
            perf_map[p] = (uint8_t)e;
            perf_remap = 1;
        }
}
/* the white key PRESETS assigns: the last pressed in the FX layer, while still held (+ 1; 0 none) */
static uint32_t fx_key_held(void)
{
    return lys.fxk && ui.ly == LAYER_FX && ((kb_layer >> (lys.fxk - 1u)) & 1u) ? lys.fxk : 0u;
}
/* "B4 FLANGER": white key k's note (F3 .. G5) and effect e */
static void fx_say(uint32_t k, uint32_t e, const char *tail)
{
    uint32_t p = key_place(k);
    char n[4] = {W_NOTE[p % 16u], (char)('3' + (p >= 4u) + (p >= 11u)), ' ', 0};
    ui_say(n, PF_NAME[e < PF_NFX ? e : PF_NFX]);
    str_cpy(ui.msg + str_len(ui.msg), tail, sizeof ui.msg - str_len(ui.msg));
}
/* PRESETS turned (s) in the FX layer: the key held steps through NONE and the effects, round; none held: how */
static void fx_assign(int32_t s)
{
    uint32_t k = fx_key_held(), p, i, n = PF_NFX + 1u;
    if (!k) {
        ui_message("HOLD A KEY: ASSIGN");
        return;
    }
    p = key_place(--k);
    i = perf_map_of(fx_keys, p);                        /* (the list: NONE, then the effects in order) */
    i = i < PF_NFX ? i + 1u : 0u;
    i = (i + (s > 0 ? 1u : n - 1u)) % n;
    perf_map_put(fx_keys, p, i ? i - 1u : PF_N);
    lys.rp_dirty = 1;
    fx_map_sync();
    fx_say(k, i ? i - 1u : PF_N, "");
    ui.force = 1;
}
/* EDIT with a key held in the FX layer: its default effect back */
static void fx_default(void)
{
    uint32_t k = fx_key_held(), p;
    if (!k)
        return;
    p = key_place(--k);
    perf_map_put(fx_keys, p, PF_DEF[p]);
    lys.rp_dirty = 1;
    fx_map_sync();
    fx_say(k, PF_DEF[p], " (DEF)");
    ui.force = 1;
}

/* 1.4.1 ENGINE QUICK ACTIONS: the EDIT layer's black keys (1..11: F#3 .. F#5) do what the selected track's engine
 * offers there: EQA, per engine up to 11, black key n the n-th; past them (and on an engine with none) a key is dark
 * and does nothing. An action: its cell's name, HOLD (on while its key is held: act down, then up; a corner triangle,
 * breathes) or SET (act on each press), its handler and its LED (on: lit). They are performance state: no sound load,
 * no undo copy, not recorded; OCT- puts back what they changed since the layer opened (eqa_put_back). A new engine's
 * actions: a table and its row in EQA, and what OCT- puts back in eqa_put_back.
 *   DRUM  1..8 MUTE of the lanes KICK .. BELL (eng_drum.c drum_mute; the cells the kit's names), latched: lit =
 *         sounding, dark = muted (as GLO's track mutes); the cell of a muted lane as a muted track's (KEY) */
enum { EQ_SHOW_SEL, EQ_SHOW_MUTE };                /* the cell: on = the selection's fill; on = idle, off = KEY */
typedef struct {
    uint8_t kind, show, arg;                       /* LK_HOLD / LK_SET, EQ_SHOW_*, the handler's argument */
    const char *(*name)(const track_t *t, uint32_t arg);   /* the cell's (2 .. 3 characters) */
    void (*act)(track_t *t, uint32_t arg, uint32_t down);  /* SET: pressed (down 1); HOLD: pressed, let go (0) */
    uint32_t (*on)(const track_t *t, uint32_t arg);        /* the LED lit, the cell's state */
} eqa_t;
typedef struct { uint8_t eng, n; const eqa_t *a; } eqa_set_t;

static void eqa_drum_mute(track_t *t, uint32_t l, uint32_t down)
{
    uint32_t k = trk_index(t) % NPART;
    if (!down)
        return;
    drum_mute[k] ^= (uint8_t)(1u << l);
    ui_say(drum_lane_name(t, l), (drum_mute[k] >> l) & 1u ? " MUTED" : " ON");
}
static uint32_t eqa_drum_on(const track_t *t, uint32_t l) { return !((drum_mute[trk_index(t) % NPART] >> l) & 1u); }
#define EQA_LANE(l) {LK_SET, EQ_SHOW_MUTE, l, drum_lane_abbr, eqa_drum_mute, eqa_drum_on}
static const eqa_t EQA_DRUM[NLANE] = {EQA_LANE(0), EQA_LANE(1), EQA_LANE(2), EQA_LANE(3), EQA_LANE(4), EQA_LANE(5),
    EQA_LANE(6), EQA_LANE(7)};
static const eqa_set_t EQA[] = {
    {ENGI_DRUM, NLANE, EQA_DRUM},
};
typedef char eqa_fits[NLANE <= 11 ? 1 : -1];       /* (11 black keys) */

static uint32_t eqa_count(const track_t *t)        /* the actions of t's engine (black keys 1..n) */
{
    uint32_t i, e = t->eng_req % NENGINES;
    for (i = 0; i < NELEM(EQA); i++)
        if (EQA[i].eng == e)
            return EQA[i].n;
    return 0;
}
static const eqa_t *eqa_of(const track_t *t, uint32_t p)   /* black key p's (0 = F#3), 0 none */
{
    uint32_t i, e = t->eng_req % NENGINES;
    for (i = 0; i < NELEM(EQA); i++)
        if (EQA[i].eng == e)
            return p < EQA[i].n ? &EQA[i].a[p] : 0;
    return 0;
}
/* black key k (its place p) pressed in the EDIT layer */
static void eqa_key(uint32_t k, uint32_t p)
{
    const eqa_t *a = eqa_of(TSEL, p);
    if (!a)
        return;
    if (a->kind == LK_HOLD)
        lys.eqh |= 1u << k;
    a->act(TSEL, a->arg, 1);
    ui.force = 1;
}
/* each pass: the HOLD actions whose keys were let go */
static void eqa_release(void)
{
    uint32_t r = lys.eqh & ~kb_layer, k;
    const eqa_t *a;
    lys.eqh &= ~r;
    for (k = 0; r && k < 27u; k++)
        if (((r >> k) & 1u) && (a = eqa_of(TSEL, key_place(k))) != 0 && a->kind == LK_HOLD) {
            a->act(TSEL, a->arg, 0);
            ui.force = 1;
        }
}
/* OCT- in the EDIT layer: the actions' state as the layer opened (1 = something changed) */
static int eqa_put_back(void)
{
    int d = memcmp(drum_mute, lys.mute, sizeof drum_mute) != 0;
    memcpy(drum_mute, lys.mute, sizeof drum_mute);
    return d;
}
/* the map's signature of the actions: their LEDs and names */
static uint32_t eqa_sig(void)
{
    uint32_t i, n = eqa_count(TSEL), h = n;
    for (i = 0; i < n; i++) {
        const eqa_t *a = eqa_of(TSEL, i);
        h = h * 33u + a->on(TSEL, a->arg) + (uint32_t)(uintptr_t)a->name(TSEL, a->arg) * 7u;
    }
    return h;
}

/* a key pressed in layer l (k: 0 = F3 .. 26 = G5) */
static void layer_key(uint32_t l, uint32_t k)
{
    uint32_t p = key_place(k), i;
    if (l == LAYER_FX) {
        if (!key_black(k) && p < PF_KEYS)
            lys.fxk = (uint8_t)(k + 1u);                /* (PRESETS assigns it while held) */
    } else if (l == LAYER_GLO) {
        if (key_black(k)) {
            if (p < NTRK)
                trk[p].p[P_MUTE] = (int16_t)!trk[p].p[P_MUTE];
        } else if (p < NTRK) {
            lys.solo |= 1u << k;                        /* (layer_masks: perf_solo while held) */
        } else if (p == 4u) {
            for (i = 0; i < NTRK; i++)
                trk[i].p[P_MUTE] = 0;
        } else if (p == 7u) {
            glo_tap();
        } else if (p == 5u) {
            ml_toggle();                                /* D4: MIDI LEARN on / off (midi_learn.c) */
        }
    } else if (l == LAYER_SCL) {
        TSEL->p[P_ROOT] = (int16_t)((k + 5u) % 12u);    /* the key's note name (F3 = F) */
    } else if (l == LAYER_EDIT && !key_black(k)) {
        if (p < NENG_SHOWN && p < LY_INIT)
            edit_load(eng_vis(p), 0);
        else if (p == LY_INIT && chain_busy())
            ui_message("STOP TO EDIT");
        else if (p == LY_INIT)
            confirm_open(CF_INIT_SOUND, song.sel);      /* (the dialog closes the layer) */
    } else if (l == LAYER_EDIT) {
        eqa_key(k, p);                                  /* (black keys: the engine's quick actions) */
    } else if (l == LAYER_REC) {
        if (!key_black(k) && p < 4u)
            rec_key(p);
    } else if (l == LAYER_SEQ) {
        if (!key_black(k))
            tools_key(p);
        else if (p < NLANE && drum_track(TSEL))
            ui.lane = (uint8_t)p;                       /* (black keys 1..8: the lane, as on the grid) */
    }
}
/* each pass: the keys the layer got now; then GLO's solo: the tracks of its keys still held (perform.c) */
static void layer_keys(uint32_t keys)
{
    uint32_t k, l = layer_open() ? layer_open() : ui.layer, s = 0;
    for (k = 0; keys && k < 27u; k++)
        if ((keys >> k) & 1u)
            layer_key(l, k);
    if (lys.eqh)
        eqa_release();
    lys.solo &= kb_layer;
    for (k = 0; k < 27u; k++)
        if ((lys.solo >> k) & 1u)
            s |= 1u << key_place(k);
    perf_solo = (uint8_t)s;
}

/* KNOB 1..4 in the open layer: its own (FX macros, GLO levels, EDIT's sound), else the four of its page */
static void layer_knob(uint32_t k, int32_t s)
{
    uint32_t l = layer_open();
    if (l == LAYER_FX) {                                /* perform.c perf_k, not recorded */
        if (k == 3u && !perf_harm_on())
            s = -s;                                     /* DEPTH (100 - perf_k[3]) rises to the right (#40: it fell) */
        perf_k[k] = (int8_t)clamp(perf_k[k] + accel(EN_K1 + k, s, k ? 100 : 200), k ? 0 : -100, 100);   /* (#126) */
    } else if (l == LAYER_GLO) {                        /* T1..T4 LEVEL, recorded as on MIXER */
        int16_t *vp = &trk[k].p[P_LEVEL];
        *vp = (int16_t)clamp(*vp + accel(EN_K1 + k, s, TP[P_LEVEL].max - TP[P_LEVEL].min), TP[P_LEVEL].min,
                             TP[P_LEVEL].max);
        motion_capture(&trk[k], P_LEVEL, *vp);
        ml_knob(&trk[k], P_LEVEL);                      /* (MIDI LEARN: Tk LEVEL picked) */
    } else if (l == LAYER_EDIT) {                       /* ENG, No., FAV */
        if (k == 0u)
            edit_load(eng_step(TSEL->eng_req, s), 0);
        else if (k == 1u)
            edit_load(NENGINES, s);
        else if (k == 2u)
            preset_mark(s > 0);
    } else if (l == LAYER_REC) {                        /* CLICK, COUNT-IN, LEVEL (KNOB 4: none) */
        if (k < 3u)
            rec_set(k, (uint32_t)clamp((int32_t)rp_get(RL_F[k]) + s, 0, 2));
    } else if (l) {                                     /* the page's knobs, wherever the page is */
        uint8_t h = ui.home, pg = ui.page;
        ui.home = 0;
        ui.page = (uint8_t)page_first(LAYERS[l].fam);
        page_over = ly_page(l);
        edit_param(k, s);
        page_over = 0;
        ui.home = h;
        ui.page = pg;
    }
}

/* PLAY with GLO held open: RESTART (from the top, playing on); stopped: PLAY. 1 = done here. GLO's lock is not
 * held: PLAY starts and stops there as in every layer (#183: locked, PLAY restarted and never stopped) */
static int layer_play(void)
{
    if (layer_open() != LAYER_GLO || !(fm1_in.buttons & ly_bit(LAYER_GLO)) || chain_busy())
        return 0;
    if (song.g[G_CLOCK] && song.playing) {
        ui_message("RESTART: CLK IS EXT");
        return 1;
    }
    transport_req = song.playing ? 3u : 1u;
    return 1;
}

/* OCT- / OCT+ pressed with a SET layer's button down (FX LATCH: FX's) before it opened: a combo, so it opens now,
 * before oct_taps and layer_oct read it (else the press was nobody's: no put back / ALL OFF, no octave) */
static void layer_oct_open(uint32_t pressed)
{
    uint32_t l = ui.ly, oct = 1u << panel.btn[B_OCTDN] | 1u << panel.btn[B_OCTUP];
    if ((pressed & oct) && l && !(ui.ly_t0 & (LY_OPEN | LY_DEAD)) && layer_held() &&
        (LAYERS[l].kind == LK_SET || (l == LAYER_FX && perf_latch_on))) {
        ui.ly_t0 |= LY_OPEN | LY_COMBO;
        layer_opened(l);
    }
}

/* OCT- / OCT+ in a SET layer: no octave; OCT- let go puts back what the layer changed. Returns the taps left */
static uint32_t layer_oct(uint32_t pressed, uint32_t oct)
{
    uint32_t dn = panel.btn[B_OCTDN], up = panel.btn[B_OCTUP], k, mine;
    if (layer_set_open())
        lys.oct |= (uint8_t)(((pressed >> dn) & 1u) | ((pressed >> up) & 1u) << 1);
    mine = oct & lys.oct;
    if (mine & 1u) {                                    /* OCT-: back to when it opened */
        if (lys.l == LAYER_FX) {                        /* (FX LATCH) every effect and macro off */
            perf_latched = 0;
            perf_k[0] = perf_k[1] = perf_k[2] = perf_k[3] = 0;
            ui_message("FX ALL OFF");
        } else if (lys.l == LAYER_EDIT) {
            uint32_t ld = lys.loaded;
            if (ld)
                undo_swap();                            /* (the copy from its first load) */
            lys.loaded = 0;
            if (eqa_put_back() && !ld)                  /* (the quick actions: the lanes' mutes) */
                ui_message("MUTES PUT BACK");
        } else if (lys.l == LAYER_SEQ || lys.l == LAYER_REC) {
            track_t *t = &trk[lys.trk % NTRK];
            if (lys.loaded && chain_busy()) {
                ui_message("STOP TO EDIT");
            } else {
                if (lys.loaded) {                       /* (SAVE held then: the tools / the clear again) */
                    tl_undo_take(t);
                    memcpy(t->step, tl_open.step, sizeof t->step);
                    if (motion_replace_track(t, &tl_open.mo))
                        motion_full = 1;
                }
                if (lys.l == LAYER_REC) {               /* REC: the settings as it opened */
                    lys.rp_dirty |= (uint8_t)(ui_rec_prefs != (uint8_t)lys.v[0]);
                    ui_rec_prefs = (uint8_t)lys.v[0];
                    rp_apply();
                } else {
                    for (k = 0; k < 4u; k++)
                        t->p[P_SLEN + k] = lys.v[k];
                }
                lys.loaded = 0;
                lys.cook = 0;
                ui_message(lys.l == LAYER_REC ? "REC PUT BACK" : "SEQUENCE PUT BACK");
            }
        } else if (lys.l == LAYER_GLO) {
            for (k = 0; k < NTRK; k++) {
                trk[k].p[P_MUTE] = lys.v[k];
                trk[k].p[P_LEVEL] = lys.v[4u + k];
            }
            song.g[G_BPM] = lys.v[8];
            ui_message("MIX PUT BACK");
        } else {
            for (k = 0; k < 4u; k++)
                trk[lys.trk % NTRK].p[P_ROOT + k] = lys.v[k];
            trk[lys.trk % NTRK].p[P_CHRD] = lys.v[4];
            trk[lys.trk % NTRK].p[P_VOIC] = lys.v[5];
            ui_message("SCALE PUT BACK");
        }
    }
    lys.oct &= (uint8_t)(((fm1_in.buttons >> dn) & 1u) | ((fm1_in.buttons >> up) & 1u) << 1);
    return oct & ~mine;
}

/* --------------------------------------------------------- the LEDs --- */
/* lit = in effect now (held, the value, a track sounding), breathing = can be pressed (*br: dark .. ~60 % and
 * back, ~1.1 s, hal/fm1_input.h fm1_led_breath; was a hard 250 ms blink, #119), dark = nothing there */
static int glo_sounding(uint32_t t) { return !trk[t].p[P_MUTE] && (!perf_solo || ((perf_solo >> t) & 1u)); }
static int rec_clearable(void) { return !seq_is_empty(TSEL) || motion_count(TSEL) != 0u; }
static uint32_t layer_leds(uint32_t *br)
{
    uint32_t k, m = 0, n = 0, l = ui.layer, held = perf_held | perf_latched, ok = perf_avail();
    uint32_t mask = scale_mask(TSEL), root = (uint32_t)TSEL->p[P_ROOT] % 12u;
    const eqa_t *a;
    for (k = 0; k < 27u; k++) {
        uint32_t p = key_place(k), b = (uint32_t)key_black(k), on = 0, can = 0, e;
        if (l == LAYER_FX) {                            /* effects breathe, held lit, a too-long REPEAT dark */
            e = perf_key(k);
            can = e < PF_N && ((ok >> e) & 1u);
            on = can && ((held >> e) & 1u);
        } else if (l == LAYER_GLO) {                    /* sounding lit; SOLO held lit, the others, C4, F4 breathe */
            on = b ? p < NTRK && glo_sounding(p) : (p < NTRK && ((lys.solo >> k) & 1u)) || (p == 5u && ui.ml.on);
            can = !b && (p < NTRK || p == 4u || p == 5u || (p == 7u && !song.g[G_CLOCK]));
        } else if (l == LAYER_SCL) {                    /* the root lit, the scale's notes breathe */
            e = (k + 5u + 12u - root) % 12u;
            on = e == 0u;
            can = (mask >> e) & 1u;
        } else if (l == LAYER_EDIT && !b) {             /* the engine lit, the others and INIT breathe */
            on = p < NENG_SHOWN && p < LY_INIT && eng_vis(p) == TSEL->eng_req % NENGINES;
            can = p < NENG_SHOWN && p < LY_INIT ? 1u : p == LY_INIT && !chain_busy();
        } else if (l == LAYER_EDIT && (a = eqa_of(TSEL, p)) != 0) {   /* the quick actions: on lit (DRUM: sounding), */
            on = a->on(TSEL, a->arg) != 0u;             /* a HOLD one off breathes, none dark */
            can = a->kind == LK_HOLD;
        } else if (l == LAYER_SEQ) {                    /* the tools breathe (not while a song plays); DRUM: the lane */
            on = b && p < NLANE && drum_track(TSEL) && p == ui.lane;   /* lit, the other lanes breathe */
            can = b ? p < NLANE && drum_track(TSEL) : tl_cell(TSEL, p) && !chain_busy();
        } else if (l == LAYER_REC && !b && p < 4u) {    /* a setting on (LEVEL: the click on) lit, the rest breathe; */
            on = p == 1u ? rp_get(RP_CLICK) != 0u : p == 2u ? rp_get(RP_COUNTIN) != 0u :   /* CLEAR: something to */
                 p == 3u && rp_get(RP_CLICK) != 0u;    /* clear, not while a song plays */
            can = p ? 1u : !chain_busy() && rec_clearable();
        }
        m |= on << k;
        n |= (can & !on) << k;
    }
    if (br)
        *br = n;
    return m;
}

/* ------------------------------------------------------ the overlay --- */
/* One template: the header names the button and the kind ("[GLO] SET"); the cards are KNOB 1..4; the panel the
 * map of the keys as cells (the key's note name, a Fukiai icon, a name; FX's 16 white keys: the note and the effect's
 * short name, 4 a row); the footer the keycaps. A cell: RAISE
 * (can be pressed), the selection's fill (the value now, SET), the accent (held, HOLD), DIM (cannot now: pressing
 * it says why), KEY (a muted track, as the MUTE badge); HOLD cells in a SET layer: a corner triangle */
static const char B_NOTE[NTRK] = {'F', 'G', 'A', 'C'};  /* black keys 1..4: F# G# A# C# */
#define LC_X(c) (6 + 58 * (int32_t)(c))                 /* cell column c: 54 px wide, 4 px apart */
#define LC_W 54
#define LC_H 42                                          /* the big cells: rows at y 4 and 50 */
#define LF_H 19                                          /* FX: the white keys' rows (F3 .. B3, C4 .. F4, G4 .. C5, D5 .. G5) */
#define LM_Y 96                                          /* the black keys' row (22 px) */
#define LM_H 22
enum { LS_OFF, LS_SEL, LS_HELD, LS_WAIT, LS_DIM, LS_MUTE };
/* 1.5 (#173): | LS_ALT, an idle (OFF, DIM) cell of the FX map's 2nd and 4th columns in a fill of its own, the columns
 * light / dark in turn as the piano roll's keys, so the effects are told apart at a glance: SURF -> TEXT 22 % (the
 * idle RAISE is 12 %); LINE (idle cells unfilled) 10 %; MONO (its one grey) none */
#define LS_ALT 0x10u
static uint16_t lc_alt(void)
{
    return ux.raise == UI_BW_GREY && ux.dim == UI_BW_GREY ? T_RAISE : ux_mix(T_SURF, T_TEXT, ux.style ? 10 : 22);
}

static uint16_t lc_fill(uint32_t st, uint16_t *ink)
{
    uint32_t alt = st & LS_ALT;
    st &= ~LS_ALT;
    *ink = st == LS_DIM ? T_DIM : T_THEME;
    if (st == LS_MUTE) {
        *ink = T_INK;
        return T_KEY;
    }
    if (st == LS_HELD) {
        *ink = T_BG;
        return T_ACCENT;
    }
    if (st == LS_SEL || st == LS_WAIT) {
        *ink = T_INK;
        return T_THEME;
    }
    return alt ? lc_alt() : T_RAISE;
}
/* a cell at x, y, h px high: big (h > 30) the icon over the name; a name: compact, the icon at the right (none: no
 * icon); else a black key's: two icons at the right. tri: a HOLD cell in a SET layer */
/* a cell's box: its fill on the panel. LINE: a RAISE (idle) cell is not filled, and 1 px rules divide the
 * cells: one in the gap left of a cell (not the first column) and one in the gap above it (not the first row), each
 * across the gap's corner, so they meet in a grid. Cells are 4 px apart from x 6, y 4. Returns the fill */
static uint16_t lc_box(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t fill)
{
    if (ux.style) {
        if (fill == T_RAISE)
            fill = T_SURF;
        if (x > 7)                                       /* (not the first column: x 6) */
            cv_rule(x - 2, y > 4 ? y - 4 : y, 1, h + (y > 4 ? 4 : 0));
        if (y > 4)
            cv_rule(x > 7 ? x - 4 : x, y - 2, w + (x > 7 ? 4 : 0), 1);
    }
    cv_rrect(x, y, w, h, 4, fill, T_SURF);
    return fill;
}
static void lcell(int32_t x, int32_t y, int32_t h, const char *note, uint32_t icon, uint32_t icon2, const char *name,
                  uint32_t st, int tri)
{
    uint16_t ink, fill = lc_fill(st, &ink), idle = fill == T_RAISE || ((st & LS_ALT) && fill == lc_alt());
    fill = lc_box(x, y, LC_W, h, fill);
    if (note)
        cv_text_on(x + 5, y + 3, &AF_S, note, idle ? T_MID : ink, fill);
    if (h > 30) {                                   /* the icon's cell over the name, 6 px between, centred */
        int32_t t = y + HALF_UP(h - (16 + 6 + AF_S_CAP_H));
        GFX_HOOK_ALIGN(0, y, 0, y + h, AL_V | AL_N(2), "layer cell icon + name centred up/down");
        GFX_HOOK_ALIGN(x, 0, x + LC_W, 0, AL_H | AL_PASS, "layer cell icon / name centred across");
        cv_icon_in(x, t, LC_W, 0, 16, icon, ink, fill);
        GFX_HOOK_ALIGN(x, 0, x + LC_W, 0, AL_H | AL_PASS, "layer cell icon / name centred across");
        cv_text_in(x, t + 22 - AF_S_CAP_Y, LC_W, &AF_S, name, ink, fill);
    } else if (name && h > 24) {                           /* the same, 12 px, 2 px between (the icon at a side) */
        int32_t t = y + HALF_UP(h - (12 + 2 + AF_S_CAP_H));
        GFX_HOOK_ALIGN(0, y, 0, y + h, AL_V | AL_N(2), "layer cell icon + name centred up/down");
        cv_icon_on(note ? x + LC_W - 17 : x + 5, t, 12, icon, ink, fill);
        GFX_HOOK_ALIGN(x, 0, x + LC_W, 0, AL_H | AL_PASS, "layer cell icon / name centred across");
        cv_text_in(x, t + 14 - AF_S_CAP_Y, LC_W, &AF_S, name, ink, fill);
    } else if (name && *name) {                           /* a key's row: the icon and the name on its middle */
        if (!note && icon < ICON_COUNT) {                  /* (the EDIT layer: the engine's icon instead of the key) */
            GFX_HOOK_ALIGN(0, y, 0, y + h, AL_V, "layer key cell icon / name centred up/down");
            cv_icon_in(x + 3, y, 0, h, 12, icon, ink, fill);
            GFX_HOOK_ALIGN(0, y, 0, y + h, AL_V, "layer key cell icon / name centred up/down");
            cv_text_r(x + LC_W - 3, y + CAP_IN(S, h), &AF_S, name, ink, fill);
        } else {
            GFX_HOOK_ALIGN(0, y, 0, y + h, AL_V, "layer key cell icon / name centred up/down");
            cv_text_r(x + LC_W - 5, y + CAP_IN(S, h), &AF_S, name, ink, fill);
        }
    } else if (!name) {                                    /* (an empty name: the note alone) */
        GFX_HOOK_ALIGN(0, y, 0, y + h, AL_V, "layer key cell icon / name centred up/down");
        cv_icon_in(x + LC_W - 31, y, 0, h, 12, icon, ink, fill);
        GFX_HOOK_ALIGN(0, y, 0, y + h, AL_V, "layer key cell icon / name centred up/down");
        cv_icon_in(x + LC_W - 17, y, 0, h, 12, icon2, ink, fill);
    }
    if (tri) {
        int32_t j;
        for (j = 0; j < 4; j++)
            cv_rect(x + LC_W - 7 + j, y + 2 + j, 4 - j, 1, ink);
    }
}
static void bnote(char *n, uint32_t t) { n[0] = B_NOTE[t]; n[1] = '#'; n[2] = 0; }

static void layer_fx(void)
{
    uint32_t held = perf_kill ? 0u : perf_held | perf_latched, act = perf_act, ok = perf_avail(), p, e, st;
    uint32_t ed = fx_key_held() ? key_place(fx_key_held() - 1u) : PF_KEYS;
    char n[3] = {0, 0, 0};
    for (p = 0; p < PF_KEYS; p++) {                     /* the white keys F3 .. G5, 4 a row: the effect each holds */
        e = perf_map[p];
        st = p == ed ? LS_SEL : e >= PF_NFX || !((ok >> e) & 1u) ? LS_DIM : !((held >> e) & 1u) ? LS_OFF
           : (act >> e) & 1u ? LS_HELD : LS_WAIT;       /* (the key PRESETS assigns: the selection's fill) */
        n[0] = W_NOTE[p];
        lcell(LC_X(p % 4u), 4 + (LF_H + 4) * (int32_t)(p / 4u), LF_H, n, 0, 0, e < PF_NFX ? PF_CELL[e] : "",
              st | (p & 1u ? LS_ALT : 0u), 0);           /* (#173: the 2nd and 4th columns' idle cells their own fill) */
    }
    for (e = 0; e < NTRK; e++) {                        /* the mutes of the black keys 1..4 */
        bnote(n, e);
        lcell(LC_X(e), LM_Y, LM_H, n, ICON_MUTE, trk_icon(e, 0), 0, (held >> (PF_M1 + e)) & 1u ? LS_MUTE : LS_OFF, 0);
    }
}
static void layer_glo(void)
{
    uint32_t e;
    char n[3] = {0, 0, 0};
    for (e = 0; e < NTRK; e++) {                        /* F3 .. B3: SOLO while held */
        n[0] = W_NOTE[e];
        lcell(LC_X(e), 4, LC_H, n, trk_icon(e, 0), 0, "SOLO", (perf_solo >> e) & 1u ? LS_HELD : LS_OFF, 1);
    }
    n[0] = 'C';
    lcell(LC_X(0), 50, LC_H, n, ICON_MUTE, 0, "ALL", LS_OFF, 0);   /* (unmute all) */
    n[0] = 'D';
    lcell(LC_X(1), 50, LC_H, n, ICON_MIDI, 0, "LEARN", ui.ml.on ? LS_SEL : LS_OFF, 0);   /* (MIDI LEARN, 1.5) */
    n[0] = 'F';
    lcell(LC_X(3), 50, LC_H, n, ICON_TEMPO, 0, "TAP", song.g[G_CLOCK] ? LS_DIM : LS_OFF, 0);
    for (e = 0; e < NTRK; e++) {                        /* the black keys 1..4: MUTE, latched */
        bnote(n, e);
        lcell(LC_X(e), LM_Y, LM_H, n, ICON_MUTE, trk_icon(e, 0), 0, trk[e].p[P_MUTE] ? LS_MUTE : LS_OFF, 0);
    }
}
static void layer_scl(void)                             /* KNOB 2's scales, 4 x 4, the one now selected */
{
    uint32_t i, sc = (uint32_t)TSEL->p[P_SCALE];
    for (i = 0; i < 16u && i <= (uint32_t)TP[P_SCALE].max; i++) {
        int32_t x = LC_X(i % 4u), y = 4 + 29 * (int32_t)(i / 4u);
        uint16_t ink, fill = lc_fill(i == sc ? LS_SEL : LS_OFF, &ink);
        fill = lc_box(x, y, LC_W, 25, fill);
        GFX_HOOK_ALIGN(x, y, x + LC_W, y + 25, AL_HV, "scale cell name centred");
        cv_text_in(x, y + CAP_IN(S, 25), LC_W, &AF_S, TP[P_SCALE].names[i], ink, fill);
    }
}
/* 1.4.1: the engine's quick actions (black keys 1..n) in a row of n cells under the engines, 18 px high, 4 px apart
 * (8: 25 px, as SEQ TOOLS' lanes; 11: 17 px): the name; SET EQ_SHOW_MUTE: off a muted track's KEY; a HOLD one's
 * corner triangle */
static void eqa_row(uint32_t n)
{
    uint32_t i, w = (228u - 4u * (n - 1u)) / n;
    for (i = 0; i < n; i++) {
        const eqa_t *a = eqa_of(TSEL, i);
        uint32_t on = a->on(TSEL, a->arg) != 0u, st = a->show == EQ_SHOW_MUTE ? (on ? LS_OFF : LS_MUTE)
                    : on ? (a->kind == LK_HOLD ? LS_HELD : LS_SEL) : LS_OFF;
        int32_t x = 6 + (int32_t)((w + 4u) * i), j;
        uint16_t ink, fill = lc_box(x, 100, (int32_t)w, 18, lc_fill(st, &ink));
        GFX_HOOK_ALIGN(x, 100, x + (int32_t)w, 118, AL_HV, "edit quick action cell name centred");
        cv_text_in(x, 100 + CAP_IN(S, 18), (int32_t)w, &AF_S, a->name(TSEL, a->arg), ink, fill);
        for (j = 0; a->kind == LK_HOLD && j < 4; j++)
            cv_rect(x + (int32_t)w - 7 + j, 102 + j, 4 - j, 1, ink);
    }
}
static void layer_edit(void)                            /* the engines from F3, INIT next (LY_INIT), the sound under them */
{                                                        /* (cells show the engine's icon, not the key's note) */
    uint32_t n = NENG_SHOWN < LY_INIT ? NENG_SHOWN : LY_INIT, cells = n + 1u, i, h = cells > 12u ? 20u : 28u;   /* 4 rows of 20: 8 px clear above the sound row */
    uint32_t na = eqa_count(TSEL);
    for (i = 0; i < cells; i++) {
        int32_t x = LC_X(i % 4u), y = 4 + (int32_t)(h + 4u) * (int32_t)(i / 4u);
        const engine_t *en = ENGINES[eng_vis(i) % NENGINES];
        if (i < n)
            lcell(x, y, (int32_t)h, 0, engine_icon(en->name), 0, h > 24u ? en->name : eng_abbr(en->name),
                  eng_vis(i) == TSEL->eng_req % NENGINES ? LS_SEL : LS_OFF, 0);
        else
            lcell(x, y, (int32_t)h, 0, ICON_X_WARN, 0, "INIT", chain_busy() ? LS_DIM : LS_OFF, 0);
    }
    if (na)                                             /* the black keys' quick actions in the sound's place (KNOB 2 */
        eqa_row(na);                                    /* still shows its number) */
    else
        engine_sound_row(104);
}

/* SEQ TOOLS: the white keys' tools, 4 a row from F3 (the whole sequence; RANDOM COOK BEAT; the lane's), on a DRUM
 * track the lanes of black keys 1..8 under them (the selected one: the selection's fill). A melodic track: the two
 * rows of the sequence's tools only */
static const uint8_t TL_ICON[TL_N] = {ICON_CLEAR, ICON_X_REVERSE, ICON_X_LEFT, ICON_X_RIGHT, ICON_PROB, ICON_W_SH,
    ICON_DRUM, 0, ICON_CLEAR, ICON_X_REVERSE, ICON_STEPS, ICON_PROB};
static const char *const TL_NAME[TL_N] = {"CLEAR", "REVERSE", "SHIFT", "SHIFT", "RANDOM", "COOK", "BEAT", 0,
    "CLR", "REV", "FILL", "RND"};
static void layer_seq(void)
{
    uint32_t p, st = chain_busy() ? LS_DIM : LS_OFF, drum = (uint32_t)drum_track(TSEL);
    char n[3] = {0, 0, 0}, nm[8];
    for (p = 0; p < TL_N; p++) {
        if (!tl_cell(TSEL, p))
            continue;
        n[0] = W_NOTE[p];
        str_cpy(nm, TL_NAME[p], sizeof nm);
        if (p >= TL_LCLR) {                             /* the lane's: "CLR BD" */
            str_cpy(nm + str_len(nm), " ", 2);
            str_cpy(nm + str_len(nm), drum_lane_abbr(TSEL, ui.lane), sizeof nm - str_len(nm));
        }
        lcell(LC_X(p % 4u), 4 + 32 * (int32_t)(p / 4u), 28, n, TL_ICON[p], 0, nm, st, 0);
    }
    for (p = 0; drum && p < NLANE; p++) {               /* the lanes (black keys 1..8): 8 cells of 25 px, 4 px apart */
        int32_t x = 6 + 29 * (int32_t)p;
        uint16_t ink, fill = lc_box(x, 100, 25, 18, lc_fill(p == ui.lane % NLANE ? LS_SEL : LS_OFF, &ink));
        GFX_HOOK_ALIGN(x, 100, x + 25, 118, AL_HV, "tools lane cell name centred");
        cv_text_in(x, 100 + CAP_IN(S, 18), 25, &AF_S, drum_lane_abbr(TSEL, p), ink, fill);
    }
}

/* REC: a row a key (26 px, 4 apart): the key's cell (its note, icon and name), then CLEAR's track and what it holds,
 * or the setting's three values, the one now with the selection's fill (as MENU > AUDIO steps them) */
#define LR_KW 78                                         /* the key's cell; the values 46 px each: x 6 .. 234 */
#define LR_VW 46
#define LR_H 26
static void layer_rec(void)
{
    uint32_t r, v;
    char n[3] = {0, 0, 0}, b[16];
    for (r = 0; r < 4u; r++) {
        int32_t y = 4 + (LR_H + 4) * (int32_t)r, x = 6, vx = 6 + LR_KW + 4;
        uint32_t dim = r == 0u && (chain_busy() || !rec_clearable());
        uint16_t ink, fill = lc_box(x, y, LR_KW, LR_H, lc_fill(dim ? LS_DIM : LS_OFF, &ink));
        n[0] = W_NOTE[r];
        GFX_HOOK_ALIGN(0, y, 0, y + LR_H, AL_V, "rec layer key cell on its middle");
        cv_text_on(x + 5, y + CAP_IN(S, LR_H), &AF_S, n, fill == T_RAISE || fill == T_SURF ? T_MID : ink, fill);
        GFX_HOOK_ALIGN(0, y, 0, y + LR_H, AL_V, "rec layer key cell on its middle");
        cv_icon_in(x + 15, y, 0, LR_H, 12, r ? RL_ICON[r - 1u] : ICON_CLEAR, ink, fill);
        GFX_HOOK_ALIGN(0, y, 0, y + LR_H, AL_V, "rec layer key cell on its middle");
        cv_text_r(x + LR_KW - 5, y + CAP_IN(S, LR_H), &AF_S, r ? RL_LABEL[r - 1u] : "CLEAR", ink, fill);
        if (r == 0u) {                                   /* "T2 SEQUENCE" / "T2 EMPTY" (a song playing: why not) */
            int32_t w = 234 - vx;
            b[0] = 'T';
            b[1] = (char)('1' + song.sel);
            str_cpy(b + 2, rec_clearable() ? " SEQUENCE" : " EMPTY", sizeof b - 2);
            if (chain_busy())
                str_cpy(b, "STOP TO EDIT", sizeof b);
            fill = lc_box(vx, y, w, LR_H, lc_fill(dim ? LS_DIM : LS_OFF, &ink));
            GFX_HOOK_ALIGN(vx, y, vx + w, y + LR_H, AL_HV, "rec layer clear cell centred");
            cv_text_in(vx, y + CAP_IN(S, LR_H), w, &AF_S, b, ink, fill);
            continue;
        }
        for (v = 0; v < 3u; v++, vx += LR_VW + 4) {
            fill = lc_box(vx, y, LR_VW, LR_H, lc_fill(v == rp_get(RL_F[r - 1u]) ? LS_SEL : LS_OFF, &ink));
            GFX_HOOK_ALIGN(vx, y, vx + LR_VW, y + LR_H, AL_HV, "rec layer value cell centred");
            cv_text_in(vx, y + CAP_IN(S, LR_H), LR_VW, &AF_S, rl_name(r - 1u, v), ink, fill);
        }
    }
}

static void layer_cards(uint32_t l)
{
    char val[12];
    const char *unit;
    uint32_t c;
    if (l == LAYER_FX) {                                /* FILTER CRUSH THROW DEPTH */
        int32_t m = perf_k[0];
        fmt_int(val, m < 0 ? -m : m);
        draw_column(0, "FILTER", m ? val : "OFF", m < 0 ? "LP" : m > 0 ? "HP" : "", m ? VAL(0u) : T_DIM, (m + 100) * 5,
                    ICON_CUTOFF);
        fmt_int(val, perf_k[1]);
        draw_column(1, "CRUSH", perf_k[1] ? val : "OFF", perf_k[1] ? "%" : "", perf_k[1] ? VAL(1u) : T_DIM,
                    perf_k[1] * 10, ICON_BITS);
        fmt_int(val, perf_k[2]);
        draw_column(2, "THROW", perf_k[2] ? val : "OFF", perf_k[2] ? "%" : "", perf_k[2] ? VAL(2u) : T_DIM,
                    perf_k[2] * 10, ICON_DELAY);
        if (perf_harm_on()) {                           /* OCT UP / DN playing: KNOB 4 is its shimmer (SHIMR) */
            fmt_int(val, perf_k[3]);
            draw_column(3, "SHIMR", perf_k[3] ? val : "OFF", perf_k[3] ? "%" : "", perf_k[3] ? VAL(3u) : T_DIM,
                        perf_k[3] * 10, ICON_FEEDBACK);
        } else {
            fmt_int(val, 100 - perf_k[3]);
            draw_column(3, "DEPTH", val, "%", VAL(3u), (100 - perf_k[3]) * 10, ICON_MIX);
        }
    } else if (l == LAYER_GLO) {                        /* T1..T4 LEVEL (the track's icon; muted: dim) */
        for (c = 0; c < NTRK; c++) {
            param_format(&TP[P_LEVEL], trk[c].p[P_LEVEL], val, &unit);
            draw_column(c, "LEVEL", val, unit, glo_sounding(c) ? VAL(c) : T_DIM, RATIO(&TP[P_LEVEL], trk[c].p[P_LEVEL]),
                        trk_icon(c, 1));
        }
    } else if (l == LAYER_EDIT) {
        engine_columns();
    } else if (l == LAYER_REC) {                        /* CLICK COUNT-IN LEVEL, an empty card */
        for (c = 0; c < 3u; c++) {
            uint32_t v = rp_get(RL_F[c]);
            draw_column(c, RL_LABEL[c], rl_name(c, v), "", c < 2u && !v ? T_DIM : VAL(c), (int32_t)v * 500, RL_ICON[c]);
        }
        draw_column(3, "", "", "", T_THEME, -1, ICON_NONE);
    } else {                                            /* the page's four (SCL: LY_SCL) */
        uint8_t h = ui.home, pg = ui.page;
        ui.home = 0;
        ui.page = (uint8_t)page_first(LAYERS[l].fam);
        page_over = ly_page(l);
        draw_columns();
        page_over = 0;
        ui.home = h;
        ui.page = pg;
    }
}

static void draw_layer(void)
{
    uint32_t l = ui.layer % LAYER_N, sig = l * 7919u + ux.gen * 977u;
    static uint8_t fx_foot;                             /* FX: the footer for a key held (PRESETS ASSIGN, EDIT DEFAULT) */
    if (fx_foot != (l == LAYER_FX && fx_key_held())) {
        fx_foot = (uint8_t)(l == LAYER_FX && fx_key_held());
        ui.force = 1;
    }
    layer_cards(l);
    if (l == LAYER_FX)
    {
        uint32_t p;
        sig += (perf_kill ? 0u : perf_held | perf_latched) * 31u + perf_latch_on * 11u + perf_act * 131u + perf_avail() * 7u + (uint32_t)perf_harm_on() * 3u +
               fx_key_held() * 4099u;
        for (p = 0; p < PF_KEYS; p++)                   /* (the key map) */
            sig = sig * 33u + perf_map[p];
    }
    else if (l == LAYER_GLO)
        sig += perf_solo * 31u + (uint32_t)song.g[G_CLOCK] * 5u + ui.ml.on * 7u +
               (uint32_t)(trk[0].p[P_MUTE] | trk[1].p[P_MUTE] << 1 | trk[2].p[P_MUTE] << 2 | trk[3].p[P_MUTE] << 3) * 131u;
    else if (l == LAYER_SCL)
        sig += (uint32_t)TSEL->p[P_SCALE] * 31u;
    else if (l == LAYER_REC)
        sig += (uint32_t)ui_rec_prefs * 31u + (uint32_t)chain_busy() * 3u + (uint32_t)rec_clearable() * 5u + song.sel * 977u;
    else if (l == LAYER_SEQ)
        sig += (uint32_t)drum_track(TSEL) * 31u + ui.lane * 5u + (uint32_t)chain_busy() * 3u + (uint32_t)TSEL->p[P_E0] * 131u + song.sel * 977u;
    else
        sig += snd_id() * 31u + (uint32_t)preset_favorite() * 5u + (uint32_t)chain_busy() * 3u + up_gen * 101u +
               eqa_sig() * 2654435761u;
    if (ui.force || sig != ui.layer_sig) {
        ui.layer_sig = sig;
        cv_begin(240, H_GRAPH, T_BG);
        cv_rrect(3, 0, 234, H_GRAPH, 5, T_SURF, T_BG);
        cv_bg = T_SURF;
        if (l == LAYER_FX)
            layer_fx();
        else if (l == LAYER_GLO)
            layer_glo();
        else if (l == LAYER_SCL)
            layer_scl();
        else if (l == LAYER_SEQ)
            layer_seq();
        else if (l == LAYER_REC)
            layer_rec();
        else
            layer_edit();
        cv_blit(0, Y_GRAPH);
    }
    if (ui.force) {                                     /* the footer: what the keys, knobs and buttons do */
        cv_begin(240, H_FOOT, T_BG);
        const khint_t *ft = l == LAYER_FX && fx_key_held() ? FX_KEY_FOOT : l == LAYER_FX && perf_latch_on ? FX_LATCH_FOOT :
                            l == LAYER_GLO && ui.lock == LAYER_GLO ? GLO_LOCK_FOOT : LAYERS[l].foot;
        int h = help_on() && help_row(HELP_LAYER[l], 21);   /* 1.5, HELP ON: a second row, what this one does not say */
        cv_key_row(8, 232, h ? 2 : 9, ft, ft[2].act ? 3u : 2u, 7u, T_BG);
        cv_blit(0, Y_FOOT);
        ui.foot_sig = 0;
    }
}
