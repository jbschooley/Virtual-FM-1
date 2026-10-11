/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The FM6 voice bank (1.4.1, Discussion #168): 32 voices loaded at once from the web editor (a 32-voice .syx, its
 * 128-byte packed records as they are), picked on the device with FM6's SLOT B1..B32 (eng_fm6.c). A voice is read
 * from flash when SLOT loads it, into the track's patch as F1..F8 load theirs: the patch is the track's from then on,
 * and a project or a user preset keeps it whatever the bank holds later. RAM: the used mask (eng_fm6.c
 * fm6_bank_used) and the transfer's state, in the pool; no copy of the bank.
 *
 * Where: flash 0xE7000 (fm1_flash.h FL_VBANK_*), the sector above the autosave's pair, which nothing else of Felucca
 * uses (the map: storage.c). Like the autosave's sectors it lies in the margin the stock firmware's own update stages
 * into, only while the stock firmware runs: a later Felucca finds no valid bank there (commit record, CRC) and starts
 * with none. Sector: the commit record (storage.c st_hdr_t, type OBJ_FM6VB) at 0, the bank's header (fvb_info_t) at
 * 224, the 32 voices at 256..3839, 112 bytes each (the record's 7-bit bytes 8 to 7, up_fm6.c upf_pk_put); the record's
 * CRC covers 224..3839. The tail from 0xF00 on stays erased: the record scans of the update loader and the SPL (a 4 KiB
 * boundary - 256) never find anything there.
 *
 * A new bank (the editor's FM6B_BEGIN / WRITE / END, editor_fm6.c) is written into the autosave's older copy first
 * (0xE5000 or 0xE6000: the one st_save writes next, never the copy the power-on restore takes), its commit record (slot
 * 1, the next seq) last and only once the voices match the editor's CRC; then it is copied to 0xE7000 (erase, payload,
 * commit record, read back). A power cut anywhere leaves the old bank (nothing committed) or the new one (committed:
 * fvb_boot finishes the copy at the next power-on, before any autosave can take that sector): never half of one. A
 * wrong CRC commits nothing. An autosave between two requests (it waits 10 s after the last, project.c) takes the
 * sector: WRITE and END find its commit record there and answer 5 (start again). Included by upreset.c. */
#define FVB_HOME 0xE7000u
#define FVB_INFO 224u                          /* the bank's header; the voices from FVB_VOICES */
#define FVB_VOICES 256u
#define FVB_MAGIC 0x56364D46u                  /* "FM6V" */
typedef struct {
    uint32_t magic;
    uint16_t ver, n;                           /* 1, FM6_NBANK */
    uint32_t used;                             /* bit k: voice k (B k+1) is there */
    char name[12];                             /* the bank's name: 10 ASCII bytes, space-padded, then 0 0 */
    uint32_t rsv[2];
} fvb_info_t;
_Static_assert(sizeof(fvb_info_t) == FVB_VOICES - FVB_INFO, "FM6 voice bank header");
#define FVB_LEN (FVB_VOICES - FVB_INFO + FM6_NBANK * UPF_PK)   /* the record's payload: 3616 bytes */
_Static_assert(FVB_INFO + FVB_LEN == 0xF00u, "the sector's tail (0xF00..) stays erased");

static struct {
    uint32_t used;                             /* the voices written since FM6B_BEGIN */
    uint8_t on, copy;                          /* a transfer is open; its sector: the autosave's copy A (0) / B (1) */
} fvb_tx __attribute__((section(".pool")));    /* (zeroed at boot) */

/* the bank was replaced: a track on a bank slot keeps the voice it loaded, as its own patch (SLOT OWN) */
static void fm6_bank_changed(void)
{
    uint32_t tr;
    for (tr = 0; tr < NTRK; tr++)
        if (trk[tr].eng_req == ENGI_FM6 && fm6_bank_slot(fm6_slot[tr]) >= 0)
            fm6_adopt(tr);
}

#if FELUCCA_FLASH
_Static_assert(FVB_LEN <= ST_PAYLOAD_MAX, "the payload fits st_buf");
static uint32_t fvb_stage(void) { return st_sector(OBJ_AUTOSAVE, fvb_tx.copy); }

/* n bytes from RAM to flash at off, a page (256 bytes) at a time */
static int fvb_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    while (n) {
        uint32_t k = 256u - (off & 255u);
        if (k > n)
            k = n;
        if (st_prog(off, s, k))
            return -1;
        off += k;
        s += k;
        n -= k;
    }
    return 0;
}

