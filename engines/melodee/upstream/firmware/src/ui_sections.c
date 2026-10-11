/* SPDX-License-Identifier: GPL-3.0-only */
/* An engine's EDIT pages in sections (docs/design README "Sound pages, fast", mock/r7_sound_nav): Prophet's Osc,
 * Mixer, Filter ..; FM6's Algo, OP1 .. OP6 (the operator pages, once per operator: fm6_opsel), Pitch, LFO, Ctrl; CZ-1's
 * Line, Line 1, Pitch 1 ..; Store and Tools last. PRESETS jumps a section (to the page last used in it), SELECT turns
 * the pages (on across the sections, as ever); the sections as tabs under the header, a section's pages as the
 * header's dots. EDIT pressed on an EDIT page: the map, a row a section, a tile a page (KNOB 1 across, KNOB 2 down,
 * OCT+ opens, OCT- or EDIT closes). EDIT held: the sections on the white keys (ui_layer.c).
 * Included by ui_draw.c */
#define SEC_MAX 16
#define SEC_TAB_H 16                                    /* the tabs: rows 18 .. 33 */
static const struct { const char *prefix, *name; } SEC_RULE[] = {   /* a page's section by its title (first match) */
    {"P5 OSC", "Osc"}, {"P5 B WAVES", "Osc"}, {"P5 MIXER", "Mixer"}, {"P5 FILTER", "Filter"}, {"P5 FLT ENV", "Filter"},
    {"P5 AMP", "Amp"}, {"P5 ENV MOD", "Mod"}, {"P5 POLYMOD", "Mod"}, {"P5 MOD FLT", "Mod"}, {"P5 LFO", "LFO"},
    {"P5 WHEEL", "Wheel"}, {"P5 UNISON", "Voice"}, {"P5 BEND", "Voice"}, {"P5 STORE", "Store"},
    {"STORE", "Store"}, {"ALGO", "Algo"}, {"PITCH", "Pitch"}, {"FM LFO", "LFO"}, {"FM ", "Ctrl"},
    {"CZ TOOLS", "Tools"}, {"CZ1 ", "Line 1"}, {"C1 PIT", "Pitch 1"}, {"C1 WAV", "Wave 1"}, {"C1 AMP", "Amp 1"},
    {"CZ2 ", "Line 2"}, {"C2 PIT", "Pitch 2"}, {"C2 WAV", "Wave 2"}, {"C2 AMP", "Amp 2"}, {"CZ ", "Line"},
    {"DCW", "Env"}, {"DCA", "Env"}, {"DCO", "Env"}, {"CZ LEVEL", "Env"}, {"OP", "Env"},
    {"DRUM", "Drum"}, {"LANES", "Lanes"}, {"VOICE", "Voice"}, {"EDIT", "Sound"}};
static const char *const SEC_OP[6] = {"OP1", "OP2", "OP3", "OP4", "OP5", "OP6"};
static struct {
    uint8_t n, eng, e7, ok;
    const char *name[SEC_MAX];
    uint8_t first[SEC_MAX], op[SEC_MAX], last[SEC_MAX];   /* op: an operator's (+1), 0 none; last: its page last used */
} sec;
static struct { uint8_t on, row, col; uint32_t sig; } smap;   /* the map: open, its cursor */
static int sec_map_on(void) { return smap.on; }

