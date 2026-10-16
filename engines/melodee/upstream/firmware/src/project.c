/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Projects: four slots in NOR, each holding 32 independent pattern banks.
 * FBKK (pattern_store.c) contains the FUN22 sound/current-pattern record below.
 * Hardware retains only slot names/occupancy in RAM; full records use one
 * main-loop staging buffer. Host fixtures also provide a RAM storage backend.
 *
 * FUN18, FUN17 and FUN16 are read: 1.2's boot pass rewrote older ones, and
 * archives of before 1.0.0 restore through 1.2. A record keeps np (byte 66), the P_COUNT it was written with;
 * one of an older count maps by count (params_by_count, proj_motion_ids). Its byte `parts` is the tracks in use
 * (1..NTRK; written NPART before, older firmware never reads it: it loads every track, those not in use empty); `phys`
 * (PROJ_PHYS) stays as written. Tracks of retired engines (DIGITAL, ANALOG ..) keep their engine bytes and play
 * silent.
 *
 * Built on the Mac too (tests/project_test.c, -DPROJ_HOST): the part above the #ifndef
 * PROJ_HOST needs core.h, params.c (TP) and engines.c. */
#define PROJ_MAGIC 0x46554E49u                 /* FUN22: one TONE per FX slot; FUN21 and earlier convert */
#define PROJ_CUT_MAGIC 0x46554E48u             /* FUN21: a low and a high cut per FX slot, read as TONE */
#define PROJ_PRE_HP_MAGIC 0x46554E47u
#define PROJ_CUT_NP 125u                       /* FUN21 / TPLK's P_COUNT: three low cuts at P_FXTONE, three high cuts */
#define PROJ_TONE_WIRE (PROJ_FM6_OFF - 2u*FX_SLOTS) /* reserved tail before the fixed native patches: the shared
                                                    * slots' TONE + 64 (FUN21: their low cuts) */
#define PROJ_LP_WIRE (PROJ_TONE_WIRE + FX_SLOTS)   /* FUN21: their high cuts; 0 since */
#define PROJ_FX_MODE_OFF (8u + 2u * G_RESERVED24)
#define PROJ_FX_WIRE_OFF (8u + 2u * G_RESERVED4)
#define PROJ_FX_FIELDS 4u
#define PROJ_GLOBALS 27u
#define PROJ_MASTER_WIRE (PROJ_FX_WIRE_OFF+4u)
#define PROJ_PHYS 2u                           /* project_t.phys: PHYS without DUST and DRUM (see the top) */
#define PROJ_NAME_LEN 12u                      /* the name: FUN7 bytes PROJ_NAME_OFF.. (the reserved tail's end) */
typedef struct {                               /* one track */
    int16_t p[P_COUNT];
    uint8_t engine, preset;
    step_t step[NSTEP];
} proj_trk_t;
typedef struct {
    uint32_t magic, size;
    int16_t g[G_COUNT];
    uint8_t sel;                               /* the selected track */
    uint8_t parts;                             /* the tracks in use, 1..NTRK (song.ntrk); else all NTRK */
    uint8_t phys;                              /* PROJ_PHYS: PHYS MODEL values as today; 1: MODEL 4 was DRUM;
                                                * 0 (a reserved byte before 1.0): MODEL 2 was DUST */
    uint8_t rsv;
    proj_trk_t t[NTRK];
    chain_config_t chain;
    motion_store_t motion;
    uint8_t fm6[NTRK][FM6_PACKED];             /* each track's FM6 patch, packed (eng_fm6.c) */
    uint8_t fm6_fn[NTRK][FM6_NFN];             /* .. and its function settings (fm6_fn_ok; else Dexed's): not in
                                                * FUN8, an FBK9 record keeps them (pattern_store.c BANK_FN_OFF) */
    uint8_t fm6_fn_ok, rsv2[3];
    cz_patch_t cz[NTRK];
    p5_patch_t p5[NTRK];
    drum_patch_t drum[NTRK];
    recorded_note_t recording[RECORD_MAX];
    uint8_t pattern[NTRK];                     /* FUN13's active-bank ids (also in the full container) */
    char name[PROJ_NAME_LEN];                  /* the project's name: upper-case ASCII 32..126, 0-padded; "" = none */
    uint16_t user[NTRK];                    /* shared slot + 1; sound remains embedded independently */
    uint32_t sum;
} __attribute__((may_alias)) project_t;
/* The four formerly reserved runtime bytes keep Prophet user-slot origins.
 * Zero means factory/unknown; otherwise the value is slot + 1. */
static uint8_t *proj_p5_origin(project_t *p,uint32_t track)
{
    return track ? &p->rsv2[track-1u] : &p->rsv;
}
static uint8_t proj_p5_origin_value(const project_t *p,uint32_t track)
{
    return track ? p->rsv2[track-1u] : p->rsv;
}
/* The serialized record (its layout grew from FUN7, hence the sizes below). Params are biased
 * bytes, steps pack n/time/flags. Reserved tail is zero and covered by hash.
 * The tail's last 12 bytes (PROJ_NAME_OFF, just before the hash) are the project's name since 1.0:
 * ASCII 32..126 (upper case), 0-padded, all 0 = no name ("PROJECT A"). Firmware before wrote them 0 and
 * never reads them, so every FUN7 file stays valid both ways; FUN6..FUN1 imports get no name.
 * FUN8: the same, 3584 bytes, the four packed FM6 patches at PROJ_FM6_OFF (before the name); 12 bytes of the
 * reserved tail are left for parameters added later (P_MPCDEG took 4). */
#define PROJ_STORE_V8 3584u
#define PROJ_CZ_BYTES (NTRK * CZ_BYTES)
#define PROJ_STORE_V11 (PROJ_STORE_V8 + PROJ_CZ_BYTES)
#define PROJ_STORE_V13 (PROJ_STORE_V11 + RECORD_MAX * 8u)
#define PROJ_P5_BYTES (NTRK * sizeof(p5_patch_t))
#define PROJ_STORE_V14 (PROJ_STORE_V13 + PROJ_P5_BYTES)
#define PROJ_PARAM_EXTRA 48u
#define PROJ_STORE_V16 (PROJ_STORE_V14 + PROJ_PARAM_EXTRA)
#define PROJ_DRUM_BYTES (NTRK*DRUM_WIRE_BYTES)
#define PROJ_REC_DIR_BYTES 44u /* 32 eleven-bit counts (0..1024) */
#define PROJ_DRUM_OFF (PROJ_P5_OFF + PROJ_P5_BYTES)
#define PROJ_REC_DIR (PROJ_DRUM_OFF + PROJ_DRUM_BYTES)
#define PROJ_STORE_SIZE (PROJ_STORE_V16 + PROJ_DRUM_BYTES + PROJ_REC_DIR_BYTES)
#define PROJ_BANK_BYTES (18980u + RECORD_MAX * 8u + PROJ_P5_BYTES + PROJ_PARAM_EXTRA + PROJ_DRUM_BYTES + PROJ_REC_DIR_BYTES - 128u)
#define PROJ_NAME_OFF (PROJ_STORE_SIZE - 4u - PROJ_NAME_LEN)
#define PROJ_REC_OFF (PROJ_STORE_V11 + PROJ_PARAM_EXTRA - 4u - PROJ_NAME_LEN)
#define PROJ_CZ_OFF (PROJ_REC_OFF - PROJ_CZ_BYTES)
#define PROJ_P5_OFF (PROJ_REC_OFF + RECORD_MAX * 8u)
#define PROJ_FM6_OFF (PROJ_CZ_OFF - NTRK * FM6_PACKED)
#define PROJ_P5_ORIGIN_TAG0 0x50u
#define PROJ_P5_ORIGIN_TAG1 0x35u
#define PROJ_P5_ORIGIN_MAX 128u /* native Prophet user collection */
typedef union { uint32_t align; uint8_t raw[PROJ_STORE_SIZE]; } project_store_t;
_Static_assert(G_MFXTYPE == PROJ_GLOBALS && G_FXMODE==40u && G_BUDGET==41u && G_MFXTONE==42u && G_COUNT==45u, "old global IDs and wire header stay fixed");
/* FUN21 / TPLK's two cuts (LO CUT 10 + 10 v Hz, HI CUT 100 + 20 v Hz, 0 OFF) -> the TONE nearest them, the low cut
 * first (TONE holds one); a low cut at or under 150 Hz is what the effects' own floor does now (fx_floor_hz) */
static int16_t proj_tone_of_cuts(int32_t lo, int32_t hi)
{
    int32_t hz, best = 0, err = 1 << 30, v, e;
    if (lo > 0 && 10 + 10 * lo <= fx_tone_hz(1))
        lo = 0;
    hz = lo > 0 ? 10 + 10 * lo : hi > 0 ? 100 + 20 * hi : 0;
    for (v = 1; hz && v <= FX_TONE_MAX; v++) {
        e = fx_tone_hz(lo > 0 ? v : -v) - hz;
        if ((e < 0 ? -e : e) < err) { err = e < 0 ? -e : e; best = lo > 0 ? v : -v; }
    }
    return (int16_t)best;
}
_Static_assert(sizeof(project_store_t) == 13212u, "FUN17 keeps the FUN16 extent");
#define PROJ_DEF_SOUND 0xFFu                   /* preset byte: the track's power-on sound, no steps (kept from an
                                                * import of before 1.0) */
#define PROJ_DEF_KEEP 0xFEu                    /* .. the 808 DRUM sound, steps and the rest kept (old drums) */
#if !defined(FM1_IRQ_TARGET)
project_store_t proj_slot[4];                 /* host-only cache; hardware reads complete projects from NOR */
static uint8_t proj_bank_slot[4][PROJ_BANK_BYTES];
#endif
static struct { uint8_t used, old; char name[PROJ_NAME_LEN + 1u]; } proj_meta[4];   /* old: a record this firmware
                                                     * does not read (written before 1.0): shown, kept, never overwritten unasked */

