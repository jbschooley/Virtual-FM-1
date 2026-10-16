/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Slot pages (docs/design: mock/r6_popups PROJECT, README "Popups"): PROJECT, SAVE > USER, FM6 STORE and P5 STORE as
 * the list of their slots alone, no action chips. KNOB 1 / 2 the slot, OCT+ its sheet (the page's own actions: act_do
 * in ui_input.c does them, as the chips did), OCT- Stage. The slot picked is a card: its name larger, a line about it,
 * PROJECT's Boot chip on the one power-on loads. PROJECT's last row, New song: OCT+ starts it. Included by ui_draw.c */
enum { SK_NONE, SK_PROJECT, SK_USER, SK_FM6, SK_P5 };
#define SL_Y0 Y_LABEL                                   /* the rows from y 32 (large type): 34 px, the card 58 */
#define SL_ROW 34
#define SL_CARD 58
static struct { uint32_t sig; uint16_t first; uint8_t save_page; } slv;   /* save_page: USER reached by SAVE (+1): its sheet on
                                                                  * Save here */
static void name_rename(void);                          /* ui_name.c */
static void act_do(void);                               /* ui_input.c */

static uint32_t slot_kind(void)
{
    const page_t *pg;
    if (ui.home || ui.menu || ui.layer)
        return SK_NONE;
    pg = cur_page();
    return pg->graph == GR_SLOTS ? SK_PROJECT : pg->graph == GR_USER ? SK_USER : pg->graph == GR_FMSTORE ? SK_FM6 :
           pg->scope == SC_P5STORE ? SK_P5 : SK_NONE;
}
static uint32_t slot_count(uint32_t k) { return k == SK_PROJECT ? (uint32_t)PROJ_TMPL + 1u : UP_SLOTS; }
static uint32_t slot_cur(uint32_t k)                    /* the row picked */
{
    switch (k) {
    case SK_PROJECT: return ui.proj_new ? (uint32_t)PROJ_TMPL : (uint32_t)song.g[G_SLOT] - 1u;
    case SK_USER: return ui.uslot % UP_SLOTS;
    case SK_FM6: return fm6_bslot % UP_SLOTS;
    default: return ((uint32_t)p5_store_slot - 1u) % UP_SLOTS;
    }
}
static uint32_t slot_boot(void)                         /* the row power-on loads (+1), 0 none */
{
    return settings_boot && graph_project_used(settings_boot - 1u) ? settings_boot
           : template_used() ? (uint32_t)PROJ_TMPL : 0u;
}
/* row r: its tag ("A", "T", "+", "U07", "F012"), its name, used (2: New song, always) */
static int slot_row(uint32_t k, uint32_t r, char *tag, char *nm)
{
    if (k == SK_PROJECT) {
        int used;
        if (r == (uint32_t)PROJ_TMPL) {
            str_cpy(tag, "+", 4);
            str_cpy(nm, "New song", 16);
            return 2;
        }
        tag[0] = (char)(r + 1u == (uint32_t)PROJ_TMPL ? 'T' : 'A' + r);
        tag[1] = 0;
        used = r + 1u == (uint32_t)PROJ_TMPL ? template_used() || template_old() : graph_project_used(r);
        str_cpy(nm, r + 1u == (uint32_t)PROJ_TMPL ? (template_old() ? "OLD FORMAT" : "Template") : used && graph_project_name(r)[0] ? graph_project_name(r)
                    : used ? "Saved" : "--", 16);
        return used;
    }
    up_slot_label(tag, r);
    if (!up_used(r) && !up_old(r)) {
        str_cpy(nm, "--", 16);
        return 0;
    }
    up_name(r, nm);
    return 1;
}
static void slot_detail(uint32_t k, uint32_t r, int used, char *b)   /* the card's second line */
{
    if (k == SK_PROJECT)
        str_cpy(b, used == 2 ? "Starts empty" : r + 1u == (uint32_t)PROJ_TMPL ? (used ? "Saved" : "Empty")
                   : used ? "Saved" : "Empty", 24);
    else if (!used)
        str_cpy(b, "Empty", 24);
    else
        str_cpy(b, ENGINES[eng_idx(up_engine(r))]->name, 24);
}

