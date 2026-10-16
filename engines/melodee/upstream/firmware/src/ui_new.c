/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* NEW SONG (SAVE > PROJECT, KNOB 1 past TMPL: NEW, then LOAD's OCT+): a new project from the template (none saved:
 * the power-on sounds), every pattern empty, in two screens. KEY: KNOB 1 ROOT, 2 SCALE, 3 TEMPO. ROLES: KNOB k track
 * k's role: -- (no track), KEEP (the template's sound) or DRUMS BASS CHORDS LEAD PAD: the first sound of that category,
 * unless the template's sound already is one. The tracks with a role are the song's, in their order (the template's
 * KEEP, the others --: one without a template). OCT+ next / create, OCT- back / cancel. Stopped only: the base loads like
 * a project */
static void template_key(uint32_t *root, uint32_t *scale, int32_t *bpm);   /* project.c */
static uint32_t template_tracks(void);
static int track_remove(uint32_t k);                                       /* tracks.c */
static void track_blank(uint32_t k);
static int32_t accel(uint32_t role, int32_t s, int32_t range);            /* ui_input.c */
static void project_new_blank(void);
static int project_dirty(void);
enum { NR_NONE, NR_KEEP, NR_DRUMS, NR_BASS, NR_CHORDS, NR_LEAD, NR_PAD, NR_N };
static const char *const NR_NAME[NR_N] = {"--", "KEEP", "DRUMS", "BASS", "CHORDS", "LEAD", "PAD"};
static const uint8_t NR_CAT[NR_N] = {CAT_NONE, CAT_NONE, CAT_DRUM, CAT_BASS, CAT_KEYS, CAT_LEAD, CAT_PAD};
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
    for (uint32_t k = 0; k < template_tracks() && k < NTRK; k++)
        nw.role[k] = NR_KEEP;                       /* (the others: --) */
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
    song.ntrk = NTRK;                               /* (every place: the roles say which stay) */
    sel = song.sel;
    undo_depth++;                                   /* (no undo levels: a new project) */
    for (k = 0; k < NTRK; k++) {
        uint32_t src, kk, cs, ck;
        track_t *t = &trk[k];
        if (nw.role[k] <= NR_KEEP)
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
    for (k = NTRK; k-- > 0u;) {                     /* the places with no role: the last ones empty, one between moves */
        if (nw.role[k] != NR_NONE || trk_n() < 2u)  /* the tracks after down (one track stays) */
            continue;
        if (k + 1u == trk_n()) {
            track_blank(k);
            song.ntrk = (uint8_t)k;
        } else {
            track_remove(k);
        }
    }
    if (song.sel >= trk_n())
        song.sel = 0;
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
 * (go, its icon; the text face); the sound designer's START too */
static void new_keys_row(const char *go, uint32_t icon)
{
    int32_t gw = text_w(TY_TX, go) + 42;
    cv_begin(240, H_FOOT, T_BG);
    cv_rrect(4, 2, 48, 24, 6, T_SURF, T_BG);
    cv_icon_mid(20, 14, 16, ICON_X_UNDO, T_MID, T_SURF);
    cv_rrect(236 - gw, 2, gw, 24, 6, T_THEME, T_BG);
    cv_icon_mid(236 - gw + 10, 14, 16, icon, T_INK, T_THEME);
    cv_text_on(236 - gw + 32, ty_y(14, TY_TX), TY_TX, go, T_INK, T_THEME);
    cv_blit(0, Y_FOOT);
}
static void new_keys(int create) { new_keys_row(create ? "Create" : "Roles", create ? ICON_X_CHECK : ICON_X_RIGHT); }
/* ROLES (large type, C): a row a track, as Stage's: its numeral in its colour, the role (the text face, in its colour;
 * KEEP secondary, none dim: "No track"), under it the sound it gets and its engine (the label face); the row tinted in
 * the track's colour, the knob turning: outlined */
#define NW_ROW_Y Y_LABEL
#define NW_ROW 45
static void new_role_row(uint32_t k)
{
    uint32_t kk, src = nw.role[k] > NR_KEEP ? new_role_sound(nw.role[k], &kk) : USER_NONE, e;
    int hot = ui.hot_t && ui.hot_col == k, on = nw.role[k] > NR_KEEP, none = nw.role[k] == NR_NONE;
    uint16_t tc = none ? T_DIM : T_TRK(k), bg = ux_mix(T_BG, tc, hot ? 24 : 11);
    char nm[16], tag[6], w[40], d[2] = {(char)('1' + k), 0};
    int32_t h = NW_ROW - 2, x = 44, ex;
    if (none) {                                         /* no track: the role alone */
        nm[0] = 0;
        e = 0xFFu;
    } else if (src == USER_NONE && nw.role[k] == NR_KEEP && !template_used()) {   /* KEEP: the power-on sound */
        entry_label(TRK_DEF[k % NTRK][0], TRK_DEF[k % NTRK][1], tag, nm);
        e = TRK_DEF[k % NTRK][0];
    } else if (src == USER_NONE) {
        str_cpy(nm, nw.role[k] > NR_KEEP ? "--" : "Template", sizeof nm);
        e = 0xFFu;
    } else {
        entry_label(src, kk, tag, nm);
        e = src_engine(src, kk);
    }
    new_words(w, nm, sizeof w);                         /* "Funk Bass II" */
    cv_begin(BOX_W, (uint32_t)h, T_BG);
    if (hot) {
        cv_rrect(0, 0, BOX_W, h, 8, tc, T_BG);
        cv_rrect(2, 2, BOX_W - 4, h - 4, 6, bg, tc);
    } else {
        cv_rrect(0, 0, BOX_W, h, 8, bg, T_BG);
    }
    cv_rrect(6, h / 2 - 13, 26, 26, 7, tc, bg);         /* the numeral */
    cv_text_c(19, ty_y(h / 2, TY_TX), TY_TX, d, T_INK, tc);
    cv_text_on(x, ty_y(14, TY_TX), TY_TX, none ? "No track" : NR_NAME[nw.role[k]], on ? tc : none ? T_DIM : T_SEC, bg);
    if (!none) {
        ex = e != 0xFFu ? BOX_W - 8 - text_w(TY_LB, ENGINES[eng_idx(e)]->name) : BOX_W - 8;
        if (text_w(TY_LB, w) > ex - 8 - x)              /* (the engine goes before the sound's name is cut) */
            e = 0xFFu, ex = BOX_W - 8;
        if (e != 0xFFu)
            cv_text_on(ex, ty_y(32, TY_LB), TY_LB, ENGINES[eng_idx(e)]->name, T_MID, bg);
        cv_free_text(x, ty_y(32, TY_LB), TY_LB, w, on ? T_TEXT : T_SEC, bg, ex - 8 - x);
    }
    cv_blit(BOX_X, (uint32_t)(NW_ROW_Y + (int32_t)k * NW_ROW));
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
        const aafont_t *f = TY_HR;
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
        if (text_w(f, key) > 220 || !text_has(f, key)) f = TY_VL;   /* (the key large: "A minor") */
        if (text_w(f, key) > 220) f = TY_TX;
        cv_begin(240, H_GRAPH, T_BG);
        cv_text_c(120, ty_y(44, f), f, key, T_TEXT, T_BG);
        fmt_int(key, nw.bpm);
        str_cpy(key + str_len(key), " bpm \xB7 4/4", 14);
        cv_text_c(120, ty_y(92, TY_TX), TY_TX, key, T_MID, T_BG);
        cv_blit(0, Y_GRAPH);
        new_keys(0);
    } else {                                        /* ROLES: a row each */
        for (k = 0; k < NTRK; k++)
            new_role_row(k);
        new_keys(1);
    }
    ui.force = 0;
    if (ui.hot_t) ui.hot_t--;
}
