/* SPDX-License-Identifier: GPL-3.0-only */
/* Melodee's Stage (HOME) and the PATTERNS grid (SEQ > PATTERNS), drawn to the redesign's mockups (docs/design:
 * stage_columns, concept_screens).
 * Stage: the header (its chip: the engine, a kit's name); a column per track: a bar and its role (the sound's
 * category) in its colour, the sound in two lines, the engine, its pattern (the next one dashed beside it while it
 * waits for the bar), the bar playing as 4 x 4 steps, its level as a ring; the selected one lifted and outlined. Under
 * them the panel: the notes the selected track played and their chord in front of its waveform, smooth, dimmed while
 * held (a DRUM track: its lanes, lit as they hit). The first turn of a knob drops the track's four knobs (its engine's,
 * stage_page) down over the columns' tops (ST_DROP frames); while they are out the panel shows what the knob turning
 * shapes and its value; ST_KNOB_MS after the last turn they go and the columns are whole again.
 * Each part remembers what it drew: a column redraws when its sound, pattern, steps or level change, the playhead its
 * steps alone; the panel follows the scope every other frame, drawn last (the SPI, 12 MHz, sends it while the next
 * frame is worked out: a blit right after it would wait for it, lcd_window).
 */
#define ST_COL_Y Y_LABEL                                /* the columns: x CARD_X(k), y 22 .. 174, the cards' width */
#define ST_COL_H 152
#define ST_GRID_Y 84                                    /* .. their steps: 4 x 4, rows 84 .. 121 (a canvas of their own) */
#define ST_GRID_H 38
#define ST_PANEL_Y (ST_COL_Y + ST_COL_H + 4)            /* the panel: y 178 .. 238 */
#define ST_PANEL_H 60
#define ST_KNOB_MS 1500u                                /* the knobs stay this long after the last turn */
#define ST_DROP 4u                                      /* .. and drop in over this many frames */

static struct {
    uint32_t col[NTRK], grid[NTRK];                     /* each column's drawn state, its steps' */
    uint8_t hit_t[NLANE];                               /* frames a DRUM lane's name stays lit */
    uint8_t hit_trk;                                    /* .. of this track */
    uint32_t knob_ms;                                   /* the last knob turn on Stage (| 1), 0 none */
    uint8_t out, drop;                                  /* the knobs out; frames still dropping */
} stage;

static void stage_knob_touch(void) { stage.knob_ms = fm1_ms | 1u; }   /* (ui_input.c: a knob on Stage) */
static int stage_knobs_out(void) { return stage.knob_ms && fm1_ms - (stage.knob_ms & ~1u) < ST_KNOB_MS; }

/* ------------------------------------------------------- the knobs --- */
/* a knob's label: the panel's, plain words for CZ-1's (its panel says R1, L1 ..) */
static const char *stage_label(uint32_t k, const param_desc_t *d)
{
    static const char *const CZ_L[4] = {"WAVE", "DCW", "DETUNE", "VIBRATO"};
    return stage_pg.scope == SC_CZ1 ? CZ_L[k & 3u] : d->label;
}
/* a knob's value as Stage shows it: the Prophet's 0..127 panel values in % (the mockup's CUTOFF 62 %) */
static void stage_value(const page_t *sp, const param_desc_t *d, int32_t v, char *val, const char **unit)
{
    if (sp->scope == SC_P5 && d->fmt == F_INT && d->min == 0 && d->max == 127) {
        fmt_int(val, (v * 100 + 63) / 127);
        *unit = "%";
        return;
    }
    param_format(d, v, val, unit);
}
/* the four cards (while the knobs are out): a value without effect on the sound as it is drawn dim */
static void stage_columns(void)
{
    const page_t *sp = stage_page();
    uint32_t c;
    for (c = 0; c < 4u; c++) {
        int16_t *vp;
        const char *unit;
        char val[12];
        const param_desc_t *d = page_desc(sp, c, &vp);
        int dim = (sp->scope == SC_CZ1 && !cz_ed_active(cz_patch[song.sel % NTRK].raw, sp->id[c])) ||
                  (sp->scope == SC_P5 && !p5_ed_active(TSEL, sp->id[c]));
        if (!d || !vp || !d->label || d->label[0] == '-') {
            draw_column(c, "", "", "", T_THEME, -1, ICON_NONE);
            continue;
        }
        stage_value(sp, d, *vp, val, &unit);
        draw_column(c, stage_label(c, d), val, unit, dim ? T_DIM : VAL(c), d->fmt == F_ENUM && d->max < 2 ? -1 : RATIO(d, *vp),
                    ICON_NONE);
    }
}

