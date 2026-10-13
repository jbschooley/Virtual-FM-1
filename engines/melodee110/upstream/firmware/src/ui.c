/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* MELODEE user interface.
 * Flat: SURF cards and panels on the palette's background, no rules, one type family (Rubik, three sizes), tracks
 * named by circled numerals. Four columns <-> KNOB 1..4. Rendering is lazy:
 * every element remembers what it last drew and is redrawn only on change. */
static int live_rec_sel(void);
static int project_save(uint32_t slot);
static void fm6_store(uint32_t k);                   /* FM6's STORE page: fm6_store.c */
static void fm6_send(void);
static void fm6_init_voice(void);
static void panel_setup(void);
static void project_load(uint32_t slot);
static int project_used(uint32_t slot);
static int template_used(void);                 /* SLOT TMPL (PROJ_TMPL): project.c */
static void template_save(void);
static void template_load(void);
static uint32_t chain_prepare(void);
static int up_used(uint32_t k);              /* user presets: upreset.c */
static int up_load(uint32_t k);
static uint32_t up_count(void);
static uint32_t up_nth(uint32_t n);
static uint32_t up_rank(uint32_t slot);
static uint32_t up_engine(uint32_t k);
static void up_name(uint32_t k, char *b);
static void up_slot_label(char *b, uint32_t k);
static void up_ui(uint32_t op, uint32_t k);
static void up_auto_name(char *b, uint32_t e, uint32_t k);   /* naming (ui_name.c) */
static void up_ui_named(uint32_t op, uint32_t k, const char *name);
static uint32_t native_limit(uint32_t e);
static int native_used(uint32_t e, uint32_t k);
static uint32_t native_count(uint32_t e);
static uint32_t native_nth(uint32_t e, uint32_t n);
static uint32_t native_rank(uint32_t e, uint32_t k);
static void native_name(uint32_t e, uint32_t k, char *out);
static int native_load(uint32_t e, uint32_t k, uint32_t tr);
static const uint8_t *native_raw(uint32_t e, uint32_t k);
static void p5_send(uint32_t tr);
static int native_store(uint32_t e,uint32_t k,uint32_t tr,const char *name);
static int native_put(uint32_t e, uint32_t k, const uint8_t *raw);
static int native_fm_active(void);
static uint32_t user_limit(void);
static int user_used(uint32_t k);
static void user_name(uint32_t k, char *out);
static void user_label(char *out, uint32_t k);
static void user_ui_named(uint32_t op, uint32_t k, const char *name);
static void slot_save_sheet(void);              /* ui_slots.c */
static int project_save_as(uint32_t slot, const char *name);
static int project_name(uint32_t slot, char *b);
static int project_rename(uint32_t slot, const char *name);
static int project_erase(uint32_t slot);
static void project_cur_name(char *b);
static uint32_t user_of(const track_t *t)    /* user preset slot its sound came from, USER_NONE = none */
{
    return t->user && (t->user_native ? native_used(t->eng_req,t->user-1u) : up_used(t->user-1u)) ? t->user-1u : USER_NONE;
}
static uint32_t up_gen;                      /* bumped on every user bank change (redraws) */
static uint8_t browse_loads, browse_mark;    /* sound loads so far; at the browser's opening (ui_browser.c: back) */
#include "favorites.c"
/* MENU's two-valued settings in a byte no engine uses (favorites.factory[15][30], saved with the settings): bit 0 FX
 * LATCH (settings_persist.c settings_latch), bit 1 KNOB ACCEL OFF (ui_input.c accel_by; clear in older settings = ON) */
#define PREF_BITS (favorites.factory[15][30])
#define PREF_ACCEL_OFF 2u

static uint8_t sync_reload;                  /* engine / preset / project / user preset loaded: editor RELOAD push */

#define ACC T_THEME                /* values, curves */
#define VAL(c) ((void)(c), T_TEXT)   /* a value (the knob just turned: its card tinted and outlined, ui_draw.c) */
#define RATIO(d, v) ((d)->max > (d)->min ? ((int32_t)(v) - (d)->min) * 1000 / ((d)->max - (d)->min) : -1)
/* layout (docs/design): header 0..18, four cards 22..66 (54 px at x 4 + 58 c), the panel 70..194 (the graphs live
 * in it; Stage: its own panel and lanes, ui_stage.c), footer 198..240; BG between them */
#define Y_HEAD 0
#define H_HEAD 18
#define Y_LABEL 22                    /* the cards */
#define Y_SEP_END 66
#define CARD_W 54
#define CARD_H 44
#define CARD_X(c) (4 + 58 * (int32_t)(c))
#define Y_GRAPH 70                    /* the panel */
#define H_GRAPH 124
#define Y_FOOT 198
#define H_FOOT 42

static struct {
    uint8_t designer_mode;       /* the sound designer: START, the stages (ui_designer.c) */
    uint8_t home;
    uint8_t drum_sound;
    uint8_t page;                /* index into PAGES */
    uint8_t fam_last[FAM_COUNT]; /* last page used per family */
    uint8_t bank;                /* SEQ: 16-step bank (follows the cursor) */
    uint8_t pat_key, pat_copy, pat_track;          /* SEQ + white-key bank gesture */
    uint8_t ptc_on, ptc_trk, ptc_src, ptc_dst;     /* PATTERNS' Copy to: track ptc_trk's pattern src to dst
                                                    * (ui_patterns.c) */
    uint8_t cursor;              /* SEQ: step being edited (STEP page KNOB 1 moves it) */
    uint16_t note_pick;          /* NOTES: event index + 1; zero chooses the first hit */
    uint8_t erase_gesture, erase_owner; /* an EDIT press captures one track/bank until released */
    uint32_t erase_generation, erase_transport;
    uint8_t note_slot;           /* individual pitch in a manually entered chord */
    uint8_t note_zoom;           /* 16, 8, 4, 2 or 1 steps across the panel */
    uint8_t note_track;
    uint8_t scale_picker_seen;
    uint8_t scale_family;        /* 0 ALL, 1..SCALE_FAMILIES, last FAV */
    uint32_t note_pattern_gen, note_generation;
    recorded_note_t note_identity; /* retain focus when unrelated event storage changes */
    uint8_t entry_open;          /* SEQ: keys held since the first press of this entry */
    uint8_t step_move;           /* a held SELECT modifier gesture: one edit until the modifier is released */
    uint8_t step_oct_used;       /* OCT buttons consumed by FX/SAVE undo or redo */
    uint16_t step_mods, step_used; /* STEP's ENV/SCL/FX buttons awaiting release, and the gestures consumed */
    uint8_t lane;                /* SEQ > STEP on a DRUM track (the grid): the lane the keys and KNOB 3 / 4 edit */
    uint8_t hot_col, hot_t;      /* column whose knob was just turned (drawn white) */
    uint8_t menu;                /* 0 off, 1 list, 2 about + credits (HOME held) */
    uint8_t menu_sel;
    uint16_t menu_scroll;        /* continuous ABOUT + CREDITS position, pixels */
    uint32_t seen_pattern_gen;
    uint32_t menu_sig, home_t0;  /* HOME press time (btn_hold) */
    uint8_t force;               /* full redraw pending */
    uint8_t msg_t;               /* transient message frames */
    uint8_t bpm_t;               /* frames the BPM stays highlighted after a SELECT turn */
    uint8_t act;                 /* action pages: the column whose action OCT+ does, + 1; 0 = none (act_col) */
    uint32_t rec_t0;             /* REC press time (transport only) */
    uint32_t seq_t0;             /* SEQ held: direct SONG entry */
    uint32_t save_t0;            /* SAVE press time (btn_hold: held = UNDO) */
    uint32_t oct_t0;             /* OCT+ press time (btn_hold: held = the page's sheet, ui_popup.c) */
    uint8_t confirm;             /* the OCT- / OCT+ dialog: CF_*, 0 = none */
    uint8_t confirm_trk;         /* the track it clears, the slot it overwrites */
    uint8_t uslot;               /* SAVE > USER: the selected user preset slot */
    uint8_t song_row;            /* SONG: row selected, count selects the next empty row */
    uint8_t uboot;               /* main.c: seconds left before UPDATE MODE (OCT- + OCT+ held), 0 = none */
    uint8_t proj_new;            /* SAVE > PROJECT: KNOB 1 past TMPL, NEW (a new song: ui_new.c); SLOT unchanged */
    uint32_t ly_t0;              /* the layer button's press time | 1, LY_* bits (ui_layer.c layer_gesture) */
    uint8_t ly;                  /* the layer whose button is down (LAYER_*), 0 = none */
    uint8_t lock; /* double-tapped quick layer */
    uint8_t layer;               /* the layer whose map is shown (LAYER_*), 0 = none */
    uint16_t pg_down;            /* page buttons down (panel ids) that act when let go */
    uint32_t layer_sig;          /* drawn-state cache of the map */
    char msg[24];
    char msg2[24];               /* a second message, shown when the first is over */
    uint32_t enc_t[NE];
    /* drawn-state cache */
    char col[4][32];
    uint32_t graph_sig, head_sig, foot_sig, frame;
    /* rolling digits (ui_draw.c roll_*): the four card values and the header BPM */
    struct {
        char from[7];            /* the value rolling out; "" = idle */
        uint8_t t0;              /* (ui.frame) of the value's last change */
        int8_t dir;              /* +1: it went up (the old digit leaves upward), -1: down */
        uint8_t sig;             /* a card: what its value is of (label, unit, track, engine, palette) */
    } roll[5];
    int16_t roll_bpm;            /* the BPM last drawn */
} ui;

