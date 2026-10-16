/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Melodee UI drawing: the header, the four knobs' cells (the strip), the footer (pages of the old template: one row)
 * and the frame; the panel between the strip and the footer is ui_graph.c. The layout: SURF cells and panels on
 * BG, rounded corners, no rules, colours from the theme tokens only (gfx.c T_*); THEME is the selected track's
 * colour (labels, gauges, curves), values in TEXT. Type: the large type's roles (gfx.c TY_*; docs/design/README.md
 * "Large type"): labels 16 px, text and the strip's values 20 px, values 28 px, the hero (the chord, big names) 48 px,
 * in Rubik or Melodee Pixel. A value too wide for its cell is set a role smaller (never under the label face); free text (names, messages)
 * is ellipsised. */
static void draw_menu(void);
static int name_on(void);                              /* NAME (ui_name.c) */
static void name_draw(void);
static int new_on(void);                               /* NEW SONG (ui_new.c) */
static void new_draw(void);

/* --------------------------------------------------------- drawing --- */
#define COL_W CARD_W                                  /* a knob's cell: 57 x 48 at x 2 + 60 c, y 32 */
#define COL_H CARD_H

static int32_t batt_level(void)                         /* thresholds 531 / 561 / 591 on ADC ch3 */
{
    return song.batt_raw >= 591 ? 3 : song.batt_raw >= 561 ? 2 : song.batt_raw >= 531 ? 1 : 0;
}
/* A USB host powers us; there is no separate charger status line.
 * 4 means external power (a steady bolt inside the battery), otherwise its level. */
static int32_t batt_shown(void)
{
    if (usb.config && !usb.suspended)
        return 4;
    return batt_level();
}

/* REC: filled, the selected track ready/recording; outlined, waiting or another track armed. */
static void draw_rec_mark(int32_t x, uint16_t bg)
{
    if (song.rec)
        cv_icon_mid(x, H_HEAD / 2, 16, ((song.rec & ~song.rec_wait) >> song.sel) & 1u ? ICON_X_REC : ICON_X_REC_O, T_REC, bg);
}

/* ---------------------------------------------------- rolling digits --- */
/* A number that changes rolls its changed digits like a slot machine; only the drawing: the value, the gauge,
 * the hot colour and the sound change at once. Up: the old digit leaves upward and the new one comes from below;
 * down: the reverse. ROLL_FRAMES UI frames (ui.frame, ~135 ms) on an integer ease-out (ROLL_EASE: cubic, over
 * half-way in a quarter of the time, no overshoot); the ones digit first, each place one frame later, all ending
 * together on a frame equal to the static render. Clipped to the value strip, which alone is redrawn while it
 * rolls (a card: rows ROLL_Y.., the header: the BPM's columns). Characters other than digits never roll.
 * A change during a roll retargets it: it restarts from the value it was going to. Shown at once (no roll):
 * a change within ROLL_SNAP frames of the last one (a fast turn reads better as plain numbers), another
 * shape (length, sign, a non-digit, a name or an enum, a value set in S), another label or unit, a track /
 * engine / palette change (ui.roll[].sig), ui.force (a page change), a card's first draw. */
#define ROLL_FRAMES 9u
#define ROLL_SNAP 3u                                    /* frames (~45 ms) */
#define ROLL_BPM 4u                                     /* ui.roll[]: the four cards, then the header BPM */
#define ROLL_Y 20                                       /* a cell's value strip: rows 20..43 (the text face) */
#define ROLL_H 24
#define BPM_X 26
#define BPM_W 42                                        /* the header's BPM strip: 42 columns, every row (the text face) */
static uint16_t roll_bg;                                /* what a card's value lies on (its fill: tinted while hot) */
static const uint8_t ROLL_EASE[17] = {0, 45, 84, 118, 147, 172, 193, 210, 223, 234, 242, 247, 251, 253, 254, 255, 255};

static int roll_digit(char c) { return c >= '0' && c <= '9'; }
/* +1 / -1: b rolls in from a (same length, digits where a has digits, the rest equal and one of + - .),
 * the sign of b - a; 0: b is shown at once */
static int roll_dir(const char *a, const char *b)
{
    uint32_t i;
    int d = 0, neg = 0;
    for (i = 0; a[i] || b[i]; i++) {
        if (i + 1u >= sizeof ui.roll[0].from || roll_digit(a[i]) != roll_digit(b[i]))
            return 0;
        if (!roll_digit(a[i]) && (a[i] != b[i] || (a[i] != '-' && a[i] != '+' && a[i] != '.')))
            return 0;
        neg |= a[i] == '-';
        if (!d && a[i] != b[i])
            d = b[i] > a[i] ? 1 : -1;
    }
    return neg ? -d : d;
}
/* field k now shows b instead of a: roll (a retarget mid-roll), or snap */
static void roll_note(uint32_t k, const char *a, const char *b, int snap)
{
    uint32_t el = (uint8_t)(ui.frame - ui.roll[k].t0);
    int d = roll_dir(a, b);
    ui.roll[k].t0 = (uint8_t)ui.frame;
    ui.roll[k].from[0] = 0;
    if (snap || !d || el < ROLL_SNAP)
        return;
    str_cpy(ui.roll[k].from, a, sizeof ui.roll[k].from);
    ui.roll[k].dir = (int8_t)d;
}
/* s (the text face) at x, y with field k's roll: each character at its pen in s (the digits are tabular and never kerned,
 * so the old value has the same pens); a rolling digit has moved o of the strip's h rows (ROLL_EASE, one frame
 * later per place from the right), clipped to the strip. The roll's last frame clears it: the static text. */
static int32_t roll_text(uint32_t k, int32_t x, int32_t y, const char *s, uint16_t fg)
{
    const aafont_t *f = TY_TX;
    uint32_t e = (uint8_t)(ui.frame - ui.roll[k].t0) + 1u, i, p = 0;
    const char *a = ui.roll[k].from;
    int32_t cy = k < ROLL_BPM ? y : 0, h = k < ROLL_BPM ? ROLL_H : H_HEAD;   /* (a card's value: at ROLL_Y) */
    uint16_t bg = k < ROLL_BPM ? roll_bg : T_BG;
    char t[8], ch[2] = {0, 0};
    if (e >= ROLL_FRAMES)
        ui.roll[k].from[0] = 0;
    if (!a[0])
        return cv_text_on(x, y, f, s, fg, bg);
    str_cpy(t, s, sizeof t);
    for (i = 0; s[i]; i++)
        p += (uint32_t)roll_digit(s[i]);
    cv_cy0 = (int16_t)(cy + cv_oy);                     /* the strip (the static characters lie inside it) */
    cv_cy1 = (int16_t)(cy + cv_oy + h);
    cv_scroll = 1;                                      /* (the lint: rolling digits are cut on purpose) */
    for (i = 0; s[i]; i++) {
        int32_t px, ny = y;
        t[i] = 0;
        px = x + text_w(f, t);                          /* (its pen: no digit kerns) */
        t[i] = s[i];
        p -= (uint32_t)roll_digit(s[i]);
        if (s[i] != a[i]) {                             /* p: the places right of it */
            int32_t o = 0, dir = ui.roll[k].dir;
            if (e > p)                                  /* (e < ROLL_FRAMES: the LUT index stays in 1..16) */
                o = (int32_t)((ROLL_EASE[((e - p) * 32u / (ROLL_FRAMES - p) + 1u) >> 1] * (uint32_t)h + 128u) >> 8);
            ch[0] = a[i];
            cv_text_on(px, y - dir * o, f, ch, fg, bg);
            ny = y + dir * (h - o);
        }
        ch[0] = s[i];
        cv_text_on(px, ny, f, ch, fg, bg);
    }
    cv_cy0 = 0;
    cv_cy1 = (int16_t)cv_h;
    cv_scroll = 0;
    return x + text_w(f, s);
}

/* the header (large type, 30 px): the transport (16 px; a song playing: its disc) at x 5, the BPM (the text face,
 * rolling) at x 26, the REC mark (filled: the selected track armed; a ring: another one), the octave shift as an
 * outlined pill ("+2") and the song's row, then the page's dots; at the right a chip in the track's colour (24 px, the
 * label face) naming where you are (Stage: the engine; a page: its title), or a message in its place (a layer: its
 * name); a battery glyph only while it runs low. Which track: Stage's rows, the pages' sound bar (its numeral) */
