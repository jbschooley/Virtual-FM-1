/* SPDX-License-Identifier: GPL-3.0-only */
/* 0.22, the sound pool (firmware/src/upool.c): the engine stores' readers of before, for the host tests that check a
 * user slot's patch (include after tools/emu/emu_firmware.h) */
static int fm6u_get(uint32_t k, uint8_t *b) { return up_patch_get(k, ENGI_FM6, b, FM6_BLOB); }
static int czu_get(uint32_t k, uint8_t *b) { return up_patch_get(k, ENGI_CZ, b, CZ_BYTES); }
static int va_store_get(uint32_t k, uint8_t *b) { return up_patch_get(k, ENGI_VA, b, VA_BLOB); }
static int quad_store_get(uint32_t k, uint8_t *b) { return up_patch_get(k, ENGI_QUAD, b, QUAD_BLOB); }
/* the slots h * 16 .. h * 16 + 15 that hold a patch of engine e: bit k - h * 16 (the legacy stores' halves' "used") */
static uint32_t pool_used_of(uint32_t e, uint32_t h)
{
    uint8_t b[UPP_MAX];
    uint32_t i, m = 0, n = up_patch_len(e);
    for (i = 0; i < 16u; i++)
        if (!up_patch_get(h * 16u + i, e, b, n))
            m |= 1u << i;
    return m;
}
static void pool_patch_clear(uint32_t k) { (void)sp_write(k, UPP_HDR, 0, SP_SLOT - UPP_HDR); }   /* a slot of no patch */
