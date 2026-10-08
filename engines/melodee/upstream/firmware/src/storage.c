/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Persistent storage on the SPI NOR.
 *
 * Every object has an A/B pair (five base plus two extension sectors per bank project; one otherwise). A save goes to the copy that is not
 * the current one: erase the sector, program the payload pages (from offset
 * 256), then the 32-byte header at offset 0 LAST. The header is the commit
 * record (magic, seq, length, payload CRC, header CRC); on load the valid
 * copy with the newest seq wins (including wrap), so a write torn at any point leaves the
 * previous copy in charge.
 *
 * Flash access goes through three hooks (also used by the host test):
 *   st_read(off, dst, n)   st_erase(off)   st_prog(off, src, n)
 */
#define ST_MAGIC 0x554C4546u                   /* "FELU" */
#define ST_SECTOR 4096u
#define ST_PAYLOAD_OFF 256u
#define ST_PAYLOAD_MAX (ST_SECTOR - ST_PAYLOAD_OFF)
#define ST_BANK_BASE_MAX (5u * ST_SECTOR - ST_PAYLOAD_OFF)
#define ST_BANK_EXT_LO 0xEA000u
#define ST_BANK_EXT_MAX (2u * ST_PAYLOAD_MAX)

/* Legacy single-pattern projects remain at 0x97000..0x9EFFF for read-only migration.
 * Bank projects occupy 0xA0000..0xC7FFF, two five-sector copies per slot.
 * Expanded project copies each use two more sectors at 0xEA000..0xF9FFF (unused USR area).
 * User samples are disabled: this area must never accept sample uploads.
 * Existing flash map (FL_DATA 0x97000..0xDFFFF, FL_GLOB 0xFC000..): settings 0xFC000, projects 0x97000..0x9EFFF,
 * user sample slots 0xA0000..0xDBFFF (sample_data.c), user preset banks 0xDC000..0xDFFFF (upreset.c), the FM6
 * patch bank (fm6_bank.c): copy A 0x9F000, copy B 0xFE000 (the two free sectors) */
/* Object numbers 0..19 stay fixed. U33..U64 use E5000..E8FFF;
 * owned FM6 U01..U32 reuse the old bank pair, U33..U64 use E9000/FA000.
 * E0000..E4FFF remains reserved for OTA loader staging. */
enum { OBJ_SETTINGS, OBJ_PROJECT0, OBJ_UPRESET0 = OBJ_PROJECT0 + 4, OBJ_FM6BANK = OBJ_UPRESET0 + 2, OBJ_BANK0, OBJ_CZBANK0 = OBJ_BANK0 + 4, OBJ_UPRESET_EXT0 = OBJ_CZBANK0 + 8, OBJ_UPFM6_EXT = OBJ_UPRESET_EXT0 + 2, OBJ_NATIVEFM0, OBJ_P5BANK0 = OBJ_NATIVEFM0 + 2, OBJ_COUNT = OBJ_P5BANK0 + 5 };

typedef struct {
    uint32_t magic;
    uint16_t type, slot;
    uint32_t seq, len, crc, rsv[2];
    uint32_t hcrc;
} st_hdr_t;
_Static_assert(sizeof(st_hdr_t) == 32u, "storage commit record layout");

static int st_read(uint32_t off, void *dst, uint32_t n);
static int st_erase(uint32_t off);
static int st_prog(uint32_t off, const void *src, uint32_t n);

static uint32_t st_crc32(const void *p, uint32_t n)   /* zlib CRC-32, 4 bits per step */
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    const uint8_t *b = p;
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *b++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return ~c;
}

