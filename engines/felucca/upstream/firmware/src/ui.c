/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FELUCCA user interface.
 * Flat: SURF cards and panels on the palette's background, no rules, one type family (Inter Tight, three sizes), tracks
 * named by their numbers 1..4 on a cushion (icons.c trk_icon). Four columns <-> KNOB 1..4. Rendering is lazy:
 * every element remembers what it last drew and is redrawn only on change. */
static int project_save(uint32_t slot);
static void panel_setup(void);
static void project_load(uint32_t slot);
static int project_used(uint32_t slot);
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
static int up_has_pat(uint32_t k);               /* user presets that hold a pattern */
static uint32_t up_pat_count(void);
static uint32_t up_pat_nth(uint32_t n);
static uint32_t up_pat_rank(uint32_t slot);
static void up_pat_load(track_t *t, uint32_t k);
static void up_auto_name(char *b, uint32_t e, uint32_t k);   /* naming (ui_name.c) */
static void up_ui_named(uint32_t op, uint32_t k, const char *name, uint32_t cat);
static uint32_t up_cat(uint32_t k);
static int project_save_as(uint32_t slot, const char *name);
static int project_name(uint32_t slot, char *b);
static int project_info(uint32_t slot, char *b, uint16_t *len48, uint32_t *den, uint8_t *n);
static int project_rename(uint32_t slot, const char *name);
static void project_cur_name(char *b);
static uint32_t user_of(const track_t *t)    /* user preset slot its sound came from, UP_SLOTS = none */
{
    return t->user && up_used(t->user - 1u) ? t->user - 1u : UP_SLOTS;
}
static uint32_t up_gen;                      /* bumped on every user bank change (redraws) */
#include "favorites.c"
#include "category.c"                   /* sound categories (Discussion #90): PRESETS > LIST */
/* MENU's two-valued settings (ui_menu.c MENU_FLAGS), a bit each in a byte no engine uses (as ui_layer.c layer_seen):
 * saved with the settings; 0 in older ones = every setting's default (append-only: a new setting takes a new bit,
 * its default is 0) */
#define ui_prefs (favorites.factory[15][30])
#define PREF_LATCH 1u                          /* MENU > FX LATCH ON (#40) */
#define PREF_ANIM_OFF 2u                       /* MENU > ANIM OFF (#46): values snap (no rolling digits, no glide) */
#define PREF_ACCEL 4u                          /* MENU > KNOB ACCEL ON (#52): fast turns of wide values x2..x8 */
#define PREF_USB_FIXED 8u                      /* MENU > USB LEVEL FIXED: USB audio at the full level, MASTER after */
#define PREF_BPM_LOCK 16u                      /* MENU > BPM LOCK ON (#58): SELECT sets the tempo only with GLO held */
#define PREF_LARGE 32u                         /* MENU > LARGE ON (#15, Discussion #80): big knob labels and values
                                                * (ui_draw.c large_kind); clear in every older setting = OFF */
#define PREF_SERIAL_OFF 64u                    /* MENU > USB SERIAL OFF (#67): no serial console, the device enumerates as
                                                * audio + MIDI only (usb.c usb_cdc_switch); clear in every older setting = ON */
#define PREF_RESTORE_OFF 128u                  /* MENU > RESTORE LAST OFF (1.2, Discussion #130): no autosave written, none
                                                * restored at power-on (project.c); clear in every older setting = ON */
#define fx_latch ui_prefs
/* MENU > AUDIO CLICK, CLICK LEVEL, COUNT-IN (1.1, Discussion #131; click.c, seq.c): two bits each in another byte no
 * engine uses, saved with the settings: bits 0-1 CLICK (OFF REC ON), 2-3 CLICK LEVEL (stored 0 MID, 1 LOW, 2 HIGH),
 * 4-5 COUNT-IN (OFF, 1 BAR, 2 BARS); 0 in older settings = OFF / MID / OFF, so nothing clicks or counts in that did
 * not; 3 (unknown) reads as 0 (settings_persist.c puts it back). Bit 6 (1.2): MENU > SCALE LEDS ON (Discussion #127,
 * ui_input.c scale_leds; 0 in older settings = OFF). Bit 7 free (append-only) */
#define ui_rec_prefs (favorites.factory[15][28])
#define PREF_REC 0xFF00u                       /* (menu_items.c MENU_FLAGS: a bit of ui_rec_prefs, << 8) */
#define PREF_SCALE_LEDS (0x40u << 8)
/* 1.2: three more two-valued MENU settings, bits of another byte no engine uses, saved with the settings; 0 in older
 * settings = today's behaviour (append-only, as ui_prefs): bit 0 DISPLAY > SCOPE MIX (Discussion #165, fx.c scope_mix),
 * bit 1 CONTROL > STEP PREVIEW ON (Discussion #169, ui_input.c step_preview), bit 2 CONTROL > CHORD ENTRY ADD (#155,
 * ui_input.c seq_entry). 1.5: bit 3 SYSTEM > HELP ON (Discussion #156, ui_help.c). Bits 4-7 free */
#define ui_prefs2 (favorites.factory[15][24])
#define PREF_X 0xFF0000u                       /* (menu_items.c MENU_FLAGS: a bit of ui_prefs2, << 16) */
#define PREF_SCOPE_MIX (0x01u << 16)
#define PREF_PREVIEW (0x02u << 16)
#define PREF_CHORD_ADD (0x04u << 16)
#define PREF_HELP (0x08u << 16)
/* 1.3 (Discussions #112, #134): MENU > DISPLAY > HOME, what the HOME page shows under its four cards: 0 SCOPE (the
 * oscilloscope, as before), 1 TRACKS (the four tracks as rows: ui_graph.c draw_home_tracks), 2 LEVELS (1.5, #134: the
 * rows of TRACKS, and KNOB 1..4 the LEVEL of T1..T4 in place of the engine's four: home_param). A byte of its own no
 * engine uses, saved with the settings; 0 in older settings = SCOPE; an unknown value (a later firmware's view) reads as
 * SCOPE and is kept as saved (1.3 / 1.4 read LEVELS so) */
#define ui_home_view (favorites.factory[15][26])
enum { HV_SCOPE, HV_TRACKS, HV_LEVELS, HV_COUNT };
static int home_tracks(void) { return ui_home_view == HV_TRACKS || ui_home_view == HV_LEVELS; }
static int home_levels(void) { return ui_home_view == HV_LEVELS; }
enum { RP_CLICK, RP_LEVEL, RP_COUNTIN };
static uint32_t rp_get(uint32_t f)                     /* a field as the MENU steps it: 0..2 */
{
    uint32_t v = (ui_rec_prefs >> (2u * f)) & 3u;
    v = v == 3u ? 0u : v;
    return f == RP_LEVEL ? (v == 0u ? 1u : v == 1u ? 0u : 2u) : v;   /* (LEVEL: LOW MID HIGH, MID stored as 0) */
}
static void rp_apply(void)                             /* the settings into click.c / seq.c (every frame: ui_input) */
{
    click_mode = (uint8_t)rp_get(RP_CLICK);
    click_lvl = (uint8_t)rp_get(RP_LEVEL);
    cin_bars = (uint8_t)rp_get(RP_COUNTIN);
}
static void rp_put(uint32_t f, uint32_t v)
{
    uint32_t st = f == RP_LEVEL ? (v == 1u ? 0u : v == 0u ? 1u : 2u) : v % 3u;
    ui_rec_prefs = (uint8_t)((ui_rec_prefs & ~(3u << (2u * f))) | st << (2u * f));
    rp_apply();
}
/* MENU > STYLE (ui_menu.c): ST_FLAT ST_LINE (gfx.c) in another byte no engine uses, saved with the settings;
 * 0 in older ones = FLAT; 2, the retired PIXEL (1.0.1), reads as LINE; anything else unknown as FLAT (settings_persist.c). gfx.c draws from its copy, ux.style
 * (ui_draw.c style_apply) */
#define ui_style (favorites.factory[15][29])
static void draw_gaps(uint32_t y, uint32_t h);        /* (ui_draw.c) */

static uint8_t sync_reload;                  /* engine / preset / project / user preset loaded: editor RELOAD push */

#define ACC T_THEME                /* values, curves */
#define VAL(c) ((c) == ui.hot_col && ui.hot_t ? T_ACCENT : T_THEME)   /* the knob just turned: accent */
#define RATIO(d, v) ((d)->max > (d)->min ? ((int32_t)(v) - (d)->min) * 1000 / ((d)->max - (d)->min) : -1)
/* layout: header 0..24, four cards 28..72 (57 px at x 3 + 59 c),
 * the panel 76..198 (a SURF area; the graphs live in it), footer 202..240; BG between them */
#define Y_HEAD 0
#define H_HEAD 24
#define Y_LABEL 28                    /* the cards */
#define Y_SEP_END 72
#define CARD_W 57
#define CARD_H 44
#define CARD_X(c) (3 + 59 * (int32_t)(c))
#define Y_GRAPH 76                    /* the panel */
#define H_GRAPH 122
#define Y_FOOT 202
#define H_FOOT 38
/* MENU > LARGE (large_kind below): on the value pages the cards are tall (28..132) and the panel a strip
 * (136..198); elsewhere (lists, rolls, layers) the layout above, its card labels in M */
#define LG_CARD_H 104
#define LG_Y_GRAPH 136
#define LG_H_GRAPH 62

