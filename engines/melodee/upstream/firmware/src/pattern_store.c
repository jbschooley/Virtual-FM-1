/* SPDX-License-Identifier: GPL-3.0-only */
/* FBKG: a FUN13 sound/current-pattern record plus seven other banks per track,
 * all bank timings, arrangement assignments and bounded motion bank tags.
 * Five base sectors plus two extension sectors per A/B copy; the 27,200-byte record is validated in main-loop
 * staging before any runtime state is replaced. */
/* FBKK preserves all 32 pattern banks alongside the expanded recording. Its extra
 * steps use exactly 64 bits: four 7-bit notes, n/time + recorded marker (4 bits), flags (2), velocity (7),
 * hits/accents (8 each), probability (7). Only FBKK (written since Melodee 1.0.0) is read; older banks were
 * rewritten by 1.2's boot pass. */
#define BANK_MAGIC 0x4B4B4246u                  /* FBKK: sound controls and full-precision per-hit pitch/length */
#define BANK_STORE_SIZE PROJ_BANK_BYTES
#define BANK_PROJ_SIZE PROJ_STORE_SIZE
#define BANK_STEP_SIZE 8u
#define BANK_ACTIVE_OFF (8u + BANK_PROJ_SIZE)
#define BANK_EXTRA_OFF (BANK_ACTIVE_OFF + NTRK)
#define BANK_TIMING_OFF (BANK_EXTRA_OFF + NTRK * (NPAT - 1u) * NSTEP * BANK_STEP_SIZE)
#define BANK_TIME_SIZE 4u
#define BANK_CHAIN_OFF (BANK_TIMING_OFF + NTRK * NPAT * BANK_TIME_SIZE)
#define BANK_MOTION_OFF (BANK_CHAIN_OFF + CHAIN_ROWS * NTRK)
#define BANK_FN_OFF (BANK_MOTION_OFF + MOTION_MAX)
#define BANK_FN_MAGIC 0x31364E46u
_Static_assert(BANK_FN_OFF + 4u + NTRK * FM6_NFN <= BANK_STORE_SIZE - 4u, "bank project extent");
static int bank_full(uint32_t n) { return n == BANK_STORE_SIZE; }
static void bank_checksum(uint8_t *raw)
{
    uint32_t sum = proj_hash(raw, BANK_STORE_SIZE - 4u);
    memcpy(raw + BANK_STORE_SIZE - 4u, &sum, 4);
}
/* Pack bits with only 32-bit shifts: pi32v2 has no 64-bit divide helpers. */
static void bank_bits_put(uint8_t *b, uint32_t *pos, uint32_t v, uint32_t n)
{
    while (n--) { b[*pos >> 3] |= (uint8_t)((v & 1u) << (*pos & 7u)); v >>= 1; ++*pos; }
}
static uint32_t bank_bits_get(const uint8_t *b, uint32_t *pos, uint32_t n)
{
    uint32_t v = 0, i;
    for (i = 0; i < n; i++, ++*pos) v |= (uint32_t)((b[*pos >> 3] >> (*pos & 7u)) & 1u) << i;
    return v;
}
static int bank_step_unpack(step_t *s, const uint8_t *b)
{
    uint32_t pos = 0, i, meta;
    for (i = 0; i < 4u; i++) s->note[i] = (uint8_t)bank_bits_get(b, &pos, 7u);
    meta = bank_bits_get(b, &pos, 4u);
    s->flags = (uint8_t)bank_bits_get(b, &pos, 2u); s->vel = (uint8_t)bank_bits_get(b, &pos, 7u);
    s->hit = (uint8_t)bank_bits_get(b, &pos, 8u); s->acc = (uint8_t)bank_bits_get(b, &pos, 8u);
    s->probability = (uint8_t)bank_bits_get(b, &pos, 7u);
    s->n = (meta & 7u) < 5u ? meta & 7u : 0u;
    s->time = (meta & 7u) < 5u ? ST_NOTE : (meta & 7u) == 5u ? ST_TIE : ST_REST;
    if (meta & 8u) s->flags |= SF_RECORDED;
    return (meta & 7u) < 7u && s->probability <= 101u && step_acc_valid(s);
}
static int bank_step_pack(uint8_t *b, const step_t *s)
{
    if (s->n > 4u || s->time > ST_REST || s->flags > 7u || s->vel > 127u || s->probability > 101u || !step_acc_valid(s)) return 0;
    uint32_t pos = 0;
    memset(b, 0, 8u);
    for (uint32_t j = 0; j < 4u; j++) { if (s->note[j] > 127u) return 0; bank_bits_put(b, &pos, s->note[j], 7u); }
    bank_bits_put(b, &pos, (s->time == ST_NOTE ? s->n : s->time == ST_TIE ? 5u : 6u) | (s->flags & SF_RECORDED ? 8u : 0u), 4u);
    bank_bits_put(b, &pos, s->flags & 3u, 2u);
    bank_bits_put(b, &pos, s->vel, 7u); bank_bits_put(b, &pos, s->hit, 8u);
    bank_bits_put(b, &pos, s->acc, 8u); bank_bits_put(b, &pos, s->probability, 7u);
    return 1;
}
static int bank_pack(uint8_t *raw, const project_t *q, int runtime)
{
    uint32_t magic = BANK_MAGIC, size = BANK_STORE_SIZE, pos, f = motion_guard();
    pos = BANK_EXTRA_OFF;
    memset(raw, 0, BANK_STORE_SIZE); memcpy(raw, &magic, 4); memcpy(raw + 4, &size, 4);
    if (!proj_pack((project_store_t *)(raw + 8u), q)) { motion_unguard(f); return 0; }
    for (uint32_t k = 0; k < NTRK; k++) {
        uint32_t active = runtime ? trk[k].pattern : q->pattern[k];
        raw[BANK_ACTIVE_OFF + k] = (uint8_t)active;
        if (runtime) pattern_commit(&trk[k]);
        for (uint32_t b = 0; b < NPAT; b++) {
            const pattern_t *p = pattern_at(k, b);
            for (uint32_t i = 0; i < 4u; i++) {
                int16_t v = runtime ? p->timing[i] : (b != active ? TP[P_SLEN + i].def : q->t[k].p[P_SLEN + i]);
                raw[BANK_TIMING_OFF+(k*NPAT+b)*4u+i]=(uint8_t)v;
            }
            if (b == active) continue;
            for (uint32_t i = 0; i < NSTEP; i++, pos += BANK_STEP_SIZE) {
                step_t empty = {{0},0,ST_REST};
                if (!bank_step_pack(raw + pos, runtime ? &p->step[i] : &empty)) { motion_unguard(f); return 0; }
            }
        }
    }
    if (runtime) {
        memcpy(raw + BANK_CHAIN_OFF, chain_patterns, sizeof chain_patterns);
        memcpy(raw + BANK_MOTION_OFF, motion_pattern, sizeof motion_pattern);
    }
    if (q->fm6_fn_ok) {
        uint32_t m = BANK_FN_MAGIC;
        memcpy(raw + BANK_FN_OFF, &m, 4);
        memcpy(raw + BANK_FN_OFF + 4u, q->fm6_fn, sizeof q->fm6_fn);
    }
    motion_unguard(f);
    bank_checksum(raw);
    return 1;
}
static uint8_t bank_import_tags[MOTION_MAX];
static int bank_valid(const uint8_t *raw, uint32_t len)
{
    uint32_t magic, size, sum, pos;
    if (!bank_full(len)) return 0;
    memcpy(&magic, raw, 4); memcpy(&size, raw + 4, 4); memcpy(&sum, raw + len - 4u, 4);
    pos = BANK_EXTRA_OFF;
    if (magic != BANK_MAGIC || size != len || sum != proj_hash(raw, len - 4u) ||
        !proj_import(&proj_scratch, raw + 8u, BANK_PROJ_SIZE)) return 0;
    for (uint32_t k = 0; k < NTRK; k++) {
        if (raw[BANK_ACTIVE_OFF + k] >= NPAT) return 0;
        if (raw[BANK_ACTIVE_OFF + k] != proj_scratch.pattern[k]) return 0;
        for (uint32_t b = 0; b < NPAT; b++) {
            for (uint32_t i = 0; i < 4u; i++) {
                int16_t v = raw[BANK_TIMING_OFF + (k * NPAT + b) * 4u + i];
                if (v < TP[P_SLEN + i].min || v > TP[P_SLEN + i].max) return 0;
                if (b == raw[BANK_ACTIVE_OFF + k] && v != proj_scratch.t[k].p[P_SLEN + i]) return 0;
            }
            if (b == raw[BANK_ACTIVE_OFF + k]) continue;
            for (uint32_t i = 0; i < NSTEP; i++, pos += BANK_STEP_SIZE) { step_t s; if (!bank_step_unpack(&s, raw + pos)) return 0; }
        }
    }
    for (uint32_t i = 0; i < CHAIN_ROWS * NTRK; i++) if (raw[BANK_CHAIN_OFF + i] >= NPAT) return 0;
    for (uint32_t i = 0; i < proj_scratch.motion.count; i++) {
        if (raw[BANK_MOTION_OFF + i] >= NPAT) return 0;
        for (uint32_t j = 0; j < i; j++) if (raw[BANK_MOTION_OFF + i] == raw[BANK_MOTION_OFF + j] &&
            proj_scratch.motion.event[i].place == proj_scratch.motion.event[j].place &&
            proj_scratch.motion.event[i].param == proj_scratch.motion.event[j].param) return 0;
    }
    memset(bank_import_tags, 0, sizeof bank_import_tags);
    uint32_t n = 0;
    for (uint32_t i = 0; i < proj_scratch.motion.count; i++) {
        bank_import_tags[n++] = raw[BANK_MOTION_OFF + i];
    }
    {   /* the FM6 function settings (none written: Dexed's) */
        uint32_t m, k;
        memcpy(&m, raw + BANK_FN_OFF, 4);
        if (m == BANK_FN_MAGIC) {
            const uint8_t *f = raw + BANK_FN_OFF + 4u;
            for (k = 0; k < NTRK; k++)
                if (!fm6_fn_ok(f + k * FM6_NFN))
                    return 0;
            memcpy(proj_scratch.fm6_fn, f, sizeof proj_scratch.fm6_fn);
            proj_scratch.fm6_fn_ok = 1;
            proj_scratch.sum = proj_sum(&proj_scratch);
        }
    }
    return 1;
}
/* Only after bank_valid and project_restore_runtime. */
static void bank_restore(const uint8_t *raw)
{
    uint32_t pos = BANK_EXTRA_OFF, f = motion_guard();
    chain_config = proj_scratch.chain;
    memcpy(chain_patterns, raw + BANK_CHAIN_OFF, sizeof chain_patterns);
    memcpy(motion_pattern, bank_import_tags, sizeof motion_pattern);
    for (uint32_t k = 0; k < NTRK; k++) {
        trk[k].pattern = raw[BANK_ACTIVE_OFF + k]; trk[k].pattern_next = 0xff; trk[k].pattern_gen++;
        for (uint32_t b = 0; b < NPAT; b++) {
            pattern_t *p = pattern_at(k, b);
            for (uint32_t i = 0; i < 4u; i++) p->timing[i] = raw[BANK_TIMING_OFF + (k * NPAT + b) * 4u + i];
            if (b == trk[k].pattern) memcpy(p->step, trk[k].step, sizeof p->step);
            else for (uint32_t i = 0; i < NSTEP; i++, pos += BANK_STEP_SIZE) bank_step_unpack(&p->step[i], raw + pos);
        }
    }
    motion_unguard(f);
}

