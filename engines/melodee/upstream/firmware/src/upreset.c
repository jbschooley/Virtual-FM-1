/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* One shared 384-slot library. Engine-native payloads and ordinary sound
 * parameters share the same slot identity. Flash uses 18 compact records per
 * A/B object; obsolete phrase bytes stay in the legacy wire format only.
 * Existing UPB1, DX7, CZ and current Prophet banks are converted with bounded,
 * restartable A/B writes. No retired Prophet format is migrated. */
/* Isolated parser/storage harnesses can override these without linking a display. */
#ifndef BOOT_WORK_BEGIN
#define BOOT_WORK_BEGIN(title, total) ((void)0)
#define BOOT_WORK_PROGRESS(done) ((void)0)
#define BOOT_WORK_END(failed) ((void)0)
#endif
#ifndef BOOT_PRESET_BEGIN
#define BOOT_PRESET_BEGIN(title, total) BOOT_WORK_BEGIN(title, total)
#endif
#ifndef BOOT_WIPE_BEGIN
#define BOOT_WIPE_BEGIN(title, total) BOOT_PRESET_BEGIN(title, total)
#endif
#ifdef UP_HOST
#define UP_PER_BANK 16u /* legacy wire-record parser harness */
#else
#define UP_PER_BANK 18u
#endif
#define UP_BANKS ((UP_SLOTS+UP_PER_BANK-1u)/UP_PER_BANK)
#define UP_DISK_MAGIC 0x32425055u
#define UP_VER_FM 11u
#define UP_VER_P5 12u
#define UP_VER_NATIVE_CZ 13u
static const uint8_t UP_NATIVE_PARAMS[] = {
    P_ATK,P_DEC,P_SUS,P_REL,P_ED_FLT,P_ED_PIT,P_ED_SHP,
    P_LRATE,P_LWAVE,P_LPHASE,P_LFADE,P_LD_PIT,P_LD_FLT,P_LD_SHP,P_LD_AMP,
    P_VOICE,P_GLIDE,P_GLMODE,P_PRIO,P_ALLOC,P_DETUNE,
    P_M1SRC,P_M1DST,P_M1AMT,P_M2SRC,P_M2DST,P_M2AMT,
    P_M3SRC,P_M3DST,P_M3AMT,P_M4SRC,P_M4DST,P_M4AMT,
    P_LSYNC,P_LTRIG,P_LPOL,P_SPRD,P_E0,P_E1,P_E2,P_E3,P_E4,P_E5,P_E6,P_E7
};
_Static_assert(sizeof UP_NATIVE_PARAMS <= 46u,"native preset parameter storage");
#include "cz_patch.h"
#define UP_VER_CZ 8u                            /* native CZ-1 bytes in the 144-byte payload */
#define UP_PMAX 72u                              /* room for P_COUNT to grow */
#define UP_USED 0xA5u
#define UP_STAR 2u
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
/* Main-loop index only, never serialized. Four Boolean flags share a byte;
 * names keep their 12 stored bytes and gain a terminator when read out. */
static struct {
    uint8_t ready:1, used:1, old:1, favorite:1;
    uint8_t engine;
    char name[12];
} up_meta[UP_SLOTS];
_Static_assert(sizeof up_meta[0] == 14u, "compact preset index");
static uint32_t up_migrate;
static uint8_t up_migration_error,up_converting;
static uint32_t up_fav_dirty;
/* Records this firmware does not read (written before 1.0: older record versions, a bank of another shape) stay as
 * they are: their slots show OLD FORMAT and a write there is refused (up_put) until the device's dialog lets it
 * (preset_old_write is scoped to that write). A bank of another shape is old as a whole. */
static uint32_t up_bank_old;
static up_bank_t *up_cache_bank(uint32_t b);
static void up_cache_reset(void) { up_cached=255; memset(up_meta,0,sizeof up_meta); up_bank_old=up_migrate=0; up_migration_error=up_converting=0;up_fav_dirty=0; }
#else
static up_bank_t up_banks_host[UP_BANKS] __attribute__((section(".pool")));
static up_bank_t *up_cache_bank(uint32_t b) { return &up_banks_host[b]; }
static void up_cache_reset(void) { memset(up_banks_host,0,sizeof up_banks_host); }
#endif

/* Set only around a confirmed device write; never retained by a cancelled name screen. */
static uint8_t preset_old_write;
static void preset_old_allow(uint32_t allow) { preset_old_write = (uint8_t)allow; }

