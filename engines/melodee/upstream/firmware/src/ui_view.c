/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* HOME's second page (HOME tapped on Stage; HOME there again: Stage): what plays as a picture, the chord in the middle. A wheel of the
 * twelve notes in fifths (C at the top), the song's key on it (its notes marked, its root larger): the notes sounding
 * on the tracks in use lit in their track's colour, joined into the chord's shape, the chord's root ringed; in the
 * wheel's middle the chord (its root 48 px, its quality 28 px; too wide: 28 and 20) and its notes, one note alone large;
 * let go, the last ones stay, faded; nothing played yet: the key's name there. In the corners each track in use as a
 * pulse swelling with its level; at the foot four octaves of keys (C2 .. B5), those sounding lit in their track's
 * colour. A DRUM track's notes are hits, not pitches: it pulses only. The wheel and the keys redraw when what they show
 * changes, the pulses as they move (every other frame, as Stage's band). Stage's knobs drop in over the top as on Stage
 * (ui_stage.c stage_knobs). Included by ui_draw.c */
#define VW_WHEEL_X 40                                   /* the wheel: a canvas of 160 x 172 at (40, 32) */
#define VW_WHEEL_Y 32
#define VW_WHEEL_W 160
#define VW_WHEEL_H 172
#define VW_CX 80                                        /* .. its centre in it (the screen's 120, 118) */
#define VW_CY 86
#define VW_R 50                                         /* the notes' ring */
#define VW_LR 66                                        /* their names (the label face) */
#define VW_KEYS_Y 208                                   /* the keys: rows 208 .. 237, C2 .. B5 */
#define VW_KEYS_H 30
#define VW_LO 36u
#define VW_OCT 4u
#define VW_PULSE 32                                     /* a pulse: 32 x 32 in a corner of the wheel's rows */

static struct {
    uint32_t wheel, keys, pulse[NTRK];                  /* what each part drew */
    uint8_t lvl[NTRK];                                  /* the pulses' levels (they fall ~2 dB a frame) */
    uint32_t sounding[4];                               /* the previous snapshot (additions start a new voicing) */
    uint32_t last[4];                                   /* the last voicing before releases (let go: shown faded) */
    uint8_t of[128];                                    /* .. each one's track */
} view;

static void view_reset(void) { memset(&view.wheel, 0, sizeof view.wheel + sizeof view.keys + sizeof view.pulse); }

/* x, y (1/1000) of place k on the wheel, 12 o'clock first, clockwise */
static const int16_t VW_SIN[12] = {0, 500, 866, 1000, 866, 500, 0, -500, -866, -1000, -866, -500};
static int32_t vw_x(uint32_t k, int32_t r) { return VW_CX + r * VW_SIN[k % 12u] / 1000; }
static int32_t vw_y(uint32_t k, int32_t r) { return VW_CY - r * VW_SIN[(k + 3u) % 12u] / 1000; }

/* the notes sounding now on the tracks in use (their keys down: held, a step's gate, the arp's), each one's track (the
 * selected one's first): 1 = some; none: the last ones (*faded 1) */
static int view_notes(uint32_t *on, uint8_t *of, int *faded)
{
    uint32_t k, i, n = 0, added = 0;
    memset(on, 0, 16);
    fm1_irq_off();                                      /* (one coherent ISR snapshot) */
    for (k = 0; k < trk_n(); k++) {                     /* (the selected one last: its colour wins) */
        uint32_t t = k + 1u == trk_n() ? song.sel % NTRK : k < song.sel ? k : k + 1u;
        if (drum_track(&trk[t]))
            continue;
        for (i = 0; i < NVOICE; i++) {
            const voice_t *v = &trk[t].v[i];
            if (v->active && v->gate && v->note < 128u) {
                on[v->note >> 5] |= 1u << (v->note & 31u);
                of[v->note] = (uint8_t)t;
                n++;
            }
        }
    }
    fm1_irq_on();
    for (i = 0; i < 4u; i++)
        added |= on[i] & ~view.sounding[i];
    memcpy(view.sounding, on, sizeof view.sounding);
    *faded = !n;
    /* Releases alone must not replace a chord with its last note. */
    if (added || (n && !memcmp(on, view.last, sizeof view.last))) { /* same voicing: refresh its track colours too */
        memcpy(view.last, on, sizeof view.last);
        memcpy(view.of, of, sizeof view.of);
    } else if (!n) {
        memcpy(on, view.last, sizeof view.last);
        memcpy(of, view.of, sizeof view.of);
    }
    return (on[0] | on[1] | on[2] | on[3]) != 0u;
}

/* a smooth line 2 px wide: discs along it, each blended over the canvas */
static void vw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)
{
    int32_t dx = x1 - x0, dy = y1 - y0, n = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy), i;
    for (i = 0; i <= n; i += 2)
        cv_disc(x0 + (n ? dx * i / n : 0), y0 + (n ? dy * i / n : 0), 3, c, 0);
}

