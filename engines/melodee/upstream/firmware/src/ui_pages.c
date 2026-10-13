/* SPDX-License-Identifier: GPL-3.0-only */
/* The pages the proposal did not draw, in the redesign's language (docs/design: mock/r5_pages, ref/env, lfo, ..):
 * no footer; the header names the page and shows where it is in its family (dots); the sound being edited at the
 * foot; the four knobs as the page needs them, the knob just turned lifted in the track's colour; each page's own
 * picture.
 * CURVE (ENV, LFO, LFO 2): the curve is the page (a panel of 160 rows, drawn in two slices: a canvas holds 124 x 240),
 * a value on each of its segments; the knobs as a slim strip under it.
 * RINGS (the pages of plain values: EDIT, the engines' own ..): a ring a knob (its value under it,
 * a dot at the arc's end) over what the page shapes: a filter's
 * response (a page with a cutoff), FM6's algorithm (its chart), else the sound itself (the scope, smooth, live).
 * NOTES (SEQ > STEP; a melodic track): the roll the whole height (PR_TOP, PR_H: ui_graph.c, in two slices),
 * the knobs as chips under it (the one turning filled).
 * MIXER: Stage's columns as channel strips: the sound, its level (a ring, dB in it) and meter, PAN, MUTE,
 * armed;
 * the selected track's lifted, the knob just turned (its control) in text.
 * Each part remembers what it drew. draw_column draws the knobs in the style col_style asks (pv_column). Included by
 * ui_draw.c */
enum { PV_NONE, PV_CURVE, PV_RINGS, PV_FADERS, PV_MIXER, PV_NOTES, PV_MOD, PV_GRID };
#define PV_GRID_Y 22                                    /* the drum grid: its panel, rows 22 .. 195 */
#define PV_GRID_H 174
static void list_words(char *d, const char *label, uint32_t n);   /* ui_list.c */
static void menu_words(char *d, const char *s, uint32_t n);         /* ui_menu.c */
#define PV_PANEL_X 4
#define PV_PANEL_W 232
#define PV_SLICE 128                                    /* rows of a panel slice (232 x 128 fits CV_MAX) */
#define PV_CURVE_Y 22                                   /* CURVE: the panel, rows 22 .. 181 */
#define PV_CURVE_H 160
#define PV_STRIP_Y 188                                  /* .. the knobs, 54 x 30, rows 188 .. 217 */
#define PV_STRIP_H 30
#define PV_SOUND_Y 220                                  /* the sound: rows 220 .. 239 */
#define PV_RING_Y 24                                    /* RINGS: a knob 58 x 80 at x 1 + 60 c, rows 24 .. 103 */
#define PV_RING_H 80
#define PV_PIC_Y 108                                    /* .. the picture: 240 x 112, rows 108 .. 219 */
#define PV_PIC_H 112
#define PV_FADER_Y 24                                   /* FADERS: a send 60 x 190 at x 60 c, rows 24 .. 213 */
#define PV_FADER_H 190
#define PV_CHIP_Y 200                                   /* NOTES: a knob a chip, 54 x 18 at CARD_X(c) */
#define PV_CHIP_H 18
static int32_t pv_chip_y = PV_CHIP_Y;                   /* (MOD: its chips at the bottom, no sound line) */
static int32_t pv_strip_y = PV_STRIP_Y;                 /* (the GLO layer: its levels at the top) */
static uint16_t pv_strip_tc[4];                         /* a strip's label colour (0: the track's) */

static struct { uint32_t panel, sound; int32_t slice0, gplay, pplay; } pv;   /* gplay, pplay: the step playing drawn
                                                                               * (drum grid, PATTERN; -1 none) */
static int32_t pv_tab_h;                                /* an engine in sections: its tabs under the header (rows 18 ..
                                                         * 33): the rings 10 rows lower and as much shorter */
#define PV_TAB_RING 10
static struct { int16_t x0, y0, x1, y1; } pv_box[6];   /* the labels on a panel (pv_text) */
static uint32_t pv_nbox;

static uint32_t pv_kind(void)
{
    const page_t *pg = cur_page();
    if (!ui.home && grid_on() && pg->graph == GR_ROLL && !ui.layer)
        return PV_GRID;
    if (ui.home || act_cols() || grid_on())
        return PV_NONE;
    if ((pg->graph == GR_ADSR && pg->scope == SC_TRACK) || pg->graph == GR_LFO || pg->graph == GR_FMEG ||
        pg->graph == GR_FMPEG)
        return PV_CURVE;
    if (pg->graph == GR_NONE || pg->graph == GR_ARP || pg->graph == GR_STEPS ||
        pg->graph == GR_CHORD)
        return PV_RINGS;
    if (pg->graph == GR_TRK)
        return PV_MIXER;
    if (pg->graph == GR_ROLL && !drum_track(TSEL))
        return PV_NOTES;
    if (pg->graph == GR_MOD)
        return PV_MOD;
    return PV_NONE;
}

/* MOD (docs/design: mock/r6_pages MOD): the matrix's four routes as rows, "1  LFO -> Cutoff  +62 %" over the amount's
 * bar (from the middle); the route KNOB 2 is on: a card in the track's colour; unused routes dim. The knobs' chips
 * under them (KNOB 1 its source, 2 the route, 3 its destination, 4 its amount, ui_draw.c) */
#define PV_MOD_Y 22                                     /* the rows: 48 px each from y 22, the chips at 220 */
#define PV_MOD_H 48
static void pv_mod_pill(int32_t x, int32_t y, int32_t w, const char *s, int on, uint16_t bg)
{
    char b[16];
    list_words(b, s, sizeof b);
    cv_rrect(x, y, w, 18, 5, on ? ux_mix(bg, T_THEME, 30) : ux_mix(bg, T_LINE, 50), bg);
    cv_free_text(x + 4 + (w - 8 - (text_w(&AF_X, b) < w - 8 ? text_w(&AF_X, b) : w - 8)) / 2, y + 4, &AF_X, b,
                 on ? T_TEXT : T_DIM, on ? ux_mix(bg, T_THEME, 30) : ux_mix(bg, T_LINE, 50), w - 8);
}
static void pv_mod_rows(void)
{
    static uint32_t sig;
    uint32_t k, sg = mod_ui_slot * 7u + ux.gen * 977u + ux.pal * 31u + song.sel * 13u + TSEL->eng_req * 613u;
    for (k = 0; k < NMSLOT * 3u; k++)
        sg = (sg ^ (uint16_t)TSEL->p[P_M1SRC + k]) * 16777619u;
    if (!ui.force && sg == sig)
        return;
    sig = sg;
    for (k = 0; k < NMSLOT; k++) {
        const int16_t *p = &TSEL->p[P_M1SRC + 3u * k];
        int sel = k == mod_ui_slot, on = p[0] && p[1] && p[2];
        uint16_t bg = sel ? T_LIFT : T_BG;
        int32_t a = clamp(p[2], TP[P_M1AMT].min, TP[P_M1AMT].max), m = 128, x;
        char b[16];
        const char *unit;
        cv_begin(240, PV_MOD_H, T_BG);
        if (sel) {                                      /* (the route picked: a card) */
            cv_rrect(4, 1, 232, PV_MOD_H - 4, 7, T_THEME, T_BG);
            cv_rrect(5, 2, 230, PV_MOD_H - 6, 6, bg, T_THEME);
        }
        b[0] = (char)('1' + k);
        b[1] = 0;
        cv_text_on(13, 7, &AF_S, b, sel ? T_THEME : T_MID, bg);
        pv_mod_pill(28, 6, 70, p[0] ? N_MSRC[clamp(p[0], 0, MS_N - 1)] : "--", p[0] != 0, bg);
        cv_rect(104, 14, 10, 2, on ? T_MID : T_DIM);    /* an arrow */
        cv_line(110, 11, 113, 14, on ? T_MID : T_DIM);
        cv_line(110, 17, 113, 14, on ? T_MID : T_DIM);
        pv_mod_pill(120, 6, 70, p[1] ? mod_dst_name(TSEL, p[1]) : "--", p[1] != 0, bg);
        param_format(&TP[P_M1AMT], a, b, &unit);        /* "+62 %" */
        if (unit[0] && str_len(b) + str_len(unit) + 2u < sizeof b) {
            str_cpy(b + str_len(b), " ", 2);
            str_cpy(b + str_len(b), unit, 8);
        }
        cv_text_r(228, 9, &AF_X, b, on ? (sel ? T_THEME : T_SEC) : T_DIM, bg);
        cv_rect(28, 30, 200, 3, T_LINE);                /* the amount, from the middle */
        x = 28 + (a - TP[P_M1AMT].min) * 200 / (TP[P_M1AMT].max - TP[P_M1AMT].min);
        if (a)
            cv_rect(x < m ? x : m, 30, x < m ? m - x : x - m, 3, on ? (sel ? T_THEME : ux_mix(bg, T_THEME, 60)) : T_DIM);
        cv_rect(m, 28, 1, 7, T_MID);
        cv_blit(0, (uint32_t)(PV_MOD_Y + (int32_t)k * PV_MOD_H));
    }
}

