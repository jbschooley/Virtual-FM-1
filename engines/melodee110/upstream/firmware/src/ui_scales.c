/* SPDX-License-Identifier: GPL-3.0-only */
/* SCL > SCALES as a screen of its own (docs/design: mock/r6_pages SCALES): the track and the page in a chip, the scales
 * in the family; the families down the left (KNOB 1), the family's scales down the right (KNOB 2; the one picked a card:
 * its name larger, its notes and period, a star when a favourite), where they are as a scrollbar; under them the
 * favourite's star and the keys: OCT- back, OCT+ done (both: SCL's list). The scale turned is the track's at once, as
 * before (scale_picker.c). Included by ui_draw.c */
#define SC_SIDE_W 74                                    /* the families: rows 19 px from y 28 */
#define SC_ROW 20                                       /* the scales: 20 px, the card 50, from y 24 */
#define SC_CARD 50
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
    uint32_t rank = scale_picker_rank(), f, r, sig, fav = 0;
    char b[32];
    for (r = 0; r < SCALE_TOTAL; r++)
        fav = (fav ^ (uint32_t)scale_favorite(r)) * 16777619u + r;
    if (ui.force) {
        lcd_fill(0, 0, 240, 240, T_BG);
        scv.top = scv.side = scv.list = scv.foot = 0;
    }
    sig = total * 7u + song.sel * 131u + ux.gen * 977u + ux.pal * 31u + 1u;
    if (sig != scv.top) {                               /* the chip "2 . SCALES", the scales in the family */
        int32_t w;
        scv.top = sig;
        cv_begin(240, 22, T_BG);
        b[0] = (char)('1' + song.sel % NTRK);
        str_cpy(b + 1, " \xB7 SCALES", 12);
        w = text_w(&AF_X, b) + 14;
        cv_rrect(6, 4, w, 16, 5, T_THEME, T_BG);
        cv_text_on(13, 6, &AF_X, b, T_INK, T_THEME);
        fmt_int(b, (int32_t)total);
        str_cpy(b + str_len(b), total == 1u ? " scale" : " scales", 8);
        cv_text_r(234, 7, &AF_X, b, T_MID, T_BG);
        cv_blit(0, 0);
    }
    sig = ui.scale_family * 7u + ux.gen * 977u + ux.pal * 31u + 2u;
    if (sig != scv.side) {                              /* the families */
        scv.side = sig;
        cv_begin(SC_SIDE_W + 2, 196, T_BG);
        for (f = 0; f < SCALE_FAMILIES + 2u; f++) {
            int on = f == ui.scale_family;
            int32_t fy = 6 + (int32_t)f * 17;
            uint16_t bg = on ? T_LIFT : T_BG;
            if (on) {
                cv_rect(0, fy, SC_SIDE_W - 2, 16, T_LIFT);
                cv_rect(0, fy, 3, 16, T_THEME);
            }
            if (f == SCALE_FAMILIES + 1u) {             /* Faves: its star */
                cv_icon_on(8, fy + 2, 12, ICON_X_STAR, on ? T_THEME : T_MID, bg);
                cv_text_on(23, fy + 2, &AF_S, SC_FAMILY_WORD[f], on ? T_THEME : T_SEC, bg);
            } else {
                cv_text_on(8, fy + 2, &AF_S, SC_FAMILY_WORD[f], on ? T_THEME : T_SEC, bg);
            }
        }
        cv_rect(SC_SIDE_W, 0, 1, 192, T_LINE);
        cv_blit(0, 24);
    }
    sig = rank * 7u + scale * 1031u + total * 40503u + ui.scale_family * 13u + fav + ux.gen * 977u + ux.pal * 31u;
    if (sig != scv.list) {                              /* the scales: two before the card, three after it */
        int32_t d;
        scv.list = sig;
        lcd_fill(SC_SIDE_W + 2, 24, 234 - SC_SIDE_W - 2, 192, T_BG);
        if (!total || rank >= total) {
            cv_begin(150, 20, T_BG);
            cv_text_c(75, 3, &AF_S, !total ? (ui.scale_family == SCALE_FAMILIES + 1u ? "No favourites" : "No scales") :
                      "Not in this family", T_MID, T_BG);
            cv_blit(SC_SIDE_W + 6, 100);
        }
        for (d = -2; d <= 3 && total && rank < total; d++) {
            int32_t at = (int32_t)rank + d;
            uint32_t id;
            if (at < 0 || at >= (int32_t)total)
                continue;
            id = scale_picker_at((uint32_t)at);
            scales_name(b, id, sizeof b);
            if (!d) {                                   /* the card */
                char dt[24];
                cv_begin(240 - SC_SIDE_W - 6, SC_CARD, T_BG);
                cv_rrect(2, 2, 240 - SC_SIDE_W - 14, SC_CARD - 4, 8, T_THEME, T_BG);
                cv_rrect(3, 3, 240 - SC_SIDE_W - 16, SC_CARD - 6, 7, T_LIFT, T_THEME);
                cv_free_text(12, 8, &AF_M, b, T_TEXT, T_LIFT, scale_favorite(id) ? 104 : 124);
                if (scale_favorite(id))
                    cv_icon_on(240 - SC_SIDE_W - 32, 10, 12, ICON_X_STAR, T_THEME, T_LIFT);
                fmt_int(dt, SCALE_DEGREES[id]);
                str_cpy(dt + str_len(dt), SCALE_FAMILY[id] == 6u ? " NOTES \xB7 NON-OCTAVE" : " NOTES \xB7 OCTAVE", 20);
                cv_text_on(12, 30, &AF_X, dt, T_THEME, T_LIFT);
                cv_blit(SC_SIDE_W + 2, 80);
            } else {
                cv_begin(240 - SC_SIDE_W - 10, SC_ROW, T_BG);
                cv_free_text(12, 4, &AF_S, b, d == 1 || d == -1 ? T_SEC : T_MID, T_BG, 136);
                cv_blit(SC_SIDE_W + 2, (uint32_t)(d < 0 ? 80 + d * SC_ROW : 132 + (d - 1) * SC_ROW));
            }
        }
        if (total > 1u) {                               /* where it is */
            int32_t span = 186, th = span * 6 / (int32_t)(total > 6u ? total : 6u), ty = (span - th) * (int32_t)rank / (int32_t)(total - 1u);
            cv_begin(2, (uint32_t)span, T_LINE);
            cv_rect(0, ty, 2, th, T_THEME);
            cv_blit(236, 26);
        }
    }
    sig = (uint32_t)scale_favorite(scale) + ux.gen * 977u + ux.pal * 31u + 3u;
    if (sig != scv.foot) {                              /* the favourite's star, the keys */
        scv.foot = sig;
        cv_begin(240, 22, T_BG);
        cv_icon_on(10, 5, 12, ICON_X_STAR, scale_favorite(scale) ? T_THEME : T_DIM, T_BG);
        cv_rrect(150, 1, 40, 20, 5, T_SURF, T_BG);
        cv_icon_mid(170 - 6, 11, 12, ICON_X_UNDO, T_MID, T_SURF);
        cv_rrect(194, 1, 40, 20, 5, T_SURF, T_BG);
        cv_icon_mid(214 - 6, 11, 12, ICON_X_CHECK, T_TEXT, T_SURF);
        cv_blit(0, 218);
    }
}
