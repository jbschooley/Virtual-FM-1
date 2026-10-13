/* SPDX-License-Identifier: GPL-3.0-only */
/* NEW SONG (SAVE > PROJECT, KNOB 1 past TMPL: NEW, then LOAD's OCT+): a new project from the template (none saved:
 * the power-on sounds), every pattern empty, in two screens. KEY: KNOB 1 ROOT, 2 SCALE, 3 TEMPO. ROLES: KNOB k track
 * k's role: KEEP (the template's sound) or DRUMS BASS CHORDS LEAD PAD: the first sound of that category, unless the
 * template's sound already is one. OCT+ next / create, OCT- back / cancel. Stopped only: the base loads like a project */
static void template_key(uint32_t *root, uint32_t *scale, int32_t *bpm);   /* project.c */
static int32_t accel(uint32_t role, int32_t s, int32_t range);            /* ui_input.c */
static void project_new_blank(void);
static int project_dirty(void);
enum { NR_KEEP, NR_DRUMS, NR_BASS, NR_CHORDS, NR_LEAD, NR_PAD, NR_N };
static const char *const NR_NAME[NR_N] = {"KEEP", "DRUMS", "BASS", "CHORDS", "LEAD", "PAD"};
static const uint8_t NR_CAT[NR_N] = {CAT_NONE, CAT_DRUM, CAT_BASS, CAT_KEYS, CAT_LEAD, CAT_PAD};
static struct {
    uint8_t on;                                     /* 0 closed, 1 KEY, 2 ROLES */
    uint8_t root, scale, role[NTRK];
    int16_t bpm;
} nw;
static int new_on(void) { return nw.on != 0; }
static void new_close(void) { if (nw.on) { nw.on = 0; ui.new_active=0; ui.force = 1; } }
static void new_open(void)
{
    ui.new_active=1;
    uint32_t r = (uint32_t)TSEL->p[P_ROOT], s = (uint32_t)TSEL->p[P_SCALE];
    int32_t b = song.g[G_BPM];
    template_key(&r, &s, &b);                       /* (the template's, when there is one) */
    memset(&nw, 0, sizeof nw);
    nw.root = (uint8_t)r;
    nw.scale = (uint8_t)s;
    nw.bpm = (int16_t)b;
    nw.on = 1;
    ui.force = 1;
}

/* the first sound of role r's category (its LIST order; DRUMS: the DRUM engine's first kit): its source, *k;
 * USER_NONE: none */
static uint32_t new_role_sound(uint32_t r, uint32_t *k)
{
    uint32_t i, total, src;
    if (r == NR_DRUMS) {
        *k = 0;
        return ENGI_DRUM;
    }
    for (i = 0; i < NELEM(CAT_ORDER) && CAT_ORDER[i] != NR_CAT[r]; i++)
        ;
    if (i == NELEM(CAT_ORDER))
        return USER_NONE;
    list_scan(LM_CAT + i, 0xFFFFFFFFu, 0, 0, &src, k, &total);
    return total ? src : USER_NONE;
}
/* the base loaded: each role's sound (selected track k meanwhile: the loads are the selected track's), the key, tempo */
static void new_create(void)
{
    uint32_t k, sel;
    if (transport_busy()) { ui_message("STOP FIRST"); return; }
    if (template_used()) template_load();
    else project_new_blank();
    sel = song.sel;
    undo_depth++;                                   /* (no undo levels: a new project) */
    for (k = 0; k < NTRK; k++) {
        uint32_t src, kk, cs, ck;
        track_t *t = &trk[k];
        if (nw.role[k] == NR_KEEP)
            continue;
        song.sel = (uint8_t)k;
        cur_entry(&cs, &ck);
        if (entry_cat(cs, ck) == NR_CAT[nw.role[k]])
            continue;                               /* the template's sound is one already */
        if ((src = new_role_sound(nw.role[k], &kk)) == USER_NONE)
            continue;
        if (src == USER_NATIVE_P5 || src == USER_NATIVE_FM || src == USER_NATIVE_CZ)
            native_load(src_engine(src, kk), kk, k);
        else if (src == USER_GENERAL)
            up_load(kk);
        else {
            if (src != t->eng_req) set_engine_of(t, src);
            apply_preset_to(t, kk);
        }
    }
    undo_depth--;
    song.sel = (uint8_t)sel;
    for (k = 0; k < NTRK; k++)
        trk[k].p[P_ROOT] = (int16_t)nw.root;
    TSEL->p[P_SCALE] = (int16_t)nw.scale;
    scale_share(TSEL);
    song.g[G_BPM] = nw.bpm;
    undo_clear();
    nw.on = 0; ui.new_active = 0;
    go_home();
    sync_reload = 1;
    ui_message("NEW SONG");
}