/* ---------------------------------------------------------- knobs --- */
/* the value and its unit centred on xc (M, rolling as column c's when it changes; S, X when it is wide; the unit 9 px,
 * mid) */
static void pv_value_c(uint32_t c, int32_t xc, int32_t y, const char *val, const char *unit, uint16_t vc, uint16_t bg,
                       int32_t room)
{
    const aafont_t *f = &AF_M;
    int32_t uw = unit[0] ? text_w(&AF_X, unit) + 2 : 0, w, x;
    if (text_w(f, val) + uw > room) f = &AF_S;
    if (text_w(f, val) + uw > room) f = &AF_X;
    if (text_w(f, val) + uw > room) uw = 0;
    w = text_w(f, val) + uw;
    x = xc - (w < room ? w : room) / 2;
    if (f == &AF_M) {
        roll_bg = bg;
        x = roll_text(c, x, y, val, vc);
    } else {
        x = cv_free_text(x, y + (f == &AF_S ? 4 : 6), f, val, vc, bg, room - uw);
    }
    if (uw)
        cv_text_on(x + 2, y + 6, &AF_X, unit, T_MID, bg);
}
/* a ring's pointer: the arc's end, 33 places from 7:30 to 4:30 (12.5 px from the centre) */
static const int8_t PV_DOT[33][2] = {
    {-9, 9}, {-10, 7}, {-11, 6}, {-12, 4}, {-12, 2}, {-12, 1}, {-12, -1}, {-12, -3}, {-12, -5}, {-11, -6}, {-10, -8},
    {-8, -9}, {-7, -10}, {-5, -11}, {-4, -12}, {-2, -12}, {0, -12}, {2, -12}, {4, -12}, {5, -11}, {7, -10}, {8, -9},
    {10, -8}, {11, -6}, {12, -5}, {12, -3}, {12, -1}, {12, 1}, {12, 2}, {12, 4}, {11, 6}, {10, 7}, {9, 9}};
/* CS_RING: a 58 x 80 cell: the label (9 px, the track's colour), the ring (3 px, KNOB_RING) with a dot at its value,
 * the value under it; the knob just turned: lifted, outlined */
/* a ring's words as the mockups write them: its label in full ("OCT" -> "OCTAVES", when it fits), a name value in
 * sentence case ("SAW" -> "Saw"; numbers and notes as they are), a unit in lower case ("STEP" -> "steps") */
static const struct { const char *k, *w; } PV_LABEL[] = {
    {"OCT", "OCTAVES"}, {"LEN", "LENGTH"}, {"DIV", "DIVISION"}, {"SWG", "SWING"}, {"PAT", "PATTERN"},
    {"DTN", "DETUNE"}, {"NOIS", "NOISE"}, {"CUT", "CUTOFF"}, {"DRV", "DRIVE"}, {"ATK", "ATTACK"}, {"DEC", "DECAY"},
    {"SUS", "SUSTAIN"}, {"REL", "RELEASE"}, {"FDBK", "FEEDBACK"}, {"DPTH", "DEPTH"}, {"VOIC", "VOICING"}, {"QNT", "QUANTIZE"}};
static void pv_words(const char *label, const char *val, const char *unit, char *lb, char *v, char *u)
{
    uint32_t i, word = val[0] >= 'A' && val[0] <= 'Z' && val[1];
    str_cpy(lb, label, 12);
    for (i = 0; i < NELEM(PV_LABEL); i++)
        if (str_eq(label, PV_LABEL[i].k) && text_w(&AF_X, PV_LABEL[i].w) <= 54) {
            str_cpy(lb, PV_LABEL[i].w, 12);
            break;
        }
    for (i = 0; val[i] && word; i++)                    /* (a name: letters, spaces and signs only) */
        word = !(val[i] >= '0' && val[i] <= '9');
    if (word) menu_words(v, val, 16);
    else str_cpy(v, val, 16);
    str_cpy(u, unit, 8);
    if (str_eq(unit, "STEP")) {
        str_cpy(u, str_eq(val, "1") ? "step" : "steps", 8);
    } else if (unit[0] && unit[1] && !str_eq(unit, "dB")) {
        for (i = 0; u[i]; i++)
            if (u[i] >= 'A' && u[i] <= 'Z') u[i] = (char)(u[i] + 32);
    }
}
static void pv_ring(uint32_t c, const char *label0, const char *val0, const char *unit0, uint16_t vc, int32_t ratio,
                    int hot)
{
    char label[12], val[16], unit[8];
    uint16_t bg = hot ? T_LIFT : T_BG, lc = vc == T_DIM ? T_DIM : T_THEME;
    int32_t h = pv_tab_h ? PV_RING_H - PV_TAB_RING : PV_RING_H, cy = pv_tab_h ? 32 : 36, vy = pv_tab_h ? 50 : 55;
    pv_words(label0, val0, unit0, label, val, unit);
    cv_begin(58, (uint32_t)h, T_BG);                    /* (under tabs: the cell 10 rows shorter, the ring closer) */
    if (hot) {
        cv_rrect(0, 0, 58, h - 2, 8, T_THEME, T_BG);
        cv_rrect(1, 1, 56, h - 4, 7, bg, T_THEME);
    }
    if (label[0] || val[0]) {
        cv_text_c(29, 2, &AF_X, label, lc, bg);
        if (ratio >= 0) {
            int32_t r = ratio > 1000 ? 1000 : ratio, a1 = -KA_END + r * 2 * KA_END / 1000, k = (r * 32 + 500) / 1000;
            knob_arc(29 - KNOB_RING_R, cy - KNOB_RING_R, KNOB_RING_R, KNOB_RING_COV, KNOB_RING_ANG, -KA_END, a1, T_LINE,
                     lc, bg);
            cv_disc(29 + PV_DOT[k][0], cy + PV_DOT[k][1], 8, bg, 0);
            cv_disc(29 + PV_DOT[k][0], cy + PV_DOT[k][1], 6, T_TEXT, 0);
        } else {                                        /* (a list of names: the ring alone) */
            knob_arc(29 - KNOB_RING_R, cy - KNOB_RING_R, KNOB_RING_R, KNOB_RING_COV, KNOB_RING_ANG, -KA_END,
                     -KA_END - 1, T_LINE, lc, bg);
        }
        pv_value_c(c, 29, vy, val, unit, vc, bg, 54);
    }
    cv_blit((uint32_t)(1 + 60 * (int32_t)c), (uint32_t)(PV_RING_Y + (pv_tab_h ? PV_TAB_RING : 0)));
}
/* CS_FADER: a send: its label, the fader (6 px, filled to its value in the track's colour, a cap), its value */
static void pv_fader(uint32_t c, const char *label, const char *val, const char *unit, uint16_t vc, int32_t ratio,
                     int hot)
{
    uint16_t lc = vc == T_DIM ? T_DIM : T_THEME, bg = hot ? T_LIFT : T_BG;
    int32_t top = 22, bot = 162;
    cv_begin(60, PV_FADER_H, T_BG);
    if (hot) {
        cv_rrect(1, 0, 58, PV_FADER_H, 8, T_THEME, T_BG);
        cv_rrect(2, 1, 56, PV_FADER_H - 2, 7, bg, T_THEME);
    }
    if (label[0] || val[0]) {
        cv_text_c(30, 3, &AF_X, label, lc, bg);
        cv_rrect(27, top, 6, bot - top, 3, T_LINE, bg);
        if (ratio >= 0) {
            int32_t y = bot - (bot - top) * (ratio > 1000 ? 1000 : ratio) / 1000;
            cv_rrect(27, y, 6, bot - y, 3, lc, T_LINE);
            cv_rrect(18, y - 5, 24, 10, 4, T_TEXT, bg);
        }
        pv_value_c(c, 30, 169, val, unit, ratio > 0 ? vc : T_DIM, bg, 56);
    }
    cv_blit((uint32_t)(60 * (int32_t)c), PV_FADER_Y);
}
/* CS_CHIP: "Step 7/16" in a 54 x 18 chip (its label in words, the value and unit after it; too long: the value); the
 * knob just turned: filled in the track's colour */