static uint32_t page_sheet(void);                       /* ui_popup.c */
static void list_words(char *d, const char *label, uint32_t n);   /* ui_list.c */
static int sec_on(void);                                /* ui_sections.c */
static uint32_t sec_of(uint32_t i);
static void sec_chip(uint32_t i, char *b, uint32_t n);
static int sec_map_on(void);
static void sound_name(const track_t *t, char *b);
static int ptc_on(void);                                /* ui_patterns.c */
static void head_chip_text(char *b)
{
    const page_t *pg = cur_page();
    uint32_t e = TSEL->eng_req % NENGINES;
    if (designer_on() && !name_on() && designer_head_chip(b)) {   /* the designer's START, FEATURES, original */
    } else if (new_on()) {                                     /* NEW SONG */
        str_cpy(b, "NEW SONG", 16);
    } else if (ptc_on()) {                              /* PATTERNS' Copy to: "copy 3 to 5" */
        str_cpy(b, "copy 1 to 1", 16);
        b[5] = (char)('1' + ui.ptc_src % NPAT);
        b[10] = (char)('1' + ui.ptc_dst % NPAT);
    } else if (ui.home) {                               /* Stage: the engine (a kit: its name, "909 KIT") */
        if (browse_pending()) { uint32_t kk, s = browse_shown(&kk); e = src_engine(s, kk); }
        if (ENGINES[eng_idx(e)] == &ENG_DRUM && !browse_pending()) sound_name(TSEL, b);
        else str_cpy(b, ENGINES[eng_idx(e)]->name, 16);
    } else if (sec_map_on()) {                          /* the map: the engine */
        str_cpy(b, ENGINES[e]->name, 16);
    } else if (pg->graph == GR_ROLL && grid_on()) {     /* the drum grid */
        str_cpy(b, "STEP", 16);
    } else if (pg->graph == GR_FMEG) {                  /* FM6's EG RATE / LVL: "OP3 EG" */
        str_cpy(b, "OP1 EG", 16);
        b[2] = (char)('1' + fm6_opsel % 6u);
    } else if (pg->fam == FAM_FX && pg->graph == GR_NONE) {   /* an FX slot: its effect ("ECHO") */
        str_cpy(b, N_FX[clamp((pg->scope == SC_GLOBAL ? song.g : TSEL->p)[pg->id[0] - 1u], 0, FX_COUNT - 1)], 16);
    } else if (pg->scope != SC_ENGINE && sec_on()) {   /* an engine in sections: the page's name in its section */
        sec_chip(ui.page, b, 16);
    } else {
        const char *t = pg->scope == SC_ENGINE ? ENGINES[e]->page_title[pg->id[0] != P_E0] : pg->title;
        str_cpy(b, t, 16);
    }
}
/* the page's place in its family: a dot a page, the page's a pill in the track's colour (no room: "3/12") */
static void head_pages(int32_t x, int32_t x1)          /* (x: after the transport's marks; x1: where the chip, or the
                                                         * battery, starts) */
{
    const page_t *pg = cur_page();
    uint32_t i, n = 0, k = 0, sc = sec_on() ? sec_of(ui.page) + 1u : 0u;
    if (designer_on() && !name_on())                    /* the designer: its stage's pages */
        designer_pages(&n, &k);
    else for (i = 0; i < NPAGES; i++)                   /* (an engine in sections: the section's pages) */
        if (PAGES[i].fam == pg->fam && page_visible(i) &&
            (!sc || (PAGES[i].scope == SC_FMOP ? PAGES[ui.page].scope == SC_FMOP : sec_of(i) + 1u == sc))) {
            if (i == ui.page)
                k = n;
            n++;
        }
    if (n < 2u)
        return;
    if (x + (int32_t)n * 12 + 10 > x1 - 6) {            /* (no room for the dots; none for that either: nothing) */
        char b[8];
        fmt_int(b, (int32_t)k + 1);
        str_cpy(b + str_len(b), "/", 2);
        fmt_int(b + str_len(b), (int32_t)n);
        if (x + text_w(TY_LB, b) <= x1 - 4)
            cv_text_on(x, ty_y(H_HEAD / 2, TY_LB), TY_LB, b, T_MID, T_BG);
        return;
    }
    for (i = 0; i < n; i++) {                           /* a dot a page, the page's a pill */
        int32_t w = i == k ? 16 : 6;
        cv_rrect(x, H_HEAD / 2 - 3, w, 6, 3, i == k ? T_THEME : T_LINE, T_BG);
        x += w + 6;
    }
}
/* the message (or the layer's name) does not fit the header from x: it goes under it as a toast (draw_toast) */
static uint8_t toast_shown;                             /* the toast drawn (its place to give back when it goes) */
/* Ongoing transport states belong in the header, never in timed messages.
 * Placement and priority: docs/design/README.md "Status indicators". */
static int head_waiting(void) { return (song.rec_wait >> song.sel) & 1u; }
static const char *head_msg(void) { return ui.msg_t ? ui.msg : ui.layer ? layer_head() : ""; }
static int head_toast(int32_t x)
{
    const char *m = head_msg();
    uint32_t n;
    int32_t w = text_w(TY_TX, m);
    if (kc_tag(m, &n) >= 0 || (m[0] && str_len(m) > 6u && m[str_len(m) - 1u] == ']'))
        w += 40;                                        /* (a keycap: its pill wider than its word) */
    return m[0] && w > 236 - x;
}
static void draw_head(void)
{
    char b[16], chip[16];
    int32_t x;
    uint32_t low = !(usb.config && !usb.suspended) && batt_level() <= 1, msg = ui.msg_t || ui.layer;
    uint32_t sig, moved = 0;
    int16_t bpm = song.g[G_BPM];                        /* (read once: what is noted is what is drawn) */
    head_chip_text(chip);
    sig = (uint32_t)seq_erase_active(TSEL) * 8191u + (uint32_t)song.playing * 3u + song.rec * 5u + song.rec_wait * 65537u + (uint32_t)(song.octave + 8) * 11u +
          song.sel * 13131u + (msg ? str_hash(7u, ui.msg_t ? ui.msg : layer_head()) : str_hash(5u, chip)) +
          (ui.bpm_t != 0) * 31u + low * 7777u + (chain.running ? (chain.row + 1u) * 104729u : 0u) + ux.gen * 977u +
          (ui.home ? ui.home_pg * 40503u : ui.page * 2654435761u) + page_sheet() * 524287u;
    if (bpm != ui.roll_bpm) {
        char a[8];
        fmt_int(a, ui.roll_bpm);
        fmt_int(b, bpm);
        roll_note(ROLL_BPM, a, b, ui.force);
        ui.roll_bpm = bpm;
        moved = 1;
    } else if (ui.force) {
        ui.roll[ROLL_BPM].from[0] = 0;
    }
    fmt_int(b, bpm);
    if (!ui.force && sig == ui.head_sig) {
        if (ui.roll[ROLL_BPM].from[0] || moved) {       /* rolling, or snapped to the new value: the BPM's strip only */
            cv_begin(BPM_W, H_HEAD, T_BG);
            roll_text(ROLL_BPM, 0, ty_y(H_HEAD / 2, TY_TX), b, ui.bpm_t ? T_ACCENT : T_TEXT);
            cv_blit(BPM_X, Y_HEAD);
        }
        return;
    }
    ui.head_sig = sig;
    cv_begin(240, H_HEAD, T_BG);
    cv_icon_mid(5, H_HEAD / 2, 16, chain.running ? ICON_X_SONG : song.playing ? ICON_X_PLAY : ICON_X_STOP,
                song.playing ? T_TEXT : T_MID, T_BG);
    roll_text(ROLL_BPM, BPM_X, ty_y(H_HEAD / 2, TY_TX), b, ui.bpm_t ? T_ACCENT : T_TEXT);
    x = BPM_X + BPM_W;                                  /* (past the widest BPM) */
    if (song.rec) {                                     /* (16 px, as the transport) */
        cv_icon_mid(x, H_HEAD / 2, 16, ((song.rec & ~song.rec_wait) >> song.sel) & 1u ? ICON_X_REC : ICON_X_REC_O, T_REC, T_BG);
        x += 18;
    }
    if (msg && head_toast(x + 6))                       /* (too long for the header: a toast under it, draw_toast; the
                                                         * header as it is) */
        msg = 0;
    if (seq_erase_active(TSEL)) {
        cv_text_r(236, ty_y(H_HEAD / 2, TY_TX), TY_TX, "ERASING", T_REC, T_BG);
    } else if (head_waiting()) {
        cv_text_r(236, ty_y(H_HEAD / 2, TY_TX), TY_TX, "WAIT LOOP", T_REC, T_BG);
    } else if (msg) {                                   /* a message, or the layer's name (may hold a keycap) */
        cv_free_hint(x + 6, ty_y(H_HEAD / 2, TY_TX), ui.msg_t ? ui.msg : layer_head(), T_TEXT, T_BG, 236 - x - 6);
    } else {
        int32_t cw = text_w(TY_TX, chip) + 4;           /* (where you are: plain words in the track's colour;
                                                         * PATTERNS plain, the grid is in colour) */
        if (song.octave) {                              /* the octave shift, signed, in an outlined pill ("+2") */
            int32_t w;
            b[0] = song.octave > 0 ? '+' : '-';
            fmt_int(b + 1, song.octave < 0 ? -song.octave : song.octave);
            w = text_w(TY_LB, b) + 12;
            cv_rrect(x + 2, 4, w, 22, 6, T_MID, T_BG);
            cv_rrect(x + 3, 5, w - 2, 20, 5, T_BG, T_MID);
            cv_text_c(x + 2 + w / 2, ty_y(H_HEAD / 2, TY_LB), TY_LB, b, T_TEXT, T_BG);
            x += w + 8;
        }
        if (chain.running) {                            /* the song's row (where there is room) */
            str_cpy(b, "ROW ", 8);
            fmt_int(b + 4, (int32_t)chain.row + 1);
            if (x + 2 + text_w(TY_LB, b) + 6 <= 236 - cw - (low ? 22 : 0) - 6)
                x = cv_text_on(x + 2, ty_y(H_HEAD / 2, TY_LB), TY_LB, b, T_MID, T_BG) + 6;
        }
        if (!ui.home) {                                 /* a page: where it is in its family */
            head_pages(x + 6, 236 - cw - (low ? 22 : 0));
        } else if (x + 6 + 28 <= 236 - cw - (low ? 22 : 0) - 6) {   /* HOME: its two pages (Stage, the chord view),
                                                                   * where there is room */
            uint32_t i;
            for (i = 0, x += 6; i < 2u; i++) {
                int32_t w = i == ui.home_pg ? 16 : 6;
                cv_rrect(x, H_HEAD / 2 - 3, w, 6, 3, i == ui.home_pg ? T_THEME : T_LINE, T_BG);
                x += w + 6;
            }
        }
        if (low)                                        /* the battery, only when it runs low */
            cv_icon_mid(236 - cw - 20, H_HEAD / 2, 16, batt_level() ? ICON_X_BAT1 : ICON_X_BAT0, T_REC, T_BG);
        cv_text_r(236, ty_y(H_HEAD / 2, TY_TX), TY_TX, chip,
                  !ui.home && cur_page()->graph == GR_PATGRID && !ptc_on() ? T_MID : T_THEME, T_BG);
    }
    cv_blit(0, Y_HEAD);
}
/* full redraw: the strips (header, cards, panel, footer; Stage: its panel and lanes) cover the rest; only the BG
 * between them is filled */