static const char *sec_rule(uint32_t i, uint32_t *plen)   /* page i's section name, the prefix it matched */
{
    uint32_t r;
    for (r = 0; r < NELEM(SEC_RULE); r++) {
        uint32_t n = str_len(SEC_RULE[r].prefix);
        if (str_prefix(PAGES[i].title, SEC_RULE[r].prefix)) {
            if (plen) *plen = n;
            return SEC_RULE[r].name;
        }
    }
    if (plen) *plen = 0;
    return "Sound";
}
static int sec_store(const char *n) { return str_eq(n, "Store") || str_eq(n, "Tools"); }
static void menu_words(char *d, const char *s, uint32_t n);   /* ui_menu.c */
/* the sections of the selected track's engine (rebuilt when the engine changes): in page order, Store and Tools last */
static void sec_build(void)
{
    uint32_t i, k, pass;
    uint8_t e = (uint8_t)TSEL->eng_req, e7 = (uint8_t)TSEL->p[P_E7];
    if (sec.ok && sec.eng == e && sec.e7 == e7)
        return;
    memset(&sec, 0, sizeof sec);
    sec.eng = e;
    sec.e7 = e7;
    sec.ok = 1;
    for (pass = 0; pass < 2u; pass++)
        for (i = 0; i < NPAGES; i++) {
            const char *nm;
            if (PAGES[i].fam != FAM_EDIT || !page_visible(i))
                continue;
            if (PAGES[i].scope == SC_FMOP) {            /* the operators: a section each, on their first page */
                if (pass || (sec.n && sec.op[sec.n - 1u]))
                    continue;
                for (k = 0; k < 6u && sec.n < SEC_MAX; k++) {
                    sec.name[sec.n] = SEC_OP[k];
                    sec.first[sec.n] = (uint8_t)i;
                    sec.op[sec.n++] = (uint8_t)(k + 1u);
                }
                continue;
            }
            nm = sec_rule(i, 0);
            if (sec_store(nm) != (int)pass)
                continue;
            for (k = 0; k < sec.n && !str_eq(sec.name[k], nm); k++)
                ;
            if (k == sec.n && sec.n < SEC_MAX) {
                sec.name[sec.n] = nm;
                sec.first[sec.n] = (uint8_t)i;
                sec.op[sec.n++] = 0;
            }
        }
    for (k = 0; k < sec.n; k++)
        sec.last[k] = sec.first[k];
}
static uint32_t sec_of(uint32_t i)                      /* page i's section (an operator page: fm6_opsel's) */
{
    uint32_t k;
    const char *nm;
    sec_build();
    if (PAGES[i].scope == SC_FMOP) {
        for (k = 0; k < sec.n && sec.op[k] != fm6_opsel % 6u + 1u; k++)
            ;
        return k < sec.n ? k : 0u;
    }
    nm = sec_rule(i, 0);
    for (k = 0; k < sec.n && !str_eq(sec.name[k], nm); k++)
        ;
    return k < sec.n ? k : 0u;
}
/* sections to show: an EDIT page of an engine with more than one */
static int sec_on(void)
{
    if (ui.home || cur_page()->fam != FAM_EDIT)
        return 0;
    sec_build();
    return sec.n > 1u;
}
static void sec_go(uint32_t k)                          /* to section k: the page last used in it */
{
    uint32_t p;
    sec_build();
    if (k >= sec.n)
        return;
    p = sec.last[k];
    if (PAGES[p].fam != FAM_EDIT || !page_visible(p))
        p = sec.first[k];
    if (sec.op[k])
        fm6_opsel = (uint8_t)(sec.op[k] - 1u);
    ui.home = 0;
    ui.page = (uint8_t)p;
    ui.fam_last[FAM_EDIT] = ui.page;
    page_entered();
}
static void sec_note(void)                              /* the page shown: the last used in its section */
{
    if (sec_on())
        sec.last[sec_of(ui.page)] = ui.page;
}
static void sec_turn(int32_t s)                         /* PRESETS on an EDIT page: the next / previous section */
{
    uint32_t k = sec_of(ui.page);
    sec_note();
    sec_go((uint32_t)clamp((int32_t)k + (s > 0 ? 1 : -1), 0, (int32_t)sec.n - 1));
}
/* page i's name without its engine's prefix, in capitals ("P5 FLT ENV" -> "FLT ENV", "FM LFO 2" -> "LFO 2", "C1 PIT
 * R1-4" -> "R1-4"; EDIT 1 / 2: the engine's page titles, "OSC") */
