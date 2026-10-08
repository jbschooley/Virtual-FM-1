/* SPDX-License-Identifier: GPL-3.0-only */
/* Eight independent banks per track. The current bank stays in track.step for
 * the established editor/recording paths; switching commits it and loads one
 * bounded 64-step copy. No flash work runs in the audio interrupt. */
typedef struct { step_t step[NSTEP]; int16_t timing[4]; } pattern_t;
static pattern_t pattern_retained[2][NPAT] __attribute__((section(".noinit")));
static pattern_t pattern_ram[2][NPAT];
static uint8_t chain_patterns[CHAIN_ROWS][NTRK];
static uint8_t motion_pattern[MOTION_MAX];
static void seq_release(track_t *t);
static void recording_reset(void);
static void recording_stop(track_t *t);
static int recording_owns(const track_t *t, uint32_t note);
static void motion_restore(track_t *t);
static pattern_t *pattern_at(uint32_t track, uint32_t bank)
{
    return track < 2u ? &pattern_retained[track][bank] : &pattern_ram[track - 2u][bank];
}
static void pattern_commit(track_t *t)
{
    pattern_t *p = pattern_at(t - trk, t->pattern);
    memcpy(p->step, t->step, sizeof p->step);
    memcpy(p->timing, &t->p[P_SLEN], sizeof p->timing);
}
static void pattern_apply(track_t *t, uint32_t bank)
{
    const pattern_t *p = pattern_at(t - trk, bank);
    seq_release(t);
    recording_stop(t);
    motion_restore(t);
    memcpy(t->step, p->step, sizeof t->step);
    memcpy(&t->p[P_SLEN], p->timing, sizeof p->timing);
    t->pattern = (uint8_t)bank;
    t->pattern_next = 0xff;
    t->rh_n = t->rskip_n = 0;
    t->pattern_gen++;
}
static void pattern_init(void)
{
    uint32_t k, b, i;
    recording_reset();
    memset(pattern_retained, 0, sizeof pattern_retained);
    memset(pattern_ram, 0, sizeof pattern_ram);
    memset(chain_patterns, 0, sizeof chain_patterns);
    memset(motion_pattern, 0, sizeof motion_pattern);
    for (k = 0; k < NTRK; k++) {
        trk[k].pattern = 0; trk[k].pattern_next = 0xff; trk[k].pattern_gen++;
        for (b = 0; b < NPAT; b++) {
            pattern_t *p = pattern_at(k, b);
            for (i = 0; i < NSTEP; i++) p->step[i].time = ST_REST;
            for (i = 0; i < 4u; i++) p->timing[i] = TP[P_SLEN + i].def;
        }
    }
}
static void pattern_switch(track_t *t, uint32_t bank)
{
    pattern_commit(t);
    pattern_apply(t, bank);
    t->seq_idx = (uint16_t)(t->p[P_SLEN] - 1);
}
