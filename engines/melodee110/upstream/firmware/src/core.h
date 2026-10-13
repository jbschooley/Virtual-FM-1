/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* MELODEE core types: tracks, voices, engines, parameters.
 * Four tracks, each a synth part: its own engine, preset, parameters, voices and 64-step
 * pattern. The parts share one budget of VBUDGET units of sounding voices (voice.c). Drums are the DRUM
 * engine (synthesized 808 on the General MIDI map) on any part.
 * Sections: sizes, parameters, voices and engines, tracks and the song, system. */
#include <stdint.h>
#include "tuning.h"

/* ------------------------------------------------------------ sizes --- */
#define NVOICE 16                /* voice slots per part (FM6 plays Dexed's 16) */
#define NPOLY 8                  /* the voices of an engine without its own cap (its per-voice state: engines.c eng_state) */
#define VBUDGET 16               /* the sounding voices of all parts, in units: a voice of an engine capped above NPOLY
                                  * (FM6) takes one, any other two, so the others keep their shared eight */
#define NPART 4                  /* synth parts: tracks 1..4 */
#define NTRK NPART               /* tracks (the formats and the protocol count these): every track is a part */
#define NSTEP 64
#define NPAT 8u
#define HALF_FRAMES 128          /* I2S half buffer: 2.9 ms at 44.1 kHz (a key waits 0..1 half, then plays 1 half later) */
/* SLICE retired. Keep its stored engine number reserved. */
#ifdef MELODEE_SLICE
#undef MELODEE_SLICE
#endif
#define MELODEE_SLICE 0
#ifndef MELODEE_FM4
#define MELODEE_FM4 0            /* the DIGITAL engine (eng_digital.c, four-operator FM): kept in the tree, not built
                                  * by default; replaced by FM6, its sounds convert (fm4_convert.c) */
#endif
#ifndef P5_OVERSAMPLE
#define P5_OVERSAMPLE 1 /* prototype: benchmark base rate before enabling optional 2x */
#endif
#ifndef MELODEE_LEGACY_EXTRAS
#define MELODEE_LEGACY_EXTRAS 0 /* reference tests only: retired TRIO, WHEEL and PHYS; PHASE, VOICE and NOISE (retired
                                 * 2026-10-10: the flash for the redesign; CZ-1 keeps PHASE's phase-distortion core) */
#endif
#define MELODEE_PROPHET 1
#define ENGI_PROPHET 19u /* 16..18 reserved: historical preset namespaces */
#define USER_GENERAL 16u
#define USER_NATIVE_P5 20u
#define NENGINES 20               /* 13 SLICE and 14 OBXF reserved; 15 native CZ-1 */
#define USER_NATIVE_FM 17u
#define USER_NATIVE_CZ 18u
#define USER_NONE 256u
#define ENGI_DIGITAL 1u          /* reserved without MELODEE_FM4: never selectable (eng_ok), its sounds load as FM6 */
#define NENG_SHOWN (NENGINES - 7u - !MELODEE_FM4 - !MELODEE_SLICE - 6u * !MELODEE_LEGACY_EXTRAS)   /* the engines one can pick: PRESETS, the EDIT
                                                * layer, the editor, in the display order of engines.c ENGINE_ORDER */
#define ENGI_SLICE 13u           /* reserved without MELODEE_SLICE: never selectable (eng_ok) */
#ifdef SMP_USER_SLOTS
#undef SMP_USER_SLOTS
#endif
#define SMP_USER_SLOTS 0u        /* sample material and its upload slots are retired */
#define UP_SLOTS 64u             /* user presets (upreset.c) */
#define NELEM(a) (sizeof(a) / sizeof((a)[0]))

/* ------------------------------------------------------- parameters --- */
enum {
    F_INT, F_PCT, F_BIPCT, F_TIME, F_LFOHZ, F_CUTOFF, F_DB, F_SEMI, F_ENUM, F_BPM, F_NOTE,
    F_ONOFF, F_OCT, F_STEPS,
    F_OFS, F_FMNOTE, F_FMFRQ    /* FM6's pages: 0 at the middle of the range, the DX7 break point, an operator's frequency */
};

