/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Melodee menu (HOME held): COLOR, SPEAKER (LOWCUT), HOLD (the layer threshold), LIGHTS (the keys' and buttons'
 * glow: ui_input.c ui_leds), USB AUDIO (the devices the host gets: IN+OUT, OUT, IN or OFF; usb.c ua_off_set),
 * CALIBRATION (the setup screen: HARDWARE CALIBRATION), ABOUT.
 * PRESETS scrolls from ABOUT through all credits. ui.menu: 1 list, 2 information. */
/* ------------------------------------------------------------ menu --- */
enum { MI_COLOR, MI_LOWCUT, MI_HOLD, MI_LIGHTS,
#if MELODEE_USB_AUDIO
       MI_USB,
#endif
       MI_PANEL, MI_ABOUT, MI_BACK, MI_COUNT };
static const char *const MI_NAME[MI_COUNT] = {"COLOR", "SPEAKER", "HOLD", "LIGHTS",
#if MELODEE_USB_AUDIO
                                              "USB AUDIO",
#endif
                                              "CALIBRATION", "ABOUT", "BACK"};
#if MELODEE_USB_AUDIO
/* USB AUDIO's four settings, as ua_off_want (UA_OFF_OUT | UA_OFF_IN) */
static const char *const MI_USB_NAME[4] = {"IN+OUT", "IN", "OUT", "OFF"};
#endif

/* https://keremimo.github.io/melodee, QR version 3, correction M (segno; checked with a decoder).
 * One row per word, leftmost module in bit 0; the constant matrix needs no encoder or RAM. */
static const uint32_t ABOUT_QR[29] = {
    0x1FC6EB7Fu, 0x104EF341u, 0x175C795Du, 0x1747C65Du, 0x174CB15Du, 0x10417241u,
    0x1FD5557Fu, 0x001C5A00u, 0x1D23F9F9u, 0x0D8F5EA5u, 0x0557D275u, 0x137256B4u,
    0x10C9E87Cu, 0x1FC7940Du, 0x143AD76Du, 0x15706E91u, 0x021C8767u, 0x0D1C8893u,
    0x1309E463u, 0x07A1260Bu, 0x0FFBAC53u, 0x0316B300u, 0x035BAB7Fu, 0x011ABF41u,
    0x1BF9275Du, 0x102DF35Du, 0x1DB1E85Du, 0x16A01241u, 0x00D06B7Fu,
};

static void draw_about_qr(int32_t x, int32_t y)
{
    uint32_t row, col;
    cv_rect(x, y, 74, 74, UI_QR_LIGHT);             /* fixed light / dark, independent of the palette */
    for (row = 0; row < 29u; row++)
        for (col = 0; col < 29u; col++)
            if ((ABOUT_QR[row] >> col) & 1u)
                cv_rect(x + 8 + (int32_t)col * 2, y + 8 + (int32_t)row * 2, 2, 2, UI_QR_DARK);
}

/* All notices are in flash. Wrap at the font's actual width, without dropping URL
 * characters; the same layout computes the scroll limit and draws clipped bands.
 * Mixed case here (long text); S throughout, the title in L. */