static int designer_on(void) { return ui.designer_mode != 0; }
static void designer_open(void);
static void designer_draw(void);
static int designer_screen(void);
static int designer_input(uint32_t pressed, uint32_t notes);
static void designer_close(int discard);
static int designer_head_chip(char *b);
static void designer_pages(uint32_t *n, uint32_t *k);
static int designer_tabs_on(void);
static void designer_tabs(void);
static int designer_foot(void);
static void (*designer_quit)(int discard);   /* designer_close while it is open (project.c: a project loading) */

#include "screen.c"

enum { CF_NONE, CF_CLEAR_SEQ, CF_CLEAR_TRK, CF_OVR_PROJ, CF_OVR_USER,
       CF_DEL_ROW, CF_CLEAR_SONG, CF_INIT_SOUND, CF_CLEAR_MOTION, CF_ERASE_USER, CF_TAKE_JAM, CF_NEW_SONG, CF_ERASE_PROJ,
       CF_DEL_PAT, CF_PASTE_PAT, CF_DISCARD_DESIGN };   /* ui.confirm: REC held on SEQ / ARP, on TRACKS; SAVE over a used slot; a pattern
                                      * over the user's steps; USER ERASE; PATTERNS: Delete pattern, Copy to over a
                                      * pattern in use */

static const page_t *page_over;   /* a quick layer's own four knobs (ui_layer.c), while it edits or draws them */
static uint32_t drum_hit_selected(void);
static const page_t *cur_page(void) { return page_over ? page_over : &PAGES[ui.page]; }

/* the quick layers (ui_layer.c): a button held, the keys and KNOB 1..4 are its shortcuts, its map over the page */
enum { LAYER_NONE, LAYER_FX, LAYER_GLO, LAYER_SCL, LAYER_EDIT, LAYER_N };
static void draw_layer(void);
static const char *layer_head(void);
static uint32_t layer_leds(void);
static uint32_t layer_btn(void);

/* FM operator pages belong to DIGITAL; they never appear on other instruments (without MELODEE_FM4: never). SLICES:
 * a SLICE track's (ui_slice.c) */
static int list_gone(uint32_t i);                        /* ui_list.c: a page in another's list */
/* ENV / LFO on the engines with envelopes and LFOs of their own (Prophet, FM6, CZ-1: engine_t.ownenv, the track's
 * ADSR and ENV DEST do nothing there): their own pages (FAM_EDIT, by title). ENV: only those; LFO: those first, then
 * the track LFO's pages and MOD (the track LFO modulates any engine through LFO DEST and is the matrix's source) */
static const char *const *native_titles(uint32_t fam)
{
    static const char *const P5_ENV[] = {"P5 FLT ENV", "P5 AMP ENV", "P5 ENV MOD", 0};
    static const char *const FM_ENV[] = {"EG RATE", "EG LVL", "PITCH EG", "PITCH LV", 0};
    static const char *const CZ_ENV[] = {"C1 PIT R1-4", "C1 WAV R1-4", "C1 AMP R1-4", "C2 PIT R1-4", "C2 WAV R1-4",
                                         "C2 AMP R1-4", 0};
    static const char *const P5_LFO[] = {"P5 LFO", "P5 WHEEL", 0};
    static const char *const FM_LFO[] = {"FM LFO", 0};
    static const char *const CZ_LFO[] = {"CZ VIBRATO", 0};
    uint32_t e = TSEL->eng_req;
    if (fam == FAM_ENV)
        return e == ENGI_PROPHET ? P5_ENV : e == ENGI_FM6 ? FM_ENV : e == ENGI_CZ ? CZ_ENV : 0;
    if (fam == FAM_LFO)
        return e == ENGI_PROPHET ? P5_LFO : e == ENGI_FM6 ? FM_LFO : e == ENGI_CZ ? CZ_LFO : 0;
    return 0;
}
static int native_page(uint32_t i, uint32_t fam)       /* page i: one of fam's native pages (a prefix of its title) */
{
    const char *const *t = native_titles(fam);
    for (; t && *t; t++)
        if (str_prefix(PAGES[i].title, *t))
            return 1;
    return 0;
}

static int page_visible(uint32_t i)
{
    if (list_gone(i))
        return 0;
    if (PAGES[i].graph == GR_MOTION)                   /* MOTION: once there is motion (Kerem: an empty page; the one
                                                         * shown stays until it is left) */
        return motion_has_lanes(TSEL) || (!ui.home && ui.page == i);
    if (PAGES[i].fam == FAM_ENV && native_titles(FAM_ENV))   /* (the track's ADSR, ENV DEST: nothing there) */
        return 0;
    if(PAGES[i].scope==SC_DRUM || PAGES[i].scope==SC_DRUMHIT)return drum_track(TSEL);
    if (PAGES[i].scope == SC_TRACK && PAGES[i].id[0] >= P_LN0 && PAGES[i].id[0] <= P_LN7)
        return drum_track(TSEL);
    if(TSEL->eng_req==ENGI_CZ && PAGES[i].fam==FAM_EDIT && (PAGES[i].id[0]==P_E0 || PAGES[i].id[0]==P_E4))return 0;
    if (PAGES[i].scope == SC_CZ)                       /* (PHASE's CZ mode; PHASE retired without MELODEE_LEGACY_EXTRAS) */
        return TSEL->eng_req == 2u && eng_ok(2u) && TSEL->p[P_E7] == 1;
    if(PAGES[i].scope==SC_P5 || PAGES[i].scope==SC_P5STORE)return TSEL->eng_req==ENGI_PROPHET;
    if (PAGES[i].scope == SC_CZ1)
        return TSEL->eng_req == ENGI_CZ;               /* CZ-1's tone: every panel value */
    if (PAGES[i].scope == SC_FM6 || PAGES[i].scope == SC_FMOP)
        return TSEL->eng_req == ENGI_FM6;              /* FM6's patch, operators and functions */
#if MELODEE_SLICE
    if (PAGES[i].graph == GR_SLICES)
        return SMP_USER_SLOTS && ENGINES[TSEL->eng_req % NENGINES] == &ENG_SLICE;
#else
    if (PAGES[i].graph == GR_SLICES)
        return 0;
#endif
    return !(PAGES[i].fam == FAM_EDIT && PAGES[i].id[0] >= P_FM1_ATK &&
             PAGES[i].id[0] <= P_FM4_LEVEL) || (MELODEE_FM4 && TSEL->eng_req % NENGINES == ENGI_DIGITAL);
}

