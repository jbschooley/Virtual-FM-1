/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* User presets (editor protocol v2, cmds 16-21; the SAVE > USER page): 64
 * slots in four storage objects (the first two at 0xDC000..0xDFFFF,
 * two more at 0xE5000..0xE8FFF), 16 records each. Only names and indexes
 * stay in RAM; one bank is read on demand when loading or editing. A record: engine, name, the instrument parameters, a 16-step
 * pattern (factory PATTERNS[] format). FM6 voices live beside their records
 * (up_fm6.c). Loading one loads the sound only; its pattern
 * is offered by SEQ > PATTERNS ("U07", up_pat_load). The format is unchanged.
 *
 * Versions: a bank whose magic, record size or slot count differ reads as
 * empty; so does a record with another layout version. A record keeps np =
 * the P_COUNT it was stored with; when that differs from today's it is
 * mapped by count: its last 8 values are P_E0..P_E7, the first np - 8 are
 * P_LEVEL.. in order, and parameters it does not have take their defaults.
 * That holds as long as common parameters are only ever added just before
 * P_E0 (else bump UP_VER and translate). Version 1 (before 1.0) has the
 * same layout; only its PHYS MODEL 2 meant DUST, which up_values loads as
 * MODAL bowed (eng_phys.c phys_legacy). A record of PHYS MODEL 4 (DRUM, version 2
 * until the kit became the DRUM engine) is that engine: up_migrate rewrites it
 * in the RAM mirror when a bank is read and when UP_PUT sends one (core.h
 * drum_from_phys); flash keeps the old record until its bank is written again,
 * which stores the new one. No version bump: today's PHYS has no MODEL 4, so
 * such a record cannot be anything else, and older firmware keeps reading the bank.
 * Version 3 (UP_VER_GRID, since the DRUM grid) is the same record with a drum grid as its pattern: note[i] is
 * step i's lane hits (bit l = lane l), flags[i] their accents. A DRUM track whose first 16 steps strike a
 * lane is stored so (its notes on their lanes); every other sound as version 2, which older firmware reads.
 * Older firmware shows a version 3 record as empty and keeps its bytes.
 * A record of engine 1 (DIGITAL, retired in 1.0) stays as it is (UP_PUT takes it too): its values are DIGITAL's,
 * and every load converts them to an FM6 sound with its own patch (ui.c fm4_apply, fm4_convert.c); lists count it
 * with FM6's (up_engine).
 *
 * With -DUP_HOST (host test) only the part above #ifndef UP_HOST is built;
 * it needs nothing but core.h. */
#define UP_PER_BANK 16u
#include "cz_patch.h"
#include "cz_legacy.h"
#define UP_VER_CZ 8u                            /* native CZ-1 bytes in the 144-byte payload */
#define UP_PMAX 72u                              /* room for P_COUNT to grow */
#define UP_USED 0xA5u
#define UP_VER 4u                                /* 2 since 1.0; 1 is read too (PHYS MODEL 2 was DUST) */
#define UP_VER_GRID 5u                           /* 2 with a drum grid as the pattern (see the top) */
#define UP_BANK_MAGIC 0x31425055u                /* "UPB1" */
typedef struct {
    uint8_t used, ver, engine, np;               /* UP_USED, UP_VER, engine, P_COUNT when stored */
    char name[12];                               /* ASCII 32..126, 0-padded (no 0 when 12 long) */
    union { int16_t p[UP_PMAX]; uint8_t packed[UP_PMAX * 2u]; };
    uint8_t note[16], flags[16];                 /* note 0 = rest; flags 1 accent, 2 slide, 4 tie (UP_VER_GRID:
                                                  * lane hits, their accents) */
    uint8_t cz_extra[46]; /* read compatibility with next's v6/v7 CZ records */
} up_rec_t;
typedef struct {
    uint32_t magic;
    uint16_t rsize, nslot;
    up_rec_t r[UP_PER_BANK];
} up_bank_t;
_Static_assert(sizeof(up_rec_t) == 238, "user preset record layout");
_Static_assert(P_COUNT <= UP_PMAX * 2u && P_COUNT < 128, "user preset record: P_COUNT");
#if MELODEE_FLASH && !defined(UP_HOST)
static up_bank_t up_cache __attribute__((section(".pool")));
static uint8_t up_cached = 255;
static struct { uint8_t ready, used, engine; char name[13]; } up_meta[UP_SLOTS];
static up_bank_t *up_cache_bank(uint32_t b);
static void up_cache_reset(void) { up_cached=255; memset(up_meta,0,sizeof up_meta); }
#else
static up_bank_t up_banks_host[UP_SLOTS / UP_PER_BANK] __attribute__((section(".pool")));
static up_bank_t *up_cache_bank(uint32_t b) { return &up_banks_host[b]; }
static void up_cache_reset(void) { memset(up_banks_host,0,sizeof up_banks_host); }
#endif