#define MENU_DOC_H 196                                /* the document: screen rows 24..220 */
enum { MC_TEXT, MC_TITLE, MC_URL };
typedef struct { const char *text; uint8_t kind; } menu_credit_t;
static const menu_credit_t MENU_CREDITS[] = {
    {"FELUCCA / UPSTREAM", MC_TITLE},                  /* Melodee is a modified Felucca */
    {"H\xFCgelton Instruments, Leo Kuroshita / GPL-3.0", MC_TEXT},
    {"https://github.com/hugelton/Felucca", MC_URL},
    {"", MC_TEXT},
    {"H\xFCgelton Instruments / OWN WORK", MC_TITLE},
    {"Leo Kuroshita", MC_TEXT},
    {"CrispyZebra (PHASE waveforms), synthesized 808: GPL-3.0", MC_TEXT},
    {"Fukiai icon font: MIT", MC_TEXT},
    {"https://github.com/hugelton", MC_URL},
    {"", MC_TEXT},
    {"KLATTSCH / VOICE REFERENCE", MC_TITLE},
    {"Tony Gies / MIT", MC_TEXT},
    {"https://github.com/tgies/klattsch", MC_URL},
    {"", MC_TEXT},
    {"DAISYSP / PHYS PORT", MC_TITLE},
    {"Electrosmith, Emilie Gillet", MC_TEXT},
    {"MIT", MC_TEXT},
    {"https://github.com/electro-smith/DaisySP", MC_URL},
    {"", MC_TEXT},
    {"uPD933 / NATIVE CZ", MC_TITLE},
    {"Devin Acker / BSD-3-Clause", MC_TEXT},
    {"https://github.com/mamedev/mame", MC_URL},
    {"", MC_TEXT},
    {"DEXED + MSFA / FM6 PORT", MC_TITLE},
    {"Pascal Gauthier, Google Inc.", MC_TEXT},
    {"msfa: Apache-2.0, Dexed: GPL-3.0", MC_TEXT},
    {"https://github.com/asb2m10/dexed", MC_URL},
    {"", MC_TEXT},
    {"RINGS / PHYS PORT", MC_TITLE},
    {"Emilie Gillet / MIT", MC_TEXT},
    {"https://github.com/pichenettes/eurorack", MC_URL},
    {"", MC_TEXT},
    {"INTER TIGHT / FONT", MC_TITLE},
    {"The Inter Project Authors", MC_TEXT},
    {"SIL Open Font License 1.1", MC_TEXT},
    {"https://github.com/rsms/inter-tight", MC_URL},
    {"", MC_TEXT},
    {"VSCO-2 CE + VCSL / SAMPLES", MC_TITLE},
    {"Versilian Studios / CC0 1.0", MC_TEXT},
    {"https://github.com/sgossner/VSCO-2-CE", MC_URL},
    {"https://github.com/sgossner/VCSL", MC_URL},
    {"", MC_TEXT},
    {"VOICE / RESEARCH", MC_TITLE},
    {"Dennis H. Klatt (1980)", MC_TEXT},
    {"https://doi.org/10.1121/1.383940", MC_URL},
    {"Hillenbrand, Getty, Clark and Wheeler (1995)", MC_TEXT},
    {"https://doi.org/10.1121/1.411872", MC_URL},
};

static int32_t menu_text(const char *s, int32_t y, uint16_t col, int draw)
{
    while (*s) {
        char line[64];
        uint32_t n = 0, space = 0, take, prev = 0;
        int32_t pen = 0;                                 /* 1/16 px, as text_w adds it up */
        while (s[n] && n < sizeof line - 1u) {
            uint32_t c = (uint8_t)s[n];
            line[n] = s[n];
            line[n + 1u] = 0;
            pen += (prev ? kern(&AF_S, prev, c) : 0) + AF_S.g[glyph(&AF_S, c)].adv;
            prev = c;
            if ((pen + 8) >> 4 > 226)
                break;
            if (s[n] == ' ')
                space = n;
            n++;
        }
        take = s[n] && space ? space : n;
        if (!take) take = 1;                         /* progress even for an unusually wide glyph */
        line[take] = 0;
        if (draw && y + AF_S.h > 0 && y < MENU_DOC_H)
            cv_text(6, y, &AF_S, line, col);
        y += 18;
        s += take;
        while (*s == ' ') s++;
    }
    return y;
}

