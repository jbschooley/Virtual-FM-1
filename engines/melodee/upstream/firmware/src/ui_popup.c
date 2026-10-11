/* SPDX-License-Identifier: GPL-3.0-only */
/* Popups (docs/design: mock/r6_popups, README "Popups"), over the page dimmed (ui_draw.c: drawn under cv_dim).
 * An action sheet: what can be done with the page (OCT+ held; a "..." in the header says there is one) or with the
 * thing its cursor is on: a card from the bottom, its rows; KNOB 2 the row, KNOB 1 a row's value, OCT+ does it (a
 * destructive one asks its question first), OCT- closes.
 * A picker: a knob whose value is a name from a list (a wave, a source, a division; not a list of numbers) turning
 * on a page: the names
 * around its value in a card under its ring; it goes ST_KNOB_MS after the last turn, on OCT- or when another knob
 * turns. The knob edits as ever; the page under it waits. Included by ui_draw.c */
typedef struct {
    const char *label;
    void (*value)(char *b);                             /* its value at the right (0: none) */
    void (*turn)(int32_t s);                            /* KNOB 1 changes it (0: none) */
    void (*act)(void);                                  /* OCT+ does it (0: OCT+ steps the value) */
    uint8_t cf;                                         /* the question it asks first (CF_*), 0 none; with act: act
                                                         * asks it itself (after its checks), the row drawn red;
                                                         * SR_RED: no question, red (a destructive act) */
} sheet_row_t;
#define SR_RED 0xFFu
enum { POP_NONE, POP_SHEET, POP_PICK, POP_LIST };
#define POP_X 6                                         /* a sheet: 228 wide, its rows 20 px, 22 apart from y 28 */
#define POP_W 228
#define POP_ROW 22
#define POP_PICK_W 110                                  /* a picker: up to five names, 20 px each */
static struct {
    uint8_t on, n, sel, col;
    char title[24], sub[20];
    const sheet_row_t *rows;
    uint32_t t, sig;
} pop;

static void confirm_open(uint32_t kind, uint32_t trk);  /* ui_input.c */
static void edit_param(uint32_t slot, int32_t steps);
static void jam_take(void);
static int name_on(void);
static int new_on(void);
static void menu_words(char *d, const char *s, uint32_t n);   /* ui_menu.c */
static uint32_t slot_kind(void);                        /* ui_slots.c */
static void step_delete_edit(void);                     /* ui_input.c */
static int live_rec_sel(void);
static void slot_save_sheet(void);

static void pop_close(void)
{
    if (pop.on) {
        pop.on = POP_NONE;
        ui.force = 1;
    }
}
static void sheet_open(const char *title, const char *sub, const sheet_row_t *rows, uint32_t n)
{
    str_cpy(pop.title, title, sizeof pop.title);
    str_cpy(pop.sub, sub, sizeof pop.sub);
    pop.rows = rows;
    pop.n = (uint8_t)n;
    pop.sel = 0;
    pop.on = POP_SHEET;
    ui.force = 1;
}

/* ----------------------------------------------------- the sheets --- */
/* the sound's (Stage, the sound pages) */
static void ps_save(void)
{
    uint32_t i;
    for (i = 0; i < NPAGES && PAGES[i].graph != GR_USER; i++)
        ;
    ui.home = 0;
    ui.page = (uint8_t)i;
    page_entered();
    slot_save_sheet();                                  /* (OCT+ then: its slot's sheet on Save here) */
}
static void ps_fav_v(char *b) { str_cpy(b, preset_favorite() ? "On" : "Off", 8); }
static void ps_fav(void) { preset_mark(!preset_favorite()); }
static void ps_undo(void) { undo_step(0); }
static const sheet_row_t SHEET_SOUND[] = {
    {"Init sound", 0, 0, 0, CF_INIT_SOUND}, {"Save as user\x85", 0, 0, ps_save, 0}, {"Favourite", ps_fav_v, 0, ps_fav, 0},
    {"Undo last load", 0, 0, ps_undo, 0}, {"Sound designer", 0, 0, designer_open, 0}};
