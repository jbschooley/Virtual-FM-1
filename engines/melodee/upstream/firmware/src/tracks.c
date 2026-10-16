/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The tracks in use (song.ntrk, core.h trk_n): one to start with, up to NTRK, always from track 1. Stage's sheet adds
 * one (track_add: the next place, its power-on sound, every pattern empty; selected, the browser open on it) or
 * removes the selected one (track_remove: stopped, after its question): the tracks after it move down a place with
 * all that is theirs (the sound and its patches, the eight patterns, their column of the song, motion, takes) and the
 * last place is left empty. A removal goes as a project load does: the project packed as SAVE packs it (proj_wire_u),
 * its tracks moved there, loaded back. Included by project.c */
#define TRK_BANK_BYTES ((NPAT - 1u) * NSTEP * BANK_STEP_SIZE)  /* a track's banks but the active one, packed */

static void track_say(uint32_t k, const char *what)     /* "TRACK 2 REMOVED" */
{
    char b[16] = "1 ";
    b[0] = (char)('1' + k % NTRK);
    str_cpy(b + 2, what, sizeof b - 2u);
    ui_say("TRACK ", b);
}

/* track j's bits down to j - 1 from the track removed (k) on; k's go */
static uint8_t trk_bits_remove(uint32_t bits, uint32_t k)
{
    return (uint8_t)((bits & ((1u << k) - 1u)) | ((bits >> (k + 1u)) << k));
}

/* remove track k (in use, not the only one): 0 done, 1 refused, 2 failed (nothing changed) */
static int track_remove(uint32_t k)
{
    project_t *p = &proj_scratch;
    uint8_t *raw = proj_wire_u.raw;
    uint32_t n = trk_n(), last = NTRK - 1u, cur = proj_cur, i, j, r, m = 0;
    if (k >= n || n < 2u)
        return 1;
    if (transport_busy()) {
        ui_message("STOP FIRST");
        return 1;
    }
    browse_commit();                                    /* (a sound still pending: into the track it was meant for) */
    momentary_restore();
    project_capture(p);
    proj_wire_gen++;
    if (!bank_pack(raw, p, 1))
        goto fail;
    for (j = k; j < last; j++) {                        /* the record: the tracks after k down a place */
        p->t[j] = p->t[j + 1u];
        memcpy(p->fm6[j], p->fm6[j + 1u], FM6_PACKED);
        memcpy(p->fm6_fn[j], p->fm6_fn[j + 1u], FM6_NFN);
        p->cz[j] = p->cz[j + 1u];
        p->p5[j] = p->p5[j + 1u];
        p->drum[j] = p->drum[j + 1u];
        p->pattern[j] = p->pattern[j + 1u];
        *proj_p5_origin(p, j) = proj_p5_origin_value(p, j + 1u);
    }
    {   /* the last place empty: its power-on sound (project_restore_runtime: PROJ_DEF_SOUND), no steps */
        proj_trk_t *t = &p->t[last];
        uint32_t e = TRK_DEF[last][0];
        t->engine = (uint8_t)e;
        t->preset = PROJ_DEF_SOUND;
        for (i = 0; i < P_COUNT; i++)
            t->p[i] = param_reserved(i) ? 0 : param_desc_of(e, i)->def;
        for (i = 0; i < NSTEP; i++)
            step_clear(&t->step[i]);
        p->pattern[last] = 0;
        *proj_p5_origin(p, last) = 0;
    }
    for (i = 0; i < RECORD_MAX; i++) {                  /* takes: k's go, the later tracks' move down */
        recorded_note_t *rn = &p->recording[i];
        uint32_t o = (rn->owner & 31u) / NPAT;
        if (!rn->vel)
            continue;
        if (o == k)
            memset(rn, 0, sizeof *rn);
        else if (o > k)
            rn->owner = (uint8_t)(rn->owner - NPAT);
    }
    for (i = 0; i < p->motion.count; i++) {             /* motion: k's go, the later tracks' move down (their tags too) */
        motion_event_t e = p->motion.event[i];
        if ((uint32_t)(e.place >> 6) == k)
            continue;
        if ((uint32_t)(e.place >> 6) > k)
            e.place = (uint8_t)(e.place - 64u);
        p->motion.event[m] = e;
        raw[BANK_MOTION_OFF + m] = raw[BANK_MOTION_OFF + i];
        m++;
    }
    for (i = m; i < MOTION_MAX; i++) {
        memset(&p->motion.event[i], 0, sizeof p->motion.event[i]);
        raw[BANK_MOTION_OFF + i] = 0;
    }
    p->motion.count = (uint8_t)m;
    p->motion.on = trk_bits_remove(p->motion.on, k);
    for (j = k; j < last; j++) {                        /* the banks, their timing, the song's columns */
        raw[BANK_ACTIVE_OFF + j] = raw[BANK_ACTIVE_OFF + j + 1u];
        memcpy(raw + BANK_EXTRA_OFF + j * TRK_BANK_BYTES, raw + BANK_EXTRA_OFF + (j + 1u) * TRK_BANK_BYTES, TRK_BANK_BYTES);
        memcpy(raw + BANK_TIMING_OFF + j * NPAT * BANK_TIME_SIZE, raw + BANK_TIMING_OFF + (j + 1u) * NPAT * BANK_TIME_SIZE,
               NPAT * BANK_TIME_SIZE);
        for (r = 0; r < CHAIN_ROWS; r++)
            raw[BANK_CHAIN_OFF + r * NTRK + j] = raw[BANK_CHAIN_OFF + r * NTRK + j + 1u];
    }
    raw[BANK_ACTIVE_OFF + last] = 0;
    for (i = 0; i < (NPAT - 1u) * NSTEP; i++) {
        step_t e = {{0}, 0, ST_REST};
        bank_step_pack(raw + BANK_EXTRA_OFF + last * TRK_BANK_BYTES + i * BANK_STEP_SIZE, &e);
    }
    for (i = 0; i < NPAT * BANK_TIME_SIZE; i++)
        raw[BANK_TIMING_OFF + last * NPAT * BANK_TIME_SIZE + i] = (uint8_t)TP[P_SLEN + i % BANK_TIME_SIZE].def;
    for (r = 0; r < CHAIN_ROWS; r++) {
        raw[BANK_CHAIN_OFF + r * NTRK + last] = 0;
        if (r < p->chain.count)                         /* (a row's slot: track 1's pattern there) */
            p->chain.row[r].slot = raw[BANK_CHAIN_OFF + r * NTRK];
    }
    if (p->fm6_fn_ok)
        memcpy(raw + BANK_FN_OFF + 4u, p->fm6_fn, sizeof p->fm6_fn);
    p->parts = (uint8_t)(n - 1u);
    p->sel = (uint8_t)(k + 1u < n ? k : k - 1u);        /* the track now in its place, else the one before */
    if (!proj_pack((project_store_t *)(raw + 8u), p))
        goto fail;
    bank_checksum(raw);
    if (!bank_valid(raw, BANK_STORE_SIZE) || project_restore_runtime(&proj_scratch))
        goto fail;
    bank_restore(raw);
    proj_cur = (uint8_t)cur;                            /* (still the project of its slot) */
    for (r = 0; r < CHAIN_ROWS; r++)                    /* the jam so far, the armed and solo tracks too */
        for (j = k; j < last; j++)
            jam.pat[r][j] = jam.pat[r][j + 1u];
    song.rec = trk_bits_remove(song.rec, k);
    song.rec_latch = trk_bits_remove(song.rec_latch, k);
    track_solo = trk_bits_remove(track_solo, k);
    track_say(k, "REMOVED");
    return 0;
fail:
    proj_wire_gen++;
    ui_message("TRACK NOT REMOVED");
    return 2;
}

