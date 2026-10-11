/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* MENU's rows and their settings: what ui_menu.c draws and steps, and what the editor reads and sets (editor_menu.c
 * MENU_DESC / MENU_SET). One apply path for both: menu_put. Grouped: the screen (COLOR, STYLE, LARGE, ANIM, LEDS; 1.1.5:
 * SCREEN OFF; 1.2: SCOPE; 1.3: HOME), the
 * controls (HOLD: the layer threshold, KNOB ACCEL, FX LATCH, BPM LOCK; 1.2: SCALE LEDS, the keys, STEP PREVIEW, CHORD
 * ENTRY), the sound (SPEAKER EQ: FLAT LOWCUT BASS+,
 * USB LEVEL; 1.1: CLICK, CLICK LEVEL, COUNT-IN; 1.2: TUNE), MIDI (1.2: MIDI IN; 1.5: LEARN CLEAR), USB SERIAL, RESTORE
 * LAST (1.2), then CALIBRATION (the setup screen: HARDWARE CALIBRATION), INFO (1.2) and ABOUT, the rows with no value
 * (MI_VALUES: the rows before them hold one, but LEARN CLEAR: menu_valued). 1.0.5: in four tabs (MI_TAB); 1.2: five.
 * 1.5: LEARN CLEAR (MIDI) is an action, not a setting: OCT+ clears MIDI LEARN's map (midi_learn.c ml_clear_all; the editor
 * has LEARN_SET op 2 for it, so MENU_DESC does not offer it); the row shows how many CCs are learned
 * 1.2: TUNE and MIDI IN are not settings: they edit the project's G_TUNE and G_ROUTE (up to 1.1.5 on GLO > GLOBAL and
 * GLO > SYSTEM), the values the project holds and saves as before (no format change; a project load brings its own);
 * nothing of theirs is in the settings record (MI_PROJECT: menu_get / menu_put read and write song.g) */
enum { MI_COLOR, MI_STYLE, MI_LARGE, MI_ANIM, MI_LEDS, MI_SCROFF, MI_SCOPE, MI_HOME, MI_HOLD, MI_ACCEL, MI_LATCH, MI_BPMLOCK, MI_SCLLED,
       MI_PREVIEW, MI_CHADD, MI_LOWCUT, MI_USB,
       MI_CLICK, MI_CLKLVL, MI_COUNTIN, MI_TUNE, MI_MIDIIN, MI_LCLEAR, MI_SERIAL, MI_RESTORE, MI_HELP, MI_PANEL, MI_INFO,
       MI_ABOUT, MI_COUNT };
                                                       /* (1.1: the metronome's rows in AUDIO; 1.2: RESTORE LAST in
                                                        * SYSTEM, SCALE LEDS STEP PREVIEW CHORD ENTRY in CONTROL, SCOPE in
                                                        * DISPLAY, TUNE in AUDIO, MIDI IN in MIDI, INFO in SYSTEM;
                                                        * 1.3: HOME in DISPLAY; 1.5: HELP in SYSTEM) */
#define MI_VALUES MI_PANEL
static int menu_valued(uint32_t row) { return row < MI_VALUES && row != MI_LCLEAR; }   /* (a row with a value) */
#define MI_PROJECT(row) ((row) == MI_TUNE || (row) == MI_MIDIIN)   /* the project's values (song.g), not settings */
static const char *const MI_NAME[MI_COUNT] = {"COLOR", "STYLE", "LARGE", "ANIM", "LEDS", "SCREEN OFF", "SCOPE", "HOME", "HOLD", "KNOB ACCEL",
                                              "FX LATCH", "BPM LOCK", "SCALE LEDS", "STEP PREVIEW", "CHORD ENTRY",
                                              "SPEAKER EQ", "USB LEVEL", "CLICK", "CLICK LEVEL", "COUNT-IN", "TUNE",
                                              "MIDI IN", "LEARN CLEAR", "USB SERIAL", "RESTORE LAST", "HELP", "CALIBRATION", "INFO",
                                              "ABOUT"};
/* 1.0.5: the MENU's tabs (ui_menu.c: ALGORITHM steps between them, PRESETS among one tab's rows; the editor gets a
 * row's tab after its MENU_DESC reply). A tab's rows follow each other in MI order (tests/ui_test.c checks it); at
 * most MTAB_ROWS each (the page does not scroll: ui_menu.c fits them; 1.2: 7, the rows of such a tab 21 px; 1.3: 8,
 * 18 px). A new row
 * joins a tab here. Tabs are in the order the device shows them; a tab's index is what the editor is told, not an id
 * (1.2: MIDI came in before SYSTEM, SYSTEM's index 3 -> 4) */