#if !defined(UP_HOST) && MELODEE_FLASH
static uint32_t up_obj(uint32_t b) { return b < 2u ? OBJ_UPRESET0 + b : OBJ_UPRESET_EXT0 + b - 2u; }
#endif

#if !defined(UP_HOST) && MELODEE_FLASH
/* Extension banks omit the redundant eight-byte UPB1 header on flash; the object
 * type identifies their fixed record layout. Backups retain the ordinary header. */
static int up_save_bank(uint32_t b) {
    return st_save(up_obj(b), b < 2u ? (const void *)up_cache_bank(b) : (const void *)up_cache_bank(b)->r,
                   b < 2u ? sizeof (*up_cache_bank(b)) : sizeof up_cache_bank(b)->r);
}
#endif

static up_rec_t *up_rec(uint32_t k) { return &up_cache_bank(k / UP_PER_BANK)->r[k % UP_PER_BANK]; }

#define UP_BANK_LEGACY_SIZE (8u+16u*192u)
static int up_legacy_cz(const up_rec_t *r){return r->engine==14u && (r->ver==6u || r->ver==7u);}
static int up_native_cz(const up_rec_t *r){return r->engine==ENGI_CZ && (r->ver==6u || r->ver==UP_VER_CZ);}
static uint32_t up_legacy_bits(const up_rec_t *r,uint32_t pos,uint32_t n)
{
    uint32_t v=0;for(uint32_t i=0;i<n;i++){uint32_t k=(pos+i)>>3;uint8_t b=k<144u?r->packed[k]:r->cz_extra[k-144u];v|=((b>>((pos+i)&7u))&1u)<<i;}return v;
}
static uint32_t up_legacy_width(uint32_t k){uint32_t w=0,x=k<LCZ_NP?LCZ_MAX[k]:127u;do{w++;x>>=1;}while(x);return w;}
static int up_cz_raw(const up_rec_t *r,uint8_t *raw)
{
    if(up_native_cz(r)){memcpy(raw,r->packed,CZ_BYTES);return cz_patch_valid(raw);}
    if(!up_legacy_cz(r))return 0;
    uint8_t p[LCZ_PACKED];uint32_t n=r->ver==6u?LCZ_OLD_PACKED:LCZ_PACKED,pos=0;
    for(uint32_t k=0;k<n;k++){uint32_t w=k<(r->ver==6u?LCZ_OLD_NP:LCZ_NP)?up_legacy_width(k):7u;p[k]=(uint8_t)up_legacy_bits(r,pos,w);pos+=w;}
    return cz_legacy_tone(raw,p,n);
}
static int up_bank_shape(uint32_t len,uint32_t rsize){return (len==sizeof(up_bank_t) && rsize==sizeof(up_rec_t)) || (len==UP_BANK_LEGACY_SIZE && rsize==192u);}
static int up_valid(const up_rec_t *r)
{
    if (r->used == UP_USED && (up_native_cz(r) || up_legacy_cz(r))) { uint8_t raw[CZ_BYTES];return r->np==P_COUNT && r->name[0] && up_cz_raw(r,raw); }
    if (!(r->used == UP_USED && r->ver >= 1u && r->ver <= UP_VER_GRID && r->engine < USER_GENERAL &&
          r->np >= 8u && r->np <= (r->ver >= 4u ? UP_PMAX * 2u : UP_PMAX) && r->name[0])) return 0;
    /* Pre-1.0 Melodee reused UPB1/version 1, but its MPC/chord ids and engine 9 mean different things.
     * Fresh-start policy: preserve the bytes while treating these fork layouts as empty. */
    if (r->ver == 1u && (r->np == 58u || r->np == 62u)) return 0;
    if (r->ver >= 4u) for (uint32_t i = 0; i < r->np; i++) if (r->packed[i] > 191u) return 0;
    return 1;
}