typedef struct {
    const char *label;
    uint8_t fmt;
    int16_t min, max, def;
    const char *const *names;   /* F_ENUM */
    const char *unit;           /* F_INT / F_ENUM optional unit */
} param_desc_t;

enum {                          /* per-track parameters */
    P_LEVEL,
    P_ATK, P_DEC, P_SUS, P_REL,
    P_ED_FLT, P_ED_PIT, P_ED_SHP, P_ED_FX,     /* P_ED_FX: reserved field now used by sequencer playback QNT */
    P_LRATE, P_LWAVE, P_LPHASE, P_LFADE,
    P_LD_PIT, P_LD_FLT, P_LD_SHP, P_LD_AMP,
    P_AMODE, P_ARATE, P_AOCT, P_AGATE,
    P_ASWING, P_APROB, P_AHOLD, P_AORDER,
    P_ROOT, P_SCALE, P_QUANT, P_TRANS,
    P_SLEN, P_SDIV, P_SSWING, P_SGATE,
    P_DIST, P_CHOR, P_DLY, P_REV,
    P_VOICE, P_GLIDE, P_PAN, P_MUTE,
    P_GLMODE, P_PRIO, P_ALLOC, P_DETUNE,
    P_SLCR, P_SLPAT, P_SLRATE, P_SLDEPTH,      /* SLICER insert (slicer.c) */
    P_M1SRC, P_M1DST, P_M1AMT,                 /* modulation matrix (mod.c): 4 slots of SRC, DST, AMT; */
    P_M2SRC, P_M2DST, P_M2AMT,                 /* new common parameters go just before P_E0 (user presets */
    P_M3SRC, P_M3DST, P_M3AMT,                 /* and projects map by count) */
    P_M4SRC, P_M4DST, P_M4AMT,
    P_FM1_ATK, P_FM1_DEC, P_FM1_SUS, P_FM1_REL, P_FM1_LEVEL,
    P_FM2_ATK, P_FM2_DEC, P_FM2_SUS, P_FM2_REL, P_FM2_LEVEL,
    P_FM3_ATK, P_FM3_DEC, P_FM3_SUS, P_FM3_REL, P_FM3_LEVEL,
    P_FM4_ATK, P_FM4_DEC, P_FM4_SUS, P_FM4_REL, P_FM4_LEVEL,
    P_CHRD, P_VOIC,                            /* chord keys (chord.c): one key plays a chord; its voicing */
    P_MPCDEG,                                  /* QNT MPC: the scale degree MPC pad H02 plays, 1 = the root */
    P_LN0, P_LN1, P_LN2, P_LN3, P_LN4, P_LN5, P_LN6, P_LN7, /* DRUM lane levels, 127 = original gain */
    P_LSYNC, P_LTRIG, P_LPOL, P_SPRD, /* Felucca 1.4 common LFO controls and stereo spread */
    P_E0, P_E1, P_E2, P_E3, P_E4, P_E5, P_E6, P_E7,
    P_COUNT
};

#define P_RECQ P_ED_FX          /* reuse an inert parameter; engine and scale ids stay fixed */

enum {                          /* global parameters */
    G_BPM, G_SWING, G_CLOCK, G_TUNE,
    G_DTIME, G_DFDBK, G_DCOLOR, G_DMIX,
    G_RSIZE, G_RDAMP, G_CRATE, G_CDEPTH,
    G_MIDI, G_DRUMCH, G_ROUTE, G_INFO, /* G_ROUTE: MIDI IN, 0 CH1-4 (channels 1..4 -> parts 1..4), 1 SEL (seq.c);
                                        * G_DRUMCH (was G_SYNC, unused): a device setting (settings_drumch) */
    G_SLOT, G_BOOT, G_LOAD, G_SAVE,   /* G_BOOT (was G_NAME, unused): a device setting (settings_boot), not song.g */
    G_ENGSEL, G_A4,            /* the editor switches the engine with a SET of G_ENGSEL; G_A4 reuses the unused
                                 * G_ENGGO id, reads tuning_a4 instead of song.g (ids/count stay fixed) */
    G_CLRSEQ, G_INITSND,
    G_RTYPE,                    /* REVERB TYPE: 0 ROOM, 1 SPRING, 2 HALL (fx.c). Was G_DRCH, the GM drum part's MIDI
                                 * channel (inert since 1.0, never read); projects of formats before FUN7 load it
                                 * as ROOM (project.c proj_rtype_room) */
    G_DTYPE, G_DWEAR,           /* DELAY TYPE (DIGI, TAPE, DG-PP, TP-PP) and WEAR (delay.c, x0x's tape delay).
                                 * Were G_DRLVL / G_DRREV, the GM drum part's level and reverb send (inert since
                                 * 1.0): ids and G_COUNT are fixed by the formats and the protocol; a project from
                                 * before 1.0 still gives them to its drum part (project.c proj_drums_to_part), any
                                 * project from before the delay came back their defaults (proj_delay_off) */
    G_COUNT
};
#define G_DRLVL G_DTYPE         /* (their meaning in a project from before 1.0: proj_drums_to_part) */
#define G_DRREV G_DWEAR