/* OCT+ next / create, OCT- back / cancel; KNOB 1..4 the screen's; HOME tapped: cancel */
static void new_input(uint32_t oct, int home_tap)
{
    uint32_t k;
    for (k = 0; k < 4u; k++) {
        int32_t s = panel_enc(EN_K1 + k);
        if (!s) continue;
        ui.hot_col = (uint8_t)k;
        ui.hot_t = 40;
        if (nw.on == 1) {
            if (k == 0u) nw.root = (uint8_t)clamp((int32_t)nw.root + (s > 0 ? 1 : -1), TP[P_ROOT].min, TP[P_ROOT].max);
            else if (k == 1u) nw.scale = (uint8_t)clamp((int32_t)nw.scale + (s > 0 ? 1 : -1), 0, SCALE_TOTAL - 1);
            else if (k == 2u) nw.bpm = (int16_t)clamp(nw.bpm + accel(EN_K1 + k, s, GP[G_BPM].max - GP[G_BPM].min), GP[G_BPM].min, GP[G_BPM].max);
        } else {
            nw.role[k] = (uint8_t)clamp((int32_t)nw.role[k] + (s > 0 ? 1 : -1), 0, NR_N - 1);
        }
    }
    if (home_tap)
        new_close();
    else if (oct & 2u) {
        if (nw.on == 1) { nw.on = 2; ui.force = 1; }
        else new_create();
    } else if (oct & 1u) {
        if (nw.on == 2) { nw.on = 1; ui.force = 1; }
        else new_close();
    }
}

/* the scale in a word or two: "Natural minor" -> "Minor" (big: "minor", after the root) */
static void new_scale_word(char *b, uint32_t n, int small)
{
    const char *t = SCALE_TITLE[nw.scale % SCALE_TOTAL];
    if (!memcmp(t, "Natural ", 8))
        t += 8;
    str_cpy(b, t, n);
    if (small && b[0] >= 'A' && b[0] <= 'Z') b[0] = (char)(b[0] + 32);
    if (!small && b[0] >= 'a' && b[0] <= 'z') b[0] = (char)(b[0] - 32);
}
/* a sound's name as words: "FUNK BASS II" -> "Funk Bass II" (two letters or fewer kept: II, EP) */
static void new_words(char *d, const char *s, uint32_t n)
{
    uint32_t i, w = 0;
    str_cpy(d, s, n);
    for (i = 0; d[i]; i++) {
        uint32_t e = i;
        while (d[e] && d[e] != ' ') e++;
        if (e - i > 2u)
            for (w = i + 1u; w < e; w++)
                if (d[w] >= 'A' && d[w] <= 'Z') d[w] = (char)(d[w] + 32);
        i = d[e] ? e : e - 1u;
    }
}
/* the keys under NEW SONG (mock/r6_pages NEW SONG): OCT- back on the left, OCT+ on the right in the track's colour
 * (go, its icon); the sound designer's START too */
static void new_keys_row(const char *go, uint32_t icon)
{
    int32_t gw = text_w(&AF_S, go) + 30;
    cv_begin(240, 24, T_BG);
    cv_rrect(6, 2, 40, 20, 5, T_SURF, T_BG);
    cv_icon_mid(20, 12, 12, ICON_X_UNDO, T_MID, T_SURF);
    cv_rrect(234 - gw, 2, gw, 20, 5, T_THEME, T_BG);
    cv_icon_mid(234 - gw + 8, 12, 12, icon, T_INK, T_THEME);
    cv_text_on(234 - gw + 22, 4, &AF_S, go, T_INK, T_THEME);
    cv_blit(0, 212);
}
static void new_keys(int create) { new_keys_row(create ? "Create" : "Roles", create ? ICON_X_CHECK : ICON_X_RIGHT); }
/* a role's column: the track's colour bar, "Track 1", the role in its colour, the sound it gets (two lines), its
 * engine; KEEP: the template's sound, dim; the knob turning: outlined */