enum { MTAB_DISPLAY, MTAB_CONTROL, MTAB_AUDIO, MTAB_MIDI, MTAB_SYSTEM, MTAB_COUNT };
#define MTAB_ROWS 8u
static const char *const MTAB_NAME[MTAB_COUNT] = {"DISPLAY", "CONTROL", "AUDIO", "MIDI", "SYSTEM"};
static const uint8_t MI_TAB[MI_COUNT] = {
    MTAB_DISPLAY, MTAB_DISPLAY, MTAB_DISPLAY, MTAB_DISPLAY, MTAB_DISPLAY,   /* COLOR STYLE LARGE ANIM LEDS */
    MTAB_DISPLAY,                                                           /* SCREEN OFF (1.1.5) */
    MTAB_DISPLAY,                                                           /* SCOPE (1.2) */
    MTAB_DISPLAY,                                                           /* HOME (1.3) */
    MTAB_CONTROL, MTAB_CONTROL, MTAB_CONTROL, MTAB_CONTROL,                 /* HOLD KNOB ACCEL FX LATCH BPM LOCK */
    MTAB_CONTROL,                                                           /* SCALE LEDS (1.2) */
    MTAB_CONTROL, MTAB_CONTROL,                                             /* STEP PREVIEW, CHORD ENTRY (1.2) */
    MTAB_AUDIO, MTAB_AUDIO, MTAB_AUDIO, MTAB_AUDIO, MTAB_AUDIO,             /* SPEAKER EQ, USB LEVEL, CLICK, CLICK LEVEL,
                                                                             * COUNT-IN */
    MTAB_AUDIO,                                                             /* TUNE (1.2) */
    MTAB_MIDI,                                                              /* MIDI IN (1.2) */
    MTAB_MIDI,                                                              /* LEARN CLEAR (1.5) */
    MTAB_SYSTEM, MTAB_SYSTEM, MTAB_SYSTEM,                                  /* USB SERIAL, RESTORE LAST, HELP (1.5) */
    MTAB_SYSTEM, MTAB_SYSTEM, MTAB_SYSTEM,                                  /* CALIBRATION, INFO (1.2), ABOUT */
};
/* TUNE: the project's tuning in cents (G_TUNE, -50..+50); as a row its values 0..TUNE_N - 1 (value - TUNE_MIN). The
 * editor gets it as a number (EDM_INT: the cents, min..max, its unit) */
#define TUNE_MIN (-50)
#define TUNE_N 101u
typedef char mtab_fits_ui[sizeof ui.menu_row >= MTAB_COUNT ? 1 : -1];   /* (ui.c: the row last picked per tab) */
static uint32_t mtab_first(uint32_t t)                 /* a tab's first row */
{
    uint32_t i = 0;
    while (i < MI_COUNT && MI_TAB[i] != t)
        i++;
    return i;
}
static uint32_t mtab_rows(uint32_t t)                  /* .. and how many it has */
{
    uint32_t i, n = 0;
    for (i = 0; i < MI_COUNT; i++)
        n += MI_TAB[i] == t;
    return n;
}
/* STYLE (ui_style, gfx.c ST_*): FLAT the filled cards; LINE black areas divided by 1 px rules (#50, #57: the 0.9 look)
 * (1.0.2: PIXEL retired, a saved PIXEL reads as LINE) */
static const char *const STYLE_N[2] = {"FLAT", "LINE"};
/* the two-valued rows: a bit of ui_prefs (PREF_REC: of ui_rec_prefs, ui.c PREF_SCALE_LEDS) and its names (bit clear =
 * the default, bit set). A step up (a knob right,
 * OCT+) = ON on the ON / OFF rows (the switch's knob to the right; ANIM, USB SERIAL: their bit clears), else the
 * second name (STYLE LINE, USB LEVEL FIXED); a step down the other (menu_step) */