static uint32_t page_first(uint32_t fam)
{
    uint32_t i;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].fam == fam && page_visible(i))
            return i;
    return 0;
}

/* transient message in the top bar, right of the transport and the BPM: a + b */
static void ui_say(const char *a, const char *b)
{
    uint32_t n;
    str_cpy(ui.msg, a, sizeof ui.msg);
    n = str_len(ui.msg);
    str_cpy(ui.msg + n, b, sizeof ui.msg - n);
    ui.msg_t = 40;
    ui.msg2[0] = 0;
}

static void ui_message(const char *s) { ui_say(s, ""); }

static int chain_busy(void) { return chain.running || chain.armed; }
/* Main loop only, with interrupts enabled. PLAY may be consumed between reads;
 * take one coherent snapshot before a flash operation or song preparation. */
static int transport_busy(void)
{
    int busy;
    fm1_irq_off();
    busy = song.playing || seq_counting() || chain_busy() || transport_req == 1u;
    fm1_irq_on();
    return busy;
}
static void chain_play_ui(void)
{
    uint32_t rc = chain_prepare();
    if (!rc) {
        ui.force = 1;
    } else if (rc >= 3u) {
        char b[8] = "A EMPTY";
        b[0] = (char)('A' + rc - 3u);
        ui_say("PATTERN ", b);
    } else {
        ui_message(rc == 1u ? "ADD A SONG ROW" : "STOP FIRST");
    }
}


static uint16_t step_midi_held;                      /* physical MIDI keys held in this entry */
static uint32_t step_midi_keys[16][4];               /* channel/source ownership, before scale and chord */
static void seq_midi_reset(void)                    /* a new page/track ends MIDI step entry */
{
    fm1_irq_off();
    step_midi_r = step_midi_w;
    step_midi_overflow = 0;
    fm1_irq_on();
    step_midi_held = 0;
    memset(step_midi_keys, 0, sizeof step_midi_keys);
}
static void undo_seal(void);
static void page_entered(void)
{
    const page_t *pg = cur_page();
    if (pg->graph == GR_SCALE_PICKER && !ui.scale_picker_seen) {
        ui.scale_family = (uint8_t)(SCALE_FAMILY[clamp(TSEL->p[P_SCALE], 0, SCALE_TOTAL - 1u)] + 1u);
        ui.scale_picker_seen = 1;
    }
    song.seq_mode = !ui.home && pg->fam == FAM_SEQ;
    ui.entry_open = 0;
    ui.note_pick = 0;
    ui.note_slot = 0;
    seq_midi_reset();
    ui.hot_t = 0;                                /* clear the previous page's emphasis */
    ui.act = 0;
    ui.proj_new = 0;
    ui.ptc_on = 0;
    if (pg->graph == GR_BROWSE) {                /* (OCT- goes back to the sound from before: its loads, a level */
        browse_mark = browse_loads;              /* of their own) */
        undo_seal();
    }
    ui.force = 1;
}

static int step_on(const step_t *st) { return st->time == ST_NOTE && (st->n || st->hit); }

static void step_clear(step_t *st)
{
    st->n = 0;
    st->time = ST_REST;
    st->flags = 0;
    st->vel = 0;
    st->hit = st->acc = 0;
    st->probability = 0;
}

/* ------------------------------------------------------- the DRUM grid --- */
/* SEQ > STEP on a DRUM track is the grid: 8 lanes x the 16 steps of a page. The white keys are the
 * steps of the page shown (a tap toggles the selected lane there), black keys 1..8 select the lane (and play
 * it), black key 9 held is ACC (white keys toggle accents, their LEDs show them), black keys 10 / 11 the page
 * down / up. KNOB 1 STEP, 2 LANE, 3 HIT, 4 ACC edit the cursor step. A sound load never converts the
 * steps: the grid shows a step's notes on their lanes (eng_drum.c step_lanes) and an edit makes the lane its
 * own (grid_own). Live recording on a DRUM track writes hits (seq.c rec_note) */
static int notes_have_recording(const track_t *t);
static int grid_on(void) { return !ui.home && !ui.menu && !ui.confirm && (cur_page()->graph == GR_ROLL || cur_page()->graph == GR_DRUMHIT) && drum_track(TSEL) && (cur_page()->scope==SC_DRUMHIT || !notes_have_recording(TSEL) || (song.playing && (song.rec & (1u << song.sel)))); }   /* (NOTES, DRUM HIT) */

/* black key place p (seq.c key_place) held, 0 = not */
static int black_held(uint32_t p)
{
    uint32_t k;
    for (k = 0; k < 27u; k++)
        if (key_black(k) && key_place(k) == p)
            return (int)(((fm1_in.notes & ~kb_layer) >> k) & 1u);
    return 0;
}

/* lane l of step s from now on is its hit alone: the step's lane notes become hits (nothing sounds different),
 * a note of another pitch on the lane (a low tom 41) becomes the lane's own */
static void grid_own(step_t *s, uint32_t l)
{
    uint32_t k, j = 0;
    step_to_grid(s);
    for (k = 0; k < s->n; k++)
        if (drum_lane(s->note[k]) == l)
            s->hit |= (uint8_t)(1u << l);
        else
            s->note[j++] = s->note[k];
    for (k = j; k < 4u; k++)
        s->note[k] = 0;
    s->n = (uint8_t)j;
}

/* lane l of step i: on 1, off 0, toggled 2 */
static void grid_hit(track_t *t, uint32_t i, uint32_t l, uint32_t on)
{
    step_t *s = &t->step[i % NSTEP];
    uint32_t b = 1u << (l % NLANE);
    s->flags &= (uint8_t)~SF_RECORDED;
    if (on == 2u)
        on = !(step_lanes(s) & b);
    if (s->time != ST_NOTE) {                    /* a REST or a TIE: an empty step (nothing to turn off) */
        if (!on)
            return;
        step_clear(s);
        s->time = ST_NOTE;
    }
    grid_own(s, l % NLANE);
    if (on) {
        s->hit |= (uint8_t)b;
    } else {
        s->hit &= (uint8_t)~b;
        s->acc &= (uint8_t)~b;
        if (!s->n && !s->hit)
            step_clear(s);
    }
}

/* the accent of lane l at step i (on 1, off 0, toggled 2); an accent on an empty lane adds the hit */
static void grid_acc(track_t *t, uint32_t i, uint32_t l, uint32_t on)
{
    step_t *s = &t->step[i % NSTEP];
    uint32_t b = 1u << (l % NLANE);
    if (on == 2u)
        on = !(step_accents(s) & b);
    if (on)
        grid_hit(t, i, l, 1);
    if (!(step_lanes(s) & b))
        return;
    grid_own(s, l % NLANE);
    if (s->flags & SF_ACCENT) {                  /* a step accent: each hit's own from now on */
        s->acc |= s->hit;
        s->flags &= (uint8_t)~SF_ACCENT;
    }
    s->acc = (uint8_t)(on ? s->acc | b : s->acc & ~b);
}