/* ------------------------------------------------------- the panel --- */
/* the output's waveform over the panel's width, smooth: triggered on a rising zero crossing, auto-scaled, each sample
 * the mean of three, 2 px anti-aliased (gfx.c cv_vspan_aa); rows top .. top + h */
static void stage_wave(uint16_t c, int32_t top, int32_t h)
{
    int16_t *snap = scope_snap;
    uint32_t w = scope_w, i, trig = 1;
    int32_t peak = 1500, x, py = 0, cy = (top + h / 2) * 16 - 16;
    for (i = 0; i < SCOPE_N; i++) {
        snap[i] = scope_buf[(w + i) & (SCOPE_N - 1u)];
        if (snap[i] > peak) peak = snap[i];
        else if (-snap[i] > peak) peak = -snap[i];
    }
    for (i = 2; i < SCOPE_N - 240u; i++)
        if (snap[i - 1] < 0 && snap[i] >= 0) { trig = i; break; }
    for (x = 0; x < 224; x++) {
        uint32_t k = trig + (uint32_t)x;
        int32_t v = ((int32_t)snap[k - 1u] + 2 * snap[k] + snap[k + 1u]) / 4;
        int32_t y = cy - v * (h / 2 - 4) * 16 / peak;
        cv_vspan_aa(8 + x, x ? py : y, y, c);
        py = y;
    }
}

/* the notes the selected track played (live_last: its last voicing, held or let go), the chord they make, in front of
 * what is on the canvas (cv_over): the root 30 px, its quality 15 px, then the notes 11 px, on one baseline (38).
 * Held: the chord in text, the notes in the track's colour; let go: both faded into the panel. 0 none, 1 let go, 2 held */
static int stage_notes(int32_t x1)
{
    uint32_t bits[4], i, n = 0, pcs = 0, root = 0, bass, held = 0, k = song.sel % NTRK;
    uint8_t note[8];
    char b[48];
    const char *q;
    uint16_t c, cn;
    int32_t x = 10;
    fm1_irq_off();                                      /* one coherent ISR snapshot, nothing drawn meanwhile */
    for (i = 0; i < 4u; i++) {
        bits[i] = live_last[k][i];
        held |= live_held[k][i];
    }
    fm1_irq_on();
    for (i = 0; i < 128u; i++)
        if ((bits[i >> 5] >> (i & 31u)) & 1u) {
            pcs |= 1u << (i % 12u);
            if (n < sizeof note) note[n] = (uint8_t)i;
            n++;
        }
    if (!n) return 0;
    c = held ? T_TEXT : ux_mix(T_PANEL, T_THEME, 75);
    cn = held ? T_THEME : ux_mix(T_PANEL, T_THEME, 55);
    bass = note[0] % 12u;
    q = micro_active(TSEL) ? 0 : chord_of(pcs, bass, &root);
    if (q) {
        x = cv_text(x, 9, &AF_L, N_NOTE[root], c);
        x = cv_text(x + 1, 23, &AF_M, q, c);
        if (root != bass) {
            x = cv_text(x + 3, 9, &AF_L, "/", c);
            x = cv_text(x, 9, &AF_L, N_NOTE[bass], c);
        }
        x += 10;
    }
    notes_fit(b, note, n, &AF_S, x1 - x);
    cv_text(x, 27, &AF_S, b, cn);
    return (int)(1u + (held != 0u));
}
/* HOME's notes alone (tests/ui_test.c): 1 = there were notes */
static int graph_notes(void) { return stage_notes(220) != 0; }