static void draw_frame(int stage)
{
    uint32_t i;
    if (stage) {                                        /* Stage, HOME's second page: their parts cover what they draw */
        lcd_fill(0, H_HEAD, 240, 240u - H_HEAD, T_BG);
        return;
    }
    lcd_fill(0, H_HEAD, 240, Y_LABEL - H_HEAD, T_BG);
    lcd_fill(0, Y_SEP_END, 240, Y_GRAPH - Y_SEP_END, T_BG);
    lcd_fill(0, Y_GRAPH + H_GRAPH, 240, Y_FOOT - Y_GRAPH - H_GRAPH, T_BG);
    lcd_fill(0, Y_LABEL, (uint32_t)CARD_X(0), CARD_H, T_BG);
    for (i = 0; i < 4u; i++)                            /* right of each card */
        lcd_fill((uint32_t)(CARD_X(i) + CARD_W), Y_LABEL, i < 3u ? (uint32_t)(CARD_X(i + 1u) - CARD_X(i) - CARD_W) :
                 240u - (uint32_t)(CARD_X(i) + CARD_W), CARD_H, T_BG);
}

/* one card = one knob: LABEL (THEME) / value unit / gauge on a SURF card, redrawn only when it changed.
 * The value is M, or S when M is too wide (engine names, long ENUMs); the unit S after it. The gauge is
 * a 3 px rounded bar: LINE track, THEME fill. The knob just turned (hot): the card outlined in THEME, the label
 * and value in the accent. ratio: 0..1000 for the gauge, -1 = no gauge. icon: ICON_* (icons.c), ICON_AUTO = by label;
 * a label too long to share the card with its icon goes without it.
 * vc: the value's colour (T_THEME, T_DIM inactive, T_ACCENT the knob just turned) */
/* A binary value remains readable without touching its encoder. */
static int binary_value(const char *val)
{
    return str_eq(val, "ON") || str_eq(val, "On") ? 1 : str_eq(val, "OFF") || str_eq(val, "Off") ? 0 : -1;
}
static void binary_badge(int32_t xc, int32_t y, int32_t h, const char *val, uint16_t bg)
{
    int on = binary_value(val);
    int32_t w = text_w(TY_LB, on ? "ON" : "OFF") + 12;
    uint16_t fill = on ? T_THEME : T_SURF;
    cv_rrect(xc - w / 2, y, w, h, 6, on ? T_THEME : T_LINE, bg);
    if (!on) cv_rrect(xc - w / 2 + 1, y + 1, w - 2, h - 2, 5, fill, T_LINE);
    cv_text_c(xc, ty_y(y + h / 2, TY_LB), TY_LB, on ? "ON" : "OFF", on ? T_INK : T_MID, fill);
}
static uint8_t stage_drop_rows;                         /* Stage's knobs dropping in: the cards' bottom rows only, 0 all */
static void col_old_value(uint32_t c, char *ov)          /* the value card c drew before (in its cache key), 16 bytes */
{
    const char *k = ui.col[c];
    uint32_t i = 0;
    while (*k && *k != '|')
        k++;
    while (*k && k[1] && k[1] != '|' && i + 1u < 16u)
        ov[i++] = *++k;
    ov[i] = 0;
}
enum { CS_CARD, CS_RING, CS_CAPTURE };               /* how draw_column draws a knob: CARD, RING: the strip's cell (the
                                                         * large type); CAPTURE: kept for a list's rows, ui_list.c */
static void list_capture(uint32_t c, const char *label, const char *val, const char *unit, uint16_t vc);
static uint8_t col_style;
static int32_t card_y = Y_LABEL;                        /* the strip's row (sections' tabs: lower) */
static uint16_t card_tc[4];                             /* a cell's label colour (0: the track's; the GLO layer: each's) */
static void pv_words(const char *label, const char *val, const char *unit, char *lb, char *v, char *u);   /* ui_pages.c */
static void pv_value_c(uint32_t c, int32_t xc, int32_t y, const char *val, const char *unit, uint16_t vc, uint16_t bg, int32_t room);
/* a label as its cell shows it: in full when it fits (55 px), else its short form */
static const struct { const char *k, *s; } CELL_SHORT[] = {
    {"PRIORITY", "PRIO"}, {"VEL AMP", "V.AMP"}, {"VEL FLT", "V.FLT"}, {"RELEASE", "REL"}, {"ENV AMT", "ENV"},
    {"VINTAGE", "VINT"}, {"PULSE A", "PLS A"}, {"PULSE B", "PLS B"}, {"WIDTH A", "WID A"}, {"WIDTH B", "WID B"},
    {"INITIAL", "INIT"}, {"SQUARE", "SQR"}, {"VOICES", "VOICE"}, {"BUDGET", "BUDG"}, {"RETURN", "RET"},
    {"DETUNE", "DTUNE"}, {"CUTOFF", "CUT"}, {"UNISON", "UNI"}, {"FREQ A", "FRQ A"}, {"FREQ B", "FRQ B"},
    {"AT LFO", "ATLFO"}, {"AT FLT", "ATFLT"}, {"FILTER", "FILT"}, {"FINE B", "FINEB"}, {"GLMOD", "GLMD"},
    {"SECTION", "SECT"}, {"LENGTH", "LEN"}, {"REPEAT", "REP"}, {"JITTER", "JITT"}, {"VIBRATO", "VIB"},
    {"TEMPO", "BPM"}, {"SNAPPY", "SNAP"}, {"SEND 1", "FX 1"}, {"SEND 2", "FX 2"}, {"SEND 3", "FX 3"}};
