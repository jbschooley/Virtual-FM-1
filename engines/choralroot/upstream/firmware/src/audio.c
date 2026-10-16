/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* I2S output (ALNK0 -> external codec) and the
 * audio ISR: per half buffer, blocks of CTL samples: mix_block (fx.c: events ->
 * each synth part -> dist -> level / pan -> sends -> buses -> master) -> 24-bit stereo. */
/* registers: hal/fm1_audio.h */
#define HALF_WORDS (HALF_FRAMES * 2u)
#define DAC_TICKS 544u            /* TIMER4 ticks per I2S frame (24 MHz / 44,117.6 Hz) */
#define CPU_AVG (4096u / HALF_FRAMES)   /* halves the load meter averages: ~93 ms whatever the half */
#define OUT_SHIFT 7               /* Q15 -> 24-bit, -6 dBFS ceiling (M0f ran clean at -18 dBFS) */

static int32_t abuf[2u * HALF_WORDS] __attribute__((aligned(4)));

/* diagnostics, kept across resets and UBOOT entry: read with `fm1t memr` */
#define DBG_MAGIC 0x44424731u                       /* "DBG1" */
struct felucca_dbg {
    uint32_t magic, halves, max_us, nested, in_audio, late, timer_irqs, ui_frames;
    uint32_t last_us, cpu_q8, boots;
    uint32_t stage, page, home;           /* where the main loop is (breadcrumbs) */
    uint32_t prev_stage, prev_page, prev_home, prev_rst, prev_frames;   /* as found at boot */
} felucca_dbg __attribute__((section(".noinit")));
#define CR_STAGE(n) (felucca_dbg.stage = (n))     /* the boot breadcrumb (cr_bootguard.h BS_*) */
static volatile uint32_t audio_halves, audio_max_us;
static volatile uint32_t t5_nested_ticks;              /* TIMER4 ticks TIMER5 spent nested in this ISR (main.c) */
static uint32_t audio_cpu_rem;                         /* keep the fractional IIR step: no low-load bias */
/* the last second's audio ISR (ChoralRoot's console `cpu`, the GEEK OUT view): the ISR adds each half's render time
 * (us, TIMER5 nested in it left out) and its whole time (ticks, nested included: what the DMA deadline sees);
 * cpu_window() in the main loop closes a window every second (cpu_last) */
static volatile uint32_t aw_max_us, aw_sum_us, aw_n, aw_max_all;
static volatile uint8_t aw_reset;                /* the UI took the window: the ISR starts a new one (lock-free) */
static struct {
    uint32_t avg_us, max_us, max_all_us, halves, late, ui_max_ms, ui_frames;   /* the last full second */
    uint32_t late0, t0, ui_last, ui_max, ui_n;                                 /* the window being filled */
} cpu_last;
#define CPU_HALF_US (HALF_FRAMES * 1000000u / FS)   /* a half's budget, us: 2902 (console.c audio_budget_us) */
/* 3y60 (the owner, 2026-10-14: "with all the user presets filled, my FM-1's UI is now very laggy"): a UI frame's own
 * time (input, LEDs, draw; main.c, emu_fw.c), the longest and the sum since 78 PEAK's last read (cr_backup.c crb_peak) */
static struct { uint32_t max_us, sum_us, n, t0; } ui_ft;
static void ui_ft_add(uint32_t t0_ticks)
{
    uint32_t us = (fm1_ticks() - t0_ticks) / FM1_TICKS_PER_US;
    if (us > ui_ft.max_us)
        ui_ft.max_us = us;
    ui_ft.sum_us += us;
    ui_ft.n++;
}
static void cpu_window(uint32_t now_ms)          /* once per UI frame: a frame's period, and the second's close */
{
    uint32_t d = now_ms - cpu_last.ui_last;
    if (cpu_last.ui_last && d > cpu_last.ui_max)
        cpu_last.ui_max = d;
    cpu_last.ui_last = now_ms;
    cpu_last.ui_n++;
    if (now_ms - cpu_last.t0 < 1000u)
        return;
    if (aw_reset)
        return;                                  /* (the ISR has not taken the last one yet: no half since) */
    cr_prof_window();                            /* the ISR's stages (cr_cpu1.h cr_dbg) */
    cpu_last.avg_us = aw_n ? aw_sum_us / aw_n : 0u;
    cpu_last.max_us = aw_max_us;
    cpu_last.max_all_us = aw_max_all / FM1_TICKS_PER_US;
    cpu_last.halves = aw_n;
    aw_reset = 1;
    cpu_last.late = felucca_dbg.late - cpu_last.late0;
    cpu_last.late0 = felucca_dbg.late;
    cpu_last.ui_max_ms = cpu_last.ui_max;
    cpu_last.ui_frames = cpu_last.ui_n;
    cpu_last.ui_max = cpu_last.ui_n = 0;
    cpu_last.t0 = now_ms;
}
#define SCOPE_N 512u
static int16_t scope_buf[SCOPE_N];
static uint32_t scope_w;
/* the output is above -60 dBFS somewhere in the scope's last 23 ms: a flash erase now (storage_hw.c st_erase: 45 ms
 * of silence) is a HOLE in something audible (st_holes: ChoralRoot's 73 DEBUG; its rule is that there are none) */
