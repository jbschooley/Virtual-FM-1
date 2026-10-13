/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* User presets (editor protocol v2, cmds 16-21; the SAVE > USER page): 64
 * slots in four storage objects (the first two at 0xDC000..0xDFFFF,
 * two more at 0xE5000..0xE8FFF), 16 records each. Only names and indexes
 * stay in RAM; one bank is read on demand when loading or editing. A record: engine, name, the instrument parameters, a 16-step
 * pattern (kept in the format, never loaded: the phrases are gone). An FM6 record's voice is native FM6 slot k
 * (native_presets.c). Loading one loads the sound only.
 *
 * Versions: a bank whose magic, record size or slot count differ reads as
 * empty; so does a record with another layout version. Read: UP_VER (4, values as biased bytes), UP_VER_GRID (5,
 * its pattern a drum grid: note[i] step i's lane hits, flags[i] their accents), UP_VER_CZ (8, native CZ-1 bytes),
 * UP_VER_DRUM / UP_VER_DRUM_GRID (9 / 10, the DRUM patch beside the values). Older versions (1..3, the CZ records
 * of before the native CZ-1, the 192-byte banks) remain protected and unreadable. A record keeps np = the
 * P_COUNT it was stored with; when that differs from today's it is mapped by count: its last 8 values are
 * P_E0..P_E7, the first np - 8 are P_LEVEL.. in order, and parameters it does not have take their defaults.
 * That holds as long as common parameters are only ever added just before P_E0 (else bump UP_VER and translate).
 * A record of a retired engine (DIGITAL, ANALOG ..) stays as it is and plays silent.
 *
 * With -DUP_HOST (host test) only the part above #ifndef UP_HOST is built;
 * it needs nothing but core.h. */
#define UP_PER_BANK 16u
#include "cz_patch.h"
#define UP_VER_CZ 8u                            /* native CZ-1 bytes in the 144-byte payload */
#define UP_PMAX 72u                              /* room for P_COUNT to grow */
#define UP_USED 0xA5u
#define UP_VER 4u                                /* the values as biased bytes */
#define UP_VER_DRUM 9u
#define UP_VER_DRUM_GRID 10u
#define UP_VER_GRID 5u                           /* 4 with a drum grid as the pattern (see the top) */
#define UP_BANK_MAGIC 0x31425055u                /* "UPB1" */
typedef struct {
    uint8_t used, ver, engine, np;               /* UP_USED, UP_VER, engine, P_COUNT when stored */
    char name[12];                               /* ASCII 32..126, 0-padded (no 0 when 12 long) */
    uint8_t packed[UP_PMAX * 2u];                /* the values + 64 (UP_VER_CZ: the native tone) */
    uint8_t note[16], flags[16];                 /* note 0 = rest; flags 1 accent, 2 slide, 4 tie (UP_VER_GRID:
                                                  * lane hits, their accents) */
    uint8_t cz_extra[46];                        /* UP_VER_DRUM*: the DRUM patch's first bytes (up_drum_raw) */
} up_rec_t;
typedef struct {
    uint32_t magic;
    uint16_t rsize, nslot;
    up_rec_t r[UP_PER_BANK];
} up_bank_t;
_Static_assert(sizeof(up_rec_t) == 238, "user preset record layout");
_Static_assert(P_COUNT <= UP_PMAX * 2u && P_COUNT < 128, "user preset record: P_COUNT");
#if MELODEE_FLASH && !defined(UP_HOST)
/* One main-loop bank cache holds whichever bank was asked for last: a user preset bank, a native FM6 or CZ-1 bank
 * (or the CZ-1 bank's older view, cz_bank_load) or a Prophet user bank (bank_buf, after native_presets.c). Each owner
 * keeps its own cached-bank byte; bank_claim(kind) before a load forgets the others' (a user bank is indexed first,
 * up_meta). Names, used masks and favourites stay in the owners' indexes, so browsing never reloads. A pointer into a
 * bank is good until the next call that may load another kind: copy what outlives it. */
enum { BK_UP, BK_FM, BK_CZ, BK_CZOLD, BK_P5 };
static void bank_claim(uint32_t kind);
static up_bank_t *bank_up(void);
#define up_cache (*bank_up())
static uint8_t up_cached = 255;
static struct { uint8_t ready, used, engine, old; char name[13]; } up_meta[UP_SLOTS];
/* Records this firmware does not read (written before 1.0: older record versions, a bank of another shape) stay as
 * they are: their slots show OLD FORMAT and a write there is refused (up_put) until the device's dialog lets it
 * (preset_old_write is scoped to that write). A bank of another shape is old as a whole. */
static uint8_t up_bank_old;
static up_bank_t *up_cache_bank(uint32_t b);
static void up_cache_reset(void) { up_cached=255; memset(up_meta,0,sizeof up_meta); up_bank_old=0; }
#else
static up_bank_t up_banks_host[UP_SLOTS / UP_PER_BANK] __attribute__((section(".pool")));
static up_bank_t *up_cache_bank(uint32_t b) { return &up_banks_host[b]; }
static void up_cache_reset(void) { memset(up_banks_host,0,sizeof up_banks_host); }
#endif

/* Set only around a confirmed device write; never retained by a cancelled name screen. */
static uint8_t preset_old_write;
static void preset_old_allow(uint32_t allow) { preset_old_write = (uint8_t)allow; }

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

static int up_native_cz(const up_rec_t *r){return r->engine==ENGI_CZ && r->ver==UP_VER_CZ;}
static int up_cz_raw(const up_rec_t *r,uint8_t *raw)
{
    if(!up_native_cz(r))return 0;
    memcpy(raw,r->packed,CZ_BYTES);return cz_patch_valid(raw);
}
static int up_bank_shape(uint32_t len,uint32_t rsize){return len==sizeof(up_bank_t) && rsize==sizeof(up_rec_t);}
static int up_drum_raw(const up_rec_t *r,drum_patch_t *p)
{
    uint8_t b[DRUM_WIRE_BYTES];if(r->np>sizeof r->packed-8u)return 0;
    memcpy(b,r->cz_extra,sizeof r->cz_extra);memcpy(b+sizeof r->cz_extra,r->packed+r->np,8u);return drum_patch_unpack(p,b);
}

static int up_valid(const up_rec_t *r)
{
    if (r->used == UP_USED && up_native_cz(r)) { uint8_t raw[CZ_BYTES];return (r->np==92u || r->np==100u || r->np==104u || r->np==108u || r->np==P_COUNT) && r->name[0] && up_cz_raw(r,raw); }
    if (!(r->used == UP_USED && (r->ver == UP_VER || r->ver == UP_VER_GRID || (r->engine==ENGI_DRUM && (r->ver==UP_VER_DRUM || r->ver==UP_VER_DRUM_GRID))) && r->engine < USER_GENERAL &&
          r->np >= 8u && r->np <= UP_PMAX * 2u && r->name[0])) return 0;
    for (uint32_t i = 0; i < r->np; i++) if (r->packed[i] > 191u) return 0;
    if(r->engine==ENGI_DRUM && (r->ver==UP_VER_DRUM || r->ver==UP_VER_DRUM_GRID)){drum_patch_t p;if(!up_drum_raw(r,&p))return 0;}
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

static int up_old(uint32_t k)
{
    if (k >= UP_SLOTS) return 0;
#if MELODEE_FLASH && !defined(UP_HOST)
    if (!up_meta[k].ready) (void)up_cache_bank(k / UP_PER_BANK);
    if (up_cached == k / UP_PER_BANK) return ((up_bank_old >> up_cached) & 1u) || (up_rec(k)->used != 0 && !up_valid(up_rec(k)));
    return up_meta[k].old;
#else
    return up_rec(k)->used != 0 && !up_valid(up_rec(k));
#endif
}
static int up_old_bank(uint32_t k)
{
#if MELODEE_FLASH && !defined(UP_HOST)
    if (k >= UP_SLOTS) return 0;
    (void)up_old(k);
    return (up_bank_old >> (k / UP_PER_BANK)) & 1u;
#else
    (void)k; return 0;
#endif
}

static int up_grid(const up_rec_t *r) { return r->ver == UP_VER_GRID || r->ver==UP_VER_DRUM_GRID; }
static int16_t up_value(const up_rec_t *r, uint32_t k) { return (int16_t)r->packed[k] - 64; }
static void up_set_value(up_rec_t *r, uint32_t k, int16_t v)
{
    r->packed[k] = (uint8_t)(v + 64);
}
static void up_bank_check(uint32_t b, int len)  /* after loading bank b (len bytes, -1 = none): wrong shape -> empty */
{
    up_bank_t *bk = up_cache_bank(b);
    if (!up_bank_shape((uint32_t)len,bk->rsize) || bk->magic != UP_BANK_MAGIC ||
        bk->nslot != UP_PER_BANK)
        memset(bk, 0, sizeof *bk);
}
#if MELODEE_FLASH && !defined(UP_HOST)
static void up_cache_index(uint32_t b)
{
    for(uint32_t j=0;j<UP_PER_BANK;j++){
        uint32_t k=b*UP_PER_BANK+j; const up_rec_t *r=&up_cache.r[j];
        up_meta[k].ready=1;up_meta[k].used=(uint8_t)up_valid(r);
        up_meta[k].old=(uint8_t)(((up_bank_old >> b) & 1u) || (r->used != 0 && !up_valid(r)));
        up_meta[k].engine=up_native_cz(r)?ENGI_CZ:eng_sound_idx(r->engine);
        uint32_t n=0;
        for(;n<12u && r->name[n];n++)up_meta[k].name[n]=r->name[n]>='a' && r->name[n]<='z'?(char)(r->name[n]-32):r->name[n];
        up_meta[k].name[n]=0;
    }
}
static up_bank_t *up_cache_bank(uint32_t b)
{
    if(up_cached != b){
        if(up_cached<UP_SLOTS/UP_PER_BANK)up_cache_index(up_cached);
        bank_claim(BK_UP);
        up_cached=(uint8_t)b;
        memset(&up_cache,0,sizeof up_cache);
        int n=flash_ok?st_load(up_obj(b),b<2u?(void *)&up_cache:(void *)up_cache.r,b<2u?sizeof up_cache:sizeof up_cache.r):-1;
        if(b>=2u && n==sizeof up_cache.r){up_cache.magic=UP_BANK_MAGIC;up_cache.rsize=sizeof(up_rec_t);up_cache.nslot=UP_PER_BANK;n=sizeof up_cache;}
        up_bank_check(b,n);
        if (up_cache.magic != UP_BANK_MAGIC && flash_ok && st_exists(up_obj(b))) up_bank_old |= (uint8_t)(1u << b);
        else up_bank_old &= (uint8_t)~(1u << b);
        up_cache_index(b);
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
    if (up_old(k)) { memcpy(b, "OLD FORMAT", 11); return; }
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
    *target = *r;
    return 0;
}

#ifndef UP_HOST
static void up_values(const up_rec_t *r, int16_t *v)   /* mapped and clamped for its engine */
{
    int16_t def[P_COUNT];
    uint32_t i;
    for (i = 0; i < P_COUNT; i++)
        def[i] = param_desc_of(up_native_cz(r)?ENGI_CZ:r->engine, i)->def;
    up_params(r, v, def);
    for (i = 0; i < P_COUNT; i++) if (param_reserved(i)) v[i] = 0;
    for (i = 0; i < P_COUNT; i++)
        if (!(eng_extra_retired(r->engine) && i >= P_E0))
        v[i] = (int16_t)clamp(v[i], param_desc_of(up_native_cz(r)?ENGI_CZ:r->engine, i)->min, param_desc_of(up_native_cz(r)?ENGI_CZ:r->engine, i)->max);
}

#include "cz_bank.c"
static void native_boot(void);
static void native_cache_reset(void);

static void up_boot(void)                      /* persist_boot: the banks from flash */
{
#if MELODEE_FLASH
    up_cache_reset();
#endif
#if MELODEE_FLASH
    for (uint32_t b = 0; b < UP_SLOTS / UP_PER_BANK; b++) (void)up_cache_bank(b);
#endif
    native_cache_reset();
    cz_bank_boot();
    native_boot();
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
    if (up_old(k) && !preset_old_write) return 2;
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
    /* A successful write replaces a wrong-shape bank; other unknown records in a current bank survive. */
#if MELODEE_FLASH
    up_bank_old &= (uint8_t)~(1u << (k / UP_PER_BANK));
    up_cache_index(k / UP_PER_BANK);
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

/* the automatic name of engine e's sound in slot k: engine name + slot number ("SID 07"); b holds 13 */
static void up_auto_name(char *b, uint32_t e, uint32_t k)
{
    char l[4];
    e %= NENGINES;
    str_cpy(b, ENGINES[eng_sound_idx(e)]->name, 9);
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
    momentary_restore();
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
        up_set_value(&r, i, (param_fx(i) || param_fx_send(i))?TP[i].def:motion_base_value(TSEL, i));
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
    if(r.engine==ENGI_DRUM){uint8_t b[DRUM_WIRE_BYTES];drum_patch_pack(b,&drum_patch[song.sel]);memcpy(r.cz_extra,b,sizeof r.cz_extra);memcpy(r.packed+r.np,b+sizeof r.cz_extra,8u);r.ver=r.ver==UP_VER_GRID?UP_VER_DRUM_GRID:UP_VER_DRUM;}
    int rc = up_put(k, &r);
    if ((rc == 0 || rc == 3) && r.engine == ENGI_FM6) {    /* its voice: native FM6 slot k */
        uint8_t pk[FM6_PACKED];
        fm6_pack(fm6_patch[song.sel % NTRK], pk);
        if (native_put(ENGI_FM6, k, pk) == 2) rc = 2;
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
 * the mix, ARP, SCL, the pattern parameters). The steps stay: the record's pattern is
 * never loaded (the phrases are gone). 0 ok, 1 empty */
static int up_load(uint32_t k)
{
    up_rec_t rec;                                       /* (a copy: native_used below may load another bank) */
    const up_rec_t *r = &rec;
    int16_t v[P_COUNT];
    uint32_t i;
    track_t *t = TSEL;
    if (!up_used(k))
        return 1;
    rec = *up_rec(k);
    up_values(r, v);
    load_begin(t, UNDO_SOUND);                          /* (ui.c: the copy for SAVE held = undo) */
    panic_req |= (uint8_t)(1u << song.sel);
    {
        fm1_irq_off();                                  /* the audio ISR must not see half a sound */
        t->eng_req = up_native_cz(r)?ENGI_CZ:r->engine;
        for (i = 0; i < P_COUNT; i++)
            if (!param_kept(i))
                t->p[i] = param_reserved(i) ? 0 : v[i];
        t->preset = 0;
        if (up_native_cz(r))up_cz_raw(r,cz_patch[song.sel % NTRK].raw);
        if(r->engine==ENGI_DRUM){memset(&drum_patch[song.sel],0,sizeof(drum_patch_t));if(r->ver==UP_VER_DRUM || r->ver==UP_VER_DRUM_GRID)up_drum_raw(r,&drum_patch[song.sel]);}
        cz_track_accept(t);
        fm1_irq_on();
        if (t->eng_req == ENGI_FM6) {                   /* FM6: its voice, native slot k (else INIT) */
            uint8_t v[FP_SIZE + 1u];
            fm6_unpack(native_used(ENGI_FM6, k) ? native_raw(ENGI_FM6, k) : FM6_INIT, v);
            fm6_set_patch(song.sel % NTRK, v);
        }
    }
    t->user = (uint8_t)(k + 1u);
    t->user_native=0;
    load_end(t);
    sync_reload = 1;
    ui.force = 1;
    return 0;
}

/* the engine a used slot's sound plays on */
static uint32_t up_engine(uint32_t k) {
#if MELODEE_FLASH && !defined(UP_HOST)
    if(up_cached != k/UP_PER_BANK && up_meta[k].ready)return up_meta[k].engine;
#endif
 if(up_native_cz(up_rec(k)))return ENGI_CZ; return eng_sound_idx(up_rec(k)->engine); }

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
    if ((op < 2u || op == 3u) && !up_used(k) && !(op == 1u && up_old(k))) {
        ui_message(up_old(k) ? "OLD FORMAT" : "EMPTY SLOT");
        return;
    }
    if (op && (song.playing || chain_busy() || transport_req == 1u)) {    /* (up_put refuses too) */
        ui_message("STOP TO SAVE");
        return;
    }
    if (op == 0u) {
        up_load(k);
        ui_say("LOADED ", l);
        return;
    }
    preset_old_write = 1;
    rc = op == 1u ? up_put(k, 0) : op == 3u ? up_rename(k, name) : up_store(k, name);
    preset_old_write = 0;
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
#if MELODEE_FLASH
static union { up_bank_t up; native_fm_t fm; cz_bank_t cz; p5_bank_t p5; } bank_buf __attribute__((section(".pool")));
static up_bank_t *bank_up(void) { return &bank_buf.up; }
static native_fm_t *bank_fm(void) { return &bank_buf.fm; }
static cz_bank_t *bank_cz(void) { return &bank_buf.cz; }
static p5_bank_t *bank_p5(void) { return &bank_buf.p5; }
static void bank_claim(uint32_t kind)
{
    if (kind != BK_UP && up_cached < UP_SLOTS / UP_PER_BANK)
        up_cache_index(up_cached);
    if (kind != BK_UP) up_cached = 255;
    if (kind != BK_FM) native_fm_cached = 255;
    if (kind != BK_CZ) native_cz_cached = 255;
    if (kind != BK_CZOLD) cz_bank_cached = 255;
    if (kind != BK_P5) p5_cached = 255;
}
#endif
#endif

#ifndef UP_HOST
/* Melodee user-preset files: F0 7D 4D 4C 01 pack7(record) checksum F7.
 * Native engines export their manufacturer's voice format instead. */
static int general_sysex_save(uint32_t k,const uint8_t *raw)
{
    up_rec_t r;memcpy(&r,raw,sizeof r);
    if(!up_valid(&r))return 1;
    return up_put(k,&r);
}
static int general_sysex_send(uint32_t k)
{
#if MELODEE_OTA
    if(!usb.config || !up_used(k))return 1;
    up_rec_t r=*up_rec(k);const uint8_t *raw=(const uint8_t *)&r;
    uint8_t frame[280];memcpy(frame,(uint8_t[]){0xf0,0x7d,0x4d,0x4c,1},5);uint32_t n=5;
    for(uint32_t pos=0;pos<sizeof r;){uint32_t count=sizeof r-pos;if(count>7u)count=7;uint32_t mask=n++;frame[mask]=0;for(uint32_t j=0;j<count;j++,pos++){frame[mask]|=(raw[pos]>>7)<<j;frame[n++]=raw[pos]&127u;}}
    frame[n]=sx_checksum(frame+1,n-1u);n++;frame[n++]=0xf7;
    return ota_wire_send(frame,n);
#else
    (void)k;return 1;
#endif
}
static int general_sysex_frame(const uint8_t *body,uint32_t n)
{
    if(n<4u || body[0]!=0x7d || body[1]!=0x4d || body[2]!=0x4c)return 0;
    if(body[3]!=1u || n!=4u+sizeof(up_rec_t)+(sizeof(up_rec_t)+6u)/7u+1u){ui_message("INVALID PRESET SYSEX");return 1;}
    for(uint32_t i=0;i<n;i++)if(body[i]>127u){ui_message("INVALID PRESET SYSEX");return 1;}
    if(sx_checksum(body,n)){ui_message("INVALID PRESET SYSEX");return 1;}
    up_rec_t r;uint8_t *raw=(uint8_t *)&r;uint32_t pos=0;
    for(uint32_t i=4;i<n-1u;){uint32_t count=sizeof r-pos;if(count>7u)count=7;uint32_t mask=body[i++];if(!count || mask>>count){ui_message("INVALID PRESET SYSEX");return 1;}for(uint32_t j=0;j<count;j++)raw[pos++]=(uint8_t)(body[i++]|((mask>>j)&1u)<<7);}
    if(!up_valid(&r)){ui_message("INVALID PRESET SYSEX");return 1;}
    sysex_stage(r.engine,(const uint8_t *)&r,1,sizeof r);return 1;
}

#endif