static int32_t menu_document(int32_t y, int draw)
{
    uint32_t i;
    if (draw) {
        cv_text(6, y + 2, &AF_L, "MELODEE", T_THEME);
        cv_text(6, y + 38, &AF_S, "Multi-engine synthesizer", T_TEXT);
        cv_text(6, y + 58, &AF_M, MELODEE_VERSION, T_THEME);
        cv_text_r(232, y + 61, &AF_S, __DATE__, T_MID, T_BG);
        cv_text(6, y + 82, &AF_S, "(C) 2026 Kerem Kilic, Ellic Studio", T_TEXT);
        cv_text(6, y + 100, &AF_S, "Based on Felucca by Leo Kuroshita,", T_TEXT);
        cv_text(6, y + 118, &AF_S, "H\xFCgelton Instruments", T_TEXT);
        cv_text(6, y + 138, &AF_S, "GPL-3.0-only", T_TEXT);
        cv_text(6, y + 156, &AF_S, "keremimo.github.io", T_MID);
        cv_text(6, y + 174, &AF_S, "/melodee", T_MID);
        draw_about_qr(158, y + 122);
    }
    y = menu_text("CREDITS", y + 216, T_THEME, draw) + 8;
    for (i = 0; i < NELEM(MENU_CREDITS); i++) {
        const menu_credit_t *c = &MENU_CREDITS[i];
        const char *text = c->text;
        /* Display host/path once; the full source URL stays in the flash table. */
        if (c->kind == MC_URL && str_len(text) >= 8u && !memcmp(text, "https://", 8))
            text += 8;
        if (!text[0]) y += 8;
        else y = menu_text(text, y, c->kind == MC_TITLE ? T_THEME : c->kind == MC_URL ? T_MID : T_TEXT, draw);
    }
    y = menu_text("LICENSES + SOURCE", y + 12, T_THEME, draw);
    y = menu_text("DRUM + own samples: GPL-3.0-only", y, T_TEXT, draw);
    y = menu_text("Fonts and icons: their licences travel with the source and the release files.", y, T_TEXT, draw);
    y = menu_text("NO WARRANTY", y, T_TEXT, draw);
    y = menu_text("Full notices in source:", y + 8, T_TEXT, draw);
    y = menu_text("github.com/keremimo/melodee", y, T_MID, draw);
    return y + 4;
}
static int32_t menu_scroll_max(void)
{
    int32_t h = menu_document(0, 0);
    return h > MENU_DOC_H ? h - MENU_DOC_H : 0;
}

/* the menu's header (0..24): its icon and title, the REC mark, the way back */
static void menu_head(void)
{
    cv_begin(240, H_HEAD, T_BG);
    cv_icon_on(8, 4, 16, ui.menu == 2 ? ICON_X_INFO : ICON_X_COG, T_THEME, T_BG);
    cv_text(30, 3, &AF_M, ui.menu == 2 ? "ABOUT / CREDITS" : "MENU", T_TEXT);
    if (ui.menu >= 2) {                               /* the way back at the right, the REC mark before it (then */
        const char *w = song.rec ? 0 : "BACK";        /* the keycap goes without its word: no room for both) */
        int32_t x = 232 - kh_w(KC_OCTDN, w);
        cv_key_hint(x, 6, KC_OCTDN, w, 1, T_BG);
        draw_rec_mark(x - 20, T_BG);
    } else {
        draw_rec_mark(178, T_BG);
    }
    cv_blit(0, Y_HEAD);
}

/* the list: SURF rows (eight items: 20 px, 22 apart from y 28; seven: 23 px, 25 apart), the selected one THEME
 * with INK; a 16 px icon, the name (S), the value (M) at the right, centred in the row (MENU_DY); COLOR shows the
 * palette's five colours. Drawn in two bands (the canvas holds 124 rows), split between two rows */
