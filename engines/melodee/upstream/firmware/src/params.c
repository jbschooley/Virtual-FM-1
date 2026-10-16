/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Parameter descriptors, formatting and the page table. */
static const char *const N_LSYNC[] = {"OFF", "4BAR", "2BAR", "1/1", "1/2", "1/4", "1/8", "8T", "1/16", "16T", "1/32"};
static const uint8_t LSYNC_DIV[11] = {0, 9, 8, 7, 6, 0, 1, 4, 2, 5, 3};
static const char *const N_LWAVE[] = {"SIN", "TRI", "SAW", "SQR", "S&H"};
static const char *const N_AMODE[] = {"OFF", "UP", "DN", "UPDN", "RND", "ORD", "REPEAT"};
static const char *const N_DIV[] = {"1/4", "1/8", "1/16", "1/32", "8T", "16T", "1/2", "1/1", "2BAR", "4BAR"};
static const char *const N_RECQ[] = {"OFF", "1/4", "1/8", "1/16", "1/32", "8T", "16T", "1/2", "1/1", "2BAR", "4BAR"};
static const char *const N_LTRIG[] = {"NOTE", "FREE"};
static const char *const N_LPOL[] = {"BI", "UNI"};
static const char *const N_FX[] = {"OFF", "PHASER", "WIDTH", "FORMANT", "TAPE", "SPIT", "ECHO", "ROOM", "REVERB", "CHORUS"};
static const char *const N_ONOFF[] = {"OFF", "ON"};
static const char *const N_QUANT[] = {"OFF", "SNAP", "WHITE", "ALL", "MPC"};   /* Q_OFF .. Q_MPC (seq.c kb_map, midi_map) */
/* chord keys (chord.c): OFF, the diatonic triad / seventh of the track's ROOT and SCALE on the key, fixed shapes */
static const char *const N_CHRD[] = {"OFF", "DIA3", "DIA7", "MAJ", "MIN", "DOM7", "MAJ7", "MIN7", "SUS4", "POW"};
static const char *const N_VOIC[] = {"CLOSE", "OPEN", "INV1", "INV2", "+OCT"};   /* VC_CLOSE .. VC_BASS */
static const char *const N_VOICE[] = {"POLY", "MONO", "LEG", "UNI"};   /* V_POLY .. V_UNISON */
static const char *const N_GLMODE[] = {"RATE", "TIME"};
static const char *const N_PRIO[] = {"LAST", "LOW", "HIGH"};
static const char *const N_ALLOC[] = {"ROT", "REUSE"};
static const char *const N_ORDER[] = {"NOTE", "PLAY"};
static const char *const N_CLOCK[] = {"INT", "USB", "TRS"};
static const char *const N_MIDI_INPUT[] = {"USB", "TRS"};
static const char *const N_ROUTE[] = {"CH1-4", "SEL", "CH5-8", "CH9-12", "CH13-16"}; /* Keep saved CH1-4/SEL ids; append ranges. */
static const char *const N_NOTE[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const N_DASH[] = {"--"};
static const char *const N_GO[] = {"--", "GO"};
static const char *const N_BOOT[] = {"OFF", "A", "B", "C", "D"};
static const char *const N_DRUMCH[] = {"OFF", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14",
                                       "15", "16"};
/* GLO > SYSTEM KNOB 2, DRUM: the MIDI channel that plays the first DRUM track within ROUT (none: normal
 * routing), 0 OFF, 1..16; 10 as General MIDI's. A device setting kept with the settings (ext.drumch), not the
 * project's: the column shows drumch_cell (seq.c midi_track) */
static uint8_t settings_drumch = 10;
static int16_t drumch_cell;
#define PROJ_TMPL 5                                      /* SLOT 5: the template (project.c tmpl) */
/* SAVE > PROJECT KNOB 2, BOOT: the project power-on loads (OFF, A..D; OFF or an empty slot: the template, if one is
 * saved). A device setting kept with the settings (ext.boot), not the project's: the column shows boot_cell */
static uint8_t settings_boot;
static int16_t settings_budget = VOICE_BUDGET_DEFAULT;
/* Device recording preferences: defaults preserve existing behavior. */
#include "recording_preferences.h"
static int16_t boot_cell;
/* modulation matrix (mod.c): sources, destinations (E1..E8 = P_E0..P_E7: shown with the engine's labels) */
static const char *const N_MSRC[] = {"OFF", "LFO", "ENV", "VEL", "KEY", "RAND", "MODW", "AT", "EXPR", "S&H", "SLEW"};
static const char *const N_MDST[] = {"OFF", "PITCH", "CUT", "SHP", "AMP", "PAN", "", "", "", "", "RATE",
                                     "VIB", "E1", "E2", "E3", "E4", "E5", "E6", "E7", "E8", "DEPTH"};
static const char *const N_ENGNAME[] = {"-", "-", MELODEE_LEGACY_EXTRAS ? "PHASE" : "-", "SID", "-", MELODEE_LEGACY_EXTRAS ? "VOICE" : "-", MELODEE_LEGACY_EXTRAS ? "TRIO" : "-", MELODEE_LEGACY_EXTRAS ? "WHEEL" : "-", "-", MELODEE_LEGACY_EXTRAS ? "PHYS" : "-",
                                             "DRUM", MELODEE_LEGACY_EXTRAS ? "NOISE" : "-", "FM6", MELODEE_SLICE ? "SLICE" : "-", "-", "CZ-1", "-", "-", "-", "PROPHET"};

#define PD(l, f, mn, mx, df) {l, f, mn, mx, df, 0, 0}
#define PE(l, n, df) {l, F_ENUM, 0, (int16_t)(sizeof(n) / sizeof(n[0]) - 1), df, n, 0}

/* FM6's pages (an FM6 track only): the selected track's patch (fm6_patch), its operator switches and the FM6 function
 * settings, in the DX7's ranges. The operator pages show operator fm6_opsel (the PRESETS knob picks it); the STORE
 * page saves the whole sound into user preset slot fm6_bslot (fm6_store.c). The values are not track parameters: page_desc gives a copy
 * (fm6_cell), fm6_page_put writes it back */
static const char *const N_FM6ALG[32] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15",
                                        "16", "17", "18", "19", "20", "21", "22", "23", "24", "25", "26", "27", "28",
                                        "29", "30", "31", "32"};