static void pv_chip(uint32_t c, const char *label, const char *val, const char *unit, uint16_t vc, int hot)
{
    char b[32];
    uint32_t i, n;
    uint16_t bg = hot ? T_THEME : T_SURF, fg = hot ? T_INK : vc == T_DIM ? T_DIM : T_TEXT;
    str_cpy(b, label, 12);
    for (i = 1; b[i]; i++)                              /* STEP -> Step */
        if (b[i] >= 'A' && b[i] <= 'Z' && b[i - 1] != ' ') b[i] = (char)(b[i] + 32);
    n = str_len(b);
    if (n) b[n++] = ' ';
    str_cpy(b + n, val, sizeof b - n);
    if (val[0] >= 'A' && val[0] <= 'Z' && val[1] >= 'A' && val[1] <= 'Z')   /* a word in capitals: "SNARE" -> "Snare", */
        for (i = n ? n : 1u; b[i]; i++)                                      /* after a label all small: "Hit on" */
            if (b[i] >= 'A' && b[i] <= 'Z') b[i] = (char)(b[i] + 32);
    str_cpy(b + str_len(b), unit, sizeof b - str_len(b));
    if (text_w(&AF_X, b) > CARD_W - 6) {
        str_cpy(b, val, sizeof b);
        str_cpy(b + str_len(b), unit, sizeof b - str_len(b));
    }
    cv_begin(CARD_W, PV_CHIP_H, T_BG);
    if (label[0] || val[0]) {
        cv_rrect(0, 0, CARD_W, PV_CHIP_H, 5, bg, T_BG);
        int32_t w = text_w(&AF_X, b) < CARD_W - 6 ? text_w(&AF_X, b) : CARD_W - 6;
        cv_free_text(3 + (CARD_W - 6 - w) / 2, 3, &AF_X, b, fg,
                     bg, CARD_W - 6);
    }
    cv_blit((uint32_t)CARD_X(c), (uint32_t)pv_chip_y);
}
/* CS_STRIP: a 54 x 30 cell, its label (8 px, the track's colour) over its value (11 px) and unit; the knob just turned:
 * tinted, outlined */
static void pv_column(uint32_t c, const char *label0, const char *val0, const char *unit0, uint16_t vc, int32_t ratio,
                      int hot)
{
    uint16_t bg = hot ? ux_mix(T_SURF, T_THEME, 22) : T_SURF, lc = vc == T_DIM ? T_DIM : pv_strip_tc[c & 3u] ? pv_strip_tc[c & 3u] : T_THEME;
    int32_t x;
    char label[12], val[16], unit[8];
    const char *l0 = label0, *v0 = val0, *u0 = unit0;
    if (col_style == CS_RING) {
        pv_ring(c, l0, v0, u0, vc, ratio, hot);
        return;
    }
    if (col_style == CS_FADER) {
        pv_fader(c, l0, v0, u0, vc, ratio, hot);
        return;
    }
    if (col_style == CS_CHIP) {
        pv_chip(c, l0, v0, u0, vc, hot);
        return;
    }
    pv_words(l0, v0, u0, label, val, unit);            /* (CS_STRIP: the words as the rings') */
    cv_begin(CARD_W, PV_STRIP_H, T_BG);
    if (hot) {
        cv_rrect(0, 0, CARD_W, PV_STRIP_H, 6, T_THEME, T_BG);
        cv_rrect(1, 1, CARD_W - 2, PV_STRIP_H - 2, 5, bg, T_THEME);
    } else {
        cv_rrect(0, 0, CARD_W, PV_STRIP_H, 6, bg, T_BG);
    }
    if (label[0])
        cv_text_fit(6, 2, &AF_X, label, lc, bg, CARD_W - 8);
    if (val[0]) {
        int32_t uw = unit[0] ? text_w(&AF_X, unit) + 2 : 0;
        x = cv_free_text(6, 14, &AF_S, val, vc, bg, CARD_W - 8 - uw);
        if (unit[0] && x + uw <= CARD_W - 2)
            cv_text_on(x + 2, 15, &AF_X, unit, T_MID, bg);
    }
    cv_blit((uint32_t)CARD_X(c), (uint32_t)pv_strip_y);
}

/* ---------------------------------------------------------- parts --- */
/* the sound being edited: its track (the numeral, filled in the track's colour), its engine (the track's colour), its
 * name */
static void pv_sound(void)
{
    char nm[20];
    const char *en = ENGINES[eng_idx(TSEL->eng_req)]->name;
    uint32_t sig;
    int32_t x;
    if (designer_on() && designer_foot())                /* the sound designer: its keys (ui_designer.c) */
        return;
    if(cur_page()->fam==FAM_FX && cur_page()->scope==SC_GLOBAL && cur_page()->id[0]!=G_TRK_SEND){en="SEND / RETURN";memcpy(nm,"ALL TRACKS",11);}
    else sound_name(TSEL, nm);
    sig = str_hash(str_hash(song.sel * 7u + ux.gen * 977u + ux.pal * 31u, en), nm);
    if (!ui.force && sig == pv.sound)
        return;
    pv.sound = sig;
    cv_begin(240, 240 - PV_SOUND_Y, T_BG);
    cv_icon_mid(6, 8, 16, trk_icon(song.sel, 1), T_THEME, T_BG);   /* (its ink at x 8, as the text was) */
    x = cv_text_on(26, 3, &AF_X, en, T_THEME, T_BG);
    cv_free_text(x + 6, 1, &AF_S, nm, T_SEC, T_BG, 232 - x - 6);
    cv_blit(0, PV_SOUND_Y);
}
/* a panel h rows tall at y, drawn by fn in slices (fn draws in panel coordinates; pv.slice0: the slice's first row) */
static void pv_panel(int32_t y, int32_t h, void (*fn)(int32_t h))
{
    int32_t top;
    for (top = 0; top < h; top += PV_SLICE) {
        int32_t sh = h - top < PV_SLICE ? h - top : PV_SLICE;
        cv_begin(PV_PANEL_W, (uint32_t)sh, T_BG);
        cv_oy = -top;
        pv.slice0 = top;
        cv_rrect(0, 0, PV_PANEL_W, h, 6, T_PANEL, T_BG);
        cv_bg = T_PANEL;
        pv_nbox = 0;
        fn(h);
        cv_oy = 0;
        cv_blit(PV_PANEL_X, (uint32_t)(y + top));
    }
}
/* a canvas drawn (at x 0, row y) for its playhead alone: sent only where it was and is, columns from .. from+w-1 and
 * to .. to+w-1 (-1: none; close together: one span) */
static void pv_cols(uint32_t y, int32_t from, int32_t to, int32_t w)
{
    if (from >= 0 && to >= 0 && from < to + w + 8 && to < from + w + 8) {
        int32_t a = from < to ? from : to, b = from < to ? to : from;
        cv_blit_spans(0, y, (uint32_t)a, (uint32_t)(b - a + w), 0, 0);
    } else {
        cv_blit_spans(0, y, from >= 0 ? (uint32_t)from : 0u, from >= 0 ? (uint32_t)w : 0u, to >= 0 ? (uint32_t)to : 0u,
                      to >= 0 ? (uint32_t)w : 0u);
    }
}
/* a label on a panel: below the labels already on it that it would cover (pv.nbox: a panel's), moved up off a slice's
 * seam, drawn in the slice that holds it */
