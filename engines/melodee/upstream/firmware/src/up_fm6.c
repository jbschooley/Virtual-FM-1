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
static upf_t upf[UP_SLOTS / UPF_SLOTS] __attribute__((section(".pool")));
#if MELODEE_FLASH
static uint32_t upf_obj(uint32_t b) { return b ? OBJ_UPFM6_EXT : OBJ_FM6BANK; }
#endif
static upf_t *upf_bank(uint32_t k) { return &upf[k / UPF_SLOTS]; }

static int upf_valid(const upf_t *u) { return u->magic == UPF_MAGIC && u->ver == 1u && u->nslot == UPF_SLOTS; }

static void upf_empty_bank(uint32_t b)
{
    memset(&upf[b], 0, sizeof upf[b]);
    upf[b].magic = UPF_MAGIC; upf[b].ver = 1; upf[b].nslot = UPF_SLOTS;
}
static void upf_empty(void) { for (uint32_t b = 0; b < NELEM(upf); b++) upf_empty_bank(b); }

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
    const upf_t *u = upf_bank(k); uint32_t slot = k % UPF_SLOTS;
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
    upf_t *u = upf_bank(k); uint32_t slot = k % UPF_SLOTS;
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
    return st_save(upf_obj(b), &upf[b], sizeof upf[b]) ? 2 : 0;
#else
    (void)b; return 3;
#endif
}
static int upf_save(void) { int rc = 0; for (uint32_t b = 0; b < NELEM(upf); b++) { int r = upf_save_bank(b); if (r == 2) return r; rc = r; } return rc; }

/* the retired bank's patches -> the FM6 user presets whose stored SLOT is a B slot and have no patch yet; the count */
static uint32_t upf_migrate(const fm6_bank_t *b)
{
    uint32_t k, moved = 0;

    for (k = 0; k < UP_SLOTS; k++) {
        const up_rec_t *r = up_rec(k);
        uint8_t pk[FM6_PACKED];
        int32_t s;
        if (!upf_fm6(k) || !upf_get(k, pk))
            continue;
        s = up_value(r, r->np - 1u) - (int32_t)FM6_NFAC;   /* SLOT (P_E7): the record's last value */
        if (s >= 0 && s < (int32_t)FM6_BANK_N && ((b->used >> s) & 1u)) {
            fm6_unpack7(pk, b->pk[s], FM6_PACKED);
            upf_set(k, pk);
            moved++;
        }
    }
    return moved;
}

static void upf_boot(void)
{
    upf_empty();
#if MELODEE_FLASH
    for (uint32_t b = 0; b < NELEM(upf); b++) {
        if (!flash_ok || st_load(upf_obj(b), &upf[b], sizeof upf[b]) != (int)sizeof upf[b] || !upf_valid(&upf[b]))
            upf_empty_bank(b);
    }
    /* Object 7 is reused, so A/B commits preserve the last old bank during migration.
     * Only its historical formats trigger migration; a valid UPF6 is the completion marker. */
    int n = flash_ok ? st_load(OBJ_FM6BANK, fm6_rx, sizeof fm6_rx) : -1;
    if (n == sizeof fm6_bank) memcpy(&fm6_bank, fm6_rx, sizeof fm6_bank);
    if ((n == sizeof fm6_bank && fm6_bank_valid(&fm6_bank)) || (n >= 4 && !fm6_bank_import(fm6_rx, n))) {
        upf_migrate(&fm6_bank);
        upf_save_bank(0);
    }
#endif
}

/* up_load: track t (its values just loaded from slot k) gets the record's patch; without one, as before 1.0.3:
 * SLOT F n that factory patch, OWN the init voice. SLOT then shows F n or OWN (fm6_adopt) */
static void upf_track_load(track_t *t, uint32_t k)
{
    uint8_t pk[FM6_PACKED], v[FP_SIZE + 1u];
    uint32_t tr = (uint32_t)(t - trk);
    if (tr >= NTRK || t->eng_req != ENGI_FM6)
        return;
    if (upf_get(k, pk)) {
        int32_t s = t->p[P_E7];
        if(native_used(ENGI_FM6,k))memcpy(pk,native_raw(ENGI_FM6,k),FM6_PACKED);
        else if (s >= 0 && s < (int32_t)FM6_NFAC) fm6_factory((uint32_t)s, pk);
        else memcpy(pk, FM6_INIT, FM6_PACKED);
    }
    fm6_unpack(pk, v);
    fm6_set_patch(tr, v);
    fm6_adopt(tr);
}

/* up_store: slot k (its record just written from track tr) gets that track's patch; upf_save's result */
static int upf_store(uint32_t k, uint32_t tr)
{
    uint8_t pk[FM6_PACKED];
    fm6_pack(fm6_patch[tr % NTRK], pk);
    upf_set(k, pk);
    if(native_fm_active())return native_put(ENGI_FM6,k,pk);
    return upf_save_bank(k / UPF_SLOTS);
}
