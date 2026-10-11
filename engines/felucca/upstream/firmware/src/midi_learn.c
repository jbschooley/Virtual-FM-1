/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* MIDI LEARN (1.5, Discussion #170): a controller's CC set to any knob's parameter, on the device. GLO held + D4
 * (LEARN, ui_layer.c) turns it on and off. On: a knob turned on any page (HOME, the sound's pages, MIXER: the
 * parameter of a track it edits) picks its parameter on the selected track, as it edits it; the next CC that comes
 * (midi_control.c midi_learned: any channel ROUT hears) is set to it, and stays until something else is learned on it.
 * Then turn the next knob. OCT- clears the picked parameter's CC, OCT+ (or GLO + D4 again) is done. The footer's first
 * row says what is picked and its CC (ml_line). The map: midi_control.c (ML_N entries in favorites.factory[14], kept
 * with the settings, saved as each one changes); one CC sets one parameter, a parameter has one CC: learning one again
 * replaces both. The editor reads and edits it (editor_learn.c). Its state: ui.ml (ui.c) */
typedef char ml_row_free[NENGINES <= 14 ? 1 : -1];   /* (factory[14]: no engine's favourites; favorites.c) */
static uint8_t *const ml_tab = favorites.factory[14];
static volatile uint8_t *const ml_io = ui.ml.io;         /* (midi_control.c ml_arm, ml_heard) */

static uint32_t ml_code(uint32_t id) { return id >= P_E0 ? ML_E0 + id - P_E0 : id + 1u; }
static uint32_t ml_get(uint32_t i) { return ml_tab[2u * i] | (uint32_t)ml_tab[2u * i + 1u] << 8; }
static void ml_put(uint32_t i, uint32_t e)       /* (the ISR reads the two bytes: never half of one) */
{
    fm1_irq_off();
    ml_tab[2u * i] = (uint8_t)e;
    ml_tab[2u * i + 1u] = (uint8_t)(e >> 8);
    fm1_irq_on();
}
/* the CC of parameter id on track k, + 1; 0 none */
static uint32_t ml_cc_of(uint32_t k, uint32_t id)
{
    uint32_t i, e;
    for (i = 0; i < ML_N; i++)
        if (((e = ml_get(i)) >> 9) == ml_code(id) && ((e >> 7) & 3u) == k)
            return (e & 127u) + 1u;
    return 0;
}
static uint32_t ml_count(void)
{
    uint32_t i, n = 0;
    for (i = 0; i < ML_N; i++)
        n += (ml_get(i) >> 9) != 0u;
    return n;
}
/* takes away cc's entry and parameter id of track k's (either: ML_N / P_COUNT for none); 1 = one went */
#define ML_NOCC 0x80u                                   /* ml_drop: no CC to match (a learned CC is 0..127) */
static int ml_drop(uint32_t cc, uint32_t k, uint32_t id)
{
    uint32_t i, e, gone = 0;
    for (i = 0; i < ML_N; i++) {
        if (!((e = ml_get(i)) >> 9))
            continue;
        if ((e & 127u) == cc || ((e >> 9) == ml_code(id) && ((e >> 7) & 3u) == k)) {
            ml_put(i, 0);
            gone = 1;
        }
    }
    return gone;
}
/* cc -> parameter id of track k, in place of what either had; 0 = no room (ML_N others), 2 = a CC that is never learned */
static uint32_t ml_learn(uint32_t cc, uint32_t k, uint32_t id)
{
    uint32_t i;
    if (!ml_free_cc(cc) || k >= NTRK || id >= P_COUNT)
        return 2;
    (void)ml_drop(cc, k, id);
    for (i = 0; i < ML_N; i++)
        if (!(ml_get(i) >> 9)) {
            ml_put(i, cc | k << 7 | ml_code(id) << 9);
            return 1;
        }
    return 0;
}

/* every learned CC cleared (MENU > MIDI > LEARN CLEAR, the editor's LEARN_SET op 2), the settings saved; 1 = there were */
static int ml_clear_all(void)
{
    uint32_t i, n = 0;
    for (i = 0; i < ML_N; i++)
        if (ml_get(i)) {
            ml_put(i, 0);
            n++;
        }
    if (n) {
        ui.foot_sig = 0;
        settings_save();                         /* (deferred while playing) */
    }
    return n != 0u;
}
static void ml_off(void)
{
    ui.ml.on = ui.ml.pick = 0;
    ml_arm = 0;
    ml_heard = 0;
    ui.foot_sig = 0;
}
static void ml_toggle(void)
{
    if (ui.ml.on) {
        ml_off();
        ui_message("LEARN DONE");
        return;
    }
    ui.ml.on = 1;
    ui.ml.pick = 0;
    ml_heard = 0;
    ui_message("MIDI LEARN ON");
}
/* a knob turned parameter id of track t (ui_input.c): picked while learning */
static void ml_knob(const track_t *t, uint32_t id)
{
    if (!ui.ml.on || id >= P_COUNT)
        return;
    ui.ml.trk = (uint8_t)trk_index(t);
    ui.ml.id = (uint8_t)id;
    ui.ml.pick = 1;
    ml_heard = 0;
    ml_arm = 1;
}
/* the picked parameter's name: "T2 CUT" (b: 12 bytes) */
static void ml_name(char *b)
{
    b[0] = 'T';
    b[1] = (char)('1' + ui.ml.trk);
    b[2] = ' ';
    str_cpy(b + 3, track_desc(&trk[ui.ml.trk % NTRK], ui.ml.id)->label, 9);
}
/* each frame: a CC heard for the picked parameter is learned */
static void ml_poll(void)
{
    uint32_t cc = ml_heard, r;
    char a[12], b[12];
    if (!cc)
        return;
    ml_heard = 0;
    if (!ui.ml.on || !ui.ml.pick)
        return;
    cc--;
    r = ml_learn(cc, ui.ml.trk, ui.ml.id);
    if (r != 1u) {
        ui_message(r ? "CC NOT LEARNABLE" : "LEARN FULL");
        return;
    }
    a[0] = 'C';
    a[1] = 'C';
    fmt_int(a + 2, (int32_t)cc);
    str_cpy(a + str_len(a), " -> ", 5);
    ml_name(b);
    ui_say(a, b);
    ui.ml.pick = 0;
    ml_arm = 0;
    ui.foot_sig = 0;
    settings_save();                             /* (deferred while playing) */
}
/* OCT- / OCT+ while learning (not on an action page): OCT- clears the picked parameter's CC, OCT+ is done */
static void ml_oct(uint32_t oct)
{
    char b[12];
    if (oct & 2u) {
        ml_off();
        ui_message("LEARN DONE");
    } else if (oct & 1u) {
        if (!ui.ml.pick || !ml_drop(ML_NOCC, ui.ml.trk, ui.ml.id)) {
            ui_message(ui.ml.pick ? "NO CC TO CLEAR" : "TURN A KNOB");
            return;
        }
        ml_name(b);
        ui_say(b, " CC CLEARED");
        ui.foot_sig = 0;
        settings_save();
    }
}
/* the footer's first row while learning: "TURN A KNOB", "T2 CUT: SEND A CC", "T2 CUT = CC74" */
static void ml_line(char *b)
{
    uint32_t cc;
    if (!ui.ml.pick) {
        str_cpy(b, "LEARN: TURN A KNOB", 24);
        return;
    }
    ml_name(b);
    cc = ml_cc_of(ui.ml.trk, ui.ml.id);
    if (!cc) {
        str_cpy(b + str_len(b), ": SEND A CC", 12);
        return;
    }
    str_cpy(b + str_len(b), " = CC", 6);
    fmt_int(b + str_len(b), (int32_t)cc - 1);
}