static void pv_text(int32_t x, int32_t y, const aafont_t *f, const char *s, uint16_t c, int align)
{
    int32_t w = text_w(f, s), seam;
    uint32_t i;
    if (align == 1) x -= w / 2;
    else if (align == 2) x -= w;
    for (i = 0; i < pv_nbox; i++)
        if (x < pv_box[i].x1 && x + w > pv_box[i].x0 && y < pv_box[i].y1 && y + f->h > pv_box[i].y0)
            y = pv_box[i].y1 + 1;
    seam = (y + f->h) / PV_SLICE * PV_SLICE;
    if (seam > y && seam < y + f->h)
        y = seam - f->h;
    if (pv_nbox < NELEM(pv_box))
        pv_box[pv_nbox++] = (typeof(pv_box[0])){(int16_t)x, (int16_t)y, (int16_t)(x + w), (int16_t)(y + f->h)};
    if (y >= pv.slice0 && y + f->h <= pv.slice0 + PV_SLICE)
        cv_text_on(x, y, f, s, c, T_PANEL);
}
/* knob c's value as the page formats it, and its unit */
static void pv_value(uint32_t c, char *val, const char **unit)
{
    int16_t *vp;
    const param_desc_t *d = page_desc(cur_page(), c, &vp);
    val[0] = 0;
    *unit = "";
    if (d && vp)
        param_format(d, *vp, val, unit);
}
static int32_t pv_hot(void) { return ui.hot_t ? (int32_t)(ui.hot_col & 3u) : -1; }

/* ---------------------------------------------------------- CURVE --- */
/* ENV: attack (linear), decay (exponential to the sustain), the sustain held, the release (exponential); the area
 * under it tinted; a handle at each segment's end; each segment's value on it; the segment turning in text */
static void pv_env(int32_t h)
{
    const page_t *pg = cur_page();
    int32_t va = TSEL->p[pg->id[0]], vd = TSEL->p[pg->id[1]], vs = TSEL->p[pg->id[2]], vr = TSEL->p[pg->id[3]];
    int32_t top = 18, bot = h - 22, x0 = 12, x1 = x0 + 10 + va * 40 / 127, x2 = x1 + 16 + vd * 52 / 127;
    int32_t x4 = PV_PANEL_W - 14, x3 = x4 - 24 - vr * 48 / 127, sus = vs * 1000 / 127, hot = pv_hot(), x, py = 0;
    int32_t e = 32768, kd = 150733 / (x2 - x1), kr = 150733 / (x4 - x3);
    uint16_t c = T_THEME, fill = ux_mix(T_PANEL, T_THEME, 14);
    char v[12], b[20];
    const char *unit;
#define PVY(l) (bot * 16 - (l) * (bot - top) * 16 / 1000)
    for (x = x0; x <= x4; x++) {                        /* the curve in 1/16 px */
        int32_t l, y, seg = x < x1 ? 0 : x < x2 ? 1 : x < x3 ? 2 : 3;
        if (seg == 0) l = (x - x0) * 1000 / (x1 - x0 ? x1 - x0 : 1);
        else if (seg == 1) { e = x == x1 ? 32768 : (e * (32768 - kd)) >> 15; l = sus + ((1000 - sus) * e >> 15); }
        else if (seg == 2) l = sus;
        else { e = x == x3 ? 32768 : (e * (32768 - kr)) >> 15; l = sus * e >> 15; }
        y = PVY(l);
        cv_rect(x, y / 16 + 1, 1, bot - y / 16, fill);
        cv_vspan_aa(x, x > x0 ? py : y, y, seg == hot ? T_TEXT : c);
        py = y;
    }
    {   /* the handles: the attack's peak, the decay's end, the sustain's end, the release's end */
        const int32_t hx[4] = {x1, x2, x3, x4}, hy[4] = {top, PVY(sus) / 16, PVY(sus) / 16, bot};
        uint32_t i;
        for (i = 0; i < 4u; i++) {
            int32_t d = (int32_t)i == hot ? 11 : 9;
            cv_disc(hx[i], hy[i], d + 4, T_PANEL, 0);
            cv_disc(hx[i], hy[i], d, (int32_t)i == hot ? T_TEXT : c, 0);
        }
        /* each value on its segment: the attack's by the peak, the decay's on its slope, the sustain's over it, the
         * release's under its end; the one turning larger, in text */
        for (i = 0; i < 4u; i++) {
            const aafont_t *f = (int32_t)i == hot ? &AF_M : &AF_X;
            uint16_t tc = (int32_t)i == hot ? T_TEXT : T_MID;
            int32_t ys = hy[2];
            pv_value(i, v, &unit);
            str_cpy(b, v, sizeof b);
            if (unit[0]) { str_cpy(b + str_len(b), " ", 2); str_cpy(b + str_len(b), unit, 6); }
            if (i == 0u) pv_text(x1 + 8, top - 13 < 2 ? 2 : top - 13, f, b, tc, 0);
            else if (i == 1u) pv_text(x1 + (x2 - x1) / 2 + 8, top + (ys - top) / 2 - 6, f, b, tc, 0);
            else if (i == 2u) pv_text((x2 + x3) / 2, ys - 6 - f->h, f, b, tc, 1);
            else pv_text(x4 - 8, bot + 4 > h - f->h - 2 ? h - f->h - 2 : bot + 4, f, b, tc, 2);
        }
    }
#undef PVY
}
/* LFO: two cycles from its phase, its reach dashed (the fade in, then the depth), the zero line; the rate at the top,
 * where it goes (the strongest of LFO DEST's) beside it */
static void pv_lfo(int32_t h)
{
    static const uint8_t DST[4] = {P_LD_PIT, P_LD_FLT, P_LD_SHP, P_LD_AMP};
    static const char *const DST_NAME[4] = {"Pitch", "Filter", "Shape", "Level"};
    const track_t *t = TSEL;
    int32_t uni = t->p[P_LPOL] != 0, cy = uni ? h - 26 : h / 2, amp = uni ? h - 66 : 48, x, py = 0;
    int32_t fw = t->p[P_LFADE] * 120 / 127, x0 = 10, w = PV_PANEL_W - 20, best = 0, bi = -1, hot = pv_hot();
    uint32_t ph = (uint32_t)t->p[P_LPHASE] << 25, i;
    char v[12], b[20];
    const char *unit;
    cv_rect(x0, cy, w, 1, T_LINE);
    {                                                   /* its reach, dashed: the fade in, then the depth */
        uint16_t gc = hot == 3 ? T_TEXT : ux_mix(T_PANEL, T_THEME, 35);
        for (x = 0; x < w; x += 5) {
            int32_t lv = x < fw ? x * amp / fw : amp;
            cv_rect(x0 + x, cy - lv, 2, 1, gc);
        }
    }
    for (x = 0; x < w; x++) {                           /* two cycles (lfo_wave only reads the track) */
        int32_t wv = lfo_wave((track_t *)t, ph + (uint32_t)x * (0xFFFFFFFFu / ((uint32_t)w / 2u))), y;
        int32_t f = fw && x < fw ? x * 1024 / fw : 1024;
        if (t->p[P_LWAVE] == 4)
            wv = (int32_t)((x / 20 * 2654435761u) >> 16) - 32768;
        y = uni ? cy * 16 - (wv + 32768) * amp / 4096 * f / 1024 : cy * 16 - wv * amp / 2048 * f / 1024;
        cv_vspan_aa(x0 + x, x ? py : y, y, T_THEME);
        py = y;
    }
    pv_value(0, v, &unit);                              /* the rate */
    str_cpy(b, v, sizeof b);
    if (unit[0]) { str_cpy(b + str_len(b), " ", 2); str_cpy(b + str_len(b), unit, 6); }
    pv_text(10, 8, &AF_S, b, T_TEXT, 0);
    for (i = 0; i < 4u; i++) {                          /* where it goes: the strongest destination */
        int32_t a = t->p[DST[i]] < 0 ? -t->p[DST[i]] : t->p[DST[i]];
        if (a > best) best = a, bi = (int32_t)i;
    }
    if (bi >= 0 && pv.slice0 == 0) {
        int32_t cw = text_w(&AF_X, DST_NAME[bi]) + 26;
        uint16_t cb = ux_mix(T_PANEL, T_THEME, 20);
        cv_rrect(PV_PANEL_W - 8 - cw, 8, cw, 16, 4, cb, T_PANEL);
        cv_icon_mid(PV_PANEL_W - 8 - cw + 5, 16, 12, ICON_X_RIGHT, T_THEME, cb);
        cv_text_on(PV_PANEL_W - 8 - cw + 19, 10, &AF_X, DST_NAME[bi], T_THEME, cb);
    }
}
/* FM6's EG RATE / EG LVL (operator fm6_opsel), PITCH EG / LV (mock/r6_pages OP3 EG): the DX7 envelope from L4 by R1..R3
 * to L1..L3, held there, then R4 back to L4 (a slower rate a wider segment); the area under it tinted, a dot at each
 * point; the knob turning: its segment (a rate) or its point (a level) in text. PITCH: the note's own pitch dashed */