static uint32_t st_holes;
static int audio_loud(void)
{
    uint32_t i;
    for (i = 0; i < SCOPE_N; i++)
        if (scope_buf[i] > 32 || scope_buf[i] < -32)
            return 1;
    return 0;
}

#if FELUCCA_UAC
static void ua_service_paced(uint32_t t0, int from_isr);              /* main.c */
static inline void ua_isr_service(void)                 /* the recording's endpoint from the render, between blocks */
{
    if (!ua.cap_alt)
        return;                                         /* (not recording: no IRQ-off section) */
    fm1_irq_off();
    if (usb_sie_owner) {
        ua_dbg.isr_busy++;                              /* (we preempted an SIE sequence: not here) */
    } else {
        usb_sie_owner = 1;
        ua_service_paced(fm1_ticks(), 1);
        usb_sie_owner = 0;
    }
    fm1_irq_on();
}
#endif

static void audio_block(int32_t *out, uint32_t n)       /* mix (fx.c), then Q15 -> 24 bit */
{
    uint32_t i;
    mix_block(out, n);
    if (fx_usb_fixed)                                   /* Options > USB Level = Fixed: USB took the full level, the */
        usb_fixed_dac(out, n);                          /* DAC (speaker, headphones) gets MASTER's (fx.c) */
#if FELUCCA_UAC
    /* the USB recording's ring (usb_audio_stream.c): the capture frames in (fx.c ua_stage). TIMER5 serves the
     * endpoint nested in this render (main.c): only this short copy goes without IRQs, stream resets and alternate
     * changes included, not the synth and FX work (Melodee's). 0.19: when the host runs ChoralRoot In at 48 kHz the
     * block is resampled first, with the IRQs on (ua_block48: 34 or 35 frames for the 32); at 44.1 kHz that is one
     * test of the rate and the frames go in as they are */
    if (ua_stage_on) {                                  /* (the computer records: else no IRQ-off section at all) */
        const int16_t *st = ua_stage;
        uint32_t fn = n, r48 = ua.r48;
        if (r48) {
            fn = ua_block48(ua_stage, n);
            st = ua.rs_out;
        }
        fm1_irq_off();
        if (usb.up && usb.config && !usb.suspended && !USB_CDC_ON)
            ua_feed(st, fn, r48);
        fm1_irq_on();
    }
#endif
    for (i = 0; i < n; i++) {
        if (i & 1u)
            scope_buf[scope_w++ & (SCOPE_N - 1u)] = (int16_t)out[2u * i];
        out[2u * i] *= 1 << OUT_SHIFT;
        out[2u * i + 1u] *= 1 << OUT_SHIFT;
    }
    if (fx_out_blk)                                     /* Output Clean (fx.c): the 7 bits under the Q15 sample */
        for (i = 0; i < 2u * n; i++)
            out[i] += out_frac[i & (2u * CTL - 1u)];
}

/* Overload: two halves in a row above 85 % (the render plus TIMER5 nested in it), fade one voice over the
 * next block, and one more each half while it lasts. Keep each part's
 * lowest POLY note and MONO / LEGATO / UNISON lead, as the shared budget does.
 * P5 ON TWO CORES (3y46). The half the shed times is core 0's, and core 0 waits in it for core 1's job: it is the
 * tick + the LONGER of the two cores' renders + the effects + the master, so a half is over when either core is. And
 * the chord part's P5 voices are shared out between the cores again every block (fx.c p5x_assign), so a voice given
 * up shortens the longer side whichever part it was. The order with P5 on the bass part: (1) a released voice, the
 * quietest, as before; (2) a voice of the BASS part's P5 stack, from the top (never its last: the lead stays);
 * (3) as before, the oldest held voice that is not its part's lowest note or lead: a chord voice. Without P5 on the
 * bass part: (1) and (3), today's rule */