/* a DRUM track: its eight lanes in a row at the panel's foot, a lane's name lit while it hits (drum_flash: the ISR's) */
static void stage_hits(void)
{
    uint32_t l;
    cv_over = 0;
    for (l = 0; l < NLANE; l++) {
        int32_t x = 7 + (int32_t)l * 28;
        int lit = stage.hit_t[l] != 0u;
        if (lit) {
            cv_rrect(x, 38, 26, 16, 4, T_THEME, T_PANEL);
        } else {                                        /* (an unlit lane: outlined in line) */
            cv_rrect(x, 38, 26, 16, 4, T_LINE, T_PANEL);
            cv_rrect(x + 1, 39, 24, 14, 3, T_SURF, T_LINE);
        }
        cv_text_c(x + 13, 40, &AF_X, drum_lane_abbr(TSEL, l), lit ? T_INK : T_DIM, lit ? T_THEME : T_SURF);
    }
}

/* While the knobs are out, the panel shows what the one turning shapes behind the notes: a filter's response (cutoff,
 * resonance; the Prophet's envelope amount too), an envelope (its attack .. release); 0 = nothing to show */
enum { SP_NONE, SP_FILTER, SP_ENV };
static uint32_t stage_picture(int32_t *v)
{
    const page_t *sp = stage_page();
    uint32_t hot = ui.hot_col & 3u, id = sp->id[hot], k;
    if (!stage_knobs_out())
        return SP_NONE;
    if (sp->scope == SC_P5) {
        const uint8_t *r = p5_patch_of(TSEL)->raw;
        if (id == P5_CUTOFF || id == P5_RESONANCE || id == P5_ENV_FILTER) {
            v[0] = r[P5_CUTOFF];
            v[1] = r[P5_RESONANCE];
            return SP_FILTER;
        }
        if (id == P5_RELEASE_AMP) {
            v[0] = r[P5_ATTACK_AMP]; v[1] = r[P5_DECAY_AMP]; v[2] = r[P5_SUSTAIN_AMP]; v[3] = r[P5_RELEASE_AMP];
            return SP_ENV;
        }
        return SP_NONE;
    }
    if (sp->scope != SC_ENGINE)
        return SP_NONE;
    if (id >= P_ATK && id <= P_REL) {
        for (k = 0; k < 4u; k++)
            v[k] = TSEL->p[P_ATK + k];
        return SP_ENV;
    }
    {   /* an engine's filter: its F_CUTOFF knob and the RES beside it */
        int32_t cut = -1, res = 0;
        for (k = 0; k < 4u; k++) {
            const param_desc_t *d = track_desc(TSEL, sp->id[k]);
            if (d->fmt == F_CUTOFF) cut = (int32_t)k;
            else if (str_eq(d->label, "RES")) res = TSEL->p[sp->id[k]];
        }
        if (cut < 0 || (hot != (uint32_t)cut && !str_eq(track_desc(TSEL, id)->label, "RES")))
            return SP_NONE;
        v[0] = TSEL->p[sp->id[cut]];
        v[1] = res;
        return SP_FILTER;
    }
}
/* a low-pass response: flat, the resonance's peak at the cutoff (a dotted line), then the slope; cut, res 0..127 */
static void stage_filter(int32_t cut, int32_t res, uint16_t c)
{
    int32_t x, fc = 10 + clamp(cut, 0, 127) * 200 / 127, peak = clamp(res, 0, 127) * 14 / 127;
    int32_t top = 3, bot = ST_PANEL_H - 4, y0 = 22 * 16, py = 0;
    for (x = top; x < bot; x += 5)
        cv_rect(fc, x, 1, 2, ux_mix(T_PANEL, c, 60));
    for (x = 0; x < 220; x++) {
        int32_t d = 8 + x - fc, y = y0 - peak * 16 * 64 / (64 + d * d) + (d > 0 ? d * 10 : 0);
        y = clamp(y, top * 16, bot * 16);
        cv_vspan_aa(8 + x, x ? py : y, y, c);
        py = y;
    }
}