/* a whole bank at base: its commit record (of slot `slot`: 0 at FVB_HOME, 1 staged) and payload valid; 0, with *h
 * its commit record and st_buf its payload */
static int fvb_valid(uint32_t base, uint32_t slot, st_hdr_t *h)
{
    const fvb_info_t *in = (const fvb_info_t *)st_buf;
    if (st_read(base, h, sizeof *h) || h->magic != ST_MAGIC || h->type != OBJ_FM6VB || h->slot != slot ||
        h->len != FVB_LEN || h->hcrc != st_crc32(h, sizeof *h - 4u) || st_read(base + FVB_INFO, st_buf, FVB_LEN) ||
        st_crc32(st_buf, FVB_LEN) != h->crc)
        return -1;
    return in->magic == FVB_MAGIC && in->ver == 1u && in->n == FM6_NBANK ? 0 : -1;
}

/* the commit record of the payload in st_buf, at base (written last) */
static int fvb_commit(uint32_t base, uint32_t slot, uint32_t seq)
{
    st_hdr_t h;
    h.magic = ST_MAGIC;
    h.type = OBJ_FM6VB;
    h.slot = (uint16_t)slot;
    h.seq = seq;
    h.len = FVB_LEN;
    h.crc = st_crc32(st_buf, FVB_LEN);
    h.rsv[0] = h.rsv[1] = 0xFFFFFFFFu;
    h.hcrc = st_crc32(&h, sizeof h - 4u);
    return st_prog(base, &h, sizeof h);
}

/* bank voice k -> pk (128 bytes): 0, 1 = none there (eng_fm6.c fm6_bank_read) */
static int fvb_read(uint32_t k, uint8_t *pk)
{
    uint8_t d[UPF_PK];
    if (k >= FM6_NBANK || !((fm6_bank_used >> k) & 1u) || st_read(FVB_HOME + FVB_VOICES + k * UPF_PK, d, UPF_PK))
        return 1;
    upf_pk_get(d, pk);
    return 0;
}

/* up_boot, and after a bank was staged: a staged bank newer than 0xE7000's is copied there; then the voices of the
 * bank at 0xE7000 (none without one) */
static void fvb_boot(void)
{
    st_hdr_t h, s, t;
    uint32_t c, best = 2u;
    int home;
    fm6_bank_used = 0;
    fm6_bank_read = fvb_read;
    if (!flash_ok)
        return;
    home = fvb_valid(FVB_HOME, 0, &h);
    for (c = 0; c < 2u; c++)
        if (!fvb_valid(st_sector(OBJ_AUTOSAVE, c), 1, &t) && (home || (t.seq != h.seq && t.seq - h.seq < 0x80000000u)) &&
            (best > 1u || (t.seq != s.seq && t.seq - s.seq < 0x80000000u))) {
            s = t;
            best = c;
        }
    if (best < 2u && !fvb_valid(st_sector(OBJ_AUTOSAVE, best), 1, &s) && !st_erase(FVB_HOME) &&
        !fvb_prog(FVB_HOME + FVB_INFO, st_buf, FVB_LEN))
        fvb_commit(FVB_HOME, 0, s.seq);
    if (!fvb_valid(FVB_HOME, 0, &h))
        fm6_bank_used = ((const fvb_info_t *)st_buf)->used;
}

/* the transfer's sector holds only what it wrote: no autosave took it since FM6B_BEGIN (its commit record still
 * erased); else the transfer ends */
static int fvb_tx_ok(void)
{
    uint8_t b[32];
    uint32_t i, x = 0xFFu;
    if (!fvb_tx.on || st_read(fvb_stage(), b, sizeof b))
        return 0;
    for (i = 0; i < sizeof b; i++)
        x &= b[i];
    if (x != 0xFFu)
        fvb_tx.on = 0;
    return fvb_tx.on;
}

