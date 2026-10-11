/* SPDX-License-Identifier: GPL-3.0-only */
/* SEQ > MOTION (docs/design: mock/r6_pages MOTION, mock/r6_popups): the selected track's recorded motion as lanes, one
 * per parameter it moves on the pattern playing: the value step by step (held from one event to the next, the sound's
 * own before the first), the playhead over them. No chips: KNOB 2 the lane, KNOB 1 Play on / off, OCT+ the lane's
 * sheet (Play, Clear <lane>, Clear all motion), OCT- Stage. Included by ui_draw.c */
#define MO_MAX 8                                        /* lanes (parameters) at most */
#define MO_Y0 22                                        /* the lanes: 64 px each, 3 shown, from y 22 */
#define MO_H 64
#define MO_VIS 3
static struct { uint8_t lane, first; uint32_t sig; } mo;
static int16_t mo_cell;                                 /* (the sheet's lane: its parameter) */

static int motion_page(void) { return !ui.home && !ui.menu && !ui.layer && cur_page()->graph == GR_MOTION; }
/* the parameters with events on the selected track's pattern, by id; their number */
static uint32_t mo_lanes(uint8_t *ids)
{
    uint32_t i, j, n = 0, k = song.sel % NTRK;
    for (i = 0; i < motion.count; i++) {
        const motion_event_t *e = &motion.event[i];
        if ((e->place >> 6) != k || motion_pattern[i] != TSEL->pattern || e->param == P_RECQ ||
            (e->param >= MO_NATIVE && (uint32_t)e->value >> 8 != motion_native_tag(TSEL)))   /* (another engine's) */
            continue;
        for (j = 0; j < n && ids[j] != e->param; j++)
            ;
        if (j < n || n >= MO_MAX)
            continue;
        for (j = n++; j > 0 && ids[j - 1] > e->param; j--)
            ids[j] = ids[j - 1];
        ids[j] = e->param;
    }
    return n;
}
static uint32_t mo_events(uint32_t id)                  /* the lane's events */
{
    uint32_t i, n = 0, k = song.sel % NTRK;
    for (i = 0; i < motion.count; i++)
        n += (motion.event[i].place >> 6) == k && motion_pattern[i] == TSEL->pattern && motion.event[i].param == id;
    return n;
}
static int mo_value(uint32_t id, uint32_t step, int16_t *v)   /* the event at step, 1 if there is one */
{
    uint32_t i, place = (song.sel % NTRK) << 6 | step;
    for (i = 0; i < motion.count; i++)
        if (motion_pattern[i] == TSEL->pattern && motion.event[i].place == place && motion.event[i].param == id) {
            *v = (int16_t)(id >= MO_NATIVE ? motion.event[i].value & 255 : motion.event[i].value);
            return 1;
        }
    return 0;
}
/* a lane of the engine's own patch (motion.c MO_NATIVE + i): the Prophet's and FM6's values by their pages' names
 * (FM6's operators "OP3 OUT"), the CZ-1's tone bytes by what they belong to ("DCW 1 env") */
static const param_desc_t MO_CZ_BYTE = {"TONE", F_INT, 0, 255, 0, 0, 0};
static const param_desc_t *mo_desc(uint32_t id)         /* its range (and name) */
{
    uint32_t i = id - MO_NATIVE;
    if (id < MO_NATIVE)
        return param_desc_of(eng_idx(TSEL->eng_req), id);
#if MELODEE_PROPHET
    if (motion_native_tag(TSEL) == MO_TAG_P5 && i < NELEM(P5_PANEL) && P5_PANEL[i].label)
        return &P5_PANEL[i];
#endif
    if (motion_native_tag(TSEL) == MO_TAG_FM6)
        return i < FP_PR1 ? &FM6_OPD[i % FP_OP] : i < FP_NAME ? &FM6_GD[i - FP_PR1] : &MO_CZ_BYTE;
    return &MO_CZ_BYTE;
}
static void mo_cz_name(uint32_t i, char *b, uint32_t n, int brief)
{
    static const char *const PART[6] = {"Wave", "DCA key", "DCW key", "DCA", "DCW", "Pitch"};
    static const uint8_t END[6] = {2, 4, 6, 23, 40, 57};   /* (a line's 57 bytes: its parts' ends) */
    uint32_t line = i >= 71u, o = i - (line ? 71u : 14u), p;
    char d[2] = {(char)('1' + line), 0};
    if (i < 14u) {
        str_cpy(b, i < 4u ? "Detune" : "Vibrato", n);
        return;
    }
    for (p = 0; p < 5u && o >= END[p]; p++)
        ;
    str_cpy(b, PART[p], n);
    str_cpy(b + str_len(b), brief ? "" : " ", n - str_len(b));
    str_cpy(b + str_len(b), d, n - str_len(b));
    if (p >= 3u && !brief)
        str_cpy(b + str_len(b), " env", n - str_len(b));
}
static void mo_name(uint32_t id, char *b, uint32_t n)   /* "Cutoff" */
{
    uint32_t i = id - MO_NATIVE;
    if (id >= MO_NATIVE && motion_native_tag(TSEL) == MO_TAG_CZ) {
        mo_cz_name(i, b, n, 0);
        return;
    }
    if (id >= MO_NATIVE && motion_native_tag(TSEL) == MO_TAG_FM6 && i < FP_PR1) {   /* "OP3 OUT" */
        str_cpy(b, "OP1 ", n);
        b[2] = (char)('6' - i / FP_OP);
        str_cpy(b + 4, mo_desc(id)->label, n - 4u);
        return;
    }
    list_words(b, mo_desc(id)->label, n);
}
static int16_t mo_base(uint32_t id)                     /* the value the patch itself has */
{
    uint32_t i = id - MO_NATIVE, k = song.sel % NTRK;
    const uint8_t *b;
    if (id < MO_NATIVE)
        return motion_base_value(TSEL, id);
    b = motion_native_at(TSEL, i);
    return (int16_t)(motion_nbit(k, i) ? motion_nbase[k][i] : b ? *b : 0);
}

