/* SPDX-License-Identifier: GPL-3.0-only */
/* SAVE > PRESETS, the sound browser, a screen of its own drawn to the redesign's mockup (docs/design: concept_screens
 * 2, ref/browser): two columns. At the top the track and the list in its colour, the list's length (a message in its
 * place while there is one). Left, LIST (KNOB 1): ALL, the favourites, RECENT and the categories, the one shown lifted
 * and marked in the track's colour. Right, its sounds (KNOB 2, PRESETS): the one shown in a lifted card (its engine,
 * its place), two before it and three after, fading; the place as a scrollbar; while a fast turn browses (the sound
 * loads when the knob rests) its acceleration as a badge (x4). At the foot the favourite's star (KNOB 3) and the keys:
 * OCT- back (the sound from before the browser, when it loaded one), OCT+ keep; both go back to Stage.
 * Each part remembers what it drew and redraws when that changes. Included by ui_draw.c */
#define BR_TOP_H 26                                     /* the top: rows 0 .. 25 */
#define BR_BODY_H 178                                   /* the columns: rows 26 .. 203 */
#define BR_SIDE_W 72                                    /* LIST: x 0 .. 71, its rule at 70 */
#define BR_LIST_W 166                                   /* the sounds: x 72 .. 237 (a canvas: 166 x 178 fits CV_MAX) */
#define BR_SIDE_ROWS 11u                                /* LIST rows shown */

static struct { uint32_t top, side, list, foot; } brv;  /* what each part drew */

/* LIST's names on the screen; the categories by CAT_* */
static const char *const BR_CAT[CAT_N] = {"-", "Bass", "Lead", "Pad", "Pluck", "Keys", "Drums", "FX", "Other",
                                          "Organ", "Strings", "Brass", "Wind", "Bells"};
static const char *browser_list_name(uint32_t m)
{
    return m == LM_ALL ? "All" : m == LM_FAV ? "Faves" : m == LM_RECENT ? "Recent" :
           BR_CAT[m - LM_CAT < NELEM(CAT_ORDER) ? CAT_ORDER[m - LM_CAT] : CAT_OTHER];
}
static int browser_shown_fav(void)                      /* the sound shown is a favourite */
{
    uint32_t k, src;
    if (!browse_pending())
        return preset_favorite();
    src = preset_at(brw.n, &k);
    return favorite_has(src, k);
}
static int browser_can_back(void) { return browse_loads != browse_mark || browse_pending(); }

/* the top: "2 · BASS" in the track's colour, the list's length (or the message) at the right */
static void browser_top(uint32_t total)
{
    char chip[16], r[20];
    int32_t cw;
    uint32_t sig = song.sel * 7u + list_mode() * 131u + total * 1031u + ux.gen * 977u + ux.pal * 31u +
                   (ui.msg_t ? str_hash(3u, ui.msg) : 0u);
    if (!ui.force && sig == brv.top)
        return;
    brv.top = sig;
    chip[0] = (char)('1' + song.sel % NTRK);
    str_cpy(chip + 1, " \xb7 ", 4);
    str_cpy(chip + 4, list_name(list_mode()), 10);
    cv_begin(240, BR_TOP_H, T_BG);
    cw = text_w(&AF_X, chip) + 16;
    cv_rrect(8, 6, cw, 15, 4, T_THEME, T_BG);
    cv_text_c(8 + cw / 2, 8, &AF_X, chip, T_INK, T_THEME);
    if (ui.msg_t) {
        cv_free_text(cw + 16, 8, &AF_X, ui.msg, T_ACCENT, T_BG, 224 - cw - 16);
    } else {
        fmt_int(r, (int32_t)total);
        str_cpy(r + str_len(r), total == 1u ? " sound" : " sounds", 8);
        cv_text_r(232, 8, &AF_X, r, T_MID, T_BG);
    }
    cv_blit(0, 0);
}