static uint32_t proj_hash(const void *p, uint32_t n) { return fnv(2166136261u, p, n); }
static uint32_t proj_sum(const project_t *p) { return proj_hash(p, sizeof *p - 4u); }
static int proj_ok(const project_t *q)
{
    if (q->magic != PROJ_MAGIC || q->size != sizeof *q || q->sum != proj_sum(q) ||
        q->sel >= NTRK || q->parts > NPART || q->phys > PROJ_PHYS ||
        !chain_valid(&q->chain) || !motion_valid(&q->motion)) return 0;
    for (uint32_t k = 0; k < NTRK; k++) if (q->t[k].engine >= NENGINES || q->pattern[k] >= NPAT || !drum_patch_valid(&q->drum[k]) || !p5_patch_valid(&q->p5[k])) return 0;
    for (uint32_t i = 0; i < RECORD_MAX; i++) if (!recording_valid(&q->recording[i])) return 0;
    return 1;
}

static int proj_unpack(project_t *q, const uint8_t *b);
static int proj_fn_none(project_t *q);
/* FUN18 / FUN17 / FUN16 or a project_t (host RAM slots) -> q; 0 = not a project. */
static int proj_import(project_t *q, const void *b, int n)
{
    if (n == (int)PROJ_STORE_SIZE && (((const uint32_t *)b)[0] == PROJ_MAGIC || ((const uint32_t *)b)[0] == PROJ_CUT_MAGIC || ((const uint32_t *)b)[0] == PROJ_PRE_HP_MAGIC || ((const uint32_t *)b)[0] == 0x46554E46u || ((const uint32_t *)b)[0] == 0x46554E45u || ((const uint32_t *)b)[0] == 0x46554E44u || ((const uint32_t *)b)[0] == 0x46554E43u))
        return proj_unpack(q, b) && proj_fn_none(q);
    if (n == (int)sizeof *q && proj_ok((const project_t *)b)) {
        memcpy(q, b, sizeof *q);
        return 1;
    }
    return 0;
}
static int proj_fn_none(project_t *q)          /* a record without FM6 function settings: Dexed's */
{
    memset(q->fm6_fn, 0, sizeof q->fm6_fn);
    q->fm6_fn_ok = 0;
    q->sum = proj_sum(q);
    return 1;
}
/* 12 stored name bytes -> d (PROJ_NAME_LEN + 1): up to the first 0, upper case; a byte outside 32..126 makes it
 * no name (a damaged tail never refuses the project) */
static uint32_t proj_name_get(char *d, const uint8_t *s)   /* its length */
{
    uint32_t i;
    for (i = 0; i < PROJ_NAME_LEN && s[i]; i++) {
        if (s[i] < 32u || s[i] > 126u) { i = 0; break; }
        d[i] = (char)(s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32u : s[i]);
    }
    d[i] = 0;
    return i;
}

/* A stable serialized schema: first header retains FUN6's fields; at byte66
 * np, format flags, then four byte-param tracks and nine-byte steps; FUN8: the patches at PROJ_FM6_OFF. */
/* Grouping by bank preserves full onset/duration precision and all 1024 hits.
 * The bank directory restores owners; event indices are runtime selection only. */
static int proj_record_pack(uint8_t *b,const project_t *q)
{
    uint32_t at=0;
    for(uint32_t owner=0;owner<NTRK*NPAT;owner++){
        uint16_t count=0;
        for(uint32_t i=0;i<RECORD_MAX;i++){
            const recorded_note_t *r=&q->recording[i];
            if(!r->vel || (r->owner&31u)!=owner)continue;
            if(!recording_valid(r))return 0;
            uint32_t pos=0;uint8_t *v=b+PROJ_REC_OFF+at++*8u;
            wire_bits_put(v,&pos,r->on,16);wire_bits_put(v,&pos,r->duration,16);
            wire_bits_put(v,&pos,r->note,7);wire_bits_put(v,&pos,r->vel,7);
            wire_bits_put(v,&pos,r->step,8);wire_bits_put(v,&pos,r->owner>>5,3);
            wire_bits_put(v,&pos,(uint32_t)(r->pitch+24),6);wire_bits_put(v,&pos,r->length,1);count++;
        }
        uint32_t dir=owner*11u;wire_bits_put(b+PROJ_REC_DIR,&dir,count,11);
    }
    return 1;
}
static int proj_record_unpack(project_t *q,const uint8_t *b)
{
    uint32_t at=0;
    for(uint32_t owner=0;owner<NTRK*NPAT;owner++){
        uint32_t dir=owner*11u;uint16_t count=(uint16_t)wire_bits_get(b+PROJ_REC_DIR,&dir,11);
        if(count>RECORD_MAX-at)return 0;
        for(uint32_t i=0;i<count;i++){
            recorded_note_t *r=&q->recording[at];const uint8_t *v=b+PROJ_REC_OFF+at++*8u;uint32_t pos=0;
            r->on=wire_bits_get(v,&pos,16);r->duration=wire_bits_get(v,&pos,16);
            r->note=wire_bits_get(v,&pos,7);r->vel=wire_bits_get(v,&pos,7);
            r->step=wire_bits_get(v,&pos,8);r->owner=owner|(wire_bits_get(v,&pos,3)<<5);
            r->pitch=(int8_t)((int32_t)wire_bits_get(v,&pos,6)-24);r->length=wire_bits_get(v,&pos,1);
            if(!r->vel || !recording_valid(r))return 0;
        }
    }
    return 1;
}

/* The bank's 64-bit step representation, also used by FUN18's active pattern. */
static int proj_step_pack(uint8_t *b,const step_t *s)
{
    if(s->n>4u || s->time>ST_REST || s->flags>7u || s->probability>101u)return 0;
    uint32_t pos=0;memset(b,0,8u);
    for(uint32_t j=0;j<4u;j++)wire_bits_put(b,&pos,s->note[j]>127u?127u:s->note[j],7u);
    wire_bits_put(b,&pos,(s->time==ST_NOTE?s->n:s->time==ST_TIE?5u:6u)|(s->flags&SF_RECORDED?8u:0u),4u);
    wire_bits_put(b,&pos,s->flags&3u,2u);wire_bits_put(b,&pos,s->vel>127u?127u:s->vel,7u);wire_bits_put(b,&pos,s->hit,8u);
    wire_bits_put(b,&pos,s->hit?s->acc&s->hit:s->time==ST_REST?0:s->acc,8u);wire_bits_put(b,&pos,s->probability,7u);return 1;
}
static int step_wire_unpack(step_t *s,const uint8_t *b)
{
    uint32_t pos=0;for(uint32_t j=0;j<4u;j++)s->note[j]=(uint8_t)wire_bits_get(b,&pos,7u);
    uint32_t meta=wire_bits_get(b,&pos,4u);
    s->n=(meta&7u)<5u?meta&7u:0;s->time=(meta&7u)<5u?ST_NOTE:(meta&7u)==5u?ST_TIE:ST_REST;
    s->flags=(uint8_t)(wire_bits_get(b,&pos,2u)|(meta&8u?SF_RECORDED:0u));s->vel=(uint8_t)wire_bits_get(b,&pos,7u);
    s->hit=(uint8_t)wire_bits_get(b,&pos,8u);s->acc=(uint8_t)wire_bits_get(b,&pos,8u);s->probability=(uint8_t)wire_bits_get(b,&pos,7u);
    return (meta&7u)<7u && s->probability<=101u && step_acc_valid(s);
}

static int proj_pack(project_store_t *out, const project_t *q)
{
    uint8_t *b = out->raw; uint32_t pos = 68u, t, i; uint32_t magic = PROJ_MAGIC, size = PROJ_STORE_SIZE, sum;
    for (t = 0; t < NTRK; t++)
        for (i = 0; i < FM6_PACKED; i++)
            if (q->fm6[t][i] > 127u) return 0;
    if (q->g[G_FXRETURN]<0 || q->g[G_FXRETURN]>127 || q->g[G_FXMODE]<0 || q->g[G_FXMODE]>1 || !chain_valid(&q->chain) || !motion_valid(&q->motion) || P_COUNT > 127u) return 0;
    memset(out, 0, sizeof *out); memcpy(b, &magic, 4); memcpy(b + 4, &size, 4);
    memcpy(b + 8, q->g, 2u*PROJ_GLOBALS);
    b[PROJ_FX_MODE_OFF]=(uint8_t)q->g[G_FXMODE];b[PROJ_FX_MODE_OFF+1u]=0;
    b[PROJ_FX_WIRE_OFF]='M';b[PROJ_FX_WIRE_OFF+1u]='F';b[PROJ_FX_WIRE_OFF+2u]='X';b[PROJ_FX_WIRE_OFF+3u]=(uint8_t)q->g[G_FXRETURN];
    for(i=0;i<FX_SLOTS*FX_PARAMS;i++){uint32_t id=G_MFXTYPE+i;if(q->g[id]<GP[id].min || q->g[id]>GP[id].max)return 0;b[PROJ_MASTER_WIRE+i]=(uint8_t)q->g[id];} b[62] = q->sel; b[63] = q->parts; b[64] = q->phys; b[66] = P_COUNT;
    for(i=0;i<FX_SLOTS;i++){int32_t v=q->g[G_MFXTONE+i];if(v<-FX_TONE_MAX || v>FX_TONE_MAX)return 0;b[PROJ_TONE_WIRE+i]=(uint8_t)(v+64);}
    uint32_t banks = 0;
    for (t = 0; t < NTRK; t++) { if (q->pattern[t] >= NPAT) return 0; banks |= q->pattern[t] << (3u * t); }
    b[65] = (uint8_t)banks; b[67] = (uint8_t)(banks >> 8);
    for (t = 0; t < NTRK; t++) {
        for (i = 0; i < P_COUNT; i++) {
            if (q->t[t].p[i] < -64 || q->t[t].p[i] > 127) return 0;
            b[pos++] = (uint8_t)(q->t[t].p[i] + 64);
        }
        b[pos++] = q->t[t].engine; b[pos++] = q->t[t].preset;
        for (i = 0; i < NSTEP; i++) {
            if(!proj_step_pack(b+pos,&q->t[t].step[i]))return 0;pos+=8u;
        }
    }
    if (pos + sizeof q->chain + sizeof q->motion + 2u + 2u*NTRK > PROJ_TONE_WIRE) return 0;
    memcpy(b + pos, &q->chain, sizeof q->chain); pos += sizeof q->chain;
    memcpy(b + pos, &q->motion, sizeof q->motion);
    pos += sizeof q->motion;
    b[pos++]='U';b[pos++]=3u;
    for(t=0;t<NTRK;t++){b[pos++]=(uint8_t)q->user[t];b[pos++]=(uint8_t)(q->user[t]>>8);}
    memcpy(b + PROJ_FM6_OFF, q->fm6, sizeof q->fm6);
    for (t = 0; t < NTRK; t++) {
        if (!cz_patch_valid(q->cz[t].raw)) return 0;
        memcpy(b + PROJ_CZ_OFF + t*CZ_BYTES, q->cz[t].raw, CZ_BYTES);
    }
    for (i = 0; i < RECORD_MAX; i++) if (!recording_valid(&q->recording[i])) return 0;
    if(!proj_record_pack(b,q))return 0;
    for(t=0;t<NTRK;t++){if(!drum_patch_valid(&q->drum[t]))return 0;}
    for(t=0;t<NTRK;t++)drum_patch_pack(b+PROJ_DRUM_OFF+t*DRUM_WIRE_BYTES,&q->drum[t]);
    for(t=0;t<NTRK;t++){if(!p5_patch_valid(&q->p5[t]))return 0;memcpy(b+PROJ_P5_OFF+t*sizeof(p5_patch_t),&q->p5[t],sizeof(p5_patch_t));}
    {   /* the name (0-padded; stops at the first 0) */
        char n[PROJ_NAME_LEN + 1u];
        memcpy(b + PROJ_NAME_OFF, n, proj_name_get(n, (const uint8_t *)q->name));
    }
    sum = proj_hash(b, PROJ_STORE_SIZE - 4u); memcpy(b + PROJ_STORE_SIZE - 4u, &sum, 4);
    return 1;
}
/* the motion of a FUN7 written with np parameters (np < P_COUNT: before the chord keys, 89) -> today's ids:
 * an event names a parameter id, and the ids from that store's P_E0 (np - 8) on moved up with P_E0, as its
 * values did (params_by_count); the ones below kept theirs */