/* SEQ cursor: wraps inside the pattern length, the bank follows, a step entry ends */
static void notes_preview(void);
static uint32_t drum_hit_selected(void);
static void cursor_set(int32_t c)
{
    int32_t len = TSEL->p[P_SLEN] > 0 ? TSEL->p[P_SLEN] : 1;
    ui.cursor = (uint8_t)((c % len + len) % len);
    ui.bank = (uint8_t)(ui.cursor / 16u);
    ui.entry_open = 0;
    ui.note_pick = 0;
    ui.note_slot = 0;
    notes_preview();
}

static void cursor_fix(void)                           /* LEN got shorter: onto the last step */
{
    if (ui.cursor >= (uint32_t)TSEL->p[P_SLEN])
        cursor_set(TSEL->p[P_SLEN] - 1);
}

static void note_name(char *b, uint32_t n)
{
    if (micro_active(TSEL)) {
        int32_t d = (int32_t)n - 60, count = micro_scale(TSEL)->count, cycle = micro_floor(d, count);
        b[0] = 'D';
        fmt_int(b + 1, d - cycle * count + 1);
        if (cycle) {
            uint32_t at = str_len(b);
            b[at] = cycle < 0 ? '-' : '+';
            fmt_int(b + at + 1, cycle < 0 ? -cycle : cycle);
        }
        return;
    }
    str_cpy(b, N_NOTE[n % 12u], 4);
    fmt_int(b + str_len(b), (int32_t)(n / 12u) - 1);
}

/* SELECT: the open section's pages, both ways (wraps; pages the track does not show skipped) */
static void page_scroll(int32_t dir)
{
    uint32_t fam = cur_page()->fam, n, i = ui.page;
    for (n = 0; n < NPAGES; n++) {
        i = (i + (dir > 0 ? 1u : NPAGES - 1u)) % NPAGES;
        if (PAGES[i].fam == fam && page_visible(i))
            break;
    }
    if (i == ui.page)
        return;
    ui.page = (uint8_t)i;
    ui.fam_last[fam] = ui.page;
    page_entered();
}

/* the pages a family's button visits, in order: ENV / LFO on an engine with its own (native_titles) its pages
 * first; n = 0: none */
static uint32_t fam_pages(uint32_t fam, uint8_t *pg)
{
    uint32_t i, n = 0;
    for (i = 0; i < NPAGES; i++)
        if (native_page(i, fam) && page_visible(i))
            pg[n++] = (uint8_t)i;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].fam == fam && page_visible(i) && !native_page(i, fam))
            pg[n++] = (uint8_t)i;
    return n;
}
static int fam_has(uint32_t fam, uint32_t page)         /* page: one the family's button visits */
{
    return page < NPAGES && (native_page(page, fam) || PAGES[page].fam == fam) && page_visible(page);
}

static void open_family(uint32_t fam)
{
    if ((fam == FAM_ENV || fam == FAM_LFO) && native_titles(fam)) {   /* ENV / LFO of the Prophet, FM6, CZ-1 */
        uint8_t pg[NPAGES];
        uint32_t n = fam_pages(fam, pg), k;
        for (k = 0; k < n && pg[k] != ui.page; k++) {}
        if (!n)
            return;
        ui.page = !ui.home && k < n ? pg[(k + 1u) % n]                 /* again: the next one; from elsewhere the */
                  : native_page(ui.fam_last[fam], fam) && fam_has(fam, ui.fam_last[fam]) ? ui.fam_last[fam] : pg[0];
                                                                        /* engine's page last used, else its first */
    } else if (!ui.home && cur_page()->fam == fam) {   /* same button again: next page */
        uint32_t n, i = ui.page;
        for (n = 0; n < NPAGES; n++) {
            i = (i + 1u) % NPAGES;
            if (PAGES[i].fam == fam && page_visible(i)) break;
        }
        ui.page = (uint8_t)i;
    } else if (fam == FAM_SAVE) {
        uint32_t i;
        for (i = 0; i < NPAGES; i++)
            if (PAGES[i].graph == GR_USER) break;
        ui.page = (uint8_t)i;                         /* SAVE enters the sound save screen directly */
    } else {
        ui.page = ui.fam_last[fam] && PAGES[ui.fam_last[fam]].fam == fam && page_visible(ui.fam_last[fam]) ? ui.fam_last[fam]
                                                                          : (uint8_t)page_first(fam);
    }
    ui.fam_last[fam] = ui.page;
    ui.home = 0;
    page_entered();
    if (fam == FAM_SAVE && cur_page()->graph == GR_USER)
        slot_save_sheet();                       /* SAVE: USER; OCT+ then its slot's sheet on Save here (ui_slots.c) */
}

/* GLO always enters the mixer from another family. Subsequent taps visit the
 * global settings, then return to the mixer; recording has no navigation role. */
static void open_global(void)
{
    uint32_t i;
    if (!ui.home && cur_page()->fam == FAM_TRK) {
        ui.page = (uint8_t)page_first(FAM_GLO);
    } else if (!ui.home && cur_page()->fam == FAM_GLO) {
        for (i = ui.page + 1u; i < NPAGES && (PAGES[i].fam != FAM_GLO || !page_visible(i)); i++) {}
        ui.page = (uint8_t)(i < NPAGES ? i : page_first(FAM_TRK));
    } else {
        ui.page = (uint8_t)page_first(FAM_TRK);
    }
    ui.home = 0;
    page_entered();
}

static void go_home(void)
{
    ui.home = 1;
    ui.entry_open = 0;
    ui.hot_t = 0;
    song.seq_mode = 0;
    ui.force = 1;
}

/* ------------------------------------------------------- track setup --- */
static int seq_is_empty(const track_t *t)
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        if (t->step[i].n || t->step[i].hit)
            return 0;
    return 1;
}

/* UNDO of loads. A sound load (a factory or user preset, an engine jump, TOOLS INIT, the editor's PRESET / G_ENGSEL /
 * UP_LOAD) changes the sound only; a Capture changes the steps and the pattern
 * parameters only. Each first copies the track as it was into a level; SAVE held swaps back what the latest level's
 * loads changed (held again: the level before, SAVE + OCT+ redoes), so steps recorded after a sound load, or a sound
 * edited after a pattern load, stay as they are. Loads in a row on one track with nothing changed in between (the
 * PRESETS knob through the list, one pattern after the other, an editor audition) keep the copy from before the first,
 * so the undo goes back past the whole browse. Levels: UNDO_LV_MAX in the cache RAM's UI part (resources.c ui_cache),
 * one without it. Not snapshotted: power-on, projects (a project load drops them all) */