static int up_used(uint32_t k)
{
    if(k>=UP_SLOTS)return 0;
#if MELODEE_FLASH && !defined(UP_HOST)
    if(up_cached != k/UP_PER_BANK && up_meta[k].ready)return up_meta[k].used;
#endif
    return up_valid(up_rec(k));
}

static int up_grid(const up_rec_t *r) { return r->ver == 3u || r->ver == UP_VER_GRID; }
static int16_t up_value(const up_rec_t *r, uint32_t k) { if(up_legacy_cz(r)){uint32_t pos=r->ver==6u?977u:983u;for(uint32_t i=0;i<k;i++)pos+=LCZ_PRESET_WIDTH[i];return (int16_t)up_legacy_bits(r,pos,LCZ_PRESET_WIDTH[k])+LCZ_PRESET_MIN[k];} return r->ver >= 4u ? (int16_t)r->packed[k] - 64 : r->p[k]; }
static void up_set_value(up_rec_t *r, uint32_t k, int16_t v)
{
    if (r->ver >= 4u) r->packed[k] = (uint8_t)(v + 64); else r->p[k] = v;
}
/* Legacy PHYS drums map through decoded parameters, not their disk representation. */
static void up_migrate(up_rec_t *r)
{
    int16_t e[8]; uint32_t k;
    if (!up_valid(r)) return;
    if (up_native_cz(r) || up_legacy_cz(r)) return;
    for (k = 0; k < 8u; k++) e[k] = up_value(r, r->np - 8u + k);
    if (drum_from_phys(r->engine, e)) {
        r->engine = ENGI_DRUM;
        for (k = 0; k < 8u; k++) up_set_value(r, r->np - 8u + k, e[k]);
    }
}

static void up_bank_check(uint32_t b, int len)  /* after loading bank b (len bytes, -1 = none): wrong shape -> empty */
{
    up_bank_t *bk = up_cache_bank(b);
    uint32_t i;
    if (!up_bank_shape((uint32_t)len,bk->rsize) || bk->magic != UP_BANK_MAGIC ||
        bk->nslot != UP_PER_BANK)
        memset(bk, 0, sizeof *bk);
    if(len==UP_BANK_LEGACY_SIZE){for(i=UP_PER_BANK;i-- > 0;){uint8_t copy[192];memcpy(copy,(uint8_t *)bk+8u+i*192u,192);memset(&bk->r[i],0,sizeof(up_rec_t));memcpy(&bk->r[i],copy,192);}bk->rsize=sizeof(up_rec_t);}
    for (i = 0; i < UP_PER_BANK; i++)
        if (up_valid(&bk->r[i]))
            up_migrate(&bk->r[i]);
}
#if MELODEE_FLASH && !defined(UP_HOST)
static void up_cache_index(uint32_t b)
{
    for(uint32_t j=0;j<UP_PER_BANK;j++){
        uint32_t k=b*UP_PER_BANK+j; const up_rec_t *r=&up_cache.r[j];
        up_meta[k].ready=1;up_meta[k].used=(uint8_t)up_valid(r);
        up_meta[k].engine=up_native_cz(r)||up_legacy_cz(r)?ENGI_CZ:eng_sound_idx(r->engine);
        uint32_t n=0;
        for(;n<12u && r->name[n];n++)up_meta[k].name[n]=r->name[n]>='a' && r->name[n]<='z'?(char)(r->name[n]-32):r->name[n];
        up_meta[k].name[n]=0;
    }
}
static up_bank_t *up_cache_bank(uint32_t b)
{
    if(up_cached != b){
        if(up_cached<UP_SLOTS/UP_PER_BANK)up_cache_index(up_cached);
        up_cached=(uint8_t)b;
        memset(&up_cache,0,sizeof up_cache);
        int n=flash_ok?st_load(up_obj(b),b<2u?(void *)&up_cache:(void *)up_cache.r,b<2u?sizeof up_cache:sizeof up_cache.r):-1;
        if(b>=2u && n==sizeof up_cache.r){up_cache.magic=UP_BANK_MAGIC;up_cache.rsize=sizeof(up_rec_t);up_cache.nslot=UP_PER_BANK;n=sizeof up_cache;}
        up_bank_check(b,n);up_cache_index(b);
    }
    return &up_cache;
}
#endif