static void proj_motion_ids(motion_store_t *m, uint32_t np)
{
    uint32_t i;
    for (i = 0; i < m->count && i < MOTION_MAX; i++)
        if (!motion_native_event(m->event[i].param,m->event[i].value) && np != P_COUNT && m->event[i].param >= np - 8u && m->event[i].param < np)
            m->event[i].param = (uint8_t)(m->event[i].param + P_COUNT - np);
}
/* FUN18 / FUN17 / FUN16 share fixed patches/timing offsets. FUN18 packs eight-byte steps.
 * FUN17 stores its original FX in
 * global wire padding and omits them from the sequential parameter bytes;
 * np still names the runtime layout, so engine motion IDs remain unambiguous. */
static int proj_unpack(project_t *q, const uint8_t *b)
{
    uint32_t pos = 68u, t, i, magic, size, sum, np = b[66];
    const uint32_t end = PROJ_NAME_OFF - PROJ_DRUM_BYTES - PROJ_REC_DIR_BYTES - PROJ_P5_BYTES - RECORD_MAX * 8u -
                         NTRK * FM6_PACKED - PROJ_CZ_BYTES;
    memcpy(&magic, b, 4); memcpy(&size, b + 4, 4); memcpy(&sum, b + PROJ_STORE_SIZE - 4u, 4);
    if ((magic != PROJ_MAGIC && magic != PROJ_CUT_MAGIC && magic != PROJ_PRE_HP_MAGIC && magic != 0x46554E46u && magic != 0x46554E45u && magic != 0x46554E44u && magic != 0x46554E43u) || size != PROJ_STORE_SIZE || sum != proj_hash(b, PROJ_STORE_SIZE - 4u) ||
        np < 8u || np > (magic==PROJ_CUT_MAGIC?PROJ_CUT_NP:P_COUNT) || (magic==PROJ_MAGIC && np!=P_COUNT) || (magic==PROJ_CUT_MAGIC && np!=PROJ_CUT_NP) || (magic==PROJ_PRE_HP_MAGIC && np!=119u) || (magic==0x46554E46u && np!=117u) || (magic==0x46554E45u && np!=116u) || (magic==0x46554E44u && np!=108u) || (magic==0x46554E43u && np!=104u) ||
        b[62] >= NTRK || b[63] > NPART || b[64] > PROJ_PHYS ||
        68u + NTRK * (np - (magic==0x46554E44u?PROJ_FX_FIELDS:0u) + 2u + NSTEP * ((magic==PROJ_MAGIC || magic==PROJ_CUT_MAGIC || magic==PROJ_PRE_HP_MAGIC || magic==0x46554E46u || magic==0x46554E45u)?8u:9u)) + sizeof q->chain + sizeof q->motion > end)
        return 0;
    memset(q, 0, sizeof *q); q->magic = PROJ_MAGIC; q->size = sizeof *q;
    memcpy(q->g,b+8,2u*PROJ_GLOBALS);
    for(i=G_MFXTYPE;i<G_COUNT;i++)q->g[i]=GP[i].def;
    if(magic==PROJ_MAGIC || magic==PROJ_CUT_MAGIC || magic==PROJ_PRE_HP_MAGIC || magic==0x46554E46u){
        if(b[PROJ_FX_WIRE_OFF]!='M' || b[PROJ_FX_WIRE_OFF+1u]!='F' || b[PROJ_FX_WIRE_OFF+2u]!='X' || b[PROJ_FX_WIRE_OFF+3u]>127u)return 0;
        q->g[G_FXRETURN]=b[PROJ_FX_WIRE_OFF+3u];
        for(i=0;i<FX_SLOTS*FX_PARAMS;i++){uint32_t id=G_MFXTYPE+i;uint8_t v=b[PROJ_MASTER_WIRE+i];if(v>GP[id].max)return 0;q->g[id]=v;}
        for(i=G_RESERVED4;i<=G_RESERVED11;i++)q->g[i]=0;
    }
    if(magic==PROJ_MAGIC || magic==PROJ_CUT_MAGIC || magic==PROJ_PRE_HP_MAGIC){if(b[PROJ_FX_MODE_OFF]>1u || b[PROJ_FX_MODE_OFF+1u])return 0;q->g[G_FXMODE]=b[PROJ_FX_MODE_OFF];}
    for(i=0;i<FX_SLOTS && (magic==PROJ_MAGIC || magic==PROJ_CUT_MAGIC);i++){
        uint32_t lo=b[PROJ_TONE_WIRE+i],hi=b[PROJ_LP_WIRE+i];
        if(magic==PROJ_MAGIC ? lo<64u-FX_TONE_MAX || lo>64u+FX_TONE_MAX || hi : lo>127u || hi>127u)return 0;
        q->g[G_MFXTONE+i]=magic==PROJ_MAGIC?(int16_t)((int32_t)lo-64):proj_tone_of_cuts((int32_t)lo,(int32_t)hi);
    }
    q->g[G_RESERVED24]=0;
    if(magic==0x46554E44u)for(i=G_RESERVED4;i<=G_RESERVED11;i++)q->g[i]=0;
    q->sel = b[62]; q->parts = b[63]; q->phys = b[64];
    {
        uint32_t banks = b[65] | (uint32_t)b[67] << 8;
        if (banks & 0xF000u) return 0;
        for (t = 0; t < NTRK; t++) q->pattern[t] = (uint8_t)((banks >> (3u * t)) & 7u);
    }
    for (t = 0; t < NTRK; t++) {
        int16_t values[PROJ_CUT_NP], def[P_COUNT];
        for (i = 0; i < np; i++) {
            if(magic==0x46554E44u && i>=P_FXTYPE && i<=P_FXCHAR){
                uint32_t value=b[PROJ_FX_WIRE_OFF+t*PROJ_FX_FIELDS+i-P_FXTYPE];
                if(value>(uint32_t)TP[i].max)return 0;values[i]=(int16_t)value;
            }else {if(b[pos]>191u)return 0;values[i]=(int16_t)b[pos++]-64;}
        }
        q->t[t].engine = b[pos++]; q->t[t].preset = b[pos++];
        if (q->t[t].engine >= NENGINES) return 0;  /* a fork's engine ID must not wrap into ours */
        for (i = 0; i < P_COUNT; i++) def[i] = param_desc_of(q->t[t].engine % NENGINES, i)->def;
        params_by_count(q->t[t].p, values, np, def);
        if (magic == PROJ_CUT_MAGIC)                   /* (FUN21: P_FXTONE.. held the low cuts, the high cuts after) */
            for (i = 0; i < FX_SLOTS; i++)
                q->t[t].p[P_FXTONE + i] = proj_tone_of_cuts(values[P_FXTONE + i], values[P_FXTONE + FX_SLOTS + i]);
        for (i = 0; i < NSTEP; i++) {
            step_t *s = &q->t[t].step[i]; uint32_t meta;
            if(magic==PROJ_MAGIC || magic==PROJ_CUT_MAGIC || magic==PROJ_PRE_HP_MAGIC || magic==0x46554E46u || magic==0x46554E45u){if(!step_wire_unpack(s,b+pos))return 0;pos+=8u;continue;}
            memcpy(s->note, b + pos, 4); pos += 4; meta = b[pos++];
            s->n = meta & 7u; s->time = (meta >> 3) & 3u; s->flags = (meta >> 5) & 3u;
            if (meta & 128u) s->flags |= SF_RECORDED;
            s->vel = b[pos++]; s->hit = b[pos++]; s->acc = b[pos++]; s->probability = b[pos++];
            if (s->n > 4u || s->time > ST_REST || s->probability > 101u) return 0;
            for (uint32_t j = 0; j < 4u; j++) if (s->note[j] > 127u) return 0;
            if (s->vel > 127u || !step_acc_valid(s)) return 0;
        }
    }
    memcpy(&q->chain, b + pos, sizeof q->chain); pos += sizeof q->chain;
    memcpy(&q->motion, b + pos, sizeof q->motion);
    pos += sizeof q->motion;
    if(pos+2u+2u*NTRK<=PROJ_FM6_OFF && b[pos]=='U' && b[pos+1u]==3u){
        for(t=0;t<NTRK;t++){uint32_t user=b[pos+2u+2u*t]|(uint32_t)b[pos+3u+2u*t]<<8;q->user[t]=(uint16_t)(user<=384u?user:0);}
    }else if(pos+2u+NTRK<=PROJ_FM6_OFF && b[pos]==PROJ_P5_ORIGIN_TAG0 && b[pos+1u]==PROJ_P5_ORIGIN_TAG1){
        for(t=0;t<NTRK;t++){uint32_t origin=b[pos+2u+t];if(origin && origin<=128u){uint32_t slot=origin-1u,bank=slot/26u,k=slot%26u,spill=bank*8u+k-18u;q->user[t]=(uint16_t)(1u+(k<18u?(16u+bank)*18u+k:spill<32u?spill/2u*18u+16u+spill%2u:378u+spill-32u));}}
    }
    proj_motion_ids(&q->motion, np);
    if (!chain_valid(&q->chain) || !motion_valid(&q->motion)) return 0;
    for (t = 0; t < NTRK; t++)
        for (i = 0; i < FM6_PACKED; i++)
            q->fm6[t][i] = b[end + t * FM6_PACKED + i] & 0x7Fu;
    for (t = 0; t < NTRK; t++) {
        memcpy(q->cz[t].raw, b + PROJ_CZ_OFF + t * CZ_BYTES, CZ_BYTES);
        if (!cz_patch_valid(q->cz[t].raw)) return 0;
        memcpy(&q->p5[t], b + PROJ_P5_OFF + t * sizeof(p5_patch_t), sizeof(p5_patch_t));
        if (!p5_patch_valid(&q->p5[t])) return 0;
        if (!drum_patch_unpack(&q->drum[t], b + PROJ_DRUM_OFF + t * DRUM_WIRE_BYTES)) return 0;
    }
    if (!proj_record_unpack(q, b)) return 0;
    for (i = 0; i < RECORD_MAX; i++) if (!recording_valid(&q->recording[i])) return 0;
    {
        char n[PROJ_NAME_LEN + 1u];
        memcpy(q->name, n, proj_name_get(n, b + PROJ_NAME_OFF));
    }
    q->sum = proj_sum(q);
    return 1;
}