static struct {
    uint8_t home;
    uint8_t page;                /* index into PAGES */
    uint8_t fam_last[FAM_COUNT]; /* last page used per family */
    uint8_t bank;                /* SEQ: 16-step bank (follows the cursor) */
    uint8_t cursor;              /* SEQ: step being edited (STEP page KNOB 1 moves it) */
    uint8_t entry_open;          /* SEQ: keys held since the first press of this entry */
    uint8_t lane;                /* SEQ > STEP on a DRUM track (the grid): the lane the keys and KNOB 3 / 4 edit */
    uint8_t plk_src;             /* parameter locks (ui_input.c lock_*): the sound page whose KNOB 1..4 a step held
                                  * locks, PAGES index + 1; 0 = HOME's four */
    uint8_t plk_lane, plk_trk;   /* .. the grid: the lane and the track of the hits plk_rm takes away */
    uint64_t plk_rm, plk_acc;    /* .. the grid's step keys pressed on a hit (bit = step): it goes when the key is let
                                  * go, unless the key was held for a lock (its accent: plk_acc) */
    uint8_t hot_col, hot_t;      /* column whose knob was just turned (drawn white) */
    uint8_t menu;                /* 0 off, 1 list, 2 about + credits, 3 info (1.2) (HOME held) */
    uint8_t menu_sel;            /* MENU: the row (menu_items.c MI_*; its tab MI_TAB), kept while the device runs */
    uint8_t menu_row[5];         /* MENU: the row last picked in each tab, from its first (ALGORITHM comes back to it) */
    uint16_t menu_scroll;        /* continuous ABOUT + CREDITS position, pixels */
    uint32_t menu_sig, home_t0;  /* HOME press time (btn_hold) */
    uint8_t force;               /* full redraw pending */
    uint8_t msg_t;               /* transient message frames */
    uint8_t help_t;              /* 1.5, MENU > HELP ON: frames the page's hint stays in the footer (ui_help.c) */
    uint8_t bpm_t;               /* frames the BPM stays highlighted after a SELECT turn */
    uint8_t act;                 /* action pages: the column whose action OCT+ does, + 1; 0 = none (act_col) */
    uint32_t rec_t0;             /* REC press time: a tap arms (btn_hold, no hold: held, REC opens its layer) */
    uint32_t save_t0;           /* SAVE press time (btn_hold: held = UNDO) */
    uint8_t confirm;             /* the OCT- / OCT+ dialog: CF_*, 0 = none */
    uint8_t confirm_trk;         /* the track it clears, the slot it overwrites */
    uint8_t uslot;               /* SAVE > USER: the selected user preset slot */
    uint8_t ppick;               /* SAVE > PHRASES: the pattern picked (pat_count list index) */
    uint8_t song_row;            /* SONG: the section selected, count selects + ADD */
    uint8_t song_trk;            /* SONG: the lane (track) selected, 0..NTRK-1 */
    uint16_t ev_row;             /* SEQ > AUTOMATION (ui_events.c ev_*): the row selected (0 PLAY / CLEAR, 1 QUANTIZE, the
                                  * last + ADD; 1.2: up to EV_ROWS, past 255) */
    uint8_t ev_step, ev_id;      /* .. + ADD: the step and the parameter (or EV_CHANCE, EV_RATCH, EV_NUDGE, EV_VEL) it
                                  * adds on */
    uint16_t ev_keep;            /* .. the CHANCE / RATCH / NUDGE row being edited (its EVC code, 0 none): listed at its default */
    uint8_t ev_vel;              /* .. 1.5: the VEL rows shown (+ ADD VEL's SHOW / HIDE; off when the page is entered) */
    uint8_t uboot;               /* main.c: seconds left before UPDATE MODE (OCT- + OCT+ held), 0 = none */
    uint32_t ly_t0;              /* the layer button's press time | 1, LY_* bits (ui_layer.c layer_gesture) */
    uint8_t ly;                  /* the layer whose button is down (LAYER_*), 0 = none */
    uint8_t layer;               /* the layer whose map is shown (LAYER_*), 0 = none */
    uint8_t lock;                /* #83: the layer locked open by a double tap (LAYER_*), 0 = none (ui_layer.c) */
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
    struct {                     /* MIDI LEARN (midi_learn.c; here, not loose globals: the audio ISR's data keeps its
                                  * layout and its cost, tests/target_budget.py) */
        uint8_t on;              /* MIDI LEARN is on */
        uint8_t pick, trk, id;   /* a parameter picked (1): its track and P_* id */
        volatile uint8_t io[2];  /* midi_control.c ml_arm, ml_heard */
    } ml;
    volatile uint8_t mpc[NTRK];  /* MIDI Program Change per track, 0x80 | program, 0 none (midi_control.c mpc_io: here
                                  * for the same reason as ml.io; midi_pc_poll) */
} ui;

enum { CF_NONE, CF_CLEAR_SEQ, CF_CLEAR_TRK, CF_OVR_PROJ, CF_OVR_USER, CF_LOAD_PAT,
       CF_DEL_ROW, CF_CLEAR_SONG, CF_INIT_SOUND, CF_CLEAR_MOTION, CF_ERASE_USER };   /* ui.confirm: TOOLS' clears;
                                   * SAVE over a used slot; a pattern over the user's steps;
                                   * USER ERASE */

/* MENU > DISPLAY > SCREEN OFF (1.1.5): after a while with no input on the panel the screen goes dark (the backlight
 * stays on: lcd.c lcd_power)
 * (a picture held for hours can stay faintly on the panel); the sound, the sequencer, MIDI and USB go on, the LEDs
 * stay as they are. NEVER (the default since 1.1.5.1), 5, 15, 30, 60 minutes, kept in a byte no engine uses, saved
 * with the settings (SCR_CODE). 1.1.5 stored value ^ 3, 0 (its default and every older setting) being 30 MIN; 1.1.5.1
 * reads that 0 as NEVER (the new default: the panel went dark for good on 1.1.5, see lcd.c lcd_power) and keeps the
 * other codes, 30 MIN now 4. Unknown codes read as NEVER.
 * Input is the panel's only: a button or key held or pressed, a knob turned (not MIDI, the editor or MASTER, an
 * analogue pot). The press, key or turn that wakes it is swallowed: no note (seq.c kb_asleep), no button, no
 * value, until everything is let go and the knobs rest for SCR_EAT_MS. A dialog, the count-in and the UPDATE MODE
 * countdown keep it on (and wake it); the crash screen, UPDATE and UBOOT wake it at once (lcd_wake_now).
 * Off: lcd_power(0) (DISPOFF, SLPIN) and nothing is drawn. Waking, a frame at a time (never a wait in
 * the loop): 120 ms after the SLPIN, SLPOUT (lcd_power(1)); 120 ms after that one frame redraws everything
 * (ui.force), then DISPON (lcd_power(2)) */
#define ui_scr (favorites.factory[15][27])
/* MENU > ANIM IDLE (1.2, Discussion #135; menu_items.c, ui_input.c idle_leds): bit 0, the LEDs' idle animation, in a
 * byte no engine uses, kept with the settings; 0 in every older setting = off. The other bits: 0 (room for a delay) */
#define ui_idle (favorites.factory[15][10])
#define SCR_DEF 0u                                     /* NEVER (1.1.5.1; 1.1.5: 30 MIN) */
static const uint8_t SCR_CODE[5] = {0, 2, 1, 4, 7};    /* the stored byte of NEVER 5 15 30 60 MIN (1.1.5: 3 2 1 0 7) */
static const uint8_t SCR_DEC[8] = {0, 2, 1, 0, 3, 0, 0, 4};   /* .. and back (3: 1.1.5's NEVER; 5, 6 unknown) */
#define SCR_EAT_MS 300u
static const uint8_t SCR_MIN[5] = {0, 5, 15, 30, 60};  /* NEVER, minutes */
enum { SCR_ON, SCR_OFF, SCR_WAKE, SCR_SLPOUT, SCR_SHOW };
static struct {
    uint8_t st;                  /* SCR_*: SCR_WAKE asked, SCR_SLPOUT sent, SCR_SHOW the frame that redraws */
    uint8_t eat;                 /* the wake gesture: panel input swallowed (ui_input) */
    uint32_t idle;               /* fm1_ms of the last panel input (or blocker) */
    uint32_t t;                  /* fm1_ms of the last panel command (SLPIN, SLPOUT) */
    uint32_t eat_t;              /* fm1_ms of the last swallowed knob detent */
} scrn;
static uint32_t scr_get(void)                          /* the MENU's value: 0 NEVER .. 4 60 MIN */
{
    return ui_scr < NELEM(SCR_DEC) ? SCR_DEC[ui_scr] : SCR_DEF;
}
static void scr_put(uint32_t v)                        /* (the time counts from the change: the editor's too) */
{
    ui_scr = SCR_CODE[v < NELEM(SCR_MIN) ? v : SCR_DEF];
    scrn.idle = fm1_ms;
}
static void scr_wake(void)
{
    if (scrn.st == SCR_OFF)
        scrn.st = SCR_WAKE;
    scrn.eat = 1;
}
static void scr_wake_now(void)                         /* main.c: the crash screen, UPDATE, UBOOT */
{
    if (scrn.st != SCR_ON)
        lcd_wake_now();
    scrn.st = SCR_ON;
    scrn.idle = fm1_ms;
}
/* ui_input, every pass: 1 = the pass is swallowed (the screen dark or waking, or the wake gesture going on) */
static int scr_input(uint32_t pressed, uint32_t notes)
{
    uint32_t held = fm1_in.buttons | fm1_in.notes, k;
    int32_t moved = 0;
    if (pressed | notes | held | panel_moved)
        scrn.idle = fm1_ms;
    panel_moved = 0;
    if (scrn.st == SCR_ON && !scrn.eat)
        return 0;
    for (k = 0; k < NE; k++)                           /* (every knob's turns go) */
        moved |= panel_enc(k);
    panel_moved = 0;
    if (moved)
        scrn.eat_t = scrn.idle = fm1_ms;
    if (pressed | notes | held | (uint32_t)moved)
        scr_wake();
    if (scrn.st != SCR_ON || held || (int32_t)(fm1_ms - scrn.eat_t) < (int32_t)SCR_EAT_MS)
        return 1;
    scrn.eat = 0;                                       /* all let go, the knobs at rest: input again */
    kb_asleep = 0;
    return 0;
}
/* ui_draw, every frame: 1 = draw it */
static int scr_frame(void)
{
    uint32_t lim = SCR_MIN[scr_get()] * 60000u;
    if (ui.confirm || ui.uboot || seq_counting()) {    /* (never dark under these) */
        scrn.idle = fm1_ms;
        if (scrn.st == SCR_OFF)
            scr_wake();
    }
    switch (scrn.st) {
    case SCR_ON:
        if (!lim || (int32_t)(fm1_ms - scrn.idle) < (int32_t)lim)   /* (signed: robust to a wrap) */
            return 1;
        lcd_power(0);
        scrn.st = SCR_OFF;
        scrn.t = fm1_ms;
        scrn.eat = 1;
        kb_asleep = 1;
        break;
    case SCR_WAKE:
        if (fm1_ms - scrn.t >= 120u) {
            lcd_power(1);
            scrn.st = SCR_SLPOUT;
            scrn.t = fm1_ms;
        }
        break;
    case SCR_SLPOUT:
        if (fm1_ms - scrn.t >= 120u) {
            scrn.st = SCR_SHOW;
            ui.force = 1;
            return 1;
        }
        break;
    case SCR_SHOW:
        return 1;
    }
    ui.msg_t = 0;                                      /* (dark: what would have shown is over; a clean page back) */
    ui.msg2[0] = 0;
    ui.hot_t = ui.bpm_t = 0;
    return 0;
}
static void scr_shown(void)                            /* after the frame: the redrawn page on */
{
    if (scrn.st != SCR_SHOW)
        return;
    lcd_power(2);
    scrn.st = SCR_ON;
    scrn.idle = fm1_ms;
}