#if MELODEE_USB_AUDIO
#define MENU_Y0 28
#define MENU_ROW 22
#define MENU_RH 20
#define MENU_SPLIT 137
#else
#define MENU_Y0 28
#define MENU_ROW 25
#define MENU_RH 23
#define MENU_SPLIT 127
#endif
#define MENU_DY ((MENU_RH - 24) / 2)                  /* the contents of a 24 px row, centred */
static const khint_t MENU_KEYS[3] = {{KC_PRESETS, "MOVE"}, {KC_OCTUP, "OK"}, {KC_OCTDN, "BACK"}};
static void draw_menu(void)
{
    static const uint16_t ICO[MI_COUNT] = {ICON_X_PALETTE, ICON_X_SPEAKER, ICON_X_TIMER, ICON_X_STAR_O,
#if MELODEE_USB_AUDIO
                                           ICON_X_USB,
#endif
                                           ICON_X_DOCTOR, ICON_X_INFO, ICON_X_BACK};
    uint32_t i, pass, sig = ui.menu * 7u + ui.menu_sel * 131u + settings.palette * 1009u + settings.lowcut * 7919u +
                            settings_hold * 3511u + settings_lights * 6151u +
#if MELODEE_USB_AUDIO
                            ua_off_want * 92821u +
#endif
                            song.rec * 65537u + song.sel * 13u +
                            (ui.menu == 2 ? ui.menu_scroll * 48611u : 0u);
    if (!ui.force && sig == ui.menu_sig)
        return;
    ui.menu_sig = sig;
    menu_head();
    if (ui.menu >= 2) {
        int32_t scroll = ui.menu_scroll, max = menu_scroll_max();
        int32_t thumb = MENU_DOC_H * MENU_DOC_H / (max + MENU_DOC_H);
        if (thumb < 12) thumb = 12;
        for (pass = 0; pass < 2u; pass++) {
            cv_begin(240, pass ? MENU_DOC_H - 124u : 124u, T_BG);
            cv_oy = pass ? -124 : 0;
            cv_scroll = 1;
            menu_document(-scroll, 1);
            cv_scroll = 0;
            cv_rrect(235, 2, 3, MENU_DOC_H - 4, 1, T_LINE, T_BG);           /* the scroll bar */
            cv_rrect(235, 2 + (max ? scroll * (MENU_DOC_H - 4 - thumb) / max : 0), 3, thumb, 1, T_MID, T_LINE);
            cv_oy = 0;
            cv_blit(0, H_HEAD + pass * 124u);
        }
        cv_begin(240, 240 - (H_HEAD + MENU_DOC_H), T_BG);
        cv_key_hint(8, 4, KC_PRESETS, "SCROLL", 1, T_BG);
        cv_blit(0, H_HEAD + MENU_DOC_H);
        return;
    }
    for (pass = 0; pass < 2u; pass++) {
        int32_t top = pass ? MENU_SPLIT : H_HEAD;
        cv_begin(240, (uint32_t)(pass ? 240 - MENU_SPLIT : MENU_SPLIT - H_HEAD), T_BG);
        cv_oy = -top;                                 /* drawn in screen rows */
        for (i = 0; i < MI_COUNT; i++) {
            int32_t y = MENU_Y0 + (int32_t)i * MENU_ROW, yt;
            int sel = i == ui.menu_sel;
            uint16_t bg = sel ? T_THEME : T_SURF, fg = sel ? T_INK : T_TEXT, val = sel ? T_INK : T_THEME;
            if (y + MENU_RH <= top || y >= top + (int32_t)cv_h)
                continue;
            cv_rrect(4, y, 232, MENU_RH, 6, bg, T_BG);
            yt = y + MENU_DY;
            cv_icon_on(12, yt + 4, 16, ICO[i], sel ? T_INK : T_MID, bg);
            cv_text_on(36, yt + 5, &AF_S, MI_NAME[i], fg, bg);
            if (i == MI_LIGHTS)
                cv_text_r(228, yt + 3, &AF_M, LIGHTS_NAME[settings_lights % LIGHTS_N], val, bg);
            if (i == MI_LOWCUT)
                cv_text_r(228, yt + 3, &AF_M, (const char *const[]){"OFF", "LOWCUT", "BASS+"}[settings.lowcut % 3u], val, bg);
#if MELODEE_USB_AUDIO
            if (i == MI_USB)
                cv_text_r(228, yt + 3, &AF_M, MI_USB_NAME[ua_off_want & 3u], val, bg);
#endif
            if (i == MI_HOLD) {                         /* "0.4" and its unit */
                char b[8] = "0.4";
                b[2] = (char)('0' + HOLD_MS[settings_hold % 4u] / 100u);
                cv_text_r(cv_text_r(228, yt + 6, &AF_S, "s", val == T_INK ? T_INK : T_MID, bg) - 3, yt + 3, &AF_M, b, val, bg);
            }
            if (i == MI_COLOR) {
                uint32_t k;
                const uint16_t *tok = &T_BG;            /* BG SURF TEXT THEME ACCENT */
                cv_text_r(146, yt + 3, &AF_M, UI_PALETTES[settings.palette % NPALETTES].name, val, bg);
                for (k = 0; k < 5u; k++) {
                    cv_rrect(152 + (int32_t)k * 15, yt + 6, 12, 12, 3, T_RAISE, bg);     /* a rim for the dark ones */
                    cv_rrect(153 + (int32_t)k * 15, yt + 7, 10, 10, 2, tok[k], T_RAISE);
                }
            }
        }
        cv_key_row(8, 232, 207, MENU_KEYS, 3, 7u, T_BG);
        cv_oy = 0;
        cv_blit(0, (uint32_t)top);
    }
}
static void enc_drop(void)                             /* knob turns nobody takes */
{
    uint32_t k;
    for (k = 0; k < NE; k++)
        panel_enc(k);
}

static void menu_close(void)
{
    settings_save();                                   /* palette / panel table, if changed */
    ui.menu = 0;
    ui.force = 1;
    go_home();
}

