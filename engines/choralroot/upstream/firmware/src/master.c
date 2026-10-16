/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* MASTER, the pot (main.c; the emulator: tools/emu/emu_fw.c, the same two functions on its own pot).
 * Felucca 1.1.5's fix for its issue #137 (commit 276f72a, firmware/src/master.c, GPL-3.0-only), ported in 0.19: the pot
 * is read BEFORE the audio starts, so the first block already plays at the knob's level, and it is followed under the
 * splash. Until then MASTER was felucca_init's 2048 (70 % of the knob's travel) until the main loop's first read,
 * about 430 ms after the audio had started, and from there the smoothed value glided from mid-scale an eighth of the
 * way a UI frame: about 0.4 s more.
 * ChoralRoot's own part of it: its power-on (cr_ui.c cr_ui_init, the main loop's first pass) set MASTER to 2048 once
 * more; cr_out.c cr_out_init, which it calls next and which opens the engine to the audio ISR, puts the pot's level
 * back (fx.c fx_master_now: master_pot_q12, and Output Clean's eased MASTER, which would otherwise ramp from the
 * default over the first block). What sounded in that window on a ChoralRoot unit: nothing under the splash (the
 * engine takes no key and no MIDI before cr_out_init), then a key held at power-on or struck in the first half second,
 * a MIDI note, at up to the default's level whatever the knob said (docs/AUDIO-QUALITY.md "Round 3").
 * master_knob: the pot x 16, smoothed a UI frame (master_poll). */
static int32_t master_knob = 512 * 16;
#ifndef MASTER_POT_READ
#define MASTER_POT_READ() fm1_adc_read(FM1_ADC_MASTER)   /* main.c: the scan's sample once TIMER5 owns the converter */
#endif
static inline uint32_t master_of_pot(uint32_t k10) { return (k10 * k10) >> 8; }   /* 0 .. ~4096 */
static void master_poll(void)
{
    int32_t a = MASTER_POT_READ();
    if (a >= 0) {
        master_knob += (a * 16 - master_knob) / 8;
        song.master_q12 = master_of_pot((uint32_t)(master_knob / 16));
        master_pot_q12 = song.master_q12 + 1u;          /* (fx.c: what cr_out_init goes back to) */
    }
}
/* power-on, before audio_init: the knob as it stands, no glide */
static void master_boot(void)
{
    uint32_t i;
    (void)fm1_adc_read(FM1_ADC_MASTER);                 /* (the first conversion after the pin setup: dropped) */
    for (i = 0; i < 3u; i++) {
        int32_t a = fm1_adc_read(FM1_ADC_MASTER);
        if (a >= 0) {
            master_knob = a * 16;
            fx_master_now(master_of_pot((uint32_t)a));
            return;
        }
    }                                                   /* (no reading: felucca_init's level, the loop glides on) */
}