static void new_role_col(uint32_t k)
{
    uint32_t kk, src = nw.role[k] ? new_role_sound(nw.role[k], &kk) : USER_NONE, e, n, cut;
    int hot = ui.hot_t && ui.hot_col == k, on = nw.role[k] != 0u;
    uint16_t bg = hot ? T_LIFT : T_PANEL;
    char nm[16], tag[6], l[8] = "Track 1", w[16];
    int32_t x = 4;
    cv_begin(CARD_W, 184, T_BG);
    cv_rrect(0, 0, CARD_W, 184, 6, hot ? T_THEME : T_PANEL, T_BG);
    cv_rrect(1, 1, CARD_W - 2, 182, 5, bg, hot ? T_THEME : T_PANEL);
    cv_rect(6, 3, CARD_W - 12, 3, on ? T_TRK(k) : T_LINE);
    l[6] = (char)('1' + k);
    cv_text_on(x, 12, &AF_X, l, T_MID, bg);
    {
        const aafont_t *rf = text_w(&AF_M, NR_NAME[nw.role[k]]) <= CARD_W - 6 ? &AF_M : &AF_S;
        cv_text_on(rf == &AF_M ? 3 : x, rf == &AF_M ? 30 : 33, rf, NR_NAME[nw.role[k]], on ? T_TRK(k) : T_DIM, bg);
    }
    if (src == USER_NONE) {
        str_cpy(nm, nw.role[k] || !template_used() ? "--" : "Template", sizeof nm);
        e = 0xFFu;
    } else {
        entry_label(src, kk, tag, nm);
        e = src_engine(src, kk);
    }
    new_words(w, nm, sizeof w);                         /* "Funk Bass II", in two lines: at the last space that fits */
    n = str_len(w);
    cut = n;
    if (text_w(&AF_S, w) > CARD_W - 8)
        for (cut = n; cut-- > 0u;)
            if (w[cut] == ' ') {
                w[cut] = 0;
                if (text_w(&AF_S, w) <= CARD_W - 8 || !cut) break;
                w[cut] = ' ';
            }
    if (!cut) cut = n;
    cv_free_text(x, 62, &AF_S, w, on ? T_TEXT : T_DIM, bg, CARD_W - 8);
    if (cut > 0u && cut < n)
        cv_free_text(x, 77, &AF_S, w + cut + 1u, on ? T_TEXT : T_DIM, bg, CARD_W - 8);
    if (e != 0xFFu)
        cv_free_text(x, 96, &AF_X, ENGINES[eng_idx(e)]->name, T_MID, bg, CARD_W - 8);
    cv_blit((uint32_t)CARD_X(k), 24);
}
static void new_draw(void)
{
    static uint32_t sig;
    uint32_t k, s = nw.on * 7u + nw.root * 131u + nw.scale * 1031u + (uint32_t)nw.bpm * 40503u + ux.gen * 977u +
                    (ui.hot_t ? ui.hot_col + 1u : 0u) * 7u + ux.pal * 31u;
    char v[12];
    const char *unit;
    for (k = 0; k < NTRK; k++)
        s = s * 33u + nw.role[k];
    if (ui.force)
        lcd_fill(0, H_HEAD, 240, 240 - H_HEAD, T_BG);
    draw_head();
    if (!ui.force && s == sig) {
        if (ui.hot_t) ui.hot_t--;
        return;
    }
    sig = s;
    if (nw.on == 1) {                               /* KEY: ROOT SCALE TEMPO as rings, the key large under them */
        char key[32];
        const aafont_t *f = &AF_L;
        uint8_t cs = col_style;
        col_style = CS_RING;
        param_format(&TP[P_ROOT], nw.root, v, &unit);
        draw_column(0, "ROOT", v, unit, VAL(0u), nw.root * 1000 / 11, ICON_AUTO);
        new_scale_word(key, 12, 0);
        draw_column(1, "SCALE", key, "", VAL(1u), -1, ICON_NONE);
        fmt_int(v, nw.bpm);
        draw_column(2, "TEMPO", v, "BPM", VAL(2u), RATIO(&GP[G_BPM], nw.bpm), ICON_AUTO);
        draw_column(3, "", "", "", T_THEME, -1, ICON_NONE);
        col_style = cs;
        param_format(&TP[P_ROOT], nw.root, key, &unit);
        str_cpy(key + str_len(key), " ", 2);
        new_scale_word(key + str_len(key), sizeof key - str_len(key), 1);   /* "A minor" */
        if (text_w(f, key) > 216) f = &AF_M;
        cv_begin(240, 100, T_BG);
        cv_rrect(PV_PANEL_X, 0, PV_PANEL_W, 100, 6, T_PANEL, T_BG);
        cv_text_c(120, f == &AF_L ? 22 : 32, f, key, T_TEXT, T_PANEL);
        fmt_int(key, nw.bpm);
        str_cpy(key + str_len(key), " bpm \xB7 4/4", 14);
        cv_text_c(120, 66, &AF_S, key, T_MID, T_PANEL);
        cv_blit(0, 108);
        new_keys(0);
    } else {                                        /* ROLES: a column each */
        for (k = 0; k < NTRK; k++)
            new_role_col(k);
        new_keys(1);
    }
    ui.force = 0;
    if (ui.hot_t) ui.hot_t--;
}