enum { UNDO_SOUND = 1, UNDO_PAT = 2 };
typedef struct {
    uint8_t trk;                 /* track + 1, 0 = nothing to undo */
    uint8_t keep;                /* the track is as the last load left it (after): a next load keeps the copy */
    uint8_t what;                /* UNDO_SOUND | UNDO_PAT: what the loads since the copy changed (undo_step) */
    uint8_t eng, preset, user, user_native, patn;
    uint8_t fm6[FP_SIZE + 1u];   /* the track's complete patch */
    cz_patch_t cz;
    p5_patch_t p5;
    drum_patch_t drum;
    int16_t p[P_COUNT];
    step_t step[NSTEP];
    motion_store_t motion_backup; /* one track only, swaps with the shared event pool on undo */
    uint32_t pattern_gen;
    uint32_t after;              /* track_sig right after the last load */
    uint32_t pat;                /* pat_sig[] of the copy */
    uint32_t t_ms;               /* time of the last load (the editor's SETs after it belong to it) */
} undo_t;
#define UNDO_LV_MAX 8u
_Static_assert(UNDO_LV_MAX * sizeof(undo_t) <= UI_CACHE_BYTES, "undo levels fit the cache RAM's UI part");
static undo_t undo_one;          /* the level of a build without cache RAM */
static undo_t *undo_lv = &undo_one;
static uint8_t undo_nlv = 1, undo_top, undo_cnt;   /* levels; undoable 0 .. top-1, redoable top .. cnt-1 */
static undo_t *undo_last(void) { return &undo_lv[undo_top ? undo_top - 1u : 0u]; }
#define undo (*undo_last())      /* the latest level (tests, the layers: undo.keep); none: level 0 */
static void undo_clear(void) { undo_top = undo_cnt = 0; undo_lv[0].trk = undo_lv[0].keep = 0; }
static void undo_levels(void)    /* the cache RAM's UI part once it is there (boot self-test passed) */
{
    uint8_t *c = ui_cache();
    if (c && undo_lv == &undo_one) {
        undo_lv = (undo_t *)(void *)c;
        undo_nlv = UNDO_LV_MAX;
        undo_clear();
    }
}
static uint8_t undo_depth;       /* loads nest (an engine jump loads its first preset): the outer one counts;
                                  * melodee_init / project_load raise it to take no copy at all */
static uint32_t pat_sig[NTRK];   /* steps_sig of the pattern the last pattern load put into each track: such
                                  * steps, untouched, are replaced by the next pattern without asking */
static uint8_t pat_last[NTRK];   /* that pattern's list index + 1, 0 = none */

static uint32_t fnv(uint32_t h, const void *p, uint32_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    while (n--)
        h = (h ^ *b++) * 16777619u;
    return h;
}
static uint32_t steps_sig(const track_t *t) { return fnv(2166136261u, t->step, sizeof t->step); }
static uint32_t track_sig(const track_t *t)      /* the sound (an FM6 track's patch too), the steps */
{
    uint8_t id[4] = {t->eng_req, t->preset, t->user, t->user_native};
    uint32_t h = fnv(fnv(steps_sig(t), t->p, sizeof t->p), id, 3), k = trk_index(t);
    h = fnv(h, fm6_patch[k], sizeof fm6_patch[k]); /* (a patch the editor sent between two loads) */
    h = fnv(h, cz_patch[k].raw, CZ_BYTES);
    h = fnv(h,p5_patch_of(&trk[k]),sizeof(p5_patch_t));
    h = fnv(h,&drum_patch[k],sizeof(drum_patch_t));
    for (uint32_t j = 0; j < motion.count; j++)
        if ((motion.event[j].place >> 6) == k) h = fnv(h, &motion.event[j], sizeof motion.event[j]);
    return h ^ ((motion.on >> k) & 1u);
}

static const param_desc_t *home_param(uint32_t k,int16_t **vp);
static int scale_shared(uint32_t id);
static struct {
    uint8_t active, track, count, native;
    int16_t *ptr[P_COUNT+G_COUNT], value[P_COUNT+G_COUNT];
    uint8_t fm[FP_SIZE+1u], fn[FM6_NFN], on;
    cz_patch_t cz; p5_patch_t p5;
    int16_t p5_voice,p5_prio,p5_detune,p5_alloc;
} momentary;
static void momentary_restore(void)
{
    if(!momentary.active)return;
    uint32_t tr=momentary.track;fm1_irq_off();
    if(momentary.native==1){fm6_put_patch(tr,momentary.fm,0);memcpy(fm6_fn[tr],momentary.fn,FM6_NFN);fm6_on[tr]=momentary.on;}
    else if(momentary.native==2)cz_patch[tr]=momentary.cz;
    else if(momentary.native==3){p5_patch[tr]=momentary.p5;trk[tr].p[P_VOICE]=momentary.p5_voice;trk[tr].p[P_PRIO]=momentary.p5_prio;trk[tr].p[P_DETUNE]=momentary.p5_detune;trk[tr].p[P_ALLOC]=momentary.p5_alloc;}
    for(uint32_t i=0;i<momentary.count;i++)*momentary.ptr[i]=momentary.value[i];
    momentary.active=momentary.count=momentary.native=0;fm1_irq_on();ui.force=1;
}
static const page_t *stage_page(void);
static void momentary_take(uint32_t slot)
{
    const page_t *pg=ui.home?stage_page():cur_page();int16_t *vp;const param_desc_t *d;   /* (HOME: Stage's knobs) */
    if(!(fm1_in.buttons&(1u<<panel.btn[B_LFO])) || pg->graph==GR_FMSTORE || pg->graph==GR_CZTOOLS)return;
    d=page_desc(pg,slot,&vp);
    if(!d || !vp || d->max==d->min)return;
    uint32_t native=pg->scope==SC_FM6 || pg->scope==SC_FMOP?1:pg->scope==SC_CZ1?2:pg->scope==SC_P5?3:0;
    if(!native && pg->scope!=SC_TRACK && pg->scope!=SC_ENGINE &&
       !(pg->scope==SC_GLOBAL && (pg->id[slot]<=G_SWING || (pg->id[slot]>=G_DTIME && pg->id[slot]<=G_CDEPTH) || pg->id[slot]==G_DWEAR)))return;
    if(!native && pg->scope==SC_TRACK && scale_shared((uint32_t)(vp-TSEL->p)))return;
    if(!momentary.active){momentary.track=song.sel;momentary.active=1;}
    if(native && !momentary.native){
        uint32_t tr=song.sel;fm1_irq_off();momentary.native=(uint8_t)native;
        memcpy(momentary.fm,fm6_patch[tr],sizeof momentary.fm);memcpy(momentary.fn,fm6_fn[tr],FM6_NFN);momentary.on=fm6_on[tr];
        momentary.cz=cz_patch[tr];momentary.p5=*p5_patch_of(TSEL);
        momentary.p5_voice=TSEL->p[P_VOICE];momentary.p5_prio=TSEL->p[P_PRIO];momentary.p5_detune=TSEL->p[P_DETUNE];momentary.p5_alloc=TSEL->p[P_ALLOC];fm1_irq_on();
    }
    if(!native){uint32_t i;for(i=0;i<momentary.count && momentary.ptr[i]!=vp;i++);
        if(i==momentary.count && i<P_COUNT+G_COUNT){momentary.ptr[i]=vp;momentary.value[i]=*vp;momentary.count++;}}
    ui.pg_down&=(uint16_t)~(1u<<panel.btn[B_LFO]);
}
static void load_begin(track_t *t, uint32_t what)
{
    undo_t *u;
    momentary_restore();
    uint32_t i = trk_index(t);
    if (undo_depth++)
        return;
    browse_loads++;
    motion_restore(t);
    undo_levels();
    u = undo_last();
    if (undo_top && u->keep && u->pattern_gen == t->pattern_gen && u->trk == i + 1u && track_sig(t) == u->after) {
        u->what |= (uint8_t)what;                 /* browsing on: the copy from before the first load stays */
        if(!designer_on())motion_reset(t);
        return;
    }
    u->keep = 0;
    if (undo_top == undo_nlv) {                   /* full: the oldest level goes (no memmove in libc.c) */
        uint32_t k;
        for (k = 0; k + 1u < undo_nlv; k++)
            memcpy(&undo_lv[k], &undo_lv[k + 1u], sizeof *undo_lv);
        undo_top--;
    }
    u = &undo_lv[undo_top++];                     /* a new level (the redoable ones go) */
    undo_cnt = undo_top;
    u->trk = (uint8_t)(i + 1u);
    u->keep = 0;
    u->pattern_gen = t->pattern_gen;
    u->what = (uint8_t)what;
    u->eng = t->eng_req;
    u->preset = t->preset;
    u->user = t->user;
    u->user_native=t->user_native;
    memcpy(u->p, t->p, sizeof u->p);
    memcpy(u->step, t->step, sizeof u->step);
    memcpy(u->fm6, fm6_patch[i], FP_SIZE);
    u->cz = cz_patch[i];
    u->p5 = *p5_patch_of(&trk[i]);
    u->drum=drum_patch[i];
    u->pat = pat_sig[i];
    u->patn = pat_last[i];
    motion_snapshot_track(t, &u->motion_backup);
    if(!designer_on())motion_reset(t);
}