/* the song's (SONG) */
static int ps_song_free(void)
{
    if (chain_busy()) {
        ui_message("STOP TO EDIT");
        return 0;
    }
    return 1;
}
static void ps_row_insert(int dup)                     /* a section at the row picked: its copy, or the patterns now */
{
    uint32_t r = ui.song_row < chain_config.count ? ui.song_row : chain_config.count, i, k;
    if (!ps_song_free())
        return;
    if (chain_config.count >= CHAIN_ROWS) {
        ui_message("SONG FULL");
        return;
    }
    for (i = chain_config.count; i > r; i--) {
        chain_config.row[i] = chain_config.row[i - 1u];
        memcpy(chain_patterns[i], chain_patterns[i - 1u], NTRK);
    }
    if (!dup || r >= chain_config.count) {
        for (k = 0; k < NTRK; k++)
            chain_patterns[r][k] = trk[k].pattern;
        chain_config.row[r].repeat = 1;
        chain_config.row[r].slot = chain_patterns[r][0];
    }
    chain_config.count++;
    ui_message(dup ? "SECTION DUPLICATED" : "SECTION INSERTED");
}
static void ps_insert(void) { ps_row_insert(0); }
static void ps_dup(void) { ps_row_insert(1); }
static void ps_jam_v(char *b)
{
    fmt_int(b, jam.n);
    str_cpy(b + str_len(b), jam.n == 1u ? " row" : " rows", 6);
}
static void ps_jam(void)
{
    if (!ps_song_free())
        return;
    if (!jam.n)
        ui_message("NO JAM YET");
    else if (chain_config.count)
        confirm_open(CF_TAKE_JAM, 0);
    else
        jam_take();
}
static const sheet_row_t SHEET_SONG[] = {
    {"Insert section", 0, 0, ps_insert, 0}, {"Duplicate section", 0, 0, ps_dup, 0}, {"Delete section", 0, 0, 0, CF_DEL_ROW},
    {"Take jam", ps_jam_v, 0, ps_jam, 0}, {"Clear song", 0, 0, 0, CF_CLEAR_SONG}};

/* the pattern's (NOTES, the drum grid, PATTERN: OCT+ held; TOOLS' Clear pattern lives here) */
static const sheet_row_t SHEET_PATTERN[] = {{"Clear pattern", 0, 0, 0, CF_CLEAR_SEQ}, {"Clear motion", 0, 0, 0, CF_CLEAR_MOTION}};
/* PATTERNS' (OCT+): the selected track's pattern; Copy to: its place picked on the grid (ui_patterns.c) */
static const sheet_row_t SHEET_PATTERNS[] = {{"Copy to\x85", 0, 0, ptc_start, 0}, {"Delete pattern", 0, 0, 0, CF_DEL_PAT}};

/* the step's: NOTES' note at the cursor (KNOB 1 its length, velocity, chance), the drum grid's hit of the lane there */
static void st_cap(uint32_t c, char *b)                 /* NOTES' column c as its chip shows it (draw_columns) */
{
    lrow_t rw;
    char lb[12];
    uint16_t vc;
    rw.page = cur_page()->title;
    rw.slot = (uint8_t)c;
    list_cap(&rw, lb, b, &vc);
}
static step_t *st_at(void)                              /* the note's step (else the cursor's) */
{
    uint32_t s = grid_on() ? NSTEP : notes_manual_start(TSEL);
    return &TSEL->step[s < NSTEP ? s : ui.cursor];
}
static int st_free(void)                                /* (a step edit now: not while a song plays, not recording) */
{
    if (chain_busy()) { ui_message("STOP TO EDIT"); return 0; }
    if (live_rec_sel()) { ui_message("STOP RECORDING"); return 0; }
    return 1;
}
static void nt_len_v(char *b)                          /* "3 steps" */
{
    uint32_t n;
    st_cap(2u, b);
    n = str_len(b);
    if (n && n < 9u && ((b[0] >= '0' && b[0] <= '9') || b[0] == '<') && b[n - 1] >= '0' && b[n - 1] <= '9')
        str_cpy(b + n, str_eq(b, "1") ? " step" : " steps", 8);
}
static void nt_len(int32_t s) { edit_param(2u, s > 0 ? 1 : -1); }
static void nt_vel_v(char *b) { st_cap(3u, b); }
static void nt_vel(int32_t s) { edit_param(3u, s); }
static void nt_chance_v(char *b)
{
    fmt_int(b, (int32_t)step_chance(st_at()));
    str_cpy(b + str_len(b), " %", 3);
}
static void nt_chance(int32_t s)
{
    step_t *st = st_at();
    if (st_free())
        step_set_chance(st, (uint32_t)clamp((int32_t)step_chance(st) + s, 0, 100));
}
static void nt_slide_v(char *b) { str_cpy(b, st_at()->flags & SF_SLIDE ? "On" : "Off", 8); }
static void nt_slide(void)
{
    if (!st_free())
        return;
    fm1_irq_off();
    st_at()->flags ^= SF_SLIDE;
    fm1_irq_on();
}
static void nt_delete(void)
{
    if (st_free())
        step_delete_edit();
}
static const sheet_row_t SHEET_NOTE[] = {{"Length", nt_len_v, nt_len, 0, 0}, {"Velocity", nt_vel_v, nt_vel, 0, 0},
    {"Chance", nt_chance_v, nt_chance, 0, 0}, {"Slide", nt_slide_v, 0, nt_slide, 0}, {"Delete note", 0, 0, nt_delete, SR_RED}};