static volatile uint8_t shed_req;
static uint8_t shed_over;                              /* bit k: the half k halves ago was over 85 % */
#define HALF_TICKS (HALF_FRAMES * 1000000u / FS * FM1_TICKS_PER_US)   /* a half (2902 us) */
/* FM1_AUDIO_ACK_LATE 1: the half's pending bit cleared after its render, as every build to 0.18's second did (a
 * build switch, should a unit want the old order: CR_ACK_LATE=1 ./build.sh). 0: before it, see fm1_alnk0_irq */
#ifndef FM1_AUDIO_ACK_LATE
#define FM1_AUDIO_ACK_LATE 0
#endif
#define SHED_TICKS ((HALF_FRAMES * 1000000u / FS) * 85u / 100u * FM1_TICKS_PER_US)   /* 85 % of a half */
static uint32_t shed_count;
/* WHAT THE SHED DID (3y44; 78 PEAK's values 35..39, tools/fm1_install.py --debug: until then voices_shed was on the
 * console alone): since the last PEAK read the halves above 85 % (shed_over_n: one alone sheds nothing, two in a row
 * do) and the time of the half that asked for the latest shed (shed_us, with its lag); and of the latest voice shed
 * which one (shed_last: 0 none, else 0x10000 | part << 12 | voice << 8 | note) and why (shed_why: 1 a released voice,
 * the quietest; 2 a held one, the oldest that is not its part's lowest note; 3 asked, and no voice to give) */
/* (they are cr_dbg's, cr_cpu1.h: a new small static here moves the target compiler's blocks of statics and with them
 * fx.c's mix loop, HISTORY 3y42) */

/* end of a half: all = its ticks, TIMER5 nested in it included -> the render alone in us. The deadline sees
 * both, and how far behind its boundary the half started (lag, ChoralRoot: cr_cpu1.h cr_prof_entry; a half that
 * starts 600 us late and renders 2400 is as late as one of 3000); shed after 2 overloaded halves in a row (out of
 * line: keeps the ISR's render loops as they were) */
static __attribute__((noinline)) uint32_t shed_check(uint32_t all, uint32_t lag)
{
    shed_over = (uint8_t)(shed_over << 1 | (all + lag > SHED_TICKS));
    cr_dbg.shed_over_n += shed_over & 1u;
    if ((shed_over & 3u) == 3u) {
        shed_req = 1;
        cr_dbg.shed_us = (all + lag) / FM1_TICKS_PER_US;
    }
    return (all - t5_nested_ticks) / FM1_TICKS_PER_US;
}

static void shed_note(const voice_t *v, uint32_t why)    /* the shed's record (above) */
{
    uint32_t p;
    cr_dbg.shed_why = why;
    for (p = 0; v && p < NPART; p++)
        if (v >= trk[p].v && v < trk[p].v + NVOICE)
            cr_dbg.shed_last = (1u << 16) | p << 12 | (uint32_t)(v - trk[p].v) << 8 | v->note;
}

#if FELUCCA_PROPHET
/* a P5 bass: a voice of its stack before any chord voice (above; out of line: the ISR's code stays as it was laid) */
static __attribute__((noinline)) int shed_p5_bass(void)
{
    uint32_t k;
    if (!P5_IS(&trk[P5_PART_BASS]))
        return 0;
    k = p5_part_victim(&trk[P5_PART_BASS], 0);
    if (k == NVOICE)
        return 0;
    shed_note(&trk[P5_PART_BASS].v[k], 4u);
    voice_kill(&trk[P5_PART_BASS].v[k]);
    shed_count++;
    return 1;
}
#endif
/* (out of line since 3y46: with the P5 step in it the target compiler laid this cold path behind the ISR's block
 * loop with a branch back over it, which tests/target_budget.py reads as two more loops around the render) */
static __attribute__((noinline)) void shed_voice(void)
{
    uint32_t p, i;
    voice_t *best = 0;
    for (p = 0; p < NPART; p++)
        for (i = 0; i < NVOICE; i++) {
            voice_t *v = &trk[p].v[i];
            if (v->active && !v->gate && v->stage != 4u && (!best || v->env < best->env))
                best = v;
        }
    if (best) {
        shed_note(best, 1);
        voice_kill(best);
        shed_count++;
        return;
    }
#if FELUCCA_PROPHET
    if (shed_p5_bass())
        return;
#endif
    for (p = 0; p < NPART; p++) {
        const track_t *t = &trk[p];
        uint32_t mode = trk_vmode(t), low = mode == V_POLY ? lowest_held(t) : 0u;
        for (i = 0; i < NVOICE; i++) {
            voice_t *v = &trk[p].v[i];
            if (v->active && v->gate && v->stage != 4u &&
                ((mode == V_POLY && i != low) || (mode != V_POLY && i > 0u))) {
                if (!best || v->age < best->age)
                    best = v;
            }
        }
    }
    shed_note(best, best ? 2u : 3u);
    if (best) {
        voice_kill(best);
        shed_count++;
    }
}