/* ------------------------------------------------------- the sheets --- */
static void sl_do(uint32_t col)                         /* the page's action in column col (act_do), done now */
{
    ui.act = (uint8_t)(col + 1u);
    act_do();
    ui.act = 0;
}
/* the columns (act_do): PROJECT SLOT BOOT LOAD SAVE; USER SLOT LOAD ERASE SAVE; the STOREs SLOT STORE SEND INIT */
static void sl_load(void) { sl_do(slot_kind() == SK_PROJECT ? 2u : 1u); }
static void sl_save(void) { sl_do(slot_kind() == SK_PROJECT || slot_kind() == SK_USER ? 3u : 1u); }
static void sl_erase(void)
{
    uint32_t r = slot_cur(SK_PROJECT);
    if (slot_kind() != SK_PROJECT)
        sl_do(2u);                                      /* (USER: act_do asks ERASE U07?) */
    else if (!graph_project_used(r))
        ui_message("EMPTY SLOT");
    else if (transport_busy())
        ui_message("STOP TO SAVE");
    else
        confirm_open(CF_ERASE_PROJ, r);
}
static void sl_send(void) { sl_do(2u); }
static void sl_init(void) { sl_do(3u); }
static void sl_boot_v(char *b) { str_cpy(b, settings_boot == song.g[G_SLOT] ? "On" : "Off", 8); }
static void sl_boot(void)
{
    settings_boot = (uint8_t)(settings_boot == song.g[G_SLOT] ? 0 : song.g[G_SLOT]);
    settings_save();
    ui.force = 1;
}
static const sheet_row_t SHEET_PROJECT[] = {{"Load", 0, 0, sl_load, 0}, {"Save here", 0, 0, sl_save, 0},
    {"Rename", 0, 0, name_rename, 0}, {"Boot", sl_boot_v, 0, sl_boot, 0}, {"Erase", 0, 0, sl_erase, CF_ERASE_PROJ}};
static const sheet_row_t SHEET_TEMPLATE[] = {{"Load template", 0, 0, sl_load, 0}, {"Save as template", 0, 0, sl_save, 0}};
static const sheet_row_t SHEET_IMPORT[] = {{"Import here",0,0,sysex_choose,0},{"Cancel import",0,0,sysex_cancel,0}};
static const sheet_row_t SHEET_BULK[] = {{"Send selected",0,0,bulk_send,0},{"Erase selected",0,0,bulk_erase_ask,CF_BULK_ERASE},{"Clear selection",0,0,bulk_clear,0}};
static const sheet_row_t SHEET_USER[] = {{"Load", 0, 0, sl_load, 0}, {"Save here", 0, 0, sl_save, 0},
    {"Rename", 0, 0, name_rename, 0}, {"Send",0,0,library_send_current,0}, {"Erase", 0, 0, sl_erase, CF_ERASE_USER}};
static const sheet_row_t SHEET_STORE[] = {{"Save here", 0, 0, sl_save, 0}, {"Send", 0, 0, sl_send, 0},
    {"Init sound", 0, 0, sl_init, 0}};

/* OCT+ on a slot: its sheet (New song: starts it) */
static void slot_enter(void)
{
    uint32_t k = slot_kind(), r;
    char tag[6], nm[16], t[24];
    int used;
    if (!k)
        return;
    r = slot_cur(k);
    if(k==SK_USER && sx_import.count){sysex_import_name(nm);sheet_open("Import SysEx",nm,SHEET_IMPORT,NELEM(SHEET_IMPORT));return;}
    if(k!=SK_PROJECT && bulk.on){sheet_open("Selected presets","USB SysEx / erase",SHEET_BULK,NELEM(SHEET_BULK));return;}
    used = slot_row(k, r, tag, nm);
    if (k == SK_PROJECT && r < 4u) {                    /* (the name as stored now, not the list's cached one) */
        used = project_name(r, nm);
        if (used && !nm[0])
            str_cpy(nm, "Saved", 16);
    }
    if (used == 2) {
        sl_do(2u);                                      /* (act_do: LOAD with NEW picked) */
        return;
    }
    str_cpy(t, tag, sizeof t);
    str_cpy(t + str_len(t), " \xB7 ", 4);
    str_cpy(t + str_len(t), used ? nm : "Empty", sizeof t - str_len(t));
    if (k == SK_PROJECT && r + 1u == (uint32_t)PROJ_TMPL)
        sheet_open(t, used ? "saved" : "empty", SHEET_TEMPLATE, NELEM(SHEET_TEMPLATE));
    else if (k == SK_PROJECT)
        sheet_open(t, used ? "saved" : "empty", SHEET_PROJECT, NELEM(SHEET_PROJECT));
    else if (k == SK_USER)
        sheet_open(t, used ? "used" : "empty", SHEET_USER, NELEM(SHEET_USER));
    else
        sheet_open(t, used ? "used" : "empty", SHEET_STORE, NELEM(SHEET_STORE));
    if (k == SK_USER && slv.save_page == ui.page + 1u)  /* (reached by SAVE: on Save here) */
        pop.sel = 1;
    slv.save_page = 0;
}
/* SAVE, the sound sheet's Save as user..: SAVE > USER shown, nothing popped up (Kerem, 2026-10-10); its slot's sheet,
 * when OCT+ opens it, on Save here */