/* stored parameters of an older layout -> today's P_* order. A store keeps np = the P_COUNT it was
 * written with; common parameters are only ever added just before P_E0, so the first np - 8 are
 * P_LEVEL.. in order and the last 8 are P_E0..P_E7; the parameters a store does not have take def[]
 * (user presets, projects of formats 1 and 2) */
static void params_by_count(int16_t *out, const int16_t *in, uint32_t np, const int16_t *def)
{
    uint32_t i, nc = np - 8u;
    for (i = 0; i < P_E0; i++)
        out[i] = i < nc ? in[i] : def[i];
    for (i = 0; i < 8u; i++)
        out[P_E0 + i] = in[nc + i];
}

/* engine indices the stores name (engines.c ENGINES[]: append-only) */
#define ENGI_PHYS 9u
#define ENGI_DRUM 10u
/* PHYS MODEL DRUM (MODEL 4, before 1.0) -> the DRUM engine, its E values in place: {MODEL, TUNE, TONE, DECY,
 * SNAP, ACC, KICK 0..127, PERC 0..127} -> {KIT, TUNE, TONE, DECY, SNAP, ACC, KICK 0..1, DRV 0}. 1 = it was one
 * (its engine is ENGI_DRUM now); projects (project.c) and user presets (upreset.c) */
static int drum_from_phys(uint32_t engine, int16_t *e)
{
    if (engine != ENGI_PHYS || e[0] != 4)
        return 0;
    e[0] = 4; /* Retired custom drum variants now play the synthesized 808. */
    e[6] = (int16_t)(e[6] >= 64);                                       /* KICK: PUNCH, ROUND */
    e[7] = 0;
    return 1;
}

/* ------------------------------------------------- voices, engines --- */
enum { V_POLY, V_MONO, V_LEGATO, V_UNISON };   /* P_VOICE */
enum { Q_OFF, Q_SNAP, Q_WHITE, Q_ALL, Q_MPC };  /* P_QUANT (seq.c kb_map, midi_map); 1 = SNAP, the former ON */
typedef struct {
    uint8_t side; /* stereo spread side, retained while sounding */
    uint8_t note, vel, gate, active;
    uint8_t stage;               /* env: 0 off, 1 attack, 2 decay/sustain, 3 release */
    int32_t env;                 /* Q24 */
    int32_t env_out;             /* last control-rate amplitude, Q15 */
    int32_t pitch16, pitch_cur;  /* 1/16 semitone, with glide */
    int32_t gstep;               /* glide TIME mode: 1/16 st per control tick, 0 = RATE mode */
    int32_t fine;                /* unison detune: phase increment * (1 + fine / 4096) */
    int32_t scale_fine;          /* note-on snapshot: scale pitch below 1/16 semitone */
    uint32_t ph[3];
    int32_t s[8];                /* engine state (filters, envs) */
    uint32_t age;
    uint8_t mvel;                /* mod.c: the note-on velocity (before UNISON scaling) and RAND of the note */
    int16_t mrnd;
} voice_t;