typedef struct { uint8_t row; uint32_t bit; const char *name[2]; } menu_flag_t;
static const menu_flag_t MENU_FLAGS[] = {
    {MI_LARGE, PREF_LARGE, {"OFF", "ON"}},            /* #15 / Discussion #80: ON, big knob labels and values (ui.c large_kind) */
    {MI_ACCEL, PREF_ACCEL, {"OFF", "ON"}},            /* #52: ui_input.c accel */
    {MI_LATCH, PREF_LATCH, {"OFF", "ON"}},
    {MI_USB, PREF_USB_FIXED, {"MASTER", "FIXED"}},     /* fx.c fx_usb_fixed: FIXED, USB at the full level */
    {MI_BPMLOCK, PREF_BPM_LOCK, {"OFF", "ON"}},        /* #58: ON, SELECT sets the tempo with GLO held only (ui_input.c) */
    {MI_SERIAL, PREF_SERIAL_OFF, {"ON", "OFF"}},       /* #67: OFF, no serial console (usb_serial_apply) */
    {MI_RESTORE, PREF_RESTORE_OFF, {"ON", "OFF"}},     /* 1.2, #130: OFF, no autosave, power-on as new (project.c) */
    {MI_SCLLED, PREF_SCALE_LEDS, {"OFF", "ON"}},      /* 1.2, Discussion #127: ON, the keys show the scale (ui_input.c) */
    {MI_SCOPE, PREF_SCOPE_MIX, {"OUT", "MIX"}},        /* 1.2, Discussion #165: MIX, the scope before MASTER (fx.c) */
    {MI_PREVIEW, PREF_PREVIEW, {"OFF", "ON"}},         /* 1.2, Discussion #169: ON, the cursor's step sounds (ui_input.c) */
    {MI_CHADD, PREF_CHORD_ADD, {"HOLD", "ADD"}},       /* 1.2, #155: ADD, keys add to the cursor step (ui_input.c) */
    {MI_HELP, PREF_HELP, {"OFF", "ON"}},               /* 1.5, Discussion #156: ON, a hint per page / layer (ui_help.c) */
};
/* SPEAKER EQ (settings.lowcut, fx.c fx_lowcut): an EQ on the master for the small speaker, not a speaker switch
 * (#42: "OFF" read as the speaker off). FLAT is the old OFF (0, stored as before). The built-in speaker cannot be
 * turned off from the firmware (no amp enable or mute line is known); the EQ reaches
 * the headphone / line out and USB audio too (one DAC, uac_tap reads the master) */
static const char *const SPK_EQ[3] = {"FLAT", "LOWCUT", "BASS+"};
static const char *const HOLD_N[4] = {"0.3 s", "0.4 s", "0.5 s", "0.6 s"};   /* (the editor's names; the menu draws its own) */
/* 1.1 (Discussion #131): the metronome (click.c) and the count-in (seq.c); ui.c ui_rec_prefs holds them */
static const char *const CLICK_N[3] = {"OFF", "REC", "ON"};          /* REC: while a track is armed and playing */
static const char *const CLKLVL_N[3] = {"LOW", "MID", "HIGH"};
static const char *const COUNTIN_N[3] = {"OFF", "1 BAR", "2 BARS"};
/* 1.1.5: SCREEN OFF (ui.c scr_*: the screen dark after this long without panel input; NEVER the default since 1.1.5.1) */
static const char *const SCROFF_N[5] = {"NEVER", "5 MIN", "15 MIN", "30 MIN", "60 MIN"};
/* ANIM (#46: OFF, values snap: ui_draw.c roll_note, ui_graph.c pr_follow; no power-on sweep, main.c boot_leds). 1.2
 * (Discussion #135): IDLE, as ON and the LEDs' idle animation after a minute without input (ui_input.c idle_leds; ui.c
 * ui_idle). Its values keep 1.1's: 0 ON, 1 OFF (the editor's), 2 IDLE; the menu steps them OFF, ON, IDLE (ANIM_ORD) */
static const char *const ANIM_N[3] = {"ON", "OFF", "IDLE"};
static const uint8_t ANIM_ORD[3] = {1, 0, 2};          /* (a value's place in the menu's order, and back: its own inverse) */
/* 1.3 (Discussions #112, #134): what HOME shows under its cards (ui.c ui_home_view): the scope, or the four tracks */
static const char *const HOME_N[HV_COUNT] = {"SCOPE", "TRACKS", "LEVELS"};   /* (1.5, #134: LEVELS) */

/* MENU > USB SERIAL (#67). The serial console is a developer tool (README: FELUCCA_CDC). ON (the default) presents it,
 * the descriptors byte for byte as before; OFF re-enumerates as audio + MIDI only, device class 0 (the bytes of a
 * FELUCCA_CDC=0 build): macOS 13-15 then attach their USB audio driver (with the console Apple's CDC composite driver
 * takes the device and the audio input never appears). USB-MIDI stays, and with it the editor, the update installer
 * and the soft key (SysEx on EP1). Applied at boot before USB starts (main.c: enumerated with it from the start, never
 * on then off) and while the menu is closed (ui_input: one re-enumeration when it closes, not one per KNOB 1 step).
 * A change from the editor (MENU_SET) waits 200 ms (menu_serial_at) so its reply leaves before the device drops off
 * the bus: the whole device re-enumerates, USB-MIDI and audio too */
