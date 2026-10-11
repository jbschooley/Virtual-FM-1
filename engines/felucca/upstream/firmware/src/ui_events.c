/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca SEQ > AUTOMATION (1.1.5 AUTO LIST; 1.2, Discussion #153: the AUTOMATION page, CHANCE and RATCH merged into it;
 * 1.2, #162: NUDGE and QUANTIZE too; 1.5, #173: VEL; included by ui.c): the selected track's automation as a list.
 * The rows:
 *   PLAY     the first: the track's automation ON / OFF (KNOB 4; OFF keeps it) and its counts; OCT+ CLEAR asks
 *            "CLEAR Tn AUTOMATION?" (the locks and events go, the steps' CHANCE, RATCH, NUDGE, VEL stay)
 *   QUANTIZE the second: the track's QUANTIZE (P_SQNT) ON / OFF (KNOB 4) and how many steps have a NUDGE. ON (the
 *            default) plays every step on its start; OFF plays the nudges (seq.c seq_tick)
 *   LOCK / AUTO  a record (motion.c): its step, its parameter, its value
 *   STEP     a step's CHANCE (not 100 %), RATCH (not x1) or NUDGE (not 0): the step's own fields (step_chance,
 *            step_ratchet, step_nudge), shown where they are not at their default, inside LEN; a row being edited
 *            stays while it is at its default. A NUDGE is dimmed where it does not play (QUANTIZE ON, a ratchet, no note)
 *   VEL      (1.5, #173) a step's velocity (step_t.vel), the level its notes and its hits play at (seq.c seq_step; a
 *            step that never had one, 0, plays 96; live recording stores the played one: the panel's keys 100). Not
 *            listed until + ADD VEL's OCT+ (SHOW) lists every note step inside LEN, until HIDE or the page is left (a
 *            recorded pattern would fill the list otherwise). An ACC step plays 127 whatever its VEL (DIM), a DRUM
 *            step's accented hits too; the others one value for all of the step's hits (no velocity per lane)
 *   + ADD    the last
 * in the order of the steps (a step's CHANCE, its RATCH, its NUDGE, its VEL, then its records by parameter).
 *   KNOB 1 (or the PRESETS knob) the row
 *   KNOB 2 the step: a record moves to the next step where its parameter has no record (1 .. LEN), a CHANCE / RATCH /
 *          NUDGE to the next step inside LEN where it is at its default (the step it leaves goes back to it); VEL none
 *   KNOB 3 a record's parameter: any one motion can record (motion.c motion_param) that the track's sound has, by name;
 *          it takes the next one with no record on that step, its value the sound's own (a lock that changes nothing
 *          yet); on + ADD, CHANCE, RATCH and NUDGE come first
 *   KNOB 4 the value (PLAY, QUANTIZE: ON / OFF; NUDGE -8..+7 sixteenths of the step, "QUANTIZE IS ON" while it is;
 *          VEL 1..127)
 *   OCT+   on + ADD: a lock on the step and parameter KNOB 2 / 3 set there (the SEQ cursor's step and the first
 *          parameter of the sound page shown last, to begin with), at the sound's own value; CHANCE 50 %, RATCH x2 or
 *          NUDGE +4/16 on that step, VEL: SHOW / HIDE the VEL rows; on a record: its kind LOCK <-> AUTO (an automation
 *          event holds its value until the next one, a lock sounds on its step only); on PLAY: CLEAR (the dialog)
 *   EDIT   deletes the record, puts a CHANCE / RATCH / NUDGE / VEL back to 100 % / x1 / 0 / 96;  OCT- goes HOME (as
 *          on the other action pages)
 * Every edit is one undo (SAVE held: ui.c motion_undo_take, step_undo_take; a knob turned on is one; PLAY and QUANTIZE
 * are settings, as on their pages before). Not while a song plays (STOP TO EDIT). The 128 records are shared by the four
 * tracks: AUTOMATION FULL when they are used */
static int32_t accel(uint32_t role, int32_t s, int32_t range);
static void confirm_open(uint32_t kind, uint32_t trk);

#define EV_VEL 0xFBu                                    /* + ADD's four before the parameters (ui.ev_id): a VEL, */
#define EV_NUDGE 0xFCu                                  /* a NUDGE, */
#define EV_CHANCE 0xFDu                                 /* a CHANCE, */
#define EV_RATCH 0xFEu                                  /* a RATCH (0xFF: none) */
enum { EVK_TOP, EVK_REC, EVK_CHANCE, EVK_RATCH, EVK_ADD, EVK_NUDGE, EVK_QNT, EVK_VEL };   /* (NUDGE, QUANTIZE: 1.2,
                                                                                           * #162; VEL: 1.5, #173) */
#define EVC(k, a) ((uint16_t)((uint32_t)(k) << 8 | (a)))   /* a row: its kind, its record (EVK_REC) or its step */
#define EV_ROWS (3u + MOTION_MAX + 4u * NSTEP)
static int ev_sid(uint32_t id) { return id >= EV_VEL && id <= EV_RATCH; }   /* a step's own field */
static int ev_skd(uint32_t k) { return k == EVK_CHANCE || k == EVK_RATCH || k == EVK_NUDGE || k == EVK_VEL; }

/* id has a row's name on track t: motion records it, and the track's sound has it (DRUM's lane levels on a DRUM
 * track, the engine's own E1..E8 that it labels; not DIGITAL's operators, retired); CHANCE, RATCH, NUDGE, VEL always */
static int ev_id_ok(const track_t *t, uint32_t id)
{
    const param_desc_t *d;
    if (ev_sid(id))
        return 1;
    if (id >= P_COUNT || !motion_param(id) || id == P_ED_FX || (id >= P_FM1_ATK && id <= P_FM4_LEVEL))
        return 0;
    if (id >= P_LN0 && id <= P_LN7)
        return drum_track(t);
    d = track_desc(t, id);
    return d && d->label && d->label[0] && d->label[0] != '-' && d->max > d->min;
}
/* its name in the list and on the card (b holds 12): the page's word before the card's label where the label alone
 * would say two things (ENV ATK, ENV FLT: ENV DEST's, LFO FLT: LFO DEST's) */
static void ev_name(const track_t *t, uint32_t id, char *b)
{
    const char *pre = id >= P_ATK && id <= P_ED_SHP ? "ENV " : id >= P_LRATE && id <= P_LD_AMP ? "LFO " : "";
    if (ev_sid(id)) {
        str_cpy(b, id == EV_CHANCE ? "CHANCE" : id == EV_RATCH ? "RATCH" : id == EV_VEL ? "VEL" : "NUDGE", 12);
        return;
    }
    str_cpy(b, pre, 12);
    str_cpy(b + str_len(b), id == P_LEVEL ? "LEVEL" : id == P_GLIDE ? "GLIDE" : track_desc(t, id)->label, 12 - str_len(b));
}

/* a step's CHANCE (kind EVK_CHANCE), RATCH (EVK_RATCH), NUDGE (EVK_NUDGE) or VEL (EVK_VEL): its value, its default
 * (100 %, x1, 0, 96), at it, set (VEL 96 is stored as 0, as a step that never had one) */
static int32_t ev_sval(const step_t *s, uint32_t k)
{
    return k == EVK_CHANCE ? (int32_t)step_chance(s) : k == EVK_RATCH ? (int32_t)step_ratchet(s) :
           k == EVK_VEL ? (s->vel ? s->vel : 96) : step_nudge(s);
}
static int32_t ev_sdefv(uint32_t k) { return k == EVK_CHANCE ? 100 : k == EVK_RATCH ? 1 : k == EVK_VEL ? 96 : 0; }
static int ev_sdef(const step_t *s, uint32_t k) { return ev_sval(s, k) == ev_sdefv(k); }
static void ev_sput(step_t *s, uint32_t k, int32_t v)
{
    if (k == EVK_CHANCE)
        step_set_chance(s, (uint32_t)v);
    else if (k == EVK_RATCH)
        step_set_ratchet(s, (uint32_t)v);
    else if (k == EVK_VEL)
        s->vel = (uint8_t)(v == 96 ? 0 : v);
    else
        step_set_nudge(s, v);
}
static uint32_t ev_skind(uint32_t id)
{
    return id == EV_CHANCE ? EVK_CHANCE : id == EV_RATCH ? EVK_RATCH : id == EV_VEL ? EVK_VEL : EVK_NUDGE;
}
/* + ADD's value: CHANCE 50 %, RATCH x2, NUDGE +4/16 (a quarter of the step late) */
static int32_t ev_sadd(uint32_t k) { return k == EVK_CHANCE ? 50 : k == EVK_RATCH ? 2 : 4; }
/* a VEL row (shown): a note step; it plays: notes, or a hit not accented, and not an ACC step (127) */
static int ev_vel_row(const step_t *s) { return s->time == ST_NOTE && (s->n || s->hit); }
static int ev_vel_plays(const step_t *s)
{
    return ev_vel_row(s) && (s->n || (s->hit & ~s->acc)) && !(s->flags & SF_ACCENT);
}
/* a NUDGE that plays: the track's QUANTIZE OFF, a note step, not ratcheted (seq.c seq_tick) */
static int ev_nudge_plays(const track_t *t, const step_t *s)
{
    return !t->p[P_SQNT] && s->time == ST_NOTE && (s->n || s->hit) && step_ratchet(s) == 1u;
}
/* "+3" "-8" "0" (b holds 4) */
static void ev_nudge_fmt(char *b, int32_t v)
{
    fmt_int(b + (v > 0), v);
    if (v > 0)
        b[0] = '+';
}
/* the steps inside LEN with a NUDGE */
static uint32_t ev_nudged(const track_t *t)
{
    uint32_t s, n = 0, len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP);
    for (s = 0; s < len; s++)
        n += step_nudge(&t->step[s]) != 0;
    return n;
}

