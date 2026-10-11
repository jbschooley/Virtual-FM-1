/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* MIDI LEARN's map for the editor (1.5, EDITOR_PROTOCOL.md "MIDI LEARN"; INFO 43 01 ML_N): LEARN_GET lists it,
 * LEARN_SET learns one CC, clears one, or clears all. The device's own rules (midi_learn.c ml_learn): one CC sets one
 * parameter, a parameter has one CC; CC0 1 6 11 32 38 64 96..101 120..127 are never learned */
enum { ED_LEARN_GET = 78, ED_LEARN_SET };
/* the map: ML_N, then per entry used (0 / 1), CC, track, parameter id (this firmware's P_*; an empty entry 0 0 0 0, one
 * of a parameter this firmware does not know: used 0) */
static void ed_learn_list(void)
{
    uint32_t i, e, id;
    ed_b(ML_N);
    for (i = 0; i < ML_N; i++) {
        e = ml_get(i);
        id = ml_id(e >> 9);
        if (!(e >> 9) || id >= P_COUNT) {
            ed_b(0); ed_b(0); ed_b(0); ed_b(0);
            continue;
        }
        ed_b(1); ed_b(e & 127u); ed_b((e >> 7) & 3u); ed_b(id);
    }
}
static int ed_learn_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    uint32_t rc = 1, ch = 0;
    if (cmd == ED_LEARN_GET) {
        ed_learn_list();
        return 1;
    }
    if (cmd != ED_LEARN_SET)
        return 0;
    if (n == 4u && a[0] == 0u) {                       /* 0, cc, track, id: learn */
        rc = ml_learn(a[1], a[2], a[3]);
        rc = rc == 1u ? 0u : rc ? 1u : 5u;              /* (5: ML_N others, no room) */
        ch = !rc;
    } else if (n == 2u && a[0] == 1u) {                /* 1, cc: its entry cleared */
        rc = 0;
        ch = (uint32_t)ml_drop(a[1], NTRK, P_COUNT);
    } else if (n == 1u && a[0] == 2u) {                /* 2: all cleared (as MENU > MIDI > LEARN CLEAR) */
        rc = 0;
        ch = (uint32_t)ml_clear_all();
    }
    if (ch) {
        ui.force = 1;
        ui.foot_sig = 0;
    }
    ed_b(rc ? rc : ed_ui_save());                      /* rc as UI_SET: 0 saved, 3 RAM only, 4 queued until STOP */
    ed_learn_list();
    return 1;
}