static uint32_t menu_serial_at;                        /* fm1_ms | 1 of the editor's change; 0 none */
static void usb_serial_apply(void)
{
#if FELUCCA_CDC
    if (menu_serial_at && fm1_ms - (menu_serial_at & ~1u) < 200u)   /* (& ~1: at an even fm1_ms the | 1 was 1 ms ahead) */
        return;
    menu_serial_at = 0;
    usb_cdc_switch(FELUCCA_CDC_DEFAULT && !(ui_prefs & PREF_SERIAL_OFF));   /* (a FELUCCA_CDC_DEFAULT=0 build: off) */
#endif
}
static const menu_flag_t *menu_flag(uint32_t row)
{
    uint32_t i;
    for (i = 0; i < NELEM(MENU_FLAGS); i++)
        if (MENU_FLAGS[i].row == row)
            return &MENU_FLAGS[i];
    return 0;
}

/* a row's value as 0..menu_n(row) - 1 in the order the menu steps it (LEDS: OFF DIM LO DIM HI INV, LEDS_MENU) */
static uint32_t menu_n(uint32_t row)
{
    return row == MI_COLOR ? NPALETTES : row == MI_LOWCUT || (row >= MI_CLICK && row <= MI_COUNTIN) ? 3u :
           row == MI_HOLD ? 4u : row == MI_LEDS ? LEDS_COUNT : row == MI_SCROFF ? NELEM(SCROFF_N) :
           row == MI_TUNE ? TUNE_N : row == MI_MIDIIN ? NELEM(N_ROUTE) : row == MI_ANIM ? 3u : row == MI_HOME ? HV_COUNT : 2u;
}
static uint32_t menu_get(uint32_t row)
{
    const menu_flag_t *f = menu_flag(row);
    uint32_t i = 0;
    if (f)
        return ((ui_prefs | (uint32_t)ui_rec_prefs << 8 | (uint32_t)ui_prefs2 << 16) & f->bit) != 0;
    switch (row) {
    case MI_COLOR: return settings.palette % NPALETTES;
    case MI_STYLE: return ui_style == ST_LINE;
    case MI_LOWCUT: return settings.lowcut % 3u;
    case MI_HOLD: return settings_hold % 4u;
    case MI_SCROFF: return scr_get();
    case MI_ANIM: return ui_prefs & PREF_ANIM_OFF ? 1u : (ui_idle & 1u) ? 2u : 0u;
    case MI_HOME: return ui_home_view < HV_COUNT ? ui_home_view : HV_SCOPE;
    case MI_CLICK: case MI_CLKLVL: case MI_COUNTIN: return rp_get(row - MI_CLICK);
    case MI_TUNE: return (uint32_t)(clamp(song.g[G_TUNE], TUNE_MIN, TUNE_MIN + (int32_t)TUNE_N - 1) - TUNE_MIN);
    case MI_MIDIIN: return (uint32_t)clamp(song.g[G_ROUTE], 0, (int32_t)NELEM(N_ROUTE) - 1);
    case MI_LEDS:
        while (i + 1u < LEDS_COUNT && LEDS_MENU[i] != settings_leds)
            i++;
        return i;
    }
    return 0;
}
static const char *menu_vname(uint32_t row, uint32_t v)
{
    const menu_flag_t *f = menu_flag(row);
    if (f)
        return f->name[v & 1u];
    switch (row) {
    case MI_COLOR: return UI_PALETTES[v % NPALETTES].name;
    case MI_STYLE: return STYLE_N[v & 1u];
    case MI_LOWCUT: return SPK_EQ[v % 3u];
    case MI_HOLD: return HOLD_N[v & 3u];
    case MI_CLICK: return CLICK_N[v % 3u];
    case MI_CLKLVL: return CLKLVL_N[v % 3u];
    case MI_COUNTIN: return COUNTIN_N[v % 3u];
    case MI_SCROFF: return SCROFF_N[v % NELEM(SCROFF_N)];
    case MI_ANIM: return ANIM_N[v % 3u];
    case MI_HOME: return HOME_N[v % HV_COUNT];
    case MI_LEDS: return LEDS_NAME[LEDS_MENU[v % LEDS_COUNT]];
    case MI_MIDIIN: return N_ROUTE[v % NELEM(N_ROUTE)];
    case MI_TUNE: {                                    /* the cents, signed ("+12", "0", "-7"; the menu adds "ct") */
        static char b[8];
        int32_t c = (int32_t)(v % TUNE_N) + TUNE_MIN;
        b[0] = '+';
        fmt_int(b + (c > 0), c);
        return b;
    }
    }
    return "";
}
/* set a row (v below menu_n(row)) and do what the menu does on a change: the palette, the EQ and USB LEVEL at once;
 * STYLE (ui_draw.c style_apply), LARGE, ANIM, LEDS, HOLD, the flags are read where they are used, every frame;
 * USB SERIAL when the menu has closed (usb_serial_apply). Saved by the caller (menu_close, the editor). TUNE and MIDI IN:
 * the project's values, as their page knobs set them up to 1.1.5 (voice.c reads TUNE, seq.c events_block ROUT's change);
 * kept with the project (PROJECT SAVE, the autosave), not by settings_save */
