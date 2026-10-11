/* SPDX-License-Identifier: GPL-3.0-only */
/* SEQ > PATTERNS and SEQ > SONG, screens of their own drawn to the redesign's mockups (docs/design: concept_screens 3
 * and 4, ref/patterns, ref/song).
 * PATTERNS (SEQ held, or its page): the header, then a row per track: its number in a circle (the selected one
 * filled), a cell per pattern: the one playing filled in the track's colour (how far through it at its foot), the one
 * waiting for the bar dashed, the others with something in them as three bars (their thirds' notes), empty ones an
 * outline. At the foot the song as its sections' letters (the rows with the same four patterns share one), the row
 * playing (else the one SONG picked) lit; no song yet: the rows the jam logged. KNOB k queues track k's pattern.
 * OCT+ the selected track's pattern's sheet (Copy to.., Delete pattern). Copy to: the place it goes framed in the
 * theme's colour on the track's row (the first empty one first), any knob moves it, OCT+ copies (over a pattern in
 * use: the question first), OCT- leaves.
 * SONG: its title, its length, the jam at the right (logging while patterns play without a song; REC held: TAKE);
 * the sections as columns (their letters; four at a time around the row picked, one more to add a row), a track's
 * patterns as blocks in its colour, one pattern over sections in a row as one block; the row picked marked, the
 * selected track's block in it outlined; a song playing: its playhead. At the foot the sections and bars, the row's
 * letter and repeats, how far the song has played. KNOB 1 the row, 2 the track, 3 its pattern in the row, 4 the
 * row's repeats; OCT+ plays, REC held takes the jam (ui_input.c).
 * Each part remembers what it drew. Included by ui_draw.c */

static int ptc_on(void);                                /* PATTERNS' Copy to (below) */
static uint32_t ptc_next(uint32_t from, int32_t dir);

/* ------------------------------------------------------------ shared --- */
/* pattern b of track k holds something: a note, a hit, a recorded note's step */
static int pattern_used(uint32_t k, uint32_t b)
{
    const step_t *s = b == trk[k].pattern ? trk[k].step : pattern_at(k, b)->step;
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        if (step_on(&s[i]) || (s[i].flags & SF_RECORDED))
            return 1;
    return 0;
}
/* pattern b of track k holds nothing at all: no note, no hit, no motion (Copy to there asks nothing) */
static int pattern_empty(uint32_t k, uint32_t b)
{
    uint32_t i;
    if (pattern_used(k, b))
        return 0;
    for (i = 0; i < motion.count; i++)
        if ((motion.event[i].place >> 6) == k && motion_pattern[i] == b)
            return 0;
    return 1;
}
/* track k's pattern b: its steps, length and division (the one playing: the track's own; its bank lags behind) */
static const step_t *pat_steps(uint32_t k, uint32_t b, uint32_t *len, uint32_t *div)
{
    const track_t *t = &trk[k];
    const pattern_t *p = pattern_at(k, b);
    if (b == t->pattern) {
        *len = (uint32_t)t->p[P_SLEN];
        *div = (uint32_t)t->p[P_SDIV];
        return t->step;
    }
    *len = (uint32_t)p->timing[0];
    *div = (uint32_t)p->timing[1];
    return p->step;
}
/* its bars, whole ones up (N_DIV: a step of 1/4 .. 4BAR; steps in 4 bars) */
static uint32_t pat_bars(uint32_t k, uint32_t b)
{
    static const uint8_t SPB4[] = {16, 32, 64, 128, 48, 96, 8, 4, 2, 1};
    uint32_t len, div, s;
    pat_steps(k, b, &len, &div);
    s = SPB4[div < NELEM(SPB4) ? div : 2u];
    len = len ? len : 1u;
    return (len * 4u + s - 1u) / s;
}
/* row r's letter: its four patterns' set; A the first set the song plays, B the next new one .. */
static char row_letter(const uint8_t (*pat)[NTRK], uint32_t r)
{
    uint32_t f, i, j, n = 0;
    for (f = 0; f < r && memcmp(pat[f], pat[r], NTRK); f++)
        ;
    for (i = 0; i < f; i++) {
        for (j = 0; j < i && memcmp(pat[j], pat[i], NTRK); j++)
            ;
        n += j == i;
    }
    return (char)('A' + n % 26u);
}
static uint32_t song_hash(const uint8_t (*pat)[NTRK], const uint8_t *rep, uint32_t n)
{
    uint32_t h = 2166136261u, i, k;
    for (i = 0; i < n; i++)
        for (k = 0; k <= NTRK; k++)
            h = (h ^ (k < NTRK ? pat[i][k] : 64u + rep[i])) * 16777619u;
    return h ^ n * 40503u;
}
static uint32_t fnv1(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }

/* ---------------------------------------------------------- PATTERNS --- */
#define PT_NUM_Y H_HEAD                                 /* the patterns' numbers: rows 18 .. 41 */
#define PT_ROW_Y 42                                     /* a track a row: 38 px, to 194 */
#define PT_ROW_H 38
#define PT_SONG_Y (PT_ROW_Y + 4 * PT_ROW_H)             /* the song: 194 .. 239 */
#define PT_CELL_X(b) (30 + 26 * (int32_t)(b))           /* a cell: 23 x 32 at row y 2 */
static struct { uint32_t num, row[NTRK], prog[NTRK], song; } ptv;

static uint32_t pt_prog(const track_t *t)               /* px through the pattern playing (of 17), 0 stopped */
{
    uint32_t len = (uint32_t)t->p[P_SLEN];
    return song.playing && len ? (t->seq_idx % len + 1u) * 17u / len : 0u;
}
static void pt_prog_bar(int32_t x, int32_t y, uint32_t prog, uint16_t tc)   /* at the playing cell's foot */
{
    uint16_t tr = ux_mix(tc, T_BG, 35);
    cv_rrect(x, y, 17, 3, 1, tr, tc);
    cv_rrect(x, y, prog < 2u ? 2 : (int32_t)prog, 3, 1, T_BG, tr);
}
/* pattern b's notes as three bars, its thirds (up to 14 px; none: a dot) */
static void pt_bars(uint32_t k, uint32_t b, int32_t x, int32_t y, uint16_t c)
{
    uint32_t len, div, i, n[3] = {0, 0, 0}, m = 1;
    const step_t *s = pat_steps(k, b, &len, &div);
    len = len > NSTEP ? NSTEP : len ? len : 1u;
    for (i = 0; i < len; i++)
        if (step_on(&s[i]))
            n[i * 3u / len]++;
    for (i = 0; i < 3u; i++)
        if (n[i] > m) m = n[i];
    for (i = 0; i < 3u; i++) {
        int32_t h = n[i] ? 4 + (int32_t)(n[i] * 10u / m) : 2;
        cv_rrect(x + 5 + 5 * (int32_t)i, y + 26 - h, 3, h, 1, c, T_SURF);
    }
}
static void pt_numbers(void)
{
    uint32_t b, sig = ux.gen * 977u + ux.pal * 31u + 1u;
    if (!ui.force && sig == ptv.num)
        return;
    ptv.num = sig;
    cv_begin(240, PT_ROW_Y - PT_NUM_Y, T_BG);
    for (b = 0; b < NPAT; b++) {
        char n[2] = {(char)('1' + b), 0};
        cv_text_c(PT_CELL_X(b) + 11, 11, &AF_X, n, T_DIM, T_BG);
    }
    cv_blit(0, PT_NUM_Y);
}
static void pt_row(uint32_t k)
{
    const track_t *t = &trk[k];
    uint16_t tc = t->p[P_MUTE] ? T_DIM : T_TRK(k);
    uint32_t b, prog = pt_prog(t), sel = k == song.sel, h;
    int32_t y0 = PT_ROW_Y + (int32_t)k * PT_ROW_H;
    char n[2] = {(char)('1' + k), 0};
    h = fnv1(fnv1(fnv1(2166136261u, steps_hash(t)), t->pattern_gen), t->pattern + 16u * t->pattern_next + 8192u * sel +
            16384u * (uint32_t)(t->p[P_MUTE] != 0) + 65536u * (uint32_t)t->p[P_SLEN]);
    h = fnv1(h, ux.gen * 977u + ux.pal * 31u + (uint32_t)song.playing);
    if (ptc_on() && k == ui.ptc_trk)                    /* Copy to: where it goes */
        h = fnv1(h, 1u + ui.ptc_src + 16u * ui.ptc_dst);
    if (!ui.force && h == ptv.row[k]) {
        if (prog != ptv.prog[k]) {                      /* playing: the bar at the cell's foot only */
            ptv.prog[k] = prog;
            cv_begin(17, 3, tc);
            pt_prog_bar(0, 0, prog, tc);
            cv_blit((uint32_t)(PT_CELL_X(t->pattern) + 3), (uint32_t)(y0 + 2 + 25));
        }
        return;
    }
    ptv.row[k] = h;
    ptv.prog[k] = prog;
    cv_begin(240, PT_ROW_H, T_BG);
    if (sel) {                                          /* the track: its number in a circle, the selected one filled */
        cv_circle(14, 18, 19, tc, T_BG);
        cv_text_c(14, 13, &AF_X, n, T_INK, tc);
    } else {
        cv_ring(14, 18, 19, tc);
        cv_text_c(14, 13, &AF_X, n, tc, T_BG);
    }
    for (b = 0; b < NPAT; b++) {
        int32_t x = PT_CELL_X(b), y = 2;
        int dst = ptc_on() && k == ui.ptc_trk && b == ui.ptc_dst;
        if (dst)                                        /* Copy to: the place, framed in the theme's colour */
            cv_rrect(x - 2, y - 2, 27, 36, 6, T_THEME, T_BG);
        if (b == t->pattern) {                          /* playing: filled, how far through it */
            cv_rrect(x, y, 23, 32, 4, tc, dst ? T_THEME : T_BG);
            if (prog)
                pt_prog_bar(x + 3, y + 25, prog, tc);
            continue;
        }
        if (pattern_used(k, b) || b == t->pattern_next) {
            cv_rrect(x, y, 23, 32, 4, T_SURF, dst ? T_THEME : T_BG);
            if (pattern_used(k, b))
                pt_bars(k, b, x, y, ux_mix(T_SURF, tc, 60));
        } else {                                        /* empty: an outline */
            cv_rrect(x, y, 23, 32, 4, dst ? T_BG : T_LINE, dst ? T_THEME : T_BG);
            cv_rrect(x + 1, y + 1, 21, 30, 3, T_BG, dst ? T_BG : T_LINE);
            if (dst)                                    /* (Copy to: an empty place) */
                cv_text_c(x + 11, y + 10, &AF_S, "+", T_THEME, T_BG);
        }
        if (b == t->pattern_next)                       /* waiting for the bar: dashed */
            cv_dashed(x, y, 23, 32, 4, tc);
    }
    cv_blit(0, (uint32_t)y0);
}
/* the song's sections as letters (six around the row playing, else the one SONG picked: lit); no song: the jam's */
static void pt_song(void)
{
    uint32_t n = chain_config.count, jamrows = !n && jam.n, cur, i, first, sig;
    const uint8_t (*pat)[NTRK] = jamrows ? (const uint8_t (*)[NTRK])jam.pat : (const uint8_t (*)[NTRK])chain_patterns;
    uint8_t rep[CHAIN_ROWS];
    if (jamrows)
        n = jam.n;
    for (i = 0; i < n; i++)
        rep[i] = jamrows ? jam.rep[i] : chain_config.row[i].repeat;
    cur = chain.running ? chain.row : jamrows ? n - 1u : ui.song_row;
    if (n && cur >= n)
        cur = n - 1u;
    sig = song_hash(pat, rep, n) + cur * 7919u + jamrows * 3u + ux.gen * 977u + ux.pal * 31u;
    if (!ui.force && sig == ptv.song)
        return;
    ptv.song = sig;
    cv_begin(240, 240 - PT_SONG_Y, T_BG);
    cv_text_on(8, 19, &AF_X, jamrows ? "JAM" : "SONG", T_MID, T_BG);
    if (!n) {
        cv_text_on(44, 19, &AF_X, "--", T_DIM, T_BG);
    } else {
        first = cur > 2u ? cur - 2u : 0u;
        if (n > 6u && first > n - 6u)
            first = n - 6u;
        if (n <= 6u)
            first = 0;
        for (i = first; i < first + 6u && i < n; i++) {
            int32_t x = 44 + 31 * (int32_t)(i - first);
            int on = i == cur;
            char l[2] = {row_letter(pat, i), 0};
            cv_rrect(x, 17, 28, 16, 4, on ? T_TEXT : T_SURF, T_BG);
            cv_text_c(x + 14, 19, &AF_X, l, on ? T_BG : T_MID, on ? T_TEXT : T_SURF);
        }
    }
    cv_blit(0, PT_SONG_Y);
}
static void patterns_draw(void)
{
    uint32_t k;
    pt_numbers();
    pt_song();
    for (k = 0; k < NTRK; k++)
        pt_row(k);
}
/* a pattern queued on any track (PATTERNS' OCT- clears them before it leaves) */
static int patterns_queued(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        if (trk[k].pattern_next < NPAT)
            return 1;
    return 0;
}
/* KNOB k: track k's next pattern (stopped: at once; playing: at the end of its bar; a song plays: refused); Copy to:
 * any knob the place */
