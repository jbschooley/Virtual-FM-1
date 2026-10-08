/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modifications Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Backported from Felucca 1.0.3: each FM6 user preset owns its packed voice.
 * Two A/B objects hold 32 entries each. Record hashes omit the name, so renaming
 * preserves a voice and restoring a different record invalidates its old voice.
 * The first object reuses the retired bank's sectors; the old newest copy survives
 * until the migration commit succeeds. Existing records remain byte compatible. */
#define UPF_SLOTS 32u
#define UPF_MAGIC 0x36465055u                    /* "UPF6" */
#define UPF_PK 112u                              /* 128 x 7 bits */
typedef struct {
    uint32_t tag;                                /* upf_tag of the record it belongs to */
    uint8_t pk[UPF_PK];
} upf_ent_t;
typedef struct {
    uint32_t magic;
    uint16_t ver, nslot;                         /* 1, UP_SLOTS */
    uint32_t used;                               /* bit k: e[k] holds a patch */
    uint32_t rsv;
    upf_ent_t e[UPF_SLOTS];
} upf_t;
_Static_assert(sizeof(upf_t) == 3728u, "user preset FM6 patches layout (at most ST_PAYLOAD_MAX, 3840)");
_Static_assert(sizeof(up_rec_t) == 238u && sizeof(((up_rec_t *)0)->name) == 12u, "upf_tag: the name at bytes 4..15");
static upf_t *upf_work[UP_SLOTS / UPF_SLOTS];
#if MELODEE_FLASH
static uint32_t upf_obj(uint32_t b) { return b ? OBJ_UPFM6_EXT : OBJ_FM6BANK; }
#endif
static int upf_valid(const upf_t *u) { return u && u->magic == UPF_MAGIC && u->ver == 1u && u->nslot == UPF_SLOTS; }
static upf_t *upf_bank(uint32_t k)
{
    uint32_t b = k / UPF_SLOTS;
    if (!upf_work[b]) {
        /* Main-loop ownership: publish the descriptor with audio excluded. */
#if MELODEE_FLASH
        uint32_t f = irq_save();
#endif
        upf_work[b] = resource_get(RES_LEGACY0 + b, sizeof(upf_t));
#if MELODEE_FLASH
        irq_restore(f);
        if (upf_work[b] && flash_ok) st_load(upf_obj(b), upf_work[b], sizeof(upf_t));
#endif
        if (upf_work[b] && !upf_valid(upf_work[b])) {
            memset(upf_work[b],0,sizeof(upf_t));upf_work[b]->magic=UPF_MAGIC;upf_work[b]->ver=1;upf_work[b]->nslot=UPF_SLOTS;
        }
    }
    return upf_work[b];
}
static void upf_release(void)
{
    if(!upf_work[0] && !upf_work[1])return;
#if MELODEE_FLASH
    uint32_t f = irq_save();
#endif
    for (uint32_t b = 0; b < UP_SLOTS / UPF_SLOTS; b++) { resource_release(RES_LEGACY0+b); upf_work[b]=0; }
#if MELODEE_FLASH
    irq_restore(f);
#endif
}
static void upf_empty_bank(uint32_t b)
{
    upf_t *u=upf_bank(b*UPF_SLOTS);if(!u)return;
    memset(u,0,sizeof *u);u->magic=UPF_MAGIC;u->ver=1;u->nslot=UPF_SLOTS;
}
static void upf_empty(void) { for (uint32_t b = 0; b < UP_SLOTS / UPF_SLOTS; b++) upf_empty_bank(b); }

static uint32_t upf_tag(const up_rec_t *r)       /* FNV-1a of the record without its name (bytes 4..15) */
{
    const uint8_t *b = (const uint8_t *)r;
    uint32_t h = 2166136261u, i;
    for (i = 0; i < sizeof *r; i++)
        if (i < 4u || i >= 16u)
            h = (h ^ b[i]) * 16777619u;
    return h;
}

static int upf_fm6(uint32_t k) { return up_used(k) && up_rec(k)->engine == ENGI_FM6; }