void fm1_alnk0_irq(void)                       /* via isr_alnk0 (hal/fm1_isr.S) */
{
    uint8_t p;
    uint32_t t0;
    felucca_dbg.in_audio = 1;                   /* first: TIMER5 nests from here on (main.c) */
    p = fm1_audio_pending();
    t0 = fm1_ticks();
    t5_nested_ticks = 0;
    fm1_audio_ack_aux(p);
    if (p & FM1_AUDIO_HALF) {
        uint32_t half, b, us, lag, wall;
        int32_t *o;
        /* THE PENDING BIT IS CLEARED BEFORE THE RENDER (0.18's third build). Cleared after it, as before, a half
         * that ended after its boundary took the next boundary's interrupt with it: one bit, set again during the
         * render, then cleared. Nobody rendered the next half, and the DAC played the one from two halves before
         * again: 2.9 ms of old sound spliced in, a pop in anything sustained (a reverb tail, a delay's repeats),
         * once for every half counted late. Cleared here, the boundary that passes during a late render keeps its
         * interrupt: the ISR runs again as it returns, the late half's last block was written long before the DAC
         * got to it (the DAC needs block k 726 us x k after the boundary), and nothing is heard until the ISR is
         * 2.1 ms behind (cr_dbg.stale counts that) */
#if !FM1_AUDIO_ACK_LATE
        fm1_audio_ack_half();
#endif
        half = fm1_audio_free_half();
        o = &abuf[half * HALF_WORDS];
        lag = cr_prof_entry(t0, HALF_TICKS);
        if (shed_req) {
            shed_req = 0;
            shed_voice();
        }
        for (b = 0; b < HALF_FRAMES; b += CTL) {
#if FELUCCA_UAC
            ua_isr_service();                   /* before each block (at most every 250 us): main.c */
#endif
#ifdef FM1_INPUT_LAT
            kb_out_tick = t0 + (HALF_FRAMES + b) * DAC_TICKS;   /* when this block plays (seq.c kb_lat) */
#endif
            audio_block(o + 2u * b, CTL);
        }
#if FM1_AUDIO_ACK_LATE
        fm1_audio_ack_half();
#endif
        cr_prof_half();                                 /* the half's stages (cr_cpu1.h): core 1's wait is in the
                                                         * half's time, so the meter and the shed see it */
        audio_halves++;
        {
            uint32_t all = fm1_ticks() - t0;
#ifdef CR_SHED_MODEL                                     /* the emulator alone (emu_firmware.h, EMU_SHED_OVER / EMU_SHED_AT:
                                                         * a unit that does not keep up, modelled; docs/PROPHET.md) */
            all = CR_SHED_MODEL(all);
#endif
            us = shed_check(all, lag);
            wall = all;
            if (aw_reset) {                             /* cpu_window took the last second */
                aw_max_us = aw_sum_us = aw_n = aw_max_all = 0;
                aw_reset = 0;
            }
            if (all > aw_max_all)
                aw_max_all = all;
        }
        if (us > audio_max_us)
            audio_max_us = us;
        if (us > aw_max_us)
            aw_max_us = us;
        aw_sum_us += us;
        aw_n++;
        {
            uint32_t load = song.cpu_q8 * (CPU_AVG - 1u) + (us * 256u) / (HALF_FRAMES * 1000000u / FS) + audio_cpu_rem;
            song.cpu_q8 = load / CPU_AVG;
            audio_cpu_rem = load % CPU_AVG;
        }
        if (fm1_audio_free_half() != half) {
            felucca_dbg.late++;                         /* the DMA moved on while we rendered */
            cr_prof_whole(us, 1, wall, t5_nested_ticks, HALF_TICKS);   /* (ChoralRoot's worst cases, cr_cpu1.h) */
        } else {
            cr_prof_whole(us, 0, wall, t5_nested_ticks, HALF_TICKS);
        }
        felucca_dbg.halves++;
        felucca_dbg.last_us = us;
        if (us > felucca_dbg.max_us)
            felucca_dbg.max_us = us;
        felucca_dbg.cpu_q8 = song.cpu_q8;
    }
    felucca_dbg.in_audio = 0;
}
extern void isr_alnk0(void);

static void audio_init(void)                   /* codec and ALNK0 bring-up (fm1_audio.h) */
{
    uint32_t i;
    for (i = 0; i < 2u * HALF_WORDS; i++)
        abuf[i] = 0;
    fm1_audio_init(abuf, HALF_WORDS, isr_alnk0, 3);
}