static void stage_panel_hits(void)                      /* every frame: the DRUM lanes hit since the last one */
{
    uint8_t f[NTRK];
    uint32_t k, sel = song.sel % NTRK, l;
    fm1_irq_off();
    for (k = 0; k < NTRK; k++) {
        f[k] = drum_flash[k];
        drum_flash[k] = 0;
    }
    fm1_irq_on();
    if (stage.hit_trk != sel + 1u) {
        stage.hit_trk = (uint8_t)(sel + 1u);
        memset(stage.hit_t, 0, sizeof stage.hit_t);
    }
    for (l = 0; l < NLANE; l++)
        stage.hit_t[l] = (f[sel] >> l) & 1u ? 10u : stage.hit_t[l] ? stage.hit_t[l] - 1u : 0u;
}
static void stage_panel(void)
{
    uint32_t sel = song.sel % NTRK, drum = (uint32_t)drum_track(TSEL), pic;
    int32_t pv[4];
    int held = 0;
    cv_begin(240, ST_PANEL_H, T_BG);
    cv_rrect(4, 0, 232, ST_PANEL_H, 6, T_PANEL, T_BG);
    cv_bg = T_PANEL;
    if (!drum) {
        uint32_t i;
        fm1_irq_off();
        for (i = 0; i < 4u; i++) held |= live_held[sel][i] != 0u;
        fm1_irq_on();
    }
    pic = drum ? SP_NONE : stage_picture(pv);
    /* behind: the waveform (dimmed while notes are held, or over a drum's lanes), or what the knob turning shapes */
    if (pic == SP_FILTER)
        stage_filter(pv[0], pv[1], ux_mix(T_PANEL, T_THEME, 55));
    else if (pic == SP_ENV)
        graph_adsr_v(pv[0], pv[1], pv[2], pv[3], 4, ST_PANEL_H - 8, ux_mix(T_PANEL, T_THEME, 55));
    else
        stage_wave(held || drum ? ux_mix(T_PANEL, T_THEME, 40) : T_THEME, drum ? 2 : 4, drum ? 34 : ST_PANEL_H - 8);
    cv_over = 1;                                        /* in front: blended over it */
    if (drum) {
        stage_hits();
    } else {
        stage_notes(pic ? 176 : 228);
        if (pic) {                                      /* the value turning, at the top right */
            int16_t *vp;
            const char *unit;
            char val[12];
            const page_t *sp = stage_page();
            const param_desc_t *d = page_desc(sp, ui.hot_col & 3u, &vp);
            if (d && vp) {
                stage_value(sp, d, *vp, val, &unit);
                str_cpy(val + str_len(val), unit, 4);
                cv_text_r(228, 4, &AF_M, val, T_THEME, T_PANEL);
            }
        }
    }
    cv_over = 0;
    cv_blit(0, ST_PANEL_Y);
}

/* ------------------------------------------------------ the columns --- */
/* track k's sound as a list entry (browse.c cur_entry, which reads the selected track) */
static void stage_entry(uint32_t k, uint32_t *src, uint32_t *kk)
{
    uint8_t s = song.sel;
    song.sel = (uint8_t)k;
    cur_entry(src, kk);
    song.sel = s;
}
/* a name in two lines of w px: the first ends at the last space, '.' or '-' that fits (one long word: cut), the rest below */
static void stage_two_lines(const char *s, char *a, char *b, int32_t w)
{
    uint32_t i, cut = 0;
    for (i = 0; s[i] && i < 15u; i++)                   /* (cut: the first line's length; a space goes) */
        if (s[i] == ' ' || s[i] == '.' || s[i] == '-') {
            uint32_t n = i + (s[i] != ' ');
            str_cpy(a, s, n + 1u);
            if (n && text_w(&AF_S, a) <= w) cut = n;
        }
    if (!cut || text_w(&AF_S, s) <= w) {
        str_cpy(a, s, 16);
        b[0] = 0;
        return;
    }
    str_cpy(a, s, cut + 1u);
    str_cpy(b, s + cut + (s[cut] == ' '), 16);
}
/* column k's 4 x 4 steps (the bar playing): hits in the track's colour, the playhead larger in text, the rest in line;
 * a canvas of 50 x ST_GRID_H at the column's (2, ST_GRID_Y), clear of the level's ring under it */