static const char *const N_FMCRV[] = {"-LIN", "-EXP", "+EXP", "+LIN"};
static const char *const N_FMMODE[] = {"RATIO", "FIXED"};
static const char *const N_FMLFW[] = {"TRI", "SAWDN", "SAWUP", "SQR", "SIN", "S&H"};
static const char *const N_FMPM[] = {"PEDAL", "ON"};   /* portamento: while CC 65 is down (Dexed), always */
static const char *const N_FMDEST[] = {"-", "P", "A", "PA", "E", "PE", "AE", "PAE"};   /* pitch, amp, EG bias */
static const char *const N_FMENG[] = {"MODRN", "MARK I", "OPL"};                  /* Dexed's engine resolutions */
static const char *const N_FMONOFF[] = {"OFF", "ON"};
static uint8_t fm6_opsel;
static uint16_t fm6_bslot;                    /* OP1..OP6 = 0..5; user preset slot 0..63 (U01..U64) */
static int16_t fm6_cell[4];
static const param_desc_t FM6_OPD[FP_OP + 1] = {        /* an operator; FP_OP: its switch */
    [FP_R1] = PD("R1", F_INT, 0, 99, 99), [FP_R1 + 1] = PD("R2", F_INT, 0, 99, 99),
    [FP_R1 + 2] = PD("R3", F_INT, 0, 99, 99), [FP_R1 + 3] = PD("R4", F_INT, 0, 99, 99),
    [FP_L1] = PD("L1", F_INT, 0, 99, 99), [FP_L1 + 1] = PD("L2", F_INT, 0, 99, 99),
    [FP_L1 + 2] = PD("L3", F_INT, 0, 99, 99), [FP_L1 + 3] = PD("L4", F_INT, 0, 99, 0),
    [FP_BP] = PD("BRK", F_FMNOTE, 0, 99, 39), [FP_LD] = PD("LDEP", F_INT, 0, 99, 0),
    [FP_RD] = PD("RDEP", F_INT, 0, 99, 0), [FP_LC] = PE("LCRV", N_FMCRV, 0), [FP_RC] = PE("RCRV", N_FMCRV, 0),
    [FP_RS] = PD("RSCL", F_INT, 0, 7, 0), [FP_AMS] = PD("AMS", F_INT, 0, 3, 0), [FP_KVS] = PD("VEL", F_INT, 0, 7, 0),
    [FP_OL] = PD("OUT", F_INT, 0, 99, 99), [FP_MODE] = PE("MODE", N_FMMODE, 0),
    [FP_FC] = PD("CRS", F_FMFRQ, 0, 31, 1), [FP_FF] = PD("FINE", F_FMFRQ, 0, 99, 0),
    [FP_DET] = PD("DTN", F_OFS, 0, 14, 7), [FP_OP] = PE("ON", N_FMONOFF, 1),
};
static const param_desc_t FM6_GD[FP_NAME - FP_PR1] = {  /* the voice: FP_PR1 .. FP_TRNSP */
    PD("R1", F_INT, 0, 99, 99), PD("R2", F_INT, 0, 99, 99), PD("R3", F_INT, 0, 99, 99), PD("R4", F_INT, 0, 99, 99),
    PD("L1", F_INT, 0, 99, 50), PD("L2", F_INT, 0, 99, 50), PD("L3", F_INT, 0, 99, 50), PD("L4", F_INT, 0, 99, 50),
    PE("ALG", N_FM6ALG, 0), PD("FB", F_INT, 0, 7, 0), PE("SYNC", N_FMONOFF, 1),
    PD("SPEED", F_INT, 0, 99, 35), PD("DELAY", F_INT, 0, 99, 0), PD("PMD", F_INT, 0, 99, 0), PD("AMD", F_INT, 0, 99, 0),
    PE("KSYNC", N_FMONOFF, 1), PE("WAVE", N_FMLFW, 0), PD("PMS", F_INT, 0, 7, 3), {"TRNS", F_OFS, 0, 48, 24, 0, "st"},
};
static const param_desc_t FM6_FD[FM6_NFN] = {           /* the FM6 function settings: FN_PBUP .. FN_ENGINE */
    {"BEND+", F_INT, 0, 12, 3, 0, "st"}, {"BEND-", F_INT, 0, 12, 3, 0, "st"}, PD("STEP", F_INT, 0, 12, 0),
    PE("PORTA", N_FMPM, 0), PD("TIME", F_INT, 0, 127, 0), PE("GLISS", N_FMONOFF, 0),
    PD("WHEEL", F_INT, 0, 99, 99), PE("W.DST", N_FMDEST, 1), PD("FOOT", F_INT, 0, 99, 0), PE("F.DST", N_FMDEST, 0),
    PD("BRTH", F_INT, 0, 99, 0), PE("B.DST", N_FMDEST, 0), PD("AFTER", F_INT, 0, 99, 0), PE("A.DST", N_FMDEST, 0),
    PE("DXVEL", N_FMONOFF, 0), PE("ENGIN", N_FMENG, FM6_MARK1),
};
static const param_desc_t FM6_SD[4] = {                 /* the STORE page: the slot, then its actions (OCT+) */
    PD("SLOT", F_INT, 1, UP_SLOTS, 1), PD("STORE", F_INT, 0, 1, 0), PD("SEND", F_INT, 0, 1, 0), PD("INIT", F_INT, 0, 1, 0),
};

/* the frequency of operator fm6_opsel for its CRS and FINE columns: a ratio (CRS: its step 0.5, 1, 2 ..; FINE: the
 * whole ratio) or, FIXED, Hz (CRS: 1, 10, 100, 1000) */
static void fm6_freq_text(char *val, const char **unit, int coarse)
{
    const uint8_t *op = &fm6_patch[song.sel % NTRK][(5u - fm6_opsel % 6u) * FP_OP];
    int32_t c = op[FP_FC], f = coarse ? 0 : op[FP_FF];
    if (!op[FP_MODE]) {
        if (coarse && c)
            fmt_int(val, c);
        else
            fmt_fix(val, (c ? c * 100 : 50) * (100 + f) / 100, 2);
        *unit = "";
    } else {                                          /* Hz x 100 = 2^(log2(10) (COARSE % 4 + FINE / 100) + log2(100)) */
        uint32_t h = fm6_pow2(217706 * ((c & 3) * 100 + f) / 100 + 435412 - (16 << 16));
        if (h < 1000u)
            fmt_fix(val, (int32_t)h, 2);
        else if (h < 100000u)
            fmt_fix(val, (int32_t)(h / 10u), 1);
        else
            fmt_int(val, (int32_t)(h / 100u));
        *unit = "Hz";
    }
}