/* place k as a new track finds it: its power-on sound, the mix's and the track's defaults, every pattern empty, no motion
 * or takes, its column of the song 0. Playing too: a place not in use is silent and its steps kept time */
static void track_blank(uint32_t k)
{
    track_t *t = &trk[k % NTRK];
    uint32_t b, i, r;
    undo_depth++;                                       /* (its sound loads take no undo levels) */
    track_defaults(t);                                  /* the mix, ARP, SCL, LEN ..; the steps, motion, takes */
    set_engine_of(t, TRK_DEF[k % NTRK][0]);
    apply_preset_to(t, TRK_DEF[k % NTRK][1]);
    undo_depth--;
    t->p[P_ROOT] = TSEL->p[P_ROOT];                     /* the song's key */
    scale_share(TSEL);
    fm1_irq_off();
    for (b = 0; b < NPAT; b++) {                        /* the other patterns empty too */
        pattern_t *p = pattern_at(k % NTRK, b);
        for (i = 0; i < NSTEP; i++)
            step_clear(&p->step[i]);
        for (i = 0; i < 4u; i++)
            p->timing[i] = TP[P_SLEN + i].def;
    }
    t->pattern = 0;
    t->pattern_next = 0xff;
    t->pattern_gen++;
    for (r = 0; r < CHAIN_ROWS; r++)
        chain_patterns[r][k % NTRK] = 0;
    fm1_irq_on();
    pat_sig[k % NTRK] = steps_sig(t);                   /* (empty: not the user's) */
    pat_last[k % NTRK] = 0;
}

/* the next track in use (Stage's sheet): a new one, selected, the browser open on it (OCT- keeps its power-on sound) */
static void track_add(void)
{
    uint32_t k = trk_n(), i;
    if (k >= NTRK) {
        ui_message("TRACKS FULL");
        return;
    }
    browse_commit();
    momentary_restore();
    track_blank(k);
    song.ntrk = (uint8_t)(k + 1u);
    track_select(k);
    for (i = 0; i < NPAGES && PAGES[i].graph != GR_BROWSE; i++)
        ;
    ui.home = 0;                                        /* the browser on it: its sound picked from there */
    ui.page = (uint8_t)i;
    page_entered();
    track_say(k, "ADDED");
}