/* the wheel and the chord in it */
static void view_wheel(const uint32_t *on, const uint8_t *of, int faded)
{
    uint32_t pc_of[12], pcs = 0, root = 0, bass = 0, i, k, n = 0, first = 0xFFu, prev = 0xFFu, npc = 0;
    uint32_t key = (uint32_t)TSEL->p[P_ROOT] % 12u, mask = scale_mask(TSEL);
    uint8_t note[8];
    const char *q = 0;
    char b[48];
    uint16_t tc[12];
    for (i = 0; i < 12u; i++)
        pc_of[i] = 0xFFu;
    for (i = 0; i < 128u; i++)                          /* each pitch class's track: its lowest note's */
        if ((on[i >> 5] >> (i & 31u)) & 1u) {
            if (pc_of[i % 12u] == 0xFFu) {
                pc_of[i % 12u] = of[i] % NTRK;
                npc++;
            }
            pcs |= 1u << (i % 12u);
            if (n < sizeof note) note[n] = (uint8_t)i;
            n++;
        }
    for (i = 0; i < 12u; i++)
        tc[i] = pc_of[i] == 0xFFu ? T_LINE : faded ? ux_mix(T_BG, T_TRK(pc_of[i]), 45) : T_TRK(pc_of[i]);
    if (n) {
        bass = note[0] % 12u;
        q = micro_active(TSEL) ? 0 : chord_of(pcs, bass, &root);
    }
    cv_begin(VW_WHEEL_W, VW_WHEEL_H, T_BG);
    cv_scroll = stage.out;                              /* (the knobs over its top: its words there cut on purpose) */
    cv_ring(VW_CX, VW_CY, 2 * VW_R + 1, T_LINE);
    for (k = 0; k <= 12u && npc > 1u; k++) {              /* the chord's shape: its notes joined round the wheel */
        uint32_t pc = (k * 7u) % 12u;
        if (k == 12u) {
            if (first != 0xFFu && prev != first && npc > 2u)
                vw_line(vw_x(prev, VW_R), vw_y(prev, VW_R), vw_x(first, VW_R), vw_y(first, VW_R),
                        ux_mix(T_BG, tc[(first * 7u) % 12u], faded ? 40 : 75));
            break;
        }
        if (!((pcs >> pc) & 1u))
            continue;
        if (prev != 0xFFu)
            vw_line(vw_x(prev, VW_R), vw_y(prev, VW_R), vw_x(k, VW_R), vw_y(k, VW_R),
                    ux_mix(T_BG, tc[(prev * 7u) % 12u], faded ? 40 : 75));
        else
            first = k;
        prev = k;
    }
    for (k = 0; k < 12u; k++) {                         /* the twelve notes: lit ones large, the root ringed; the key's
                                                         * marked, its root larger */
        uint32_t pc = (k * 7u) % 12u, lit = (pcs >> pc) & 1u, in = (mask >> ((pc + 12u - key) % 12u)) & 1u;
        int32_t x = vw_x(k, VW_R), y = vw_y(k, VW_R);
        if (lit)
            cv_disc(x, y, 13, tc[pc], 0);
        else
            cv_disc(x, y, pc == key ? 9 : in ? 7 : 4, in ? T_MID : T_LINE, 0);
        if (q && pc == root)
            cv_ring(x, y, 19, faded ? T_MID : T_TEXT);
        cv_text_c(vw_x(k, VW_LR), ty_y(vw_y(k, VW_LR), TY_LB), TY_LB, N_NOTE[pc],
                  lit ? (faded ? T_MID : T_TEXT) : in ? T_MID : T_DIM, T_BG);
    }
    if (n) {                                            /* in the middle: the chord, its notes */
        uint16_t c = faded ? ux_mix(T_BG, T_TEXT, 45) : T_TEXT, cn = faded ? ux_mix(T_BG, T_THEME, 55) : T_THEME;
        if (q) {                                        /* (the chord's line on row VW_CY + 4, the notes under it) */
            const aafont_t *rf = TY_HR, *qf = TY_VL;
            int32_t w, x, base = VW_CY + 4;
            for (;;) {
                w = text_w(rf, N_NOTE[root]) + 1 + text_w(qf, q);
                if (root != bass)
                    w += (q[0] ? 3 : 0) + text_w(rf, "/") + text_w(rf, N_NOTE[bass]);
                if (w <= 2 * VW_R - 8 || rf != TY_HR)
                    break;
                rf = TY_VL, qf = TY_TX;                 /* (too wide for the ring: a size smaller) */
            }
            x = VW_CX - w / 2;
            x = cv_text_on(x, base - rf->asc, rf, N_NOTE[root], c, T_BG);
            x = cv_text_on(x + (q[0] ? 1 : 0), base - qf->asc, qf, q, c, T_BG);
            if (root != bass) {
                x = cv_text_on(x + (q[0] ? 3 : 0), base - rf->asc, rf, "/", c, T_BG);
                cv_text_on(x, base - rf->asc, rf, N_NOTE[bass], c, T_BG);
            }
            notes_fit(b, note, n, TY_LB, 2 * VW_R - 4);
            cv_text_c(VW_CX, base + 6, TY_LB, b, cn, T_BG);
        } else if (n == 1u) {                           /* one note: large */
            note_name(b, note[0]);
            cv_text_c(VW_CX, ty_y(VW_CY, TY_HR), TY_HR, b, c, T_BG);
        } else {
            notes_fit(b, note, n, TY_TX, 2 * VW_R - 4);
            cv_text_c(VW_CX, ty_y(VW_CY, TY_TX), TY_TX, b, cn, T_BG);
        }
    } else {                                            /* nothing played yet: the key ("A minor") */
        const char *t = SCALE_TITLE[clamp(TSEL->p[P_SCALE], 0, SCALE_TOTAL - 1u)];
        if (!memcmp(t, "Natural ", 8))
            t += 8;
        str_cpy(b, N_NOTE[key], 4);
        str_cpy(b + str_len(b), " ", 2);
        str_cpy(b + str_len(b), t, sizeof b - str_len(b));
        if (b[str_len(N_NOTE[key]) + 1u] >= 'A' && b[str_len(N_NOTE[key]) + 1u] <= 'Z')
            b[str_len(N_NOTE[key]) + 1u] = (char)(b[str_len(N_NOTE[key]) + 1u] + 32);
        {   /* (in the ring: the text face, the label face, then on two lines: nothing cut) */
            const aafont_t *f = ty_fit(TY_TX, b, 2 * VW_R - 4);
            if (text_w(f, b) <= 2 * VW_R - 4) {
                cv_text_c(VW_CX, ty_y(VW_CY, f), f, b, T_MID, T_BG);
            } else {
                char l[TL_MAX][32];
                uint32_t n = text_lines(b, l, 3u, 2 * VW_R - 4, TY_LB), i;
                for (i = 0; i < n; i++)
                    cv_free_text(st_mid_x(VW_CX, TY_LB, l[i], 2 * VW_R - 4),
                                 ty_y(VW_CY - 9 * ((int32_t)n - 1) + 18 * (int32_t)i, TY_LB), TY_LB, l[i], T_MID, T_BG,
                                 2 * VW_R - 4);
            }
        }
    }
    cv_scroll = 0;
    if (stage.out)                                      /* (the knobs over its top: the rows under them only) */
        cv_blit_from(VW_WHEEL_X, VW_WHEEL_Y, (uint32_t)(Y_LABEL + CARD_H - VW_WHEEL_Y));
    else
        cv_blit(VW_WHEEL_X, VW_WHEEL_Y);
}