static const param_desc_t TP[P_COUNT] = {
    [P_FXTYPE] = PE("FX", N_FX, 0),
    [P_FXMIX] = PD("MIX", F_PCT, 0, 127, 0),
    [P_FXRATE] = PD("RATE", F_LFOHZ, 0, 127, 60),
    [P_FXCHAR] = PD("CHAR", F_PCT, 0, 127, 64),
    [P_FX2TYPE] = PE("FX", N_FX, 0), [P_FX2MIX] = PD("MIX", F_PCT, 0, 127, 0),
    [P_FX2RATE] = PD("RATE", F_LFOHZ, 0, 127, 60), [P_FX2CHAR] = PD("CHAR", F_PCT, 0, 127, 64),
    [P_FX3TYPE] = PE("FX", N_FX, 0), [P_FX3MIX] = PD("MIX", F_PCT, 0, 127, 0),
    [P_FX3RATE] = PD("RATE", F_LFOHZ, 0, 127, 60), [P_FX3CHAR] = PD("CHAR", F_PCT, 0, 127, 64),
    [P_LN0] = PD("KICK", F_PCT, 0, 127, 127),
    [P_LN1] = PD("SNARE", F_PCT, 0, 127, 127),
    [P_LN2] = PD("CLAP", F_PCT, 0, 127, 127),
    [P_LN3] = PD("HATCL", F_PCT, 0, 127, 127),
    [P_LN4] = PD("HATOP", F_PCT, 0, 127, 127),
    [P_LN5] = PD("TOM", F_PCT, 0, 127, 127),
    [P_LN6] = PD("RIM", F_PCT, 0, 127, 127),
    [P_LN7] = PD("BELL", F_PCT, 0, 127, 127),
    [P_LEVEL] = PD("LVL", F_DB, 0, 127, 104),
    [P_ATK] = PD("ATK", F_TIME, 0, 127, 10),
    [P_DEC] = PD("DEC", F_TIME, 0, 127, 70),
    [P_SUS] = PD("SUS", F_PCT, 0, 127, 90),
    [P_REL] = PD("REL", F_TIME, 0, 127, 60),
    [P_ED_FLT] = PD("FLT", F_BIPCT, -64, 63, 0),
    [P_ED_PIT] = PD("PIT", F_BIPCT, -64, 63, 0),
    [P_ED_SHP] = PD("SHP", F_BIPCT, -64, 63, 0),
    [P_RECQ] = PE("QNT", N_RECQ, 0),
    [P_LSYNC] = PE("SYNC", N_LSYNC, 0),
    [P_LTRIG] = PE("TRIG", N_LTRIG, 0),
    [P_LPOL] = PE("POL", N_LPOL, 0),
    [P_SPRD] = PD("SPRD", F_PCT, 0, 127, 0),
    [P_LRATE] = PD("RATE", F_LFOHZ, 0, 127, 60),
    [P_LWAVE] = PE("WAVE", N_LWAVE, 0),
    [P_LPHASE] = PD("PHS", F_INT, 0, 127, 0),
    [P_LFADE] = PD("FADE", F_TIME, 0, 127, 0),
    [P_LD_PIT] = PD("PIT", F_BIPCT, -64, 63, 0),
    [P_LD_FLT] = PD("FLT", F_BIPCT, -64, 63, 0),
    [P_LD_SHP] = PD("SHP", F_BIPCT, -64, 63, 0),
    [P_LD_AMP] = PD("AMP", F_PCT, 0, 127, 0),
    [P_AMODE] = PE("MODE", N_AMODE, 0),
    [P_ARATE] = PE("RATE", N_DIV, 2),
    [P_AOCT] = PD("OCT", F_INT, 1, 4, 1),
    [P_AGATE] = PD("GATE", F_PCT, 1, 127, 64),
    [P_ASWING] = PD("SWG", F_PCT, 0, 100, 0),
    [P_APROB] = PD("PROB", F_PCT, 0, 127, 127),
    [P_AHOLD] = PE("HOLD", N_ONOFF, 0),
    [P_AORDER] = PE("ORD", N_ORDER, 0),
    [P_ROOT] = PD("ROOT", F_NOTE, 0, 11, 0),
    [P_SCALE] = PE("SCL", N_SCALE, 0),
    [P_QUANT] = PE("QNT", N_QUANT, 0),
    [P_TRANS] = PD("TRN", F_SEMI, -24, 24, 0),
    [P_SLEN] = PD("LEN", F_STEPS, 1, NSTEP, 16),
    [P_SDIV] = PE("DIV", N_DIV, 2),
    [P_SSWING] = PD("SWG", F_PCT, 0, 100, 0),
    [P_SGATE] = PD("GATE", F_PCT, 1, 127, 64),
    [P_RESERVED33] = PD("", F_INT, 0, 127, 0),
    [P_RESERVED34] = PD("", F_INT, 0, 127, 0),
    [P_RESERVED35] = PD("", F_INT, 0, 127, 0),
    [P_RESERVED36] = PD("", F_INT, 0, 127, 0),
    [P_VOICE] = PE("VCE", N_VOICE, 0),
    [P_GLIDE] = PD("GLD", F_TIME, 0, 127, 0),
    [P_GLMODE] = PE("GLMOD", N_GLMODE, 0),
    [P_PRIO] = PE("PRIO", N_PRIO, 0),
    [P_ALLOC] = PE("ALLOC", N_ALLOC, 0),
    [P_DETUNE] = PD("DTUNE", F_INT, 0, 127, 40),
    [P_PAN] = PD("PAN", F_BIPCT, -64, 63, 0),
    [P_MUTE] = PE("MUTE", N_ONOFF, 0),
    [P_RESERVED45] = PD("", F_INT, 0, 127, 0),
    [P_RESERVED46] = PD("", F_INT, 0, 127, 0),
    [P_RESERVED47] = PD("", F_INT, 0, 127, 0),
    [P_RESERVED48] = PD("", F_INT, 0, 127, 0),
#define MSLOT(k) [P_M##k##SRC] = PE("SRC" #k, N_MSRC, 0), [P_M##k##DST] = PE("DST" #k, N_MDST, 0), \
                 [P_M##k##AMT] = PD("AMT" #k, F_BIPCT, -64, 63, 0)
    MSLOT(1), MSLOT(2), MSLOT(3), MSLOT(4),
#undef MSLOT
/* the OP ENV / OP LEVEL values of DIGITAL (ids 61..80, retired): inert, on no page and in no editor layout. Their labels
 * stay: the editor's library files key parameters by label ("ATK#2" ..), so an old file's values still find them */
#define FMOP(k) [P_FM##k##_ATK] = PD("ATK", F_TIME, 0, 127, 0), \
                [P_FM##k##_DEC] = PD("DEC", F_TIME, 0, 127, 0), \
                [P_FM##k##_SUS] = PD("SUS", F_PCT, 0, 127, 127), \
                [P_FM##k##_REL] = PD("REL", F_TIME, 0, 127, 0), \
                [P_FM##k##_LEVEL] = PD("LVL", F_PCT, 0, 127, 127)
    FMOP(1), FMOP(2), FMOP(3), FMOP(4),
#undef FMOP
    [P_FXSEND] = PD("SEND", F_PCT, 0, 127, 0),
    [P_FXSEND2] = PD("SEND 2", F_PCT, 0, 127, 0),
    [P_FXSEND3] = PD("SEND 3", F_PCT, 0, 127, 0),
    [P_FXTONE] = PD("TONE", F_TONE, -FX_TONE_MAX, FX_TONE_MAX, 0),
    [P_FX2TONE] = PD("TONE", F_TONE, -FX_TONE_MAX, FX_TONE_MAX, 0),
    [P_FX3TONE] = PD("TONE", F_TONE, -FX_TONE_MAX, FX_TONE_MAX, 0),
    [P_CHRD] = PE("CHRD", N_CHRD, 0),
    [P_VOIC] = PE("VOIC", N_VOIC, 0),
    [P_MPCDEG] = PD("DEG", F_INT, 1, 12, 1),         /* its max: the scale's notes (track_desc) */
};

static const param_desc_t GP[G_COUNT] = {
    [G_BPM] = PD("BPM", F_BPM, 40, 240, 120),
    [G_SWING] = PD("SWG", F_PCT, 0, 100, 0),
    [G_CLOCK] = PE("CLK", N_CLOCK, 0),
    [G_TUNE] = PD("TUNE", F_INT, -50, 50, 0),
    [G_RESERVED4] = PD("", F_INT, 0, 127, 0),
    [G_RESERVED5] = PD("", F_INT, 0, 127, 0),
    [G_RESERVED6] = PD("", F_INT, 0, 127, 0),
    [G_RESERVED7] = PD("", F_INT, 0, 127, 0),
    [G_RESERVED8] = PD("", F_INT, 0, 127, 0),
    [G_RESERVED9] = PD("", F_INT, 0, 127, 0),
    [G_RESERVED10] = PD("", F_INT, 0, 127, 0),
    [G_RESERVED11] = PD("", F_INT, 0, 127, 0),
    [G_MIDI] = PE("MIDI", N_MIDI_INPUT, 0),
    [G_DRUMCH] = PE("DRUM", N_DRUMCH, 10),
    [G_ROUTE] = PE("ROUT", N_ROUTE, 0),          /* (was "--": stored 0 = CH1-4, as MIDI IN always was) */
    [G_INFO] = PD("CPU", F_INT, 0, 0, 0),
    [G_SLOT] = PD("SLOT", F_INT, 1, PROJ_TMPL, 1),     /* PROJ_TMPL: the template (param_format: TMPL) */
    [G_BOOT] = PE("BOOT", N_BOOT, 0),
    [G_LOAD] = PE("LOAD", N_GO, 0),
    [G_SAVE] = PE("SAVE", N_GO, 0),
    [G_ENGSEL] = PE("ENG", N_ENGNAME, 0),
    [G_A4] = {"A4", F_INT, A4_MIN, A4_MAX, A4_DEFAULT, 0, "Hz"},
    [G_CLRSEQ] = PE("CLRSQ", N_GO, 0),
    [G_INITSND] = PE("INIT", N_GO, 0),
    [G_RESERVED24] = PD("", F_INT, 0, 127, 0),
    [G_RESERVED25] = PD("", F_INT, 0, 127, 0),
    [G_RESERVED26] = PD("", F_INT, 0, 127, 0),
    [G_MFXTYPE] = PE("TYPE", N_FX, 0),
    [G_MFXMIX] = PD("MIX", F_PCT, 0, 127, 127),
    [G_MFXRATE] = PD("RATE", F_LFOHZ, 0, 127, 60),
    [G_MFXCHAR] = PD("CHAR", F_PCT, 0, 127, 64),
    [G_MFX2TYPE] = PE("TYPE", N_FX, 0),
    [G_MFX2MIX] = PD("MIX", F_PCT, 0, 127, 127),
    [G_MFX2RATE] = PD("RATE", F_LFOHZ, 0, 127, 60),
    [G_MFX2CHAR] = PD("CHAR", F_PCT, 0, 127, 64),
    [G_MFX3TYPE] = PE("TYPE", N_FX, 0),
    [G_MFX3MIX] = PD("MIX", F_PCT, 0, 127, 127),
    [G_MFX3RATE] = PD("RATE", F_LFOHZ, 0, 127, 60),
    [G_MFX3CHAR] = PD("CHAR", F_PCT, 0, 127, 64),
    [G_FXRETURN] = PD("RETURN", F_PCT, 0, 127, 100),
    [G_FXMODE] = {"MODE", F_ENUM, 0, 1, 0, (const char *const []){"CHAIN", "SPLIT"}, 0},
    [G_BUDGET] = {"BUDGET", F_INT, 8, 32, VOICE_BUDGET_DEFAULT, 0, "pts"},
    [G_MFXTONE] = PD("TONE", F_TONE, -FX_TONE_MAX, FX_TONE_MAX, 0),
    [G_MFX2TONE] = PD("TONE", F_TONE, -FX_TONE_MAX, FX_TONE_MAX, 0),
    [G_MFX3TONE] = PD("TONE", F_TONE, -FX_TONE_MAX, FX_TONE_MAX, 0),

};

static const param_desc_t *global_desc(uint32_t id)
{
    if(param_master_fx(id) && (id-G_MFXTYPE)%FX_PARAMS>=2u){
        static param_desc_t d;static const char *const character[]={"CHAR","DEPTH","DEPTH","FOCUS","JITTER","BITE","REPEAT","DECAY","DEPTH","DEPTH"};
        uint32_t base=G_MFXTYPE+(id-G_MFXTYPE)/FX_PARAMS*FX_PARAMS,type=(uint32_t)clamp(song.g[base],0,NELEM(N_FX)-1);d=GP[id];
        if(id-base==3u)d.label=character[type];else if(type>=6u && type<=8u){d.label=type==6u?"TIME":"SIZE";d.fmt=F_PCT;}return &d;
    }
    return &GP[id];
}
static uint32_t scale_count(const track_t *t);          /* seq.c */
static const param_desc_t *track_desc(const track_t *t, uint32_t id)
{
    if (param_fx(id) && !param_fx_tone(id) && ((id-P_FXTYPE)%FX_PARAMS)>=2u) {
        static param_desc_t fx_desc;
        static const char *const character[] = {"CHAR", "DEPTH", "DEPTH", "FOCUS", "JITTER", "BITE", "REPEAT", "DECAY", "DEPTH", "DEPTH"};
        uint32_t base=P_FXTYPE+(id-P_FXTYPE)/FX_PARAMS*FX_PARAMS;
        fx_desc=TP[id];uint32_t type=(uint32_t)clamp(t->p[base],0,NELEM(N_FX)-1);
        if(id-base==3u)fx_desc.label=character[type];
        else if(type>=6u && type<=8u){fx_desc.label=type==6u?"TIME":"SIZE";fx_desc.fmt=F_PCT;}
        return &fx_desc;
    }
    if (t->eng_req == 2u && id >= P_FM1_ATK && id <= P_FM4_LEVEL)
        return &CZ_ED[id - P_FM1_ATK];
    if (id == P_MPCDEG) {                             /* a degree of the track's scale */
        static param_desc_t degree;
        degree = TP[P_MPCDEG];
        degree.max = (int16_t)scale_count(t);
        return &degree;
    }
    if (id >= P_E0 && id <= P_E7) {                   /* the engine asked for (t->engine follows after a fade) */
        const engine_t *e = ENGINES[eng_idx(t->eng_req)];
        const param_desc_t *d = e->desc ? e->desc(t, id - P_E0) : 0;   /* a mode-dependent label / names */
        return d ? d : &e->edit[id - P_E0];
    }
    return &TP[id];
}

/* the descriptor of parameter id for engine e without the engine's desc hook (the device display
 * only; range and default are the same): what the editor protocol, user presets and projects use */
static const param_desc_t *param_desc_of(uint32_t e, uint32_t id)
{
    if (e == 2u && id >= P_FM1_ATK && id <= P_FM4_LEVEL)
        return &CZ_ED[id - P_FM1_ATK];
    return id >= P_E0 ? &ENGINES[e]->edit[id - P_E0] : &TP[id];
}

/* a retired F_ENUM value kept as an alias, so stored values stay valid: SAMPLE SET and GRAIN SRC 1, once
 * TRANH, play PIANO (tools/gen_samples.py SMP_SET_ORIG). It shows the original's name; knobs step over it
 * and the editor's SET lands on the original. -> the value v stands for */
static int32_t enum_orig(const param_desc_t *d, int32_t v)
{
    return d->fmt==F_ENUM && d->names && !d->names[v][0] ? 0 : v;
}

/* a knob moved an F_ENUM from `from` to v: past any alias in that direction (back to `from` at the end) */
static int32_t enum_step(const param_desc_t *d, int32_t from, int32_t v)
{
    int32_t dir = v > from ? 1 : -1;
    while (v != from && enum_orig(d, v) != v)
        v = v + dir > d->max || v + dir < d->min ? from : v + dir;
    return v;
}

static uint8_t fmt_named;        /* the last param_format was a name (F_ENUM, even "4"): ui_draw.c's digits do not roll it */

/* value string (<= 5 chars) and unit for a parameter value */
static void param_format(const param_desc_t *d, int32_t v, char *val, const char **unit)
{
    *unit = "";
    fmt_named = d->fmt == F_ENUM;
    if (d == &GP[G_SLOT] && v == PROJ_TMPL) {
        str_cpy(val, "TMPL", 8);
        fmt_named = 1;
        return;
    }
    switch (d->fmt) {
    case F_PCT:                                     /* a 0..100 range (SWG) is its value, others a share of 127 */
        fmt_int(val, d->max == 100 ? v : (v * 100 + 63) / 127);
        *unit = "%";
        break;
    case F_BIPCT:
        fmt_int(val, v * 100 / 64);
        if (v > 0) {
            char t[8];
            fmt_int(t, v * 100 / 64);
            val[0] = '+';
            str_cpy(val + 1, t, 6);
        }
        *unit = "%";
        break;
    case F_TIME: {
        uint32_t ms10 = TIME_MS_X10[v & 127];
        if (ms10 < 100u) {
            fmt_fix(val, (int32_t)ms10, 1);
            *unit = "ms";
        } else if (ms10 < 10000u) {
            fmt_int(val, (int32_t)((ms10 + 5u) / 10u));
            *unit = "ms";
        } else {
            fmt_fix(val, (int32_t)(ms10 / 100u), 2);
            if (ms10 >= 100000u)
                fmt_fix(val, (int32_t)(ms10 / 1000u), 1);
            *unit = "s";
        }
        break;
    }
    case F_LFOHZ: {
        uint32_t h = LFO_HZ_X100[v & 127];
        if (h < 1000u)
            fmt_fix(val, (int32_t)h, 2);
        else
            fmt_fix(val, (int32_t)(h / 10u), 1);
        *unit = "Hz";
        break;
    }
    case F_TONE:                                      /* (the cut's frequency: the FX picture's strip) */
        str_cpy(val, v < 0 ? "DARK" : v > 0 ? "THIN" : "FULL", 6);
        break;
    case F_CUTOFF: {
        uint32_t h = CUTOFF_HZ[v & 127];
        if (h < 1000u) {
            fmt_int(val, (int32_t)h);
            *unit = "Hz";
        } else {
            fmt_fix(val, (int32_t)(h / 100u), 1);
            *unit = "kHz";
        }
        break;
    }
    case F_DB:
        if (v <= 0) {
            str_cpy(val, "OFF", 6);
        } else {
            fmt_fix(val, LEVEL_DB_X10[v], 1);
            *unit = "dB";
        }
        break;
    case F_SEMI:
        fmt_int(val, v);
        if (v > 0) {
            char t[8];
            fmt_int(t, v);
            val[0] = '+';
            str_cpy(val + 1, t, 6);
        }
        *unit = "st";
        break;
    case F_ENUM:
        if (d->names == N_ROUTE) {
            v = clamp(v, d->min, d->max);
            /* The panel's compact card fits "13-16"; editor/picker names retain the CH prefix. */
            str_cpy(val, d->names[v] + (v == 1 ? 0 : 2), 6);
        } else
            str_cpy(val, d->names==N_ENGNAME && v==ENGI_PROPHET ? "P5" : d->names[v < d->min ? d->min : v > d->max ? d->max : v], 6);
        if (d->unit)
            *unit = d->unit;
        break;
    case F_BPM:
        fmt_int(val, v);
        *unit = "BPM";
        break;
    case F_NOTE:
        str_cpy(val, N_NOTE[v % 12], 6);
        break;
    case F_ONOFF:
        str_cpy(val, N_ONOFF[v ? 1 : 0], 6);
        break;
    case F_STEPS:
        fmt_int(val, v);
        *unit = "STEP";
        break;
    case F_OFS: {                                     /* FM6: 0 at the middle of the range (DTN -7..7, TRNS +-24) */
        int32_t o = v - (d->min + d->max) / 2;
        if (o > 0) {
            val[0] = '+';
            fmt_int(val + 1, o);
        } else {
            fmt_int(val, o);
        }
        if (d->unit)
            *unit = d->unit;
        break;
    }
    case F_FMNOTE:                                    /* the DX7 break point: 0 = A-1, 39 = C3 (MIDI 60) */
        str_cpy(val, N_NOTE[(v + 21) % 12], 6);
        fmt_int(val + str_len(val), (v + 21) / 12 - 1);
        break;
    case F_FMFRQ:                                     /* an operator's frequency: CRS its step, FINE the result */
        fm6_freq_text(val, unit, d == &FM6_OPD[FP_FC]);
        break;
    default:
        if (d->names) {                               /* F_INT with a 0-terminated name list: the range */
            uint32_t k = 0;                           /* split evenly over the names (engine desc hooks) */
            while (d->names[k])
                k++;
            str_cpy(val, d->names[(uint32_t)(clamp(v, d->min, d->max) - d->min) * k / (uint32_t)(d->max - d->min + 1)], 6);
        } else {
            fmt_int(val, v);
        }
        if (d->unit)
            *unit = d->unit;
        break;
    }
}

/* ------------------------------------------------------------ pages --- */
enum { FAM_HOME, FAM_ENV, FAM_LFO, FAM_SCL, FAM_EDIT, FAM_GLO, FAM_SAVE, FAM_ARP, FAM_SEQ, FAM_TRK,
       FAM_FX, FAM_COUNT };
enum { SC_TRACK, SC_GLOBAL, SC_ENGINE, SC_STEP, SC_TRK,   /* SC_TRK: the TRACKS page (ui_input.c tracks_edit) */
       SC_FM6, SC_FMOP, SC_CZ, SC_CZ1, SC_P5, SC_P5STORE, SC_DRUM, SC_DRUMHIT };                         /* FM6: its patch, functions; operator fm6_opsel;
                                                                  * CZ1: a native CZ-1 tone (cz_edit.h) */
enum { GR_NONE, GR_ADSR, GR_LFO, GR_STEPS, GR_ARP, GR_SCALE, GR_ROLL, GR_BROWSE, GR_SLOTS, GR_USER, GR_TRK,
       GR_MOD, GR_PATS, GR_SONG, GR_MOTION, GR_CHORD, GR_SLICES,
       GR_FMEG, GR_FMPEG, GR_FMSTORE, GR_CZTOOLS, GR_SCALE_PICKER, GR_DRUMHIT, GR_PATGRID,
       GR_FXRACK }; /* NOTES shares GR_ROLL for steps and recorded events */

typedef struct {
    const char *title;
    uint8_t fam, scope, graph;
    uint8_t id[4];               /* param ids; 0xFF = empty slot */
} page_t;

#include "cz_edit.h"
#include "prophet_edit.h"
#define CZ_ENV_PAGES(t, l, e) \
    {t " R1-4", FAM_EDIT, SC_CZ1, GR_NONE, {LCZ_EBASE(l, e), LCZ_EBASE(l, e) + 1, LCZ_EBASE(l, e) + 2, LCZ_EBASE(l, e) + 3}}, \
    {t " R5-8", FAM_EDIT, SC_CZ1, GR_NONE, {LCZ_EBASE(l, e) + 4, LCZ_EBASE(l, e) + 5, LCZ_EBASE(l, e) + 6, LCZ_EBASE(l, e) + 7}}, \
    {t " L1-4", FAM_EDIT, SC_CZ1, GR_NONE, {LCZ_EBASE(l, e) + 8, LCZ_EBASE(l, e) + 9, LCZ_EBASE(l, e) + 10, LCZ_EBASE(l, e) + 11}}, \
    {t " L5-8", FAM_EDIT, SC_CZ1, GR_NONE, {LCZ_EBASE(l, e) + 12, LCZ_EBASE(l, e) + 13, LCZ_EBASE(l, e) + 14, LCZ_EBASE(l, e) + 15}}, \
    {t " POINT", FAM_EDIT, SC_CZ1, GR_NONE, {LCZ_EBASE(l, e) + 16, LCZ_EBASE(l, e) + 17, 0xFF, 0xFF}}

#define G_TRK_RECQ 0xFEu                                 /* a global page's column that is the selected track's P_RECQ */
static const page_t PAGES[] = {
    /* FX (docs/design README "FX rack"): the track's rack, its three slots (shown while they hold an effect), the
     * shared rack, its three slots. A rack: KNOB 2 the row (fx_rack_row), 1 its effect, 4 its amount, SHARED's 3 ROUTE
     * (page_desc) */
    {"FX", FAM_FX, SC_TRACK, GR_FXRACK, {P_FXTYPE, 0xFF, 0xFF, P_FXMIX}},
    {"FX 1", FAM_FX, SC_TRACK, GR_NONE, {P_FXMIX, P_FXRATE, P_FXCHAR, P_FXTONE}},
    {"FX 2", FAM_FX, SC_TRACK, GR_NONE, {P_FX2MIX, P_FX2RATE, P_FX2CHAR, P_FX2TONE}},
    {"FX 3", FAM_FX, SC_TRACK, GR_NONE, {P_FX3MIX, P_FX3RATE, P_FX3CHAR, P_FX3TONE}},
    {"SHARED", FAM_FX, SC_GLOBAL, GR_FXRACK, {G_MFXTYPE, 0xFF, 0xFF, G_MFXMIX}},
    {"SEND FX 1", FAM_FX, SC_GLOBAL, GR_NONE, {G_MFXMIX, G_MFXRATE, G_MFXCHAR, G_MFXTONE}},
    {"SEND FX 2", FAM_FX, SC_GLOBAL, GR_NONE, {G_MFX2MIX, G_MFX2RATE, G_MFX2CHAR, G_MFX2TONE}},
    {"SEND FX 3", FAM_FX, SC_GLOBAL, GR_NONE, {G_MFX3MIX, G_MFX3RATE, G_MFX3CHAR, G_MFX3TONE}},
    {"ENV", FAM_ENV, SC_TRACK, GR_ADSR, {P_ATK, P_DEC, P_SUS, P_REL}},
    {"ENV DEST", FAM_ENV, SC_TRACK, GR_NONE, {P_ED_FLT, P_ED_PIT, P_ED_SHP, 0xFF}},   /* the former fourth slot: SEQ > TEMPO's QNT */
    {"LFO", FAM_LFO, SC_TRACK, GR_LFO, {P_LRATE, P_LWAVE, P_LPHASE, P_LFADE}},
    {"LFO 2", FAM_LFO, SC_TRACK, GR_LFO, {P_LSYNC, P_LTRIG, P_LPOL, 0xFF}},
    {"LFO DEST", FAM_LFO, SC_TRACK, GR_NONE, {P_LD_PIT, P_LD_FLT, P_LD_SHP, P_LD_AMP}},
    {"MOD", FAM_LFO, SC_TRACK, GR_MOD, {P_M1SRC, 0xFF, P_M1DST, P_M1AMT}},   /* KNOB 2: the route (mod_ui_slot) */
    {"SCALES", FAM_SCL, SC_TRACK, GR_SCALE_PICKER, {0xFF, P_SCALE, P_ROOT, P_QUANT}},
    {"SCL", FAM_SCL, SC_TRACK, GR_SCALE, {P_ROOT, 0xFF, P_QUANT, P_TRANS}}, /* K2: scale favorite */
    {"CHORD", FAM_SCL, SC_TRACK, GR_CHORD, {P_CHRD, P_VOIC, 0xFF, 0xFF}},   /* SCL again: the chord keys (chord.c) */
    {"MPC", FAM_SCL, SC_TRACK, GR_NONE, {P_MPCDEG, 0xFF, 0xFF, 0xFF}},   /* the degree of pad H02: a row of SCL's list
                                                                          * (QNT MPC only), not a page of its own */
    {"EDIT 1", FAM_EDIT, SC_ENGINE, GR_NONE, {P_E0, P_E1, P_E2, P_E3}},
    {"EDIT 2", FAM_EDIT, SC_ENGINE, GR_NONE, {P_E4, P_E5, P_E6, P_E7}},
    {"DRUM SOUND",FAM_EDIT,SC_DRUM,GR_NONE,{0,1,2,3}},
    {"DRUM MIX",FAM_EDIT,SC_DRUM,GR_NONE,{0,4,255,255}},
    /* FM6 only (page_visible): the patch as the DX7 has it, its operators (PRESETS picks one), the functions */
    {"P5 STORE",FAM_EDIT,SC_P5STORE,GR_NONE,{0,1,2,3}},
    {"P5 OSC A",FAM_EDIT,SC_P5,GR_NONE,{0,3,4,8}},
    {"P5 OSC B",FAM_EDIT,SC_P5,GR_NONE,{1,2,9,12}},
    {"P5 B WAVES",FAM_EDIT,SC_P5,GR_NONE,{5,6,7,11}},
    {"P5 MIXER",FAM_EDIT,SC_P5,GR_NONE,{14,15,16,10}},
    {"P5 FILTER",FAM_EDIT,SC_P5,GR_NONE,{17,18,19,20}},
    {"P5 FLT ENV",FAM_EDIT,SC_P5,GR_NONE,{43,45,47,49}},
    {"P5 AMP ENV",FAM_EDIT,SC_P5,GR_NONE,{44,46,48,50}},
    {"P5 ENV MOD",FAM_EDIT,SC_P5,GR_NONE,{40,41,42,51}},
    {"P5 POLYMOD",FAM_EDIT,SC_P5,GR_NONE,{32,33,34,35}},
    {"P5 MOD FLT",FAM_EDIT,SC_P5,GR_NONE,{36,37,38,39}},
    {"P5 LFO",FAM_EDIT,SC_P5,GR_NONE,{21,23,24,25}},
    {"P5 WHEEL",FAM_EDIT,SC_P5,GR_NONE,{22,26,27,28}},
    {"P5 WHEEL 2",FAM_EDIT,SC_P5,GR_NONE,{29,30,31,13}},
    {"P5 UNISON",FAM_EDIT,SC_P5,GR_NONE,{52,53,54,87}},
    {"P5 BEND",FAM_EDIT,SC_P5,GR_NONE,{86,255,255,255}},
    {"STORE", FAM_EDIT, SC_FM6, GR_FMSTORE, {0, 1, 2, 3}},   /* SLOT STORE SEND INIT (ui_input.c act_do) */
    {"ALGO", FAM_EDIT, SC_FM6, GR_NONE, {FP_ALG, FP_FB, FP_OKS, FP_TRNSP}},
    {"FREQ", FAM_EDIT, SC_FMOP, GR_NONE, {FP_FC, FP_FF, FP_DET, FP_MODE}},
    {"OUT", FAM_EDIT, SC_FMOP, GR_NONE, {FP_OL, FP_KVS, FP_AMS, FP_OP}},
    {"EG RATE", FAM_EDIT, SC_FMOP, GR_FMEG, {FP_R1, FP_R1 + 1, FP_R1 + 2, FP_R1 + 3}},
    {"EG LVL", FAM_EDIT, SC_FMOP, GR_FMEG, {FP_L1, FP_L1 + 1, FP_L1 + 2, FP_L1 + 3}},
    {"SCALE", FAM_EDIT, SC_FMOP, GR_NONE, {FP_BP, FP_LD, FP_RD, FP_RS}},
    {"CURVE", FAM_EDIT, SC_FMOP, GR_NONE, {FP_LC, FP_RC, 0xFF, 0xFF}},
    {"PITCH EG", FAM_EDIT, SC_FM6, GR_FMPEG, {FP_PR1, FP_PR1 + 1, FP_PR1 + 2, FP_PR1 + 3}},
    {"PITCH LV", FAM_EDIT, SC_FM6, GR_FMPEG, {FP_PL1, FP_PL1 + 1, FP_PL1 + 2, FP_PL1 + 3}},
    {"FM LFO", FAM_EDIT, SC_FM6, GR_NONE, {FP_LFW, FP_LFS, FP_LFD, FP_LKS}},
    {"FM LFO 2", FAM_EDIT, SC_FM6, GR_NONE, {FP_LPMD, FP_LAMD, FP_LPMS, 0xFF}},
    /* the FM6 function settings (a DX7's function mode, the track's own: saved with the project) */
    {"FM BEND", FAM_EDIT, SC_FM6, GR_NONE, {FP_SIZE + FN_PBUP, FP_SIZE + FN_PBDN, FP_SIZE + FN_PBSTEP, FP_SIZE + FN_VNORM}},
    {"FM PORTA", FAM_EDIT, SC_FM6, GR_NONE, {FP_SIZE + FN_PMODE, FP_SIZE + FN_PTIME, FP_SIZE + FN_GLISS, FP_SIZE + FN_ENGINE}},
    {"FM WH/FT", FAM_EDIT, SC_FM6, GR_NONE, {FP_SIZE + FN_MWR, FP_SIZE + FN_MWA, FP_SIZE + FN_FCR, FP_SIZE + FN_FCA}},
    {"FM BR/AT", FAM_EDIT, SC_FM6, GR_NONE, {FP_SIZE + FN_BCR, FP_SIZE + FN_BCA, FP_SIZE + FN_ATR, FP_SIZE + FN_ATA}},
    /* CZ-1 only (page_visible): the native tone, every panel value (cz_edit.h) */
    {"CZ TOOLS", FAM_EDIT, SC_CZ1, GR_CZTOOLS, {0, 1, 2, 3}},   /* NAME 1>2 2>1 COMP (ui_input.c act_do) */
    {"CZ LINE", FAM_EDIT, SC_CZ1, GR_NONE, {LCZ_LINE, LCZ_MOD, LCZ_OCT, 0xFF}},
    {"CZ DETUNE", FAM_EDIT, SC_CZ1, GR_NONE, {LCZ_SIGN, LCZ_DOCT, LCZ_NOTE, LCZ_FINE}},
    {"CZ VIBRATO", FAM_EDIT, SC_CZ1, GR_NONE, {LCZ_VWAVE, LCZ_VRATE, LCZ_VDEP, LCZ_VDELAY}},
    {"CZ WINDOW", FAM_EDIT, SC_CZ1, GR_NONE, {LCZ_WIN(0), LCZ_WIN(1), 0xFF, 0xFF}},
    {"CZ1 WAVE", FAM_EDIT, SC_CZ1, GR_NONE, {LCZ_LBASE(0) + LCZ_W1, LCZ_LBASE(0) + LCZ_W2, LCZ_LBASE(0) + LCZ_KW, LCZ_LBASE(0) + LCZ_KA}},
    {"CZ1 TOUCH", FAM_EDIT, SC_CZ1, GR_NONE, {LCZ_LBASE(0) + LCZ_LEVEL, LCZ_LBASE(0) + LCZ_VP, LCZ_LBASE(0) + LCZ_VW, LCZ_LBASE(0) + LCZ_VA}},
    CZ_ENV_PAGES("C1 PIT", 0, 0), CZ_ENV_PAGES("C1 WAV", 0, 1), CZ_ENV_PAGES("C1 AMP", 0, 2),
    {"CZ2 WAVE", FAM_EDIT, SC_CZ1, GR_NONE, {LCZ_LBASE(1) + LCZ_W1, LCZ_LBASE(1) + LCZ_W2, LCZ_LBASE(1) + LCZ_KW, LCZ_LBASE(1) + LCZ_KA}},
    {"CZ2 TOUCH", FAM_EDIT, SC_CZ1, GR_NONE, {LCZ_LBASE(1) + LCZ_LEVEL, LCZ_LBASE(1) + LCZ_VP, LCZ_LBASE(1) + LCZ_VW, LCZ_LBASE(1) + LCZ_VA}},
    CZ_ENV_PAGES("C2 PIT", 1, 0), CZ_ENV_PAGES("C2 WAV", 1, 1), CZ_ENV_PAGES("C2 AMP", 1, 2),
    /* PHASE: the retired FM4 storage fields hold split CZ envelopes. */
    {"DCW1 ENV", FAM_EDIT, SC_CZ, GR_NONE, {P_FM1_ATK, P_FM1_DEC, P_FM1_SUS, P_FM1_REL}},
    {"DCW2 ENV", FAM_EDIT, SC_CZ, GR_NONE, {P_FM2_ATK, P_FM2_DEC, P_FM2_SUS, P_FM2_REL}},
    {"DCA2 ENV", FAM_EDIT, SC_CZ, GR_NONE, {P_FM3_ATK, P_FM3_DEC, P_FM3_SUS, P_FM3_REL}},
    {"DCO ENV", FAM_EDIT, SC_CZ, GR_NONE, {P_FM4_ATK, P_FM4_DEC, P_FM4_SUS, P_FM4_REL}},
    {"CZ LEVEL", FAM_EDIT, SC_CZ, GR_NONE, {P_FM1_LEVEL, P_FM2_LEVEL, P_FM3_LEVEL, P_FM4_LEVEL}},
    {"OP1 ENV", FAM_EDIT, SC_TRACK, GR_ADSR, {P_FM1_ATK, P_FM1_DEC, P_FM1_SUS, P_FM1_REL}},
    {"OP2 ENV", FAM_EDIT, SC_TRACK, GR_ADSR, {P_FM2_ATK, P_FM2_DEC, P_FM2_SUS, P_FM2_REL}},
    {"OP3 ENV", FAM_EDIT, SC_TRACK, GR_ADSR, {P_FM3_ATK, P_FM3_DEC, P_FM3_SUS, P_FM3_REL}},
    {"OP4 ENV", FAM_EDIT, SC_TRACK, GR_ADSR, {P_FM4_ATK, P_FM4_DEC, P_FM4_SUS, P_FM4_REL}},
    {"OP LEVEL", FAM_EDIT, SC_TRACK, GR_NONE, {P_FM1_LEVEL, P_FM2_LEVEL, P_FM3_LEVEL, P_FM4_LEVEL}},
    {"LANES", FAM_EDIT, SC_TRACK, GR_NONE, {P_LN0, P_LN1, P_LN2, P_LN3}},
    {"LANES 2", FAM_EDIT, SC_TRACK, GR_NONE, {P_LN4, P_LN5, P_LN6, P_LN7}},
    {"VOICE", FAM_EDIT, SC_TRACK, GR_NONE, {P_VOICE, P_GLIDE, P_GLMODE, P_PRIO}},
    {"VOICE 2", FAM_EDIT, SC_TRACK, GR_NONE, {P_ALLOC, P_DETUNE, P_PAN, P_MUTE}},
    {"VOICE 3", FAM_EDIT, SC_TRACK, GR_NONE, {P_SPRD, 0xFF, 0xFF, 0xFF}},
    {"GLOBAL", FAM_GLO, SC_GLOBAL, GR_NONE, {G_A4, G_BUDGET, G_CLOCK, G_TUNE}}, /* device reference/budget; CLK/TUNE kept */
    {"SYSTEM", FAM_GLO, SC_GLOBAL, GR_NONE, {G_MIDI, G_DRUMCH, G_ROUTE, G_INFO}},
    {"PRESETS", FAM_SAVE, SC_GLOBAL, GR_BROWSE, {0xFF, 0xFF, 0xFF, 0xFF}},   /* browser: KNOB 1 LIST, 2 / PRESETS the sounds */
    {"USER", FAM_SAVE, SC_GLOBAL, GR_USER, {0xFF, 0xFF, 0xFF, 0xFF}},       /* user presets: SLOT LOAD ERASE SAVE */
    {"PROJECT", FAM_SAVE, SC_GLOBAL, GR_SLOTS, {G_SLOT, G_BOOT, G_LOAD, G_SAVE}},
    {"ARP", FAM_ARP, SC_TRACK, GR_ARP, {P_AMODE, P_ARATE, P_AOCT, P_AGATE}},
    {"ARP 2", FAM_ARP, SC_TRACK, GR_NONE, {P_ASWING, P_APROB, P_AHOLD, P_AORDER}},
    {"NOTES", FAM_SEQ, SC_STEP, GR_ROLL, {0, 1, 2, 3}},
    {"PATTERN", FAM_SEQ, SC_TRACK, GR_STEPS, {P_SLEN, P_SDIV, P_SSWING, P_SGATE}},
    {"TEMPO", FAM_SEQ, SC_GLOBAL, GR_NONE, {G_BPM, G_SWING, G_TRK_RECQ, 0xFF}},   /* the project's: saved with it; QNT: the
                                                                                  * selected track's (TIMING's, folded in) */
    {"MIXER", FAM_TRK, SC_TRK, GR_TRK, {0, 1, 2, 3}},   /* GLO button; LEVEL PAN MUTE */
    {"PATTERNS", FAM_SEQ, SC_GLOBAL, GR_PATGRID, {0xFF, 0xFF, 0xFF, 0xFF}},   /* the 4 x 8 grid: KNOB k track k's (ui_stage.c) */
    {"SONG", FAM_SEQ, SC_GLOBAL, GR_SONG, {0xFF, 0xFF, 0xFF, 0xFF}},
    {"MOTION", FAM_SEQ, SC_TRACK, GR_MOTION, {0xFF, 0xFF, 0xFF, 0xFF}},
    {"DRUM HIT",FAM_SEQ,SC_DRUMHIT,GR_DRUMHIT,{0,1,2,3}},
};
#define NPAGES (sizeof(PAGES) / sizeof(PAGES[0]))
static uint8_t mod_ui_slot;      /* the MOD page: the matrix slot (0..3) KNOB 2..4 edit */
static uint8_t fx_rack_row[2];   /* the racks' rows KNOB 2 picks: FX's (0..2 its slots, 3 the shared FX), SHARED's
                                  * (0..2 its slots, 3 RETURN) */

/* FM6's pages: the value in column slot (a copy in fm6_cell) and its descriptor; 0 = none (not an FM6 track) */
static const param_desc_t *fm6_page_desc(const page_t *pg, uint32_t slot, int16_t **valp)
{
    uint32_t id = pg->id[slot], tr = song.sel % NTRK;
    const uint8_t *v = fm6_patch[tr];
    const param_desc_t *d;
    *valp = 0;
    if (id == 0xFFu || TSEL->eng_req != ENGI_FM6)
        return 0;
    if (pg->graph == GR_FMSTORE) {
        fm6_cell[slot] = (int16_t)(slot ? 0 : fm6_bslot + 1u);
        d = &FM6_SD[slot & 3u];
    } else if (pg->scope == SC_FMOP) {
        uint32_t k = 5u - fm6_opsel % 6u;               /* the patch keeps the sixth operator first */
        fm6_cell[slot] = (int16_t)(id == FP_OP ? (fm6_on[tr] >> k) & 1u : v[k * FP_OP + id]);
        d = &FM6_OPD[id];
    } else if (id >= FP_SIZE) {
        fm6_cell[slot] = fm6_fn[tr][(id - FP_SIZE) % FM6_NFN];
        d = &FM6_FD[(id - FP_SIZE) % FM6_NFN];
    } else if (id >= FP_PR1 && id < FP_NAME) {
        fm6_cell[slot] = v[id];
        d = &FM6_GD[id - FP_PR1];
    } else {
        return 0;
    }
    *valp = &fm6_cell[slot];
    return d;
}

/* .. and a new value there, into the patch (an edit: the sounding notes follow), the switches or the functions */
static void fm6_page_put(const page_t *pg, uint32_t slot, int32_t val)
{
    uint32_t id = pg->id[slot], tr = song.sel % NTRK;
    uint8_t v[FP_SIZE + 1u];
    if (pg->graph == GR_FMSTORE) {
        if (!slot)
            fm6_bslot = (uint16_t)(val - 1);
        return;
    }
    memcpy(v, fm6_patch[tr], FP_SIZE);
    if (pg->scope == SC_FMOP) {
        uint32_t k = 5u - fm6_opsel % 6u;
        if (id == FP_OP) {
            fm6_on[tr] = (uint8_t)(val ? fm6_on[tr] | 1u << k : fm6_on[tr] & ~(1u << k));
            return;
        }
        v[k * FP_OP + id] = (uint8_t)val;
    } else if (id >= FP_SIZE) {
        fm6_fn_set(tr, (id - FP_SIZE) % FM6_NFN, val);   /* (the track's: saved with the project) */
        return;
    } else {
        v[id] = (uint8_t)val;
    }
    fm6_put_patch(tr, v, 0);
}

/* a rack's KNOB 1 (its row's effect) and KNOB 4 (the row's amount): FX's slots their MIX, its shared row the track's
 * SEND (CHAIN; in SPLIT the track's three sends are SHARED's rows); SHARED's slots their MIX (SPLIT: the track's send to
 * each), its last row RETURN. SHARED's KNOB 3: ROUTE (G_FXMODE) */
static const param_desc_t *fx_rack_desc(const page_t *pg, uint32_t slot, int16_t **valp)
{
    uint32_t shared = pg->scope == SC_GLOBAL, r = fx_rack_row[shared] & 3u, id;
    *valp = 0;
    if (slot == 0u && r < FX_SLOTS && !shared) {
        id = P_FXTYPE + r * FX_PARAMS;
        *valp = &TSEL->p[id];
        return track_desc(TSEL, id);
    }
    if (slot == 0u && r < FX_SLOTS) {                   /* (SHARED's effect: "FX" as the track's, not TYPE) */
        static param_desc_t type;
        id = G_MFXTYPE + r * FX_PARAMS;
        *valp = &song.g[id];
        type = *global_desc(id);
        type.label = "FX";
        return &type;
    }
    if (slot == 2u && shared) {                         /* SHARED's KNOB 3: the routing, CHAIN or SPLIT (its sheet too) */
        static param_desc_t route;
        *valp = &song.g[G_FXMODE];
        route = *global_desc(G_FXMODE);
        route.label = "ROUTE";
        return &route;
    }
    if (slot != 3u || (!shared && r == FX_SLOTS && song.g[G_FXMODE]))
        return 0;
    if (shared && r == FX_SLOTS) {
        *valp = &song.g[G_FXRETURN];
        return global_desc(G_FXRETURN);
    }
    if (shared && !song.g[G_FXMODE]) {
        *valp = &song.g[G_MFXMIX + r * FX_PARAMS];
        return global_desc(G_MFXMIX + r * FX_PARAMS);
    }
    id = shared ? P_FXSEND + r : r < FX_SLOTS ? P_FXMIX + r * FX_PARAMS : P_FXSEND;
    *valp = &TSEL->p[id];
    if (shared) {                                       /* (SPLIT: "SEND", the row says to which) */
        static param_desc_t send;
        send = *track_desc(TSEL, id);
        send.label = "SEND";
        return &send;
    }
    return track_desc(TSEL, id);
}
static const param_desc_t *page_desc(const page_t *pg, uint32_t slot, int16_t **valp)
{
    uint32_t id = pg->id[slot];
    if(pg->scope==SC_P5 || pg->scope==SC_P5STORE){
        if(id==255u || (pg->scope==SC_P5 && (id>=88u || !P5_PANEL[id].label))){*valp=0;return 0;}
        p5_cell[slot&3u]=pg->scope==SC_P5STORE?(slot?0:p5_store_slot):p5_patch_of(TSEL)->raw[id]+(id==P5_BEND?1:0);
        /* Sequential stores zero for the default five-voice unison count. */
        if(pg->scope==SC_P5 && id==P5_UNISON_COUNT && !p5_cell[slot&3u])p5_cell[slot&3u]=5;
        *valp=&p5_cell[slot&3u];
        if(pg->scope==SC_P5 && p5_keyed_tune(TSEL,id)){
            p5_cell[slot&3u]=(int16_t)(clamp(p5_patch_of(TSEL)->raw[id],0,96)/2-12);
            return &P5_TUNE_DISPLAY[id];
        }
        return pg->scope==SC_P5STORE?&P5_STORE[slot&3u]:id==P5_BEND?&P5_BEND_DISPLAY:&P5_PANEL[id];
    }
    if (pg->scope == SC_CZ1) {                           /* a copy of the tone's panel value (cz_ed_put writes it) */
        *valp = 0;
        if (id == 0xFFu || TSEL->eng_req != ENGI_CZ)
            return 0;
        if (pg->graph == GR_CZTOOLS) {
            cz_ed_cell[slot & 3u] = 0;
            *valp = &cz_ed_cell[slot & 3u];
            return &CZ_ACTIONS[slot & 3u];
        }
        return cz_ed_desc(song.sel % NTRK, id, slot, valp);
    }
    if (pg->scope == SC_FM6 || pg->scope == SC_FMOP)
        return fm6_page_desc(pg, slot, valp);
    if(pg->scope==SC_DRUM || pg->scope==SC_DRUMHIT){*valp=0;return 0;}
    if (pg->graph == GR_FXRACK)
        return fx_rack_desc(pg, slot, valp);
    if (id == 0xFFu) {
        *valp = 0;
        return 0;
    }
    if (pg->scope == SC_STEP || pg->scope == SC_TRK) {
        *valp = 0;
        return 0;
    }
    if (id == G_TRK_RECQ) {                          /* (TEMPO's QNT: the selected track's recording quantise) */
        *valp = &TSEL->p[P_RECQ];
        return track_desc(TSEL, P_RECQ);
    }
    if (pg->scope == SC_GLOBAL && id == G_BOOT) {
        boot_cell = settings_boot;                       /* (a copy: ui_input.c edit_param writes it back) */
        *valp = &boot_cell;
        return global_desc(id);
    }
    if (pg->scope == SC_GLOBAL && id == G_A4) {
        *valp = &tuning_a4;
        return global_desc(id);
    }
    if (pg->scope == SC_GLOBAL && id == G_BUDGET) {
        *valp = &settings_budget;
        return global_desc(id);
    }
    if (pg->scope == SC_GLOBAL && id == G_DRUMCH) {
        drumch_cell = settings_drumch;                   /* (as BOOT) */
        *valp = &drumch_cell;
        return global_desc(id);
    }
    if ((pg->scope == SC_GLOBAL && id >= G_MFXTONE && id <= G_MFX3TONE &&    /* TONE: ECHO, ROOM and REVERB only */
         !fx_has_tone(song.g[G_MFXTYPE + (id - G_MFXTONE) * FX_PARAMS])) ||
        (pg->scope == SC_TRACK && param_fx_tone(id) && !fx_has_tone(TSEL->p[P_FXTYPE + (id - P_FXTONE) * FX_PARAMS]))) {
        *valp = 0;
        return 0;
    }
    if (pg->scope == SC_GLOBAL) {
        *valp = &song.g[id];
        return global_desc(id);
    }
    if (pg->graph == GR_MOD)                          /* SRC DST AMT of the slot shown */
        id += 3u * mod_ui_slot;
    *valp = &TSEL->p[id];
    return track_desc(TSEL, id);
}