static const char *cell_label(const char *label, int32_t room)
{
    uint32_t i;
    if (text_w(TY_LB, label) <= room)
        return label;
    for (i = 0; i < NELEM(CELL_SHORT); i++)
        if (str_eq(label, CELL_SHORT[i].k))
            return CELL_SHORT[i].s;
    return label;
}
static void draw_column(uint32_t c, const char *label0, const char *val0, const char *unit0, uint16_t vc,
                        int32_t ratio, uint32_t icon)
{
    char key[48], label[12], val[16], unit[8];
    int hot = c == ui.hot_col && ui.hot_t, named = fmt_named, strip, snap;
    uint8_t sig;
    uint16_t lc = vc == T_DIM ? T_DIM : card_tc[c & 3u] ? card_tc[c & 3u] : T_KNOB(c), cbg = T_BG;   /* (knob c's colour) */
    int32_t x, uw, room = COL_W;                        /* (the 3 px between cells keep values apart) */
    const aafont_t *vf = TY_TX, *lf = TY_LB;
    uint32_t n, kn;
    int32_t kid;
    if (col_style == CS_CAPTURE) {                      /* a list's row (ui_list.c) */
        list_capture(c, label0, val0, unit0, vc);
        fmt_named = 0;
        return;
    }
    fmt_named = 0;                                      /* (params.c: a name, for this card only) */
    pv_words(label0, val0, unit0, label, val, unit);    /* (the words as the pages write them: "Saw", "ms") */
    if (str_eq(unit0, label0))
        unit[0] = 0;                                    /* "BPM 124 BPM", "USB OFF USB": the label says it */
    kid = kc_tag(val, &kn);
    if (icon == ICON_AUTO)
        icon = icon_for_label(label);
    str_cpy(key, label, 12);                            /* cache key: texts + colour + gauge */
    str_cpy(key + str_len(key), "|", 2);
    str_cpy(key + str_len(key), val, 14);
    str_cpy(key + str_len(key), "|", 2);
    str_cpy(key + str_len(key), unit, 8);
    n = str_len(key);
    /* every RGB565 bit: status colours can change with identical text */
    key[n] = (char)('A' + (vc & 15u));
    key[n + 1] = (char)('A' + ((vc >> 4) & 15u));
    key[n + 2] = (char)('A' + ((vc >> 8) & 15u));
    key[n + 3] = (char)('A' + (vc >> 12));
    key[n + 4] = (char)(' ' + (ratio < 0 ? 0 : 1 + ratio / 20));
    key[n + 5] = (char)(icon == ICON_NONE ? '~' : '!' + icon % 90u);
    key[n + 6] = (char)('0' + hot);
    key[n + 7] = 0;
    uw = unit[0] ? text_w(lf, unit) + 2 : 0;
    if (uw && text_w(vf, val) + uw > room)              /* ("1.0 ms"): the value keeps its size, the unit goes */
        unit[0] = 0, uw = 0;
    if (text_w(vf, val) > room)                         /* ("HARM MIN"): the label face before it is cut */
        vf = lf;
    sig = (uint8_t)str_hash(str_hash(song.sel + TSEL->eng_req * 4u + ux.gen * 64u, label), unit);
    snap = ui.force || sig != ui.roll[c].sig;           /* what the value is of: label, unit, track, engine, palette */
    strip = !snap && str_eq(key, ui.col[c]);
    if (strip && !ui.roll[c].from[0])
        return;
    roll_bg = cbg;
    if (hot && vc != T_DIM)                             /* the knob turning: its value in the track's colour */
        vc = lc;
    if (strip) {                                        /* rolling: the value strip only */
        cv_begin(COL_W, ROLL_H, cbg);
        cv_oy = -ROLL_Y;
    } else {
        char ov[16];                                    /* the value drawn before (in the cache key) */
        col_old_value(c, ov);
        ui.roll[c].sig = sig;
        if (!str_eq(ov, val))
            roll_note(c, ov, val, snap || named || vf != TY_TX || kid >= 0);
        else if (snap)
            ui.roll[c].from[0] = 0;
        str_cpy(ui.col[c], key, sizeof ui.col[c]);
        cv_begin(COL_W, COL_H, T_BG);
        if (hot)                                        /* (and a bar under it) */
            cv_rrect(COL_W / 2 - 14, COL_H - 4, 28, 3, 1, lc, T_BG);
    }
    if (label[0] || val[0]) {
        /* the label: capitals in the track's colour, centred, rows 3..21; the value under it (the text face, rolling),
         * the unit (label face, mid) on its baseline, both centred; the gauge: 3 px at row 44 */
        int32_t vw = text_w(vf, val) + uw, vx = COL_W / 2 - (vw < room ? vw : room) / 2;
        (void)icon;                                     /* (the design draws no card icons) */
        if (!strip && label[0]) {
            const char *ls = cell_label(label, COL_W - 2);
            cv_text_fit(COL_W / 2 - (text_w(lf, ls) < COL_W - 2 ? text_w(lf, ls) : COL_W - 2) / 2, 3, lf, ls, lc,
                        cbg, COL_W - 2);
        }
        if (kid >= 0)                                   /* "[OCT+]": the keycap (accent: it would act; DIM: it would not) */
            x = cv_keycap(COL_W / 2 - kc_w((uint32_t)kid) / 2, 21, (uint32_t)kid,
                          vc == T_ACCENT || vc == T_DIM ? vc : T_KEY, T_INK, cbg);
        else if (binary_value(val) >= 0) {
            binary_badge(COL_W / 2, 21, 22, val, cbg);
            x = COL_W - 6;
        } else if (vf == TY_TX)                         /* the value rolls */
            x = roll_text(c, vx, ROLL_Y, val, vc);
        else if (val[0] >= 'A' && val[0] <= 'Z' && val[1] >= 'a' && val[1] <= 'z')   /* a name ("Chorus"): free text */
            x = cv_free_text(vx, ROLL_Y + TY_TX->asc - vf->asc, vf, val, vc, cbg, room - uw);
        else
            x = cv_text_fit(vx, ROLL_Y + TY_TX->asc - vf->asc, vf, val, vc, cbg, room - uw);
        if (unit[0])                                    /* the unit on the value's baseline */
            cv_text_on(x + 2, ROLL_Y + TY_TX->asc - lf->asc, lf, unit, T_MID, cbg);
    }
    cv_oy = 0;
    if (stage_drop_rows && !strip)                      /* (rows r0 .. under the header: Y_LABEL - r0 wraps, + r0 back) */
        cv_blit_from((uint32_t)CARD_X(c), (uint32_t)card_y - (uint32_t)(COL_H - stage_drop_rows), COL_H - stage_drop_rows);
    else
        cv_blit((uint32_t)CARD_X(c), (uint32_t)(card_y + (strip ? ROLL_Y : 0)));
}

/* an action's column (act_cols): picked, the OCT+ keycap (accent while it would do something); else "--" */
static void draw_act_column(uint32_t c, const char *label, uint16_t vc, uint32_t icon)   /* icon: ICON_AUTO = by label */
{
    int picked = act_col() == c + 1u;
    draw_column(c, label, picked ? "[OCT+]" : "--", "", picked ? (act_ready() ? T_ACCENT : T_DIM) : vc, -1, icon);
}

/* action pages: the footer's first row says what OCT+ and OCT- do ("OCT+ LOAD   OCT- BACK") */
static void foot_hint(char *a, char *b)
{
    uint32_t c = act_col();
    str_cpy(a, "OCT+ ", 8);
    str_cpy(a + 5, c ? act_name(c - 1u) : "--", 8);
    str_cpy(b, ui.act ? "OCT- CANCEL" : "OCT- BACK", 16);
}

/* the sound's name on the track: a user preset or the engine's preset (b holds 16) */
static void sound_name(const track_t *t, char *b)
{
    const engine_t *e = ENGINES[t->eng_req % NENGINES];
    b[0] = 0;
    if(t->eng_req==ENGI_PROPHET)p5_short_name(t,b,16);
    else if(t->user_native && user_of(t)<USER_NONE)native_name(t->eng_req,user_of(t),b);
    else if (user_of(t) < UP_SLOTS)
        up_name(user_of(t), b);
    else if (e->npresets)
        str_cpy(b, eng_preset(e, t->preset % e->npresets)->name, 16);
}

#if MELODEE_UART
#define MIDI_TRS_STATE "ON"                               /* (the input is on; no cable detection) */
#else
#define MIDI_TRS_STATE "OFF"
#endif
/* SYSTEM's MIDI column: input trs (0 USB, 1 TRS) received something in the last 250 ms (both inputs always play;
 * KNOB 1 only picks the one shown). Sampled whenever the column draws */
static int midi_rx_recent(uint32_t trs)
{
    static uint32_t last[2], at[2];
    static uint8_t seen[2];
#if MELODEE_UART
    uint32_t n = trs ? um.bytes : usb.rx_pkts;
#else
    uint32_t n = trs ? 0u : usb.rx_pkts;
#endif
    if (n != last[trs & 1u]) {
        last[trs & 1u] = n;
        at[trs & 1u] = fm1_ms;
        seen[trs & 1u] = 1;
    }
    return seen[trs & 1u] && fm1_ms - at[trs & 1u] < 250u;
}

/* the foot (large type, one row of 28 px under the old pages: NOTES on a drum track, DRUM HIT, CZ TOOLS, the
 * actions): what OCT+ / OCT- do (keycaps and their words); recording notes: EDIT held erases; else the sound (its
 * track's numeral and name, as pv_sound_bar) and, on a grid, its page ("1/2"; ACCENT held: "Accent") */