static void menu_put(uint32_t row, uint32_t v)
{
    const menu_flag_t *f = menu_flag(row);
    if (f) {
        if (f->bit & PREF_X)
            ui_prefs2 = (uint8_t)(v ? ui_prefs2 | f->bit >> 16 : ui_prefs2 & ~(f->bit >> 16));
        else if (f->bit & PREF_REC)
            ui_rec_prefs = (uint8_t)(v ? ui_rec_prefs | f->bit >> 8 : ui_rec_prefs & ~(f->bit >> 8));
        else
            ui_prefs = (uint8_t)(v ? ui_prefs | f->bit : ui_prefs & ~f->bit);
        fx_usb_fixed = (ui_prefs & PREF_USB_FIXED) != 0u;   /* (at once; every frame too: ui_input) */
        scope_mix = (ui_prefs2 & (PREF_SCOPE_MIX >> 16)) != 0u;
        return;
    }
    switch (row) {
    case MI_COLOR:
        settings.palette = v;
        palette_set(v);                                /* (the menu signature redraws) */
        ui.force = 1;
        break;
    case MI_STYLE: ui_style = (uint8_t)(v ? ST_LINE : ST_FLAT); break;   /* (drawn so from the next frame) */
    case MI_LOWCUT: settings.lowcut = v; fx_lowcut = (uint8_t)v; break;
    case MI_HOLD: settings_hold = (uint8_t)v; break;
    case MI_SCROFF: scr_put(v); break;                 /* (read every frame: ui.c scr_frame) */
    case MI_ANIM:                                      /* (read where they are used, every frame) */
        ui_prefs = (uint8_t)(v == 1u ? ui_prefs | PREF_ANIM_OFF : ui_prefs & ~PREF_ANIM_OFF);
        ui_idle = (uint8_t)(v == 2u ? ui_idle | 1u : ui_idle & ~1u);
        break;                                         /* (1.5: up to 1.4.1 it fell through: ANIM set HOME too) */
    case MI_HOME:                                      /* (another look: HOME drawn anew) */
        ui_home_view = (uint8_t)(v < HV_COUNT ? v : HV_SCOPE);
        ui.force = 1;
        break;
    case MI_LEDS: settings_leds = LEDS_MENU[v]; break;
    case MI_CLICK: case MI_CLKLVL: case MI_COUNTIN: rp_put(row - MI_CLICK, v); break;   /* (at once: click.c, seq.c) */
    case MI_TUNE: song.g[G_TUNE] = (int16_t)((int32_t)v + TUNE_MIN); break;
    case MI_MIDIIN: song.g[G_ROUTE] = (int16_t)v; break;
    }
}
/* the menu's step, any of KNOB 1..4 or OCT+ (s > 0) / OCT- (s < 0): the next / previous value, stopping at the ends;
 * COLOR wraps round its palettes. On an ON / OFF row up is ON (MENU_FLAGS). TUNE: s cents at a time (101 values: a fast
 * turn goes as far as the knob went, as the old page's knob did) */
static uint32_t menu_step(uint32_t row, int32_t s)
{
    const menu_flag_t *f = menu_flag(row);
    uint32_t v = menu_get(row), n = menu_n(row);
    if (row == MI_TUNE)
        return (uint32_t)clamp((int32_t)v + s, 0, (int32_t)n - 1);
    if (row == MI_ANIM)                                /* OFF ON IDLE, stopping at the ends */
        return ANIM_ORD[clamp((int32_t)ANIM_ORD[v % 3u] + (s > 0) - (s < 0), 0, 2)];
    if (f && str_eq(f->name[0], "ON"))                 /* (ANIM, USB SERIAL, RESTORE LAST: ON is their value 0) */
        s = -s;
    if (s > 0)
        return v + 1u < n ? v + 1u : row == MI_COLOR ? 0u : v;
    if (s < 0)
        return v ? v - 1u : row == MI_COLOR ? n - 1u : 0u;
    return v;
}