typedef struct {                 /* per-voice control-rate modulation, computed in voice.c */
    uint32_t inc;                /* phase increment of the base pitch */
    int32_t pitch16;
    int32_t amp0, amp1;          /* Q15 ramp over the block */
    int32_t cutoff;              /* 0..127 << 8 */
    int32_t shape;               /* 0..127 << 8 */
    int32_t envq15;              /* env value (for engines that use it as a mod source) */
    int32_t fine;                /* the residual below 1/16 semitone in inc: unison detune, TUNE, bend (1/4096) */
    int32_t plog;                /* the voice's own pitch offset without TUNE and bend (glide, LFO and ENV pitch, the
                                  * matrix's, unison detune), Q24 octaves: for engines that figure their pitch (FM6) */
} vmod_t;

typedef struct {
    const char *name;
    int8_t e[8];                 /* P_E0..P_E7 (signed: an interval below the note; every value fits) */
    uint8_t env[4];              /* ATK DEC SUS REL */
    int8_t fenv;                 /* ENV -> FILTER amount (-64..63) */
    uint8_t mono;                /* 1 = MONO (bass / lead), 0 = POLY */
    /* the rest of the patch; each value is stored + 1, 0 = the default */
    uint8_t fx[4];               /* DIST, CHORUS, DELAY, REVERB sends */
                                 /* (a sound load never touches the steps; the arp is the track's: a preset does
                                  * not set it) */
} preset_t;
#define FX(d, c, dl, r) .fx = {(d) + 1, (c) + 1, (dl) + 1, (r) + 1}

struct track;
typedef struct {                 /* an engine (engines.c ENGINES[]; the eng_*.c files) */
    const char *name;            /* "ANALOG" (PRESETS, the editor) */
    const char *page_title[2];   /* EDIT 1 and EDIT 2 */
    param_desc_t edit[8];        /* P_E0..P_E7 */
    const preset_t *presets;
    uint8_t npresets;
    uint8_t knob[4];             /* HOME: the four parameters on KNOB 1..4 */
    uint8_t poly;                /* voice cap for POLY and UNISON (up to NVOICE), 0 = NPOLY */
    uint8_t sampled;             /* 1 = plays recorded material (a position, not a phase): voice.c keeps
                                  * no phases over a retrigger, spreads none for UNISON, renders at SUS 0 */
    uint8_t keep;                /* bit k: s[k] is kept when a sounding voice is retriggered (filters) */
    uint8_t oneshot;             /* 1 = hits (DRUM): always POLY, no glide; amp ends its voices (eng_drum.c) */
    void (*note_on)(struct track *t, voice_t *v);
    void (*render)(struct track *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m);
    /* optional (0 = none): the voice amplitude instead of the ADSR curve, once per control tick;
     * gets the ADSR value (Q15, env_tick already ran: it still gates the voice), returns Q15 */
    int32_t (*amp)(struct track *t, voice_t *v, int32_t adsr);
    /* optional: a mode-dependent descriptor of EDIT k (the same range and default as edit[k],
     * another label / value names), 0 = edit[k] */
    const param_desc_t *(*desc)(const struct track *t, uint32_t k);
    /* optional: once per block and part, before its voices (also with no voice sounding) */
    void (*block)(struct track *t);
    /* optional: the note of key k (0 = the lowest) when the engine maps the keys itself, else -1
     * (then the scale keyboard of seq.c kb_map) */
    int32_t (*keys)(const struct track *t, uint32_t k);
    /* 1 = the engine's own envelopes are the voice's amplitude (FM6): voice.c renders it at 1.0 (no ADSR, no
     * velocity; LFO -> AMP, the matrix's AMP and the fades still apply; ENV is 0) and the voice ends when
     * done() says so (once per control tick, before the render), not at the end of the ADSR's release */
    uint8_t ownenv;
    int (*done)(struct track *t, voice_t *v);
    /* optional: the POLY voice (0..cap-1) for a new note, the engine's own choice (FM6: Dexed's) */
    uint32_t (*alloc)(struct track *t, uint32_t note);
    /* optional: MONO / LEGATO / UNISON moved a sounding voice to a new note without a new attack */
    void (*legato)(struct track *t, voice_t *v);
    /* optional: a key went down in MONO / LEGATO / UNISON, whether or not it takes the voice */
    void (*mono_key)(struct track *t, uint32_t note);
    /* optional: the part's block after its voices (FM6: Dexed's DC filter); nr: voices rendered */
    void (*post)(struct track *t, int32_t *out, uint32_t n, uint32_t nr);
    /* optional: the voice cap of the part now, instead of poly  */
    uint32_t (*cap)(const struct track *t);
    /* optional: the voice budget units one of its voices takes now  */
    uint32_t (*units)(const struct track *t);
} engine_t;
/* voice_start: what the voice it starts did just before (0 free, 1 released, 2 its key down: a steal or a move);
 * engine_t.note_on may read it  */