/* the record's values in today's P_* order (mapped by count, see above); def = the defaults */
static void up_params(const up_rec_t *r, int16_t *out, const int16_t *def)
{
    if (up_native_cz(r)) { memcpy(out, def, P_COUNT * sizeof *out); out[P_E7] = CZ_NATIVE; return; }
    int16_t values[UP_PMAX * 2u];
    for (uint32_t i = 0; i < r->np && i < NELEM(values); i++) values[i] = up_value(r, i);
    params_by_count(out, values, r->np, def);
    if(up_legacy_cz(r)){for(uint32_t i=P_E0;i<P_COUNT;i++)out[i]=def[i];out[P_E7]=CZ_NATIVE;}
}

static int up_name_ok(const uint8_t *s, uint32_t n)   /* 1..12 printable ASCII */
{
    uint32_t i;
    if (!n || n > 12u)
        return 0;
    for (i = 0; i < n; i++)
        if (s[i] < 32u || s[i] > 126u)
            return 0;
    return 1;
}

static void up_name(uint32_t k, char *b)       /* upper case, 0-terminated: b holds 13 */
{
#if MELODEE_FLASH && !defined(UP_HOST)
    if(up_cached != k/UP_PER_BANK && up_meta[k].ready){memcpy(b,up_meta[k].name,13);return;}
#endif
    const up_rec_t *r = up_rec(k);
    uint32_t i;
    for (i = 0; i < 12u && r->name[i]; i++)
        b[i] = r->name[i] >= 'a' && r->name[i] <= 'z' ? (char)(r->name[i] - 32) : r->name[i];
    b[i] = 0;
}

static void up_pat_norm(uint8_t *note, uint8_t *flags)   /* tie: no note; rest: no flags */
{
    *note &= 127u;
    if (*flags & 4u) {
        *note = 0;
        *flags = 4;
    } else {
        *flags = *note ? (uint8_t)(*flags & (SF_ACCENT | SF_SLIDE)) : 0u;
    }
}

static void up_pat_from(up_rec_t *r, const step_t *st)   /* the first 16 steps -> the pattern */
{
    uint32_t i;
    for (i = 0; i < 16u; i++) {
        r->note[i] = st[i].time == ST_NOTE && st[i].n ? st[i].note[0] : 0u;
        r->flags[i] = st[i].time == ST_TIE ? 4u : st[i].flags;
        up_pat_norm(&r->note[i], &r->flags[i]);
    }
}

static int up_pat_empty(const up_rec_t *r)
{
    uint32_t i;
    for (i = 0; i < 16u; i++)
        if (r->note[i])
            return 0;
    return 1;
}

/* UP_PUT arguments: slot, engine, name, P_COUNT x v14, 16 x (note, flags) [, kind, 16 x hi] -> *r (values not
 * yet clamped); 0 ok, 1 bad arguments. *slot gets the slot byte when there is one. kind 1 (and its 16 bytes):
 * a drum grid, the pairs the low 7 bits of each step's hits and accents, hi bit 0 / 1 their bit 7 (lane 8) */