static void stage_grid_at(const track_t *t, uint32_t page, uint32_t ph, uint16_t tc, uint16_t bg, int32_t ox)
{
    uint32_t i, len = (uint32_t)t->p[P_SLEN];
    for (i = 0; i < 16u; i++) {
        uint32_t si = page * 16u + i;
        const step_t *st = &seq_steps(t)[si % NSTEP];
        int32_t x = ox + 8 + (int32_t)(i % 4u) * 113 / 10, y = 6 + (int32_t)(i / 4u) * 95 / 10;
        if (si >= len)
            break;
        cv_circle(x, y, si == ph ? 6 : 5, si == ph ? T_TEXT : step_on(st) ? tc : st->time == ST_TIE ? ux_mix(bg, tc, 40) : T_LINE, bg);
    }
}
static void stage_col(uint32_t k)
{
    const track_t *t = &trk[k];
    uint32_t sel = k == song.sel, mute = t->p[P_MUTE] != 0u, arm = (song.rec >> k) & 1u, len = (uint32_t)t->p[P_SLEN];
    uint32_t page = song.playing && t->seq_idx < len ? t->seq_idx / 16u : 0u, ph = song.playing ? t->seq_idx : 0xFFFFu;
    uint32_t next = t->pattern_next < NPAT ? t->pattern_next + 1u : 0u, lvl = (uint32_t)t->p[P_LEVEL] & 127u, sig, src, kk;
    int pending = sel && browse_pending();
    uint16_t tc = mute ? T_DIM : T_TRK(k), bg = sel ? T_LIFT : T_PANEL;
    int32_t x = CARD_X(k);
    char nm[16], l1[16], l2[16], chip[4] = {'P', (char)('1' + t->pattern % NPAT), 0, 0};
    const char *en;
    if (pending) {                                      /* browsing: the sound the list shows, not yet loaded */
        char tag[6];
        src = browse_shown(&kk);
        entry_label(src, kk, tag, nm);
        if (!nm[0]) str_cpy(nm, tag, sizeof nm);
    } else {
        trk_short_name(k, nm);
        if (t->eng_req == ENGI_PROPHET)
            p5_short_name(t, nm, sizeof nm);
        stage_entry(k, &src, &kk);
    }
    en = ENGINES[eng_idx(pending ? src_engine(src, kk) : t->eng_req)]->name;
    sig = str_hash(str_hash(1u + sel * 2u + mute * 4u + arm * 8u + (uint32_t)pending * 16u + stage.out * 32u, nm), en) +
          next * 131u + lvl * 40503u + t->pattern * 7919u + len * 13u + ux.gen * 977u + ux.pal * 31u + entry_cat(src, kk) * 61u;
    if (!ui.force && sig == stage.col[k]) {
        uint32_t g = steps_hash(t) + page * 613u + ph * 104729u;
        if (g != stage.grid[k]) {                       /* the steps or the playhead: the grid only */
            stage.grid[k] = g;
            cv_begin(50, ST_GRID_H, bg);
            stage_grid_at(t, page, ph, tc, bg, 0);
            cv_blit((uint32_t)(x + 2), (uint32_t)(ST_COL_Y + ST_GRID_Y));
        }
        return;
    }
    stage.col[k] = sig;
    stage.grid[k] = steps_hash(t) + page * 613u + ph * 104729u;
    cv_begin(CARD_W, ST_COL_H, T_BG);
    if (sel) {                                          /* the selected track: lifted, outlined in its colour */
        cv_rrect(0, 0, CARD_W, ST_COL_H, 6, T_TRK(k), T_BG);
        cv_rrect(1, 1, CARD_W - 2, ST_COL_H - 2, 5, bg, T_TRK(k));
    } else {
        cv_rrect(0, 0, CARD_W, ST_COL_H, 6, bg, T_BG);
    }
    if (!stage.out) {                                   /* (under the knobs, while they are out: not drawn) */
        cv_rect(3, 0, CARD_W - 6, 3, tc);               /* the bar */
        {   /* the role: the sound's category, in NEW SONG's words (DRUMS, CHORDS); none, OTHER: the track */
            uint32_t cat = entry_cat(src, kk) % CAT_N;
            char tn[3] = {'T', (char)('1' + k), 0};
            cv_text_on(6, 6, &AF_X, cat == CAT_DRUM ? "DRUMS" : cat == CAT_KEYS ? "CHORDS" : cat && cat != CAT_OTHER ? CAT_NAME[cat] : tn, tc, bg);
        }
        if (arm)                                        /* armed: a REC dot */
            cv_circle(47, 10, 6, T_REC, bg);
        stage_two_lines(nm, l1, l2, CARD_W - 10);
        cv_free_text(6, 20, &AF_S, l1, pending ? T_ACCENT : mute ? T_DIM : T_TEXT, bg, CARD_W - 8);
        if (l2[0])
            cv_free_text(6, 34, &AF_S, l2, pending ? T_ACCENT : mute ? T_DIM : T_TEXT, bg, CARD_W - 8);
    }
    cv_text_fit(6, 49, &AF_X, en, T_MID, bg, CARD_W - 8);
    cv_rrect(6, 64, 22, 14, 4, tc, bg);                 /* the pattern playing */
    cv_text_c(17, 65, &AF_X, chip, T_INK, tc);
    if (next) {                                         /* the next one, waiting for the bar: dashed */
        char n2[3] = {'P', (char)('0' + next), 0};
        cv_dashed(31, 64, 20, 14, 3, tc);
        cv_text_c(41, 65, &AF_X, n2, tc, bg);
    }
    {   /* the steps, as stage_grid draws them on their own canvas (2 px in, from row ST_GRID_Y) */
        int32_t ox = 2;
        cv_oy = ST_GRID_Y;
        stage_grid_at(t, page, ph, tc, bg, ox);
        cv_oy = 0;
    }
    {   /* the level: a ring (KNOB_RING, 3 px, from 7:30), its dB inside */
        char v[8];
        int32_t a1 = lvl ? -KA_END + (int32_t)lvl * 2 * KA_END / 127 : -KA_END - 1;
        knob_arc(27 - KNOB_RING_R, 136 - KNOB_RING_R, KNOB_RING_R, KNOB_RING_COV, KNOB_RING_ANG, -KA_END, a1, T_LINE, tc, bg);
        if (lvl) {
            int32_t d = LEVEL_DB_X10[lvl];
            fmt_int(v, (d + (d < 0 ? -5 : 5)) / 10);
        } else {
            str_cpy(v, "OFF", sizeof v);
        }
        cv_text_c(27, 131, &AF_X, v, T_TEXT, bg);
    }
    if (stage.out)                                      /* the knobs over the tops: the rows under them only */
        cv_blit_from((uint32_t)x, ST_COL_Y, CARD_H);
    else
        cv_blit((uint32_t)x, ST_COL_Y);
}