static const page_t *page_over;   /* a quick layer's own four knobs (ui_layer.c), while it edits or draws them */
static const page_t *cur_page(void) { return page_over ? page_over : &PAGES[ui.page]; }

/* MENU > LARGE (#15, Discussion #80: what KNOB 1..4 do, in bigger type). Per page type:
 *   LK_TALL  HOME and the value pages (EDIT ENV LFO MOD FX SLICER SCL CHORD ARP VOICE, MIXER, CLOCK,
 *            TOOLS): tall cards (a K1..K4 keycap, the icon, the label in M, the value in L,
 *            in M or S when L is too wide or lacks a glyph, the unit under it, the gauge), the panel a strip: HOME's
 *            scope, ENV's ADSR, LFO's wave, MIXER's four tracks (name, state, meter) small; the
 *            page's title and number in L on the others (their charts cannot be read that small);
 *   LK_LABEL the list and graph pages (PRESETS, USER, PROJECT, PHRASES, SONG, the piano roll and the drum grid,
 *            AUTOMATION, SLICES), HOME with MENU > HOME TRACKS (1.3) and the quick layers' maps: the layout as it
 *            is, the card labels in M;
 *   LK_OFF   LARGE off; the menu (its own LARGE: ui_menu.c), the dialogs and NAME keep their own layout in every case. */
enum { LK_OFF, LK_LABEL, LK_TALL };
static uint32_t large_kind(void)
{
    uint32_t g;
    if (!(ui_prefs & PREF_LARGE))
        return LK_OFF;
    if (ui.layer)
        return LK_LABEL;
    if (ui.home)                                        /* (HOME > TRACKS: its rows need the panel, the cards as on */
        return home_tracks() ? LK_LABEL : LK_TALL;      /* a list page; the rows' names in M: draw_home_tracks) */
    g = cur_page()->graph;
    return g == GR_BROWSE || g == GR_SLOTS || g == GR_USER || g == GR_PATS || g == GR_SONG || g == GR_ROLL ||
           g == GR_SLICES || g == GR_EVENTS ? LK_LABEL : LK_TALL;
}
/* the geometry of the page shown: the cards' height, the panel's top and height */
static uint32_t card_h(void) { return large_kind() == LK_TALL ? LG_CARD_H : CARD_H; }
static uint32_t graph_y(void) { return large_kind() == LK_TALL ? LG_Y_GRAPH : Y_GRAPH; }
static uint32_t graph_h(void) { return large_kind() == LK_TALL ? LG_H_GRAPH : H_GRAPH; }
static int large_face_has(const char *s)                /* every glyph of s in the sparse L face */
{
    for (; *s; s++)
        if (glyph_at(&AF_L, fold(&AF_L, (uint8_t)*s)) < 0)
            return 0;
    return 1;
}
static int32_t ink_w(const aafont_t *f, const char *s)
{
    int32_t b[4];
    text_ink(f, s, b);
    return b[2] > b[0] ? b[2] - b[0] : 0;
}

/* the quick layers (ui_layer.c): a button held, the keys and KNOB 1..4 are its shortcuts, its map over the page */
enum { LAYER_NONE, LAYER_FX, LAYER_GLO, LAYER_SCL, LAYER_EDIT, LAYER_SEQ, LAYER_REC, LAYER_N };
static void draw_layer(void);
static const char *layer_head(void);
static int layer_locked(void);
static uint32_t layer_leds(uint32_t *br);
static uint32_t layer_btn(void);

/* FM operator pages belong to DIGITAL; they never appear on other instruments (without FELUCCA_FM4: never). SLICES:
 * a SLICE track's (ui_slice.c); LANES / LANES 2 a DRUM track's (its lane levels); FILTER an ANALOG track's; OPERATOR, OP ENV, OPERATOR 2 an FM6
 * track's (its patch's operators, ui_fm6op.c) */
static int page_visible(uint32_t i)
{
    if (PAGES[i].scope == SC_TRACK && PAGES[i].id[0] >= P_LN0 && PAGES[i].id[0] <= P_LN7)
        return TSEL->eng_req % NENGINES == ENGI_DRUM;
    if (PAGES[i].id[0] == P_FTYPE)                    /* FILTER (1.5, #104): ANALOG's */
        return ENGINES[TSEL->eng_req % NENGINES] == &ENG_ANALOG;
    if (PAGES[i].graph >= GR_FMOP && PAGES[i].graph <= GR_FMENV)
        return TSEL->eng_req % NENGINES == ENGI_FM6;
#if FELUCCA_SLICE
    if (PAGES[i].graph == GR_SLICES)
        return ENGINES[TSEL->eng_req % NENGINES] == &ENG_SLICE;
#else
    if (PAGES[i].graph == GR_SLICES)
        return 0;
#endif
    return !(PAGES[i].fam == FAM_EDIT && PAGES[i].id[0] >= P_FM1_ATK &&
             PAGES[i].id[0] <= P_FM4_LEVEL) || (FELUCCA_FM4 && TSEL->eng_req % NENGINES == ENGI_DIGITAL);
}

static uint32_t page_first(uint32_t fam)
{
    uint32_t i;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].fam == fam)
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
/* a message led by an icon: its first byte (ui_draw.c draw_head draws the icon, then the words) */
#define MSG_NOFILE "\x01"                      /* the no-file icon (ICON_X_NOFILE) */
/* a missing sample (ui_input.c sample_notice): NO SAMPLE (SAMPLE NOT FOUND, the SLICES page's, is 4 px too wide
 * for the header's 130 px after the icon) */
#define MSG_NO_SAMPLE MSG_NOFILE "NO SAMPLE"

static int chain_busy(void) { return chain.running || chain.armed; }
/* Main loop only, with interrupts enabled. PLAY may be consumed between reads;
 * take one coherent snapshot before a flash operation or song preparation. */
static int transport_busy(void)
{
    int busy;
    fm1_irq_off();
    busy = song.playing || chain_busy() || transport_req == 1u || seq_counting();   /* (a count-in: as playing) */
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
        ui_message(rc == 1u ? "ADD A SECTION" : "STOP FIRST");
    }
}


/* parameter locks: a page whose KNOB 1..4 can lock (a track's sound: one of its parameters can be motion; not MOD's
 * slots), the parameter KNOB k locks (ui.plk_src's page, HOME's four), 0xFF = none */
static int lock_page_ok(uint32_t i)
{
    uint32_t k;
    if (i >= NPAGES || (PAGES[i].scope != SC_TRACK && PAGES[i].scope != SC_ENGINE) || PAGES[i].graph == GR_MOD)
        return 0;
    for (k = 0; k < 4u; k++)
        if (PAGES[i].id[k] != 0xFFu && motion_param(PAGES[i].id[k]))
            return 1;
    return 0;
}
static uint32_t lock_id(uint32_t k)
{
    uint32_t id = ui.plk_src && lock_page_ok(ui.plk_src - 1u) ? PAGES[ui.plk_src - 1u].id[k & 3u]
                                                               : ENGINES[TSEL->eng_req % NENGINES]->knob[k & 3u];
    return id < P_COUNT && motion_param(id) ? id : 0xFFu;
}

static void ev_enter(void);
/* 1.5 (Discussion #156): MENU > HELP ON: a page entered (here, go_home) shows its hint in the footer for ~2 s
 * (ui_help.c, ui_draw.c draw_foot) */
#define HELP_FRAMES 120u                       /* (~2 s at the UI's ~60 frames a second) */
static int help_on(void) { return (ui_prefs2 & (PREF_HELP >> 16)) != 0u; }
static void help_start(void) { ui.help_t = (uint8_t)(help_on() ? HELP_FRAMES : 0u); }
static void page_entered(void)
{
    const page_t *pg = cur_page();
    if (!ui.home && pg->graph == GR_EVENTS)      /* AUTOMATION: + ADD on the SEQ cursor's step */
        ev_enter();
    if (!ui.home && lock_page_ok(ui.page))
        ui.plk_src = (uint8_t)(ui.page + 1u);
    song.seq_mode = !ui.home && pg->fam == FAM_SEQ;
    ui.entry_open = 0;
    ui.hot_t = 0;                                /* clear the previous page's emphasis */
    ui.act = pg->graph == GR_USER ? 4u : 0u;     /* the save screen is ready for OCT+ */
    help_start();
    ui.force = 1;
}