static int up_parse(const uint8_t *a, uint32_t na, up_rec_t *r, uint32_t *slot)
{
    uint32_t i, n, k, end;
    if (na < 3u)
        return 1;
    *slot = a[0];
    for (n = 0; 2u + n < na && a[2 + n]; n++)
        ;
    k = 3u + n;                                  /* after the name's 0 */
    end = k + 2u * P_COUNT + 32u;
    if (a[0] >= UP_SLOTS || a[1] >= NENGINES || 2u + n >= na || !up_name_ok(a + 2, n) ||
        (na != end && (na != end + 17u || a[end] > 1u)))
        return 1;
    up_rec_t staged, *target = r;
    r = &staged;
    memset(r, 0, sizeof *r);
    r->used = UP_USED;
    r->ver = UP_VER;
    r->engine = a[1];
    if(r->engine==ENGI_PROPHET)return 1;
    r->np = P_COUNT;
    for (i = 0; i < n; i++)
        r->name[i] = (char)a[2 + i];
    for (i = 0; i < P_COUNT; i++, k += 2u)
        {
            int32_t value = (int32_t)((a[k] & 127u) | (a[k + 1] & 127u) << 7) - 8192;
            if (value < -64 || value > 127) return 1;
            up_set_value(r, i, (int16_t)value);
        }
    if (na >= k + 33u + 16u && a[k + 32u] == 1u) {    /* a drum grid */
        r->ver = UP_VER_GRID;
        for (i = 0; i < 16u; i++) {
            uint32_t hi = a[k + 33u + i];
            r->note[i] = (uint8_t)((a[k + 2u * i] & 127u) | (hi & 1u) << 7);
            r->flags[i] = (uint8_t)(((a[k + 2u * i + 1u] & 127u) | (hi & 2u) << 6) & r->note[i]);
        }
        *target = *r;
        return 0;
    }
    for (i = 0; i < 16u; i++, k += 2u) {
        r->note[i] = a[k];
        r->flags[i] = a[k + 1];
        up_pat_norm(&r->note[i], &r->flags[i]);
    }
    up_migrate(r);                               /* (an editor of before the DRUM engine) */
    *target = *r;
    return 0;
}

#ifndef UP_HOST
static void up_values(const up_rec_t *r, int16_t *v)   /* mapped and clamped for its engine */
{
    int16_t def[P_COUNT];
    uint32_t i;
    for (i = 0; i < P_COUNT; i++)
        def[i] = param_desc_of(up_native_cz(r)||up_legacy_cz(r)?ENGI_CZ:r->engine, i)->def;
    up_params(r, v, def);
    if (r->ver == 1u && r->engine == ENGI_PHYS)   /* (before 1.0: MODEL 2 was DUST) */
        phys_legacy(&v[P_E0]);
    for (i = 0; i < P_COUNT; i++)
        if (!(eng_extra_retired(r->engine) && i >= P_E0))
        v[i] = (int16_t)clamp(v[i], param_desc_of(up_native_cz(r)||up_legacy_cz(r)?ENGI_CZ:r->engine, i)->min, param_desc_of(up_native_cz(r)||up_legacy_cz(r)?ENGI_CZ:r->engine, i)->max);
}

#include "cz_bank.c"
#include "fm6_bank.c" /* historical bank formats, migration only */
#include "up_fm6.c"
static void native_boot(void);
static void native_cache_reset(void);

static void up_boot(void)                      /* persist_boot: the banks from flash */
{
#if MELODEE_FLASH
    up_cache_reset();
#endif
#if MELODEE_FLASH
    uint32_t b;
    for (b = 0; b < UP_SLOTS / UP_PER_BANK; b++) {
        int len;
        if (b < 2u) len = flash_ok ? st_load(up_obj(b), up_cache_bank(b), sizeof (*up_cache_bank(b))) : -1;
        else {
            len = flash_ok ? st_load(up_obj(b), up_cache_bank(b)->r, sizeof up_cache_bank(b)->r) : -1;
            if (len == sizeof up_cache_bank(b)->r) {
                up_cache_bank(b)->magic = UP_BANK_MAGIC; up_cache_bank(b)->rsize = sizeof(up_rec_t); up_cache_bank(b)->nslot = UP_PER_BANK;
                len = sizeof (*up_cache_bank(b));
            } else len = -1;
        }
        up_bank_check(b, len);
#ifndef UP_HOST
        up_cache_index(b);
#endif
    }
#endif
    native_cache_reset();
    if(!native_fm_active())upf_boot();else upf_release();
    cz_bank_boot();
    native_boot();
    upf_release();
#ifdef MELODEE_FAVORITES
    for (uint32_t k = 0; k < UP_SLOTS; k++)
        if (!up_used(k)) favorite_set(USER_GENERAL, k, 0);
#endif
}