static const sheet_row_t SHEET_REC_NOTE[] = {{"Length", nt_len_v, nt_len, 0, 0}, {"Velocity", nt_vel_v, nt_vel, 0, 0},
    {"Delete note", 0, 0, nt_delete, SR_RED}};
static int ht_acc_on(void) { return (step_accents(&seq_steps(TSEL)[ui.cursor]) >> ui.lane) & 1u; }
static void ht_acc_v(char *b) { str_cpy(b, ht_acc_on() ? "On" : "Off", 8); }
static void ht_acc(void) { edit_param(3u, ht_acc_on() ? -1 : 1); }
static void ht_clear(void) { edit_param(2u, -1); }
static const sheet_row_t SHEET_HIT[] = {{"Accent", ht_acc_v, 0, ht_acc, 0}, {"Chance", nt_chance_v, nt_chance, 0, 0},
    {"Clear hit", 0, 0, ht_clear, SR_RED}};

/* OCT+ on NOTES / the drum grid: an empty place gets a note (the last one played) / the lane's hit; on a note or a
 * hit: its sheet. 0: not such a page */
static int step_enter(void)
{
    char t[24], sub[12];
    uint32_t chosen, start;
    if (ui.home || ui.layer || ui.menu || cur_page()->graph != GR_ROLL)
        return 0;
    str_cpy(sub, "step ", sizeof sub);
    fmt_int(sub + 5, (int32_t)ui.cursor + 1);
    if (grid_on()) {
        if (!((step_lanes(&seq_steps(TSEL)[ui.cursor]) >> ui.lane) & 1u)) {
            edit_param(2u, 1);                          /* (the grid's HIT: its checks) */
            return 1;
        }
        str_cpy(t, drum_lane_abbr(TSEL, ui.lane), sizeof t);   /* "SD \xB7 step 5" */
        str_cpy(t + str_len(t), " \xB7 ", 4);
        str_cpy(t + str_len(t), sub, sizeof t - str_len(t));
        sheet_open(t, "", SHEET_HIT, NELEM(SHEET_HIT));
        return 1;
    }
    chosen = notes_selected(TSEL);
    start = notes_manual_start(TSEL);
    if (chosen >= RECORD_MAX && !(start < NSTEP && TSEL->step[start].n)) {
        edit_param(1u, 0);                              /* (NOTES' PITCH on an empty step: the last note there) */
        return 1;
    }
    str_cpy(t, "Note ", sizeof t);
    st_cap(1u, t + 5);
    if (chosen < RECORD_MAX)
        sheet_open(t, sub, SHEET_REC_NOTE, NELEM(SHEET_REC_NOTE));
    else
        sheet_open(t, sub, SHEET_NOTE, NELEM(SHEET_NOTE));
    return 1;
}

