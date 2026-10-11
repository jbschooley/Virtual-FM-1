/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM6's operator pages (EDIT family, an FM6 track only, after EDIT 2; Discussion #76): the six operators of the track's
 * own patch (eng_fm6.c fm6_patch) edited on the device. KNOB 1 is the operator (OP 1..6, as the MOD page's SLOT: one
 * choice for the three pages, kept while one goes round them), KNOB 2..4 its bytes:
 *   OPERATOR    OP  RATIO / FIXED (the coarse frequency: 0.50, 1..31; fixed 1 10 100 1000 HZ)  FINE (0..99)  LEVEL (0..99)
 *   OP ENV      OP  STAGE (1 ATK, 2 DEC, 3 SUS, 4 REL: the envelope's stage KNOB 3 / 4 edit)  RATE  LEVEL (0..99)
 *   OPERATOR 2  OP  MODE (RATIO / FIXED)  DTUN (-7..+7)  VEL (the velocity sensitivity, 0..7)
 * The panel: OPERATOR and OPERATOR 2 the algorithm chart (ui_graph.c graph_fm6) with the operator picked in the accent
 * and its frequency top right ("x1.50", "440 HZ"); OP ENV its envelope (graph_fm6env), the stage picked in the accent.
 * A detent writes one byte of the patch (fm6_set_patch: the audio ISR takes it at its next block) and makes the patch
 * the track's own: SLOT OWN (as a patch from the editor; a factory F n edited is OWN from then on). Each knob gesture is
 * one undo (SAVE held: the sound before it, the patch and SLOT with it; ui.c edit_undo_take). The patch is saved with
 * projects and user presets as before (no new format, no new parameter ids: nothing here is automatable or lockable).
 * The values shown are the patch's own: EDIT 1 / 2's macros still apply on top of it as before (MLVL moves the
 * modulators' LEVEL, MRAT their RATIO, MEG their RATEs, VMOD their VEL, DTUN the carriers' pitch, FB the feedback, ALG
 * another algorithm: the chart and which operators are carriers follow ALG). Included by ui_graph.c (after icons.c: the
 * cards' icons); the cards: ui_draw.c, the knobs: ui_input.c edit_param. */
static int32_t accel(uint32_t role, int32_t s, int32_t range);   /* (ui_input.c) */
static uint8_t fop_op;                               /* the operator shown: 0..5 = OP 1..6 (the patch holds OP 6 first) */
static uint8_t fop_stg;                              /* OP ENV: the stage KNOB 3 / 4 edit, 0..3 */

enum { FO_OP, FO_STG, FO_CRS, FO_FINE, FO_LVL, FO_MODE, FO_DTUN, FO_VEL, FO_RATE, FO_ELVL };
static const uint8_t FO_PAGE[3][4] = {
    {FO_OP, FO_CRS, FO_FINE, FO_LVL},                /* OPERATOR */
    {FO_OP, FO_MODE, FO_DTUN, FO_VEL},               /* OPERATOR 2 */
    {FO_OP, FO_STG, FO_RATE, FO_ELVL},               /* OP ENV */
};
/* per field: its byte in an operator's 21 (RATE / LEVEL: + the stage) */
static const uint8_t FO_OFF[10] = {0, 0, FP_FC, FP_FF, FP_OL, FP_MODE, FP_DET, FP_KVS, FP_R1, FP_L1};

static int fop_page(uint32_t g) { return g >= GR_FMOP && g <= GR_FMENV; }
static int fop_ok(void) { return TSEL->eng_req % NENGINES == ENGI_FM6; }
static uint32_t fop_field(uint32_t slot)
{
    uint32_t g = cur_page()->graph;
    return FO_PAGE[g == GR_FMOP2 ? 1u : g == GR_FMENV ? 2u : 0u][slot & 3u];
}
/* the selected operator's 21 bytes in track tr's patch */
static const uint8_t *fop_bytes(uint32_t tr) { return fm6_patch[tr % NTRK] + (5u - fop_op % 6u) * FP_OP; }
/* field f's byte in the 155-byte voice (the selected operator, the stage) */
static uint32_t fop_byte(uint32_t f)
{
    return (5u - fop_op % 6u) * FP_OP + FO_OFF[f] + (f == FO_RATE || f == FO_ELVL ? fop_stg & 3u : 0u);
}

/* byte b of the selected track's patch = v: the track's own patch now (SLOT OWN), one undo per knob gesture */
static void fop_write(uint32_t b, uint32_t v)
{
    uint32_t tr = song.sel % NTRK;
    track_t *t = TSEL;
    uint8_t p[FP_SIZE + 1u];
    if (fm6_patch[tr][b] == v)
        return;
    edit_undo_take(t, 0x1000u | b, UNDO_SOUND);      /* (a knob turned on, this byte: one copy) */
    memcpy(p, fm6_patch[tr], FP_SIZE);
    p[b] = (uint8_t)v;
    fm6_set_patch(tr, p);
    fm6_own_ok &= (uint8_t)~(1u << tr);              /* (an own patch kept aside for SLOT: this one replaces it) */
    fm6_slot[tr] = FM6_OWN;                          /* (fm6_poll: nothing reloads over it) */
    t->p[P_E7] = FM6_OWN;
    motion_undo_done(t);
}

/* KNOB slot turned by steps on an operator page */
static void fop_knob(uint32_t slot, int32_t steps)
{
    uint32_t f = fop_field(slot), b;
    int32_t max;
    if (!fop_ok() || !steps)
        return;
    if (f == FO_OP) {
        fop_op = (uint8_t)clamp((int32_t)fop_op + steps, 0, 5);
        return;
    }
    if (f == FO_STG) {
        fop_stg = (uint8_t)clamp((int32_t)fop_stg + steps, 0, 3);
        return;
    }
    b = fop_byte(f);
    max = (int32_t)fm6_max(b);
    fop_write(b, (uint32_t)clamp((int32_t)fm6_patch[song.sel % NTRK][b] + accel(EN_K1 + slot, steps, max), 0, max));
}

/* "w.ff" (frac: digits places) */
static void fop_dec(char *s, uint32_t w, uint32_t frac, uint32_t digits)
{
    fmt_int(s, (int32_t)w);
    s += str_len(s);
    *s++ = '.';
    if (digits > 1u)
        *s++ = (char)('0' + frac / 10u % 10u);
    *s++ = (char)('0' + frac % 10u);
    *s = 0;
}
/* the operator's frequency (op: its 21 bytes): "x1.50" (ratio: COARSE, 0 = 0.50, x (1 + FINE / 100)) or "440 HZ"
 * (fixed: 10 ^ (COARSE & 3 + FINE / 100) Hz, as fm6_core.c fm6_op_freq); s holds 12 */
static void fop_freq(const uint8_t *op, char *s)
{
    uint32_t c = op[FP_FC] & 31u, f = op[FP_FF] % 100u, m = 1000u, k;
    if (!op[FP_MODE]) {
        uint32_t r = (c ? c * 100u : 50u) * (100u + f) / 100u;   /* hundredths */
        s[0] = 'x';
        fop_dec(s + 1, r / 100u, r % 100u, 2u);
        return;
    }
    for (k = 0; k < f; k++)
        m = (m * 67063u + 32768u) >> 16;            /* x 10 ^ 0.01 (Q16): m = 1000 .. 9772 */
    for (k = 0; k < (c & 3u); k++)
        m *= 10u;                                    /* mHz */
    if (m < 10000u)
        fop_dec(s, m / 1000u, m % 1000u / 10u, 2u);
    else if (m < 100000u)
        fop_dec(s, m / 1000u, m % 1000u / 100u, 1u);
    else if (m < 1000000u)
        fmt_int(s, (int32_t)(m / 1000u));
    else
        fop_dec(s, m / 1000000u, m % 1000000u / 10000u, 2u);
    str_cpy(s + str_len(s), m < 1000000u ? " HZ" : " KHZ", 5);
}

/* card slot of an operator page: its label, value (12), unit, gauge (0..1000, -1 none), icon; 0 = shown DIM (a level
 * at 0, the operator silent) */
static int fop_card(uint32_t slot, const char **label, char *val, const char **unit, int32_t *ratio, uint32_t *icon)
{
    static const char *const STG[4] = {"ATK", "DEC", "SUS", "REL"};
    uint32_t f = fop_field(slot), v;
    const uint8_t *op = fop_bytes(song.sel);
    *unit = "";
    *ratio = -1;
    if (f == FO_OP) {
        *label = "OP";
        fmt_int(val, (int32_t)fop_op + 1);
        *unit = "/6";
        *ratio = (int32_t)fop_op * 200;
        *icon = ICON_ALGORITHM;
        return 1;
    }
    if (f == FO_STG) {
        *label = "STAGE";
        fmt_int(val, (int32_t)fop_stg + 1);
        *unit = STG[fop_stg & 3u];
        *ratio = (int32_t)fop_stg * 333;
        *icon = ICON_ENV;
        return 1;
    }
    v = fm6_patch[song.sel % NTRK][fop_byte(f)];
    *ratio = (int32_t)(v * 1000u / fm6_max(fop_byte(f)));
    fmt_int(val, (int32_t)v);
    switch (f) {
    case FO_CRS:
        *label = op[FP_MODE] ? "FIXED" : "RATIO";
        *icon = ICON_RATIO;
        if (op[FP_MODE]) {
            static const char *const HZ[4] = {"1", "10", "100", "1000"};
            str_cpy(val, HZ[v & 3u], 12);
            *unit = "HZ";
        } else if (!v) {
            str_cpy(val, "0.50", 12);
        }
        return 1;
    case FO_FINE:
        *label = "FINE";
        *icon = ICON_TUNE;
        return 1;
    case FO_LVL:
    case FO_ELVL:
        *label = "LEVEL";
        *icon = ICON_LEVEL;
        return f == FO_ELVL || v != 0u;
    case FO_MODE:
        *label = "MODE";
        str_cpy(val, v ? "FIXED" : "RATIO", 12);
        *icon = ICON_RATIO;
        return 1;
    case FO_DTUN:
        *label = "DTUN";
        val[0] = v > 7u ? '+' : 0;
        fmt_int(val + (v > 7u), (int32_t)v - 7);
        *icon = ICON_DETUNE;
        return 1;
    case FO_VEL:
        *label = "VEL";
        *icon = ICON_ACCENT;
        return 1;
    default:                                         /* FO_RATE */
        *label = "RATE";
        *icon = ICON_RATE;
        return 1;
    }
}