static void pv_dxeg(int32_t h)
{
    const uint8_t *v = fm6_patch[song.sel % NTRK];
    int pitch = cur_page()->graph == GR_FMPEG, lv = cur_page()->id[0] == FP_L1 || cur_page()->id[0] == FP_PL1;
    const uint8_t *r = pitch ? v + FP_PR1 : v + (5u - fm6_opsel % 6u) * FP_OP + FP_R1;
    const uint8_t *l = pitch ? v + FP_PL1 : v + (5u - fm6_opsel % 6u) * FP_OP + FP_L1;
    int32_t x0 = 10, x1 = 222, top = 14, bot = h - 12, px[6], py[6], i, x, hot = pv_hot();
    uint16_t fill = ux_mix(T_PANEL, T_THEME, 22);
#define DXY(a) (bot - (int32_t)(a) * (bot - top) / 99)
    int32_t w[5], sum = 0;
    for (i = 0; i < 4; i++)                             /* (a segment 8..46 wide by its rate, the hold 30: the whole */
        sum += w[i < 3 ? i : 4] = 8 + (99 - r[i]) * 38 / 99;   /* stretched over the panel) */
    w[3] = 30;
    sum += 30;
    px[0] = x0;
    py[0] = DXY(l[3]);
    for (i = 0; i < 5; i++) {                           /* P1..P3 (after R1..R3), P4 the hold's end, P5 after R4 */
        px[i + 1] = px[i] + w[i] * (x1 - x0) / sum;
        py[i + 1] = i < 3 ? DXY(l[i]) : i == 3 ? py[3] : DXY(l[3]);
    }
    px[5] = x1;
    for (x = x0, i = 0; x <= x1; x++) {                 /* the area under it */
        int32_t y;
        while (i < 5 && x > px[i + 1]) i++;
        y = i < 5 && px[i + 1] > px[i] ? py[i] + (py[i + 1] - py[i]) * (x - px[i]) / (px[i + 1] - px[i]) : py[5];
        cv_rect(x, y, 1, bot - y, fill);
    }
    if (pitch)
        for (x = x0; x < x1; x += 8)
            cv_rect(x, DXY(50), 4, 1, T_LINE);
    for (i = 0; i < 5; i++) {                           /* the segments; R1..R3 the first three, R4 the last */
        int seg = i < 3 ? i : i == 4 ? 3 : -1;
        cv_line_t(px[i], py[i], px[i + 1], py[i + 1], !lv && seg >= 0 && seg == hot ? T_TEXT : T_THEME,
                  !lv && seg >= 0 && seg == hot ? 3 : 2);
    }
    for (i = 1; i < 5; i++) {                           /* the points: L1 L2 L3, the hold's end (the turning one's: text) */
        int on = hot >= 0 && (lv ? i == hot + 1 || (hot == 2 && i == 4) : hot < 3 && i == hot + 1);
        cv_disc(px[i], py[i], on ? 9 : 7, on ? T_TEXT : T_THEME, 0);
    }
#undef DXY
}
static void pv_curve(int32_t h)
{
    if (cur_page()->graph == GR_FMEG || cur_page()->graph == GR_FMPEG)
        pv_dxeg(h);
    else if (cur_page()->graph == GR_ADSR)
        pv_env(h);
    else
        pv_lfo(h);
}
/* the drum grid (mock/r6_pages STEP): the eight lanes down the panel, 16 steps of the page across; a hit a cell in the
 * track's colour (an accent in text), an empty step a dot (every fourth brighter); the lane picked a band, its name in
 * the track's colour, the cursor a frame on it; the step playing marked over the lanes */
static int32_t pv_grid_play(void)                       /* the step playing on the page shown, -1 */
{
    uint32_t idx = TSEL->seq_idx;
    return song.playing && idx < (uint32_t)TSEL->p[P_SLEN] && idx / 16u == ui.bank ? (int32_t)(idx % 16u) : -1;
}
static void pv_grid_mark(void)                          /* (rows 3 .. 5, over the lanes) */
{
    if (pv.gplay >= 0)
        cv_rect(34 + pv.gplay * 12 + pv.gplay / 4 * 2, 3, 10, 3, T_TEXT);
}
static void pv_grid(int32_t h)
{
    const track_t *t = TSEL;
    uint32_t l, i, len = (uint32_t)t->p[P_SLEN], base = ui.bank * 16u;
    (void)h;
    pv_grid_mark();
    for (l = 0; l < NLANE; l++) {
        int32_t y = 10 + (int32_t)l * 20;
        int sel = l == ui.lane;
        uint16_t row = sel ? T_LIFT : T_PANEL;
        if (sel)
            cv_rect(2, y, PV_PANEL_W - 4, 18, T_LIFT);
        cv_text_on(8, y + 3, &AF_X, drum_lane_abbr(t, l), sel ? T_THEME : T_MID, row);
        for (i = 0; i < 16u && base + i < len; i++) {
            const step_t *st = &seq_steps(t)[base + i];
            uint32_t b = 1u << l;
            int32_t x = 34 + (int32_t)i * 12 + (int32_t)(i / 4u) * 2;
            if (sel && base + i == ui.cursor) {         /* the cursor: a rounded ring around the cell */
                cv_rrect(x - 2, y, 14, 18, 5, T_SEC, row);
                cv_rrect(x - 1, y + 1, 12, 16, 4, row, T_SEC);
            }
            if (step_lanes(st) & b)
                cv_rrect(x, y + 2, 10, 14, 3, (step_accents(st) & b) ? T_TEXT : T_THEME, row);
            else
                cv_disc(x + 5, y + 9, i % 4u == 0u || sel ? 4 : 3, i % 4u == 0u || sel ? T_MID : T_DIM, 0);
        }
    }
}
/* EG RATE / EG LVL: the operators as tabs over the panel, the one shown filled */
#define PV_OPTAB_H 20
static void pv_optabs(void)
{
    uint32_t i;
    char b[4] = {'O', 'P', '1', 0};
    cv_begin(240, PV_OPTAB_H + 2, T_BG);                /* (and the 2 rows down to the panel) */
    for (i = 0; i < 6u; i++) {
        int on = i == fm6_opsel % 6u;
        int32_t x = 5 + (int32_t)i * 39;
        b[2] = (char)('1' + i);
        cv_rrect(x, 1, 35, 16, 5, on ? T_THEME : T_SURF, T_BG);
        cv_text_c(x + 17, 4, &AF_X, b, on ? T_INK : T_MID, on ? T_THEME : T_SURF);
    }
    cv_blit(0, PV_CURVE_Y);
}
static uint32_t pv_curve_sig(void)
{
    const page_t *pg = cur_page();
    uint32_t h = 2166136261u, c;
    for (c = 0; c < 4u; c++)
        h = (h ^ (uint32_t)(pg->id[c] < P_COUNT ? TSEL->p[pg->id[c]] : 0)) * 16777619u;
    h = (h ^ (uint32_t)(TSEL->p[P_LD_PIT] + 131 * TSEL->p[P_LD_FLT] + 17161 * TSEL->p[P_LD_SHP])) * 16777619u;
    h = (h ^ (uint32_t)(TSEL->p[P_LD_AMP] + 256 * TSEL->p[P_LPOL] + 65536 * TSEL->p[P_LWAVE])) * 16777619u;
    h ^= fm6_pgen[song.sel % NTRK] * 2654435761u + fm6_opsel * 131u;   /* (FM6's EG pages: its patch, the operator) */
    return h ^ (uint32_t)(pv_hot() + 1) * 40503u ^ ux.gen * 977u ^ ux.pal * 31u ^ ui.page * 7919u ^ song.sel * 13u;
}

/* ---------------------------------------------------------- RINGS --- */

