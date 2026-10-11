/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* HELP (1.5, Discussion #156; MENU > SYSTEM > HELP, OFF by default): a short, visual hint per page and per layer, key
 * caps and two or three words, never a screen of text.
 *   a page   entered (a page button, HOME): its hint in the footer's first row for HELP_FRAMES (~2 s; in place of the
 *            steps or the OCT+ / OCT- hint, which come back after), ui_draw.c draw_foot
 *   a layer  while its map shows: a second footer row under the layer's own (ui_layer.c draw_layer)
 * A hint is "[KEYCAP]WORDS" up to three times (gfx.c kc_tag names the keycaps); help_parse splits it for cv_key_row.
 * Pages by their title (a page added later has none until it is written here); HOME, HOME LEVELS and the DRUM grid by
 * what they are. Every hint fits its row with every word (tests/ui_test.c test_help: no word dropped). The setting and
 * the timer: ui.c help_on, help_start (PREF_HELP, ui.help_t) */

static const char H_EDIT[] = "[K1-4]SOUND[EDIT]HOLD: ENGINES";
static const char H_LANES[] = "[K1-4]LANE LEVELS[EDIT]HOLD: MUTES";
static const char H_FMOP[] = "[K1]OPERATOR[SAVE]HOLD: UNDO";
static const struct { const char *title, *hint; } HELP_PAGE[] = {
    {"ENV", "[K1-4]A D S R[ENV]ENV DEST"},
    {"ENV DEST", "[K1-4]ENV AMOUNTS[ENV]ENV"},
    {"LFO", "[K1-4]SHAPE[LFO]HOLD: MOMENTARY"},
    {"LFO 2", "[K1]SYNC TO BPM[LFO]LFO DEST"},
    {"LFO DEST", "[K1-4]LFO AMOUNTS[LFO]MOD"},
    {"MOD", "[K1]SLOT[K2]SOURCE[K3]DEST"},
    {"FX", "[K1-4]SENDS[FX]HOLD: PERFORM"},
    {"SLICER", "[K1]ON[K2]PATTERN[FX]INSERT"},
    {"INSERT", "[K1]TYPE[FX]DRY / WET"},
    {"INSERT 2", "[K1]DRY / WET[FX]DLY"},
    {"DLY", "[K1-4]ALL TRACKS[FX]REVERB"},
    {"REVERB", "[K1]ROOM SPRING HALL[FX]CHORUS"},
    {"CHORUS", "[K1-4]ALL TRACKS[FX]FX"},
    {"SCL", "[K2]SCALE[K3]QUANTIZE[SCL]CHORDS"},
    {"CHORD", "[K1]CHORD[KEYS]ONE FINGER[SCL]SCL"},
    {"EDIT 1", H_EDIT},
    {"EDIT 2", H_EDIT},
    {"FILTER", "[K1]LP BP HP[K2]CUTOFF[EDIT]VOICE"},
    {"LANES", H_LANES},
    {"LANES 2", H_LANES},
    {"SLICES", "[K1]SLICE[K2]MOVE[OCT+]SPLIT, JOIN"},
    {"OPERATOR", H_FMOP},
    {"OP ENV", H_FMOP},
    {"OPERATOR 2", H_FMOP},
    {"VOICE", "[K1]POLY, MONO[K2]GLIDE"},
    {"VOICE 2", "[K2]DETUNE[K3]PAN[K4]MUTE"},
    {"VOICE 3", "[K1]STEREO SPREAD[EDIT]EDIT 1"},
    {"SONG", "[K1]SECTION[KEYS]A-D[PLAY]SONG"},
    {"USER", "[K1]SLOT[OCT+]SAVE[SAVE]PRESETS"},
    {"PRESETS", "[K2]ENGINE[K3]FAV[K4]LIST"},
    {"PHRASES", "[K1]PHRASE[OCT+]LOAD[SAVE]PROJECT"},
    {"PROJECT", "[K1]SLOT[K4]SAVE[EDIT]NAME"},
    {"TOOLS", "[K1]CLEAR[K2]INIT SOUND[OCT+]DO"},
    {"ARP", "[K1]MODE[KEYS]CHORD[ARP]ARP 2"},
    {"ARP 2", "[K3]LATCH[K4]ORDER[ARP]ARP"},
    {"STEP", "[KEYS]WRITE[K1]STEP[SEQ]RATCH"},   /* (1.5.1: SEQ to DETAIL) */
    {"DETAIL", "[PRESETS]STEP[K1]CHANCE[K2]RATCH"},
    {"AUTOMATION", "[K1]ROW[OCT+]DO[EDIT]DELETE"},
    {"MIXER", "[K4]MUTE[ALGO]TRACK[HOME]CLOCK"},
    {"CLOCK", "[SELECT]TEMPO[GLO]HOLD: TAP"},
};
#define H_HOME "[PRESETS]SOUND[HOME]HOLD: MENU"
#define H_LEVELS "[K1-4]T1-T4 LEVEL[HOME]MIXER"
#define H_GRID "[KEYS]STEPS[K2]LANE[SEQ]RATCH"
/* a layer's (LAYER_*): what its footer row (LAYERS[].foot) does not say */
static const char *const HELP_LAYER[LAYER_N] = {
    "", "[KEYS]BLACK: MUTES[FX]2 TAPS: LOCK", "[KEYS]SOLO MUTE TAP[K1-4]LEVELS", "[KEYS]ROOT[K1-4]ROOT SCL CHORD",
    "[K1]ENGINE[K2]SOUND[K3]FAV", "[K1]LENGTH[K3]SWING[K4]GATE", "[K1]CLICK[K2]COUNT-IN[K3]LEVEL",
};

/* the hint of the page shown, 0 = none */
static const char *help_page(void)
{
    uint32_t i;
    const char *t;
    if (ui.home)
        return home_levels() ? H_LEVELS : H_HOME;
    if (grid_on() && !detail_on())
        return H_GRID;
    t = cur_page()->title;
    for (i = 0; i < NELEM(HELP_PAGE); i++)
        if (str_eq(HELP_PAGE[i].title, t))
            return HELP_PAGE[i].hint;
    return 0;
}
/* "[K1]SLOT[OCT+]SAVE" -> kh[] (at most 3), their words in b (bn bytes); the number of hints */
static uint32_t help_parse(const char *s, khint_t *kh, char *b, uint32_t bn)
{
    uint32_t n = 0, i = 0, len;
    int32_t id;
    while (s && n < 3u && (id = kc_tag(s, &len)) >= 0) {
        s += len;
        kh[n].key = (uint8_t)id;
        kh[n++].act = b + i;
        while (*s && *s != '[' && i + 2u < bn)
            b[i++] = *s++;
        b[i++] = 0;
    }
    return n;
}
/* a hint as a row of key hints from x 8 to 232 at y (the footer's rows: 2, 21); 0 = nothing to draw */
static int help_row(const char *s, int32_t y)
{
    khint_t kh[3];
    char b[48];
    uint32_t n = help_parse(s, kh, b, sizeof b);
    if (n)
        cv_key_row(8, 232, y, kh, n, 7u, T_BG);
    return n != 0u;
}