static void sec_chip(uint32_t i, char *b, uint32_t n)
{
    static const char *const STRIP[] = {"P5 ", "FM ", "CZ1 ", "CZ2 ", "CZ ", "C1 PIT ", "C1 WAV ", "C1 AMP ", "C2 PIT ",
                                        "C2 WAV ", "C2 AMP "};
    const char *t = PAGES[i].title;
    uint32_t k;
    if (PAGES[i].scope == SC_ENGINE) {
        str_cpy(b, ENGINES[eng_idx(TSEL->eng_req)]->page_title[PAGES[i].id[0] != P_E0], n);
        return;
    }
    for (k = 0; k < NELEM(STRIP); k++)
        if (str_prefix(t, STRIP[k]) && t[str_len(STRIP[k])]) {
            t += str_len(STRIP[k]);
            break;
        }
    str_cpy(b, t, n);
}
static void sec_short(uint32_t i, char *b, uint32_t n)   /* .. in words ("Flt env", "LFO 2") */
{
    char c[24];
    sec_chip(i, c, sizeof c);
    menu_words(b, c, n);
}

/* tabs under the header: name[cur] filled, its neighbours; as many as fit around it (sig: what they show) */
static void tabs_draw(const char *const *name, uint32_t n, uint32_t cur, uint32_t sig)
{
    static uint32_t drawn;
    uint32_t k, first;
    int32_t x, w;
    if (!ui.force && sig == drawn)
        return;
    drawn = sig;
    for (first = cur; first > 0u; first--) {           /* from the furthest back that still lets cur fit */
        for (x = 6, k = first - 1u; k <= cur; k++)
            x += text_w(&AF_X, name[k]) + 15;
        if (x > 236)
            break;
    }
    cv_begin(240, SEC_TAB_H, T_BG);
    for (x = 6, k = first; k < n; k++) {
        int on = k == cur, far = (k > cur ? k - cur : cur - k) > 2u;
        w = text_w(&AF_X, name[k]) + 12;
        if (x + w > 236)
            break;
        if (on)
            cv_rrect(x, 0, w, 16, 5, T_THEME, T_BG), cv_rrect(x + 1, 1, w - 2, 14, 4, ux_mix(T_BG, T_THEME, 22), T_THEME);
        cv_text_c(x + w / 2, 3, &AF_X, name[k], on ? T_THEME : far ? T_DIM : T_MID, on ? ux_mix(T_BG, T_THEME, 22) : T_BG);
        x += w + 3;
    }
    cv_blit(0, H_HEAD);
}
/* an engine's sections as the tabs */
static void sec_tabs(void)
{
    uint32_t cur;
    sec_build();
    cur = sec_of(ui.page);
    tabs_draw(sec.name, sec.n, cur, cur * 7u + sec.n * 131u + sec.eng * 1031u + ux.gen * 977u + ux.pal * 31u + song.sel);
}