static int step_on(const step_t *st) { return st->time == ST_NOTE && (st->n || st->hit); }
/* 1.5.1 (#199): a step whose CHANCE, RATCH or NUDGE is off its default: marked on the roll and the grid (ui_graph.c).
 * Not VEL: every recorded step has one */
static int step_detailed(const step_t *st)
{
    return step_chance(st) < 100u || step_ratchet(st) > 1u || step_nudge(st) != 0;
}

static void step_clear(step_t *st)
{
    st->n = 0;
    st->time = ST_REST;
    st->flags = 0;
    st->vel = 0;
    st->hit = st->acc = 0;
    st->probability = 0;
}

/* LEN (1.5, Discussions #178, #173): a NOTE step's length on the piano roll is the step and the TIE steps after it
 * (inside LEN, no wrap); SEQ > STEP's KNOB 3 on a note sets it (ui_input.c step_edit). Longer: the steps after it
 * become clean TIEs (a REST or an empty step; it stops at the next step holding notes or hits, a REST's kept ones too,
 * which stays, and at LEN);
 * shorter: its last TIEs become RESTs (step_clear). The steps' locks stay. Returns the length it got */
static uint32_t note_len(const track_t *t, uint32_t i)
{
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), n = 1;
    while (i + n < len && t->step[i + n].time == ST_TIE)
        n++;
    return n;
}
static uint32_t note_set_len(track_t *t, uint32_t i, uint32_t want)
{
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), n = note_len(t, i);
    for (; n > want && n > 1u; n--)
        step_clear(&t->step[i + n - 1u]);
    for (; n < want && i + n < len && !t->step[i + n].n && !t->step[i + n].hit; n++) {
        step_clear(&t->step[i + n]);
        t->step[i + n].time = ST_TIE;
    }
    return n;
}

/* ------------------------------------------------------- the DRUM grid --- */
/* SEQ > STEP on a DRUM track is the grid: 8 lanes x the 16 steps of a page. The white keys are the
 * steps of the page shown (a tap toggles the selected lane there), black keys 1..8 select the lane (and play
 * it), black key 9 held is ACC (white keys toggle accents, their LEDs show them), black keys 10 / 11 the page
 * down / up. KNOB 1 STEP, 2 LANE, 3 HIT, 4 ACC edit the cursor step. A sound load never converts the
 * steps: the grid shows a step's notes on their lanes (eng_drum.c step_lanes) and an edit makes the lane its
 * own (grid_own). Live recording on a DRUM track writes hits (seq.c rec_note) */
static int grid_on(void) { return !ui.home && !ui.menu && !ui.confirm && cur_page()->graph == GR_ROLL && drum_track(TSEL); }
/* SEQ > DETAIL (1.5.1, Discussion #199; params.c, between STEP and AUTOMATION): a step page as STEP (the cursor, the
 * roll's keys, the grid's lane and page keys), its knobs the cursor step's CHANCE RATCH NUDGE VEL (ui_input.c sd_turn),
 * its panel their lanes (ui_graph.c graph_detail). No state of its own: the page is what says so */
static int detail_on(void)
{
    return !ui.home && !ui.menu && !ui.confirm && cur_page()->scope == SC_STEP && str_eq(cur_page()->title, "DETAIL");
}
/* GLO > SONG (1.2): the white keys A B C D (A3 B3 C4 D4, white places 2..5: seq.c song_key) set the selected cell's slot;
 * they do not sound there, the other keys play as anywhere (seq.c keyboard_block: song.grid 3) */
static int song_keys_on(void) { return !ui.home && !ui.menu && !ui.confirm && !ui.layer && cur_page()->graph == GR_SONG; }

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
static void cursor_set(int32_t c)
{
    int32_t len = TSEL->p[P_SLEN] > 0 ? TSEL->p[P_SLEN] : 1;
    ui.cursor = (uint8_t)((c % len + len) % len);
    ui.bank = (uint8_t)(ui.cursor / 16u);
    ui.entry_open = 0;
}

static void cursor_fix(void)                           /* LEN got shorter: onto the last step */
{
    if (ui.cursor >= (uint32_t)TSEL->p[P_SLEN])
        cursor_set(TSEL->p[P_SLEN] - 1);
}

static void note_name(char *b, uint32_t n)
{
    str_cpy(b, N_NOTE[n % 12u], 4);
    fmt_int(b + str_len(b), (int32_t)(n / 12u) - 1);
}

/* a page button tapped (1.2, Discussion #153: SEQ STEP <-> AUTOMATION, GLO SONG (its only page: GLOBAL became HOME's
 * CLOCK, SYSTEM went to the MENU), SAVE USER -> PRESETS -> PHRASES -> PROJECT -> TOOLS (1.5, #173: PRESETS second; up
 * to 1.4.1 the last); HOME: home_tap). From another
 * family the family's page shown last (SAVE: USER); one that is not the family's any more (a page that moved, as GLOBAL
 * and SYSTEM in 1.2), or not shown for this track, gives the family's first */
static void open_family(uint32_t fam)
{
    if (!ui.home && cur_page()->fam == fam) {          /* same button again: next page */
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
        uint32_t i = ui.fam_last[fam];
        ui.page = (uint8_t)(i && i < NPAGES && PAGES[i].fam == fam && page_visible(i) ? i : page_first(fam));
    }
    ui.fam_last[fam] = ui.page;
    ui.home = 0;
    page_entered();
}

static void go_home(void)
{
    ui.home = 1;
    ui.plk_src = 0;                              /* (parameter locks: HOME's four) */
    ui.entry_open = 0;
    ui.hot_t = 0;
    song.seq_mode = 0;
    ui.force = 1;
    help_start();
}

/* HOME tapped (1.2): HOME -> MIXER (up to 1.1.5 GLO's first page) -> CLOCK (up to 1.1.5 GLO > GLOBAL) -> HOME; from any
 * other page HOME. HOME's pages are FAM_TRK's, in PAGES order, always from the first (none remembered). Held: the menu
 * (ui_input) */
static void home_tap(void)
{
    uint32_t i = ui.page + 1u;
    if (ui.home) {
        ui.fam_last[FAM_TRK] = 0;                     /* (open_family: the family's first, the MIXER) */
        open_family(FAM_TRK);
    } else if (cur_page()->fam == FAM_TRK && i < NPAGES && PAGES[i].fam == FAM_TRK) {
        open_family(FAM_TRK);                         /* (on HOME's page again: the next one) */
    } else {
        go_home();
    }
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

/* One-step UNDO of a load. A sound load (a factory or user preset, an engine jump, TOOLS INIT, the
 * editor's PRESET / G_ENGSEL / UP_LOAD) changes the sound only; a pattern load (SAVE > PHRASES) changes
 * the steps and the pattern parameters only. Each first copies the track as it was; SAVE held 0.7 s
 * swaps back what the loads changed (held again: the loads again), so steps recorded after a sound
 * load, or a sound edited after a pattern load, stay as they are. One copy for all tracks: the last
 * load wins. Loads in a row on one track with nothing changed in between (the PRESETS knob through the
 * list, one pattern after the other, an editor audition) keep the copy from before the first, so the
 * undo goes back past the whole browse. Not snapshotted: power-on, projects.
 * 1.1.5: a lock or automation edit by hand (a step held and a knob turned, a step's locks cleared, SEQ > AUTOMATION)
 * takes the copy too (UNDO_MOT: SAVE held puts the track's motion back, the sound and the steps stay; motion_undo_take).
 * A sound load keeps the track's motion on the parameters every engine has (load_end, motion.c motion_sound_loaded);
 * a pattern load drops it all (the steps it was on are gone) */
enum { UNDO_SOUND = 1, UNDO_PAT = 2, UNDO_MOT = 4 };
static struct {
    uint8_t trk;                 /* track + 1, 0 = nothing to undo */
    uint8_t keep;                /* the track is as the last load left it (undo.after): a next load keeps the copy */
    uint8_t what;                /* UNDO_SOUND | UNDO_PAT: what the loads since the copy changed (undo_swap) */
    uint8_t eng, preset, user, patn;
    uint8_t fm6_slot;            /* the track's FM6 patch and its SLOT (eng_fm6.c): an edited or a project's */
    uint8_t fm6[FP_SIZE + 1u];   /* patch is the track's own, not a factory one */
    int16_t p[P_COUNT];
    step_t step[NSTEP];
    motion_store_t motion_backup; /* one track only, swaps with the shared event pool on undo */
    uint32_t after;              /* track_sig right after the last load */
    uint32_t pat;                /* pat_sig[] of the copy */
    uint32_t from_h;             /* pat_from_h[], pat_from[] of the copy (the header's slot letter) */
    uint8_t from;
    uint32_t t_ms;               /* time of the last load (the editor's SETs after it belong to it) */
} undo __attribute__((section(".pool")));   /* (main loop only: off the audio code's .bss) */
static uint8_t undo_depth;       /* loads nest (an engine jump loads its first preset): the outer one counts;
                                  * felucca_init / project_load raise it to take no copy at all */
static uint32_t pat_sig[NTRK];   /* steps_sig of the pattern the last pattern load put into each track: such
                                  * steps, untouched, are replaced by the next pattern without asking */
static uint8_t pat_last[NTRK];   /* that pattern's list index + 1, 0 = none */
/* the header's slot letter (ui_draw.c head_slot): the PROJECT slot each track's working pattern came from (the last
 * PROJECT load or save: 1..4 = A..D; 0 none: power-on, a PHRASES pattern, the editor's restore) and pat_from_sig() of
 * it then: an edit since shows the letter with a mark */
static uint8_t pat_from[NTRK];
static uint32_t pat_from_h[NTRK];
static uint8_t load_from;        /* a sound load (the outer one): the engine before it (load_end) */
static uint8_t load_mo;          /* .. the track's motion paused while the sound changes: bit 0 set, bit 1 it was ON */

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
    uint8_t id[3] = {t->eng_req, t->preset, t->user};
    uint32_t h = fnv(fnv(steps_sig(t), t->p, sizeof t->p), id, 3), k = trk_index(t);
    h = fnv(h, fm6_patch[k], sizeof fm6_patch[k]); /* (a patch the editor sent between two loads) */
    for (uint32_t j = 0; j < motion.count; j++)
        if ((motion.event[j].place >> 6) == k) h = fnv(h, &motion.event[j], sizeof motion.event[j]);
    return h ^ ((motion.on >> k) & 1u);
}
static uint32_t motion_sig(const track_t *t)     /* the track's motion only (the sound moves while it plays) */
{
    uint32_t h = 2166136261u, k = trk_index(t);
    for (uint32_t j = 0; j < motion.count; j++)
        if ((motion.event[j].place >> 6) == k) h = fnv(h, &motion.event[j], sizeof motion.event[j]);
    return h ^ ((motion.on >> k) & 1u);
}
/* the track's pattern as a song plays it from a slot: the steps, LEN DIV SWING GATE, QUANTIZE, the automation (the
 * editable ones: while a song plays the header shows the section's slot instead) */