static void draw_foot(void)
{
    char pn[16], b[16];
    const track_t *t = TSEL;
    uint32_t sig;
    const page_t *pg = cur_page();
    const engine_t *e = ENGINES[TSEL->eng_req % NENGINES];
    const char *ename = e->name;
    int roll = !ui.home && pg->graph == GR_ROLL && !grid_on();
    sound_name(t, pn);
    if (browse_pending()) {                            /* browsing: the sound the list shows, not yet loaded */
        uint32_t k, src = browse_shown(&k);
        char tag[6];
        entry_label(src, k, tag, pn);
        e = ENGINES[src_engine(src, k)];
        ename = e->name;
    }
    b[0] = 0;
    if (grid_on()) {                                   /* (the grid's page) */
        uint32_t len = (uint32_t)t->p[P_SLEN];
        fmt_int(b, (int32_t)ui.bank + 1);
        str_cpy(b + str_len(b), "/", 4);
        fmt_int(b + str_len(b), (int32_t)((len + 15u) / 16u));
        if (black_held(GK_ACC))
            str_cpy(b, "Accent", sizeof b);
    }
    sig = str_hash(str_hash(str_hash(0x9E3779B9u + song.sel * 7u, ename), pn), b) + ux.gen * 977u + ux.pal * 31u;
    if (act_cols()) {                                  /* the hint, and whether OCT+ would act */
        char ha[16], hb[16];
        foot_hint(ha, hb);
        sig += str_hash(str_hash(act_ready() ? 7u : 3u, ha), hb);
    }
    if (roll)
        sig += (uint32_t)seq_erase_active(t) * 8191u + (uint32_t)live_rec_sel() * 113u + ui.note_pick * 40503u +
               (uint32_t)(notes_selected(t) < RECORD_MAX) * 937u + bulk.on * 521u + ui.note_move * 197u + ui.note_copy * 317u;
    if (!ui.force && sig == ui.foot_sig)
        return;
    ui.foot_sig = sig;
    cv_begin(240, H_FOOT, T_BG);
    if (act_cols()) {                                  /* what OCT+ / OCT- do: keycaps and their words */
        char ha[16], hb[16];
        khint_t kh[2];
        foot_hint(ha, hb);                             /* ("OCT+ LOAD", "OCT- BACK") */
        kh[0] = (khint_t){KC_OCTUP, ha + 5};
        kh[1] = (khint_t){KC_OCTDN, hb + 5};
        cv_key_row(8, 232, (H_FOOT - KC_PH) / 2, kh, 2, act_ready() ? 3u : 2u, T_BG);
    } else if (roll && live_rec_sel()) {
        cv_key_hint(8, (H_FOOT - KC_PH) / 2, KC_EDIT, seq_erase_active(t) ? "RELEASE TO STOP" : "HOLD TO ERASE", 1, T_BG);
    } else if (roll) {
        uint32_t start = notes_manual_start(t);
        int selected = notes_selected(t) < RECORD_MAX || (start < NSTEP && t->step[start].n);
        int moving = bulk.on || ui.note_move || ui.note_copy;
        khint_t hints[2] = {{selected || moving ? KC_OCTUP : KC_KEYS, selected || moving ? "NOTE MENU" : "ADD"},
                            {moving ? KC_OCTDN : KC_EDIT, moving ? "DONE" : "DELETE"}};
        cv_key_row(8, 232, (H_FOOT - KC_PH) / 2, hints, 2, 3u, T_BG);
    } else {
        int32_t bw = b[0] ? text_w(TY_TX, b) + 12 : 0;
        char d[2] = {(char)('1' + song.sel % NTRK), 0};
        cv_rrect(4, H_FOOT / 2 - 11, 22, 22, 6, T_THEME, T_BG);   /* (as pv_sound_bar, the page at the right) */
        cv_text_c(15, ty_y(H_FOOT / 2, TY_LB), TY_LB, d, T_INK, T_THEME);
        cv_free_text(34, ty_y(H_FOOT / 2, TY_TX), TY_TX, pn[0] ? pn : ename, T_SEC, T_BG, 236 - bw - 34);
        if (bw)
            cv_text_r(236, ty_y(H_FOOT / 2, TY_TX), TY_TX, b, black_held(GK_ACC) ? T_ACCENT : T_THEME, T_BG);
    }
    cv_blit(0, Y_FOOT);
}
#include "ui_sections.c"                                /* the EDIT pages in sections, the map */
#include "ui_stage.c"                                   /* Stage (HOME) */
#include "ui_view.c"                                    /* HOME's second page: the chord, pictures of what plays */
#include "ui_browser.c"                                 /* SAVE > PRESETS, the sound browser */
#include "ui_patterns.c"                                /* SEQ > PATTERNS, SEQ > SONG */
static int own_screen(void);
static void draw_columns(void)
{
    if (own_screen() && col_style != CS_CAPTURE)         /* own screens have no cards; lists still need their values */
        return;
    if(cur_page()->scope==SC_DRUM){
        char v[12];uint32_t sound=ui.drum_sound%drum_sound_count(TSEL);const int8_t *c=drum_patch[song.sel].c[sound];
        draw_column(0,"DRUM",drum_sound_name(TSEL,sound),"",VAL(0u),-1,ICON_AUTO);
        if(cur_page()->id[1]==4){fmt_int(v,127-c[3]);draw_column(1,"LEVEL",v,"",VAL(1u),-1,ICON_AUTO);draw_column(2,"","","",T_THEME,-1,ICON_NONE);draw_column(3,"","","",T_THEME,-1,ICON_NONE);return;}
        fmt_int(v,c[0]);draw_column(1,"TUNE",v,"ST",VAL(1u),-1,ICON_PITCH);
        fmt_int(v,c[1]+64);draw_column(2,"DECAY",v,"",VAL(2u),-1,ICON_GATE);
        fmt_int(v,c[2]+64);draw_column(3,(drum_is909(TSEL)?drum_character(sound):drum_issid(TSEL)?"SNAP":sound==D9_SD?"SNAPPY":"TONE"),v,"",VAL(3u),-1,ICON_AUTO);return;
    }
    if(cur_page()->scope==SC_DRUMHIT){
        char v[12];uint32_t i=drum_hit_selected();const recorded_note_t *r=i<RECORD_MAX?&recording[i]:0;
        fmt_int(v,ui.cursor+1);draw_column(0,"STEP",v,"",VAL(0u),-1,ICON_AUTO);
        draw_column(1,"DRUM",drum_sound_name(TSEL,ui.drum_sound%drum_sound_count(TSEL)),"",VAL(1u),-1,ICON_AUTO);
        fmt_int(v,r?r->pitch:0);draw_column(2,"PITCH",v,"ST",VAL(2u),-1,ICON_PITCH);
        if(r && r->length){uint32_t x=((uint32_t)r->duration*(1u<<(r->owner>>5))*100u+RECORD_UNIT/2)/RECORD_UNIT;fmt_int(v,x/100);str_cpy(v+str_len(v),".",2);if(x%100<10)str_cpy(v+str_len(v),"0",2);fmt_int(v+str_len(v),x%100);}
        else str_cpy(v,"FREE",sizeof v);
        draw_column(3,"LENGTH",v,"",VAL(3u),-1,ICON_GATE);return;
    }

    uint32_t c;
    char val[12];
    const char *unit;
    if (ui.home)                                        /* Stage: its knobs drop in when turned (ui_stage.c stage_knobs) */
        return;
    if (cur_page()->scope == SC_TRK) {                 /* LEVEL PAN MUTE of the selected track */
        const track_t *t = TSEL;
        uint32_t lvl = trk_level(song.sel);
        param_format(&TP[P_LEVEL], (int32_t)lvl, val, &unit);   /* (0: OFF) */
        draw_column(0, "LEVEL", val, unit, lvl && !t->p[P_MUTE] ? VAL(0u) : T_DIM, (int32_t)lvl * 1000 / 127, ICON_AUTO);
        param_format(&TP[P_PAN], t->p[P_PAN], val, &unit);
        draw_column(1, "PAN", val, unit, VAL(1u), RATIO(&TP[P_PAN], t->p[P_PAN]), param_icon(&TP[P_PAN], t->p[P_PAN]));
        draw_column(2, "", "", "", T_DIM, -1, ICON_NONE);
        draw_column(3, "MUTE", t->p[P_MUTE] ? "ON" : "OFF", "", t->p[P_MUTE] ? T_ACCENT : VAL(3u), -1, ICON_AUTO);
        return;
    }
#if MELODEE_SLICE
    if (cur_page()->graph == GR_SLICES) {                /* SLICE POS, then SPLIT JOIN (ui_slice.c) */
        uint32_t n = slice_count(), j = slice_sel(), src, div, ok = slice_src(&src, &div) && src;
        char u[8];
        if (j < n) {
            fmt_int(val, (int32_t)j + 1);
            str_cpy(u, "/", 8);
            fmt_int(u + 1, (int32_t)n);
        } else {
            str_cpy(val, n ? "END" : "--", sizeof val);
            u[0] = 0;
        }
        draw_column(0, "SLICE", val, u, VAL(0u), -1, ICON_SLICE);
        slice_time(val, slice_mark(j));
        draw_column(1, "POS", val, "S", ok ? VAL(1u) : T_DIM, -1, ICON_AUTO);
        draw_act_column(2, "SPLIT", ok ? T_THEME : T_DIM, ICON_AUTO);
        draw_act_column(3, "JOIN", ok ? T_THEME : T_DIM, ICON_AUTO);
        return;
    }
#endif
    if (cur_page()->graph == GR_MOD) {                   /* the route's SRC, the route, its DST AMT */
        const track_t *t = TSEL;
        uint32_t id = P_M1SRC + 3u * mod_ui_slot;
        int32_t s = t->p[id], d = t->p[id + 1u], a = t->p[id + 2u];
        list_words(val, N_MSRC[clamp(s, 0, MS_N - 1)], sizeof val);   /* (the chips: words, no labels) */
        draw_column(0, "", val, "", s ? VAL(0u) : T_DIM, -1, mod_src_icon(s));
        fmt_int(val, (int32_t)mod_ui_slot + 1);
        draw_column(1, "SLOT", val, "/4", VAL(1u), (int32_t)mod_ui_slot * 1000 / 3, mod_src_icon(MS_OFF));   /* (the mod icon) */
        list_words(val, mod_dst_name(t, d), sizeof val);
        draw_column(2, "", val, "", d ? VAL(2u) : T_DIM, -1, mod_dst_icon(t, d));
        param_format(&TP[id + 2u], a, val, &unit);
        draw_column(3, "", val, unit[0] ? " %" : "", a ? VAL(3u) : T_DIM, RATIO(&TP[id + 2u], a), mod_src_icon(MS_OFF));
        return;
    }
    if (cur_page()->graph == GR_ROLL && !grid_on()) {
        uint32_t chosen = notes_selected(TSEL), start = notes_manual_start(TSEL);
        const step_t *st = start < NSTEP ? &TSEL->step[start] : 0;
        int selected = chosen < RECORD_MAX || (st && st->n);
        char sn[12], u[12];
        fmt_int(sn, (int32_t)ui.cursor + 1); u[0] = '/'; fmt_int(u + 1, TSEL->p[P_SLEN]);
        draw_column(0, ui.note_copy?"COPY":bulk.on||ui.note_move?"MOVE":"STEP", sn, bulk.on||ui.note_move?"":u, VAL(0u), -1, ICON_AUTO);
        uint32_t pitch = chosen < RECORD_MAX ? recording[chosen].note : st && st->n ? st->note[notes_manual_slot(st)] : last_note;
        note_name(sn, pitch);
        if(chosen<RECORD_MAX && drum_track(TSEL))fmt_int(sn,recording[chosen].pitch);
        draw_column(1, "PITCH", sn, chosen<RECORD_MAX && drum_track(TSEL)?"ST":"", selected ? VAL(1u) : T_DIM, -1, ICON_PITCH);
        if (chosen < RECORD_MAX) {
            uint32_t hundredths = ((uint32_t)recording[chosen].duration * (1u << (recording[chosen].owner >> 5)) * 100u + RECORD_UNIT / 2u) / RECORD_UNIT;
            if (!hundredths) str_cpy(sn, "<.01", sizeof sn);
            else {
                fmt_int(sn, (int32_t)(hundredths / 100u));
                if (hundredths % 100u) {
                    str_cpy(sn + str_len(sn), ".", 2);
                    uint32_t fraction = hundredths % 100u;
                    if (fraction < 10u) str_cpy(sn + str_len(sn), "0", 2);
                    fmt_int(sn + str_len(sn), (int32_t)fraction);
                }
            }
        } else if (selected) fmt_int(sn, (int32_t)step_note_length(TSEL, start));
        else str_cpy(sn, "--", sizeof sn);
        draw_column(2, "LENGTH", sn, chosen >= RECORD_MAX && st && st->n > 1u ? "ALL" : "", selected ? VAL(2u) : T_DIM, -1, ICON_GATE);
        if (chosen >= RECORD_MAX && st && (ui.step_mods & (1u << panel.btn[B_ENV]))) {
            draw_column(3, "SLIDE", st->flags & SF_SLIDE ? "ON" : "OFF", "", VAL(3u), -1, ICON_SLIDE);
            return;
        }
        if (selected) fmt_int(sn, chosen < RECORD_MAX ? recording[chosen].vel : st->flags & SF_ACCENT ? 127 : st->vel ? st->vel : 96);
        else str_cpy(sn, "--", sizeof sn);
        draw_column(3, "VEL", sn, chosen >= RECORD_MAX && st && st->n > 1u ? "ALL" : "", selected ? VAL(3u) : T_DIM, -1, ICON_ACCENT);
        return;
    }
    if (cur_page()->scope == SC_STEP && drum_track(TSEL)) {   /* the grid: STEP LANE HIT ACC */
        const step_t *st = &seq_steps(TSEL)[ui.cursor];
        uint32_t b = 1u << ui.lane, on = (step_lanes(st) & b) != 0u, ac = (step_accents(st) & b) != 0u;
        char sn[8], sl[8];
        fmt_int(sn, (int32_t)ui.cursor + 1);
        str_cpy(sl, "/", 8);
        fmt_int(sl + 1, TSEL->p[P_SLEN]);
        draw_column(0, "STEP", sn, sl, VAL(0u), -1, ICON_AUTO);
        draw_column(1, "", drum_lane_name(TSEL, ui.lane), "", VAL(1u), -1, ICON_AUTO);   /* (its chip: "Snare") */
        draw_column(2, "HIT", on ? "ON" : "--", "", on ? VAL(2u) : T_DIM, -1, ICON_AUTO);
        draw_column(3, "ACC", ac ? "ON" : "--", "", ac ? VAL(3u) : T_DIM, -1, ICON_AUTO);
        return;
    }
    if (cur_page()->scope == SC_STEP) {
        static const char *const TIME_N[3] = {"NOTE", "TIE", "REST"};
        const step_t *st = &seq_steps(TSEL)[ui.cursor];
        char u[8];
        uint32_t cnt = st->n, first = st->note[0], l;
        for (l = NLANE; l-- > 0;)                         /* (lane hits count as their notes) */
            if ((st->hit >> l) & 1u) {
                cnt++;
                if (!st->n)
                    first = DRUM_LANE_NOTE[l];
            }
        if (cnt) {
            note_name(val, first);
            u[0] = 0;
            if (cnt > 1) {
                str_cpy(u, "+", 8);
                fmt_int(u + 1, (int32_t)cnt - 1);
            }
        } else {
            str_cpy(val, "--", 12);
            u[0] = 0;
        }
        {
            static const char *const FLAG_N[4] = {"-", "ACC", "SLD", "A+S"};
            char sn[8], sl[8];
            fmt_int(sn, (int32_t)ui.cursor + 1);
            str_cpy(sl, "/", 8);
            fmt_int(sl + 1, TSEL->p[P_SLEN]);
            draw_column(0, "STEP", sn, sl, VAL(0u), -1, ICON_AUTO);
            draw_column(1, "NOTE", val, u, step_on(st) ? VAL(1u) : T_DIM, -1, ICON_AUTO);
            draw_column(2, "TIME", TIME_N[st->time % 3u], "", VAL(2u), -1, ICON_AUTO);
            draw_column(3, "FLAG", FLAG_N[(st->flags & SF_ACCENT ? 1u : 0u) | (st->flags & SF_SLIDE ? 2u : 0u)], "",
                        VAL(3u), -1, ICON_AUTO);
        }
        return;
    }
    for (c = 0; c < 4u; c++) {
        int16_t *vp;
        if (scale_settings_page(cur_page()) && c == 1u) {
            draw_column(c, "FAV", scale_favorite((uint32_t)TSEL->p[P_SCALE]) ? "ON" : "OFF", "", VAL(c), -1, ICON_X_STAR);
            continue;
        }
        if (cur_page()->graph == GR_FXRACK && c == 1u) {   /* a rack's KNOB 2: the row ("2", "Shared"; SHARED: "B") */
            uint32_t sh = cur_page()->scope == SC_GLOBAL, r = fx_rack_row[sh] & 3u;
            val[0] = (char)((sh ? 'A' : '1') + r);
            val[1] = 0;
            draw_column(c, "SLOT", r < FX_SLOTS ? val : sh ? "Return" : "Shared", "", VAL(c), -1, ICON_AUTO);
            continue;
        }
        const param_desc_t *d = page_desc(cur_page(), c, &vp);
        if (!d || !d->label || d->label[0] == '-') {
            draw_column(c, "", "", "", T_THEME, -1, ICON_AUTO);
            continue;
        }
        if (cur_page()->id[c] == G_MIDI && cur_page()->scope == SC_GLOBAL) {   /* KNOB 1: which input's status */
            uint32_t trs = song.g[G_MIDI] != 0, rx = midi_rx_recent(trs);
            str_cpy(val, trs ? "TRS" : "USB", 12);
            unit = rx ? "RX" : trs ? MIDI_TRS_STATE :
                   !usb.up ? "OFF" : usb.config ? "ON" : usb.setups ? "ENUM" : usb.sof_seen ? "BUS" : "WAIT";
            draw_column(c, "MIDI", val, unit, rx ? T_ACCENT : T_THEME, -1, ICON_AUTO);
            continue;
        }
        if ((act_cols() >> c) & 1u) {
            draw_act_column(c, d->label, T_THEME, ICON_AUTO);
            continue;
        }
        if (cur_page()->scope == SC_GLOBAL && ui.proj_new && (cur_page()->id[c] == G_SLOT || cur_page()->id[c] == G_SAVE)) {
            draw_column(c, d->label, cur_page()->id[c] == G_SLOT ? "NEW" : "--", "",   /* SLOT past TMPL: NEW (no SAVE) */
                        cur_page()->id[c] == G_SLOT ? VAL(c) : T_DIM, -1, ICON_AUTO);
            continue;
        }
        if (cur_page()->id[c] == G_INFO && cur_page()->scope == SC_GLOBAL) {
            fmt_int(val, (int32_t)(song.cpu_q8 * 100u / 256u));
            unit = "%";
        } else {
            param_format(d, *vp, val, &unit);
            /* Rows have room for enum names that the compact parameter formatter abbreviates. */
            if (d->fmt == F_ENUM && (col_style == CS_CAPTURE ||
                (d->names == N_FX && (*vp == FX_REVERB || *vp == FX_CHORUS))))
                str_cpy(val, d->names[clamp(*vp, d->min, d->max)], sizeof val);
        }
        draw_column(c, d->label, val, unit, (cur_page()->scope == SC_CZ1 &&   /* CZ-1: no effect on the tone as it is */
                    !cz_ed_active(cz_patch[song.sel % NTRK].raw, cur_page()->id[c])) ||
                    (cur_page()->scope == SC_P5 && !p5_ed_active(TSEL, cur_page()->id[c])) ||
                    (vp == &TSEL->p[P_LRATE] && TSEL->p[P_LSYNC]) ? T_DIM : VAL(c),   /* LFO 2 SYNC on: RATE does nothing */
                    d->fmt == F_ENUM && d->max < 2 ? -1 : RATIO(d, *vp), param_icon(d, *vp));
    }
}

