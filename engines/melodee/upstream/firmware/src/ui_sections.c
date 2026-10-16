/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* An engine's EDIT pages in sections (docs/design README "Sound pages, fast", mock/r7_sound_nav): Prophet's Osc,
 * Mixer, Filter ..; FM6's Algo, OP1 .. OP6 (the operator pages, once per operator: fm6_opsel), Pitch, LFO, Ctrl; CZ-1's
 * Line, Line 1, Pitch 1 ..; Store and Tools last. PRESETS jumps a section (to the page last used in it), SELECT turns
 * the pages (on across the sections, as ever); the sections as tabs under the header, a section's pages as the
 * header's dots. EDIT pressed on an EDIT page: the map, a row a section, a tile a page (KNOB 1 or SELECT a page of
 * the section, KNOB 2 the section, OCT+ opens, OCT- or EDIT closes).
 * Included by ui_draw.c */
#define SEC_MAX 16
#define SEC_TAB_H 26                                    /* the tabs: rows 30 .. 55 (a pill 24 tall at 31) */
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
        for (x = 2, k = first - 1u; k <= cur; k++)
            x += text_w(TY_LB, name[k]) + 20;
        if (x > 238)
            break;
    }
    cv_begin(240, SEC_TAB_H, T_BG);
    for (x = 2, k = first; k < n; k++) {
        int on = k == cur, far = (k > cur ? k - cur : cur - k) > 2u;
        w = text_w(TY_LB, name[k]) + 16;
        if (x + w > 238)
            break;
        if (on)
            cv_rrect(x, 1, w, 24, 6, T_THEME, T_BG), cv_rrect(x + 1, 2, w - 2, 22, 5, ux_mix(T_BG, T_THEME, 22), T_THEME);
        cv_text_c(x + w / 2, ty_y(13, TY_LB), TY_LB, name[k], on ? T_THEME : far ? T_DIM : T_MID,
                  on ? ux_mix(T_BG, T_THEME, 22) : T_BG);
        x += w + 4;
    }
    cv_blit(0, H_HEAD);
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
/* KNOB 2 a category a detent (to the page last used in it), KNOB 1 (or SELECT) a page of it a detent; every detent
 * counts, however long the frame was. OCT+ opens, OCT- closes (oct: bit 0 OCT-, bit 1 OCT+) */
static void smap_input(int32_t pages, int32_t categories, uint32_t oct)
{
    uint8_t pg[8];
    uint32_t n, row = smap.row;
    if (categories)
        smap.row = (uint8_t)clamp((int32_t)smap.row + categories, 0, (int32_t)sec.n - 1);
    n = smap_pages(smap.row, pg);
    if (smap.row != row) {                             /* category movement is independent of its page count */
        uint32_t c;
        for (c = 0; c < n && pg[c] != sec.last[smap.row]; c++)
            ;
        smap.col = (uint8_t)(c < n ? c : 0u);
    }
    if (pages)
        smap.col = (uint8_t)clamp((int32_t)smap.col + pages, 0, (int32_t)n - 1);
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
/* the map (large type, C): a row a section, its name; the row under the cursor open, its pages as pills under its name
 * (wrapping; the cursor's filled, the page open a dot); the section open its name in the track's colour */
#define SM_RH 32                                        /* a row (as the lists', ui_list.c LIST_RH), 34 apart */
#define SM_ROW 34
#define SM_PILL 24
#define SM_LINE 28
static int32_t smap_pills(uint32_t r, int32_t y, uint16_t bg, int draw, uint32_t cur)   /* returns their height */
{
    uint8_t pg[8];
    uint32_t n = smap_pages(r, pg), c;
    int32_t x = 12, ly = y;
    for (c = 0; c < n; c++) {
        char b[16];
        int on = c == smap.col, here = pg[c] == ui.page && r == cur;
        int32_t w;
        sec_short(pg[c], b, sizeof b);
        w = text_w(TY_LB, b) + (here ? 26 : 16);
        if (x > 12 && x + w > 228)
            x = 12, ly += SM_LINE;
        if (draw) {
            uint16_t f = on ? T_THEME : T_BG;
            cv_rrect(x, ly, w, SM_PILL, 8, f, bg);
            if (here)
                cv_disc(x + 10, ly + SM_PILL / 2, 6, on ? T_BG : T_THEME, 0);
            cv_text_on(x + (here ? 18 : 8), ty_y(ly + SM_PILL / 2, TY_LB), TY_LB, b, on ? T_BG : T_TEXT, f);
        }
        x += w + 6;
    }
    return n ? ly + SM_LINE - y : 0;
}
static int32_t smap_h(uint32_t r) { return r == smap.row ? SM_RH + smap_pills(r, 0, T_BG, 0, 0) + 6 : SM_RH; }
static void smap_draw(void)
{
    uint32_t r, sig, cur = sec_of(ui.page), first, last;
    int32_t h = 240 - H_HEAD, top, used;
    if (ui.force)
        lcd_fill(0, H_HEAD, 240, 240 - H_HEAD, T_BG);
    draw_head();
    sec_build();
    sig = smap.row * 7u + smap.col * 131u + sec.eng * 1031u + ux.gen * 977u + ux.pal * 31u + ui.page * 40503u;
    if (!ui.force && sig == smap.sig)
        return;
    smap.sig = sig;
    for (first = smap.row, used = smap_h(smap.row) + (smap.row + 1u < sec.n ? SM_ROW : 0) + 2; first > 0u &&
         used + SM_ROW <= h; first--)                 /* (as many above as fit with one below) */
        used += SM_ROW;
    for (last = first, used = 2; last < sec.n && used + smap_h(last) <= h; last++)
        used += smap_h(last) + 2;
    for (top = 0; top < h; ) {                          /* in slices between rows */
        int32_t sh = 0, y = 2;
        for (r = first; r < last; y += smap_h(r) + 2, r++)
            if (y >= top && y + smap_h(r) + 2 - top <= 124)
                sh = y + smap_h(r) + 2 - top;
        if (sh <= 0)
            sh = h - top < 124 ? h - top : 124;
        cv_begin(240, (uint32_t)sh, T_BG);
        cv_oy = -top;
        for (r = first, y = 2; r < last; y += smap_h(r) + 2, r++) {
            int open = r == smap.row;
            uint16_t bg = open ? ux_mix(T_BG, T_THEME, 18) : T_BG;
            if (y + smap_h(r) <= top || y >= top + sh)
                continue;
            if (open)
                cv_rrect(BOX_X, y, BOX_W, smap_h(r), 8, bg, T_BG);
            cv_text_on(12, ty_y(y + SM_RH / 2, TY_TX), TY_TX, sec.name[r], r == cur ? T_THEME : open ? T_TEXT : T_SEC,
                       bg);
            if (open)
                smap_pills(r, y + SM_RH, bg, 1, cur);
        }
        cv_oy = 0;
        cv_blit(0, (uint32_t)(H_HEAD + top));
        top += sh;
    }
}