static uint32_t pat_from_sig(const track_t *t)
{
    int16_t q = t->p[P_SQNT];
    uint32_t h = fnv(fnv(steps_sig(t), &t->p[P_SLEN], 4u * sizeof t->p[0]), &q, sizeof q);
    return h ^ motion_sig(t);
}
static void pat_from_set(uint32_t slot)             /* every track's pattern is slot's now (1..4), 0 none */
{
    for (uint32_t k = 0; k < NTRK; k++) {
        pat_from[k] = (uint8_t)slot;
        pat_from_h[k] = pat_from_sig(&trk[k]);
    }
}

/* load_begin, the copy taken: a pattern load drops the track's motion (its steps go); a sound load pauses it (PLAY
 * OFF while the sound changes under it) and load_end keeps what still means the same (motion_sound_loaded) */
static void load_motion_hold(track_t *t, uint32_t what)
{
    if (what & UNDO_PAT) {
        motion_reset(t);
        load_mo = 0;
        return;
    }
    load_from = t->eng_req;
    load_mo = (uint8_t)(1u | (motion_enabled(t) ? 2u : 0u));
    motion_set_enabled(t, 0);
}

/* MOMENTARY (ui_input.c mom_turn: LFO held, the knobs' changes go back when it is let go): the values turned, where
 * they are (a track's p[], song.g[]) and what they were. In the main loop's pool (no .bss) */
#define MOM_N 8u
static struct {
    uint8_t n;                                    /* values held, 0 = none */
    int16_t *vp[MOM_N];
    int16_t v[MOM_N];
} mom __attribute__((section(".pool")));
static void mom_restore(void)                     /* LFO let go: every value back */
{
    while (mom.n) {
        mom.n--;
        *mom.vp[mom.n] = mom.v[mom.n];
    }
}
static void mom_back(const track_t *t)            /* a sound load into t (0: a project): its values back first */
{
    uint32_t i, k = 0;
    for (i = 0; i < mom.n; i++)
        if (!t || (mom.vp[i] >= t->p && mom.vp[i] < t->p + P_COUNT) || (mom.vp[i] >= song.g && mom.vp[i] < song.g + G_COUNT)) {
            *mom.vp[i] = mom.v[i];                /* (a global: back too, before the load's undo copy) */
        } else {
            mom.vp[k] = mom.vp[i];
            mom.v[k++] = mom.v[i];
        }
    mom.n = (uint8_t)k;
}
static void load_begin(track_t *t, uint32_t what)
{
    uint32_t i = trk_index(t);
    if (what & UNDO_SOUND) {
        mom_back(t);                              /* (MOMENTARY: the sound as it was, then the load) */
        drum_mute[i] = 0;                         /* (1.4.1: the lanes' mutes go with the sound) */
    }
    if (undo_depth++)
        return;
    motion_restore(t);
    if (undo.keep && undo.trk == i + 1u && !(undo.what & UNDO_MOT) && track_sig(t) == undo.after) {
        undo.what |= (uint8_t)what;               /* browsing on: the copy from before the first load stays */
        load_motion_hold(t, what);
        return;
    }
    undo.trk = (uint8_t)(i + 1u);
    undo.what = (uint8_t)what;
    undo.eng = t->eng_req;
    undo.preset = t->preset;
    undo.user = t->user;
    memcpy(undo.p, t->p, sizeof undo.p);
    memcpy(undo.step, t->step, sizeof undo.step);
    memcpy(undo.fm6, fm6_patch[i], FP_SIZE);
    undo.fm6_slot = fm6_slot[i];
    undo.pat = pat_sig[i];
    undo.from = pat_from[i];
    undo.from_h = pat_from_h[i];
    undo.patn = pat_last[i];
    motion_snapshot_track(t, &undo.motion_backup);
    load_motion_hold(t, what);
}

static void load_end(track_t *t)
{
    if (--undo_depth)
        return;
    if (load_mo & 1u) {                           /* a sound load: the motion on the common parameters stays */
        (void)motion_sound_loaded(t, load_from, t->eng_req);
        if ((load_mo & 2u) && motion_count(t))
            motion_set_enabled(t, 1);
        load_mo = 0;
    }
    motion_rebase(t);
    undo.after = track_sig(t);
    undo.keep = 1;
    undo.t_ms = fm1_ms;
}

/* the editor's SET right after a load on the selected track (an audition: G_ENGSEL, then the patch's
 * values): part of that load, the copy from before it stays */
static void load_extend(track_t *t)
{
    if (undo.keep && undo.trk == trk_index(t) + 1u && !(undo.what & UNDO_MOT) && fm1_ms - undo.t_ms < 1500u) {
        undo.after = track_sig(t);
        undo.t_ms = fm1_ms;
    }
}

/* 1.1.5: a lock / automation edit by hand on track t is about to happen: the undo copy of the track as it is (as
 * ui_tools.c tl_undo_take, UNDO_MOT: the swap puts back its motion only). A knob turned on (the same tag, not 0:
 * the same knob of the same gesture), on the same track, nothing else changed and less than 1.5 s apart, shares the
 * copy from before its first detent; tag 0 (an add, a delete, a clear, a kind) always takes its own.
 * motion_undo_done after each */
static uint32_t undo_mot_tag;
/* what an edit's copy compares (undo.after): the motion; with the steps (UNDO_PAT: step_undo_take) the steps too */
static uint32_t edit_sig(const track_t *t) { return undo.what & UNDO_PAT ? motion_sig(t) ^ steps_sig(t) : motion_sig(t); }
static void edit_undo_take(track_t *t, uint32_t tag, uint32_t what)
{
    uint32_t i = trk_index(t);
    if (tag && tag == undo_mot_tag && undo.keep && undo.trk == i + 1u && undo.what == what &&
        edit_sig(t) == undo.after && fm1_ms - undo.t_ms < 1500u)
        return;
    undo_mot_tag = tag;
    undo.trk = (uint8_t)(i + 1u);
    undo.what = (uint8_t)what;
    undo.eng = t->eng_req;
    undo.preset = t->preset;
    undo.user = t->user;
    memcpy(undo.p, t->p, sizeof undo.p);
    memcpy(undo.step, t->step, sizeof undo.step);
    memcpy(undo.fm6, fm6_patch[i], FP_SIZE);
    undo.fm6_slot = fm6_slot[i];
    undo.pat = pat_sig[i];
    undo.from = pat_from[i];
    undo.from_h = pat_from_h[i];
    undo.patn = pat_last[i];
    motion_snapshot_track(t, &undo.motion_backup);
    undo.keep = 0;
}
static void motion_undo_take(track_t *t, uint32_t tag) { edit_undo_take(t, tag, UNDO_MOT); }
/* 1.2: a step's CHANCE / RATCH edited on SEQ > AUTOMATION (ui_events.c): the swap puts back the steps (and LEN DIV
 * SWING GATE, as copied) and the motion, the sound stays. motion_undo_done after each, as for a lock */
static void step_undo_take(track_t *t, uint32_t tag) { edit_undo_take(t, tag, UNDO_MOT | UNDO_PAT); }
static void motion_undo_done(track_t *t)
{
    undo.after = edit_sig(t);
    undo.keep = 1;
    undo.t_ms = fm1_ms;
}

static int param_kept(uint32_t i);

/* SAVE held: what the loads changed (undo.what) and the copy change places, so held again = the loads
 * again. The sound: engine, preset and its parameters (not param_kept); the pattern: the steps and LEN
 * DIV SWING GATE. The mix (LEVEL PAN MUTE) stays: no load changes it */