static uint32_t st_sector(uint32_t obj, uint32_t copy)  /* flash offset of copy A (0) / B (1) */
{
    if (obj >= OBJ_P5BANK0) return 0x89000u+(obj-OBJ_P5BANK0)*2u*ST_SECTOR+copy*ST_SECTOR;
    if (obj >= OBJ_NATIVEFM0) return 0xD8000u + (obj - OBJ_NATIVEFM0) * 2u * ST_SECTOR + copy * ST_SECTOR;
    if (obj == OBJ_UPFM6_EXT) return copy ? 0xFA000u : 0xE9000u;
    if (obj >= OBJ_UPRESET_EXT0) return 0xE5000u + (obj - OBJ_UPRESET_EXT0) * 2u * ST_SECTOR + copy * ST_SECTOR;
    if (obj >= OBJ_CZBANK0)
        return 0xC8000u + (obj - OBJ_CZBANK0) * 2u * ST_SECTOR + copy * ST_SECTOR;
    if (obj >= OBJ_BANK0)
        return 0xA0000u + (obj - OBJ_BANK0) * 10u * ST_SECTOR + copy * 5u * ST_SECTOR;
    if (obj == OBJ_SETTINGS)
        return 0xFC000u + copy * ST_SECTOR;
    if (obj == OBJ_FM6BANK)
        return copy ? 0xFE000u : 0x9F000u;
    if (obj >= OBJ_UPRESET0)
        return 0xDC000u + (obj - OBJ_UPRESET0) * 2u * ST_SECTOR + copy * ST_SECTOR;
    return 0x97000u + (obj - OBJ_PROJECT0) * 2u * ST_SECTOR + copy * ST_SECTOR;
}

static int st_banked(uint32_t obj) { return obj >= OBJ_BANK0 && obj < OBJ_CZBANK0; }
/* New preset objects start directly after the commit header, leaving the last
 * 256 bytes erased so the SPL/update loader cannot interpret musical data as OTA. */
static uint32_t st_payload_off(uint32_t obj) { return obj >= OBJ_UPRESET_EXT0 ? sizeof(st_hdr_t) : ST_PAYLOAD_OFF; }
static uint32_t st_capacity(uint32_t obj) { return st_banked(obj) ? ST_BANK_BASE_MAX + ST_BANK_EXT_MAX : obj >= OBJ_UPRESET_EXT0 ? ST_SECTOR - 256u - sizeof(st_hdr_t) : ST_PAYLOAD_MAX; }
static uint32_t st_extension(uint32_t obj, uint32_t copy)
{
    return ST_BANK_EXT_LO + ((obj - OBJ_BANK0) * 2u + copy) * 2u * ST_SECTOR;
}
/* Keep existing five-sector copies in place. Each copy has its own two-sector
 * extension in unused flash, so migration never overwrites the current copy.
 * The last 256 bytes of extension sectors stay erased: the SPL scans those
 * tails for update records, and musical data must never look like one. */
static uint32_t st_address(uint32_t obj, uint32_t copy, uint32_t off, uint32_t *span)
{
    if (!st_banked(obj) || off < ST_BANK_BASE_MAX) {
        *span = (st_banked(obj) ? ST_BANK_BASE_MAX : st_capacity(obj)) - off;
        return st_sector(obj, copy) + st_payload_off(obj) + off;
    }
    off -= ST_BANK_BASE_MAX;
    *span = ST_PAYLOAD_MAX - off % ST_PAYLOAD_MAX;
    return st_extension(obj, copy) + off / ST_PAYLOAD_MAX * ST_SECTOR + off % ST_PAYLOAD_MAX;
}
static int st_read_range(uint32_t obj, uint32_t copy, uint32_t off, void *dst, uint32_t n)
{
    if (obj >= OBJ_COUNT || copy > 1u || off > st_capacity(obj) || n > st_capacity(obj) - off) return -1;
    uint8_t *d = dst;
    while (n) {
        uint32_t span, addr = st_address(obj, copy, off, &span), k = n < span ? n : span;
        if (st_read(addr, d, k)) return -1;
        d += k; off += k; n -= k;
    }
    return 0;
}

static uint8_t st_buf[256] __attribute__((aligned(4)));

static int st_head(uint32_t obj, uint32_t copy, st_hdr_t *h)   /* commit record valid: 0 */
{
    if (obj >= OBJ_COUNT || copy > 1u)
        return -1;
    if (st_read(st_sector(obj, copy), h, sizeof *h))
        return -1;
    if (h->magic != ST_MAGIC || h->type != obj || h->slot != copy || h->len > st_capacity(obj) ||
        h->hcrc != st_crc32(h, sizeof *h - 4u))
        return -1;
    return 0;
}