/* id free on track t's step (no record of it there; CHANCE / RATCH: at the default); every one past the steps */
static int ev_id_free(const track_t *t, uint32_t step, uint32_t id)
{
    if (step >= NSTEP)
        return 1;
    if (ev_sid(id))
        return ev_sdef(&t->step[step], ev_skind(id));
    return motion_find(t, step, id) < 0;
}
/* + ADD's order: CHANCE, RATCH, NUDGE, VEL, then the parameters by id; (uint32_t)-1 before the first */
static int32_t ev_id_pos(uint32_t id)
{
    return id == EV_CHANCE ? 0 : id == EV_RATCH ? 1 : id == EV_NUDGE ? 2 : id == EV_VEL ? 3
           : id < P_COUNT ? (int32_t)id + 4 : -1;
}
static uint32_t ev_id_at(int32_t v)
{
    return v == 0 ? EV_CHANCE : v == 1 ? EV_RATCH : v == 2 ? EV_NUDGE : v == 3 ? EV_VEL : (uint32_t)(v - 4);
}
/* the next one after id in direction dir (+1 / -1) that is free on track t's step; id itself when there is none */
static uint32_t ev_id_step(const track_t *t, uint32_t step, uint32_t id, int32_t dir)
{
    int32_t i = ev_id_pos(id);
    for (;;) {
        i += dir;
        if (i < 0 || i >= (int32_t)P_COUNT + 4)
            return id;
        if (ev_id_ok(t, ev_id_at(i)) && ev_id_free(t, step, ev_id_at(i)))
            return ev_id_at(i);
    }
}
static uint32_t ev_len(const track_t *t) { return (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP); }