/* slot k's patch -> pk (128 bytes), 0 = it has one */
static int upf_get(uint32_t k, uint8_t *pk)
{
    uint32_t i, acc = 0, n = 0, o = 0;
    const uint8_t *d;
    if (k >= UP_SLOTS || !upf_fm6(k)) return 1;
    if(!upf_work[k/UPF_SLOTS] && native_fm_active())return 1;
    const upf_t *u = upf_bank(k); if(!u)return 1; uint32_t slot = k % UPF_SLOTS;
    if (!upf_valid(u) || !((u->used >> slot) & 1u) || u->e[slot].tag != upf_tag(up_rec(k)))
        return 1;
    d = u->e[slot].pk;
    for (i = 0; i < FM6_PACKED; i++) {
        while (n < 7u) {
            acc |= (uint32_t)d[o++] << n;
            n += 8u;
        }
        pk[i] = (uint8_t)(acc & 127u);
        acc >>= 7;
        n -= 7u;
    }
    return 0;
}

/* slot k's patch = pk (128 bytes; every value into its range), tagged with the record as it is now. RAM only */
static void upf_set(uint32_t k, const uint8_t *pk)
{
    uint8_t v[FP_SIZE + 1u], c[FM6_PACKED];
    uint32_t i, acc = 0, n = 0, o = 0;
    if (k >= UP_SLOTS)
        return;
    upf_t *u = upf_bank(k); if(!u)return; uint32_t slot = k % UPF_SLOTS;
    if (!upf_valid(u)) upf_empty_bank(k / UPF_SLOTS);
    fm6_unpack(pk, v);
    fm6_pack(v, c);
    memset(u->e[slot].pk, 0, UPF_PK);
    for (i = 0; i < FM6_PACKED; i++) {
        acc |= (uint32_t)(c[i] & 127u) << n;
        n += 7u;
        while (n >= 8u) {
            u->e[slot].pk[o++] = (uint8_t)acc;
            acc >>= 8;
            n -= 8u;
        }
    }
    u->e[slot].tag = upf_tag(up_rec(k));
    u->used |= 1u << slot;
}

/* the object to flash: 0 ok, 2 flash error, 3 no flash (kept in RAM). Its first write keeps the retired bank's
 * newest copy (see the top) */
static int upf_save_bank(uint32_t b)
{
#if MELODEE_FLASH
    if (!flash_ok) return 3;
    if(!upf_bank(b*UPF_SLOTS))return 2;
    return st_save(upf_obj(b), upf_bank((b) * UPF_SLOTS), sizeof (*upf_bank((b) * UPF_SLOTS))) ? 2 : 0;
#else
    (void)b; return 3;
#endif
}
static int upf_save(void) { int rc = 0; for (uint32_t b = 0; b < (UP_SLOTS / UPF_SLOTS); b++) { int r = upf_save_bank(b); if (r == 2) return r; rc = r; } return rc; }

static void upf_boot(void)
{
    upf_release();
#if MELODEE_FLASH
    for (uint32_t b = 0; b < (UP_SLOTS / UPF_SLOTS); b++) {
        if(!upf_bank(b*UPF_SLOTS))return;
        if (!flash_ok || st_load(upf_obj(b), upf_bank((b) * UPF_SLOTS), sizeof (*upf_bank((b) * UPF_SLOTS))) != (int)sizeof (*upf_bank((b) * UPF_SLOTS)) || !upf_valid(upf_bank((b) * UPF_SLOTS)))
            upf_empty_bank(b);
    }
#endif
}

/* A user sound loads its stored patch; a missing patch uses INIT. */
static void upf_track_load(track_t *t, uint32_t k)
{
    uint8_t pk[FM6_PACKED], v[FP_SIZE + 1u];
    uint32_t tr = (uint32_t)(t - trk);
    if (tr >= NTRK || t->eng_req != ENGI_FM6)
        return;
    if (upf_get(k, pk)) {
        if(native_used(ENGI_FM6,k))memcpy(pk,native_raw(ENGI_FM6,k),FM6_PACKED);
        else memcpy(pk, FM6_INIT, FM6_PACKED);
    }
    fm6_unpack(pk, v);
    fm6_set_patch(tr, v);
}

/* up_store: slot k (its record just written from track tr) gets that track's patch; upf_save's result */
static int upf_store(uint32_t k, uint32_t tr)
{
    uint8_t pk[FM6_PACKED];
    fm6_pack(fm6_patch[tr % NTRK], pk);
    if(native_fm_active())return native_put(ENGI_FM6,k,pk);
    upf_set(k, pk);
    return upf_save_bank(k / UPF_SLOTS);
}