/* a page with a cutoff: the filter's response (flat, the resonance's peak at the cutoff, then the slope) */
static void pv_filter(int32_t cut, int32_t res)
{
    int32_t x, fc = 16 + clamp(cut, 0, 127) * 196 / 127, peak = clamp(res, 0, 127) * 30 / 127, y0 = 44 * 16, py = 0;
    for (x = 10; x < 102; x += 5)
        cv_rect(fc, x, 1, 2, ux_mix(T_PANEL, T_THEME, 60));
    for (x = 0; x < 216; x++) {
        int32_t d = 12 + x - fc, y = y0 - peak * 16 * 64 / (64 + d * d) + (d > 0 ? d * 14 : 0);
        y = clamp(y, 8 * 16, 100 * 16);
        cv_vspan_aa(12 + x, x ? py : y, y, T_THEME);
        py = y;
    }
}
/* which picture: 3 a filter (cut: its knob), 4 FM6's chart, 0 the scope */
/* ARP (mock/r6_pages ARP): the notes it plays, a block a step (16 at most): the keys held (else C3 E3 G3) over OCT
 * octaves in MODE's order, low to high bottom to top; the notes and the octaves at the top. OFF: dim */
static void pv_arp(void)
{
    static const uint8_t DEF[3] = {48, 52, 55};
    const track_t *t = TSEL;
    uint8_t base[16], seq[16];
    uint32_t nb = t->nheld ? t->nheld : 3u, n = 0, o, i, j, oct = (uint32_t)clamp(t->p[P_AOCT], 1, 4), mode = (uint32_t)t->p[P_AMODE];
    int32_t lo = 127, hi = 0, w;
    char b[32];
    if (nb > 16u) nb = 16u;
    for (i = 0; i < nb; i++) base[i] = t->nheld ? t->held[i] : DEF[i];
    if (mode != 5u)                                     /* (ORD: as played; the others from the lowest) */
        for (i = 1; i < nb; i++)
            for (j = i; j > 0 && base[j - 1] > base[j]; j--) { uint8_t x = base[j]; base[j] = base[j - 1]; base[j - 1] = x; }
    for (o = 0; o < oct; o++)
        for (i = 0; i < nb && n < 16u; i++) seq[n++] = (uint8_t)clamp(base[i] + 12 * (int32_t)o, 0, 127);
    if (mode == 2u)                                     /* DN */
        for (i = 0; i < n / 2u; i++) { uint8_t x = seq[i]; seq[i] = seq[n - 1u - i]; seq[n - 1u - i] = x; }
    else if (mode == 3u)                                /* UPDN: up, then down without the ends again */
        for (i = n >= 2u ? n - 2u : 0u; i > 0u && n < 16u; i--) seq[n++] = seq[i];
    else if (mode == 4u)                                /* RND: a shuffle (as a picture: one of its orders) */
        for (i = n; i > 1u; i--) { j = (i * 7u + 3u) % i; uint8_t x = seq[i - 1u]; seq[i - 1u] = seq[j]; seq[j] = x; }
    for (i = 0; i < n; i++) { lo = seq[i] < lo ? seq[i] : lo; hi = seq[i] > hi ? seq[i] : hi; }
    w = n ? 208 / (int32_t)n : 208;
    for (i = 0; i < n; i++) {
        int32_t y = 92 - (hi > lo ? (seq[i] - lo) * 64 / (hi - lo) : 32);
        cv_rrect(16 + (int32_t)i * w, y, w - 4 < 18 ? w - 4 : 18, 7, 2, mode ? T_THEME : T_DIM, T_PANEL);
    }
    b[0] = 0;
    for (i = 0; i < nb && i < 4u; i++) {
        note_name(b + str_len(b), base[i]);
        str_cpy(b + str_len(b), " ", 2);
    }
    str_cpy(b + str_len(b), "\xB7 ", 4);
    fmt_int(b + str_len(b), (int32_t)oct);
    str_cpy(b + str_len(b), " oct", 5);
    cv_text_on(12, 8, &AF_X, b, T_MID, T_PANEL);
}
/* PATTERN (mock/r6_pages PATTERN): the 64 steps as dots, four rows of 16: a note or a hit the track's colour, a rest
 * a line, past LEN dimmer still; the step playing white; LEN of 64 as the bar under them */
#define PV_STEP_X(s) (22 + (int32_t)((s) % 16u) * 12 + (int32_t)((s) % 16u / 4u) * 4)   /* a step's dot (7 px) */
static void pv_steps(void)
{
    const track_t *t = TSEL;
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), s, ph = pv.pplay >= 0 ? (uint32_t)pv.pplay : 0xFFu;
    for (s = 0; s < NSTEP; s++) {
        int32_t x = PV_STEP_X(s), y = 16 + (int32_t)(s / 16u) * 18;
        uint16_t c = s == ph ? T_TEXT : s >= len ? ux_mix(T_PANEL, T_LINE, 50) :
                     step_on(&seq_steps(t)[s]) ? T_THEME : T_LINE;
        cv_disc(x, y, s == ph || (s < len && step_on(&seq_steps(t)[s])) ? 7 : 5, c, 0);
    }
    cv_rrect(16, 92, 208, 4, 2, T_LINE, T_PANEL);
    cv_rrect(16, 92, (int32_t)len * 208 / NSTEP, 4, 2, T_THEME, T_LINE);
}


static uint32_t pv_pic(int32_t *cut, int32_t *res)
{
    const page_t *pg = cur_page();
    uint32_t e = TSEL->eng_req % NENGINES, c;
    *cut = -1;
    *res = 0;
    if(pg->fam==FAM_FX)return 9u;
    if (pg->graph == GR_ARP || pg->graph == GR_STEPS)
        return pg->graph == GR_ARP ? 5u : 6u;
    if (pg->fam == FAM_ARP)                             /* ARP 2: the notes it plays too */
        return 5u;
    if (pg->graph == GR_CHORD)                          /* CHORD: its name, its notes (ui_graph.c) */
        return 8u;
    if ((pg->scope == SC_ENGINE || pg->scope == SC_FM6 || pg->scope == SC_FMOP) && e == ENGI_FM6)
        return 4;
    for (c = 0; c < 4u; c++) {
        int16_t *vp;
        const param_desc_t *d = page_desc(pg, c, &vp);
        if (!d || !vp)
            continue;
        if (d->fmt == F_CUTOFF || str_eq(d->label, "CUTOFF")) *cut = *vp;   /* (the Prophet's: by its label) */
        else if (str_eq(d->label, "RES") || str_eq(d->label, "RESO")) *res = *vp;
    }
    return *cut >= 0 ? 3u : 0u;
}
/* FX follows R5: four rings, a rounded track-colour picture, compact labels.
 * WIDTH explicitly names its two sides and the mono tradeoff. */