/* the page's sheet: 0 none, 1 the sound's, 2 the song's, 3 the pattern's, 4 PATTERNS' */
static uint32_t page_sheet(void)
{
    uint32_t fam;
    if (designer_on() || ui.menu || ui.confirm || ui.layer || pop.on || name_on() || new_on() || slot_kind() || ptc_on())
        return 0;
    if (ui.home)
        return 1;
    if (cur_page()->graph == GR_SONG)
        return 2;
    if (cur_page()->graph == GR_PATGRID)
        return 4;
    if (cur_page()->graph == GR_ROLL || cur_page()->graph == GR_STEPS)
        return 3;
    fam = cur_page()->fam;
    return fam == FAM_EDIT || fam == FAM_ENV || fam == FAM_LFO || fam == FAM_FX ? 1u : 0u;
}
static void page_sheet_open(void)
{
    char nm[20];
    switch (page_sheet()) {
    case 1:
        sound_name(TSEL, nm);
        sheet_open(nm[0] ? nm : "Sound", ENGINES[eng_idx(TSEL->eng_req)]->name, SHEET_SOUND, NELEM(SHEET_SOUND));
        break;
    case 2:
        fmt_int(nm, chain_config.count);
        str_cpy(nm + str_len(nm), chain_config.count == 1u ? " section" : " sections", 10);
        sheet_open("Song", nm, SHEET_SONG, NELEM(SHEET_SONG));
        break;
    case 3:
        str_cpy(nm, "T1", sizeof nm);
        nm[1] = (char)('1' + song.sel % NTRK);
        sheet_open("Pattern", nm, SHEET_PATTERN, NELEM(SHEET_PATTERN));
        break;
    case 4: {                                           /* "Pattern 3", "T2" */
        char t[12] = "Pattern 1";
        t[8] = (char)('1' + TSEL->pattern % NPAT);
        str_cpy(nm, "T1", sizeof nm);
        nm[1] = (char)('1' + song.sel % NTRK);
        sheet_open(t, nm, SHEET_PATTERNS, NELEM(SHEET_PATTERNS));
        break;
    }
    default:
        break;
    }
}

/* a clearing row with nothing there to clear (or a song playing): says so instead of asking; 0 = ask */
static int cf_refused(uint32_t cf)
{
    int edit = cf == CF_DEL_ROW || cf == CF_CLEAR_SONG || cf == CF_CLEAR_SEQ || cf == CF_CLEAR_MOTION || cf == CF_DEL_PAT;
    if (edit && chain_busy())
        ui_message("STOP TO EDIT");
    else if ((cf == CF_DEL_ROW && ui.song_row >= chain_config.count) ||
             (cf == CF_DEL_PAT && pattern_empty(song.sel % NTRK, TSEL->pattern)))
        ui_message("NOTHING TO DELETE");
    else if ((cf == CF_CLEAR_SONG && !chain_config.count) || (cf == CF_CLEAR_SEQ && seq_is_empty(TSEL)) ||
             (cf == CF_CLEAR_MOTION && !motion_count(TSEL)))
        ui_message("NOTHING TO CLEAR");
    else
        return 0;
    return 1;
}

/* KNOB 2 the row, KNOB 1 its value; OCT+ does it (its question first), OCT- closes (oct: bit 0 OCT-, bit 1 OCT+) */
static void sheet_input(int32_t k1, int32_t k2, uint32_t oct)
{
    const sheet_row_t *r;
    if (k2)
        pop.sel = (uint8_t)clamp((int32_t)pop.sel + (k2 > 0 ? 1 : -1), 0, (int32_t)pop.n - 1);
    r = &pop.rows[pop.sel % pop.n];
    if (k1 && r->turn)
        r->turn(k1);
    if (oct & 1u) {
        pop_close();
    } else if (oct & 2u) {
        pop_close();
        if (r->act)
            r->act();
        else if (r->cf && !cf_refused(r->cf))
            confirm_open(r->cf, r->cf == CF_DEL_ROW ? ui.song_row : song.sel);
        else if (r->turn)
            r->turn(1);
    }
}

/* ---------------------------------------------------- the pickers --- */
/* knob c turned on a page: its picker when its value is a name from a long list; another knob: the picker goes.
 * Not on a screen of its own: SCALES is the scales' list itself (Kerem: the picker covered it) */
static int own_screen(void);                            /* ui_draw.c */
/* a list of numbers (a MIDI channel, an algorithm, an envelope step; one name such as OFF among them): its value steps
 * on the card as MIDI's and ROUT's do, no picker (Kerem, 2026-10-11) */
static int pick_numbers(const param_desc_t *d)
{
    uint32_t named = 0;
    int32_t v;
    for (v = d->min; v <= d->max; v++) {
        const char *s = d->names[v];
        while (*s >= '0' && *s <= '9')
            s++;
        named += *s || s == d->names[v];
    }
    return named <= 1u;
}
static void pick_touch(uint32_t c)
{
    int16_t *vp;
    const param_desc_t *d;
    if (pop.on == POP_PICK && pop.col != c)
        pop_close();
    if (ui.home || pop.on == POP_SHEET || own_screen())
        return;
    d = page_desc(cur_page(), c, &vp);
    if (!d || !vp || d->fmt != F_ENUM || !d->names || d->max - d->min < 4 || pick_numbers(d))
        return;
    if (pop.on != POP_PICK)
        ui.force = 1;                                   /* (the page dimmed under it) */
    pop.on = POP_PICK;
    pop.col = (uint8_t)c;
    pop.t = fm1_ms;
}
static void pick_poll(void)                             /* ST_KNOB_MS after the last turn: gone */
{
    if (pop.on == POP_PICK && fm1_ms - pop.t > ST_KNOB_MS)
        pop_close();
}

