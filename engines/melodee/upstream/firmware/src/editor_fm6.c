/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Editor protocol: the FM6 patches (EDITOR_PROTOCOL.md "FM6 patches", cmds 68..71). A patch travels as the
 * 128-byte packed record (every byte 7-bit: no pack7). Targets: 0 a track's own patch (index 0..3), 1 the patch bank
 * (retired in 1.0.3: GET / PUT / ERASE answer rc 3, "no bank"; LIST counts 0 bank slots), 2 a factory patch
 * (0..FM6_NFAC-1, read only), 3 a user preset's patch (0..UP_SLOTS-1: an FM6 user preset; up_fm6.c). */
enum { ED_FM6_GET = 68, ED_FM6_PUT, ED_FM6_LIST, ED_FM6_ERASE };
enum { ED_FM6_TRACK, ED_FM6_BANK, ED_FM6_FACTORY, ED_FM6_USER };
#define ED_FM6_NOBANK 3u                                   /* rc: this firmware has no patch bank */

static void ed_fm6_name(const uint8_t *pk)       /* the record's name, trailing spaces off */
{
    char s[11];
    uint32_t i, n = 0;
    for (i = 0; i < 10u; i++) {
        s[i] = (char)(pk[118 + i] >= 32u && pk[118 + i] <= 126u ? pk[118 + i] : ' ');
        if (s[i] != ' ')
            n = i + 1u;
    }
    s[n] = 0;
    ed_str(s, 10);
}

static int ed_fm6_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    uint8_t pk[FM6_PACKED];
    uint32_t i, rc;
    switch (cmd) {
    case ED_FM6_GET:                                       /* target, index -> target, index, rc, [128 bytes] */
        rc = n != 2u || a[0] > ED_FM6_USER ? 1u : 0u;
        if (!rc) {
            if (a[0] == ED_FM6_TRACK && a[1] < NTRK)
                fm6_pack(fm6_patch[a[1]], pk);
            else if (a[0] == ED_FM6_BANK)
                rc = ED_FM6_NOBANK;
            else if (a[0] == ED_FM6_FACTORY && a[1] < FM6_NFAC)
                fm6_factory(a[1], pk);
            else if (a[0] == ED_FM6_USER && a[1] < UP_SLOTS)
                {if(native_used(ENGI_FM6,a[1]))memcpy(pk,native_raw(ENGI_FM6,a[1]),FM6_PACKED);else rc=upf_get(a[1],pk)?2u:0u;}
            else
                rc = 1;
        }
        ed_b(n ? a[0] : 127u); ed_b(n > 1u ? a[1] : 127u); ed_b(rc);
        for (i = 0; !rc && i < FM6_PACKED; i++)
            ed_b(pk[i]);
        return 1;
    case ED_FM6_PUT:                                       /* target, index, 128 bytes -> target, index, rc */
        rc = n != 2u + FM6_PACKED ? 1u : 0u;
        if (!rc && a[0] == ED_FM6_TRACK && a[1] < NTRK) {
            uint8_t v[FP_SIZE + 1u];
            fm6_unpack(a + 2, v);                          /* (every value into its range) */
            fm6_set_patch(a[1], v);
            fm6_adopt(a[1]);                               /* the track's own patch now: SLOT OWN (F n if it is that
                                                            * factory patch unchanged); fm6_poll keeps it */
            ui.force = 1;
        } else if (!rc && a[0] == ED_FM6_BANK) {
            rc = ED_FM6_NOBANK;
        } else if (!rc && a[0] == ED_FM6_USER && a[1] < UP_SLOTS && upf_fm6(a[1])) {
            int r = 2;
            if (!ed_flash_stop()) {
                upf_set(a[1], a + 2);
                r = native_fm_active()?native_put(ENGI_FM6,a[1],a+2):upf_save_bank(a[1] / UPF_SLOTS);
            }
            rc = r == 2 ? 2u : 0u;                         /* (3, no flash: kept in RAM) */
        } else {
            rc = 1;
        }
        ed_b(n ? a[0] : 127u); ed_b(n > 1u ? a[1] : 127u); ed_b(rc);
        return 1;
    case ED_FM6_LIST:                                      /* -> factory count, bank count (0), then per slot: used, name */
        if (n)
            return 0;
        ed_b(FM6_NFAC); ed_b(0);
        for (i = 0; i < FM6_NFAC; i++) {
            ed_b(1);
            fm6_factory(i, pk); ed_fm6_name(pk);
        }
        return 1;
    case ED_FM6_ERASE:                                     /* bank index -> index, rc (no bank: 3) */
        rc = n != 1u ? 1u : ED_FM6_NOBANK;
        ed_b(n ? a[0] : 127u); ed_b(rc);
        return 1;
    }
    return 0;
}