/* the keys: C2 .. B5, those sounding lit in their track's colour (let go: faded) */
static int vw_black(uint32_t n) { return (int)((0x54Au >> (n % 12u)) & 1u); }
static void view_keys(const uint32_t *on, const uint8_t *of, int faded)
{
    uint32_t n, w = 0;
    cv_begin(240, VW_KEYS_H, T_BG);
    for (n = VW_LO; n < VW_LO + 12u * VW_OCT; n++) {    /* the white keys, then the black over them */
        uint32_t lit = (on[n >> 5] >> (n & 31u)) & 1u;
        if (vw_black(n))
            continue;
        cv_rrect(8 + (int32_t)w * 8, 0, 7, VW_KEYS_H - 1, 2,
                 lit ? (faded ? ux_mix(T_BG, T_TRK(of[n]), 45) : T_TRK(of[n])) : T_SURF, T_BG);
        w++;
    }
    for (n = VW_LO, w = 0; n < VW_LO + 12u * VW_OCT; n++) {
        uint32_t lit = (on[n >> 5] >> (n & 31u)) & 1u;
        if (!vw_black(n)) {
            w++;
            continue;
        }
        cv_rect(8 + (int32_t)w * 8 - 4, 0, 7, 18, T_BG);
        cv_rrect(8 + (int32_t)w * 8 - 3, 0, 5, 17, 2,
                 lit ? (faded ? ux_mix(T_BG, T_TRK(of[n]), 45) : T_TRK(of[n])) : T_PANEL, T_BG);
    }
    cv_blit(0, VW_KEYS_Y);
}