/* ------------------------------------------------ a value's list --- */
/* OCT+ on a list page's row whose value is a name from a list: the whole list (KNOB 2, OCT+ takes it, OCT- closes) */
static struct { uint8_t page, slot; int16_t sel; } vl;
static const param_desc_t *vlist_desc(int16_t **vp) { return page_desc(&PAGES[vl.page], vl.slot, vp); }
static void vlist_open(uint8_t page, uint8_t slot)
{
    int16_t *vp;
    const param_desc_t *d;
    vl.page = page;
    vl.slot = slot;
    d = vlist_desc(&vp);
    if (!d || !vp)
        return;
    vl.sel = *vp;
    pop.on = POP_LIST;
    ui.force = 1;
}
static void vlist_input(int32_t k2, uint32_t oct)
{
    int16_t *vp;
    const param_desc_t *d = vlist_desc(&vp);
    if (!d || !vp) {
        pop_close();
        return;
    }
    if (k2)
        vl.sel = (int16_t)clamp(vl.sel + (k2 > 0 ? 1 : -1), d->min, d->max);
    if (oct & 1u) {
        pop_close();
    } else if (oct & 2u) {                              /* (its page edits it, as if it were shown) */
        uint8_t pg = ui.page;
        int32_t steps = vl.sel - *vp;
        ui.page = vl.page;
        if (steps)
            edit_param(vl.slot, steps);
        ui.page = pg;
        pop_close();
    }
}
static void vlist_draw(void)
{
    int16_t *vp;
    const param_desc_t *d = vlist_desc(&vp);
    int32_t i, n, first, h, y0, top;
    uint32_t sig;
    char b[20];
    if (!d || !vp || !d->names) {
        pop_close();
        return;
    }
    n = d->max - d->min + 1 < 7 ? d->max - d->min + 1 : 7;
    sig = (uint32_t)(vl.sel + 1) * 7919u + vl.page * 131u + ux.gen * 977u + ux.pal * 31u;
    if (!ui.force && sig == pop.sig)
        return;
    pop.sig = sig;
    first = clamp(vl.sel - 3, d->min, d->max - n + 1);
    h = 30 + n * 20 + 6;
    y0 = 236 - h;
    for (top = 0; top < h; ) {                          /* in slices between rows (a canvas holds 124 rows) */
        int32_t sh = h - top <= 124 ? h - top : 28 + (top + 124 - 28) / 20 * 20 - top;
        cv_begin(POP_W, (uint32_t)sh, T_BG);
        cv_oy = -top;
        cv_rrect(0, 0, POP_W, h, 10, T_LINE, T_BG);
        cv_rrect(1, 1, POP_W - 2, h - 2, 9, T_SURF, T_LINE);
        menu_words(b, d->label, sizeof b);
        if (top == 0)
            cv_text_on(10, 8, &AF_S, b, T_TEXT, T_SURF);
        for (i = 0; i < n; i++) {
            int32_t v = first + i, y = 28 + i * 20, on = v == vl.sel, cur = v == *vp;
            uint16_t bg = on ? T_LIFT : T_SURF;
            if (y + 20 <= top || y >= top + sh)
                continue;
            if (on) {
                cv_rect(1, y, POP_W - 2, 18, T_LIFT);
                cv_rect(1, y, 3, 18, T_THEME);
            }
            menu_words(b, d->names[v - d->min], sizeof b);
            cv_free_text(10, y + 2, &AF_S, b, on ? T_TEXT : T_SEC, bg, POP_W - 40);
            if (cur)                                    /* (the value now: a dot) */
                cv_disc(POP_W - 14, y + 9, 6, T_THEME, 0);
        }
        cv_oy = 0;
        cv_blit(POP_X, (uint32_t)(y0 + top));
        top += sh;
    }
}