static uint8_t voice_was;

/* ------------------------------------------------- tracks, the song --- */
enum { ST_NOTE, ST_TIE, ST_REST };
#define SF_ACCENT 1u
#define SF_SLIDE 2u
#define SF_RECORDED 4u                         /* step is a view of timed notes, played by recording.c */
#define RECORD_MAX 1024u
#define RECORD_UNIT 65536u                     /* fractional onset within its swung step; duration uses an exponent */
/* owner: bank/track in low 5 bits, duration exponent in high 3. step: actual index in
 * low 6 bits, overview rounded forward in bit 6, wrapped to zero in bit 7. */
typedef struct { uint16_t on, duration; uint8_t note, vel, owner, step; int8_t pitch; uint8_t length; } recorded_note_t;
_Static_assert(sizeof(recorded_note_t) == 10u, "timed note layout");
/* Sound offsets: semitones, decay and voice character; zero preserves the factory kit. */
typedef struct { int8_t c[16][4]; } drum_patch_t;
static drum_patch_t drum_patch[NTRK];
static int drum_patch_valid(const drum_patch_t *p)
{
    for(uint32_t i=0;i<16;i++)if(p->c[i][0]<-24 || p->c[i][0]>24 || p->c[i][1]<-64 || p->c[i][1]>63 || p->c[i][2]<-64 || p->c[i][2]>63 || p->c[i][3]<0)return 0;
    return 1;
}
#define DRUM_WIRE_BYTES 54u /* 16 voices x (6-bit tune, 7-bit decay/character/attenuation) */
static void drum_wire_put(uint8_t *b,uint32_t *pos,uint32_t v,uint32_t n)
{while(n--){b[*pos>>3]|=(uint8_t)((v&1u)<<(*pos&7u));v>>=1;++*pos;}}
static uint32_t drum_wire_get(const uint8_t *b,uint32_t *pos,uint32_t n)
{uint32_t v=0;for(uint32_t i=0;i<n;i++,++*pos)v|=(uint32_t)((b[*pos>>3]>>(*pos&7u))&1u)<<i;return v;}
static void drum_patch_pack(uint8_t *b,const drum_patch_t *p)
{
    memset(b,0,DRUM_WIRE_BYTES);uint32_t pos=0;
    for(uint32_t i=0;i<16;i++){
        drum_wire_put(b,&pos,(uint32_t)(p->c[i][0]+24),6);
        drum_wire_put(b,&pos,(uint32_t)(p->c[i][1]+64),7);
        drum_wire_put(b,&pos,(uint32_t)(p->c[i][2]+64),7);
        drum_wire_put(b,&pos,(uint32_t)p->c[i][3],7);
    }
}
static int drum_patch_unpack(drum_patch_t *p,const uint8_t *b)
{
    uint32_t pos=0;
    for(uint32_t i=0;i<16;i++){
        p->c[i][0]=(int8_t)((int32_t)drum_wire_get(b,&pos,6)-24);
        p->c[i][1]=(int8_t)((int32_t)drum_wire_get(b,&pos,7)-64);
        p->c[i][2]=(int8_t)((int32_t)drum_wire_get(b,&pos,7)-64);
        p->c[i][3]=(int8_t)drum_wire_get(b,&pos,7);
    }
    return drum_patch_valid(p);
}

#define NLANE 8                  /* drum lanes of a step (the DRUM engine's: eng_drum.c DRUM_LANE_NOTE) */
typedef struct {                 /* acid-style step: up to 4 notes (POLY), time, accent, slide; drum hits */
    uint8_t note[4];
    uint8_t n;                   /* notes in use, 0 = empty */
    uint8_t time;                /* ST_NOTE / ST_TIE / ST_REST */
    uint8_t flags;               /* SF_ACCENT | SF_SLIDE */
    uint8_t vel;
    uint8_t hit;                 /* bit l: lane l hits (its GM note, on any engine): the DRUM grid */
    uint8_t acc;                 /* hit != 0: lane accents; hit == 0: recorded gate, 1..255 of a step (0 legacy) */
    uint8_t probability;         /* 0 = legacy 100%; 1..100 = percent, 101 = silent */
} step_t;
typedef struct {                 /* a step as formats 1..4 (projects to FUN4) stored it: no hits */
    uint8_t note[4];
    uint8_t n, time, flags, vel;
} step8_t;