#ifndef PROJ_HOST
/* Old GM projects used this kit before DRUM existed. Its saved initializer is private now;
 * restore the same sound without passing its index through factory preset browsing. */
static void proj_legacy_perc(track_t *t)
{

    const preset_t *pr = &DRUM_PRESETS[0];
    t->eng_req = ENGI_DRUM;
    uint32_t i;
    t->preset = 0;                              /* display metadata; SET remains the legacy kit */
    for (i = 0; i < P_E0; i++)
        if (!param_kept(i))
            t->p[i] = TP[i].def;
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = pr->e[i];
    t->p[P_ATK] = pr->env[0];
    t->p[P_DEC] = pr->env[1];
    t->p[P_SUS] = pr->env[2];
    t->p[P_REL] = pr->env[3];
    t->p[P_ED_FLT] = pr->fenv;
    t->p[P_VOICE] = pr->mono ? V_LEGATO : V_POLY;
}

#ifdef CV_WORK_BYTES
/* Used synchronously by the main loop, outside drawing. Slot names are
 * cached in proj_meta; drawing never decodes a project into its own canvas.
 * may_alias on project_t covers the canvas's other typed view. */
_Static_assert(sizeof(project_t) <= CV_WORK_BYTES, "decoded project fits canvas scratch");
#define proj_scratch (*(project_t *)cv_work())
#else
static project_t proj_scratch __attribute__((section(".pool"))); /* standalone host fixtures */
#endif
static char proj_name[PROJ_NAME_LEN + 1u]    /* the name of the music as it is now (loaded, saved, the editor's */
    __attribute__((section(".pool")));       /* runtime restore); "" = none. A save takes it unless one is given */
#define PROJ_NO_SLOT 0xFFu
static uint8_t proj_cur = PROJ_NO_SLOT;      /* the slot the music was loaded from or last saved to (a rename of it
                                              * renames the music too); PROJ_NO_SLOT none (the editor's restore) */
static union {                               /* serialized main-loop work, also used for archive transfers */
    project_store_t p;                       /* standalone legacy record view */
    uint8_t raw[PROJ_BANK_BYTES];              /* complete FBKG, including legacy imports */
} proj_wire_u __attribute__((section(".pool")));
#define proj_wire proj_wire_u.p
static uint8_t proj_wire_gen;                /* +1 whenever proj_wire is rewritten (a backup's runtime copy lives there) */
static uint8_t *proj_wire_borrow(void) { proj_wire_gen++; return proj_wire_u.raw; }   /* scratch for another job */

static void proj_steps(step_t *s)            /* a loaded sequence stays inside its fixed fields */
{
    uint32_t i, j;
    for (i = 0; i < NSTEP; i++) {
        if (s[i].n > 4u) s[i].n = 4;
        if (s[i].time > ST_REST) s[i].time = ST_REST;
        for (j = 0; j < 4u; j++) s[i].note[j] &= 127u;
        if (s[i].hit) s[i].acc &= s[i].hit;
        else if (s[i].time == ST_REST) s[i].acc = 0;
    }
}

/* an imported older format may hold values FUN7 cannot pack: keep them inside the fields and ranges */
static void proj_bound(project_t *q)
{
    uint32_t t, i;
    for (t = 0; t < NTRK; t++) {
        proj_steps(q->t[t].step);
        for (i = 0; i < NSTEP; i++) {
            step_t *s = &q->t[t].step[i];
            s->flags &= 7u;
            s->vel &= 127u;
            if (s->probability > 101u) s->probability = 0;
        }
        for (i = 0; i < P_COUNT; i++) {
            const param_desc_t *d = param_desc_of(q->t[t].engine % NENGINES, i);
            q->t[t].p[i] = (int16_t)clamp(q->t[t].p[i], d->min, d->max);
        }
    }
    q->sum = proj_sum(q);
}

#include "pattern_store.c"

/* FNV of each slot's stored bytes, as last written or read (0: unknown): autosave writes only what differs */
static uint32_t proj_saved_hash[4];
static int proj_read_slot(uint32_t slot)
{
    int n = -1;
    slot &= 3u;
    proj_wire_gen++;
#if MELODEE_FLASH
    if (flash_ok) {
        n = st_load(OBJ_BANK0 + slot, proj_wire_u.raw, sizeof proj_wire_u.raw);   /* (the legacy objects: retired) */
    }
#else
    if (bank_valid(proj_bank_slot[slot], BANK_STORE_SIZE) &&
        !memcmp(proj_bank_slot[slot] + 8u, &proj_slot[slot], sizeof proj_slot[slot])) {
        n = BANK_STORE_SIZE; memcpy(proj_wire_u.raw, proj_bank_slot[slot], BANK_STORE_SIZE);
    } else { n = sizeof proj_slot[slot]; memcpy(proj_wire_u.raw, &proj_slot[slot], sizeof proj_slot[slot]); }
#endif
    int valid = bank_full((uint32_t)n) ? bank_valid(proj_wire_u.raw, n) : proj_import(&proj_scratch, proj_wire_u.raw, n);
#if MELODEE_FLASH && !defined(FM1_IRQ_TARGET)
    if (valid) {
        if (n == BANK_STORE_SIZE) { memcpy(proj_bank_slot[slot], proj_wire_u.raw, BANK_STORE_SIZE); memcpy(&proj_slot[slot], proj_wire_u.raw + 8u, sizeof proj_slot[slot]); }
        else { memset(proj_bank_slot[slot], 0, sizeof proj_bank_slot[slot]); proj_bound(&proj_scratch); proj_pack(&proj_slot[slot], &proj_scratch); }
    }
#endif
    proj_meta[slot].used = (uint8_t)valid;
#if MELODEE_FLASH
    proj_meta[slot].old = (uint8_t)(!valid && flash_ok && st_exists(OBJ_BANK0 + slot));
#endif
    if (valid) proj_name_get(proj_meta[slot].name, (const uint8_t *)proj_scratch.name);
    else proj_meta[slot].name[0] = 0;
    proj_saved_hash[slot] = valid && n == BANK_STORE_SIZE ? proj_hash(proj_wire_u.raw, BANK_STORE_SIZE) : 0u;
    return valid ? n : 0;
}
static void proj_fetch(uint32_t slot) { (void)proj_read_slot(slot); }
static int proj_write_slot(uint32_t slot, const uint8_t *raw, uint32_t len)
{
    slot &= 3u;
#if MELODEE_FLASH
    if (!flash_ok || st_save(OBJ_BANK0 + slot, raw, len)) return 2;
#endif
#if !defined(FM1_IRQ_TARGET)
    memset(proj_bank_slot[slot], 0, sizeof proj_bank_slot[slot]);
    memset(&proj_slot[slot], 0, sizeof proj_slot[slot]);
    if (len) { memcpy(proj_bank_slot[slot], raw, len); memcpy(&proj_slot[slot], raw + 8u, sizeof proj_slot[slot]); }
#endif
    proj_meta[slot].used = len != 0;
    proj_meta[slot].old = 0;
    proj_saved_hash[slot] = len ? proj_hash(raw, len) : 0u;
    if (len) proj_name_get(proj_meta[slot].name, (const uint8_t *)proj_scratch.name);
    else proj_meta[slot].name[0] = 0;
    return 0;
}

static void project_capture(project_t *p)
{
    uint32_t i;
    uint8_t native[FP_SIZE + 1u];
    uint32_t f = motion_guard();
    memset(p, 0, sizeof *p);
    p->magic = PROJ_MAGIC;
    p->size = sizeof *p;
    for (i = 0; i < G_COUNT; i++)
        p->g[i] = song.g[i];
    p->sel = song.sel;
    p->parts = (uint8_t)trk_n();
    p->phys = PROJ_PHYS;
    p->chain = chain_config;
    for (i = 0; i < NTRK; i++) {
        for (uint32_t j = 0; j < P_COUNT; j++) p->t[i].p[j] = motion_base_value(&trk[i], j);
        p->t[i].engine = trk[i].eng_req;
        p->t[i].preset = trk[i].preset;
        if(user_of(&trk[i])<UP_SLOTS)p->user[i]=(uint16_t)(user_of(&trk[i])+1u);
        p->pattern[i] = trk[i].pattern;
        memcpy(p->t[i].step, trk[i].step, sizeof trk[i].step);
        p->cz[i] = cz_patch[i];
        p->p5[i] = *p5_patch_of(&trk[i]);
        motion_native_snapshot(&trk[i], p->cz[i].raw, MO_TAG_CZ, CZ_BYTES);
        motion_native_snapshot(&trk[i], p->p5[i].raw, MO_TAG_P5, sizeof p->p5[i].raw);
        memcpy(native, fm6_patch[i], sizeof native);
        motion_native_snapshot(&trk[i], native, MO_TAG_FM6, sizeof native);
        fm6_pack(native, p->fm6[i]);
        memcpy(p->fm6_fn[i], fm6_fn[i], FM6_NFN);
    }
    memcpy(p->drum,drum_patch,sizeof drum_patch);
    p->fm6_fn_ok = 1;
    p->motion = motion;
    for (i = 0; i < RECORD_MAX; i++) p->recording[i] = recording_snapshot(i);
    p->motion.rsv[0] = 1;                     /* full container supplies the bank tags */
    motion_unguard(f);
    memcpy(p->name, proj_name, str_len(proj_name));
    p->sum = proj_sum(p);
}