/* record k = *r (0: erase), then the bank to flash: 0 ok, 1 bad slot, 2 flash error or the transport runs (nothing
 * written), 3 no flash (kept in RAM) */
static int up_put(uint32_t k, const up_rec_t *r)
{
    up_bank_t *bk;
#if MELODEE_FLASH
    up_rec_t old;
    uint32_t magic;
    uint16_t rsize, nslot;
#endif
    if (k >= UP_SLOTS)
        return 1;
#if MELODEE_FLASH
    if (!flash_ok) return 2;
#endif
    if (transport_busy()) {                            /* no flash erase while playing (project_save) */
        ui_message("STOP TO SAVE");
        return 2;
    }
    bk = up_cache_bank(k / UP_PER_BANK);
#if MELODEE_FLASH
    old = *up_rec(k);
    magic = bk->magic;
    rsize = bk->rsize;
    nslot = bk->nslot;
#endif
    bk->magic = UP_BANK_MAGIC;
    bk->rsize = sizeof(up_rec_t);
    bk->nslot = UP_PER_BANK;
    if (r)
        *up_rec(k) = *r;
    else
        memset(up_rec(k), 0, sizeof(up_rec_t));
#if MELODEE_FLASH
    if (flash_ok && up_save_bank(k / UP_PER_BANK)) {
        *up_rec(k) = old;
        bk->magic = magic;
        bk->rsize = rsize;
        bk->nslot = nslot;
        return 2;
    }
#endif
    if (!r) {
#ifdef MELODEE_FAVORITES
        if (favorite_set(USER_GENERAL, k, 0)) settings_save();
#endif
        uint32_t i;
        for (i = 0; i < NTRK; i++)
            if (!trk[i].user_native && trk[i].user == k + 1u)
                trk[i].user = 0;
    }
    up_gen++;
#if MELODEE_FLASH
    if (flash_ok)
        return 0;
#endif
    return 3;
}

static void up_slot_label(char *b, uint32_t k)  /* "U07" */
{
    b[0] = 'U';
    b[1] = (char)('0' + (k + 1u) / 10u);
    b[2] = (char)('0' + (k + 1u) % 10u);
    b[3] = 0;
}

/* the automatic name of engine e's sound in slot k: engine name + slot number ("ANALOG 07"); b holds 13 */
static void up_auto_name(char *b, uint32_t e, uint32_t k)
{
    char l[4];
    e %= NENGINES;
    str_cpy(b, ENGINES[eng_sound_idx(e)]->name, 9);   /* (a DIGITAL record plays as FM6) */
    up_slot_label(l, k);
    str_cpy(b + str_len(b), " ", 2);
    str_cpy(b + str_len(b), l + 1, 3);
}

/* the record's name = name (at most 12), 0 or "": the automatic one */
static void up_set_name(up_rec_t *r, uint32_t k, const char *name)
{
    char b[16];
    uint32_t i;
    if (!name || !name[0]) {
        up_auto_name(b, r->engine, k);
        name = b;
    }
    memset(r->name, 0, sizeof r->name);
    for (i = 0; i < 12u && name[i]; i++)
        r->name[i] = name[i];
}

