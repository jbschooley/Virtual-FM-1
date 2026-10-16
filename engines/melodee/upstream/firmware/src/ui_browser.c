/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* SAVE > PRESETS, the sound browser, a screen of its own in the large type (docs/design/README.md "Large type": C,
 * rows). At the top the track's numeral and the list (KNOB 1: ALL, the favourites, RECENT, the categories) in its
 * colour, the sound's place in it at the right (a message in its place while there is one; a fast turn's acceleration
 * beside it, "x4"). Under it the sounds (KNOB 2, PRESETS): the one shown a card tinted in the track's colour (its
 * name, its engine, its star: KNOB 3), two before and two after it, fading; the place as a bar at the right edge.
 * OCT- back (the sound from before the browser, when it loaded one), OCT+ keep; both go back to Stage.
 * Each part remembers what it drew and redraws when that changes. Included by ui_draw.c */
#define BR_TOP_H H_HEAD                                 /* the top: rows 0 .. 29 */
#define BR_BODY_Y 32                                    /* the sounds: rows 32 .. 237 */
#define BR_ROW 34
#define BR_CARD 60

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

/* the top: the track's numeral (filled in its colour), the list in its colour, the place at the right (or the message) */
static void browser_top(uint32_t cur, uint32_t total)
{
    if (head_waiting()) { draw_head(); return; }
    char d[2] = {(char)('1' + song.sel % NTRK), 0}, r[20];
    const aafont_t *tf = TY_TX, *lf = TY_LB;
    int pending = browse_pending(), badge = pending && brw.x > 1u;
    int32_t x, cy = BR_TOP_H / 2;
    uint32_t sig = song.sel * 7u + list_mode() * 131u + total * 1031u + cur * 3001u + ux.gen * 977u + ux.pal * 31u +
                   (ui.msg_t ? str_hash(3u, ui.msg) : 0u) + (badge ? brw.x : 0u) * 40503u;
    if (!ui.force && sig == brv.top)
        return;
    brv.top = sig;
    cv_begin(240, BR_TOP_H, T_BG);
    cv_rrect(4, cy - 11, 22, 22, 6, T_THEME, T_BG);
    cv_text_c(15, ty_y(cy, lf), lf, d, T_INK, T_THEME);
    x = cv_text_on(34, ty_y(cy, tf), tf, browser_list_name(list_mode()), T_THEME, T_BG) + 10;
    if (ui.msg_t) {
        cv_free_text(x, ty_y(cy, lf), lf, ui.msg, T_ACCENT, T_BG, 236 - x);
    } else if (total) {
        fmt_int(r, (int32_t)(cur < total ? cur : 0u) + 1);
        str_cpy(r + str_len(r), "/", 2);
        fmt_int(r + str_len(r), (int32_t)total);
        x = cv_text_r(236, ty_y(cy, lf), lf, r, T_MID, T_BG);
        if (badge) {                                    /* a fast turn: its acceleration */
            char b[4] = {'x', 0, 0, 0};
            fmt_int(b + 1, brw.x);
            cv_text_r(x - 8, ty_y(cy, lf), lf, b, T_THEME, T_BG);
        }
    }
    cv_blit(0, 0);
}

/* the sounds: the one shown in a card, two before and two after (near: secondary, far: dim); the place as a bar at the
 * right edge. The loaded sound out of the list: the list's first, its card untinted */
static void browser_list(uint32_t cur, uint32_t total)
{
    uint32_t m = list_mode(), out = cur >= total, at = out ? 0u : cur, k, src, sig;
    int pending = browse_pending(), fav = browser_shown_fav();
    const aafont_t *tf = TY_TX, *lf = TY_LB;
    int32_t r, top, h = 240 - BR_BODY_Y - 2;
    char tag[6], nm[16];
    sig = at * 7u + total * 131u + m * 1031u + (uint32_t)pending * 3u + out * 17u + (uint32_t)fav * 5u +
          song.sel * 11u + up_gen * 7919u + ux.gen * 977u + ux.pal * 31u;
    if (!ui.force && sig == brv.list)
        return;
    brv.list = sig;
    for (top = 0; top < h; top += 124) {                /* (in two slices: a canvas holds 124 rows) */
        int32_t sh = h - top < 124 ? h - top : 124;
        cv_begin(240, (uint32_t)sh, T_BG);
        cv_oy = -top;
        if (!total) {
            cv_text_c(120, ty_y(h / 2, tf), tf, m == LM_FAV ? "No favourites" : m == LM_RECENT ? "Nothing loaded yet" :
                      "No sounds", T_MID, T_BG);
        } else {
            for (r = -2; r <= 2; r++) {                 /* rows -2 -1 (the card) +1 +2 */
                int32_t n = (int32_t)at + r, y = r < 0 ? (r + 2) * BR_ROW : 2 * BR_ROW + BR_CARD + (r - 1) * BR_ROW;
                if (n < 0 || n >= (int32_t)total)
                    continue;
                uint16_t hc;                            /* (each engine its shade of the track's colour) */
                src = preset_at((uint32_t)n, &k);
                hc = T_KNOB(eng_rank(src_engine(src, k)));
                entry_label(src, k, tag, nm);
                if (src == ENGI_PROPHET)                /* (the Prophet's own name, whole) */
                    str_cpy(nm, eng_preset(ENGINES[ENGI_PROPHET], k)->name, sizeof nm);
                if (!r) {                               /* the card: the name, its engine, its star */
                    uint16_t bg = out ? T_SURF : ux_mix(T_BG, hc, 18);
                    uint32_t e = src_engine(src, k);
                    y = 2 * BR_ROW;
                    cv_rrect(BOX_X, y, BOX_W, BR_CARD - 2, 8, bg, T_BG);
                    cv_free_text(12, ty_y(y + 18, ty_fit(tf, nm[0] ? nm : tag, 196)), ty_fit(tf, nm[0] ? nm : tag, 196),
                                 nm[0] ? nm : tag, out ? T_MID : T_TEXT, bg, 196);
                    cv_text_on(12, ty_y(y + 42, lf), lf, ENGINES[eng_idx(e)]->name, out ? T_MID : hc, bg);
                    cv_icon_mid(212, y + 42, 16, fav ? ICON_X_STAR : ICON_X_STAR_O, fav ? hc : T_DIM, bg);
                    continue;
                }
                cv_free_text(12, ty_y(y + BR_ROW / 2 - 1, ty_fit(tf, nm[0] ? nm : tag, 220)), ty_fit(tf, nm[0] ? nm : tag, 220),
                             nm[0] ? nm : tag, ux_mix(T_BG, hc, r == -1 || r == 1 ? 78 : 45), T_BG, 220);
            }
            cv_rect(238, 0, 2, h, T_LINE);              /* the place */
            {
                int32_t th = 24, ty = total > 1u ? (int32_t)at * (h - th) / (int32_t)(total - 1u) : 0;
                cv_rect(238, ty, 2, th, out ? T_MID : T_THEME);
            }
        }
        cv_oy = 0;
        cv_blit(0, (uint32_t)(BR_BODY_Y + top));
    }
}

static void browser_draw(void)
{
    uint32_t total, cur = preset_pos(&total);
    if (ui.force)                                       /* (the canvases cover the rest) */
        lcd_fill(0, BR_TOP_H, 240, BR_BODY_Y - BR_TOP_H, T_BG);
    browser_top(cur, total);
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