/* the music as it is now -> slot, named `name` (0: the current name; "" none); 0 saved, nonzero refused or failed.
 * The current name becomes the saved one */
static int project_save_as(uint32_t slot, const char *name)
{
    project_t *p = &proj_scratch;
    if (transport_busy()) {                            /* a flash erase silences the audio and stalls the */
        ui_message("STOP TO SAVE");                     /* sequencer (storage_hw.c): only while stopped */
        return 1;
    }
    momentary_restore();
    project_capture(p);
    if (name) {
        memset(p->name, 0, sizeof p->name);
        memcpy(p->name, name, str_len(name) < PROJ_NAME_LEN ? str_len(name) : PROJ_NAME_LEN);
    }
    proj_wire_gen++;
    if (!bank_pack(proj_wire_u.raw, p, 1)) { ui_message("SAVE FORMAT ERROR"); return 2; }

    if (proj_write_slot(slot, proj_wire_u.raw, BANK_STORE_SIZE)) { ui_message("SAVE ERROR"); return 2; }
    proj_name_get(proj_name, (const uint8_t *)p->name);
    proj_cur = (uint8_t)(slot & 3u);
    ui_message(MELODEE_FLASH ? "SAVED" : "SAVED (RAM)");
    return 0;
}
static int project_save(uint32_t slot) { return project_save_as(slot, 0); }

/* Hold SAVE or SAVE + REC (ui_input.c, stopped): the music back to the slot it was loaded from or last saved to, at once ("SAVED
 * B"); a new one (the template, the power-on sounds) has none yet: PROJECT opens on a free slot, SAVE picked */
static uint32_t project_free_slot(void);
static void project_quick_save(void)
{
    uint32_t i;
    if (proj_cur < 4u) {
        uint32_t slot = proj_cur;
        if (!project_save(slot)) {
            char b[12] = "SAVED A";
            b[6] = (char)('A' + slot);
            ui_message(b);
        }
        return;
    }
    song.g[G_SLOT] = (int16_t)(project_free_slot() + 1u);
    for (i = 0; i < NPAGES && PAGES[i].graph != GR_SLOTS; i++)
        ;
    ui.home = 0;
    ui.page = (uint8_t)i;
    page_entered();
    ui.act = 4;                                         /* (SAVE: OCT+ names and writes it) */
    ui.force = 1;
    ui_message("NEW PROJECT: PICK SLOT");
}
/* autosave (ui_input.c autosave_poll: stopped, untouched a while): the music back to the slot it was loaded from or last
 * saved to when it differs from what that slot holds ("SAVED B"); a new project (no slot yet) waits for a save */
static void project_autosave(void)
{
    project_t *p = &proj_scratch;
    uint32_t slot = proj_cur;
    if (slot >= 4u || transport_busy())
        return;
    project_capture(p);
    proj_wire_gen++;                                    /* (the staging RAM: a backup's copy there is gone) */
    if (!bank_pack(proj_wire_u.raw, p, 1))
        return;
    if (proj_hash(proj_wire_u.raw, BANK_STORE_SIZE) == proj_saved_hash[slot])
        return;                                         /* as saved: nothing to write */
    project_quick_save();
}
static void project_cur_name(char *b) { str_cpy(b, proj_name, PROJ_NAME_LEN + 1u); }   /* b: 13 bytes */

/* slot's name -> b (PROJ_NAME_LEN + 1 bytes); 0 = an empty slot (b ""). Uses proj_scratch */
static int project_old(uint32_t slot) { return !project_used(slot) && proj_meta[slot & 3u].old; }
/* the slot's name ("" none): 1 a project, 0 empty; an old record's: OLD FORMAT (1: the slot is taken) */
static int project_name(uint32_t slot, char *b)
{
    b[0] = 0;
    if (project_old(slot)) { str_cpy(b, "OLD FORMAT", PROJ_NAME_LEN + 1u); return 1; }
    if (!project_used(slot)) return 0;
    str_cpy(b, proj_meta[slot & 3u].name, PROJ_NAME_LEN + 1u);
    return 1;
}

/* a stored project renamed in place (nothing else of it changes; the music playing is not touched, but the slot
 * it was loaded from / saved to: its name is the new one, the next SAVE's prefill): 0 done, 1 refused (playing,
 * an empty slot), 2 failed (the slot as it was) */
static int project_rename(uint32_t slot, const char *name)
{
    project_t *p = &proj_scratch;
    if (transport_busy()) {
        ui_message("STOP TO SAVE");
        return 1;
    }
    int n = proj_read_slot(slot);
    if (!n) { ui_message("EMPTY SLOT"); return 1; }
    memset(p->name, 0, sizeof p->name);
    memcpy(p->name, name, str_len(name) < PROJ_NAME_LEN ? str_len(name) : PROJ_NAME_LEN);
    p->sum = proj_sum(p);
    if (n == BANK_STORE_SIZE) {
        memcpy(proj_wire_u.raw + BANK_MOTION_OFF, bank_import_tags, sizeof bank_import_tags);
        if (!proj_pack((project_store_t *)(proj_wire_u.raw + 8u), p)) return 2;
        bank_checksum(proj_wire_u.raw);
    } else {
        proj_bound(p); chain_defaults(&p->chain);
        if (!bank_pack(proj_wire_u.raw, p, 0)) return 2;
    }
    if (proj_write_slot(slot, proj_wire_u.raw, BANK_STORE_SIZE)) { ui_message("SAVE ERROR"); return 2; }
    if (proj_cur == (slot & 3u))
        proj_name_get(proj_name, (const uint8_t *)p->name);
#if MELODEE_FLASH
    if (flash_ok) { ui_message("RENAMED"); return 0; }
#endif
    ui_message("RENAMED (RAM)");
    return 0;
}

/* slot -> empty (PROJECT's Erase, after its question): an empty record, read as no project; 0 done */
static int project_erase(uint32_t slot)
{
    if (transport_busy()) {
        ui_message("STOP TO SAVE");
        return 1;
    }
    slot &= 3u;
    if (proj_write_slot(slot, proj_wire_u.raw, 0)) { ui_message("SAVE ERROR"); return 2; }
    if (proj_cur == slot)
        proj_cur = PROJ_NO_SLOT;
#if MELODEE_FLASH
    if (flash_ok) { ui_message("ERASED"); return 0; }
#endif
    ui_message("ERASED (RAM)");
    return 0;
}

static int project_restore_runtime(const project_t *input)
{
    momentary_restore();
    project_t *p = &proj_scratch;
    uint32_t i, k;
    if (!proj_ok(input)) return 1;
    if(designer_on() && designer_quit)designer_quit(0);   /* a loaded project owns the new sound */
    if (p != input) memcpy(p, input, sizeof *p);
    transport_req = 2;
    panic_req = (1u << NTRK) - 1u;
    fm1_irq_off();                                      /* the audio ISR must not see half a project */
    seq_stop();
    fx_clear_all();
    transport_req = 0;
    pattern_init();
    memcpy(recording, p->recording, sizeof recording);
    memcpy(drum_patch,p->drum,sizeof drum_patch);
    recording_reindex();
    chain_defaults(&chain_config);
    motion = p->motion;
    memset(motion_active, 0, sizeof motion_active);
    memset(motion_nactive, 0, sizeof motion_nactive);   /* (the patches come with the project) */
    motion_base_valid = 0;
    ui.song_row = 0;
    for (i = 0; i < G_COUNT; i++)
        if (i != G_SLOT && i != G_LOAD && i != G_SAVE)
            song.g[i] = global_reserved(i) ? 0 : (int16_t)clamp(p->g[i], GP[i].min, GP[i].max);
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        const proj_trk_t *s = &p->t[k];
        uint32_t e = s->engine % NENGINES;
        t->eng_req = (uint8_t)e;
        t->user = 0; t->user_native=0;                                    /* (no user preset slot is saved) */
        for (i = 0; i < P_COUNT; i++) {                 /* every value back inside its range */
            const param_desc_t *d = param_desc_of(e, i);
            t->p[i] = param_reserved(i) ? 0 : eng_extra_retired(e) && i >= P_E0 ? s->p[i] : (int16_t)clamp(s->p[i], d->min, d->max);
        }
        t->preset = eng_extra_retired(e) ? s->preset : (uint8_t)(ENGINES[e]->npresets ? (s->preset >= PROJ_DEF_KEEP ? 0u : s->preset) % ENGINES[e]->npresets : 0u);
        t->pattern = p->pattern[k];
        memcpy(t->step, s->step, sizeof t->step);
        proj_steps(t->step);
        pattern_commit(t);
        {   /* the project's own FM6 patch */
            uint8_t v[FP_SIZE + 1u];
            fm6_unpack(p->fm6[k], v);
            p5_patch[k]=p->p5[k];p5_ready[k]=1;
            uint32_t origin=p->user[k];
            if(origin && up_used(origin-1u) && up_engine(origin-1u)==e)t->user=(uint16_t)origin;
            if(e==ENGI_PROPHET && !t->preset && !t->user){
                for(uint32_t slot=0;slot<P5_FACTORY_N;slot++){p5_patch_t f;p5_factory(slot,&f);if(!memcmp(&p->p5[k],&f,sizeof f)){t->preset=(uint8_t)(slot+1u);break;}}
            }
            cz_patch[k] = p->cz[k];
            cz_track_accept(t);
            fm6_set_patch(k, v);
            memcpy(fm6_fn[k], p->fm6_fn_ok && fm6_fn_ok(p->fm6_fn[k]) ? p->fm6_fn[k] : FM6_FNDEF, FM6_NFN);
            if(!t->preset && !t->user && native_limit(e))for(uint32_t slot=0;slot<UP_SLOTS;slot++)if(native_used(e,slot)){
                const uint8_t *raw=native_raw(e,slot),*saved=e==ENGI_PROPHET?(const uint8_t *)&p->p5[k]:e==ENGI_CZ?p->cz[k].raw:p->fm6[k];
                if(!memcmp(raw,saved,native_size(e))){t->user=(uint16_t)(slot+1u);t->user_native=0;break;}
            }
        }
    }
    song.ntrk = (uint8_t)(p->parts - 1u < NTRK ? p->parts : NTRK);
    song.sel = (uint8_t)(p->sel < song.ntrk ? p->sel : 0u);
    fm1_irq_on();
    proj_name_get(proj_name, (const uint8_t *)p->name);
    proj_cur = PROJ_NO_SLOT;                            /* (project_load: its slot) */
    undo_clear();                                       /* (ui.c) the undo levels belong to the old project */
    undo_depth++;                                       /* and these loads take none */
    for (k = 0; k < NTRK; k++) {                        /* the power-on sounds: format 1 (tracks 2..4), old drums */
        track_t *t = &trk[k];
        int16_t keep[P_COUNT];
        if (p->t[k].preset == PROJ_DEF_SOUND) {
            apply_preset_to(t, TRK_DEF[k][1]);
            track_defaults_steps(t);
        } else if (p->t[k].preset == PROJ_DEF_KEEP) {   /* the steps, LEVEL PAN MUTE, LEN DIV SWING GATE kept */
            memcpy(keep, t->p, sizeof keep);
            fm1_irq_off();                           /* publish the legacy sound as one bounded parameter batch */
            proj_legacy_perc(t);                      /* track-owned parameters stay */
            for (i = P_AMODE; i <= P_TRANS; i++)        /* the drum part had no arp or scale */
                t->p[i] = TP[i].def;
            fm1_irq_on();
        }
        pat_sig[k] = ~steps_sig(t);                     /* a project's steps are the user's */
    }
    scale_share(TSEL);                                  /* (older projects: one scale per part, the selected wins) */
    undo_depth--;
    sync_reload = 1;
    ui.force = 1;
    ui_message("LOADED");
    return 0;
}
static void project_load(uint32_t slot)
{
    int n = proj_read_slot(slot);
    if (!n) { ui_message(project_old(slot) ? "OLD FORMAT" : "EMPTY SLOT"); return; }
    if (!project_restore_runtime(&proj_scratch)) {
        if (n == BANK_STORE_SIZE) bank_restore(proj_wire_u.raw);
        proj_cur = (uint8_t)(slot & 3u);
    }
}