static void undo_swap(void)
{
    track_t *t;
    uint32_t i;
    char b[4] = {'T', 0, 0, 0};
    if (!undo.trk) {
        ui_message("NOTHING TO UNDO");
        return;
    }
    t = &trk[(undo.trk - 1u) % NTRK];
    motion_restore(t);
    motion_store_t current_motion;
    motion_snapshot_track(t, &current_motion);
    if (motion_replace_track(t, &undo.motion_backup) != 0) {
        ui_message("AUTOMATION FULL");
        return;
    }
    undo.motion_backup = current_motion;
    fm1_irq_off();                                /* the audio ISR must not see half a sound */
    if (undo.what & UNDO_SOUND) {
        uint8_t e = t->eng_req, pr = t->preset, u = t->user;
        panic_req |= (uint8_t)(1u << trk_index(t));
        t->eng_req = undo.eng;
        t->preset = undo.preset;
        t->user = undo.user;
        undo.eng = e;
        undo.preset = pr;
        undo.user = u;
        for (i = 0; i < P_COUNT; i++)
            if (!param_kept(i)) {
                int16_t v = t->p[i];
                t->p[i] = undo.p[i];
                undo.p[i] = v;
            }
    }
    if (undo.what & UNDO_PAT) {
        uint32_t ps = pat_sig[trk_index(t)];
        uint8_t pn = pat_last[trk_index(t)];
        pat_sig[trk_index(t)] = undo.pat;
        undo.pat = ps;
        pat_last[trk_index(t)] = undo.patn;
        undo.patn = pn;
        pn = pat_from[trk_index(t)];
        ps = pat_from_h[trk_index(t)];
        pat_from[trk_index(t)] = undo.from;
        pat_from_h[trk_index(t)] = undo.from_h;
        undo.from = pn;
        undo.from_h = ps;
        for (i = P_SLEN; i <= P_SGATE; i++) {
            int16_t v = t->p[i];
            t->p[i] = undo.p[i];
            undo.p[i] = v;
        }
        for (i = 0; i < NSTEP; i++) {
            step_t s = t->step[i];
            t->step[i] = undo.step[i];
            undo.step[i] = s;
        }
    }
    fm1_irq_on();
    if (undo.what & UNDO_SOUND) {                 /* FM6: the track's patch as it was (edited, a project's, a
                                                   * converted DIGITAL sound), not a factory one */
        uint32_t tr = trk_index(t);
        uint8_t v[FP_SIZE + 1u], sl = fm6_slot[tr];
        memcpy(v, fm6_patch[tr], FP_SIZE);
        fm6_set_patch(tr, undo.fm6);
        fm6_slot[tr] = undo.fm6_slot;             /* (fm6_poll: the patch stays) */
        fm6_own_ok &= (uint8_t)~(1u << tr);
        memcpy(undo.fm6, v, FP_SIZE);
        undo.fm6_slot = sl;
    }
    undo.keep = 0;                                /* the next load copies the track as it is now */
    if (t == TSEL)
        sync_reload = 1;
    b[1] = (char)('1' + trk_index(t));
    ui_say("UNDO/REDO ", b);
    ui.force = 1;
}

/* a 16-step pattern (PATTERNS[] format, user presets too) into steps 1..16, the rest empty, LEN 16 */
static void load_pat16(track_t *t, const uint8_t *note, const uint8_t *flags)
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++) {
        step_t *s = &t->step[i];
        uint8_t n = i < 16u ? note[i] : 0, fl = i < 16u ? flags[i] : 0;
        s->note[0] = n;
        s->n = n ? 1 : 0;
        s->time = (fl & 4u) ? ST_TIE : n ? ST_NOTE : ST_REST;
        s->flags = n ? (fl & (SF_ACCENT | SF_SLIDE | SF_RATCH)) : 0;
        s->vel = n ? 96 : 0;
        s->hit = s->acc = 0;
        s->probability = 0;
        if (drum_track(t))                           /* a DRUM track: the lanes' notes as its grid */
            step_to_grid(s);
    }
    t->p[P_SLEN] = 16;
}

/* a 16-step drum grid (user presets of version 3: lane hits, their accents) into steps 1..16, the rest empty,
 * LEN 16 */
static void load_grid16(track_t *t, const uint8_t *hit, const uint8_t *acc)
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++) {
        step_t *s = &t->step[i];
        step_clear(s);
        if (i < 16u && hit[i]) {
            s->time = ST_NOTE;
            s->hit = hit[i];
            s->acc = acc[i] & hit[i];
        }
    }
    t->p[P_SLEN] = 16;
}

static void track_defaults_steps(track_t *t)
{
    uint32_t i;
    motion_reset(t);
    for (i = 0; i < NSTEP; i++)
        step_clear(&t->step[i]);
}

/* SAVE > PHRASES: the factory patterns (PATTERNS[], "01".."13"), then the used user presets that hold
 * one ("U07"): list index n. Loading one replaces the track's steps 1..16 (the rest cleared) and LEN;
 * a user preset's pattern brings its stored LEN (at most 16), DIV, SWING and GATE too. The notes are
 * loaded as they are: the patterns are written for the register of their kind of sound, DRUM and
 * SLICE patterns are drum and slice numbers, and SCL TRANS / OCT transpose what plays */
static uint32_t pat_count(void) { return NPATTERNS + up_pat_count(); }

static void pat_label(uint32_t n, char *tag, char *name)   /* tag: 4 bytes ("01", "U07"), name: 13 */
{
    if (n < NPATTERNS) {
        tag[0] = (char)('0' + (n + 1u) / 10u);
        tag[1] = (char)('0' + (n + 1u) % 10u);
        tag[2] = 0;
        str_cpy(name, PATTERNS[n].name, 13);
    } else {
        uint32_t k = up_pat_nth(n - NPATTERNS);
        up_slot_label(tag, k);
        up_name(k, name);
    }
}

static void pat_load(track_t *t, uint32_t n)
{
    load_begin(t, UNDO_PAT);
    if (n < NPATTERNS)
        load_pat16(t, PATTERNS[n].note, PATTERNS[n].flags);
    else
        up_pat_load(t, up_pat_nth(n - NPATTERNS));
    pat_sig[trk_index(t)] = steps_sig(t);
    pat_last[trk_index(t)] = (uint8_t)(n + 1u);
    pat_from[trk_index(t)] = 0;                     /* (a phrase: no PROJECT slot) */
    load_end(t);
    ui.force = 1;
}

/* a pattern load would throw away steps of the user's (recorded, edited, from a project): ask first */
static int pat_needs_confirm(const track_t *t)
{
    return !seq_is_empty(t) && steps_sig(t) != pat_sig[trk_index(t)];
}

static uint32_t pat_pick(void)                   /* ui.ppick inside the list (user presets may be gone) */
{
    uint32_t n = pat_count();
    return ui.ppick < n ? ui.ppick : n - 1u;
}

/* SAVE > PHRASES LOAD: pattern n into track t, "LOADED 03 MELODY" */
static void pat_load_ui(track_t *t, uint32_t n)
{
    char tag[4], nm[13], b[20];
    pat_label(n, tag, nm);
    pat_load(t, n);
    str_cpy(b, tag, sizeof b);
    str_cpy(b + str_len(b), " ", 2);
    str_cpy(b + str_len(b), nm, sizeof b - str_len(b));
    ui_say("LOADED ", b);
}

/* the track's settings, not the sound's: what a sound load (factory or user preset, an engine jump,
 * TOOLS INIT) leaves alone. The mix (LEVEL, PAN, MUTE: the TRACKS faders), the arpeggiator (ARP, ARP 2),
 * the scale and key map (SCL), the pattern parameters (LEN, DIV, SWING, GATE; 1.2: QUANTIZE) and the SLICER
 * insert, which chops whatever the track plays in time with its sequencer */
static int param_kept(uint32_t i)
{
    return i == P_LEVEL || i == P_PAN || i == P_MUTE || (i >= P_AMODE && i <= P_SGATE) ||
           (i >= P_SLCR && i <= P_SLDEPTH) || i == P_CHRD || i == P_VOIC || i == P_SQNT;
}

/* a retired preset kept as an alias, so stored preset numbers stay valid: SAMPLE 1, once TRANH, is PIANO
 * (tools/gen_samples.py SMP_SET_ORIG). It loads as the original; browsing skips it. -> the preset k stands for.
 * (SAMPLE 4, once PERC, is past SMP_NPRESETS: apply_preset_to loads it as DRUM) */
static uint32_t preset_orig(const engine_t *e, uint32_t k)
{
    return e->presets == SMP_PRESET_TABLE && k < SMP_NSETS ? SMP_SET_ORIG[k] : k;
}

/* the presets of engine e that browsing shows before preset k (k = npresets: all of them) */
static uint32_t preset_rank(const engine_t *e, uint32_t k)
{
    uint32_t i, n = 0;
    if (e->presets != SMP_PRESET_TABLE)
        return k;
    for (i = 0; i < k; i++)
        n += preset_orig(e, i) == i;
    return n;
}

#define preset_shown(e) (ENGINES[e]->npresets - (ENGINES[e]->presets == SMP_PRESET_TABLE ? SMP_NALIAS : 0u))

#if !FELUCCA_FM4
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
    fm6_adopt(tr);                                    /* SLOT OWN: the converted patch is the track's own */
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
    t->user = 0;
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

static void set_engine_of(track_t *t, uint32_t ei);
/* preset pi of the engine the track asked for: the sound only (not the steps, not param_kept) */
static void apply_preset_to(track_t *t, uint32_t pi)
{
    const engine_t *e = ENGINES[t->eng_req % NENGINES];
    uint32_t i;
#if !FELUCCA_FM4
    if (t->eng_req % NENGINES == ENGI_DIGITAL) {
        fm4_load_preset(t, pi);
        return;
    }
#endif
    if (t->eng_req % NENGINES == ENGI_SAMPLE && pi == SMP_SET_PERC) {   /* SAMPLE preset 4 was PERC (retired, a stored */
        set_engine_of(t, ENGI_DRUM);                  /* number: the editor's PRESET, a favourite): DRUM's kit */
        return;                                       /* (core.h drum_from_perc) */
    }
    load_begin(t, UNDO_SOUND);
    panic_req |= (uint8_t)(1u << trk_index(t));       /* MONO/POLY may change: release what sounds */
    t->user = 0;
    if (t == TSEL)
        sync_reload = 1;
    if (!e->npresets) {
        load_end(t);
        return;
    }
    pi = preset_orig(e, pi % e->npresets);
    t->preset = (uint8_t)pi;
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
        for (i = 0; i < 4u; i++)
            t->p[P_DIST + i] = (int16_t)(pr->fx[i] ? pr->fx[i] - 1 : FX_DEF[i]);
    }
    fm6_track_loaded(t);                              /* FM6: the preset's patch (its SLOT) */
    load_end(t);
}