/* UPDATE MODE countdown (main.c: OCT- + OCT+ held): over everything, the menu and the dialogs too */
static void draw_uboot(void)
{
    static uint8_t shown;
    char d[4] = {(char)('0' + ui.uboot % 10u), 0, 0, 0};
    if (!ui.force && shown == ui.uboot)
        return;
    shown = ui.uboot;
    lcd_fill(0, H_HEAD, 240, 240 - H_HEAD, T_BG);
    ui.head_sig = ~0u;
    draw_text_box(0, 50, 240, TY_TX, "Update mode in", T_TEXT, 1);   /* (large type: the count large, the keys held,
                                                                     * what letting go does) */
    draw_text_box(0, 80, 240, TY_HR, d, T_THEME, 1);
    {
        int32_t w = kc_w(KC_OCTDN) + 6 + kc_w(KC_OCTUP), x = (240 - w) / 2;
        cv_begin(240, KC_PH + 2, T_BG);
        x = cv_keycap(x, 1, KC_OCTDN, T_KEY, T_INK, T_BG) + 6;
        cv_keycap(x, 1, KC_OCTUP, T_KEY, T_INK, T_BG);
        cv_blit(0, 150);
        lcd_sync();
    }
    draw_text_box(0, 184, 240, TY_TX, "Let go to cancel", T_MID, 1);
    ui.force = 0;
    scr_shown();
}