/* the rows of the selected track (rw[EV_ROWS]): their count, PLAY, QUANTIZE and + ADD included */
static uint32_t ev_list(uint16_t *rw)
{
    const track_t *t = TSEL;
    uint8_t idx[MOTION_MAX];
    uint32_t m = motion_rows(song.sel, idx), len = ev_len(t), n = 0, j = 0, s, k;
    static const uint8_t SK[4] = {EVK_CHANCE, EVK_RATCH, EVK_NUDGE, EVK_VEL};
    rw[n++] = EVC(EVK_TOP, 0);
    rw[n++] = EVC(EVK_QNT, 0);
    for (s = 0; s < NSTEP; s++) {
        for (k = 0; s < len && k < 4u; k++)
            if ((k == 3u ? ui.ev_vel && ev_vel_row(&t->step[s]) : !ev_sdef(&t->step[s], SK[k])) ||
                ui.ev_keep == EVC(SK[k], s))
                rw[n++] = EVC(SK[k], s);
        for (; j < m && (motion.event[idx[j]].place & 63u) == s; j++)
            rw[n++] = EVC(EVK_REC, idx[j]);
    }
    rw[n++] = EVC(EVK_ADD, 0);
    return n;
}
static uint32_t ev_row(uint32_t n) { return ui.ev_row < n ? ui.ev_row : n - 1u; }
/* the row selected: its code (*np: the count) */
static uint32_t ev_cur(uint16_t *rw, uint32_t *np)
{
    uint32_t n = ev_list(rw);
    if (np)
        *np = n;
    return rw[ev_row(n)];
}
/* the step a row is on (+ ADD: where it adds) */
static uint32_t ev_step_of(uint32_t c)
{
    uint32_t k = c >> 8, a = c & 0xFFu;
    return k == EVK_REC ? motion.event[a].place & 63u : k == EVK_ADD ? ui.ev_step : k == EVK_QNT ? 0u : a;
}