/* LIST: ALL, FAV, RECENT (mid), the categories (secondary), the one shown lifted with the track's mark */
static void browser_side(void)
{
    uint32_t m = list_mode(), first, r, sig = m * 7u + ux.gen * 977u + ux.pal * 31u + song.sel;
    if (!ui.force && sig == brv.side)
        return;
    brv.side = sig;
    first = (uint32_t)clamp((int32_t)m - 5, 0, (int32_t)(LM_N - BR_SIDE_ROWS));
    cv_begin(BR_SIDE_W, BR_BODY_H, T_BG);
    for (r = 0; r < BR_SIDE_ROWS && first + r < LM_N; r++) {
        uint32_t i = first + r;
        int32_t y = 3 + (int32_t)r * 16, x = 9;
        int sel = i == m;
        uint16_t bg = sel ? T_LIFT : T_BG, c = sel ? T_TEXT : i < LM_CAT ? T_MID : T_SEC;
        if (sel) {
            cv_rect(0, y, 66, 15, T_LIFT);
            cv_rect(0, y, 3, 15, T_THEME);
        }
        if (i == LM_FAV) {
            cv_icon_on(x, y + 1, 12, ICON_X_STAR, sel ? T_THEME : c, bg);
            x += 15;
        }
        cv_text_on(x, y, &AF_S, browser_list_name(i), c, bg);
    }
    cv_rect(70, 2, 1, BR_BODY_H - 2, T_LINE);
    cv_blit(0, BR_TOP_H);
}

/* the sounds: the one shown in a card, two before, three after (near: mid, far: dim); the place as a scrollbar; the
 * acceleration's badge while a fast turn browses. The loaded sound out of the list: the list's first, outlined in line */
static void browser_list(uint32_t cur, uint32_t total)
{
    static const uint8_t ROW_Y[6] = {15, 35, 0, 113, 133, 153};   /* rows -2 -1 (the card) +1 +2 +3 */
    uint32_t m = list_mode(), out = cur >= total, at = out ? 0u : cur, k, src, sig;
    int pending = browse_pending(), badge = pending && brw.x > 1u;
    int32_t r;
    char tag[6], nm[16];
    sig = at * 7u + total * 131u + m * 1031u + (uint32_t)pending * 3u + (badge ? brw.x : 0u) * 40503u + out * 17u +
          song.sel * 11u + up_gen * 7919u + ux.gen * 977u + ux.pal * 31u;
    if (!ui.force && sig == brv.list)
        return;
    brv.list = sig;
    cv_begin(BR_LIST_W, BR_BODY_H, T_BG);
    if (!total) {
        cv_text_c(80, 70, &AF_S, m == LM_FAV ? "No favourites" : m == LM_RECENT ? "Nothing loaded yet" : "No sounds",
                  T_MID, T_BG);
        cv_blit(BR_SIDE_W, BR_TOP_H);
        return;
    }
    for (r = -2; r <= 3; r++) {
        int32_t n = (int32_t)at + r;
        if (!r || n < 0 || n >= (int32_t)total)
            continue;
        src = preset_at((uint32_t)n, &k);
        entry_label(src, k, tag, nm);
        if (src == ENGI_PROPHET)                        /* (the Prophet's own name, whole) */
            str_cpy(nm, ENGINES[ENGI_PROPHET]->presets[k].name, sizeof nm);
        cv_free_text(8, ROW_Y[r + 2], &AF_S, nm[0] ? nm : tag, r == -1 || r == 1 ? T_MID : T_DIM, T_BG, 150);
    }
    {   /* the card */
        uint16_t ol = out ? T_LINE : T_THEME;
        uint32_t e;
        char pl[16];
        src = preset_at(at, &k);
        entry_label(src, k, tag, nm);
        if (src == ENGI_PROPHET)
            str_cpy(nm, ENGINES[ENGI_PROPHET]->presets[k].name, sizeof nm);
        e = src_engine(src, k);
        cv_rrect(4, 58, 150, 46, 6, ol, T_BG);
        cv_rrect(5, 59, 148, 44, 5, T_LIFT, ol);
        cv_free_text(12, 63, &AF_M, nm[0] ? nm : tag, out ? T_MID : T_TEXT, T_LIFT, 136);
        cv_text_on(12, 86, &AF_X, ENGINES[eng_idx(e)]->name, out ? T_MID : T_THEME, T_LIFT);
        fmt_int(pl, (int32_t)at + 1);
        str_cpy(pl + str_len(pl), " / ", 4);
        fmt_int(pl + str_len(pl), (int32_t)total);
        cv_text_r(148, 86, &AF_X, pl, T_MID, T_LIFT);
    }
    cv_rect(161, 4, 2, BR_BODY_H - 4, T_LINE);         /* the scrollbar: the place */
    {
        int32_t span = BR_BODY_H - 8 - 20, ty = 4 + (total > 1u ? (int32_t)at * span / (int32_t)(total - 1u) : 0);
        cv_rrect(160, ty, 4, 20, 2, out ? T_MID : T_THEME, T_BG);
    }
    if (badge) {                                        /* a fast turn: its acceleration */
        char b[4] = {'x', 0, 0, 0};
        fmt_int(b + 1, brw.x);
        cv_rrect(124, 4, 30, 15, 4, T_THEME, T_BG);
        cv_text_c(139, 6, &AF_X, b, T_INK, T_THEME);
    }
    cv_blit(BR_SIDE_W, BR_TOP_H);
}