/* FM6B_BEGIN: the sector erased, no voice yet. rc 0, 4 flash */
static uint32_t fvb_begin(void)
{
    st_hdr_t h;
    fvb_tx.on = 0;
    if (!flash_ok)
        return 4;
    fvb_tx.copy = st_current(OBJ_AUTOSAVE, &h) == 0 ? 1u : 0u;   /* the copy st_save writes next */
    if (st_erase(fvb_stage()))
        return 4;
    fvb_tx.used = 0;
    fvb_tx.on = 1;
    return 0;
}

/* FM6B_WRITE: voice k = pk (128 bytes), once each. rc 0, 1 index, 4 flash, 5 no transfer */
static uint32_t fvb_write(uint32_t k, const uint8_t *pk)
{
    uint8_t d[UPF_PK];
    if (!fvb_tx_ok())
        return 5;
    if (k >= FM6_NBANK || ((fvb_tx.used >> k) & 1u))
        return 1;
    upf_pk_put(pk, d);
    if (fvb_prog(fvb_stage() + FVB_VOICES + k * UPF_PK, d, UPF_PK)) {
        fvb_tx.on = 0;
        return 4;
    }
    fvb_tx.used |= 1u << k;
    return 0;
}

/* FM6B_END: name (10 bytes), crc: the CRC-32 of the written voices' records in index order. Staged, committed,
 * copied to 0xE7000. rc 0, 2 the CRC (nothing changed), 4 flash, 5 no transfer */
static uint32_t fvb_end(const uint8_t *name, uint32_t crc)
{
    fvb_info_t *in = (fvb_info_t *)st_buf;
    st_hdr_t h;
    uint32_t k, c = 0xFFFFFFFFu, seq, base = fvb_stage();
    if (!fvb_tx_ok())
        return 5;
    fvb_tx.on = 0;
    for (k = 0; k < FM6_NBANK; k++)
        if ((fvb_tx.used >> k) & 1u) {
            uint8_t d[UPF_PK], pk[FM6_PACKED];
            if (st_read(base + FVB_VOICES + k * UPF_PK, d, UPF_PK))
                return 4;
            upf_pk_get(d, pk);
            c = st_crc32_add(c, pk, FM6_PACKED);
        }
    if (~c != crc)
        return 2;
    seq = fvb_valid(FVB_HOME, 0, &h) ? 0u : h.seq;           /* after the newest bank anywhere */
    for (k = 0; k < 2u; k++)
        if (!fvb_valid(st_sector(OBJ_AUTOSAVE, k), 1, &h) && h.seq - seq < 0x80000000u)
            seq = h.seq;
    seq++;
    if (st_read(base + FVB_INFO, st_buf, FVB_LEN))           /* the voices as written, the header still erased */
        return 4;
    memset(in, 0, sizeof *in);
    in->magic = FVB_MAGIC;
    in->ver = 1;
    in->n = FM6_NBANK;
    in->used = fvb_tx.used;
    for (k = 0; k < 10u; k++)
        in->name[k] = (char)(name[k] >= 32u && name[k] <= 126u ? name[k] : ' ');
    if (fvb_prog(base + FVB_INFO, in, sizeof *in) || fvb_commit(base, 1, seq) || fvb_valid(base, 1, &h))
        return 4;
    fvb_boot();                                              /* the copy to 0xE7000, the voices */
    fm6_bank_changed();
    return fvb_valid(FVB_HOME, 0, &h) || h.seq != seq ? 4u : 0u;
}

/* FM6B_LIST: the bank at 0xE7000 -> its header (0 = none) */
static const fvb_info_t *fvb_info(void)
{
    st_hdr_t h;
    return flash_ok && !fvb_valid(FVB_HOME, 0, &h) ? (const fvb_info_t *)st_buf : 0;
}
#else
static void fvb_boot(void) {}
static uint32_t fvb_begin(void) { return 4; }
static uint32_t fvb_write(uint32_t k, const uint8_t *pk) { (void)k; (void)pk; return 5; }
static uint32_t fvb_end(const uint8_t *name, uint32_t crc) { (void)name; (void)crc; return 5; }
static const fvb_info_t *fvb_info(void) { return 0; }
#endif