/* the page opened, or the track or its sound changed: + ADD's step and parameter inside what the track has */
static void ev_fix(void)
{
    const track_t *t = TSEL;
    if (ui.ev_step >= ev_len(t))
        ui.ev_step = (uint8_t)(ev_len(t) - 1u);
    if (!ev_id_ok(t, ui.ev_id)) {
        uint32_t id = lock_id(0);
        ui.ev_id = (uint8_t)(id != 0xFFu && ev_id_ok(t, id) ? id : ev_id_step(t, NSTEP, (uint32_t)-1, 1));
    }
}
static void ev_enter(void)
{
    ui.ev_step = (uint8_t)ui.cursor;
    ui.ev_id = 0xFFu;
    ui.ev_keep = 0;
    ui.ev_vel = 0;
    ev_fix();
}

/* the row showing c (after a move it may sit elsewhere in the order) */
static void ev_follow(uint32_t c)
{
    uint16_t rw[EV_ROWS];
    uint32_t n = ev_list(rw), r;
    for (r = 0; r < n && rw[r] != c; r++)
        ;
    if (r < n)
        ui.ev_row = (uint16_t)r;
}

/* what the list shows (graph_signature): the rows, their values, the one selected, PLAY, QUANTIZE, + ADD's place */
static uint32_t ev_sig(void)
{
    const track_t *t = TSEL;
    uint16_t rw[EV_ROWS];
    uint32_t n = ev_list(rw), h = 2166136261u, i, c;
    for (i = 0; i < n; i++) {
        c = rw[i];
        if (c >> 8 == EVK_REC)
            c ^= (uint32_t)motion.event[c & 0xFFu].place << 24 | (uint32_t)motion.event[c & 0xFFu].param << 16 |
                 (uint32_t)(uint16_t)motion.event[c & 0xFFu].value << 8;
        else if (ev_skd(c >> 8))
            c ^= (uint32_t)ev_sval(&t->step[c & 0xFFu], c >> 8) << 16;
        h = (h ^ c) * 16777619u;
    }
    return (h ^ ev_row(n) * 2654435761u ^ ((uint32_t)ui.ev_step << 8 | ui.ev_id) * 104729u ^
            (uint32_t)motion_enabled(t) * 7919u ^ (uint32_t)t->p[P_SQNT] * 1299709u ^ ui.ev_vel * 15485863u) *
           16777619u;
}

