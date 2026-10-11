/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* MENU's settings for the editor (EDITOR_PROTOCOL.md "MENU settings"): MENU_DESC describes one, MENU_SET sets one;
 * INFO 4E 01 count. The editor builds its settings from these; the values, names and the apply path are the menu's
 * own (menu_items.c), so the two cannot differ. CALIBRATION and ABOUT are not offered (no value). 1.0.5: each reply
 * ends with the row's MENU tab (MI_TAB: its index and name), so the editor can group the settings as the device does.
 * ED_MENU: {row, id}, the 1.0.4 settings in the menu's order, then the later ones as they came (1.1: CLICK, CLICK LEVEL,
 * COUNT-IN, rows of AUDIO; 1.2: RESTORE LAST, SYSTEM; SCALE LEDS, CONTROL; 1.1.5: SCREEN OFF, DISPLAY; 1.2: SCOPE,
 * DISPLAY; STEP PREVIEW, CHORD ENTRY, CONTROL; MIDI IN, MIDI; TUNE, AUDIO; 1.3: HOME, DISPLAY): an index keeps its
 * setting too, so an older editor lists what it knew where it was (the
 * editor groups them by their tab). An id keeps its meaning for good: a new setting takes the next free id
 * (append-only), wherever its row goes; ids 0..126 (127: none).
 * 1.2: MIDI IN (21) and TUNE (22) are the project's G_ROUTE and G_TUNE (menu_items.c MI_PROJECT), the same values as a
 * SET of global 14 / 3: MENU_SET applies them to the music in RAM, kept with the project (the autosave, PROJECT SAVE),
 * not in the settings record: rc 0, nothing for settings_save to write. TUNE is a number (EDM_INT: the cents -50..50,
 * unit "ct"), the only one so far */
enum { ED_MENU_DESC = 72, ED_MENU_SET };
enum { EDM_ENUM, EDM_INT };                            /* kind: names follow; or a unit (none yet) */
static const uint8_t ED_MENU[][2] = {
    {MI_COLOR, 0}, {MI_STYLE, 1}, {MI_LARGE, 2}, {MI_ANIM, 3}, {MI_LEDS, 4}, {MI_HOLD, 5}, {MI_ACCEL, 6},
    {MI_LATCH, 7}, {MI_BPMLOCK, 8}, {MI_LOWCUT, 9}, {MI_USB, 10}, {MI_SERIAL, 11},
    {MI_CLICK, 12}, {MI_CLKLVL, 13}, {MI_COUNTIN, 14},
    {MI_RESTORE, 15},                                  /* 1.2 (Discussion #130), a row of SYSTEM */
    {MI_SCLLED, 16},                                   /* 1.2 (Discussion #127), a row of CONTROL */
    {MI_SCROFF, 17},                                   /* 1.1.5, a row of DISPLAY */
    {MI_SCOPE, 18},                                    /* 1.2 (Discussion #165), a row of DISPLAY */
    {MI_PREVIEW, 19},                                  /* 1.2 (Discussion #169), a row of CONTROL */
    {MI_CHADD, 20},                                    /* 1.2 (#155), a row of CONTROL */
    {MI_MIDIIN, 21},                                   /* 1.2: the project's G_ROUTE (was GLO > SYSTEM ROUT), MIDI */
    {MI_TUNE, 22},                                     /* 1.2: the project's G_TUNE (was GLO > GLOBAL), AUDIO */
    {MI_HOME, 23},                                     /* 1.3 (Discussions #112, #134): HOME SCOPE / TRACKS, DISPLAY */
    {MI_HELP, 24},                                     /* 1.5 (Discussion #156): HELP OFF / ON, SYSTEM */
};
#define ED_MENU_N NELEM(ED_MENU)

/* rc as UI_SET: 0 applied and saved, 3 applied in RAM only (no flash, a failed write), 4 queued until STOP */
static uint32_t ed_menu_save(void)
{
    settings_save();
#if FELUCCA_FLASH
    if (!flash_ok || persist_pending == 2u) return 3;
    if (persist_pending == 1u) return 4;
    {
        persist_t p = persist_saved;
        settings_export(&p);
        return memcmp(&p, &persist_saved, sizeof p) ? 3u : 0u;
    }
#else
    return 3;
#endif
}

static int ed_menu_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    uint32_t i, row, k;
    int32_t v, lo;
    if (cmd == ED_MENU_DESC) {                         /* index -> index, id, kind, value, min, max (v14), name, names,
                                                          * (1.0.5) its tab: index, name */
        ed_b(a[0]);
        if (a[0] >= ED_MENU_N) {
            ed_b(127);                                 /* no such item: the list ends */
            return 1;
        }
        row = ED_MENU[a[0]][0];
        k = menu_n(row);
        lo = row == MI_TUNE ? TUNE_MIN : 0;            /* (a number: its values from min) */
        ed_b(ED_MENU[a[0]][1]);
        ed_b(row == MI_TUNE ? EDM_INT : EDM_ENUM);
        ed_v((int32_t)menu_get(row) + lo);
        ed_v(lo);
        ed_v((int32_t)k - 1 + lo);
        ed_str(MI_NAME[row], 12);
        if (row == MI_TUNE)
            ed_str("ct", 12);                          /* (EDM_INT: its unit) */
        else
            for (i = 0; i < k; i++)
                ed_str(menu_vname(row, i), 12);
        ed_b(MI_TAB[row]);                             /* (after the names: an older editor stops before it) */
        ed_str(MTAB_NAME[MI_TAB[row]], 12);
        return 1;
    }
    if (cmd != ED_MENU_SET)
        return 0;
    (void)n;                                           /* id, value v14 -> rc, id, value v14 (the device's, clamped) */
    v = ed_rv(a + 1);
    for (i = 0; i < ED_MENU_N && ED_MENU[i][1] != a[0]; i++)
        ;
    if (i == ED_MENU_N) {
        ed_b(1); ed_b(a[0]); ed_v(v);
        return 1;
    }
    row = ED_MENU[i][0];
    lo = row == MI_TUNE ? TUNE_MIN : 0;                /* (a number: from its min) */
    menu_put(row, (uint32_t)(clamp(v, lo, lo + (int32_t)menu_n(row) - 1) - lo));
    if (row == MI_SERIAL)
        menu_serial_at = fm1_ms | 1u;                  /* (applied 200 ms after this reply: usb_serial_apply) */
    ui.force = 1;                                      /* (the MENU page, if shown, redraws) */
    ed_b(MI_PROJECT(row) ? 0u : ed_menu_save());       /* (the project's: in the music, no settings to save) */
    ed_b(a[0]);
    ed_v((int32_t)menu_get(row) + lo);
    return 1;
}