static void patgrid_edit(uint32_t k, int32_t steps)
{
    track_t *t = &trk[k % NTRK];
    if (ptc_on()) {
        ui.ptc_dst = (uint8_t)ptc_next(ui.ptc_dst, steps > 0 ? 1 : -1);
        ui.force = 1;
        return;
    }
    uint32_t from = t->pattern_next < NPAT ? t->pattern_next : t->pattern;
    if (pattern_request(t, (uint32_t)clamp((int32_t)from + (steps > 0 ? 1 : -1), 0, NPAT - 1u)))
        ui_message("STOP SONG TO SWITCH");
    ui.force = 1;
}

/* ----------------------------------------------- PATTERNS: Copy to --- */
static void confirm_open(uint32_t kind, uint32_t trk);  /* ui_input.c */
static int ptc_on(void) { return ui.ptc_on && !ui.home && cur_page()->graph == GR_PATGRID && ui.ptc_trk == song.sel; }
static uint32_t ptc_next(uint32_t from, int32_t dir)    /* the next place that is not the pattern copied (ends kept) */
{
    int32_t b = (int32_t)from + dir;
    if (b == (int32_t)ui.ptc_src)
        b += dir;
    return b < 0 || b >= (int32_t)NPAT ? from : (uint32_t)b;
}
static void ptc_start(void)                             /* the pattern sheet's Copy to: the selected track's pattern */
{
    uint32_t k = song.sel % NTRK, src = trk[k].pattern, i, b = (src + 1u) % NPAT;
    if (chain_busy()) {
        ui_message("STOP SONG TO COPY");
        return;
    }
    if (pattern_empty(k, src)) {
        ui_message("NOTHING TO COPY");
        return;
    }
    for (i = 1; i < NPAT && !pattern_empty(k, (src + i) % NPAT); i++)   /* the first empty place after it */
        ;
    if (i < NPAT)
        b = (src + i) % NPAT;
    ui.ptc_on = 1;
    ui.ptc_trk = (uint8_t)k;
    ui.ptc_src = (uint8_t)src;
    ui.ptc_dst = (uint8_t)b;
    ui.force = 1;
}
static void ptc_paste(int asked)                        /* OCT+: copy it there (over one in use: asked first) */
{
    uint32_t k = ui.ptc_trk % NTRK;
    int rc;
    if (!asked && !pattern_empty(k, ui.ptc_dst)) {
        confirm_open(CF_PASTE_PAT, k);
        return;
    }
    rc = pattern_copy(&trk[k], ui.ptc_src, ui.ptc_dst);
    if (!rc)
        ui.ptc_on = 0;
    ui_message(!rc ? "PATTERN COPIED" : rc == 2 ? "PATTERN DATA FULL" : chain_busy() ? "STOP SONG TO COPY" :
               "PATTERN IS PLAYING");
    ui.force = 1;
}