static void pv_fx(void)
{
    if(cur_page()->id[0]==G_TRK_SEND || cur_page()->id[0]==P_FXSEND){
        uint32_t sends=cur_page()->id[0]==P_FXSEND;
        cv_text_on(12,8,&AF_X,song.g[G_FXMODE]?"Independent FX sends":"Track to shared FX",T_MID,T_PANEL);
        cv_text_on(12,45,&AF_X,sends?"One level per effect":song.g[G_FXMODE]?"SEND 1: this track":"SEND: this track",T_THEME,T_PANEL);
        cv_text_on(12,65,&AF_X,sends?"Returns join the dry mix":"RETURN: all tracks",T_THEME,T_PANEL);return;
    }
    uint32_t base=cur_page()->id[0],master=cur_page()->scope==SC_GLOBAL,slot=(base-(master?G_MFXTYPE:P_FXTYPE))/FX_PARAMS;
    const int16_t *values=master?song.g:TSEL->p;uint32_t type=(uint32_t)clamp(values[base],0,FX_COUNT-1);
    static const char *const hint[FX_COUNT]={"Dry signal","Moving notches","All-pass L / dry R","Random vowels","Phase / crossover jitter","Jumping resonance","Short lo-fi echoes","Small lo-fi space","Diffused stereo reverb","Stereo pitch shimmer"};
    int32_t mix=values[base+1u],cha=values[base+3u],py=50*16;
    cv_text_on(12,8,&AF_X,hint[type],T_MID,T_PANEL);
    for(int32_t x=0;x<208;x++){
        int32_t ph=(int32_t)(sine_i((uint32_t)x*0x06000000u)>>10);
        int32_t y=50*16;
        if(type==FX_PHASER || type==FX_WIDTH || type==FX_CHORUS)y+=ph*mix/9;
        else if(type==FX_FORMANT)y-=((ph+32)*mix)/15;
        else if(type==FX_TAPE || type==FX_SPIT)y+=(ph*((x*(cha+1)/32)%32))/4;
        else if(type>=FX_ECHO)y+=(ph*mix*(208-x))/1900;
        y=clamp(y,27*16,76*16);cv_vspan_aa(16+x,py,y,type&&mix?T_THEME:T_DIM);py=y;
    }
    const char *foot=fx_state[(master?NPART:song.sel%NPART)*FX_SLOTS+slot].failed?"MEMORY FULL":type==FX_WIDTH?"Mono changes the tone":type==FX_OFF || !mix?"Turn MIX to hear FX":master?"Shared by all tracks":"OCT+ sound actions";
    cv_text_on(12,88,&AF_X,foot,T_DIM,T_PANEL);
}
static void pv_picture(void)
{
    int32_t cut, res;
    uint32_t pic = pv_pic(&cut, &res), sig = pv_curve_sig() ^ pic * 131u ^ (uint32_t)(cut + 1) * 7919u, i;
    int32_t pp = pic == 6u && song.playing ? (int32_t)(TSEL->seq_idx % (uint32_t)clamp(TSEL->p[P_SLEN], 1, NSTEP)) : -1;
    sig ^= pic == 0u ? (ui.frame >> 1) * 2654435761u : 0u;   /* (the scope: every other frame) */
    if(pic==9u && cur_page()->id[0]!=G_TRK_SEND && cur_page()->id[0]!=P_FXSEND)sig^=(uint32_t)fx_state[(cur_page()->scope==SC_GLOBAL?NPART:song.sel%NPART)*FX_SLOTS+(cur_page()->id[0]-(cur_page()->scope==SC_GLOBAL?G_MFXTYPE:P_FXTYPE))/FX_PARAMS].failed*347u;
    if (pic >= 5u) {                                    /* ARP, PATTERN: the keys and steps */
        sig ^= steps_hash(TSEL) + (song.playing ? 1u + TSEL->seq_idx * 257u : 0u) * 2246822519u;
        for (i = 0; i < TSEL->nheld && i < 16u; i++)
            sig = (sig ^ TSEL->held[i]) * 16777619u;
        sig ^= TSEL->nheld * 131u;
    }
    if (pic == 4u || pic == 8u) {                       /* FM6: its chart, CHORD: its chord, as the panel draws them */
        graph_y = PV_PIC_Y;
        graph_h = PV_PIC_H;
        draw_graph();
        graph_y = Y_GRAPH;
        graph_h = H_GRAPH;
        return;
    }
    if (!ui.force && sig == pv.panel && pp == pv.pplay)
        return;
    pv.panel = sig;                                     /* (PATTERN, the step playing alone moved: cv_blit sends its */
    pv.pplay = pp;                                      /*  two dots) */
    {
        int32_t dy = 0;                                 /* (the picture whole under tabs too: the rings shorter) */
        cv_begin(240, (uint32_t)(PV_PIC_H - dy), T_BG);
        cv_rrect(PV_PANEL_X, 0, PV_PANEL_W, PV_PIC_H - dy, 6, T_PANEL, T_BG);
        cv_bg = T_PANEL;
        cv_oy = -dy / 2;                                /* (under the tabs: the picture's middle) */
        if (pic == 5u) pv_arp();
        else if (pic == 6u) pv_steps();
        else if (pic == 3u) pv_filter(cut, res);
        else if (pic == 9u) pv_fx();
        else stage_wave(T_THEME, 8 + dy / 2, PV_PIC_H - 16 - dy);
        cv_oy = 0;
        cv_blit(0, (uint32_t)(PV_PIC_Y + dy));
    }
}

/* ---------------------------------------------------------- MIXER --- */
#define PV_STRIP_TOP 22                                 /* a strip: 54 x 212 at CARD_X(k), rows 22 .. 233 */
#define PV_STRIP_ROWS 212
static struct { uint32_t strip[NTRK]; uint8_t meter[NTRK]; } pvm;
/* knob_arc as knob() draws it (a bipolar value from 12 o'clock), on bg */
static void pv_knob(int32_t x, int32_t y, int32_t r, const uint8_t *cov, const uint8_t *ang, int32_t v, int32_t lo,
                    int32_t hi, uint16_t vc, uint16_t bg)
{
    int32_t a0, a1;
    if (lo < 0) {
        int32_t s = v * KA_END / (v < 0 ? -lo : hi);
        a0 = (s < 0 ? s : 0) - 8;
        a1 = (s > 0 ? s : 0) + 8;
    } else {
        a0 = -KA_END;
        a1 = v > lo ? -KA_END + (v - lo) * 2 * KA_END / (hi - lo) : -KA_END - 1;
    }
    knob_arc(x, y, r, cov, ang, a0, a1, T_LINE, vc, bg);
}
static void pv_meter(uint32_t k, uint16_t c)           /* track k's meter: 3 x 44 beside its level */
{
    int32_t h = pvm.meter[k] * 44 / (TS_MH - 2);
    cv_begin(3, 44, T_LINE);
    cv_rect(0, 0, 3, 44, k == song.sel ? T_LIFT : T_PANEL);
    cv_rrect(0, 0, 3, 44, 1, T_LINE, k == song.sel ? T_LIFT : T_PANEL);
    if (h > 0)
        cv_rrect(0, 44 - h, 3, h, 1, c, T_LINE);
    cv_blit((uint32_t)(CARD_X(k) + 48), PV_STRIP_TOP + 50);
}
static void pv_strip(uint32_t k)
{
    track_t *t = &trk[k];
    uint32_t sel = k == song.sel, mute = t->p[P_MUTE] != 0, arm = (song.rec >> k) & 1u, lvl = trk_level(k), sig;
    uint32_t hot = sel && ui.hot_t ? ui.hot_col + 1u : 0u;   /* 1 LEVEL, 2 PAN, 3 unused, 4 MUTE */
    int32_t pk = t->peak, m, pan = clamp(t->p[P_PAN], -64, 63);
    uint16_t tc = mute ? T_DIM : T_TRK(k), bg = sel ? T_LIFT : T_PANEL;
    char nm[16], l1[16], l2[16], b[8];
    t->peak = 0;
    m = mute ? 0 : meter_px(pk);
    if (m < pvm.meter[k] - 1)
        m = pvm.meter[k] - 1;                           /* (falls ~2 dB a frame) */
    trk_short_name(k, nm);
    if (t->eng_req == ENGI_PROPHET)
        p5_short_name(t, nm, sizeof nm);
    sig = str_hash(1u + sel * 2u + mute * 4u + arm * 8u + hot * 16u, nm) + lvl * 7919u +
          (uint32_t)(pan + 128) * 104729u;
    if (!ui.force && sig == pvm.strip[k]) {
        if (m != pvm.meter[k]) {                        /* the meter alone */
            pvm.meter[k] = (uint8_t)(m < 0 ? 0 : m);
            pv_meter(k, tc);
        }
        return;
    }
    pvm.strip[k] = sig;
    pvm.meter[k] = (uint8_t)(m < 0 ? 0 : m);
    cv_begin(CARD_W, PV_STRIP_ROWS, T_BG);
    if (sel) {
        cv_rrect(0, 0, CARD_W, PV_STRIP_ROWS, 6, T_TRK(k), T_BG);
        cv_rrect(1, 1, CARD_W - 2, PV_STRIP_ROWS - 2, 5, bg, T_TRK(k));
    } else {
        cv_rrect(0, 0, CARD_W, PV_STRIP_ROWS, 6, bg, T_BG);
    }
    cv_rect(3, 0, CARD_W - 6, 3, tc);                   /* the bar */
    stage_two_lines(nm, l1, l2, CARD_W - 10);
    cv_free_text(6, 7, &AF_S, l1, mute ? T_DIM : T_TEXT, bg, CARD_W - 8);
    if (l2[0])
        cv_free_text(6, 20, &AF_S, l2, mute ? T_DIM : T_TEXT, bg, CARD_W - 8);
    pv_knob(27 - KNOB_LEVEL_R, 70 - KNOB_LEVEL_R, KNOB_LEVEL_R, KNOB_LEVEL_COV, KNOB_LEVEL_ANG, (int32_t)lvl, 0, 127,
            hot == 1u ? T_TEXT : tc, bg);
    if (lvl) {
        int32_t d = LEVEL_DB_X10[lvl];
        fmt_int(b, (d + (d < 0 ? -5 : 5)) / 10);
    } else {
        str_cpy(b, "OFF", sizeof b);
    }
    cv_text_c(27, 63, &AF_S, b, hot == 1u ? T_TEXT : mute ? T_DIM : T_TEXT, bg);
    if (lvl)
        cv_text_c(27, 76, &AF_X, "dB", T_MID, bg);
    cv_rrect(48, 50, 3, 44, 1, T_LINE, bg);             /* (the meter: pv_meter) */
    cv_text_c(15, 104, &AF_X, "PAN", T_MID, bg);
    pv_knob(15 - KNOB_SMALL_R, 130 - KNOB_SMALL_R, KNOB_SMALL_R, KNOB_SMALL_COV, KNOB_SMALL_ANG, pan, -64, 63,
            hot == 2u ? T_TEXT : tc, bg);
    if (pan) {
        b[0] = pan < 0 ? 'L' : 'R';
        fmt_int(b + 1, pan < 0 ? -pan : pan);
    } else {
        str_cpy(b, "C", sizeof b);
    }
    cv_text_c(15, 145, &AF_X, b, hot == 2u ? T_TEXT : T_SEC, bg);
    if (mute) {                                         /* MUTE: lit when muted */
        cv_rrect(6, 168, 42, 16, 5, hot == 4u ? T_TEXT : ux_mix(T_SURF, T_MID, 50), bg);
        cv_text_c(27, 170, &AF_X, "MUTE", T_BG, hot == 4u ? T_TEXT : ux_mix(T_SURF, T_MID, 50));
    } else {
        cv_rrect(6, 168, 42, 16, 5, hot == 4u ? T_TEXT : T_LINE, bg);
        cv_rrect(7, 169, 40, 14, 4, T_SURF, hot == 4u ? T_TEXT : T_LINE);
        cv_text_c(27, 170, &AF_X, "MUTE", T_DIM, T_SURF);
    }
    if (arm)                                            /* armed: a REC dot, else its ring */
        cv_disc(27, 198, 9, T_REC, 0);
    else
        cv_disc(27, 198, 9, T_DIM, 1);
    if (pvm.meter[k]) {
        int32_t h = pvm.meter[k] * 44 / (TS_MH - 2);
        cv_rrect(48, 94 - h, 3, h, 1, tc, T_LINE);
    }
    cv_blit((uint32_t)CARD_X(k), PV_STRIP_TOP);
}