/* ------------------------------------------------------- drawing --- */
static void sheet_draw(void)
{
    uint32_t i, sig = str_hash(pop.sel * 7u + pop.n * 131u + ux.gen * 977u + ux.pal * 31u, pop.title);
    int32_t h = 34 + (int32_t)pop.n * POP_ROW, y0 = 236 - h, top;
    char v[20];
    for (i = 0; i < pop.n; i++)
        if (pop.rows[i].value) {
            pop.rows[i].value(v);
            sig = str_hash(sig, v);
        }
    if (!ui.force && sig == pop.sig)
        return;
    pop.sig = sig;
    for (top = 0; top < h; ) {                          /* in slices between rows (a canvas holds 124 rows) */
        int32_t sh = h - top <= 124 ? h - top : 28 + (top + 124 - 28) / POP_ROW * POP_ROW - top;
        cv_begin(POP_W, (uint32_t)sh, T_BG);
        cv_oy = -top;
        cv_rrect(0, 0, POP_W, h, 10, T_LINE, T_BG);
        cv_rrect(1, 1, POP_W - 2, h - 2, 9, T_SURF, T_LINE);
        cv_text_on(10, 8, &AF_S, pop.title, T_TEXT, T_SURF);
        if (pop.sub[0])
            cv_text_r(POP_W - 10, 9, &AF_X, pop.sub, T_MID, T_SURF);
        for (i = 0; i < pop.n; i++) {
            const sheet_row_t *r = &pop.rows[i];
            int32_t y = 28 + (int32_t)i * POP_ROW;
            int on = i == pop.sel;
            uint16_t bg = on ? T_LIFT : T_SURF;
            if (y + POP_ROW <= top || y >= top + sh)
                continue;
            if (on) {
                cv_rect(1, y, POP_W - 2, 20, T_LIFT);
                cv_rect(1, y, 3, 20, T_THEME);
            }
            cv_text_on(10, y + 3, &AF_S, r->label, r->cf ? T_REC : on ? T_TEXT : T_SEC, bg);
            if (r->value) {
                r->value(v);
                cv_text_r(POP_W - 10, y + 3, &AF_S, v, on ? T_THEME : T_MID, bg);
            }
        }
        cv_oy = 0;
        cv_blit(POP_X, (uint32_t)(y0 + top));
        top += sh;
    }
}
static void pick_draw(void)
{
    int16_t *vp;
    const param_desc_t *d = page_desc(cur_page(), pop.col, &vp);
    int32_t x, v, i;
    uint32_t sig;
    if (!d || !vp || !d->names) {
        pop_close();
        return;
    }
    v = *vp;
    sig = (uint32_t)(v + 1) * 7919u + pop.col * 131u + ux.gen * 977u + ux.pal * 31u;
    if (!ui.force && sig == pop.sig)
        return;
    pop.sig = sig;
    {
    int32_t rows = d->max - d->min + 1 < 5 ? d->max - d->min + 1 : 5, h = 16 + rows * 20;
    int32_t first = clamp(v - 2, d->min, d->max - rows + 1);   /* five names around it, the list's ends kept */
    x = clamp(30 + 60 * (int32_t)pop.col - POP_PICK_W / 2, 4, 236 - POP_PICK_W);
    cv_begin(POP_PICK_W, (uint32_t)h, T_BG);
    cv_rrect(0, 0, POP_PICK_W, h, 10, T_LINE, T_BG);
    cv_rrect(1, 1, POP_PICK_W - 2, h - 2, 9, T_SURF, T_LINE);
    for (i = 0; i < rows; i++) {
        int32_t n = first + i, y = 8 + i * 20, far = n - v > 1 || v - n > 1;
        char b[16];
        list_words(b, d->names[n - d->min], sizeof b);
        if (n == v) {
            cv_rrect(4, y - 1, POP_PICK_W - 8, 20, 5, T_THEME, T_SURF);
            cv_rrect(5, y, POP_PICK_W - 10, 18, 4, T_LIFT, T_THEME);
            cv_free_text(12, y + 2, &AF_S, b, T_TEXT, T_LIFT, POP_PICK_W - 20);
        } else {
            cv_free_text(12, y + 2, &AF_S, b, far ? T_MID : T_SEC, T_SURF, POP_PICK_W - 20);
        }
    }
    cv_blit((uint32_t)x, 40);
    }
}
static void pop_draw(void)
{
    if (pop.on == POP_SHEET)
        sheet_draw();
    else if (pop.on == POP_PICK)
        pick_draw();
    else if (pop.on == POP_LIST)
        vlist_draw();
}
