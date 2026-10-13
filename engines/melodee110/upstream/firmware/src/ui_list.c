/* SPDX-License-Identifier: GPL-3.0-only */
/* List pages (docs/design README "Pages: lists or knobs", mock/r6_pages SCL): the pages one sets and leaves as a list,
 * a row a setting: KNOB 2 the row, KNOB 1 its value, OCT+ a list value's whole list (SCL's Scale: the SCALES page),
 * OCT- Stage. Only what has more than four settings (Kerem, 2026-10-11: four or fewer are a page's own four knobs;
 * TEMPO, GLOBAL, SYSTEM stay knobs too: BPM and TUNE are turned while playing). A list may gather a few
 * pages (VOICE 1-3, FM6's LFO and controller pages): the
 * others leave the SELECT round (list_gone). The rows' labels and values are the pages' own (draw_column captures
 * them: CS_CAPTURE); a page's picture under its rows (SCL: the octave, the scale lit; CHORD, LFO 2: their graphs).
 * Included by ui_draw.c */
typedef struct { const char *page; uint8_t slot; } lrow_t;
enum { LP_NONE, LP_SCALE, LP_GRAPH };
typedef struct { const char *title; uint8_t pic, n; lrow_t row[16]; } list_t;
static const list_t LISTS[] = {
    {"SCL", LP_SCALE, 6, {{"SCL", 0}, {"SCALES", 1}, {"SCL", 2}, {"MPC", 0}, {"SCL", 3}, {"SCL", 1}}},   /* (MPC: QNT MPC's
                                                                                                    * pad degree) */
    {"VOICE", LP_NONE, 9, {{"VOICE", 0}, {"VOICE", 1}, {"VOICE", 2}, {"VOICE", 3}, {"VOICE 2", 0}, {"VOICE 2", 1},
                           {"VOICE 2", 2}, {"VOICE 2", 3}, {"VOICE 3", 0}}},
    {"FM LFO", LP_NONE, 7, {{"FM LFO", 0}, {"FM LFO", 1}, {"FM LFO", 2}, {"FM LFO", 3}, {"FM LFO 2", 0}, {"FM LFO 2", 1},
                            {"FM LFO 2", 2}}},
    {"FM BEND", LP_NONE, 16, {{"FM BEND", 0}, {"FM BEND", 1}, {"FM BEND", 2}, {"FM BEND", 3}, {"FM PORTA", 0},
                              {"FM PORTA", 1}, {"FM PORTA", 2}, {"FM PORTA", 3}, {"FM WH/FT", 0}, {"FM WH/FT", 1},
                              {"FM WH/FT", 2}, {"FM WH/FT", 3}, {"FM BR/AT", 0}, {"FM BR/AT", 1}, {"FM BR/AT", 2},
                              {"FM BR/AT", 3}}},
};
#define LIST_Y0 22                                      /* the rows: 20 px, 22 apart from y 22 */
#define LIST_ROW 22
static struct { uint8_t row, first, page; uint32_t sig; } lst;
static void vlist_open(uint8_t page, uint8_t slot);     /* ui_popup.c */
static void edit_param(uint32_t slot, int32_t steps);   /* ui_input.c */
static struct { char label[12], val[16], unit[8]; uint16_t vc; uint8_t on; } lcap[4];   /* (CS_CAPTURE) */