/* Reuse every former preset object, preserving its numeric backup identity.
 * The final six slots use the previously free 0x9D000/0x9E000 pair. */
#if !defined(UP_HOST) && MELODEE_FLASH
static uint32_t up_obj(uint32_t b)
{
    if(b<2u)return OBJ_UPRESET0+b;
    if(b<4u)return OBJ_UPRESET_EXT0+b-2u;
    if(b<8u)return b==4u?OBJ_NATIVEFM0:b==5u?OBJ_NATIVEFM0+1u:b==6u?OBJ_FM6BANK:OBJ_UPFM6_EXT;
    if(b<16u)return OBJ_CZBANK0+b-8u;
    if(b<21u)return OBJ_P5BANK0+b-16u;
    return OBJ_SHARED_EXT;
}
typedef struct { uint8_t head[160], extra[46]; } up_disk_rec_t;
typedef struct { uint32_t magic; uint16_t rsize,nslot; up_disk_rec_t r[UP_PER_BANK]; } up_disk_bank_t;
_Static_assert(sizeof(up_disk_bank_t)<=3808u,"unified presets fit every A/B sector");
static int up_save_bank(uint32_t b)
{
    up_disk_bank_t disk;memset(&disk,0,sizeof disk);
    disk.magic=UP_DISK_MAGIC;disk.rsize=sizeof(up_disk_rec_t);disk.nslot=UP_PER_BANK;
    up_bank_t *bank=up_cache_bank(b);
    for(uint32_t i=0;i<UP_PER_BANK;i++){
        memcpy(disk.r[i].head,&bank->r[i],160u);memcpy(disk.r[i].extra,bank->r[i].cz_extra,46u);
        uint32_t k=b*UP_PER_BANK+i;
        if((up_fav_dirty>>b&1u) && k<UP_SLOTS && (bank->r[i].used&~UP_STAR)==UP_USED)disk.r[i].head[0]=(uint8_t)((disk.r[i].head[0]&~UP_STAR)|(up_meta[k].favorite?UP_STAR:0));
    }
    int rc=st_save(up_obj(b),&disk,sizeof disk);
    if(!rc){
        up_migrate&=~(1u<<b);
        for(uint32_t i=0;i<UP_PER_BANK;i++)bank->r[i].used=disk.r[i].head[0];
        up_fav_dirty&=~(1u<<b);
    }
    return rc;
}
#endif

static up_rec_t *up_rec(uint32_t k) { return &up_cache_bank(k / UP_PER_BANK)->r[k % UP_PER_BANK]; }