static void slot_save_sheet(void)
{
    slv.save_page = (uint8_t)(ui.page + 1u);
}

/* ----------------------------------------------------------- drawing --- */
static void slot_draw(void)
{
    uint32_t k = slot_kind(), n = slot_count(k), cur = slot_cur(k), boot = k == SK_PROJECT ? slot_boot() : 0u;
    uint32_t vis = 1u + (uint32_t)((PV_SOUND_Y - 2 - SL_Y0 - SL_CARD) / SL_ROW), r, sig;
    const aafont_t *tf = TY_TX, *lf = TY_LB;
    int32_t y, nx = 12 + text_w(tf, k == SK_PROJECT ? "W" : "F000") + 10;   /* the names after the widest tag */
    char tag[6], nm[16];
    if (ui.force) {
        lcd_fill(0, H_HEAD, 240, PV_SOUND_Y - H_HEAD, T_BG);
        ui.graph_sig = 0;
    }
    draw_head();
    pv_sound();
    if (vis > n)
        vis = n;
    slv.first = (uint16_t)clamp((int32_t)cur - (int32_t)vis / 2, 0, (int32_t)(n - vis));
    sig = cur * 7u + slv.first * 131u + boot * 1031u + k * 40503u + ux.gen * 977u + ux.pal * 31u + song.sel * 13u +
          up_gen * 7919u + library_engine() * 104729u + bulk.on * 19u + bulk.anchor * 23u + bulk.end * 29u + sx_import.count * 37u;
    for (r = slv.first; r < slv.first + vis; r++)
        sig = str_hash(sig + (uint32_t)slot_row(k, r, tag, nm), nm);
    if (!ui.force && sig == slv.sig)
        return;
    slv.sig = sig;
    for (r = slv.first, y = SL_Y0; r < slv.first + vis; r++) {
        int used = slot_row(k, r, tag, nm), on = r == cur;
        int32_t h = on ? SL_CARD : SL_ROW;
        cv_begin(240, (uint32_t)h, T_BG);
        if (on) {                                       /* the card: tinted in the track's colour, a line about it */
            char d[24];
            uint16_t bg = ux_mix(T_BG, T_THEME, 18);
            int32_t nw = 236 - nx - 8 - (boot == r + 1u ? text_w(lf, "Boot") + 10 : 0);
            cv_rrect(BOX_X, 0, BOX_W, h - 2, 8, bg, T_BG);
            cv_text_on(12, ty_y(17, tf), tf, tag, T_THEME, bg);
            cv_free_text(nx, ty_y(17, ty_fit(tf, nm, nw)), ty_fit(tf, nm, nw), used ? nm : "Empty", used ? T_TEXT : T_MID, bg, nw);
            slot_detail(k, r, used, d);
            if(sx_import.count){fmt_int(d,sx_import.count);str_cpy(d+str_len(d)," incoming patches",20);}
            else if(bulk_has(r))str_cpy(d,"Selected",24);
            cv_free_text(nx, ty_y(41, lf), lf, d, T_MID, bg, 236 - nx - 8);
            if (boot == r + 1u)
                cv_text_r(228, ty_y(17, lf), lf, "Boot", T_THEME, bg);
        } else {
            if(k!=SK_PROJECT && bulk_has(r))cv_rect(BOX_X,6,3,SL_ROW-14,T_THEME);
            cv_text_on(12, ty_y(SL_ROW / 2 - 1, tf), tf, tag, used ? T_MID : T_DIM, T_BG);
            {
                int32_t nw = boot == r + 1u ? 228 - text_w(lf, "Boot") - 8 - nx : 228 - nx;
                const aafont_t *f = ty_fit(tf, nm, nw);
                cv_free_text(nx, ty_y(SL_ROW / 2 - 1, f), f, nm, used ? T_SEC : T_DIM, T_BG, nw);
            }
            if (boot == r + 1u)
                cv_text_r(228, ty_y(SL_ROW / 2 - 1, lf), lf, "Boot", T_MID, T_BG);
        }
        if (n > vis) {                                  /* where the rows are in the list: a bar at the right edge */
            int32_t span = (int32_t)(SL_CARD + (vis - 1u) * SL_ROW), th = span * (int32_t)vis / (int32_t)n;
            int32_t ty = (span - th) * slv.first / (int32_t)(n - vis) - (y - SL_Y0);
            cv_rect(238, 0, 2, h, T_LINE);
            if (ty < h && ty + th > 0)
                cv_rect(238, ty, 2, th, T_THEME);
        }
        cv_blit(0, (uint32_t)y);
        y += h;
    }
}