static const char *ev_act_name(void)
{
    uint16_t rw[EV_ROWS];
    uint32_t c = ev_cur(rw, 0), k = c >> 8;
    if (k == EVK_TOP)
        return "CLEAR";
    if (k == EVK_ADD)
        return ui.ev_id != EV_VEL ? "ADD" : ui.ev_vel ? "HIDE" : "SHOW";
    if (k != EVK_REC)
        return "--";
    return (motion.event[c & 0xFFu].param & MOTION_LOCK) ? "AUTO" : "LOCK";   /* what OCT+ turns it into */
}
static int ev_act_ready(void)
{
    uint16_t rw[EV_ROWS];
    uint32_t c = ev_cur(rw, 0), k = c >> 8;
    if (chain_busy())
        return 0;
    if (k == EVK_TOP)
        return motion_count(TSEL) != 0u;
    if (k == EVK_ADD)
        return ev_sid(ui.ev_id) || (motion.count < MOTION_MAX && ev_id_ok(TSEL, ui.ev_id));
    return k == EVK_REC;
}

static int ev_busy(void)
{
    if (!chain_busy())
        return 0;
    ui_message("STOP TO EDIT");
    return 1;
}

/* a CHANCE / RATCH / NUDGE / VEL row (code c, kind k, step a) turned s on KNOB slot (2 the step, 4 the value) */
static void ev_step_knob(track_t *t, uint32_t slot, int32_t s, uint32_t c)
{
    uint32_t k = c >> 8, a = c & 0xFFu, len = ev_len(t), m = (uint32_t)(s > 0 ? s : -s);
    int32_t to = (int32_t)a, at = (int32_t)a, v = ev_sval(&t->step[a], k);
    if (slot == 1u) {                                   /* the next step inside LEN where it is at its default */
        if (k == EVK_VEL)
            return;                                     /* (VEL: every note step has its row) */
        while (m--) {                                   /* a detent each */
            do
                at += s > 0 ? 1 : -1;
            while (at >= 0 && at < (int32_t)len && !ev_sdef(&t->step[at], k));
            if (at < 0 || at >= (int32_t)len)
                break;
            to = at;
        }
        if (to == (int32_t)a)
            return;
        step_undo_take(t, 0x400u | slot | k << 4);      /* (a knob turned on, this kind of row) */
        ev_sput(&t->step[a], k, ev_sdefv(k));
        ev_sput(&t->step[to], k, v);
        if (ui.ev_keep == c)
            ui.ev_keep = EVC(k, to);
        c = EVC(k, to);
    } else if (slot == 3u) {                            /* the value: CHANCE 0..100 %, RATCH x1..x4, NUDGE -8..+7, */
        v = k == EVK_CHANCE ? clamp(v + accel(EN_K1 + slot, s, 100), 0, 100) : k == EVK_RATCH ? clamp(v + s, 1, 4)
            : k == EVK_VEL ? clamp(v + accel(EN_K1 + slot, s, 126), 1, 127) : clamp(v + s, -8, 7);   /* VEL 1..127 */
        if (k == EVK_NUDGE && t->p[P_SQNT])
            ui_message("QUANTIZE IS ON");              /* (kept: it plays once QUANTIZE is OFF) */
        if (v == ev_sval(&t->step[a], k))
            return;
        step_undo_take(t, 0x400u | slot | k << 4);
        ev_sput(&t->step[a], k, v);
        ui.ev_keep = (uint16_t)c;                      /* (back at its default it stays listed while edited) */
    } else {
        return;
    }
    motion_undo_done(t);
    ev_follow(c);
}