/* Probability keeps zero-initialized and legacy patterns at 100%. */
static uint32_t step_chance(const step_t *s) { return !s->probability ? 100u : s->probability <= 100u ? s->probability : 0u; }
static void step_set_chance(step_t *s, uint32_t chance) { s->probability = (uint8_t)(chance >= 100u ? 0u : chance ? chance : 101u); }
static uint32_t step_gate(const step_t *s) { return s->hit ? 0u : s->acc; }
static int step_acc_valid(const step_t *s) { return s->hit ? !(s->acc & ~s->hit) : s->time != ST_REST || !s->acc; }
#define MOTION_MAX 64u
/* Four tracks x64 steps fit one byte. Values retain their signed parameter range. */
typedef struct { uint8_t place, param; int16_t value; } motion_event_t;
typedef struct { uint8_t count, on, rsv[2]; motion_event_t event[MOTION_MAX]; } motion_store_t;
_Static_assert(sizeof(motion_store_t) == 260u, "motion disk layout");
#define CHAIN_ROWS 16u
typedef struct { uint8_t slot, repeat; } chain_row_t;
typedef struct {
    uint8_t count, rsv[3];
    chain_row_t row[CHAIN_ROWS];
} chain_config_t;

typedef struct track {
    int16_t p[P_COUNT];
    uint8_t engine, preset;      /* engine: what the audio ISR renders */
    uint8_t eng_req;             /* engine the UI asked for (the ISR switches at a block start) */
    uint8_t user;                /* user preset slot + 1 the sound came from (UI), 0 = none */
    uint8_t user_native;         /* user refers to the engine's native pool */
    voice_t v[NVOICE];
    /* LFO */
    uint32_t lfo_ph;
    int32_t lfo_val;             /* Q15 */
    int32_t lfo_fade;            /* Q15 ramp after note-on */
    uint32_t lfo_rnd;
    int16_t lfo_prev;
    uint8_t sp_alt;
    /* keyboard / arp input: held notes in press order */
    uint8_t held[16];
    uint8_t nheld;
    uint8_t arp_phys;            /* keys physically held for the arp */
    uint8_t latched;             /* HOLD: keep notes after release */
    /* arp runtime */
    uint32_t arp_pos;            /* samples (INT), or MIDI musical units, into the arp step */
    uint32_t arp_idx;
    uint8_t arp_note;            /* sounding arp note, 0 = none */
    uint32_t arp_off;            /* remaining gate in the same clock units */
    /* sequencer */
    step_t step[NSTEP];
    volatile uint8_t pattern, pattern_next;             /* active bank, queued bank (0xff: none) */
    volatile uint32_t pattern_gen;                      /* invalidates editor history even for identical banks */
    uint32_t seq_pos;            /* samples (INT), or MIDI musical units, into the step */
    uint16_t seq_idx;
    uint8_t seq_notes[4 + NLANE];   /* sounding seq notes (the step's notes, then its hits) */
    uint8_t seq_n;
    uint8_t seq_hold;            /* last step slides: keep the notes until the next step */
    uint8_t slide_glide;         /* next legato note glides (slide) */
    uint32_t seq_off;
    uint8_t seq_active;          /* any step programmed */
    uint8_t rskip_idx;           /* live recording put notes into the step about to play: */
    uint8_t rskip_n, rskip[NLANE];   /* do not trigger them again there (they sound already) */
    /* live recording of held notes (seq.c rec_hold): the steps they are held into become TIEs */
    uint8_t rh_n, rh_note[4];    /* recorded notes still held, 0 = none */
    uint8_t rh_start;            /* the step they were recorded into */
    uint8_t rh_ties;             /* TIE steps written after it */
    uint8_t rh_last;             /* the last of them; rh_bak: what it held (an early release puts it back) */
    step_t rh_bak;
    uint32_t rh_elapsed;         /* actual held duration before onset quantization */
    /* mono */
    uint8_t mono_stack[NVOICE];  /* keys held, in press order (as many as Dexed keeps voices for) */
    uint8_t nmono;
    uint8_t mono_note;           /* note the MONO / LEGATO / UNISON voice(s) play, 0 = none */
    uint8_t rr;                  /* POLY ROTATE: next voice to try */
    /* mix runtime */
    int32_t peak;
    int32_t dist_hp, dist_lp1, dist_lp2;   /* DIST insert state (fx.c) */
    uint8_t tail;                /* blocks to mix after the last voice (the DIST tail) */
    int16_t armp, aholdp;        /* P_AMODE / P_AHOLD as last seen by the ISR */
    /* engine switch (voice.c engine_block): the old engine's voices fade out, then it switches */
    uint8_t xf_on, xf;           /* fading; blocks of the fade still to render */
    int16_t pe_old[8];           /* P_E0..P_E7 of the sounding engine: the fade renders with these */
    uint8_t xp_n, xp_note[4], xp_vel[4];   /* note-ons during the fade, played on the new engine */
    /* modulation matrix (mod.c): MIDI performance controllers of the track's channel, 0..127 */
    uint8_t mw, at;              /* CC1 mod wheel, channel aftertouch */
    uint8_t foot, breath, porta; /* CC4 foot, CC2 breath, CC65 portamento on: FM6's DX7 controllers */
    int16_t bend_raw;            /* the pitch bend as sent (signed 14-bit): FM6 bends by its own range */
    uint8_t ex_off;              /* 127 - CC11 expression (0: full, the MIDI default) */
    uint8_t m_vel, m_key, m_vi;  /* the latest note-on: velocity, note, voice index (per-block destinations) */
    int16_t m_rnd;               /* .. its RAND */
    int32_t m_env;               /* the amp envelope of voice m_vi, last block (Q15) */
} track_t;