static uint32_t page_titled(const char *t)              /* the page of that title, NPAGES none */
{
    uint32_t i;
    for (i = 0; i < NPAGES && !str_eq(PAGES[i].title, t); i++)
        ;
    return i;
}
static const list_t *list_of(uint32_t page)             /* the list page i shows, 0 none */
{
    uint32_t l;
    for (l = 0; l < NELEM(LISTS); l++)
        if (str_eq(PAGES[page].title, LISTS[l].title))
            return &LISTS[l];
    return 0;
}
/* page i gathered into another's list: not in the SELECT round (ui.c page_visible) */
static int list_gone(uint32_t i)
{
    uint32_t l, r;
    for (l = 0; l < NELEM(LISTS); l++)
        for (r = 0; r < LISTS[l].n; r++)
            if (!str_eq(LISTS[l].row[r].page, LISTS[l].title) && str_eq(LISTS[l].row[r].page, PAGES[i].title) &&
                !str_eq(PAGES[i].title, "SCALES"))
                return 1;
    return 0;
}
/* list l's rows shown: all but MPC's degree while QNT is not MPC (the page it was on alone, Kerem 2026-10-11) */
static int list_row_on(const lrow_t *rw) { return !str_eq(rw->page, "MPC") || TSEL->p[P_QUANT] == Q_MPC; }
static uint32_t list_n(const list_t *l)
{
    uint32_t r, n = 0;
    for (r = 0; r < l->n; r++)
        n += (uint32_t)list_row_on(&l->row[r]);
    return n;
}
static const lrow_t *list_at(const list_t *l, uint32_t k)   /* the k-th row shown */
{
    uint32_t r;
    for (r = 0; r < l->n; r++)
        if (list_row_on(&l->row[r]) && !k--)
            return &l->row[r];
    return &l->row[0];
}
static const list_t *list_on(void)                      /* the page shown is a list (not under a layer, the menu ..) */
{
    if (ui.home || ui.menu || ui.layer)
        return 0;
    return list_of(ui.page);
}

/* a knob's short label in full words (a list has the room) */
static const struct { const char *k, *w; } LIST_WORDS[] = {
    {"QNT", "Quantize"}, {"TRN", "Transpose"}, {"FAV", "Favourite"}, {"VCE", "Voice"}, {"GLD", "Glide"},
    {"GLMOD", "Glide mode"}, {"PRIO", "Priority"}, {"ALLOC", "Allocation"}, {"DTUNE", "Detune"}, {"SPRD", "Spread"},
    {"CHRD", "Chord"}, {"VOIC", "Voicing"}, {"SWG", "Swing"}, {"PROB", "Probability"}, {"ORD", "Order"},
    {"POL", "Polarity"}, {"TRIG", "Trigger"}, {"PIT", "Pitch"}, {"FLT", "Filter"}, {"SHP", "Shape"}, {"AMP", "Level"},
    {"DEG", "MPC degree"}, {"PAN", "Pan"}, {"MUTE", "Mute"}, {"WEAR", "Wear"}, {"TYPE", "Type"}, {"SYNC", "Sync"},
    {"HOLD", "Hold"}, {"ROOT", "Root"}, {"SCL", "Scale"}, {"CUT", "Cutoff"}, {"RES", "Resonance"}, {"DRV", "Drive"},
    {"MODW", "Mod wheel"}, {"VEL", "Velocity"}, {"AT", "Aftertouch"}, {"EXPR", "Expression"}, {"RAND", "Random"},
    {"ENV", "Envelope"}, {"KEY", "Key"}, {"SLEW", "Slew"}, {"SHP", "Shape"}, {"CHO", "Chorus"}, {"DLY", "Delay"},
    {"REV", "Reverb"}, {"DIST", "Distortion"}, {"RATE", "Rate"}};
static void list_words(char *d, const char *label, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < NELEM(LIST_WORDS); i++)
        if (str_eq(label, LIST_WORDS[i].k)) {
            str_cpy(d, LIST_WORDS[i].w, n);
            return;
        }
    menu_words(d, label, n);
}
/* row r's label, value and unit as its page draws them (draw_column, CS_CAPTURE) */
static void list_cap(const lrow_t *rw, char *label, char *val, uint16_t *vc)
{
    uint32_t p = page_titled(rw->page), c = rw->slot & 3u;
    uint8_t pg = ui.page, cs = col_style;
    label[0] = val[0] = 0;
    *vc = T_TEXT;
    if (p >= NPAGES)
        return;
    memset(lcap, 0, sizeof lcap);
    ui.page = (uint8_t)p;
    col_style = CS_CAPTURE;
    draw_columns();
    col_style = cs;
    ui.page = pg;
    if (!lcap[c].on)
        return;
    list_words(label, lcap[c].label, 12);
    menu_words(val, lcap[c].val, 16);
    if (lcap[c].unit[0] && str_len(val) + str_len(lcap[c].unit) + 1u < 16u) {
        str_cpy(val + str_len(val), " ", 2);
        str_cpy(val + str_len(val), lcap[c].unit, 8);
    }
    *vc = lcap[c].vc;
}
static void list_capture(uint32_t c, const char *label, const char *val, const char *unit, uint16_t vc)
{
    c &= 3u;
    str_cpy(lcap[c].label, label, sizeof lcap[c].label);
    str_cpy(lcap[c].val, val, sizeof lcap[c].val);
    str_cpy(lcap[c].unit, unit, sizeof lcap[c].unit);
    lcap[c].vc = vc;
    lcap[c].on = label[0] || val[0];
}