/* the foot: the star (KNOB 3: the sound shown a favourite), the keys OCT- back and OCT+ keep */
static void browser_foot(void)
{
    int fav = browser_shown_fav(), back = browser_can_back();
    uint32_t sig = (uint32_t)fav * 3u + (uint32_t)back * 7u + ux.gen * 977u + ux.pal * 31u + song.sel * 11u;
    if (!ui.force && sig == brv.foot)
        return;
    brv.foot = sig;
    cv_begin(240, 240 - BR_TOP_H - BR_BODY_H, T_BG);
    cv_icon_on(10, 14, 12, fav ? ICON_X_STAR : ICON_X_STAR_O, fav ? T_THEME : T_DIM, T_BG);
    cv_rrect(150, 12, 36, 17, 4, T_LINE, T_BG);
    cv_rrect(151, 13, 34, 15, 3, T_SURF, T_LINE);
    cv_icon_mid(162, 20, 12, ICON_X_UNDO, back ? T_MID : T_DIM, T_SURF);
    cv_rrect(192, 12, 40, 17, 4, T_LINE, T_BG);
    cv_rrect(193, 13, 38, 15, 3, T_SURF, T_LINE);
    cv_icon_mid(206, 20, 12, ICON_X_CHECK, T_TEXT, T_SURF);
    cv_blit(0, BR_TOP_H + BR_BODY_H);
}

static void browser_draw(void)
{
    uint32_t total, cur = preset_pos(&total);
    if (ui.force)                                       /* (the canvases cover the rest) */
        lcd_fill(BR_SIDE_W + BR_LIST_W, BR_TOP_H, 240u - BR_SIDE_W - BR_LIST_W, BR_BODY_H, T_BG);
    browser_top(total);
    browser_side();
    browser_foot();
    browser_list(cur, total);                           /* (the busiest last: the SPI sends it during the next frame) */
}

/* OCT+ keeps the sound shown (a pending one loads now), OCT- goes back to the one from before the browser (the loads
 * since are one undo level: ui.c load_begin); either way back to Stage */
static void browser_key(int keep)
{
    if (keep) {
        browse_commit();
    } else if (browser_can_back()) {
        brw.on = 0;
        if (browse_loads != browse_mark)
            undo_step(0);
    }
    go_home();
}