/* the knobs: out while one turns (and ST_KNOB_MS after), dropping in over ST_DROP frames (each its cards' bottom rows,
 * more each frame: no column under them redraws); gone at once, the columns whole again */
static void stage_knobs(void)
{
    int out = stage_knobs_out();
    if (out && !stage.out) {
        stage.out = 1;
        stage.drop = ST_DROP;
    } else if (!out && stage.out) {
        stage.out = 0;
        memset(stage.col, 0, sizeof stage.col);         /* (the columns' tops back) */
        memset(ui.col, 0, sizeof ui.col);
        return;
    }
    if (!stage.out)
        return;
    if (stage.drop) {                                   /* each card whole (its value snaps, no roll), its bottom rows */
        uint32_t c;
        for (c = 0; c < 4u; c++) {
            ui.col[c][0] = 0;
            ui.roll[c].sig = (uint8_t)~ui.roll[c].sig;
        }
        stage_drop_rows = (uint8_t)((ST_DROP + 1u - stage.drop) * CARD_H / ST_DROP);
        stage_columns();
        stage_drop_rows = 0;
        stage.drop--;
        return;
    }
    stage_columns();
}

/* the BG around the columns and the panel (a full redraw: the canvases cover the rest) */
static void stage_frame(void)
{
    uint32_t k;
    for (k = 0; k <= NTRK; k++) {                       /* left of each column, right of the last */
        uint32_t x0 = k ? (uint32_t)(CARD_X(k - 1u) + CARD_W) : 0u, x1 = k < NTRK ? (uint32_t)CARD_X(k) : 240u;
        lcd_fill(x0, ST_COL_Y, x1 - x0, ST_COL_H, T_BG);
    }
    lcd_fill(0, ST_COL_Y + ST_COL_H, 240, ST_PANEL_Y - ST_COL_Y - ST_COL_H, T_BG);
    lcd_fill(0, ST_PANEL_Y + ST_PANEL_H, 240, 240u - ST_PANEL_Y - ST_PANEL_H, T_BG);
}

/* every frame the knobs; every other frame (the scope's) the columns, then the panel last */
static void stage_draw(void)
{
    uint32_t k;
    stage_panel_hits();
    stage_knobs();
    if (!ui.force && (ui.frame & 1u))
        return;
    for (k = 0; k < NTRK; k++)
        stage_col(k);
    stage_panel();
}