/* settings + learned panel table: one flash object. The flash copy wins at
 * boot (the .noinit copies are garbage after a power-off). */
#include "settings_persist.c"

/* The template (SAVE > PROJECT, SLOT TMPL): the music without its patterns: the globals, each track's sound
 * (engine, preset, parameters, FM6 patch and function settings) and the selected track. SAVE there keeps it, LOAD makes a new project
 * from it (every pattern empty, LEN / DIV / SWG / GATE their defaults, no name, SLOT on a free slot), and power-on
 * loads it when BOOT is OFF or its slot empty. No flash sector is free: it follows the settings in their record
 * (set_rec, its size and magic last). P_COUNT, G_COUNT or FM6_PACKED changing changes it (the assert): convert. */
#define TMPL_MAGIC 0x4C4C5054u                    /* "TPLL": one TONE per FX slot; TPLK (two cuts) and earlier convert */
typedef struct {
    int16_t g[PROJ_GLOBALS];
    uint8_t sel, ntrk;                            /* (G_COUNT odd: the tracks start on a word); ntrk: the tracks in
                                                   * use, 0 (a template of before) all NTRK */
    struct { uint8_t engine, preset; int16_t p[P_COUNT]; } t[NTRK];
    uint8_t fm6[NTRK][FM6_PACKED];
    uint8_t fm6_fn[NTRK][FM6_NFN];                /* (TPL6) */
    cz_patch_t cz[NTRK];
    p5_patch_t p5[NTRK];
    drum_patch_t drum[NTRK];
    uint8_t master[20];                           /* the shared FX: 12 slot values, RETURN, MODE, the TONEs + 64,
                                                   * 3 zero (TPLK: the low cuts, the high cuts) */
    uint32_t size, magic;                         /* last: the record's end */
} tmpl_t;
typedef struct {
    int16_t g[PROJ_GLOBALS];
    uint8_t sel, rsv;                             /* (G_COUNT odd: the tracks start on a word) */
    struct { uint8_t engine, preset; int16_t p[108]; } t[NTRK];
    uint8_t fm6[NTRK][FM6_PACKED];
    uint8_t fm6_fn[NTRK][FM6_NFN];                /* (TPL6) */
    cz_patch_t cz[NTRK];
    p5_patch_t p5[NTRK];
    drum_patch_t drum[NTRK];
    uint32_t size, magic;                         /* last: the record's end */
} tmpl_fx_single_t;
typedef struct {
    int16_t g[PROJ_GLOBALS];
    uint8_t sel, rsv;                             /* (G_COUNT odd: the tracks start on a word) */
    struct { uint8_t engine, preset; int16_t p[104]; } t[NTRK];
    uint8_t fm6[NTRK][FM6_PACKED];
    uint8_t fm6_fn[NTRK][FM6_NFN];                /* (TPL6) */
    cz_patch_t cz[NTRK];
    p5_patch_t p5[NTRK];
    drum_patch_t drum[NTRK];
    uint32_t size, magic;                         /* last: the record's end */
} tmpl_fx_legacy_t;
_Static_assert(sizeof(tmpl_t) == 2u * PROJ_GLOBALS + 2u + NTRK * (2u + 2u * P_COUNT) + NTRK * (FM6_PACKED + FM6_NFN) + PROJ_CZ_BYTES + PROJ_P5_BYTES + NTRK*sizeof(drum_patch_t) + 20u + 8u,
               "template layout (P_COUNT, G_COUNT, FM6_PACKED: a conversion)");
static struct { persist_t p; tmpl_t t; } set_rec __attribute__((section(".pool")));   /* the settings record */
#define tmpl set_rec.t                                /* the template lives where the settings record stages it */
_Static_assert(sizeof set_rec == sizeof(persist_t) + sizeof(tmpl_t), "the template follows the settings");
static uint8_t tmpl_dirty;                        /* saved in RAM, not yet in flash (settings_poll) */
static uint8_t persist_old, tmpl_old;
static int template_old(void) { return persist_old || tmpl_old; }
static void settings_old_accept(void) { persist_old = tmpl_old = 0; }

static int template_used(void) { return tmpl.magic == TMPL_MAGIC && tmpl.size == sizeof tmpl; }

/* a stored template (len bytes, as tmpl_t is laid out) is today's: its size and magic, valid tones */
static uint32_t tmpl_param_count(uint32_t len)   /* (TPLJ, TPLI: a 16-byte master; TPLH none) */
{
    return len==sizeof(tmpl_t)?P_COUNT:len==sizeof(tmpl_t)+NTRK*2u*(PROJ_CUT_NP-P_COUNT)?PROJ_CUT_NP:
           len==sizeof(tmpl_t)-NTRK*2u*(P_COUNT-119u)-4u?119u:len==sizeof(tmpl_t)-NTRK*2u*(P_COUNT-117u)-4u?117u:
           len==sizeof(tmpl_t)-NTRK*2u*(P_COUNT-116u)-20u?116u:len==sizeof(tmpl_fx_single_t)?108u:len==sizeof(tmpl_fx_legacy_t)?104u:0u;
}
static uint32_t tmpl_blob_schema(const uint8_t *b,uint32_t len)
{
    uint32_t np=tmpl_param_count(len),size,magic;if(!np)return 0;
    memcpy(&size,b+len-8u,4);memcpy(&magic,b+len-4u,4);
    return size==len && magic==(np==P_COUNT?TMPL_MAGIC:np==PROJ_CUT_NP?0x4B4C5054u:np==119u?0x4A4C5054u:np==117u?0x494C5054u:np==116u?0x484C5054u:np==108u?0x474C5054u:0x464C5054u) ? np : 0u;
}
static int tmpl_blob_valid(const uint8_t *b,uint32_t len)
{
    uint32_t np=tmpl_blob_schema(b,len);
    if(!np || b[2u*PROJ_GLOBALS]>=NTRK)return 0;
    int32_t delta=(int32_t)(NTRK*2u)*((int32_t)P_COUNT-(int32_t)np);   /* (TPLK: more parameters, below 0) */
    for(uint32_t k=0;k<NTRK;k++){
        if(b[2u*PROJ_GLOBALS+2u+k*(2u+2u*np)]>=NENGINES)return 0;
        p5_patch_t p;memcpy(&p,b+((int32_t)__builtin_offsetof(tmpl_t,p5)-delta)+k*sizeof p,sizeof p);
        if(!cz_patch_valid(b+((int32_t)__builtin_offsetof(tmpl_t,cz)-delta)+k*sizeof(cz_patch_t)) || !p5_patch_valid(&p) ||
            !drum_patch_valid((const drum_patch_t *)(b+((int32_t)__builtin_offsetof(tmpl_t,drum)-delta)+k*sizeof(drum_patch_t))))return 0;
    }
    if(np>=117u){uint32_t off=(uint32_t)((int32_t)__builtin_offsetof(tmpl_t,master)-delta);
        for(uint32_t i=0;i<13u;i++)if(b[off+i]>GP[G_MFXTYPE+i].max)return 0;
        if(b[off+13u]>(np>=119u?1u:0u))return 0;
        if(np==P_COUNT){
            for(uint32_t i=0;i<FX_SLOTS;i++)if(b[off+14u+i]<64u-FX_TONE_MAX || b[off+14u+i]>64u+FX_TONE_MAX || b[off+17u+i])return 0;
        }else if(np==PROJ_CUT_NP){
            for(uint32_t i=14u;i<20u;i++)if(b[off+i]>127u)return 0;
        }else for(uint32_t i=14u;i<16u;i++)if(b[off+i])return 0;
    }
    return 1;
}
static int tmpl_take(const uint8_t *b,uint32_t len)
{
    memset(&tmpl,0,sizeof tmpl);if(!tmpl_blob_valid(b,len))return 0;
    uint32_t np=tmpl_param_count(len),off=2u*PROJ_GLOBALS+2u;
    memcpy(tmpl.g,b,sizeof tmpl.g);tmpl.sel=b[2u*PROJ_GLOBALS];tmpl.ntrk=b[2u*PROJ_GLOBALS+1u]<=NTRK?b[2u*PROJ_GLOBALS+1u]:0u;
    for(uint32_t k=0;k<NTRK;k++){
        int16_t def[P_COUNT],values[PROJ_CUT_NP];tmpl.t[k].engine=b[off++];tmpl.t[k].preset=b[off++];
        for(uint32_t i=0;i<P_COUNT;i++)def[i]=param_desc_of(tmpl.t[k].engine%NENGINES,i)->def;
        memcpy(values,b+off,np*2u);off+=np*2u;params_by_count(tmpl.t[k].p,values,np,def);
        if(np==PROJ_CUT_NP)for(uint32_t i=0;i<FX_SLOTS;i++)tmpl.t[k].p[P_FXTONE+i]=proj_tone_of_cuts(values[P_FXTONE+i],values[P_FXTONE+FX_SLOTS+i]);
    }
    uint32_t native=__builtin_offsetof(tmpl_t,master)-__builtin_offsetof(tmpl_t,fm6);memcpy(tmpl.fm6,b+off,native);
    for(uint32_t i=0;i<FX_SLOTS*FX_PARAMS;i++){uint32_t id=G_MFXTYPE+i;uint32_t v=np>=117u?b[off+native+i]:(uint32_t)GP[id].def;if(v>GP[id].max)return 0;tmpl.master[i]=(uint8_t)v;}
    tmpl.master[12]=np>=117u?b[off+native+12u]:(uint8_t)GP[G_FXRETURN].def;if(tmpl.master[12]>127u)return 0;
    tmpl.master[13]=np>=119u?b[off+native+13u]:0;
    for(uint32_t i=0;i<FX_SLOTS;i++)
        tmpl.master[14u+i]=np==P_COUNT?b[off+native+14u+i]:(uint8_t)(64+(np==PROJ_CUT_NP?proj_tone_of_cuts(b[off+native+14u+i],b[off+native+17u+i]):0));
    tmpl.magic=TMPL_MAGIC;tmpl.size=sizeof tmpl;
    for(uint32_t k=0;k<NTRK;k++)if(!fm6_fn_ok(tmpl.fm6_fn[k]))memcpy(tmpl.fm6_fn[k],FM6_FNDEF,FM6_NFN);
    return 1;
}