static int up_native_cz(const up_rec_t *r){return r->engine==ENGI_CZ && (r->ver==UP_VER_CZ || r->ver==UP_VER_NATIVE_CZ);}
static int up_native(const up_rec_t *r)
{
    return (r->engine==ENGI_FM6 && r->ver==UP_VER_FM) || (r->engine==ENGI_PROPHET && r->ver==UP_VER_P5) || (r->engine==ENGI_CZ && r->ver==UP_VER_NATIVE_CZ);
}
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
    if((r->used & ~UP_STAR)==UP_USED && up_native(r)){
        if(!r->name[0] || (r->np && r->np!=sizeof UP_NATIVE_PARAMS))return 0;
        for(uint32_t i=0;i<r->np;i++)if(r->cz_extra[i]>191u)return 0;
        if(r->engine==ENGI_CZ)return cz_patch_valid(r->packed);
        if(r->engine==ENGI_FM6){for(uint32_t i=0;i<128u;i++)if(r->packed[i]>127u)return 0;return 1;}
#ifndef UP_HOST
        p5_patch_t p;memcpy(&p,r->packed,sizeof p);return p5_patch_valid(&p);
#else
        return r->packed[133]==128u || r->packed[133]==133u;
#endif
    }
    if ((r->used & ~UP_STAR) == UP_USED && up_native_cz(r)) { uint8_t raw[CZ_BYTES];return (r->np==92u || r->np==100u || r->np==104u || r->np==108u || r->np==119u || r->np==125u /* (the FX cuts) */ || r->np==P_COUNT) && r->name[0] && up_cz_raw(r,raw); }
    if (!((r->used & ~UP_STAR) == UP_USED && (r->ver == UP_VER || r->ver == UP_VER_GRID || (r->engine==ENGI_DRUM && (r->ver==UP_VER_DRUM || r->ver==UP_VER_DRUM_GRID))) && r->engine < USER_GENERAL &&
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
static int up_decode_bank(uint32_t b,int n);
static void up_cache_index(uint32_t b)
{
    for(uint32_t j=0;j<UP_PER_BANK;j++){
        uint32_t k=b*UP_PER_BANK+j; if(k>=UP_SLOTS)break; const up_rec_t *r=&up_cache.r[j];
        int valid = up_valid(r);
        up_meta[k].ready=1;up_meta[k].used=(uint8_t)valid;if(!(up_fav_dirty>>b&1u))up_meta[k].favorite=(uint8_t)(!!(r->used&UP_STAR) && valid);
        up_meta[k].old=(uint8_t)(((up_bank_old >> b) & 1u) || (r->used != 0 && !valid));
        up_meta[k].engine=up_native_cz(r)?ENGI_CZ:eng_sound_idx(r->engine);
        uint32_t n=0;
        for(;n<12u && r->name[n];n++)up_meta[k].name[n]=r->name[n]>='a' && r->name[n]<='z'?(char)(r->name[n]-32):r->name[n];
        if(n<sizeof up_meta[k].name)up_meta[k].name[n]=0;
    }
}
static up_bank_t *up_cache_bank(uint32_t b)
{
    if(up_cached != b){
        if(up_cached<UP_BANKS)up_cache_index(up_cached);
        bank_claim(BK_UP);
        up_cached=(uint8_t)b;
        memset(&up_cache,0,sizeof up_cache);
        int n=flash_ok?st_load(up_obj(b),&up_cache,sizeof up_cache):-1;
        int decoded=up_decode_bank(b,n);
        if(!decoded)memset(&up_cache,0,sizeof up_cache);
        if(decoded!=1 && flash_ok && st_exists(up_obj(b)))up_bank_old|=1u<<b;
        else up_bank_old&=~(1u<<b);
        up_cache_index(b);
    }
    return &up_cache;
}
#endif


/* the record's values in today's P_* order (mapped by count, see above); def = the defaults */
static void up_params(const up_rec_t *r, int16_t *out, const int16_t *def)
{
    if(up_native(r)){
        memcpy(out,def,P_COUNT*sizeof *out);
        for(uint32_t i=0;i<r->np;i++)out[UP_NATIVE_PARAMS[i]]=(int16_t)r->cz_extra[i]-64;
        if(r->engine==ENGI_CZ)out[P_E7]=CZ_NATIVE;
        return;
    }
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
    if(up_cached != k/UP_PER_BANK && up_meta[k].ready){memcpy(b,up_meta[k].name,12);b[12]=0;return;}
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
    if (a[0] >= 128u || a[1] >= NENGINES || 2u + n >= na || !up_name_ok(a + 2, n) ||
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
static void shared_boot(void);
static void native_cache_reset(void);

static void up_boot(void)                      /* persist_boot: the banks from flash */
{
#if MELODEE_FLASH
    up_cache_reset();
#endif
#if MELODEE_FLASH
    /* shared_boot indexes and converts in source-preserving order. */
#endif
    native_cache_reset();
    cz_bank_boot();
    shared_boot();
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
    uint8_t favorite_before;
#endif
    if (k >= UP_SLOTS || (r && !up_valid(r)))
        return 1;
#if MELODEE_FLASH
    if(!flash_ok)return 2;
    if(up_migration_error && !(preset_old_write && k/UP_PER_BANK<16u && up_old_bank(k)))return 2;
#endif
    if (transport_busy()) {                            /* no flash erase while playing (project_save) */
        ui_message("STOP TO SAVE");
        return 2;
    }
    bk = up_cache_bank(k / UP_PER_BANK);
#if MELODEE_FLASH
    if((up_migrate>>(k/UP_PER_BANK)&1u) && !up_converting)return 2;
#endif
    if (up_old(k) && !preset_old_write) return 2;
#if MELODEE_FLASH
    favorite_before=up_meta[k].favorite;
    old = *up_rec(k);
    magic = bk->magic;
    rsize = bk->rsize;
    nslot = bk->nslot;
#endif
    if(up_old_bank(k) && preset_old_write)memset(bk->r,0,sizeof bk->r);
    bk->magic = UP_BANK_MAGIC;
    bk->rsize = sizeof(up_rec_t);
    bk->nslot = UP_PER_BANK;
    if (r) {
        uint8_t star=up_valid(up_rec(k)) ? up_rec(k)->used&UP_STAR : 0;
        *up_rec(k) = *r;up_rec(k)->used|=star;
#if MELODEE_FLASH
        up_meta[k].favorite=(uint8_t)(favorite_before || !!(r->used&UP_STAR));
#endif
    }
    else {
        memset(up_rec(k), 0, sizeof(up_rec_t));
#if MELODEE_FLASH
        up_meta[k].favorite=0;
#endif
    }
#if MELODEE_FLASH
    if (flash_ok && up_save_bank(k / UP_PER_BANK)) {
        *up_rec(k) = old;up_meta[k].favorite=favorite_before;
        bk->magic = magic;
        bk->rsize = rsize;
        bk->nslot = nslot;
        if(up_old_bank(k)){up_cached=255;(void)up_cache_bank(k/UP_PER_BANK);}
        return 2;
    }
#endif
    /* A successful write replaces a wrong-shape bank; other unknown records in a current bank survive. */
#if MELODEE_FLASH
    up_bank_old &= ~(1u << (k / UP_PER_BANK));
    up_cache_index(k / UP_PER_BANK);
#endif
    if (!r) {
#ifdef MELODEE_FAVORITES
        /* Erasing the record erases its favorite bit atomically. */
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

static void up_slot_label(char *b, uint32_t k)  /* "U007" */
{
    b[0] = 'U';
    b[1] = (char)('0' + (k + 1u) / 100u);
    b[2] = (char)('0' + (k + 1u) / 10u % 10u);
    b[3] = (char)('0' + (k + 1u) % 10u);
    b[4] = 0;
}

/* the automatic name of engine e's sound in slot k: engine name + slot number ("SID 007"); b holds 13 */
static void up_auto_name(char *b, uint32_t e, uint32_t k)
{
    char l[5];
    e %= NENGINES;
    str_cpy(b, ENGINES[eng_sound_idx(e)]->name, 9);
    up_slot_label(l, k);
    str_cpy(b + str_len(b), " ", 2);
    str_cpy(b + str_len(b), l + 1, 4);
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
static void up_native_name(up_rec_t *r,const char *name)
{
    uint32_t e=r->engine,off=e==ENGI_PROPHET?P5_NAME:e==ENGI_FM6?118u:128u;
    uint32_t len=e==ENGI_PROPHET?20u:e==ENGI_FM6?10u:16u;
    memset(r->packed+off,' ',len);
    for(uint32_t i=0;i<len && name && name[i];i++)r->packed[off+i]=(uint8_t)name[i];
}
static int up_store_to(uint32_t k,uint32_t tr,const char *name)
{
    momentary_restore();
    if(k>=UP_SLOTS || tr>=NTRK)return 1;
    track_t *t=&trk[tr];up_rec_t r;
    memset(&r,0,sizeof r);r.used=UP_USED;r.ver=UP_VER;r.engine=t->eng_req;r.np=P_COUNT;
    up_set_name(&r,k,name);
    if(r.engine==ENGI_FM6 || r.engine==ENGI_PROPHET || (r.engine==ENGI_CZ && t->p[P_E7]==CZ_NATIVE)){
        r.ver=r.engine==ENGI_FM6?UP_VER_FM:r.engine==ENGI_PROPHET?UP_VER_P5:UP_VER_NATIVE_CZ;
        if(r.engine==ENGI_FM6)fm6_pack(fm6_patch[tr],r.packed);
        else if(r.engine==ENGI_PROPHET)memcpy(r.packed,p5_patch_of(t),sizeof(p5_patch_t));
        else memcpy(r.packed,cz_patch[tr].raw,CZ_BYTES);
        r.np=sizeof UP_NATIVE_PARAMS;
        for(uint32_t i=0;i<r.np;i++)r.cz_extra[i]=(uint8_t)(motion_base_value(t,UP_NATIVE_PARAMS[i])+64);
        if(name && name[0])up_native_name(&r,name);
        return up_put(k,&r);
    }
    for(uint32_t i=0;i<P_COUNT;i++)up_set_value(&r,i,(param_fx(i)||param_fx_send(i))?param_desc_of(r.engine,i)->def:motion_base_value(t,i));
    up_pat_from(&r,t->step);
    if(drum_track(t)){
        uint32_t any=0;for(uint32_t i=0;i<16u;i++)any|=step_lanes(&t->step[i]);
        if(any){r.ver=UP_VER_GRID;for(uint32_t i=0;i<16u;i++){r.note[i]=(uint8_t)step_lanes(&t->step[i]);r.flags[i]=(uint8_t)step_accents(&t->step[i]);}}
    }
    if(r.engine==ENGI_DRUM){uint8_t b[DRUM_WIRE_BYTES];drum_patch_pack(b,&drum_patch[tr]);memcpy(r.cz_extra,b,sizeof r.cz_extra);memcpy(r.packed+r.np,b+sizeof r.cz_extra,8u);r.ver=r.ver==UP_VER_GRID?UP_VER_DRUM_GRID:UP_VER_DRUM;}
    return up_put(k,&r);
}
static int up_store(uint32_t k,const char *name){return up_store_to(k,song.sel,name);}

/* slot k renamed (name 0 or "": the automatic one), the sound and its pattern as they are; up_put's result, 1 for
 * an empty slot */
static int up_rename(uint32_t k, const char *name)
{
    up_rec_t r;
    if (!up_used(k))
        return 1;
    r = *up_rec(k);
    up_set_name(&r, k, name);
    if(up_native(&r) || up_native_cz(&r)){char nm[13];memcpy(nm,r.name,12);nm[12]=0;up_native_name(&r,name && name[0]?name:nm);}
    return up_put(k, &r);
}

/* slot k -> the selected part's sound: engine and every parameter except the track's own (param_kept:
 * the mix, ARP, SCL, the pattern parameters). The steps stay: the record's pattern is
 * never loaded (the phrases are gone). 0 ok, 1 empty */
static int up_load_to(uint32_t k,uint32_t tr)
{
    up_rec_t rec;                                       /* (a copy: native_used below may load another bank) */
    const up_rec_t *r = &rec;
    int16_t v[P_COUNT];
    uint32_t i;
    if(tr>=NTRK)return 1;
    track_t *t = &trk[tr];
    if (!up_used(k))
        return 1;
    rec = *up_rec(k);
    up_values(r, v);
    load_begin(t, UNDO_SOUND);                          /* (ui.c: the copy for EDIT held = undo) */
    panic_req |= (uint8_t)(1u << tr);
    {
        fm1_irq_off();                                  /* the audio ISR must not see half a sound */
        t->eng_req = up_native_cz(r)?ENGI_CZ:r->engine;
        for (i = 0; i < P_COUNT; i++)
            if (!param_kept(i))
                t->p[i] = param_reserved(i) ? 0 : v[i];
        t->preset = 0;
        if (up_native_cz(r))up_cz_raw(r,cz_patch[tr].raw);
        if(r->engine==ENGI_DRUM){memset(&drum_patch[tr],0,sizeof(drum_patch_t));if(r->ver==UP_VER_DRUM || r->ver==UP_VER_DRUM_GRID)up_drum_raw(r,&drum_patch[tr]);}
        if(r->engine==ENGI_PROPHET && r->ver==UP_VER_P5){memcpy(&p5_patch[tr],r->packed,sizeof(p5_patch_t));p5_ready[tr]=1;p5_track_accept(t);}
        cz_track_accept(t);
        fm1_irq_on();
        if (t->eng_req == ENGI_FM6) {                   /* FM6: its voice, native slot k (else INIT) */
            uint8_t v[FP_SIZE + 1u];
            fm6_unpack(r->ver==UP_VER_FM?r->packed:FM6_INIT,v);
            fm6_set_patch(tr, v);
        }
    }
    t->user = (uint16_t)(k + 1u);
    t->user_native=0;
    load_end(t);
    sync_reload = 1;
    ui.force = 1;
    return 0;
}

static int up_load(uint32_t k){return up_load_to(k,song.sel);}

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
    char l[5];
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
#include "shared_presets.c"
#if MELODEE_FLASH
static union { up_bank_t up; native_fm_t fm; cz_bank_t cz; p5_bank_t p5; } bank_buf __attribute__((section(".pool")));
static up_bank_t *bank_up(void) { return &bank_buf.up; }
static native_fm_t *bank_fm(void) { return &bank_buf.fm; }
static cz_bank_t *bank_cz(void) { return &bank_buf.cz; }
static void bank_claim(uint32_t kind)
{
    if (kind != BK_UP && up_cached < UP_BANKS)
        up_cache_index(up_cached);
    if (kind != BK_UP) up_cached = 255;
    if (kind != BK_FM) native_fm_cached = 255;
    if (kind != BK_CZOLD) cz_bank_cached = 255;
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