/* KNOB slot turned s on AUTOMATION */
static void ev_knob(uint32_t slot, int32_t s)
{
    track_t *t = TSEL;
    uint16_t rw[EV_ROWS];
    uint32_t n, c = ev_cur(rw, &n), r = ev_row(n), k = c >> 8, i, step, id;
    const motion_event_t *e;
    const param_desc_t *d;
    if (slot == 0u) {
        uint32_t to = (uint32_t)clamp((int32_t)r + s, 0, (int32_t)n - 1);
        if (to != r) {                                  /* another row: the one edited may leave the list now */
            ui.ev_keep = 0;
            ui.ev_row = (uint16_t)to;
            ev_follow(rw[to]);
        }
        return;
    }
    if (ev_busy())
        return;
    if (k == EVK_TOP) {                                 /* PLAY: KNOB 4 right ON, left OFF */
        if (slot == 3u)
            motion_set_enabled(t, s > 0);
        return;
    }
    if (k == EVK_QNT) {                                 /* QUANTIZE: KNOB 4 right ON, left OFF */
        if (slot == 3u)
            t->p[P_SQNT] = s > 0;
        return;
    }
    if (k == EVK_ADD) {                                 /* + ADD: where it goes */
        ev_fix();
        if (slot == 1u)
            ui.ev_step = (uint8_t)clamp((int32_t)ui.ev_step + s, 0, (int32_t)ev_len(t) - 1);
        else if (slot == 2u)
            for (i = (uint32_t)(s > 0 ? s : -s); i; i--)
                ui.ev_id = (uint8_t)ev_id_step(t, NSTEP, ui.ev_id, s > 0 ? 1 : -1);
        return;
    }
    if (k != EVK_REC) {
        ev_step_knob(t, slot, s, c);
        return;
    }
    i = c & 0xFFu;
    e = &motion.event[i];
    step = e->place & 63u;
    id = MOTION_ID(e);
    if (slot == 1u) {                                   /* the next step where its parameter is free */
        uint32_t top = ev_len(t) > step ? ev_len(t) : step + 1u, m = (uint32_t)(s > 0 ? s : -s);
        int32_t to = (int32_t)step, at = (int32_t)step;
        while (m--) {                                   /* a detent each: the next free step */
            do
                at += s > 0 ? 1 : -1;
            while (at >= 0 && at < (int32_t)top && motion_find(t, (uint32_t)at, id) >= 0);
            if (at < 0 || at >= (int32_t)top)
                break;
            to = at;
        }
        if (to == (int32_t)step)
            return;
        motion_undo_take(t, 0x200u | slot | i << 4);   /* (a knob turned on, this record) */
        (void)motion_move(t, i, (uint32_t)to, id, e->value);
    } else if (slot == 2u) {                            /* another parameter, from the sound's own value */
        uint32_t to = id, m = (uint32_t)(s > 0 ? s : -s), nx;
        while (m--) {                                   /* a detent each: the next free parameter (no step field) */
            nx = ev_id_step(t, step, to, s > 0 ? 1 : -1);
            if (nx >= P_COUNT)
                break;
            to = nx;
        }
        if (to == id)
            return;
        motion_undo_take(t, 0x200u | slot | i << 4);   /* (a knob turned on, this record) */
        (void)motion_move(t, i, step, to, motion_base_value(t, to));
        ui.ev_id = (uint8_t)to;
    } else {                                            /* the value */
        d = track_desc(t, id);
        s = accel(EN_K1 + slot, s, d->fmt == F_ENUM ? 0 : d->max - d->min);
        motion_undo_take(t, 0x200u | slot | i << 4);   /* (a knob turned on, this record) */
        (void)motion_move(t, i, step, id, (int16_t)param_turn(d, e->value, s));
    }
    motion_undo_done(t);
    ev_follow(c);
}

/* OCT+: on + ADD what KNOB 2 / 3 set; a record's kind LOCK <-> AUTO; on PLAY, CLEAR (the dialog); QUANTIZE, a
 * step's field: nothing (their value is KNOB 4's) */