/* the engine's defaults and its first preset. With the audio IRQ off: the ISR sees the old engine with
 * its values or the new one with its own (voice.c engine_block), never one with the other's */
static void set_engine_of(track_t *t, uint32_t ei)
{
    const engine_t *e = ENGINES[ei % NENGINES];
    uint32_t i;
#if !FELUCCA_FM4
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
    apply_preset_to(t, 0);
    fm1_irq_on();
    load_end(t);
}

static void apply_preset(uint32_t pi) { apply_preset_to(TSEL, pi); }
static void set_engine(uint32_t ei) { set_engine_of(TSEL, ei); }

/* MIDI Program Change (1.5.1, #179 by renebohne): program n on track t, as the PRESETS list numbers the engine's sounds
 * from 0: its n-th preset, the aliases skipped (preset_orig), loaded as from there (the track's own settings kept, one
 * undo for a run of them). DRUM: its n-th KIT, the retired variants skipped (enum_orig); FM6: its presets (F1..F8),
 * then the voice bank's B1..B32 through SLOT as the knob loads them (fm6_poll; the rest of the sound stays), an empty
 * one ignored. A program past the last is ignored, not wrapped: a number is always the same sound */
static volatile uint8_t *const mpc_io = ui.mpc;
static void midi_pc_load(track_t *t, uint32_t n)
{
    uint32_t e = t->eng_req % NENGINES, k;
    if (e == ENGI_DRUM) {
        const param_desc_t *d = param_desc_of(e, P_E0);
        for (k = (uint32_t)d->min; k <= (uint32_t)d->max && (enum_orig(d, (int32_t)k) != (int32_t)k || n--); k++)
            ;
        if (k <= (uint32_t)d->max)
            t->p[P_E0] = (int16_t)k;
        return;
    }
    if (n >= preset_shown(e)) {
        int32_t s = (int32_t)(FM6_OWN + 1u + n - preset_shown(e));
        if (e == ENGI_FM6 && fm6_bank_slot(s) >= 0 && fm6_slot_ok(s))
            t->p[P_E7] = (int16_t)s;
        return;
    }
    for (k = 0; preset_orig(ENGINES[e], k) != k || n--; k++)   /* the n-th shown preset */
        ;
    apply_preset_to(t, k);
}
static void midi_pc_poll(void)                       /* main loop (ui_input) */
{
    uint32_t k, v;
    for (k = 0; k < NTRK; k++) {
        if (!ui.mpc[k])
            continue;
        fm1_irq_off();                               /* (the ISR may note another meanwhile) */
        v = ui.mpc[k];
        ui.mpc[k] = 0;
        fm1_irq_on();
        midi_pc_load(&trk[k], v & 127u);
        ui.force = 1;
    }
}

static void track_defaults(track_t *t)
{
    uint32_t i;
    for (i = 0; i < P_E0; i++)
        t->p[i] = TP[i].def;
    track_defaults_steps(t);
}

/* switch engine (its defaults + first preset) and say so */
static void select_engine(uint32_t e)
{
    set_engine(e);
    ui_say("ENGINE ", ENGINES[TSEL->eng_req]->name);
    ui.force = 1;
}

/* the presets of every engine (in ENGINE_ORDER), then the used user presets, as one list (the PRESETS knob and the
 * PRESETS page browse it) */
static uint32_t preset_all_pos(uint32_t *total)          /* list index of the selected track's preset */
{
    uint32_t n = 0, cur = 0, e, r;
    for (r = 0; r < NENG_SHOWN; r++) {                  /* (engines.c ENGINE_ORDER) */
        const engine_t *en = ENGINES[e = eng_vis(r)];
        if (e == TSEL->eng_req)
            cur = n + preset_rank(en, preset_orig(en, TSEL->preset % (en->npresets ? en->npresets : 1u)));
        n += preset_shown(e);
    }
    if (user_of(TSEL) < UP_SLOTS)
        cur = n + up_rank(user_of(TSEL));
    *total = n + up_count();
    return cur;
}

/* list index n (< total) -> engine, *k its preset; NENGINES = user preset, *k its slot */
static uint32_t preset_all_at(uint32_t n, uint32_t *k)
{
    uint32_t e = 0, i, r;
    for (r = 0; r < NENG_SHOWN && n >= preset_shown(eng_vis(r)); r++)
        n -= preset_shown(eng_vis(r));
    if (r == NENG_SHOWN) {
        *k = up_nth(n);
        return NENGINES;
    }
    e = eng_vis(r);
    for (i = 0; preset_orig(ENGINES[e], i) != i || n--; i++)    /* the n-th shown preset */
        ;
    *k = i;
    return e;
}

/* PRESETS > LIST (KNOB 4): ALL, FAV (favorites.filter, as up to 1.1.5), or one category (1.2, Discussion #90: BASS ..
 * OTHER, category.c): the list mode 0 ALL, 1 FAV, 1 + CAT_* a category. The category is kept with the settings in a
 * byte no engine uses (ui_lcat: 0 = none; firmware before reads ALL there, and favorites.filter stays 0 / 1 for it, the
 * editor and backups); the PRESETS knob and page browse that list */
#define ui_lcat (favorites.factory[15][11])
#define LIST_N (1u + CAT_N)                              /* ALL FAV BASS LEAD PAD PLUCK KEYS DRUM FX OTHER */
static uint32_t list_mode(void) { return favorites.filter ? 1u : ui_lcat && ui_lcat < CAT_N ? 1u + ui_lcat : 0u; }
static void list_set(uint32_t m)
{
    favorites.filter = m == 1u;
    ui_lcat = (uint8_t)(m >= 2u && m < LIST_N ? m - 1u : 0u);
}
static const char *list_name(uint32_t m) { return m == 0u ? "ALL" : m == 1u ? "FAV" : CAT_NAME[(m - 1u) % CAT_N]; }
/* the category of list entry e, k (preset_all_at: NENGINES = user preset k) */
static uint32_t sound_cat(uint32_t e, uint32_t k) { return e == NENGINES ? up_cat(k) : preset_cat(e, k); }
static int list_keep(uint32_t e, uint32_t k)
{
    uint32_t m = list_mode();
    return m == 1u ? favorite_has(e, k) : m ? sound_cat(e, k) == m - 1u : 1;
}
/* the category of the sound track t plays: its user preset's, else its factory preset's (the NAME screen's default) */
static uint32_t track_cat(const track_t *t)
{
    uint32_t u = user_of(t);
    return u < UP_SLOTS ? up_cat(u) : preset_cat(t->eng_req % NENGINES, t->preset);
}

static uint32_t preset_pos(uint32_t *total)
{
    uint32_t all, current = preset_all_pos(&all), n = 0, pos = 0xFFFFFFFFu;
    if (!list_mode()) { *total = all; return current; }
    for (uint32_t i = 0; i < all; i++) {
        uint32_t k, e = preset_all_at(i, &k);
        if (!list_keep(e, k)) continue;
        if (i == current) pos = n;
        n++;
    }
    *total = n;
    return pos == 0xFFFFFFFFu ? n : pos; /* current sound need not be in the list */
}
static uint32_t preset_at(uint32_t n, uint32_t *k)
{
    uint32_t all;
    if (!list_mode()) return preset_all_at(n, k);
    preset_all_pos(&all);
    for (uint32_t i = 0; i < all; i++) {
        uint32_t e = preset_all_at(i, k);
        if (list_keep(e, *k) && !n--) return e;
    }
    *k = UP_SLOTS;
    return NENGINES;
}
static int preset_favorite(void)
{
    uint32_t k = user_of(TSEL);
    return favorite_has(k < UP_SLOTS ? NENGINES : TSEL->eng_req,
                                        k < UP_SLOTS ? k : TSEL->preset);
}
/* #197: a settings change made by a knob (PRESETS > LIST, the FAV mark on PRESETS and in the EDIT layer) is written
 * later, as MENU's are when it closes: a write erases a flash sector with the interrupts off (~40 ms), which stops the
 * LED scan (every LED but one column's goes dark: a flash of the whole panel) and the audio. Written once the view it
 * was made in is left (another page, HOME, a layer opened or let go, the MENU) or after SET_LATER_MS without panel
 * input (ui.c scrn.idle; the autosave's wait), never on every detent; settings_poll still waits for the transport */
#define SET_LATER_MS 10000u
static struct { uint8_t on, page, home, layer; } set_later __attribute__((section(".pool")));   /* (.bss as before) */
static void settings_later(void)
{
    set_later.on = 1;
    set_later.page = ui.page;
    set_later.home = ui.home;
    set_later.layer = ui.layer;
}
static void settings_later_poll(void)                  /* ui_input, every pass */
{
    if (set_later.on && (set_later.page != ui.page || set_later.home != ui.home || set_later.layer != ui.layer ||
                         ui.menu || fm1_ms - scrn.idle >= SET_LATER_MS)) {
        set_later.on = 0;
        settings_save();
    }
}
static void preset_mark(int on)
{
    uint32_t k = user_of(TSEL);
    if (favorite_set(k < UP_SLOTS ? NENGINES : TSEL->eng_req, k < UP_SLOTS ? k : TSEL->preset, on)) {
        ui.force = 1;
        settings_later();
    }
}

/* the pattern the selected track's sound suggests: its index in the SAVE > PHRASES list, or -1. A factory
 * preset's PAT(n); a user preset that holds a pattern: that one ("U07") */