/* CLK TUNE MIDI ROUT: the device keeps them as last used (settings ext.glo), not only the projects */
static const uint8_t GLO_KEPT[4] = {G_CLOCK, G_TUNE, G_MIDI, G_ROUTE};
static void glo_restore(void)                     /* power-on, before the BOOT project or the template (theirs win) */
{
    uint32_t k;
    for (k = 0; k < 4u; k++)
        song.g[GLO_KEPT[k]] = (int16_t)clamp(settings_glo[k], GP[GLO_KEPT[k]].min, GP[GLO_KEPT[k]].max);
}
/* main loop: a kept value changed (a knob, the editor, a project load): saved 1.5 s after the last change, stopped */
static void glo_poll(void)
{
    static uint32_t t;
    static uint8_t dirty;
    uint32_t k;
    for (k = 0; k < 4u; k++)
        if (song.g[GLO_KEPT[k]] != settings_glo[k]) {
            settings_glo[k] = song.g[GLO_KEPT[k]];
            dirty = 1;
            t = fm1_ms;
        }
    if (dirty && fm1_ms - t > 1500u && !song.playing && !seq_counting() && !momentary.active) {
        dirty = 0;
        settings_save();
    }
}

/* the music without its patterns -> the template, saved with the settings */
static void template_save(void)
{
    momentary_restore();
    uint32_t i, k;
    if (transport_busy()) {
        ui_message("STOP TO SAVE");
        return;
    }
    if (template_old()) { confirm_open(CF_OLD_TEMPLATE, 0); return; }
    memset(&tmpl, 0, sizeof tmpl);
    for (i = 0; i < PROJ_GLOBALS; i++)
        tmpl.g[i] = song.g[i];
    for(i=0;i<FX_SLOTS*FX_PARAMS;i++)tmpl.master[i]=(uint8_t)song.g[G_MFXTYPE+i];
    tmpl.master[12]=(uint8_t)song.g[G_FXRETURN];
    tmpl.master[13]=(uint8_t)song.g[G_FXMODE];
    for(i=0;i<FX_SLOTS;i++)tmpl.master[14u+i]=(uint8_t)(clamp(song.g[G_MFXTONE+i],-FX_TONE_MAX,FX_TONE_MAX)+64);
    tmpl.sel = song.sel;
    tmpl.ntrk = (uint8_t)trk_n();
    for (k = 0; k < NTRK; k++) {
        tmpl.t[k].engine = trk[k].eng_req;
        tmpl.t[k].preset = trk[k].preset;
        for (i = 0; i < P_COUNT; i++)
            tmpl.t[k].p[i] = motion_base_value(&trk[k], i);
        tmpl.cz[k] = cz_patch[k];
        tmpl.p5[k] = *p5_patch_of(&trk[k]);
        tmpl.drum[k]=drum_patch[k];
        fm6_pack(fm6_patch[k], tmpl.fm6[k]);
        memcpy(tmpl.fm6_fn[k], fm6_fn[k], FM6_NFN);
    }
    tmpl.size = sizeof tmpl;
    tmpl.magic = TMPL_MAGIC;
    tmpl_dirty = 1;
#if MELODEE_FLASH
    if (flash_ok) {
        settings_save();                              /* (stopped: written now) */
        ui_message(tmpl_dirty ? "SAVE ERROR" : "TEMPLATE SAVED");
        return;
    }
#endif
    ui_message("TEMPLATE SAVED (RAM)");
}

/* the first empty project slot (0..3), else 0 */
static uint32_t project_free_slot(void)
{
    uint32_t i;
    for (i = 0; i < 4u; i++)
        if (!project_used(i) && !project_old(i))
            return i;
    return 0;
}

/* a new project from the template: its sounds and globals, every pattern empty; SLOT on a free slot */
static void template_load(void)
{
    momentary_restore();
    project_t *p = &proj_scratch;
    uint32_t i, k;
    if (!template_used()) {
        ui_message("NO TEMPLATE");
        return;
    }
    memset(p, 0, sizeof *p);
    p->magic = PROJ_MAGIC;
    p->size = sizeof *p;
    for (i = 0; i < PROJ_GLOBALS; i++)
        p->g[i] = tmpl.g[i];
    for(i=0;i<FX_SLOTS*FX_PARAMS;i++)p->g[G_MFXTYPE+i]=tmpl.master[i];
    p->g[G_FXRETURN]=tmpl.master[12];
    p->g[G_FXMODE]=tmpl.master[13];
    for(i=0;i<FX_SLOTS;i++)p->g[G_MFXTONE+i]=(int16_t)(tmpl.master[14u+i]-64);
    p->sel = tmpl.sel;
    p->parts = tmpl.ntrk ? tmpl.ntrk : NPART;
    p->phys = PROJ_PHYS;
    chain_defaults(&p->chain);
    for (k = 0; k < NTRK; k++) {
        p->t[k].engine = tmpl.t[k].engine;
        p->t[k].preset = tmpl.t[k].preset;
        for (i = 0; i < P_COUNT; i++)
            p->t[k].p[i] = tmpl.t[k].p[i];
        for (i = 0; i < 4u; i++)                      /* LEN DIV SWG GATE: as a new track's */
            p->t[k].p[P_SLEN + i] = TP[P_SLEN + i].def;
        p->cz[k] = tmpl.cz[k];
        p->p5[k] = tmpl.p5[k]; p->drum[k]=tmpl.drum[k];
        memcpy(p->fm6[k], tmpl.fm6[k], FM6_PACKED);
        memcpy(p->fm6_fn[k], tmpl.fm6_fn[k], FM6_NFN);
    }
    p->fm6_fn_ok = 1;
    p->sum = proj_sum(p);
    if (project_restore_runtime(p))
        return;
    for (k = 0; k < NTRK; k++) {
        track_defaults_steps(&trk[k]);                /* every pattern empty */
        pat_last[k] = 0;
        pat_sig[k] = steps_sig(&trk[k]);
    }
    proj_cur = PROJ_NO_SLOT;
    song.g[G_SLOT] = (int16_t)(project_free_slot() + 1u);
    ui_message("TEMPLATE LOADED");
}

/* NEW SONG (ui_new.c): the tracks a new song starts with: the template's (one of before: all), else one */
static uint32_t template_tracks(void) { return !template_used() ? 1u : tmpl.ntrk ? tmpl.ntrk : NTRK; }
/* NEW SONG (ui_new.c): the template's key and tempo (track 1's ROOT, the song's SCALE, BPM), when there is one */
static void template_key(uint32_t *root, uint32_t *scale, int32_t *bpm)
{
    if (!template_used())
        return;
    *root = (uint32_t)tmpl.t[0].p[P_ROOT];
    *scale = (uint32_t)tmpl.t[0].p[P_SCALE];
    *bpm = tmpl.g[G_BPM];
}
/* .. no template: the power-on sounds (melodee_init's), every pattern, the song and the motion empty, the globals at
 * their defaults but CLK TUNE MIDI ROUT (the device's); a new project: no slot, no name, SLOT on a free one */
static void project_new_blank(void)
{
    uint32_t i, k;
    int16_t kept[4];
    momentary_restore();
    for (k = 0; k < 4u; k++)
        kept[k] = song.g[GLO_KEPT[k]];
    fm1_irq_off();
    chain_defaults(&chain_config);
    pattern_init();
    fm1_irq_on();
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    for (k = 0; k < 4u; k++)
        song.g[GLO_KEPT[k]] = kept[k];
    undo_depth++;
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        track_defaults(t);
        set_engine_of(t, TRK_DEF[k][0]);
        apply_preset_to(t, TRK_DEF[k][1]);
        track_defaults_steps(t);
        pat_last[k] = 0;
        pat_sig[k] = steps_sig(t);
    }
    undo_depth--;
    undo_clear();
    proj_cur = PROJ_NO_SLOT;
    proj_name[0] = 0;
    song.sel = 0;
    song.ntrk = 1;                                      /* (one track to start with: Stage's sheet adds more) */
    song.g[G_SLOT] = (int16_t)(project_free_slot() + 1u);
}
/* the music differs from its slot (none: it holds notes): a new song asks first. Packs the project (proj_wire_u) */
static int project_dirty(void)
{
    uint32_t k;
    if (proj_cur < 4u) {
        project_capture(&proj_scratch);
        proj_wire_gen++;
        return !bank_pack(proj_wire_u.raw, &proj_scratch, 1) ||
               proj_hash(proj_wire_u.raw, BANK_STORE_SIZE) != proj_saved_hash[proj_cur];
    }
    for (k = 0; k < NTRK; k++)
        if (!seq_is_empty(&trk[k]) || notes_have_recording(&trk[k]))
            return 1;
    return 0;
}