/* -------------------------------------------------------------- SONG --- */
#define SG_TOP_H 26                                     /* the title: rows 0 .. 25 */
#define SG_LAB_H 18                                     /* the sections' letters: 26 .. 43 */
#define SG_ROW_Y (SG_TOP_H + SG_LAB_H)                  /* a track a row: 36 px, 44 .. 187 */
#define SG_ROW_H 36
#define SG_FOOT_Y (SG_ROW_Y + 4 * SG_ROW_H)             /* the foot: 188 .. 239 */
#define SG_COL_W 56                                     /* a section: x 8 + 56 j, four shown */
#define SG_COLS 4u
static struct { uint32_t top, lab, row[NTRK], foot; } sgv;
static struct { uint32_t first, ncols, sel, total, bars, play; int32_t px; } sg;   /* this frame's */

static uint32_t sg_row_bars(uint32_t r)                 /* a row's bars: its longest pattern's (x its repeats) */
{
    uint32_t k, b = 1;
    for (k = 0; k < NTRK; k++)
        if (pat_bars(k, chain_patterns[r][k]) > b)
            b = pat_bars(k, chain_patterns[r][k]);
    return b * chain_config.row[r].repeat;
}
/* the window (four sections around the row picked or playing), the song's bars, the playhead's x (-1 none) */
static void sg_frame(void)
{
    uint32_t n = chain_config.count, r;
    sg.ncols = n < CHAIN_ROWS ? n + 1u : CHAIN_ROWS;     /* (and one to add a row) */
    sg.sel = ui.song_row < sg.ncols ? ui.song_row : sg.ncols - 1u;
    r = chain.running ? chain.row : sg.sel;
    sg.first = r > 1u ? r - 1u : 0u;
    if (sg.ncols > SG_COLS && sg.first > sg.ncols - SG_COLS)
        sg.first = sg.ncols - SG_COLS;
    if (sg.ncols <= SG_COLS)
        sg.first = 0;
    for (r = 0, sg.bars = 0, sg.play = 0; r < n; r++) {
        uint32_t b = sg_row_bars(r);
        if (chain.running && r < chain.row)
            sg.play += b;
        sg.bars += b;
    }
    sg.px = -1;
    if (chain.running && chain.row < n) {               /* how far through its row, its repeats */
        const track_t *t = &trk[0];
        uint32_t len = t->p[P_SLEN] ? (uint32_t)t->p[P_SLEN] : 1u, rep = chain_config.row[chain.row].repeat;
        uint32_t done = rep > chain.remaining ? rep - chain.remaining : 0u, at = done * len + t->seq_idx % len;
        if (chain.row >= sg.first && chain.row < sg.first + SG_COLS)
            sg.px = 8 + SG_COL_W * (int32_t)(chain.row - sg.first) + (int32_t)(at * SG_COL_W / (rep * len));
        sg.play += sg_row_bars(chain.row) * at / (rep * len);
    }
}
static void sg_lines(int32_t y, int32_t h)              /* the sections' rules and the playhead, rows y .. y + h */
{
    uint32_t j;
    for (j = 0; j < SG_COLS && sg.first + j < sg.ncols; j++)
        cv_rect(8 + SG_COL_W * (int32_t)j, y, 1, h, T_LINE);
}
static void sg_head(void)
{
    char b[24], t[12];
    uint32_t secs = sg.bars * 240u / (uint32_t)(song.g[G_BPM] > 0 ? song.g[G_BPM] : 120), jam_on = song.playing && !chain.running;
    uint32_t sig = secs * 7u + jam.n * 131u + jam_on * 3u + (uint32_t)(ui.act == 4u) * 5u + ux.gen * 977u + ux.pal * 31u +
                   (ui.msg_t ? str_hash(3u, ui.msg) : 0u);
    int32_t x;
    if (!ui.force && sig == sgv.top)
        return;
    sgv.top = sig;
    cv_begin(240, SG_TOP_H, T_BG);
    x = cv_text_on(8, 5, &AF_S, "Song", T_TEXT, T_BG);
    if (sg.bars) {                                      /* its length at the tempo */
        str_cpy(t, "\xb7 ", 4);
        fmt_int(t + 2, (int32_t)(secs / 60u));
        str_cpy(t + str_len(t), secs % 60u < 10u ? ":0" : ":", 3);
        fmt_int(t + str_len(t), (int32_t)(secs % 60u));
        cv_text_on(x + 6, 7, &AF_X, t, T_MID, T_BG);
    }
    if (ui.msg_t) {
        cv_free_text(100, 7, &AF_X, ui.msg, T_ACCENT, T_BG, 132);
    } else if (jam_on || jam.n) {                       /* the jam: logging (a dot), its rows */
        if (jam_on) {
            str_cpy(b, "capturing jam", sizeof b);
        } else {
            str_cpy(b, "jam \xb7 ", 8);
            fmt_int(b + str_len(b), jam.n);
            str_cpy(b + str_len(b), jam.n > 1u ? " rows" : " row", 6);
        }
        x = cv_text_r(232, 7, &AF_X, b, jam_on ? ux_mix(T_REC, T_TEXT, 30) : T_MID, T_BG);
        if (jam_on)
            cv_circle(232 - text_w(&AF_X, b) - 8, 12, 7, T_REC, T_BG);
    }
    cv_blit(0, 0);
}
static void sg_labels(void)                             /* the sections' letters (repeats after them), the playhead */
{
    uint32_t j, sig = sg.first * 7u + sg.ncols * 131u + sg.sel * 1031u + (uint32_t)(sg.px + 1) * 40503u + ux.gen * 977u +
                      ux.pal * 31u;
    for (j = 0; j < chain_config.count; j++)
        sig = fnv1(sig, chain_config.row[j].repeat + 32u * (uint32_t)row_letter((const uint8_t (*)[NTRK])chain_patterns, j));
    if (!ui.force && sig == sgv.lab)
        return;
    sgv.lab = sig;
    cv_begin(240, SG_LAB_H, T_BG);
    sg_lines(2, SG_LAB_H - 2);
    for (j = 0; j < SG_COLS && sg.first + j < sg.ncols; j++) {
        uint32_t r = sg.first + j;
        int32_t x = 11 + SG_COL_W * (int32_t)j;
        int sel = r == sg.sel;
        char l[8] = {'+', 0};
        if (r < chain_config.count) {
            l[0] = row_letter((const uint8_t (*)[NTRK])chain_patterns, r);
            if (chain_config.row[r].repeat > 1u) {
                l[1] = ' ';
                l[2] = 'x';
                fmt_int(l + 3, chain_config.row[r].repeat);
            }
        }
        x = cv_text_on(x, 1, &AF_X, l, sel ? T_TEXT : r < chain_config.count ? T_MID : T_DIM, T_BG);
        if (sel)                                        /* the row picked: marked in the track's colour */
            cv_rrect(11 + SG_COL_W * (int32_t)j, 14, 12, 2, 1, T_THEME, T_BG);
        (void)x;
    }
    if (sg.px >= 0)
        cv_rect(sg.px, 14, 2, SG_LAB_H - 14, T_TEXT);
    cv_blit(0, SG_TOP_H);
}
/* track k's blocks: a pattern over sections in a row as one (the selected track's block in the row picked alone,
 * outlined); an empty pattern: nothing (picked: dashed) */