static int32_t preset_pat_hint(void)
{
    const engine_t *e = ENGINES[TSEL->eng_req % NENGINES];
    uint32_t u = user_of(TSEL);
    if (u < UP_SLOTS)
        return up_has_pat(u) ? (int32_t)(NPATTERNS + up_pat_rank(u)) : -1;
    if (!e->npresets)
        return -1;
    u = e->presets[TSEL->preset % e->npresets].pat;
    return u && u <= NPATTERNS ? (int32_t)u - 1 : -1;
}

static void preset_hinted(void)                     /* after a sound load: SAVE > PHRASES starts at the suggested pattern */
{
    int32_t h;
    if ((h = preset_pat_hint()) >= 0)
        ui.ppick = (uint8_t)h;
    ui.force = 1;
}

static void preset_go(uint32_t n)                    /* load list index n into the selected track (the sound only) */
{
    uint32_t k, e = preset_at(n, &k);
    if (e == NENGINES) {
        up_load(k);
    } else {
        if (e != TSEL->eng_req)
            select_engine(e);
        apply_preset(k);
    }
    preset_hinted();
}

/* the EDIT layer's KNOB 2 (ui_layer.c): the selected track's engine only: its factory presets, then the used user presets
 * made with it (slot order). List index of the current sound; *total the length */
static uint32_t eng_list_pos(uint32_t *total)
{
    const engine_t *en = ENGINES[TSEL->eng_req % NENGINES];
    uint32_t e = TSEL->eng_req % NENGINES, np = preset_shown(e), u = user_of(TSEL), k, n = 0;
    uint32_t cur = np ? preset_rank(en, preset_orig(en, TSEL->preset % en->npresets)) : 0u;   /* (#124: the aliases
                                        * skipped, as on PRESETS: SAMPLE 1 loads as 0, so counting it stuck KNOB 2) */
    for (k = 0; k < UP_SLOTS; k++)
        if (up_used(k) && up_engine(k) == e) {
            if (k == u)
                cur = np + n;
            n++;
        }
    *total = np + n;
    return cur;
}

static void eng_list_step(int32_t direction)         /* the next / previous sound of the engine (wraps) */
{
    uint32_t total, cur = eng_list_pos(&total), e = TSEL->eng_req % NENGINES, np = preset_shown(e), k, n;
    if (total < 2u)
        return;
    n = (cur + (direction > 0 ? 1u : total - 1u)) % total;
    if (n < np) {
        for (k = 0; preset_orig(ENGINES[e], k) != k || n--; k++)   /* the n-th shown preset */
            ;
        apply_preset(k);
    } else {
        n -= np;
        for (k = 0; k < UP_SLOTS; k++)
            if (up_used(k) && up_engine(k) == e && !n--) {
                up_load(k);
                break;
            }
    }
    preset_hinted();
}

static void preset_step(int32_t direction)
{
    uint32_t total, cur = preset_pos(&total);
    if (!total) { ui_message(list_mode() == 1u ? "NO FAVORITES" : "NO SOUNDS IN LIST"); return; }
    preset_go(cur >= total ? (direction > 0 ? 0 : total - 1) :
              (cur + (direction > 0 ? 1u : total - 1u)) % total);
}

/* Seven display rows. FAV and a category use a bounded window, not a repeating carousel.
 * Return total for an empty row; a current sound not in the list shows the start. */
static uint32_t preset_visible(uint32_t cur, uint32_t total, uint32_t row)
{
    uint32_t first, last;
    if (!total || row >= 7u) return total;
    if (!list_mode())
        return (cur + total * 4u + row - 3u) % total;
    first = cur < total && cur > 3u ? cur - 3u : 0u;
    last = total > 7u ? total - 7u : 0u;
    if (first > last) first = last;
    return first + row < total ? first + row : total;
}

/* HOME: the track KNOB k edits (MENU > HOME LEVELS, 1.5: track k; else the selected one) and what: the engine's four
 * main parameters, or (LEVELS) the track's LEVEL, as the GLO layer's knobs */
static track_t *home_trk(uint32_t k) { return home_levels() ? &trk[k & 3u] : TSEL; }
static const param_desc_t *home_param(uint32_t k, int16_t **vp)
{
    uint32_t id = home_levels() ? (uint32_t)P_LEVEL : ENGINES[TSEL->eng_req % NENGINES]->knob[k & 3u];
    *vp = &home_trk(k)->p[id];
    return track_desc(home_trk(k), id);
}

/* select track i (KNOB 1 on TRACKS, the editor): its sound, pages and pattern from now on */
static void track_select(uint32_t i)
{
    if (i >= NTRK || i == song.sel)
        return;
    song.sel = (uint8_t)i;
    ui.entry_open = 0;
    ui.hot_t = 0;
    ui.ev_keep = 0;                              /* (AUTOMATION: a row kept at its default was the other track's) */
    ui.cursor = 0;
    ui.bank = 0;
    sync_reload = 1;
    ui.force = 1;
}

#include "ui_slice.c"                             /* EDIT > SLICES: SLICE's slices by hand (an action page too) */
#include "ui_events.c"                            /* SEQ > AUTOMATION: locks, events, CHANCE, RATCH as a list (an action page) */
#include "midi_learn.c"                           /* MIDI LEARN (1.5): GLO + D4, a knob, a CC */

/* ---------------------------------------------------- action pages --- */
/* Pages whose purpose is an action (SAVE > USER, PHRASES, PROJECT, TOOLS, EDIT > SLICES, SEQ > AUTOMATION): the knobs pick,
 * OCT+ does it, OCT- cancels the picked action or goes HOME (ui_input.c). There OCT- / OCT+ do not
 * shift the octave */
static int go_id(uint32_t id) { return id == G_LOAD || id == G_SAVE || id == G_CLRSEQ || id == G_INITSND; }

static uint32_t act_cols(void)                   /* the columns that are actions, a bit each; 0 = not such a page */
{
    const page_t *pg = cur_page();
    uint32_t c, m = 0;
    if (ui.home)
        return 0;
    if (pg->graph == GR_EVENTS) return 16u;     /* AUTOMATION: no knob is an action; OCT+ by the row (ev_oct) */
    if (pg->graph == GR_TOOLS) return 15u;
    if (pg->graph == GR_SONG)
        return 1u;                               /* PLAY / STOP (also the PLAY button) */
    if (pg->graph == GR_PATS)
        return 2u;                               /* LOAD */
    if (pg->graph == GR_USER)
        return 14u;                              /* LOAD ERASE SAVE */
    if (pg->graph == GR_SLICES)
        return slice_page_ok() ? 12u : 0u;       /* SPLIT JOIN (a SLICE track only) */
    if (pg->scope == SC_GLOBAL)
        for (c = 0; c < 4u; c++)
            if (go_id(pg->id[c]))
                m |= 1u << c;
    return m;
}

/* the action OCT+ does: its column + 1, 0 = none picked yet (PHRASES has LOAD only) */
static uint32_t act_col(void)
{
    if (!ui.home && cur_page()->graph == GR_SONG) return 1u;
    if (!ui.home && cur_page()->graph == GR_EVENTS) return 5u;
    return !ui.home && cur_page()->graph == GR_PATS ? 2u : ui.act;
}

static const char *act_name(uint32_t c)          /* column c's action (the footer hint) */
{
    static const char *const UP_GO[3] = {"LOAD", "ERASE", "SAVE"};
    uint32_t id = cur_page()->id[c & 3u];
    if (cur_page()->graph == GR_EVENTS) return ev_act_name();
    if (cur_page()->graph == GR_TOOLS) {
        static const char *const actions[] = {"CLEAR", "INIT", "DELETE", "CLEAR"};
        return actions[c & 3u];
    }
    if (cur_page()->graph == GR_SONG)
        return song.playing || chain_busy() ? "STOP" : "PLAY";
    if (cur_page()->graph == GR_PATS)
        return "LOAD";
    if (cur_page()->graph == GR_USER)
        return UP_GO[(c + 2u) % 3u];
    if (cur_page()->graph == GR_SLICES)
        return c == 3u ? "JOIN" : "SPLIT";
    return id == G_CLRSEQ ? "CLEAR" : id == G_INITSND ? "INIT" : id == G_LOAD ? "LOAD" : "SAVE";
}

/* the picked action would do something now (OCT+ breathes): another pattern, a used slot, stopped for
 * a flash write, steps to clear */
static int act_ready(void)
{
    uint32_t c = act_col(), s = song.sel, id;
    if (!c--)
        return 0;
    if (cur_page()->graph == GR_EVENTS) return ev_act_ready();
    if (cur_page()->graph == GR_TOOLS)                  /* one case per column: CLEAR PAT, INIT, DELETE ROW, CLEAR SONG */
        return !chain_busy() && (c == 0u ? !seq_is_empty(TSEL) || motion_count(TSEL) : c == 1u ? 1 :
                                 c == 2u ? ui.song_row < chain_config.count : chain_config.count != 0u);
    if (cur_page()->graph == GR_SONG)
        return song.playing || chain_busy() || chain_config.count;
    if (cur_page()->graph == GR_PATS)
        return pat_last[s] != pat_pick() + 1u || steps_sig(TSEL) != pat_sig[s];
    if (cur_page()->graph == GR_USER)
        return c == 3u ? !song.playing : up_used(ui.uslot) && (c == 1u || !song.playing);
#if FELUCCA_SLICE
    if (cur_page()->graph == GR_SLICES)
        return slice_act_ready(c);
#endif
    id = cur_page()->id[c & 3u];
    if (id == G_LOAD)
        return project_used((uint32_t)song.g[G_SLOT] - 1u);
    if (id == G_SAVE)
        return !song.playing;
    if (id == G_CLRSEQ)
        return !seq_is_empty(TSEL);
    return 1;
}
