/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Read-only previous DX7 objects, used only while embedding old sidecars. */
#define NATIVE_FM_MAGIC 0x314D464Eu
#define NATIVE_FM_SLOTS UP_SLOTS
#define NATIVE_CZ_SLOTS UP_SLOTS
typedef struct { uint32_t magic; uint16_t ver,slots; uint32_t used; uint8_t tone[16][FM6_PACKED]; } native_fm_t;
#if MELODEE_FLASH
static uint32_t native_fm_obj(uint32_t b){return b<2u?OBJ_NATIVEFM0+b:b==2u?OBJ_FM6BANK:OBJ_UPFM6_EXT;}
static native_fm_t *bank_fm(void);
#define native_fm_cache (*bank_fm())
static uint8_t native_fm_cached=255;
static native_fm_t *native_fm_bank(uint32_t b)
{
    if(native_fm_cached!=b){bank_claim(BK_FM);int n=flash_ok?st_load(native_fm_obj(b),&native_fm_cache,sizeof native_fm_cache):-1;if(n!=sizeof native_fm_cache)memset(&native_fm_cache,0,sizeof native_fm_cache);native_fm_cached=(uint8_t)b;}
    return &native_fm_cache;
}
#else
static native_fm_t native_fm_host[4] __attribute__((section(".pool")));
static native_fm_t *native_fm_bank(uint32_t b){return &native_fm_host[b];}
#endif
static int native_fm_valid(const native_fm_t *b)
{
    if(b->magic!=NATIVE_FM_MAGIC || b->ver!=1u || b->slots!=16u || b->used>>16)return 0;
    for(uint32_t k=0;k<16u;k++)if(b->used>>k&1u)for(uint32_t j=0;j<FM6_PACKED;j++)if(b->tone[k][j]>127u)return 0;
    return 1;
}

static void native_cache_reset(void)
{
#if MELODEE_FLASH
    native_fm_cached=cz_bank_cached=255;
#endif
}
static int legacy_native_used(uint32_t e,uint32_t k)
{
    if(e!=ENGI_FM6 || k>=64u)return 0;
    const native_fm_t *b=native_fm_bank(k/16u);return native_fm_valid(b) && (b->used>>(k%16u)&1u);
}
static const uint8_t *legacy_native_raw(uint32_t e,uint32_t k){(void)e;return native_fm_bank(k/16u)->tone[k%16u];}
static uint32_t native_size(uint32_t e){return e==ENGI_PROPHET?sizeof(p5_patch_t):e==ENGI_FM6?FM6_PACKED:CZ_BYTES;}
