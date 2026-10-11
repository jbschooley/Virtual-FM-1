/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SONG arranges the current project's banks, one per track per row.
 * Main-loop preparation commits the original selections. The ISR switches
 * bounded in-RAM patterns and timing; STOP restores the original selections. */
#include "pattern.c"
static chain_config_t chain_config;
static struct {
    uint8_t original[NTRK];
    chain_config_t config;
    int16_t timing[NTRK][4];
    volatile uint8_t armed, running, row, remaining;
    uint8_t slot, rec;
    uint32_t carry;
} chain;

static void seq_release(track_t *t);
static void seq_stop(void);
static uint32_t div_samples(uint32_t div);
static uint32_t step_samples(const track_t *t, uint32_t period, uint32_t idx);

static void chain_defaults(chain_config_t *c)
{
    uint32_t i;
    memset(c, 0, sizeof *c);
    for (i = 0; i < CHAIN_ROWS; i++)
        c->row[i].repeat = 1;
}
static int chain_valid(const chain_config_t *c)
{
    uint32_t i;
    if (c->count > CHAIN_ROWS)
        return 0;
    for (i = 0; i < c->count; i++)
        if (c->row[i].slot >= NPAT || !c->row[i].repeat || c->row[i].repeat > 16u)
            return 0;
    return 1;
}
static const step_t *seq_steps(const track_t *t)
{
    return t->step;
}
static void motion_restore(track_t *t);
static void chain_apply(void)
{
    uint32_t i;
    chain.slot = chain.config.row[chain.row].slot;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        seq_release(t);
        motion_restore(t);
        pattern_apply(t, chain_patterns[chain.row][i]);
        t->seq_idx = (uint16_t)(t->p[P_SLEN] - 1);
        t->seq_pos = 0x7FFFFFFFu;
        t->rh_n = t->rskip_n = 0;
    }
}
static void chain_start(void)
{
    uint32_t i;
    if (!chain.armed)
        return;
    chain.rec = song.rec;
    song.rec = 0;
    for (i = 0; i < NTRK; i++) {
        chain.original[i] = trk[i].pattern;
        memcpy(chain.timing[i], &trk[i].p[P_SLEN], sizeof chain.timing[i]);
    }
    chain.row = 0;
    chain.remaining = chain.config.row[0].repeat;
    chain.carry = 0;
    chain.running = 1;
    chain.armed = 0;
    chain_apply();
}
static void chain_stop(void)
{
    uint32_t i;
    chain.armed = 0;
    if (!chain.running)
        return;
    for (i = 0; i < NTRK; i++) {
        pattern_apply(&trk[i], chain.original[i]);
        memcpy(&trk[i].p[P_SLEN], chain.timing[i], sizeof chain.timing[i]);
    }
    song.rec = chain.rec;
    chain.running = 0;
}
static void chain_tick(uint32_t n)
{
    const track_t *t = &trk[0];
    uint32_t length;
    if (!chain.running || t->seq_pos >= 0x7FFFFFFFu || t->seq_idx + 1u != (uint32_t)t->p[P_SLEN])
        return;
    length = step_samples(t, seq_div_samples((uint32_t)t->p[P_SDIV]), t->seq_idx);
    if (t->seq_pos + n < length)
        return;
    if (chain.remaining > 1u) {
        chain.remaining--;
        return;
    }
    if (chain.row + 1u >= chain.config.count) {
        seq_stop();
        return;
    }
    chain.carry = t->seq_pos + n - length;
    chain.row++;
    chain.remaining = chain.config.row[chain.row].repeat;
    chain_apply();
}