/* ------------------------------------------------------- the sheet --- */
static int mo_free(void)
{
    if (chain_busy()) {
        ui_message("STOP TO EDIT");
        return 0;
    }
    return 1;
}
static void mo_play_v(char *b) { str_cpy(b, motion_enabled(TSEL) ? "On" : "Off", 8); }
static void mo_play(void)
{
    if (mo_free())
        motion_set_enabled(TSEL, !motion_enabled(TSEL));
}
static void mo_clear_lane(void)
{
    if (!mo_free())
        return;
    load_begin(TSEL, UNDO_PAT);
    motion_clear_param(TSEL, (uint32_t)mo_cell);
    load_end(TSEL);
    ui_message("LANE CLEARED");
}
static char mo_clear_label[20];
static const sheet_row_t SHEET_MOTION[] = {{"Play", mo_play_v, 0, mo_play, 0}, {mo_clear_label, 0, 0, mo_clear_lane, SR_RED},
    {"Clear all motion", 0, 0, 0, CF_CLEAR_MOTION}};

static void motion_enter(void)                          /* OCT+ on a lane: its sheet */
{
    uint8_t ids[MO_MAX];
    uint32_t n = mo_lanes(ids);
    char t[20], sub[16];
    if (!n)
        return;
    mo_cell = ids[mo.lane % n];
    mo_name((uint32_t)mo_cell, t, sizeof t);
    str_cpy(mo_clear_label, "Clear ", sizeof mo_clear_label);
    mo_name((uint32_t)mo_cell, mo_clear_label + 6, sizeof mo_clear_label - 6);
    if (mo_clear_label[6] >= 'A' && mo_clear_label[6] <= 'Z')
        mo_clear_label[6] = (char)(mo_clear_label[6] + 32);   /* ("Clear cutoff") */
    fmt_int(sub, (int32_t)mo_events((uint32_t)mo_cell));
    str_cpy(sub + str_len(sub), " events", 8);
    sheet_open(t, sub, SHEET_MOTION, NELEM(SHEET_MOTION));
}
static void motion_knob(uint32_t k, int32_t s)          /* KNOB 2 the lane, KNOB 1 Play */
{
    uint8_t ids[MO_MAX];
    uint32_t n = mo_lanes(ids);
    if (k == 1u && n)
        mo.lane = (uint8_t)clamp((int32_t)mo.lane + (s > 0 ? 1 : -1), 0, (int32_t)n - 1);
    else if (k == 0u && mo_free())
        motion_set_enabled(TSEL, s > 0);
}

/* ----------------------------------------------------------- drawing --- */
/* one panel, a lane every 64 px (a faint line between): its name at the left (the lane picked: the track's colour), the
 * value step by step over x 52..230, the playhead across them all; motion off: the lines dim */