static void load_end(track_t *t)
{
    if (--undo_depth || !undo_top)
        return;
    motion_rebase(t);
    undo.after = track_sig(t);
    undo.keep = 1;
    undo.t_ms = fm1_ms;
}

/* the editor's SET right after a load on the selected track (an audition: G_ENGSEL, then the patch's
 * values): part of that load, the copy from before it stays */
static void load_extend(track_t *t)
{
    if (undo_top && undo.keep && undo.trk == trk_index(t) + 1u && fm1_ms - undo.t_ms < 1500u) {
        undo.after = track_sig(t);
        undo.t_ms = fm1_ms;
    }
}

static int param_kept(uint32_t i);

/* SAVE held (redo 0) / SAVE + OCT+ (redo 1): what a level's loads changed (what) and its copy change places, so its
 * redo is the same swap. The sound: engine, preset and its parameters (not param_kept); the pattern: the steps and LEN
 * DIV SWING GATE. The mix (LEVEL PAN MUTE) stays: no load changes it */
static void undo_step(int redo)
{
    track_t *t;
    undo_t *u;
    uint32_t i;
    char b[4] = {'T', 0, 0, 0};
    if (redo ? undo_top >= undo_cnt : !undo_top) {
        ui_message(redo ? "NOTHING TO REDO" : "NOTHING TO UNDO");
        return;
    }
    u = &undo_lv[redo ? undo_top : undo_top - 1u];
    t = &trk[(u->trk - 1u) % NTRK];
    if (u->pattern_gen != t->pattern_gen) { undo_clear(); ui_message(redo ? "NOTHING TO REDO" : "NOTHING TO UNDO"); return; }
    motion_restore(t);
    motion_store_t current_motion;
    motion_snapshot_track(t, &current_motion);
    if (motion_replace_track(t, &u->motion_backup) != 0) {
        ui_message("MOTION FULL");
        return;
    }
    u->motion_backup = current_motion;
    fm1_irq_off();                                /* the audio ISR must not see half a sound */
    if (u->what & UNDO_SOUND) {
        uint8_t e = t->eng_req, pr = t->preset, us = t->user, un=t->user_native;
        panic_req |= (uint8_t)(1u << trk_index(t));
        t->eng_req = u->eng;
        t->preset = u->preset;
        t->user = u->user;
        t->user_native=u->user_native;
        u->eng = e;
        u->preset = pr;
        u->user = us;
        u->user_native=un;
        for (i = 0; i < P_COUNT; i++)
            if (!param_kept(i)) {
                int16_t v = t->p[i];
                t->p[i] = u->p[i];
                u->p[i] = v;
            }
    }
    if (u->what & UNDO_PAT) {
        uint32_t ps = pat_sig[trk_index(t)];
        uint8_t pn = pat_last[trk_index(t)];
        pat_sig[trk_index(t)] = u->pat;
        u->pat = ps;
        pat_last[trk_index(t)] = u->patn;
        u->patn = pn;
        for (i = P_SLEN; i <= P_SGATE; i++) {
            int16_t v = t->p[i];
            t->p[i] = u->p[i];
            u->p[i] = v;
        }
        for (i = 0; i < NSTEP; i++) {
            step_t s = t->step[i];
            t->step[i] = u->step[i];
            u->step[i] = s;
        }
    }
    fm1_irq_on();
    if (u->what & UNDO_SOUND) {                 /* FM6: the track's patch as it was (edited, a project's, a
                                                   * converted DIGITAL sound), preserved with the sound */
        uint32_t tr = trk_index(t);
        uint8_t v[FP_SIZE + 1u];
        memcpy(v, fm6_patch[tr], FP_SIZE);
        fm6_set_patch(tr, u->fm6);
        memcpy(u->fm6, v, FP_SIZE);
        {drum_patch_t dp=drum_patch[tr];drum_patch[tr]=u->drum;u->drum=dp;}
        { p5_patch_t pp=*p5_patch_of(t);p5_patch[tr]=u->p5;u->p5=pp;p5_ready[tr]=1; }
        { cz_patch_t cp = cz_patch[tr]; cz_patch[tr] = u->cz; u->cz = cp; cz_track_accept(t); }

    }
    u->keep = 0;                                /* the next load copies the track as it is now */
    undo_top = (uint8_t)(redo ? undo_top + 1u : undo_top - 1u);
    if (undo_top) undo_lv[undo_top - 1u].keep = 0;
    if (t == TSEL)
        sync_reload = 1;
    b[1] = (char)('1' + trk_index(t));
    ui_say(redo ? "REDO " : "UNDO ", b);
    ui.force = 1;
}
static void undo_swap(void) { undo_step(0); }
static void undo_seal(void) { if (undo_top) undo_lv[undo_top - 1u].keep = 0; }   /* the next load: a level of its own */

static void track_defaults_steps(track_t *t)
{
    uint32_t i;
    motion_reset(t);
    for (i = 0; i < NSTEP; i++)
        step_clear(&t->step[i]);
}

/* the track's settings, not the sound's: what a sound load (factory or user preset, an engine jump,
 * TOOLS INIT) leaves alone. The mix (LEVEL, PAN, MUTE: the TRACKS faders), the arpeggiator (ARP, ARP 2),
 * the scale and key map (SCL), the pattern parameters (LEN, DIV, SWING, GATE) and the SLICER insert,
 * which chops whatever the track plays in time with its sequencer */
static int param_kept(uint32_t i)
{
    return i == P_LEVEL || i == P_PAN || i == P_MUTE || (i >= P_AMODE && i <= P_SGATE) ||
           (i >= P_SLCR && i <= P_SLDEPTH) || i == P_CHRD || i == P_VOIC || i == P_MPCDEG || i == P_RECQ;
}

/* SCL, QNT and the MPC degree are the song's: one scale for every part, kept the same in each track's own slots
 * (the formats and the editor keep them per track). ROOT and TRN stay the track's */
static int scale_shared(uint32_t id) { return id == P_SCALE || id == P_QUANT || id == P_MPCDEG; }
static void scale_share(const track_t *from)
{
    uint32_t k;
    for (k = 0; k < NPART; k++) {
        trk[k].p[P_SCALE] = from->p[P_SCALE];
        trk[k].p[P_QUANT] = from->p[P_QUANT];
        trk[k].p[P_MPCDEG] = (int16_t)clamp(from->p[P_MPCDEG], 1, (int32_t)scale_count(from));
    }
}

#include "scale_picker.c"

/* a retired preset kept as an alias, so stored preset numbers stay valid: SAMPLE 1, once TRANH, is PIANO
 * (tools/gen_samples.py SMP_SET_ORIG). It loads as the original; browsing skips it. -> the preset k stands for */
static uint32_t preset_orig(const engine_t *e, uint32_t k) { (void)e; return k; }
static uint32_t preset_rank(const engine_t *e, uint32_t k) { (void)e; return k; }
#define preset_shown(e) (ENGINES[e]->npresets)