/* the OCT- / OCT+ dialog: what it does (ui.confirm, ui.confirm_trk) on two lines, on a surface */
#define DLG_X 8                                         /* (large type: 224 wide, as tall as its words, centred) */
#define DLG_Y 36
#define DLG_W 224
#define DLG_H 168
static void confirm_text(char *a, char *b)
{
    uint32_t k = ui.confirm_trk;
    b[0] = 0;
    switch (ui.confirm) {
    case CF_CLEAR_TRK:
        str_cpy(a, "CLEAR TRACK 1?", 24);
        a[12] = (char)('1' + k % NTRK);
        break;
    case CF_OVR_PROJ:
        str_cpy(a, "OVERWRITE PROJECT A?", 24);
        a[18] = (char)('A' + (k & 3u));
        if (!project_name(k & 3u, b) || !b[0])          /* the project's name, else what SONG plays from it */
            str_cpy(b, "Its song and patterns change", 32);
        break;
    case CF_ERASE_PROJ:
        str_cpy(a, "ERASE PROJECT A?", 24);
        a[14] = (char)('A' + (k & 3u));
        if (!project_name(k & 3u, b) || !b[0])
            str_cpy(b, "Its song and patterns go", 32);
        break;
    case CF_DEL_ROW:
        str_cpy(a, "DELETE SONG ROW?", 24);
        break;
    case CF_CLEAR_SONG:
        str_cpy(a, "CLEAR SONG ORDER?", 24);
        break;
    case CF_NEW_SONG:                                   /* what is lost */
        str_cpy(a, "START A NEW SONG?", 24);
        str_cpy(b, "Unsaved changes", 24);
        break;
    case CF_TAKE_JAM:                                   /* the rows it replaces */
        str_cpy(a, "SONG FROM JAM?", 24);
        fmt_int(b, (int32_t)chain_config.count);
        str_cpy(b + str_len(b), " rows replaced", 16);
        break;
    case CF_INIT_SOUND:
        str_cpy(a, "INITIALIZE SOUND?", 24);
        break;
    case CF_DEL_PAT:                                    /* PATTERNS: the track's pattern, its notes and motion */
        str_cpy(a, "DELETE PATTERN 1?", 24);
        a[15] = (char)('1' + trk[k % NTRK].pattern % NPAT);
        str_cpy(b, "Track 1 \xB7 notes and motion", 32);
        b[6] = (char)('1' + k % NTRK);
        break;
    case CF_PASTE_PAT:                                  /* PATTERNS' Copy to over a pattern in use */
        str_cpy(a, "REPLACE PATTERN 1?", 24);
        a[16] = (char)('1' + ui.ptc_dst % NPAT);
        str_cpy(b, "Track 1 \xB7 with pattern 1", 32);
        b[6] = (char)('1' + k % NTRK);
        b[str_len(b) - 1u] = (char)('1' + ui.ptc_src % NPAT);
        break;
    case CF_CLEAR_MOTION:
        str_cpy(a, "CLEAR T1 MOTION?", 24); a[7] = (char)('1' + k % NTRK);
        break;
    case CF_CLEAR_SHARED:                               /* SHARED's sheet */
        str_cpy(a, "CLEAR SHARED FX?", 24);
        str_cpy(b, "All tracks \xB7 three slots off", 32);
        break;
    case CF_OVR_USER:
        if (up_old_bank(k)) { str_cpy(a, "REPLACE OLD BANK?", 24); str_cpy(b, "All 18 old slots replaced", 32); break; }
        str_cpy(a, "OVERWRITE ", 24);
        up_slot_label(a + str_len(a), k);
        str_cpy(a + str_len(a), "?", 2);
        up_name(k, b);                             /* the sound stored there */
        break;
    case CF_DISCARD_DESIGN:
        str_cpy(a, "DISCARD THE DESIGN?", 24);
        str_cpy(b, "The original sound returns", 32);
        break;
    case CF_IMPORT:
        for (uint32_t slot = sx_import.first; slot < sx_import.first + sx_import.count; slot++) {
            if (up_old_bank(slot)) {
                str_cpy(a, "REPLACE OLD BANKS?", 24); str_cpy(b, "All slots in old banks replaced", 32); return;
            }
        }
        str_cpy(a,"OVERWRITE USER SLOTS?",24);
        fmt_int(b,sx_import.count);str_cpy(b+str_len(b)," incoming patches",20);break;
    case CF_BULK_ERASE:
        str_cpy(a,"ERASE SELECTED PRESETS?",24);
        str_cpy(b,"Selected slots will be empty",32);break;
    case CF_ERASE_USER:
        if (up_old_bank(k)) { str_cpy(a, "ERASE OLD BANK?", 24); str_cpy(b, "All 18 old slots erased", 32); break; }
        str_cpy(a, "ERASE ", 24);
        up_slot_label(a + str_len(a), k);
        str_cpy(a + str_len(a), "?", 2);
        up_name(k, b);
        break;
    case CF_OLD_SETTINGS:
        str_cpy(a, "REPLACE OLD SETTINGS?", 24);
        str_cpy(b, "Old settings/template replaced", 32);
        break;
    case CF_OLD_TEMPLATE:
        str_cpy(a, "REPLACE OLD TEMPLATE?", 24);
        str_cpy(b, "Old settings/template replaced", 32);
        break;
    case CF_PAT_KEY:                                    /* SEQ + a white key held: what its keys do now */
        str_cpy(a, "PATTERN 1", 24);
        a[8] = (char)('1' + ui.pat_dlg % NPAT);
        str_cpy(b, "Again: delete \xB7 other: copy", 32);
        break;
    case CF_DEL_TRACK:                                  /* Stage's sheet: the tracks after it move down a place */
        str_cpy(a, "REMOVE TRACK 1?", 24);
        a[13] = (char)('1' + k % NTRK);
        str_cpy(b, "Its sound, patterns and motion", 32);
        break;
    default:
        str_cpy(a, "CLEAR T1 SEQUENCE?", 24);     /* the header (T1..T4) is hidden */
        a[7] = (char)('1' + k % NTRK);
        break;
    }
}
/* the question (docs/design: mock/r5_pages, ref/dialog): a card over the page dimmed (ui_draw: drawn under cv_dim):
 * the warning in a tinted ring, the question in words, its detail, OCT- as "↶ No" and OCT+ as "✓ Yes" (the track's
 * colour) */
static void confirm_words(char *s)                      /* "OVERWRITE PROJECT A?" -> "Overwrite project A?" */
{
    uint32_t i;
    for (i = 1; s[i]; i++) {
        int single = s[i - 1] == ' ' && (s[i + 1] == '?' || !s[i + 1]);   /* (a slot's letter at the end) */
        if (s[i] >= 'A' && s[i] <= 'Z' && !single)
            s[i] = (char)(s[i] + 32);
    }
}
/* the question card: the warning, the question (the text face) and its detail (the label face), each wrapped onto as
 * many lines as it needs (nothing is ever cut), OCT- "No" and OCT+ "Yes" (the track's colour); the card as tall as
 * its words, centred over the page under it (dimmed). SEQ's pattern dialog: smaller, no warning, "Cancel" "Delete" */
static void draw_confirm(void)
{
    char a[24], b[32], q[TL_MAX][32], d[TL_MAX][32];
    const aafont_t *tf = TY_TX, *lf = TY_LB;
    uint32_t nq, nd, i, nc = 0, k, pk = ui.confirm == CF_PAT_KEY;
    const char *no = pk ? "Cancel" : "No", *yes = pk ? "Delete" : "Yes";
    int32_t top, h, y0, sh, cut[2 * TL_MAX + 2], ty = pk ? 14 : 34;   /* (ty: the words' first row) */
    confirm_text(a, b);
    confirm_words(a);
    nq = text_lines(a, q, 3u, DLG_W - 20, tf);
    nd = b[0] ? text_lines(b, d, 3u, DLG_W - 20, lf) : 0u;
    h = ty + (int32_t)nq * 24 + (nd ? 6 + (int32_t)nd * 20 : 0) + 14 + 34 + 12;
    y0 = H_HEAD + (240 - H_HEAD - h) / 2;
    for (i = 0; i < nq; i++)                            /* (where a slice may end: between lines, over the buttons) */
        cut[nc++] = ty + (int32_t)i * 24;
    for (i = 0; i < nd; i++)
        cut[nc++] = ty + (int32_t)nq * 24 + 6 + (int32_t)i * 20;
    cut[nc++] = h - 48;
    cut[nc++] = h;
    for (top = 0; top < h; top += sh) {                 /* (in slices: a canvas holds 132 rows of it) */
        int32_t y = ty;
        for (k = 0, sh = 0; k < nc; k++)
            if (cut[k] > top && cut[k] - top <= (int32_t)(CV_MAX / DLG_W))
                sh = cut[k] - top;
        if (!sh)
            sh = h - top < (int32_t)(CV_MAX / DLG_W) ? h - top : (int32_t)(CV_MAX / DLG_W);
        cv_begin(DLG_W, (uint32_t)sh, T_BG);
        cv_oy = -top;
        cv_rrect(0, 0, DLG_W, h, 12, T_LINE, T_BG);
        cv_rrect(1, 1, DLG_W - 2, h - 2, 11, T_SURF, T_LINE);
        if (!pk)
            cv_icon_mid(DLG_W / 2 - 8, 18, 16, ui.confirm == CF_CLEAR_MOTION ? ICON_X_MOTION_DEL : ICON_X_WARN, T_THEME,
                        T_SURF);
        for (i = 0; i < nq; i++, y += 24)
            cv_text_c(DLG_W / 2, y, tf, q[i], T_TEXT, T_SURF);
        for (i = 0, y += 6; i < nd; i++, y += 20)
            cv_text_c(DLG_W / 2, y, lf, d[i], T_MID, T_SURF);
        {   /* OCT-: no, OCT+: yes (the track's colour) */
            int32_t by = h - 46, cy = by + 17;
            cv_rrect(12, by, 96, 34, 8, T_LINE, T_SURF);
            cv_rrect(13, by + 1, 94, 32, 7, T_PANEL, T_LINE);
            cv_icon_mid(60 - text_w(tf, no) / 2 - 12, cy, 16, ICON_X_UNDO, T_MID, T_PANEL);
            cv_text_on(60 - text_w(tf, no) / 2 + 8, ty_y(cy, tf), tf, no, T_MID, T_PANEL);
            cv_rrect(116, by, 96, 34, 8, T_THEME, T_SURF);
            cv_icon_mid(164 - text_w(tf, yes) / 2 - 12, cy, 16, ICON_X_CHECK, T_INK, T_THEME);
            cv_text_on(164 - text_w(tf, yes) / 2 + 8, ty_y(cy, tf), tf, yes, T_INK, T_THEME);
        }
        cv_oy = 0;
        cv_blit(DLG_X, (uint32_t)(y0 + top));
    }
}