/* ----------------------------------------------------------- draw --- */
static void pv_draw(void)
{
    uint32_t kind = pv_kind(), sig;
    int32_t px;
    if (kind == PV_NOTES) {
        if (ui.force) {
            lcd_fill(0, H_HEAD, 240, 240 - H_HEAD, T_BG);
            ui.graph_sig = 0;
        }
        draw_head();
        col_style = CS_CHIP;
        draw_columns();
        col_style = CS_CARD;
        pv_sound();
        sig = graph_sig(0);
        px = notes_play_x(TSEL);
        if (ui.force || sig != ui.graph_sig || px != proll.play) {   /* the roll, in two slices (its labels may cross
                                                                      * the seam); the playhead alone: its columns */
            int32_t top, from = proll.play, cols = !ui.force && sig == ui.graph_sig;
            ui.graph_sig = sig;
            proll.play = px;
            for (top = 0; top < PR_H; top += H_GRAPH) {
                int32_t sh = PR_H - top < H_GRAPH ? PR_H - top : H_GRAPH;
                cv_begin(240, (uint32_t)sh, T_BG);
                cv_oy = -top;
                cv_rrect(PV_PANEL_X, 0, PV_PANEL_W, PR_H, 6, T_PANEL, T_BG);
                cv_bg = T_PANEL;
                cv_scroll = 1;
                graph_recorded_notes(TSEL, T_THEME);
                cv_scroll = 0;
                cv_oy = 0;
                if (cols)
                    pv_cols((uint32_t)(PR_TOP + top), from, px, 1);
                else
                    cv_blit(0, (uint32_t)(PR_TOP + top));
            }
        }
        return;
    }
    if (kind == PV_GRID) {
        if (ui.force)
            lcd_fill(0, H_HEAD, 240, 240 - H_HEAD, T_BG);
        draw_head();
        col_style = CS_CHIP;
        draw_columns();
        col_style = CS_CARD;
        pv_sound();
        sig = graph_sig(0);
        px = pv_grid_play();
        if (ui.force || sig != ui.graph_sig || px != pv.gplay) {   /* (the step playing alone: cv_blit sends its mark) */
            ui.graph_sig = sig;
            pv.gplay = px;
            pv_panel(PV_GRID_Y, PV_GRID_H, pv_grid);
        }
        return;
    }
    if (kind == PV_MOD) {
        if (ui.force)
            lcd_fill(0, H_HEAD, 240, 240 - H_HEAD, T_BG);
        draw_head();
        pv_mod_rows();
        col_style = CS_CHIP;
        pv_chip_y = 220;
        draw_columns();
        pv_chip_y = PV_CHIP_Y;
        col_style = CS_CARD;
        return;
    }
    if (kind == PV_MIXER) {
        uint32_t k;
        if (ui.force)
            lcd_fill(0, H_HEAD, 240, 240 - H_HEAD, T_BG);
        draw_head();
        for (k = 0; k < NTRK; k++)
            pv_strip(k);
        return;
    }
    pv_tab_h = kind == PV_RINGS && (sec_on() || (designer_on() && designer_tabs_on())) ? SEC_TAB_H : 0;
    if (kind != PV_CURVE) {                             /* RINGS, FADERS */
        if (ui.force) {                                 /* (the parts then cover what they draw) */
            lcd_fill(0, H_HEAD, 240, PV_SOUND_Y - H_HEAD, T_BG);
            ui.graph_sig = 0;
        }
        draw_head();
        if (pv_tab_h && designer_on()) {                /* the sound designer: its stages */
            designer_tabs();
        } else if (pv_tab_h) {                          /* an engine in sections: its tabs, the page noted */
            sec_note();
            sec_tabs();
        }
        col_style = kind == PV_RINGS ? CS_RING : CS_FADER;
        draw_columns();
        col_style = CS_CARD;
        pv_sound();
        if (kind == PV_RINGS)
            pv_picture();
        return;
    }
    if (ui.force) {                                     /* the BG the parts do not cover */
        uint32_t i;
        lcd_fill(0, H_HEAD, 240, PV_CURVE_Y - H_HEAD, T_BG);
        lcd_fill(0, PV_CURVE_Y, PV_PANEL_X, PV_CURVE_H, T_BG);
        lcd_fill(PV_PANEL_X + PV_PANEL_W, PV_CURVE_Y, 240u - PV_PANEL_X - PV_PANEL_W, PV_CURVE_H, T_BG);
        lcd_fill(0, PV_CURVE_Y + PV_CURVE_H, 240, PV_STRIP_Y - PV_CURVE_Y - PV_CURVE_H, T_BG);
        lcd_fill(0, PV_STRIP_Y, (uint32_t)CARD_X(0), PV_STRIP_H, T_BG);
        for (i = 0; i < 4u; i++)
            lcd_fill((uint32_t)(CARD_X(i) + CARD_W), PV_STRIP_Y,
                     i < 3u ? (uint32_t)(CARD_X(i + 1u) - CARD_X(i) - CARD_W) :
                     240u - (uint32_t)(CARD_X(i) + CARD_W), PV_STRIP_H, T_BG);
        lcd_fill(0, PV_STRIP_Y + PV_STRIP_H, 240, PV_SOUND_Y - PV_STRIP_Y - PV_STRIP_H, T_BG);
    }
    draw_head();
    col_style = CS_STRIP;
    draw_columns();
    col_style = CS_CARD;
    pv_sound();
    (void)kind;
    sig = pv_curve_sig();
    if (ui.force || sig != pv.panel) {                  /* (the busiest last) */
        int tabs = cur_page()->graph == GR_FMEG;        /* (EG RATE / LVL: the operators over the panel) */
        pv.panel = sig;
        if (tabs)
            pv_optabs();
        pv_panel(PV_CURVE_Y + (tabs ? PV_OPTAB_H + 2 : 0), PV_CURVE_H - (tabs ? PV_OPTAB_H + 2 : 0), pv_curve);
    }
}