/* menu: PRESETS moves, OCT+ confirms, OCT- cancels (ABOUT -> list -> close) */
static void menu_input(uint32_t oct)                  /* oct: ui_input.c oct_taps */
{
    int32_t s;
    uint32_t ok = (oct >> 1) & 1u, back = oct & 1u;
    if (back) {
        if (ui.menu >= 2)
            ui.menu = 1, ui.force = 1;
        else
            menu_close();
        return;
    }
    if ((s = panel_enc(EN_PRESET)) != 0 && ui.menu == 1)
        ui.menu_sel = (uint8_t)((ui.menu_sel + (s > 0 ? 1u : MI_COUNT - 1u)) % MI_COUNT);
    else if (s != 0 && ui.menu >= 2) {             /* bounded document scrolling */
        ui.menu_scroll = (uint16_t)clamp((int32_t)ui.menu_scroll + clamp(s, -128, 128) * 18, 0, menu_scroll_max());
    }
    s = panel_enc(EN_K1);
    if (s != 0 && ui.menu == 1 && ui.menu_sel == MI_COLOR) {
        settings.palette = (settings.palette + (s > 0 ? 1u : NPALETTES - 1u)) % NPALETTES;   /* (wraps) */
        palette_set(settings.palette);              /* (the menu signature redraws) */
        ui.force = 1;
    }
    if ((s != 0 || ok) && ui.menu == 1 && ui.menu_sel == MI_LOWCUT) {
        /* KNOB 1: right = ON, left = OFF; OCT+ toggles (SPEAKER: OFF LOWCUT BASS+, OCT+ steps) */
        uint32_t *v = &settings.lowcut, top = 2u;
        *v = s > 0 ? (*v < top ? *v + 1u : top) : s < 0 ? (*v ? *v - 1u : 0u) : (*v < top ? *v + 1u : 0u);
        fx_lowcut = (uint8_t)(settings.lowcut % 3u);
        ok = 0;
    }
#if MELODEE_USB_AUDIO
    if ((s != 0 || ok) && ui.menu == 1 && ui.menu_sel == MI_USB) {
        /* KNOB 1 steps IN+OUT IN OUT OFF, OCT+ steps and wraps; the host gets it once it rests (main.c) */
        uint32_t v = ua_off_want & 3u;
        v = s > 0 ? (v < 3u ? v + 1u : 3u) : s < 0 ? (v ? v - 1u : 0u) : (v + 1u) % 4u;
        ua_off_set(UA_OFF_OUT, !(v & UA_OFF_OUT), fm1_ms);
        ua_off_set(UA_OFF_IN, !(v & UA_OFF_IN), fm1_ms);
        ok = 0;
    }
#endif
    if ((s != 0 || ok) && ui.menu == 1 && ui.menu_sel == MI_LIGHTS) {
        /* KNOB 1 steps OFF .. FULL, OCT+ steps and wraps (saved when the menu closes) */
        uint32_t v = settings_lights % LIGHTS_N;
        settings_lights = (uint8_t)(s > 0 ? (v + 1u < LIGHTS_N ? v + 1u : v) : s < 0 ? (v ? v - 1u : 0u) : (v + 1u) % LIGHTS_N);
        ok = 0;
    }
    if ((s != 0 || ok) && ui.menu == 1 && ui.menu_sel == MI_HOLD) {
        /* KNOB 1 steps 0.3 .. 0.6 s, OCT+ steps and wraps */
        uint32_t v = settings_hold % 4u;
        settings_hold = (uint8_t)(s > 0 ? (v < 3u ? v + 1u : 3u) : s < 0 ? (v ? v - 1u : 0u) : (v + 1u) % 4u);
        ok = 0;
    }
    if (ok && ui.menu == 1) {
        switch (ui.menu_sel) {
        case MI_COLOR:                                 /* OCT+ steps through the palettes too */
            settings.palette = (settings.palette + 1u) % NPALETTES;
            palette_set(settings.palette);
            ui.force = 1;
            break;
        case MI_PANEL:
            panel_setup();
            ui.force = 1;
            break;
        case MI_ABOUT:
            ui.menu = 2;
            ui.menu_scroll = 0;
            ui.force = 1;
            break;
        default:
            menu_close();
            break;
        }
    }
    enc_drop();                                        /* swallow the rest while the menu is up */
}