/* ---------------------------------------------------------- the map --- */
static uint32_t smap_pages(uint32_t r, uint8_t *pg)     /* section r's pages (an operator's: the operator pages) */
{
    uint32_t i, n = 0;
    for (i = 0; i < NPAGES && n < 8u; i++) {
        if (PAGES[i].fam != FAM_EDIT || !page_visible(i))
            continue;
        if (sec.op[r] ? PAGES[i].scope == SC_FMOP : PAGES[i].scope != SC_FMOP && str_eq(sec_rule(i, 0), sec.name[r]))
            pg[n++] = (uint8_t)i;
    }
    return n;
}
static void smap_open(void)
{
    uint8_t pg[8];
    uint32_t n, c;
    sec_build();
    smap.on = 1;
    smap.row = (uint8_t)sec_of(ui.page);
    n = smap_pages(smap.row, pg);
    for (c = 0; c < n && pg[c] != ui.page; c++)
        ;
    smap.col = (uint8_t)(c < n ? c : 0u);
    ui.force = 1;
}
static void smap_close(void)
{
    smap.on = 0;
    ui.force = 1;
}
/* KNOB 1 across, KNOB 2 down; OCT+ opens the page, OCT- closes (oct: bit 0 OCT-, bit 1 OCT+) */
static void smap_input(int32_t k1, int32_t k2, uint32_t oct)
{
    uint8_t pg[8];
    uint32_t n;
    if (k2)
        smap.row = (uint8_t)clamp((int32_t)smap.row + (k2 > 0 ? 1 : -1), 0, (int32_t)sec.n - 1);
    n = smap_pages(smap.row, pg);
    if (k1)
        smap.col = (uint8_t)clamp((int32_t)smap.col + (k1 > 0 ? 1 : -1), 0, (int32_t)n - 1);
    if (smap.col >= n)
        smap.col = (uint8_t)(n ? n - 1u : 0u);
    if (oct & 1u) {
        smap_close();
    } else if ((oct & 2u) && n) {
        if (sec.op[smap.row])
            fm6_opsel = (uint8_t)(sec.op[smap.row] - 1u);
        ui.home = 0;
        ui.page = pg[smap.col];
        ui.fam_last[FAM_EDIT] = ui.page;
        page_entered();
        smap_close();
    }
}
/* a row a section (its name), a tile a page; the cursor lifted, outlined; the page open a dot in its tile */
static void smap_draw(void)
{
    uint32_t r, sig, cur = sec_of(ui.page);
    int32_t rh, top, h = 240 - H_HEAD;
    if (ui.force)
        lcd_fill(0, H_HEAD, 240, 240 - H_HEAD, T_BG);
    draw_head();
    sec_build();
    sig = smap.row * 7u + smap.col * 131u + sec.eng * 1031u + ux.gen * 977u + ux.pal * 31u + ui.page * 40503u;
    if (!ui.force && sig == smap.sig)
        return;
    smap.sig = sig;
    {   /* (the operators' rows: their pages named once, a row above OP1) */
        uint32_t extra = 0;
        for (r = 0; r < sec.n; r++)
            extra |= sec.op[r] != 0u;
        rh = sec.n ? (h - 8) / (int32_t)(sec.n + extra) : 20;
    }
    if (rh > 24) rh = 24;
    for (top = 0; top < h; ) {                          /* in slices between rows */
        int32_t sh = h - top <= 124 ? h - top : 4 + (top + 124 - 4) / rh * rh - top;
        cv_begin(240, (uint32_t)sh, T_BG);
        cv_oy = -top;
        for (r = 0; r < sec.n; r++) {
            uint8_t pg[8];
            uint32_t n = smap_pages(r, pg), c, ops_above = 0;
            int32_t y, w;
            for (c = 0; c < r; c++)
                ops_above |= sec.op[c] != 0u;
            y = 4 + (int32_t)(r + (sec.op[r] || ops_above)) * rh;
            w = n ? (178 - (int32_t)(n - 1u) * 3) / (int32_t)n : 0;
            if (sec.op[r] == 1u && y - rh + rh > top && y - rh < top + sh)   /* the operators' pages, named */
                for (c = 0; c < n; c++) {
                    char b[16];
                    sec_short(pg[c], b, sizeof b);
                    cv_free_text(56 + (int32_t)c * (w + 3), y - rh + (rh - 12) / 2, &AF_X, b, T_MID, T_BG, w);
                }
            if (y + rh <= top || y >= top + sh)
                continue;
            cv_text_on(10, y + (rh - 12) / 2, &AF_X, sec.name[r], r == cur ? T_THEME : T_MID, T_BG);
            for (c = 0; c < n; c++) {
                int32_t x = 56 + (int32_t)c * (w + 3);
                int on = r == smap.row && c == smap.col, here = pg[c] == ui.page && r == cur;
                uint16_t bg = on ? T_LIFT : T_PANEL;
                char b[16];
                if (on) {
                    cv_rrect(x, y, w, rh - 3, 5, T_THEME, T_BG);
                    cv_rrect(x + 1, y + 1, w - 2, rh - 5, 4, bg, T_THEME);
                } else {
                    cv_rrect(x, y, w, rh - 3, 5, bg, T_BG);
                }
                if (sec.op[r]) b[0] = 0;                /* (an operator's tile: its column says it) */
                else sec_short(pg[c], b, sizeof b);
                {   /* its name centred (the page open: a dot before it) */
                    int32_t lx = x + (here ? 11 : 4), room = w - (here ? 15 : 8), tw = text_w(&AF_X, b);
                    if (here)
                        cv_disc(x + 6, y + (rh - 3) / 2, 4, T_THEME, 0);
                    cv_free_text(lx + (tw < room ? (room - tw) / 2 : 0), y + (rh - 15) / 2, &AF_X, b, on ? T_TEXT : T_SEC,
                                 bg, room);
                }
            }
        }
        cv_oy = 0;
        cv_blit(0, (uint32_t)(H_HEAD + top));
        top += sh;
    }
}