#if !MELODEE_FM4
/* DIGITAL (engine 1, retired): t's sound = p, values as DIGITAL has them, converted to FM6 with a patch of its own
 * (fm4_convert.c). Every path that brings a DIGITAL sound into a track ends here: a track never keeps engine 1 */
static void fm4_apply(track_t *t, int16_t *p)
{
    uint8_t v[FP_SIZE + 1u];
    uint32_t pr = fm4_convert(p, v), tr = trk_index(t), f;
    fm6_set_patch(tr, v);
    f = motion_guard();                               /* the audio ISR sees the old sound or the new one */
    memcpy(t->p, p, sizeof t->p);
    t->eng_req = ENGI_FM6;
    t->preset = (uint8_t)pr;
    motion_unguard(f);
}
static void fm4_track(track_t *t)                     /* t holds a DIGITAL sound (engine 1): convert it */
{
    int16_t p[P_COUNT];
    memcpy(p, t->p, sizeof p);
    fm4_apply(t, p);
}
/* DIGITAL preset k (a stored preset number of engine 1: the editor's PRESET, SET G_ENGSEL, an old project's
 * power-on sound): its sound as a preset load sets it, converted. The sound only (not the steps, not param_kept) */
static void fm4_load_preset(track_t *t, uint32_t k)
{
    int16_t p[P_COUNT];
    uint32_t i;
    load_begin(t, UNDO_SOUND);
    panic_req |= (uint8_t)(1u << trk_index(t));
    t->user = 0; t->user_native=0;
    if (t == TSEL)
        sync_reload = 1;
    memcpy(p, t->p, sizeof p);
    for (i = 0; i < P_E0; i++)
        if (!param_kept(i))
            p[i] = TP[i].def;
    fm4_preset_values(p, k);
    fm4_apply(t, p);
    load_end(t);
}
#endif

/* preset pi of the engine the track asked for: the sound only (not the steps, not param_kept) */
static void apply_preset_to(track_t *t, uint32_t pi)
{
    const engine_t *e = ENGINES[t->eng_req % NENGINES];
    uint32_t i;
#if !MELODEE_FM4
    if (t->eng_req % NENGINES == ENGI_DIGITAL) {
        fm4_load_preset(t, pi);
        return;
    }
#endif
    load_begin(t, UNDO_SOUND);
    panic_req |= (uint8_t)(1u << trk_index(t));       /* MONO/POLY may change: release what sounds */
    t->user = 0; t->user_native=0;
    if (t == TSEL)
        sync_reload = 1;
    if (!e->npresets) {
        load_end(t);
        return;
    }
    pi = preset_orig(e, pi % e->npresets);
    t->preset = (uint8_t)pi;
    memset(&drum_patch[t-trk],0,sizeof(drum_patch_t));
    for (i = 0; i < P_E0; i++)                        /* the rest of the sound to its defaults: a preset */
        if (!param_kept(i))                           /* sounds the same after any edit */
            t->p[i] = TP[i].def;
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = (int16_t)e->presets[pi].e[i];
    t->p[P_ATK] = e->presets[pi].env[0];
    t->p[P_DEC] = e->presets[pi].env[1];
    t->p[P_SUS] = e->presets[pi].env[2];
    t->p[P_REL] = e->presets[pi].env[3];
    t->p[P_ED_FLT] = e->presets[pi].fenv;
    t->p[P_VOICE] = e->presets[pi].mono ? V_LEGATO : V_POLY;   /* mono presets keep the legato feel */
    {   /* the sends */
        static const uint8_t FX_DEF[4] = {0, 24, 28, 36};
        const preset_t *pr = &e->presets[pi];
        for (i = 0; i < 4u; i++)                      /* (no delay: its sends predate the delay bus coming back) */
            t->p[P_DIST + i] = (int16_t)(i == 2u ? 0 : pr->fx[i] ? pr->fx[i] - 1 : FX_DEF[i]);
    }
    if(t->eng_req==ENGI_PROPHET)p5_preset_loaded(t,pi);
    cz_factory_loaded(t);
    cz_track_accept(t);
    fm6_track_loaded(t);                              /* FM6: the preset's patch */
    load_end(t);
}

/* the engine's defaults and its preset pi. With the audio IRQ off: the ISR sees the old engine with
 * its values or the new one with its own (voice.c engine_block), never one with the other's */
static void engine_load_of(track_t *t, uint32_t ei, uint32_t pi)
{
    const engine_t *e = ENGINES[ei % NENGINES];
    uint32_t i;
#if !MELODEE_FM4
    if (ei % NENGINES == ENGI_DIGITAL) {             /* DIGITAL (retired): its first preset, as FM6 */
        fm4_load_preset(t, 0);
        return;
    }
#endif
    load_begin(t, UNDO_SOUND);
    fm1_irq_off();
    t->eng_req = (uint8_t)(ei % NENGINES);
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = e->edit[i].def;
    apply_preset_to(t, pi);
    fm1_irq_on();
    load_end(t);
}
/* the engine's defaults and its first preset (PROPHET: Sequential's first factory program, not INIT) */
static void set_engine_of(track_t *t, uint32_t ei) { engine_load_of(t, ei, ei % NENGINES == ENGI_PROPHET ? 1u : 0u); }

/* the engine's own starting point, nothing changed: its INIT preset (Prophet, CZ-1, SID, FM6: "INIT .."), else
 * its first (DRUM: the 808 kit) */
static uint32_t preset_init(uint32_t e)
{
    const engine_t *en = ENGINES[e % NENGINES];
    uint32_t k;
    for (k = 0; k < en->npresets; k++) {
        const char *n = en->presets[k].name;
        if (n[0] == 'I' && n[1] == 'N' && n[2] == 'I' && n[3] == 'T' && (n[4] == ' ' || !n[4]))
            return k;
    }
    return 0;
}
/* Init sound (the sound's sheet, EDIT held's black key, the browser's ENGINE knob): the engine's INIT, the steps
 * stay */
static void sound_init_of(track_t *t, uint32_t ei) { engine_load_of(t, ei, preset_init(ei)); }

static void apply_preset(uint32_t pi) { apply_preset_to(TSEL, pi); }
static void set_engine(uint32_t ei) { set_engine_of(TSEL, ei); }

static void track_defaults(track_t *t)
{
    uint32_t i;
    for (i = 0; i < P_E0; i++)
        t->p[i] = TP[i].def;
    track_defaults_steps(t);
}

/* switch engine (its INIT sound: one starts a sound from there, the browser's next turn its presets) and say so */
static void select_engine(uint32_t e)
{
    sound_init_of(TSEL, e);
    ui_say("ENGINE ", ENGINES[TSEL->eng_req]->name);
    ui.force = 1;
}

#include "browse.c"                     /* the PRESETS list: LIST, categories, RECENT, browsing */

static int preset_favorite(void)
{
    uint32_t k = user_of(TSEL);
    uint32_t e=TSEL->eng_req;
    return favorite_has(k<USER_NONE ? (TSEL->user_native?(e==ENGI_PROPHET?USER_NATIVE_P5:e==ENGI_FM6?USER_NATIVE_FM:USER_NATIVE_CZ):USER_GENERAL) : e,k<USER_NONE?k:TSEL->preset);
}
static void preset_mark(int on)
{
    uint32_t k = user_of(TSEL);
    uint32_t e=TSEL->eng_req;
    if (favorite_set(k<USER_NONE ? (TSEL->user_native?(e==ENGI_PROPHET?USER_NATIVE_P5:e==ENGI_FM6?USER_NATIVE_FM:USER_NATIVE_CZ):USER_GENERAL) : e,k<USER_NONE?k:TSEL->preset,on)) {
        ui.force = 1;
        settings_save();
    }
}