static void mo_label(uint32_t id, char *b)              /* "CUTOFF" (too wide: the knob's own, "RES") */
{
    uint32_t i;
    mo_name(id, b, 16);
    for (i = 0; b[i]; i++)
        if (b[i] >= 'a' && b[i] <= 'z') b[i] = (char)(b[i] - 32);
    if (text_w(&AF_X, b) <= 40)
        return;
    if (id >= MO_NATIVE && motion_native_tag(TSEL) == MO_TAG_CZ) {   /* "DCW1" */
        mo_cz_name(id - MO_NATIVE, b, 16, 1);
        for (i = 0; b[i]; i++)
            if (b[i] >= 'a' && b[i] <= 'z') b[i] = (char)(b[i] - 32);
    } else {
        str_cpy(b, mo_desc(id)->label, 16);
    }
}
static void motion_draw(void)
{
    uint8_t ids[MO_MAX];
    uint32_t n = mo_lanes(ids), len = (uint32_t)clamp(TSEL->p[P_SLEN], 1, NSTEP), r, s, sig, on = motion_enabled(TSEL);
    uint32_t ph = song.playing ? TSEL->seq_idx % len : 0xFFu;
    if (ui.force)
        mo.sig = 0;
    draw_head();
    pv_sound();
    if (n && mo.lane >= n)
        mo.lane = (uint8_t)(n - 1u);
    if (mo.lane < mo.first)
        mo.first = mo.lane;
    if (mo.lane >= mo.first + MO_VIS)
        mo.first = (uint8_t)(mo.lane + 1u - MO_VIS);
    sig = n * 7u + mo.lane * 131u + mo.first * 1031u + ph * 40503u + on * 977u + len * 13u + ux.gen * 7919u + ux.pal * 31u +
          song.sel * 3u + TSEL->pattern * 104729u + TSEL->eng_req * 613u;
    for (r = 0; r < motion.count; r++)
        sig = (sig ^ (uint32_t)(motion.event[r].place * 131u + motion.event[r].param * 7u + (uint16_t)motion.event[r].value)) *
              16777619u;
    if (!ui.force && sig == mo.sig)
        return;
    mo.sig = sig;
    for (r = 0; r < MO_VIS; r++) {
        uint32_t l = mo.first + r;
        int32_t top = (int32_t)r * MO_H;
        cv_begin(240, MO_H, T_BG);
        cv_oy = -top;
        cv_rrect(4, 0, 232, MO_VIS * MO_H, 6, T_PANEL, T_BG);
        cv_oy = 0;
        if (r)
            cv_rect(10, 0, 220, 1, T_LINE);
        if (!n && r == 1u)
            cv_text_c(120, 24, &AF_S, "No motion", T_MID, T_PANEL);
        if (l < n) {
            const param_desc_t *d = mo_desc(ids[l]);
            int32_t lo, hi, span, x0 = 52, x1 = 230, gy0 = 10, gh = 44, py = -1;
            int sel = l == mo.lane;
            uint16_t lc = !on ? T_DIM : sel ? T_THEME : ux_mix(T_PANEL, T_THEME, 60);
            int16_t v = mo_base(ids[l]), w = v;
            char b[16];
            lo = hi = clamp(v, d->min, d->max);         /* (the lane scaled to what it moves through, 16 at least) */
            for (s = 0; s < len; s++) {
                mo_value(ids[l], s, &w);
                lo = clamp(w, d->min, d->max) < lo ? clamp(w, d->min, d->max) : lo;
                hi = clamp(w, d->min, d->max) > hi ? clamp(w, d->min, d->max) : hi;
            }
            if (hi - lo < 16) {
                lo = (lo + hi) / 2 - 8;
                hi = lo + 16;
            }
            span = hi - lo;
            for (s = 4; s < len; s += 4)                /* (the beats, faint) */
                cv_rect(x0 + (int32_t)s * (x1 - x0) / (int32_t)len, 4, 1, MO_H - 8, ux_mix(T_PANEL, T_LINE, 50));
            mo_label(ids[l], b);
            cv_text_on(10, MO_H / 2 - 6, &AF_X, b, !on ? T_DIM : sel ? T_THEME : T_MID, T_PANEL);
            for (s = 0; s < len; s++) {                 /* the value held step by step */
                int32_t xa = x0 + (int32_t)s * (x1 - x0) / (int32_t)len, xb = x0 + (int32_t)(s + 1u) * (x1 - x0) / (int32_t)len;
                int32_t y;
                mo_value(ids[l], s, &v);
                y = gy0 + gh - 2 - (clamp(clamp(v, d->min, d->max), lo, hi) - lo) * (gh - 2) / span;
                if (py >= 0 && py != y)
                    cv_rect(xa, py < y ? py : y, 2, (py < y ? y - py : py - y) + 2, lc);
                cv_rect(xa, y, xb - xa, 2, lc);
                py = y;
            }
        }
        if (ph < len && n) {                            /* the playhead, across the lanes */
            int32_t x = 52 + (int32_t)ph * (230 - 52) / (int32_t)len;
            cv_rect(x, r ? 0 : 6, 2, MO_H - (r ? 0 : 6) - (r + 1u == MO_VIS ? 6 : 0), T_TEXT);
        }
        if (n > MO_VIS) {                               /* where the lanes are */
            int32_t sp = MO_VIS * MO_H, th = sp * MO_VIS / (int32_t)n, ty = (sp - th) * mo.first / (int32_t)(n - MO_VIS) - top;
            cv_rect(237, 0, 2, MO_H, T_LINE);
            if (ty < MO_H && ty + th > 0)
                cv_rect(237, ty, 2, th, T_THEME);
        }
        cv_blit(0, (uint32_t)(MO_Y0 + top));
    }
}