/* power-on, after melodee_init: the BOOT project (SAVE > PROJECT KNOB 2) in place of the default sounds, SLOT on it
 * so SAVE writes back there. BOOT OFF or an empty BOOT slot: a new project, from the template if one is saved, SLOT
 * on a free slot. A boot that crashed or hung within 30 s (bootguard) skips the project or the template: one that
 * cannot load does not lock the FM-1 out */
static void project_boot(void)
{
    if (settings_boot && project_used(settings_boot - 1u)) {
        song.g[G_SLOT] = (int16_t)settings_boot;
        if (bootguard.failed)
            ui_message("BOOT PROJECT SKIPPED");
        else
            project_load(settings_boot - 1u);
        return;
    }
    song.ntrk = 1;                                      /* the power-on sounds: one track (theirs: the template's) */
    if (template_used()) {
        if (bootguard.failed)
            ui_message("TEMPLATE SKIPPED");
        else
            template_load();
    }
    song.g[G_SLOT] = (int16_t)(project_free_slot() + 1u);
}
#include "tracks.c"                               /* adding and removing tracks */
#if MELODEE_FLASH && MELODEE_SLICE
#include "slice_store.c"                          /* SLICE's MAN slices, kept in the user slots */
#endif
#if MELODEE_FLASH
static persist_t persist_saved;
static uint8_t persist_pending;                 /* 1 requested, 2 waiting after a flash error */
static uint8_t persist_repair;                  /* boot repair must retry even when RAM equals defaults */
static uint32_t persist_retry_ms;
#endif

static void persist_boot(void)                    /* before settings_init / panel_init */
{
#if MELODEE_FLASH
    persist_t p;
    boot_work.state = 0;
    uint32_t f = irq_save();
    flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;       /* the expected 1 MiB part, else stay RAM-only */
    irq_restore(f);
    if (!flash_ok)
        return;
    fl_plain_window_init();                        /* flash above 0x93000 reads as plaintext through XIP
                                                    * (user sample sets are played from there) */
#if MELODEE_SLICE
    slc_store_boot();                              /* (the scans read each slot's stored slices) */
#endif
    {   /* the settings, then a template if one follows them */
        int n = st_load(OBJ_SETTINGS, &proj_wire_u, sizeof proj_wire_u);
        int tail_ok = n == (int)sizeof p || (n > (int)sizeof p &&
            tmpl_blob_schema((const uint8_t *)&proj_wire_u + sizeof p, (uint32_t)n - sizeof p));
        int owned = 0, foreign, future_owner;
        persist_t original;
        memset(&p, 0, sizeof p);
        if (n >= (int)sizeof p) memcpy(&p, &proj_wire_u, sizeof p);
        original = p;
        owned = p.magic == PERSIST_MAGIC && n >= (int)sizeof p && settings_owned(&p);
        future_owner = p.magic == PERSIST_MAGIC && n >= (int)sizeof p &&
            !memcmp(p.favorites.factory[1] + 28, SETTINGS_OWNER, 3) && !owned;
        /* Never truncate a foreign extension into our ext block. Legacy PER5
         * is shared with forks, so both settings and the whole tail must match. */
        foreign = future_owner || (!owned && !(settings_legacy_valid(&p) && tail_ok));
        persist_old = tmpl_old = 0;
        persist_pending = persist_repair = 0;
        memset(&tmpl, 0, sizeof tmpl);
        if (!foreign && n > (int)sizeof set_rec.p) {
            tmpl_old = (uint8_t)!tmpl_take((const uint8_t *)&proj_wire_u + sizeof set_rec.p, (uint32_t)n - sizeof set_rec.p);
        }
        if (foreign) {
            memset(&p, 0, sizeof p);
            p.magic = PERSIST_MAGIC;
            p.palette = palette_to_stored(UI_DEFAULT_INDEX);
            p.panel = PANEL_DEFAULT;
            for (uint32_t k = 0; k < 4u; k++) p.ext.glo[k] = GP[GLO_KEPT[k]].def;
        }
        settings_import(&p, sizeof p);            /* replaces retained RAM too, before USB enumeration */
        panel_init(); settings_init();
        for (uint32_t k = 0; k < 4u; k++) {
            const param_desc_t *d = &GP[GLO_KEPT[k]];
            if (settings_glo[k] < d->min || settings_glo[k] > d->max) settings_glo[k] = d->def;
        }
        settings_export(&p);
        persist_saved = p;
        persist_old = (uint8_t)future_owner;       /* an unknown Melodee schema is protected, not foreign */
        /* Unknown owned templates stay protected. Otherwise commit defaults
         * or the ownership/normalization repair once through settings' A/B save. */
        if (!template_old() && (foreign || memcmp(&p, &original, sizeof p))) {
            BOOT_WORK_BEGIN(foreign ? "RESETTING SETTINGS" : "CHECKING SETTINGS", 1);
            set_rec.p = p;
            if (st_save(OBJ_SETTINGS, &set_rec, sizeof p + (template_used() ? sizeof tmpl : 0u))) {
                persist_repair = 1;
                persist_pending = 2;
                persist_retry_ms = fm1_ms;
                BOOT_WORK_END(1);
            } else {
                tmpl_dirty = 0;
                BOOT_WORK_PROGRESS(1);
                BOOT_WORK_END(0);
            }
        }
    }
    {   /* projects: fill empty RAM slots from flash, so the slot list is right after power-on */
        uint32_t i;
        for (i = 0; i < 4u; i++)
            if (!project_used(i))
                proj_fetch(i);
    }
    settings_init();                               /* maintenance uses the saved palette */
    up_boot();                                     /* user presets */
#endif
}

static int project_used(uint32_t slot)
{
#if !MELODEE_FLASH
    slot &= 3u;
    int valid = bank_valid(proj_bank_slot[slot], BANK_STORE_SIZE) &&
        !memcmp(proj_bank_slot[slot] + 8u, &proj_slot[slot], sizeof proj_slot[slot]);
    if (!valid) valid = proj_import(&proj_scratch, &proj_slot[slot], sizeof proj_slot[slot]);
    proj_meta[slot].used = (uint8_t)valid;
    if (valid) proj_name_get(proj_meta[slot].name, (const uint8_t *)proj_scratch.name);
#endif
    return proj_meta[slot & 3u].used;
}

/* Main loop only: no flash access or copies when the ISR changes rows. */
static uint32_t chain_prepare(void)
{
    if (transport_busy()) return 2;
    if (!chain_valid(&chain_config) || !chain_config.count) return 1;
    for (uint32_t i = 0; i < chain_config.count; i++)
        for (uint32_t k = 0; k < NTRK; k++) if (chain_patterns[i][k] >= NPAT) return 1;
    uint32_t f = motion_guard();
    if (transport_busy()) { motion_unguard(f); return 2; }
    for (uint32_t k = 0; k < NTRK; k++) { pattern_commit(&trk[k]); trk[k].pattern_next = 0xff; }
    chain.config = chain_config;
    RING_PUBLISH(); chain.armed = 1; transport_req = 1;
    motion_unguard(f);
    return 0;
}

static void settings_poll(void)
{
#if MELODEE_FLASH
    persist_t p;
#if MELODEE_SLICE
    slc_store_poll();                              /* SLICE's slices edited on the SLICES page */
#endif
    up_favorite_poll();
    if (!persist_pending || !flash_ok || transport_busy() ||
        (persist_pending == 2u && (uint32_t)(fm1_ms - persist_retry_ms) < 1000u))
        return;
#ifdef FM1_IRQ_TARGET
    /* Live preference edits queue their flash save until audio is idle. */
    for(uint32_t k=0;k<NPART;k++)for(uint32_t i=0;i<NVOICE;i++)if(trk[k].v[i].active)return;
#if MELODEE_USB_AUDIO
    if(ua.play_alt || ua.cap_alt)return;
#endif
#endif
    if (template_old()) {
        if (!ui.confirm && !ui.name_active) {
            persist_pending = 0;
            confirm_open(CF_OLD_SETTINGS, 0);
        }
        return;
    }
    p = persist_saved;
    settings_export(&p);
    if (!persist_repair && !memcmp(&p, &persist_saved, sizeof p) && !tmpl_dirty) {
        persist_pending = 0;
        return;                                    /* unchanged: no erase cycle */
    }
    set_rec.p = p;                                 /* the record: the settings, the template (tmpl) after them */
    uint8_t repairing = persist_repair;
    if (repairing) BOOT_WORK_BEGIN("RESETTING SETTINGS", 1);
    if (st_save(OBJ_SETTINGS, &set_rec, sizeof set_rec.p + (template_used() ? sizeof set_rec.t : 0u)) == 0) {
        persist_saved = p;
        persist_pending = 0;
        persist_repair = 0;
        tmpl_dirty = 0;
        if (repairing) { BOOT_WORK_PROGRESS(1); BOOT_WORK_END(0); }
    } else {
        persist_pending = 2;
        persist_retry_ms = fm1_ms;
        if (repairing) BOOT_WORK_END(1);
    }
#endif
}

static void settings_save(void)
{
#if MELODEE_FLASH
    persist_pending = 1;
#endif
    settings_poll();
}
static int shared_preferences_save(void)
{
    settings_save();
#if MELODEE_FLASH
    return persist_pending || persist_old || tmpl_old;
#else
    return 0;
#endif
}


#if MELODEE_FLASH
_Static_assert(sizeof(persist_t) <= ST_PAYLOAD_MAX, "settings do not fit one flash sector");
#endif
#endif /* PROJ_HOST */