static int st_body(uint32_t obj, uint32_t copy, const st_hdr_t *h)   /* streaming CRC, bounded 256-byte scratch, valid: 0 */
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t off = 0; off < h->len; off += sizeof st_buf) {
        uint32_t n = h->len - off > sizeof st_buf ? sizeof st_buf : h->len - off;
        if (st_read_range(obj, copy, off, st_buf, n)) return -1;
        for (uint32_t i = 0; i < n; i++) {
            crc ^= st_buf[i];
            for (uint32_t j = 0; j < 8u; j++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc == h->crc ? 0 : -1;

}

/* the current copy: the newest valid sequence (A on a tie), -1 when
 * neither is valid. Headers first, so only the winner's payload is read (it
 * is verified); *h gets its header. */
static int st_current(uint32_t obj, st_hdr_t *h)
{
    st_hdr_t a, b;
    int va = st_head(obj, 0, &a) == 0, vb = st_head(obj, 1, &b) == 0;
    if (vb && (!va || (b.seq != a.seq && b.seq - a.seq < 0x80000000u))) {
        if (st_body(obj, 1, &b) == 0) {
            *h = b;
            return 1;
        }
        vb = 0;
    }
    if (va && st_body(obj, 0, &a) == 0) {
        *h = a;
        return 0;
    }
    if (vb && st_body(obj, 1, &b) == 0) {
        *h = b;
        return 1;
    }
    return -1;
}

/* load the whole object into dst; returns its length, or -1 if it does not fit */
static int st_load(uint32_t obj, void *dst, uint32_t max)
{
    st_hdr_t h;
    int cur;
    if (obj >= OBJ_COUNT || (cur = st_current(obj, &h)) < 0 || h.len > max) return -1;
    if (st_read_range(obj, (uint32_t)cur, 0, dst, h.len)) return -1;
    return (int)h.len;
}

static int st_save(uint32_t obj, const void *src, uint32_t len)
{
    uint32_t seq, base, off;
    int cur, rc;
    st_hdr_t h;
    if (obj >= OBJ_COUNT || len > st_capacity(obj))
        return -1;
    cur = st_current(obj, &h);
    seq = cur < 0 ? 0u : h.seq;
    base = st_sector(obj, cur == 0 ? 1u : 0u);       /* write the other copy */
    uint32_t crc = st_crc32(src, len);
    uint32_t extent = obj >= OBJ_BANK0 && obj < OBJ_CZBANK0 ? 5u * ST_SECTOR : ST_SECTOR;
    for (off = 0; off < extent; off += ST_SECTOR)
        if ((rc = st_erase(base + off)) != 0) return rc;
    if (st_banked(obj)) for (off = 0; off < 2u * ST_SECTOR; off += ST_SECTOR)
        if ((rc = st_erase(st_extension(obj, cur == 0 ? 1u : 0u) + off)) != 0) return rc;
    for (off = 0; off < len; off += sizeof st_buf) {
        uint32_t n = len - off > sizeof st_buf ? sizeof st_buf : len - off;
        memcpy(st_buf, (const uint8_t *)src + off, n);
        uint32_t span, addr = st_address(obj, cur == 0 ? 1u : 0u, off, &span);
        if ((rc = st_prog(addr, st_buf, n)) != 0) return rc;
    }
    h.magic = ST_MAGIC;
    h.type = (uint16_t)obj;
    h.slot = (uint16_t)(cur == 0 ? 1 : 0);
    h.seq = seq + 1u;
    h.len = len;
    h.crc = crc;
    h.rsv[0] = h.rsv[1] = 0xFFFFFFFFu;
    h.hcrc = st_crc32(&h, sizeof h - 4u);
    if ((rc = st_prog(base, &h, sizeof h)) != 0)       /* the commit record, last */
        return rc;
    {   /* read back: a write-protected or failing part must not report SAVED */
        st_hdr_t chk;
        uint32_t c = cur == 0 ? 1u : 0u;
        if (st_head(obj, c, &chk) || memcmp(&chk, &h, sizeof h) || st_body(obj, c, &chk))
            return -7;
    }
    return 0;
}