/* the selected part's sound -> slot k; name 0 or "": the automatic name (up_auto_name); up_put's result */
static int up_store(uint32_t k, const char *name)
{
    if (k >= UP_SLOTS || TSEL->eng_req == ENGI_PROPHET) return 1;
    up_rec_t r;
    uint32_t i;
    memset(&r, 0, sizeof r);
    r.used = UP_USED;
    r.ver = UP_VER;
    r.engine = TSEL->eng_req;
    r.np = P_COUNT;
    up_set_name(&r, k, name);
    for (i = 0; i < P_COUNT; i++)
        up_set_value(&r, i, motion_base_value(TSEL, i));
    if (r.engine == ENGI_CZ && TSEL->p[P_E7] == CZ_NATIVE) {
        r.ver = UP_VER_CZ;
        memcpy(r.packed, cz_patch[song.sel % NTRK].raw, CZ_BYTES);
    }
    up_pat_from(&r, TSEL->step);
    if (drum_track(TSEL)) {                             /* a DRUM track that strikes a lane: its grid */
        uint32_t any = 0;
        for (i = 0; i < 16u; i++)
            any |= step_lanes(&TSEL->step[i]);
        if (any) {
            r.ver = UP_VER_GRID;
            for (i = 0; i < 16u; i++) {
                r.note[i] = (uint8_t)step_lanes(&TSEL->step[i]);
                r.flags[i] = (uint8_t)step_accents(&TSEL->step[i]);
            }
        }
    }
    if (r.engine == ENGI_FM6) {
        /* A fresh nonce prevents an interrupted save of the same macros from
         * pairing the new record with the previous voice. Renames keep it. */
        uint32_t oldtag = 0;
        if(!native_fm_active()){
            upf_t *work=upf_bank(k);if(!work)return 2;
            oldtag=work->e[k % UPF_SLOTS].tag;
        }
        uint32_t nonce = oldtag + 1u;
        do { memcpy(r.cz_extra, &nonce, sizeof nonce); nonce++; }
        while (upf_tag(&r) == oldtag);
    }
    int rc = up_put(k, &r);
    if ((rc == 0 || rc == 3) && r.engine == ENGI_FM6) {
        int u = upf_store(k, (uint32_t)(TSEL - trk));
        if (u == 2) rc = 2;
    }
    return rc;
}

/* slot k renamed (name 0 or "": the automatic one), the sound and its pattern as they are; up_put's result, 1 for
 * an empty slot */
static int up_rename(uint32_t k, const char *name)
{
    up_rec_t r;
    if (!up_used(k))
        return 1;
    r = *up_rec(k);
    up_set_name(&r, k, name);
    return up_put(k, &r);
}

/* slot k -> the selected part's sound: engine and every parameter except the track's own (param_kept:
 * the mix, ARP, SCL, the pattern parameters, the SLICER). The steps stay: the record's pattern is
 * loaded only from SEQ > PATTERNS (up_pat_load). 0 ok, 1 empty */
static int up_load(uint32_t k)
{
    const up_rec_t *r;
    int16_t v[P_COUNT];
    uint32_t i;
    track_t *t = TSEL;
    if (!up_used(k))
        return 1;
    r = up_rec(k);
    up_values(r, v);
    load_begin(t, UNDO_SOUND);                          /* (ui.c: the copy for SAVE held = undo) */
    panic_req |= (uint8_t)(1u << song.sel);
#if !MELODEE_FM4
    if (r->engine == ENGI_DIGITAL) {                    /* a DIGITAL sound (kept as it was stored): FM6 */
        int16_t p[P_COUNT];
        for (i = 0; i < P_COUNT; i++)
            p[i] = param_kept(i) ? t->p[i] : v[i];
        fm4_apply(t, p);
    } else
#endif
    {
        fm1_irq_off();                                  /* the audio ISR must not see half a sound */
        t->eng_req = up_native_cz(r)||up_legacy_cz(r)?ENGI_CZ:r->engine;
        for (i = 0; i < P_COUNT; i++)
            if (!param_kept(i))
                t->p[i] = v[i];
        t->preset = 0;
        if (up_native_cz(r)||up_legacy_cz(r))up_cz_raw(r,cz_patch[song.sel % NTRK].raw);
        cz_track_accept(t);
        fm1_irq_on();
        upf_track_load(t, k); /* FM6: load the preset's actual voice */
    }
    t->user = (uint8_t)(k + 1u);
    t->user_native=0;
    load_end(t);
    sync_reload = 1;
    ui.force = 1;
    return 0;
}

/* the patterns of the user presets (SEQ > PATTERNS lists them after the factory ones, ui.c pat_count) */
static int up_has_pat(uint32_t k) { return up_used(k) && !up_pat_empty(up_rec(k)); }

static uint32_t up_pat_count(void)
{
    uint32_t k, n = 0;
    for (k = 0; k < UP_SLOTS; k++)
        n += (uint32_t)up_has_pat(k);
    return n;
}