/* track k's pulse: a ring in its colour, a disc in it as large as its level now */
static void view_pulse(uint32_t k)
{
    static const uint8_t PX[NTRK] = {6, 202, 6, 202}, PY[NTRK] = {36, 36, 170, 170};
    track_t *t = &trk[k];
    int32_t m = t->p[P_MUTE] ? 0 : meter_px(t->peak);
    uint32_t sig;
    uint16_t c = t->p[P_MUTE] ? T_DIM : T_TRK(k);
    t->peak = 0;
    if (m < view.lvl[k] - 1)
        m = view.lvl[k] - 1;                            /* (falls ~2 dB a frame) */
    view.lvl[k] = (uint8_t)(m < 0 ? 0 : m);
    sig = 1u + view.lvl[k] * 7u + (uint32_t)(t->p[P_MUTE] != 0) * 3u + ux.gen * 977u + ux.pal * 31u;
    if ((stage.out && PY[k] < Y_LABEL + CARD_H) || (!ui.force && sig == view.pulse[k]))
        return;                                         /* (under the knobs: not drawn) */
    view.pulse[k] = sig;
    cv_begin(VW_PULSE, VW_PULSE, T_BG);
    cv_ring(VW_PULSE / 2, VW_PULSE / 2, VW_PULSE - 3, ux_mix(T_BG, c, 40));
    cv_disc(VW_PULSE / 2, VW_PULSE / 2, 7 + view.lvl[k] * (VW_PULSE - 11) / (TS_MH - 2), c, 0);
    cv_blit(PX[k], PY[k]);
}

/* every frame the knobs; every other frame (the scope's) the pulses, the wheel and the keys when they changed */
static void view_draw(void)
{
    uint32_t on[4], k, sig;
    uint8_t of[128];
    int faded;
    stage_knobs();
    if (!ui.force && (ui.frame & 1u))
        return;
    for (k = 0; k < trk_n(); k++)
        view_pulse(k);
    view_notes(on, of, &faded);
    sig = 2166136261u;
    for (k = 0; k < 128u; k++)
        if ((on[k >> 5] >> (k & 31u)) & 1u)
            sig = (sig ^ (k * 4u + of[k])) * 16777619u;
    sig += (uint32_t)faded * 7u + ux.gen * 977u + ux.pal * 31u + (uint32_t)stage.out * 131u + (uint32_t)micro_active(TSEL) +
           (uint32_t)TSEL->p[P_ROOT] * 7919u + (uint32_t)TSEL->p[P_SCALE] * 104729u + scale_mask(TSEL) * 13u;
    if (ui.force || sig != view.wheel) {
        view.wheel = sig;
        view_wheel(on, of, faded);
    }
    sig += song.sel * 613u;
    if (ui.force || sig != view.keys) {
        view.keys = sig;
        view_keys(on, of, faded);
    }
}