/* KNOB 2 the row, KNOB 1 its value (the row's page edits it, as if it were shown) */
static void list_knob(uint32_t k, int32_t s)
{
    const list_t *l = list_on();
    uint8_t pg;
    uint32_t p;
    if (!l)
        return;
    if (k == 1u) {
        lst.row = (uint8_t)clamp((int32_t)lst.row + s, 0, (int32_t)list_n(l) - 1);
        return;
    }
    if (k != 0u || (p = page_titled(list_at(l, lst.row)->page)) >= NPAGES)
        return;
    pg = ui.page;
    ui.page = (uint8_t)p;
    edit_param(list_at(l, lst.row)->slot, s);
    ui.page = pg;
}
/* OCT+ on a row: SCL's Scale: the SCALES page; a list value: its whole list (ui_popup.c) */
static void list_enter(void)
{
    const list_t *l = list_on();
    const lrow_t *rw;
    uint32_t p;
    int16_t *vp;
    const param_desc_t *d;
    if (!l)
        return;
    rw = list_at(l, lst.row);
    if (str_eq(rw->page, "SCALES")) {
        p = page_titled("SCALES");
        ui.page = (uint8_t)p;
        page_entered();
        return;
    }
    p = page_titled(rw->page);
    if (p >= NPAGES)
        return;
    d = page_desc(&PAGES[p], rw->slot, &vp);
    if (d && vp && d->fmt == F_ENUM && d->names && d->max > d->min)
        vlist_open((uint8_t)p, rw->slot);
}

/* the octave under SCL's rows: the scale's notes lit in the track's colour, the root in text */
static void list_scale_pic(int32_t y, int32_t h)
{
    static const uint8_t WHITE[7] = {0, 2, 4, 5, 7, 9, 11}, BLACK[5][2] = {{1, 0}, {3, 1}, {6, 3}, {8, 4}, {10, 5}};
    uint32_t mask = scale_mask(TSEL), root = (uint32_t)TSEL->p[P_ROOT] % 12u, i;
    cv_begin(240, (uint32_t)h, T_BG);
    cv_rrect(PV_PANEL_X, 0, PV_PANEL_W, h, 6, T_PANEL, T_BG);
    for (i = 0; i < 7u; i++) {
        uint32_t n = WHITE[i], on = (mask >> ((n + 12u - root) % 12u)) & 1u;
        uint16_t c = n == root ? T_TEXT : on ? T_THEME : T_SURF;
        cv_rrect(20 + 29 * (int32_t)i, 8, 27, h - 16, 4, c, T_PANEL);
    }
    for (i = 0; i < 5u; i++) {
        uint32_t n = BLACK[i][0], on = (mask >> ((n + 12u - root) % 12u)) & 1u;
        uint16_t c = n == root ? T_TEXT : on ? ux_mix(T_BG, T_THEME, 75) : T_BG;
        int32_t x = 20 + 29 * BLACK[i][1] + 19;
        cv_rrect(x, 8, 16, (h - 16) * 58 / 100, 3, T_LINE, T_PANEL);
        cv_rrect(x + 1, 9, 14, (h - 16) * 58 / 100 - 2, 2, c, T_LINE);
    }
    cv_blit(0, (uint32_t)y);
}

/* n rows from LIST_Y0, row r's label and value from cap (1: a list behind it, ">"), row lifted, first .. first + vis
 * shown, where they are as a bar at the right */