static void ev_oct(void)
{
    track_t *t = TSEL;
    uint16_t rw[EV_ROWS];
    uint32_t c = ev_cur(rw, 0), k = c >> 8;
    int32_t at;
    if (ev_busy())
        return;
    if (k == EVK_TOP) {
        if (!motion_count(t))
            ui_message("NOTHING TO CLEAR");
        else
            confirm_open(CF_CLEAR_MOTION, song.sel);
        return;
    }
    if (k == EVK_ADD) {
        ev_fix();
        if (!ev_id_ok(t, ui.ev_id))
            return;
        if (ui.ev_id == EV_VEL) {                       /* VEL: its rows shown / hidden (on the step, if a note) */
            ui.ev_vel ^= 1u;
            ui.ev_keep = 0;
            ev_follow(EVC(EVK_VEL, ui.ev_step));
            ui_message(ui.ev_vel ? "VEL SHOWN" : "VEL HIDDEN");
            return;
        }
        if (ev_sid(ui.ev_id)) {                         /* a step's CHANCE 50 % / RATCH x2 / NUDGE +4 */
            uint32_t sk = ev_skind(ui.ev_id);
            if (!ev_sdef(&t->step[ui.ev_step], sk)) {
                ev_follow(EVC(sk, ui.ev_step));
                ui_message("ALREADY LISTED");
                return;
            }
            step_undo_take(t, 0);
            ev_sput(&t->step[ui.ev_step], sk, ev_sadd(sk));
            motion_undo_done(t);
            ev_follow(EVC(sk, ui.ev_step));
            ui_message(sk == EVK_CHANCE ? "CHANCE ADDED" : sk == EVK_RATCH ? "RATCH ADDED" :
                       t->p[P_SQNT] ? "QUANTIZE IS ON" : "NUDGE ADDED");
            return;
        }
        if ((at = motion_find(t, ui.ev_step, ui.ev_id)) >= 0) {   /* one is there already: show it */
            ev_follow(EVC(EVK_REC, at));
            ui_message("ALREADY LISTED");
            return;
        }
        if (motion.count >= MOTION_MAX) {
            ui_message("AUTOMATION FULL");
            return;
        }
        motion_undo_take(t, 0);
        (void)motion_set_lock(t, ui.ev_step, ui.ev_id, motion_base_value(t, ui.ev_id));
        motion_undo_done(t);
        if ((at = motion_find(t, ui.ev_step, ui.ev_id)) >= 0)
            ev_follow(EVC(EVK_REC, at));
        ui_message("LOCK ADDED");
        return;
    }
    if (k == EVK_REC) {
        const motion_event_t e = motion.event[c & 0xFFu];
        uint32_t lock = !(e.param & MOTION_LOCK);
        motion_undo_take(t, 0);
        (void)motion_put(t, e.place & 63u, MOTION_ID(&e), e.value, lock ? MOTION_LOCK : 0u);
        motion_undo_done(t);
        ui_message(lock ? "NOW A LOCK" : "NOW AUTOMATION");
    }
}

/* SEQ > DETAIL (1.5.1, #199; ui_input.c sd_turn, ui_draw.c, ui_graph.c graph_detail): KNOB 1..4 are these fields of
 * the cursor step, the ones the rows above list (one storage). A step takes them where they play: a NOTE step (notes or
 * hits); CHANCE a TIE too (a failed roll cuts the held note). A REST or an empty step none (nothing to hear) */
static const uint8_t SD_KIND[4] = {EVK_CHANCE, EVK_RATCH, EVK_NUDGE, EVK_VEL};
static int sd_takes(const step_t *s, uint32_t k) { return step_on(s) || (k == EVK_CHANCE && s->time == ST_TIE); }

/* EDIT: the record goes, a CHANCE / RATCH / NUDGE / VEL back to its default (the row below moves up) */
static void ev_delete(void)
{
    track_t *t = TSEL;
    uint16_t rw[EV_ROWS];
    uint32_t n, c = ev_cur(rw, &n), r = ev_row(n), k = c >> 8;
    const motion_event_t *e;
    if (k == EVK_TOP || k == EVK_QNT || k == EVK_ADD) {
        ui_message("NOTHING TO DELETE");
        return;
    }
    if (ev_busy())
        return;
    if (k == EVK_REC) {
        e = &motion.event[c & 0xFFu];
        motion_undo_take(t, 0);
        motion_delete_event(t, e->place & 63u, MOTION_ID(e));
    } else {
        step_undo_take(t, 0);
        ev_sput(&t->step[c & 0xFFu], k, ev_sdefv(k));
        if (ui.ev_keep == c)
            ui.ev_keep = 0;
    }
    motion_undo_done(t);
    ui.ev_row = (uint16_t)r;                             /* (ev_row clamps it to the new count) */
    ui_message(k == EVK_VEL ? "VEL 96" : "DELETED");
}
