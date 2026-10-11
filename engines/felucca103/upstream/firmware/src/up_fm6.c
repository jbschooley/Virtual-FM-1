/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The user presets' FM6 patches (since 1.0.3): an FM6 user preset carries its track's patch, as a project does.
 * One storage.c object (OBJ_UPFM6, A/B: 0x9F000 / 0xFE000), mirrored in RAM (the pool): per user preset slot the
 * 128-byte packed patch (eng_fm6.c fm6_pack) with its 7-bit bytes packed 8 to the byte (112 bytes), and a tag.
 *
 * The tag ties an entry to its record: a hash of the user preset record (upreset.c up_rec_t) without its name. An
 * entry counts only while its record is an FM6 sound and still has that hash, so a rename keeps the patch, and any
 * other change of the record (UP_PUT from an editor that sends no patch, an erase, a restore of the banks, a power
 * cut between the record's write and this object's) leaves the record without one. Such a record loads as before
 * 1.0.3: SLOT F n -> that factory patch, OWN -> the init voice. The record's write comes first, this object's second
 * (up_store): a cut between them never gives a record another record's patch.
 *
 * The FM6 patch bank of 1.0..1.0.2 (OBJ_FM6BANK, 27 slots B1..B27 that SLOT 8..34 loaded; the editor's FM6 target
 * 1, backup id 8) is retired. Its patches move into the user presets that used them, once (upf_boot):
 *   1. no valid OBJ_UPFM6 copy: the bank's newest valid copy is read; every FM6 user preset whose stored SLOT is
 *      8..34 (B1..B27) and has no entry gets that bank slot's patch (an empty bank slot: none, the init voice as
 *      before; SLOT F1..F8: none, which loads the factory patch as before);
 *   2. the object is written into the sector that does NOT hold that bank copy (st_save_to), and read back;
 *   3. a valid OBJ_UPFM6 copy is the "migration done" mark: the bank is never read again, and the next save of
 *      this object (A/B as any other) may erase the bank's last sector.
 * A power cut in 2 leaves the bank's copy whole and no valid OBJ_UPFM6: the next boot starts again at 1
 * (idempotent: the records did not change). No bank, or no flash: nothing to move, nothing written.
 * A full backup of older firmware (id 8, the bank) restores the same way into the restored user presets
 * (upf_migrate, editor_backup.c). Included by upreset.c. */
#define FM6_BANK_MAGIC 0x42364D46u               /* "FM6B": the retired bank (read only) */
#define FM6_BANK_N 27u
typedef struct {
    uint32_t magic;
    uint16_t ver, nslot;                         /* 1, FM6_BANK_N */
    uint32_t used;                               /* bit k: slot k holds a patch */
    uint32_t rsv;
    uint8_t v[FM6_BANK_N][FM6_PACKED];
} fm6_bank_t;
_Static_assert(sizeof(fm6_bank_t) == 3472u, "FM6 bank layout");

static int fm6_bank_valid(const fm6_bank_t *b)
{
    uint32_t k, i;
    if (b->magic != FM6_BANK_MAGIC || b->ver != 1u || b->nslot != FM6_BANK_N || (b->used >> FM6_BANK_N))
        return 0;
    for (k = 0; k < FM6_BANK_N; k++)
        for (i = 0; i < FM6_PACKED; i++)
            if (b->v[k][i] > 127u)
                return 0;
    return 1;
}

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
    upf_ent_t e[UP_SLOTS];
} upf_t;
_Static_assert(sizeof(upf_t) == 3728u, "user preset FM6 patches layout (at most ST_PAYLOAD_MAX, 3840)");
_Static_assert(sizeof(up_rec_t) == 192u && sizeof(((up_rec_t *)0)->name) == 12u, "upf_tag: the name at bytes 4..15");
static upf_t upf __attribute__((section(".pool")));

static int upf_valid(const upf_t *u) { return u->magic == UPF_MAGIC && u->ver == 1u && u->nslot == UP_SLOTS; }

static void upf_empty(void)
{
    memset(&upf, 0, sizeof upf);
    upf.magic = UPF_MAGIC;
    upf.ver = 1;
    upf.nslot = UP_SLOTS;
}

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
    if (k >= UP_SLOTS || !upf_valid(&upf) || !((upf.used >> k) & 1u) || !upf_fm6(k) ||
        upf.e[k].tag != upf_tag(up_rec(k)))
        return 1;
    d = upf.e[k].pk;
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
    if (!upf_valid(&upf))
        upf_empty();
    fm6_unpack(pk, v);
    fm6_pack(v, c);
    memset(upf.e[k].pk, 0, UPF_PK);
    for (i = 0; i < FM6_PACKED; i++) {
        acc |= (uint32_t)(c[i] & 127u) << n;
        n += 7u;
        while (n >= 8u) {
            upf.e[k].pk[o++] = (uint8_t)acc;
            acc >>= 8;
            n -= 8u;
        }
    }
    upf.e[k].tag = upf_tag(up_rec(k));
    upf.used |= 1u << k;
}

/* the object to flash: 0 ok, 2 flash error, 3 no flash (kept in RAM). Its first write keeps the retired bank's
 * newest copy (see the top) */
static int upf_save(void)
{
#if FELUCCA_FLASH
    st_hdr_t h;
    int to = -1;
    if (!flash_ok)
        return 3;
    if (st_current(OBJ_UPFM6, &h) < 0)
        to = st_current(OBJ_FM6BANK, &h) == 0 ? 1 : 0;
    return st_save_to(OBJ_UPFM6, &upf, sizeof upf, to) ? 2 : 0;
#else
    return 3;
#endif
}

/* the retired bank's patches -> the FM6 user presets whose stored SLOT is a B slot and have no patch yet; the count */
static uint32_t upf_migrate(const fm6_bank_t *b)
{
    uint32_t k, moved = 0;
    if (!upf_valid(&upf))
        upf_empty();
    for (k = 0; k < UP_SLOTS; k++) {
        const up_rec_t *r = up_rec(k);
        uint8_t pk[FM6_PACKED];
        int32_t s;
        if (!upf_fm6(k) || !upf_get(k, pk))
            continue;
        s = up_value(r, r->np - 1u) - (int32_t)FM6_NFACTORY;   /* SLOT (P_E7): the record's last value */
        if (s >= 0 && s < (int32_t)FM6_BANK_N && ((b->used >> s) & 1u)) {
            upf_set(k, b->v[s]);
            moved++;
        }
    }
    return moved;
}

static void upf_boot(void)                       /* up_boot, after the user preset banks */
{
#if FELUCCA_FLASH
    st_hdr_t h;
    if (flash_ok && st_load(OBJ_UPFM6, &upf, sizeof upf) == (int)sizeof upf && upf_valid(&upf))
        return;
    upf_empty();
    if (flash_ok && st_current(OBJ_FM6BANK, &h) >= 0 && h.len == sizeof(fm6_bank_t) &&
        fm6_bank_valid((const fm6_bank_t *)st_buf)) {
        upf_migrate((const fm6_bank_t *)st_buf);   /* (st_buf holds the bank until upf_save) */
        upf_save();
    }
#else
    upf_empty();
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
        memcpy(pk, s >= 0 && s < (int32_t)FM6_NFACTORY ? FM6_FACTORY[s] : FM6_INIT, FM6_PACKED);
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
    return upf_save();
}