typedef struct {
    int16_t g[G_COUNT];
    uint8_t playing, seq_mode;
    uint8_t grid;                /* 1: the keys are the DRUM grid (ui.c grid_on): only the lane keys play (seq.c);
                                  * 2: NAME (ui_name.c): no key plays */
    uint8_t rec;                 /* live recording armed: bit per track */
    uint8_t sel;                 /* selected track 0..NTRK-1: keys, pages, editor */
    int8_t octave;
    uint32_t tick;               /* sub-blocks since play */
    uint32_t cpu_q8;             /* audio ISR load, 1/256 */
    uint32_t master_q12;
    int32_t batt_raw;            /* smoothed ADC ch3 (battery divider), 0 = not read yet */
} song_t;

static track_t trk[NTRK];        /* the instrument: four parts */
static song_t song;
#define TSEL (&trk[song.sel])    /* the selected track */

#include "microtonal.h"

/* SWING of a track's step clock: the track's own plus the global one, at most 100 (#31). The sequencer
 * (seq.c step_samples) and the SLICER (slicer.c sl_enter) both time steps with it. At 100 the even
 * steps are 1.4 x the straight length and the odd ones 0.6 x. */
#define SWING_MAX 100
static inline int32_t track_swing(const track_t *t)
{
    int32_t sw = t->p[P_SSWING] + song.g[G_SWING];
    return sw < 0 ? 0 : sw > SWING_MAX ? SWING_MAX : sw;
}
/* the length of step idx of a straight length `base`, swung: even steps longer, odd ones shorter */
static inline uint32_t swing_step_len(const track_t *t, uint32_t base, uint32_t idx)
{
    int32_t sw = track_swing(t) * (int32_t)base / 250;
    return base + (uint32_t)((idx & 1u) ? -sw : sw);
}

/* ----------------------------------------------------------- system --- */
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")   /* slot store before the index update */
static volatile uint32_t fm1_ms;  /* milliseconds since boot (TIMER4-based, TIMER5 ISR in main.c) */
/* boot-loop guard (main.c): two boots in a row that die in the first 30 s -> UBOOT */
#define BOOTGUARD_MAGIC 0x42475244u
struct { uint32_t magic, failed, pending; } bootguard __attribute__((section(".noinit")));