static void sg_row(uint32_t k)
{
    const track_t *t = &trk[k];
    uint16_t tc = t->p[P_MUTE] ? T_DIM : T_TRK(k);
    uint32_t j, h = fnv1(2166136261u, sg.first + 16u * sg.ncols + 256u * sg.sel + 4096u * (k == song.sel) +
                                     65536u * (uint32_t)(t->p[P_MUTE] != 0));
    h = fnv1(fnv1(h, (uint32_t)(sg.px + 1)), ux.gen * 977u + ux.pal * 31u + t->pattern_gen);
    for (j = 0; j < SG_COLS && sg.first + j < chain_config.count; j++)
        h = fnv1(h, chain_patterns[sg.first + j][k] + 16u * (uint32_t)pattern_used(k, chain_patterns[sg.first + j][k]));
    if (!ui.force && h == sgv.row[k])
        return;
    sgv.row[k] = h;
    cv_begin(240, SG_ROW_H, T_BG);
    sg_lines(0, SG_ROW_H);
    for (j = 0; j < SG_COLS && sg.first + j < chain_config.count; ) {
        uint32_t r = sg.first + j, p = chain_patterns[r][k], e = j + 1u, pick = k == song.sel && r == sg.sel;
        int used = pattern_used(k, p);
        int32_t x = 9 + SG_COL_W * (int32_t)j, w;
        char l[4] = {'P', (char)('1' + p % NPAT), 0, 0};
        while (!pick && used && e < SG_COLS && sg.first + e < chain_config.count &&
               chain_patterns[sg.first + e][k] == p && !(k == song.sel && sg.first + e == sg.sel))
            e++;
        w = SG_COL_W * (int32_t)(e - j) - 2;
        if (used) {
            if (pick)                                   /* the selected track's, in the row picked: outlined */
                cv_rrect(x - 1, 1, w + 2, 30, 6, T_TEXT, T_BG);
            cv_rrect(x, 2, w, 28, 5, tc, pick ? T_TEXT : T_BG);
            cv_text_on(x + 5, 11, &AF_X, l, T_INK, tc);
        } else if (pick) {
            cv_dashed(x, 2, w, 28, 5, tc);
            cv_text_on(x + 5, 11, &AF_X, l, tc, T_BG);
        }
        j = e;
    }
    if (sg.px >= 0)
        cv_rect(sg.px, 0, 2, SG_ROW_H, T_TEXT);
    cv_blit(0, (uint32_t)(SG_ROW_Y + (int32_t)k * SG_ROW_H));
}
static void sg_foot(void)                               /* the sections and bars, the row's letter x repeats, how far */
{
    char b[32];
    uint32_t n = chain_config.count, r = chain.running ? chain.row : sg.sel;
    uint32_t sig = n * 7u + sg.bars * 131u + sg.play * 1031u + r * 40503u + (uint32_t)(sg.px + 1) * 613u + ux.gen * 977u +
                   ux.pal * 31u + (r < n ? chain_config.row[r].repeat * 7919u : 0u) + sg.first * 17u + sg.ncols;
    if (!ui.force && sig == sgv.foot)
        return;
    sgv.foot = sig;
    cv_begin(240, 240 - SG_FOOT_Y, T_BG);
    sg_lines(0, 4);
    if (sg.px >= 0)
        cv_rect(sg.px, 0, 2, 6, T_TEXT);
    if (!n) {
        cv_text_on(8, 17, &AF_S, "No sections yet", T_MID, T_BG);
    } else {
        fmt_int(b, (int32_t)n);
        str_cpy(b + str_len(b), n > 1u ? " sections \xb7 " : " section \xb7 ", 14);
        fmt_int(b + str_len(b), (int32_t)sg.bars);
        str_cpy(b + str_len(b), sg.bars > 1u ? " bars" : " bar", 6);
        cv_text_on(8, 17, &AF_S, b, T_MID, T_BG);
        if (r < n) {
            b[0] = row_letter((const uint8_t (*)[NTRK])chain_patterns, r);
            str_cpy(b + 1, " x", 3);
            fmt_int(b + 3, chain_config.row[r].repeat);
            cv_text_r(232, 17, &AF_S, b, T_TEXT, T_BG);
        }
    }
    cv_rrect(8, 36, 224, 6, 3, T_SURF, T_BG);
    if (chain.running && sg.bars) {
        int32_t w = (int32_t)(sg.play * 224u / sg.bars);
        cv_rrect(8, 36, w < 6 ? 6 : w, 6, 3, T_TEXT, T_SURF);
    }
    cv_blit(0, SG_FOOT_Y);
}
static void song_draw(void)
{
    uint32_t k;
    sg_frame();
    sg_head();
    sg_foot();
    sg_labels();
    for (k = 0; k < NTRK; k++)
        sg_row(k);
}