static uint32_t up_pat_nth(uint32_t n)         /* slot of the n-th one that holds a pattern (n < up_pat_count()) */
{
    uint32_t k;
    for (k = 0; k < UP_SLOTS; k++)
        if (up_has_pat(k) && !n--)
            return k;
    return 0;
}

static uint32_t up_pat_rank(uint32_t slot)     /* ones that hold a pattern before it */
{
    uint32_t k, n = 0;
    for (k = 0; k < slot && k < UP_SLOTS; k++)
        n += (uint32_t)up_has_pat(k);
    return n;
}

/* slot k's pattern -> track t's steps 1..16 (the rest cleared), with the record's LEN (at most 16), DIV,
 * SWING and GATE; the sound stays (ui.c pat_load: the undo copy) */
static void up_pat_load(track_t *t, uint32_t k)
{
    const up_rec_t *r;
    int16_t v[P_COUNT];
    uint32_t i;
    if (!up_has_pat(k))
        return;
    r = up_rec(k);
    up_values(r, v);
    if (up_grid(r))
        load_grid16(t, r->note, r->flags);
    else
        load_pat16(t, r->note, r->flags);
    for (i = P_SDIV; i <= P_SGATE; i++)
        t->p[i] = v[i];
    t->p[P_SLEN] = (int16_t)clamp(v[P_SLEN], 1, 16);   /* (the pattern has 16 steps) */
}

/* the engine a used slot's sound plays on (a DIGITAL record: FM6, without MELODEE_FM4) */
static uint32_t up_engine(uint32_t k) {
#if MELODEE_FLASH && !defined(UP_HOST)
    if(up_cached != k/UP_PER_BANK && up_meta[k].ready)return up_meta[k].engine;
#endif
 if(up_native_cz(up_rec(k))||up_legacy_cz(up_rec(k)))return ENGI_CZ; return eng_sound_idx(up_rec(k)->engine); }

static uint32_t up_count(void)                 /* used slots */
{
    uint32_t k, n = 0;
    for (k = 0; k < UP_SLOTS; k++)
        n += (uint32_t)up_used(k);
    return n;
}

static uint32_t up_nth(uint32_t n)             /* slot of the n-th used one (n < up_count()) */
{
    uint32_t k;
    for (k = 0; k < UP_SLOTS; k++)
        if (up_used(k) && !n--)
            return k;
    return 0;
}

static uint32_t up_rank(uint32_t slot)         /* used slots before it */
{
    uint32_t k, n = 0;
    for (k = 0; k < slot && k < UP_SLOTS; k++)
        n += (uint32_t)up_used(k);
    return n;
}

/* SAVE > USER page actions, with the message in the top bar. name: the save's or the rename's (0 or "": automatic) */
static void up_ui_named(uint32_t op, uint32_t k, const char *name)   /* 0 load, 1 erase, 2 save, 3 rename */
{
    char l[4];
    int rc;
    up_slot_label(l, k);
    if ((op < 2u || op == 3u) && !up_used(k)) {
        ui_message("EMPTY SLOT");
        return;
    }
    if (op && (song.playing || chain_busy() || transport_req == 1u)) {    /* (up_put refuses too) */
        ui_message("STOP TO SAVE");
        return;
    }
    if (op == 0u) {
        up_load(k);
        if (up_has_pat(k))                              /* SEQ > PATTERNS starts at its pattern */
            ui.ppick = (uint8_t)(NPATTERNS + up_pat_rank(k));
        ui_say("LOADED ", l);
        return;
    }
    rc = op == 1u ? up_put(k, 0) : op == 3u ? up_rename(k, name) : up_store(k, name);
    if (rc == 3)
        ui_message(op == 1u ? "ERASED (RAM)" : op == 3u ? "RENAMED (RAM)" : "SAVED (RAM)");
    else if (rc)
        ui_message(op == 1u ? "ERASE ERROR" : "SAVE ERROR");
    else
        ui_say(op == 1u ? "ERASED " : op == 3u ? "RENAMED " : "SAVED ", l);
    ui.force = 1;
}
static void up_ui(uint32_t op, uint32_t k) { up_ui_named(op, k, 0); }
#include "prophet_user.c"
#include "native_presets.c"
#endif