#include "ui_pages.c"                                   /* the redesign's other pages (R5) */
#include "ui_list.c"                                    /* list pages */
#include "ui_popup.c"                                   /* action sheets, pickers */
#include "ui_slots.c"                                   /* PROJECT, USER, the STOREs: their slots */
#include "ui_motion.c"                                  /* MOTION: its lanes */
#include "ui_scales.c"                                  /* SCALES: a screen of its own */
#include "ui_designer.c"                                /* guided native sound creation */
static int own_screen(void)                             /* a page drawn whole by its own code, no cards or footer */
{
    uint32_t g = cur_page()->graph;
    return !ui.home && (g == GR_BROWSE || g == GR_PATGRID || g == GR_SONG || g == GR_SCALE_PICKER);
}
/* the page (or Stage): its own screen, the redesign's, else its cards, panel and footer */
static void draw_page(void)
{
    if (!ui.home && !page_visible(ui.page)) {          /* an OP page (DIGITAL's, retired): EDIT 1; an FX slot
                                                         * emptied (the editor): its rack */
        ui.page = (uint8_t)page_first(cur_page()->fam == FAM_FX ? FAM_FX : FAM_EDIT);
        page_entered();
    }
    cursor_fix();
    if (designer_screen()) {                            /* the sound designer's START, FEATURES (ui_designer.c) */
        designer_draw();
    } else if (!own_screen() && slot_kind()) {          /* a slot page (ui_slots.c) */
        slot_draw();
    } else if (!own_screen() && motion_page()) {        /* MOTION's lanes (ui_motion.c) */
        motion_draw();
    } else if (!own_screen() && list_on()) {            /* a list page (ui_list.c) */
        list_draw();
    } else if (!own_screen() && pv_kind()) {            /* the redesign's other pages (ui_pages.c) */
        pv_draw();
    } else if (own_screen()) {                          /* the browser, PATTERNS, SONG: screens of their own */
        if (cur_page()->graph == GR_BROWSE)
            browser_draw();
        else if (cur_page()->graph == GR_SCALE_PICKER)
            scales_draw();
        else if (cur_page()->graph == GR_SONG)
            song_draw();
        else {
            draw_head();
            patterns_draw();
        }
    } else {
        if (ui.force)
            draw_frame(ui.home);
        melodee_dbg.stage = 3;
        draw_head();
        melodee_dbg.stage = 4;
        draw_columns();
        melodee_dbg.stage = 5;
        if (ui.home && ui.home_pg)                      /* HOME's second page (ui_view.c) */
            view_draw();
        else if (ui.home)                               /* Stage: its panel and the lanes (ui_stage.c), no footer */
            stage_draw();
        else
            draw_graph();
    }
    melodee_dbg.stage = 6;
    if (!ui.home && !own_screen() && !pv_kind() && !list_on() && !slot_kind() && !motion_page())
        draw_foot();
}
/* the toast: a message too long for the header on a card under it, wrapped (nothing cut); when it goes, what it covered
 * is drawn again */
static void draw_toast(void)
{
    char l[3][32];
    const char *m = head_msg();
    uint32_t n, i;
    int32_t h;
    if (!(ui.msg_t || ui.layer) || !head_toast(BPM_X + BPM_W + 6))
        return;
    n = text_lines(m, l, 3u, BOX_W - 20, TY_TX);
    h = 10 + (int32_t)n * 24;
    cv_begin(BOX_W, (uint32_t)h, T_BG);
    cv_rrect(0, 0, BOX_W, h, 8, T_LINE, T_BG);
    cv_rrect(1, 1, BOX_W - 2, h - 2, 7, T_SURF, T_LINE);
    for (i = 0; i < n; i++)
        cv_free_hint(10, 5 + (int32_t)i * 24, l[i], T_TEXT, T_SURF, BOX_W - 20);
    GFX_HOOK_COVER(BOX_X, H_HEAD + 2, BOX_X + BOX_W, H_HEAD + 2 + h);
    cv_blit(BOX_X, H_HEAD + 2);
    toast_shown = 1;
}
static void ui_draw_frame(void);
static void ui_draw(void)                               /* (gfx.c: the fills wait for the frame's canvases) */
{
    gfx_frame(1);
    ui_draw_frame();
    gfx_frame(0);
}
static void ui_draw_frame(void)
{
    if (boot_work.state) {
        scrn.idle = fm1_ms;                          /* maintenance/errors never sleep or get covered */
        if (ui.force) { boot_work_draw(); ui.force = 0; }
        return;
    }
    if (!scr_frame()) return;
    if (ui.waiting_shown != head_waiting()) {
        ui.waiting_shown = (uint8_t)head_waiting();
        ui.force = 1;                                  /* restore custom headers on start/cancel/track change */
    }
    ui.frame++;
    if (palette_track(song.sel))                       /* THEME is the selected track's colour */
        ui.force = 1;
    if(seq_counting()) { char text[24]="COUNT IN ";fmt_int(text+9,(int32_t)cin_left);
        ui_message(text); }
    else if(!memcmp(ui.msg,"COUNT IN ",9))ui.msg_t=0;

    /* Restore a departing toast before drawing the page beneath it. A force
     * requested by draw_toast after the page was drawn is cleared this frame. */
    if (toast_shown && !head_toast(BPM_X + BPM_W + 6)) {
        toast_shown = 0;
        ui.force = 1;
    }

    if (ui.uboot) {
        draw_uboot();
        draw_head();
        return;
    }
    if (ui.menu) {
        draw_menu();
        ui.force = 0;
        return;
    }
    if (ui.confirm) {                                   /* the OCT- / OCT+ dialog */
        if (ui.force) {                                 /* over the page, dimmed (each canvas darkened as it goes) */
            uint8_t cf = ui.confirm;
            ui.confirm = 0;
            cv_dim = cv_under = 1;
            draw_page();
            cv_dim = cv_under = 0;
            ui.confirm = cf;
            ui.force = 1;
            draw_confirm();
            ui.force = 0;
        }
        draw_head();                                    /* recording remains visible over confirmations */
        return;
    }
    if (smap.on && (ui.home || cur_page()->fam != FAM_EDIT || ui.layer))
        smap.on = 0;
    if (smap.on) {                                      /* an engine's map (ui_sections.c) */
        smap_draw();
        ui.force = 0;
        return;
    }
    pick_poll();
    if (pop.on) {                                       /* a popup over the page, dimmed (ui_popup.c); a knob's picker
                                                         * over it as it is (it comes with every turn of a list knob:
                                                         * dimming sent the whole screen twice, ~150 ms on the SPI) */
        if (ui.force) {
            cv_dim = pop.on != POP_PICK;
            cv_under = 1;
            draw_page();
            cv_dim = cv_under = 0;
            ui.force = 1;
        }
        pop_draw();
        ui.force = 0;
        return;
    }
    if (name_on()) {                                    /* NAME: a user preset's or a project's (ui_name.c) */
        name_draw();
        return;
    }
    if (new_on()) {                                     /* NEW SONG (ui_new.c) */
        new_draw();
        return;
    }
    if (ui.layer) {                                     /* a layer's map over the page (ui_layer.c) */
        if (ui.force)
            draw_frame(0);
        draw_head();
        draw_layer();
        draw_toast();
        if (ui.msg_t && !--ui.msg_t && ui.msg2[0]) {
            str_cpy(ui.msg, ui.msg2, sizeof ui.msg);
            ui.msg2[0] = 0;
            ui.msg_t = 60;
        }
        if (ui.hot_t)
            ui.hot_t--;
        if (ui.bpm_t)
            ui.bpm_t--;
        ui.force = 0;
        return;
    }
    draw_page();
    draw_toast();
    if (ui.msg_t && !--ui.msg_t && ui.msg2[0]) {     /* the second message (ui_notices) */
        str_cpy(ui.msg, ui.msg2, sizeof ui.msg);
        ui.msg2[0] = 0;
        ui.msg_t = 60;
    }
    if (ui.bpm_t)
        ui.bpm_t--;
    if (ui.hot_t)
        ui.hot_t--;
    ui.force = 0;
    scr_shown();
}
