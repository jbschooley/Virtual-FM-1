/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* SCL > SCALES as a screen of its own, in the large type (docs/design/README.md "Large type": C, rows): at the top the
 * track's numeral and the family (KNOB 1) in its colour, the scale's place in it at the right; under it the family's
 * scales (KNOB 2): the one picked a card tinted in the track's colour (its name, its notes and period, its star), two
 * before and two after it, fading; the place as a bar at the right edge. OCT- back, OCT+ done (both: SCL's list). The
 * scale turned is the track's at once, as before (scale_picker.c). Included by ui_draw.c */
#define SC_BODY_Y 32                                    /* the scales: rows 32 .. 237 (as the browser's) */
#define SC_ROW 34
#define SC_CARD 60
static struct { uint32_t top, side, list, foot; } scv;
static const char *const SC_FAMILY_WORD[SCALE_FAMILIES + 2u] = {"All", "Western", "EDO", "Just", "Harmonic", "Historical",
                                                                 "Regional", "Non-octave", "Maqam", "Faves"};

static void scales_back(void)                           /* OCT- / OCT+: SCL's list */
{
    uint32_t i;
    for (i = 0; i < NPAGES && !str_eq(PAGES[i].title, "SCL"); i++)
        ;
    if (i < NPAGES) {
        ui.page = (uint8_t)i;
        page_entered();
    }
}
static void scales_name(char *b, uint32_t id, uint32_t n)   /* "53-tone equal division" -> "53-tone equal" */
{
    uint32_t l;
    str_cpy(b, SCALE_TITLE[id], n);
    l = str_len(b);
    if (l > 9u && str_eq(b + l - 9u, " division"))
        b[l - 9u] = 0;
}

static void scales_draw(void)
{
    uint32_t scale = (uint32_t)clamp(TSEL->p[P_SCALE], 0, SCALE_TOTAL - 1u), total = scale_picker_count();
    uint32_t rank = scale_picker_rank(), r, sig, fav = 0;
    int message = ui.msg_t && !head_toast(BPM_X + BPM_W + 6);   /* longer statuses use draw_toast */
    const aafont_t *tf = TY_TX, *lf = TY_LB;
    char b[32];
    for (r = 0; r < SCALE_TOTAL; r++)
        fav = (fav ^ (uint32_t)scale_favorite(r)) * 16777619u + r;
    if (ui.force) {
        lcd_fill(0, 0, 240, 240, T_BG);
        scv.top = scv.list = 0;
    }
    sig = total * 7u + rank * 3001u + ui.scale_family * 13u + song.sel * 131u + ux.gen * 977u + ux.pal * 31u + 1u +
          (message ? str_hash(3u, ui.msg) : 0u);
    if (head_waiting()) {
        draw_head();
    } else if (sig != scv.top) {                        /* the numeral, the family, the place */
        char d[2] = {(char)('1' + song.sel % NTRK), 0};
        int32_t cy = H_HEAD / 2;
        scv.top = sig;
        cv_begin(240, H_HEAD, T_BG);
        cv_rrect(4, cy - 11, 22, 22, 6, T_THEME, T_BG);
        cv_text_c(15, ty_y(cy, lf), lf, d, T_INK, T_THEME);
        cv_text_on(34, ty_y(cy, tf), tf, message ? ui.msg : SC_FAMILY_WORD[ui.scale_family % (SCALE_FAMILIES + 2u)],
                   message ? T_ACCENT : T_THEME, T_BG);
        if (!message && total && rank < total) {
            fmt_int(b, (int32_t)rank + 1);
            str_cpy(b + str_len(b), "/", 2);
            fmt_int(b + str_len(b), (int32_t)total);
            cv_text_r(236, ty_y(cy, lf), lf, b, T_MID, T_BG);
        }
        cv_blit(0, 0);
    }
    sig = rank * 7u + scale * 1031u + total * 40503u + ui.scale_family * 13u + fav + ux.gen * 977u + ux.pal * 31u;
    if (sig != scv.list) {                              /* the scales: two before the card, two after it */
        int32_t d, top, h = 240 - SC_BODY_Y - 2;
        scv.list = sig;
        for (top = 0; top < h; top += 124) {            /* (in two slices: a canvas holds 124 rows) */
            int32_t sh = h - top < 124 ? h - top : 124;
            cv_begin(240, (uint32_t)sh, T_BG);
            cv_oy = -top;
            if (!total || rank >= total)
                cv_text_c(120, ty_y(h / 2, tf), tf, !total ? (ui.scale_family == SCALE_FAMILIES + 1u ? "No favourites" :
                          "No scales") : "Not in this family", T_MID, T_BG);
            for (d = -2; d <= 2 && total && rank < total; d++) {
                int32_t at = (int32_t)rank + d, y = d < 0 ? (d + 2) * SC_ROW : 2 * SC_ROW + SC_CARD + (d - 1) * SC_ROW;
                uint32_t id;
                if (at < 0 || at >= (int32_t)total)
                    continue;
                uint16_t hc;                            /* (each family its shade of the track's colour) */
                id = scale_picker_at((uint32_t)at);
                hc = T_KNOB(SCALE_FAMILY[id]);
                scales_name(b, id, sizeof b);
                if (!d) {                               /* the card */
                    char dt[24];
                    uint16_t bg = ux_mix(T_BG, hc, 18);
                    y = 2 * SC_ROW;
                    cv_rrect(BOX_X, y, BOX_W, SC_CARD - 2, 8, bg, T_BG);
                    cv_free_text(12, ty_y(y + 18, ty_fit(tf, b, 222)), ty_fit(tf, b, 222), b, T_TEXT, bg, 222);
                    fmt_int(dt, SCALE_DEGREES[id]);
                    str_cpy(dt + str_len(dt), SCALE_FAMILY[id] == 6u ? " notes \xB7 non-octave" : " notes \xB7 octave", 20);
                    cv_text_on(12, ty_y(y + 42, lf), lf, dt, hc, bg);
                    cv_icon_mid(212, y + 42, 16, scale_favorite(id) ? ICON_X_STAR : ICON_X_STAR_O,
                                scale_favorite(id) ? hc : T_DIM, bg);
                    continue;
                }
                cv_free_text(12, ty_y(y + SC_ROW / 2 - 1, ty_fit(tf, b, 220)), ty_fit(tf, b, 220), b,
                             ux_mix(T_BG, hc, d == 1 || d == -1 ? 78 : 45), T_BG, 220);
            }
            if (total > 1u && rank < total) {           /* where it is */
                int32_t th = 24, ty = (int32_t)rank * (h - th) / (int32_t)(total - 1u);
                cv_rect(238, 0, 2, h, T_LINE);
                cv_rect(238, ty, 2, th, T_THEME);
            }
            cv_oy = 0;
            cv_blit(0, (uint32_t)(SC_BODY_Y + top));
        }
    }
}