static void preset_hinted(void) { ui.force = 1; }  /* after a sound load (the phrases it suggested: gone, 2026-10-10) */

static void preset_go(uint32_t n)                    /* load list index n into the selected track (the sound only) */
{
    uint32_t k, e = preset_at(n, &k);
    if(e==USER_NATIVE_P5 || e==USER_NATIVE_FM || e==USER_NATIVE_CZ){native_load(e==USER_NATIVE_P5?ENGI_PROPHET:e==USER_NATIVE_FM?ENGI_FM6:ENGI_CZ,k,song.sel);}
    else if (e == USER_GENERAL) {
        up_load(k);
    } else {
        if (e != TSEL->eng_req) {
            set_engine(e);
            ui_say("ENGINE ", ENGINES[TSEL->eng_req]->name);
        }
        apply_preset(k);
    }
    recent_loaded();
    preset_hinted();
}

/* the EDIT layer's KNOB 2 (ui_layer.c): the selected track's engine only: its factory presets, then the used user presets
 * made with it (slot order). List index of the current sound; *total the length */
static uint32_t eng_list_pos(uint32_t *total)
{
    uint32_t e = TSEL->eng_req % NENGINES, np = ENGINES[e]->npresets, u = user_of(TSEL), k, n = 0;
    uint32_t cur = np ? TSEL->preset % np : 0u;
    uint32_t nn=native_count(e);
    if(TSEL->user_native && u<USER_NONE)cur=np+native_rank(e,u);
    for (k = 0; k < UP_SLOTS; k++)
        if (up_used(k) && up_engine(k) == e) {
            if (!TSEL->user_native && k == u)
                cur = np + nn + n;
            n++;
        }
    *total = np + nn + n;
    return cur;
}

static void eng_list_step(int32_t direction)         /* the next / previous sound of the engine (wraps) */
{
    uint32_t total, cur = eng_list_pos(&total), e = TSEL->eng_req % NENGINES, np = ENGINES[e]->npresets, k, n;
    if (total < 2u)
        return;
    n = (cur + (direction > 0 ? 1u : total - 1u)) % total;
    if (n < np) {
        apply_preset(n);
    } else {
        n -= np;
        if(n<native_count(e)){native_load(e,native_nth(e,n),song.sel);recent_loaded();preset_hinted();return;}
        n-=native_count(e);
        for (k = 0; k < UP_SLOTS; k++)
            if (up_used(k) && up_engine(k) == e && !n--) {
                up_load(k);
                break;
            }
    }
    recent_loaded();
    preset_hinted();
}

/* Seven display rows. ALL is a carousel; FAV, RECENT and a category use a bounded window.
 * Return total for an empty row; a current sound not in the list shows the start. */
static uint32_t preset_visible(uint32_t cur, uint32_t total, uint32_t row)
{
    uint32_t first, last;
    if (!total || row >= 7u) return total;
    if (list_mode() == LM_ALL)
        return (cur + total * 4u + row - 3u) % total;
    first = cur < total && cur > 3u ? cur - 3u : 0u;
    last = total > 7u ? total - 7u : 0u;
    if (first > last) first = last;
    return first + row < total ? first + row : total;
}

/* Stage's (HOME's) four knobs: the selected track's engine's EDIT values (engine_t .knob), a native sound's own panel
 * values for PROPHET (filter, its envelope amount, the amp release) and CZ-1 (line 1's wave and DCW peak, detune,
 * vibrato): a page of their scope, edited and drawn as its pages are (ui_input.c edit_param, ui_stage.c) */
static page_t stage_pg;
static const page_t *stage_page(void)
{
    static const uint8_t P5_K[4] = {P5_CUTOFF, P5_RESONANCE, P5_ENV_FILTER, P5_RELEASE_AMP};
    static const uint8_t CZ_K[4] = {LCZ_LBASE(0) + LCZ_W1, LCZ_EBASE(0, 1) + 8, LCZ_FINE, LCZ_VDEP};
    uint32_t e = eng_idx(TSEL->eng_req), k;
    stage_pg.title = "STAGE";
    stage_pg.fam = FAM_HOME;
    stage_pg.graph = GR_NONE;
    stage_pg.scope = e == ENGI_PROPHET ? SC_P5 : e == ENGI_CZ ? SC_CZ1 : SC_ENGINE;
    for (k = 0; k < 4u; k++)
        stage_pg.id[k] = e == ENGI_PROPHET ? P5_K[k] : e == ENGI_CZ ? CZ_K[k] : ENGINES[e]->knob[k];
    return &stage_pg;
}
/* HOME: what KNOB k edits (0: nothing there) */
static const param_desc_t *home_param(uint32_t k, int16_t **vp)
{
    return page_desc(stage_page(), k & 3u, vp);
}

/* select track i (KNOB 1 on TRACKS, the editor): its sound, pages and pattern from now on */
static void track_select(uint32_t i)
{
    if (i >= NTRK || i == song.sel)
        return;
    browse_commit();                             /* (a sound still pending loads into the track it was meant for) */
    momentary_restore();
    song.sel = (uint8_t)i;
    ui.entry_open = 0;
    seq_midi_reset();                            /* (MIDI notes held for the other track enter nothing here) */
    ui.hot_t = 0;
    ui.cursor = 0;
    ui.bank = 0;
    sync_reload = 1;
    ui.force = 1;
}

static int slice_page_ok(void) { return 0; }
static int slice_page_on(void) { return 0; }

/* ---------------------------------------------------- action pages --- */
/* Pages whose purpose is an action (SONG, EDIT > SLICES, CZ TOOLS): the knobs pick, OCT+ does it, OCT- cancels
 * the picked action or goes HOME (ui_input.c). The slot pages (PROJECT, USER, the STOREs: ui_slots.c) do their
 * actions from a slot's sheet, through act_do too */
static uint32_t act_cols(void)                   /* the columns that are actions, a bit each; 0 = not such a page */
{
    const page_t *pg = cur_page();
    if (ui.home)
        return 0;
    if (pg->graph == GR_SONG)
        return 1u;                               /* PLAY / STOP (also the PLAY button; TAKE JAM: REC held) */
    if (pg->graph == GR_SLICES)
        return slice_page_ok() ? 12u : 0u;       /* SPLIT JOIN (a SLICE track only) */
    if (pg->graph == GR_CZTOOLS)
        return 15u;                              /* NAME 1>2 2>1 COMP */
    return 0;
}

/* the action OCT+ does: its column + 1, 0 = none picked yet */
static uint32_t act_col(void)
{
    if (!ui.home && cur_page()->graph == GR_SONG) return 1u;
    return ui.act;
}

static const char *act_name(uint32_t c)          /* column c's action (the footer hint) */
{
    if (cur_page()->graph == GR_SONG)
        return song.playing || chain_busy() ? "STOP" : "PLAY";
    if (cur_page()->graph == GR_SLICES)
        return c == 3u ? "JOIN" : "SPLIT";
    return CZ_ACTIONS[c & 3u].label;             /* (CZ TOOLS) */
}

/* the picked action would do something now (OCT+ blinks) */
static int act_ready(void)
{
    uint32_t c = act_col();
    if (!c--)
        return 0;
    if (cur_page()->graph == GR_SONG)
        return song.playing || chain_busy() || chain_config.count;
#if MELODEE_SLICE
    if (cur_page()->graph == GR_SLICES)
        return slice_act_ready(c);
#endif
    return !chain_busy();                        /* (CZ TOOLS) */
}

#include "seq_edit.c"                             /* SEQ > STEP: a note's length, its move, its deletion */
#include "seq_notes.c"                            /* NOTES: select original hits without snapping them */
#include "seq_undo.c"                             /* .. and the eight edits SAVE held undoes */
