/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modifications Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Retired FM6 bank formats, retained only for boot and backup migration.
 * Melodee v2: 32 voices packed eight 7-bit bytes into seven; the older Melodee
 * format has only a magic and these voices. Felucca v1 has 27 unpacked voices.
 * New writes use user-preset-owned voices in up_fm6.c. */
#define FM6_BANK_MAGIC 0x42364D46u               /* "FM6B" */
#define FM6_BANK_VER 2u
#define FM6_BANK_PK 112u                         /* a slot's 128 bytes, 7-bit packed */
typedef struct {
    uint32_t magic;
    uint16_t ver, nslot;                         /* FM6_BANK_VER, FM6_BANK_N */
    uint32_t used;                               /* bit k: slot k holds a patch */
    uint8_t fn[FM6_NFN];                         /* inert: the function settings, which are the tracks' now (kept
                                                  * as stored; a new bank: Dexed's) */
    uint8_t pk[FM6_BANK_N][FM6_BANK_PK];
} fm6_bank_t;
_Static_assert(sizeof(fm6_bank_t) == 3612u && sizeof(fm6_bank_t) <= 3840u, "FM6 bank layout (a backup object)");
static fm6_bank_t fm6_bank __attribute__((section(".pool"))); /* boot/restore migration scratch */

/* eight 7-bit bytes <-> seven: the eighth rides in the top bits of the other seven */
static void fm6_pack7(uint8_t *d, const uint8_t *s, uint32_t n)
{
    uint32_t i, j;
    for (i = 0; i < n; i += 8u, d += 7, s += 8)
        for (j = 0; j < 7u; j++)
            d[j] = (uint8_t)((s[j] & 0x7Fu) | ((s[7] >> j) & 1u) << 7);
}

static void fm6_unpack7(uint8_t *d, const uint8_t *s, uint32_t n)
{
    uint32_t i, j;
    for (i = 0; i < n; i += 8u, d += 8, s += 7) {
        d[7] = 0;
        for (j = 0; j < 7u; j++) {
            d[j] = s[j] & 0x7Fu;
            d[7] |= (uint8_t)((s[j] >> 7) << j);
        }
    }
}

static int fm6_bank_valid(const fm6_bank_t *b)
{
    uint32_t k;
    if (b->magic != FM6_BANK_MAGIC || b->ver != FM6_BANK_VER || b->nslot != FM6_BANK_N)
        return 0;
    for (k = 0; k < FM6_NFN; k++)
        if (b->fn[k] > FM6_FNMAX[k])
            return 0;
    return 1;
}

static void fm6_bank_empty(void)
{
    memset(&fm6_bank, 0, sizeof fm6_bank);
    fm6_bank.magic = FM6_BANK_MAGIC;
    fm6_bank.ver = FM6_BANK_VER;
    fm6_bank.nslot = FM6_BANK_N;
    memcpy(fm6_bank.fn, FM6_FNDEF, FM6_NFN);
}

/* a bank of earlier firmware (n bytes at b) -> the mirror. 0 = it was one */
static int fm6_bank_import(const uint8_t *b, int n)
{
    uint32_t magic = b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24, k;
    if (magic != FM6_BANK_MAGIC)
        return 1;
    if (n == 4 + (int)(FM6_BANK_N * FM6_BANK_PK)) {      /* Melodee before: 32 packed voices, all of them used */
        fm6_bank_empty();
        memcpy(fm6_bank.pk, b + 4, FM6_BANK_N * FM6_BANK_PK);
        fm6_bank.used = 0xFFFFFFFFu;
        return 0;
    }
    if (n == 16 + 27 * 128 && b[4] == 1u && b[6] == 27u) {   /* Felucca 1.0: 27 records, a used mask */
        uint32_t used = b[8] | (uint32_t)b[9] << 8 | (uint32_t)b[10] << 16 | (uint32_t)b[11] << 24;
        uint8_t v[FP_SIZE + 1u], pk[FM6_PACKED];
        fm6_bank_empty();
        for (k = 0; k < 27u; k++)
            if ((used >> k) & 1u) {
                fm6_unpack(b + 16 + k * 128u, v);           /* (every value into its range) */
                memset(pk, 0, sizeof pk);
                fm6_pack(v, pk);
                fm6_pack7(fm6_bank.pk[k], pk, FM6_PACKED);
                fm6_bank.used |= 1u << k;
            }
        return 0;
    }
    return 1;
}