static void list_rows(uint32_t n, uint32_t row, uint32_t first, uint32_t vis,
                      int (*cap)(uint32_t r, char *lb, char *v, uint16_t *vc))
{
    uint32_t r;
    int32_t top;
    char lb[16], v[16];
    uint16_t vc;
    for (top = 0; top < (int32_t)vis * LIST_ROW; ) {   /* in slices between rows */
        int32_t sh = (int32_t)vis * LIST_ROW - top <= 110 ? (int32_t)vis * LIST_ROW - top : 110;
        cv_begin(240, (uint32_t)sh, T_BG);
        cv_oy = -top;
        for (r = first; r < n && r < first + vis; r++) {
            int32_t y = (int32_t)(r - first) * LIST_ROW;
            int on = r == row, more;
            uint16_t bg = on ? T_LIFT : T_BG;
            if (y + LIST_ROW <= top || y >= top + sh)
                continue;
            more = cap(r, lb, v, &vc);
            if (on) {
                cv_rect(0, y, 240, 20, T_LIFT);
                cv_rect(0, y, 3, 20, T_THEME);
            }
            cv_text_on(12, y + 3, &AF_S, lb, on ? T_TEXT : T_SEC, bg);
            {
                int32_t w = text_w(&AF_S, v), x = 228 - (w < 104 ? w : 104);
                cv_free_text(x, y + 3, &AF_S, v, vc == T_DIM ? T_DIM : on ? T_THEME : T_MID, bg, 104);
            }
            if (more)
                cv_text_on(230, y + 2, &AF_S, ">", on ? T_THEME : T_DIM, bg);
        }
        if (n > vis) {
            int32_t th = (int32_t)vis * LIST_ROW * (int32_t)vis / (int32_t)n;
            int32_t ty = (int32_t)((vis * LIST_ROW - (uint32_t)th) * first / (n - vis));
            cv_rect(237, 0, 2, (int32_t)vis * LIST_ROW, T_LINE);
            cv_rect(237, ty, 2, th, T_THEME);
        }
        cv_oy = 0;
        cv_blit(0, (uint32_t)(LIST_Y0 + top));
        top += sh;
    }
}
static const list_t *list_drawn;                        /* (list_text) */
static int list_text(uint32_t r, char *lb, char *v, uint16_t *vc)
{
    list_cap(list_at(list_drawn, r), lb, v, vc);
    return str_eq(list_at(list_drawn, r)->page, "SCALES");   /* (OCT+: its list) */
}
static void list_draw(void)
{
    const list_t *l = list_on();
    uint32_t r, sig, vis;
    int32_t pic_y = l->pic == LP_SCALE ? 142 : l->pic == LP_GRAPH ? 96 : PV_SOUND_Y;
    char lb[16], v[16];
    uint16_t vc;
    if (ui.force) {
        lcd_fill(0, H_HEAD, 240, PV_SOUND_Y - H_HEAD, T_BG);
        ui.graph_sig = 0;
    }
    if (lst.page != ui.page) {                          /* (another page: its first row) */
        lst.page = ui.page;
        lst.row = lst.first = 0;
    }
    draw_head();
    pv_sound();
    vis = (uint32_t)((pic_y - LIST_Y0 - 2) / LIST_ROW);
    if (lst.row >= list_n(l)) lst.row = 0;
    if (lst.row < lst.first) lst.first = lst.row;
    if (lst.row >= lst.first + vis) lst.first = (uint8_t)(lst.row + 1u - vis);
    sig = lst.row * 7u + lst.first * 131u + ui.page * 1031u + ux.gen * 977u + ux.pal * 31u + song.sel;
    for (r = lst.first; r < list_n(l) && r < lst.first + vis; r++) {
        list_cap(list_at(l, r), lb, v, &vc);
        sig = str_hash(str_hash(sig, lb), v) + vc;
    }
    if (ui.force || sig != lst.sig) {
        lst.sig = sig;
        list_drawn = l;
        list_rows(list_n(l), lst.row, lst.first, vis, list_text);
    }
    if (l->pic == LP_SCALE) {
        static uint32_t ps;
        uint32_t s2 = scale_mask(TSEL) * 13u + (uint32_t)TSEL->p[P_ROOT] + ux.gen * 977u + ux.pal * 31u + song.sel * 7u;
        if (ui.force || s2 != ps) {
            ps = s2;
            list_scale_pic(pic_y, PV_SOUND_Y - pic_y);
        }
    } else if (l->pic == LP_GRAPH) {                    /* (the page's graph, as the panel draws it) */
        graph_y = pic_y;
        graph_h = PV_SOUND_Y - pic_y;
        draw_graph();
        graph_y = Y_GRAPH;
        graph_h = H_GRAPH;
    }
}
