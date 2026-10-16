/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#define MELODEE_FAVORITES 1
/* Stable engine/preset references; fixed capacity independent of optional engines.
 * Bytes no engine's presets reach hold other settings, kept with the favorites (settings_persist.c):
 *   row 8  (GRAIN, retired; Felucca 1.0's stars in bits 0..11) 16..25 the FX key map, 28..31 its tag (below)
 *   row 14 (OBXF, retired; its old stars in 0..15)             16..27 the scale favorites, 28..31 their tag
 *   row 15 (CZ-1: factory tones bits 0..64, native slots 65..192) 26 tagged USB INPUT (settings_persist.c), 27 SCREEN OFF,
 *          28 tagged AUTOSAVE OFF, 29 the PRESETS LIST (browse.c),
 *          30 bit 0 retired, bit 1 KNOB ACCEL OFF (ui.c PREF_BITS), bits 2..4 retired (OVERCLOCK until 1.3.4 / 1.4),
 *          30 bit 5 PICKER OFF, bit 6 FONT PIXEL (ui.c PREF_BITS), bit 7 retired (STAGE EFFECTS OFF, 1.4.0-rc.4),
 *          31 the quick layers seen (ui_layer.c), 25 HOME SELECT (ui.c HOME_SELECT), 24 bits 1..4 GLO SELECT (ui.c
 *          GLO_SELECT); free: 24 bits 5..7
 *   row 0 bytes 2..27 hold all 201 Prophet factory stars, tagged in bytes 28..31;
 *         bytes 0..1 held retired ANALOG's stars.
 *   row 1 bytes 28..31 mark validated Melodee settings (MDS + version 1).
 * Rows 1 (DIGITAL, retired), 4 (SAMPLE) and 13 (SLICE, built with MELODEE_SLICE) may hold Felucca 1.0's stars; 6, 7, 9 are LEGACY_EXTRAS' engines. Native FM6 slots: row 12 bits 24..87. */
typedef struct {
    uint8_t factory[16][32]; /* engine 0..15, preset 0..255 */
    uint32_t user, filter;
} favorites_t;
static favorites_t favorites;
static uint32_t favorites_user_hi;
/* Scale favorites use the upper half of retired engine 14's PER5 row. Keep
 * old preset bits 0..127 intact; tagged storage ignores unrecognized bytes.
 * No settings/template layout change. */
#define SCALE_FAV_CAP 96u
static int scale_fav_valid(void)
{
    const uint8_t *p = favorites.factory[14];
    return p[28] == 'S' && p[29] == 'C' && p[30] == 'L' && p[31] == 1;
}
static int scale_favorite(uint32_t scale)
{
    return scale < SCALE_FAV_CAP && scale_fav_valid() &&
        ((favorites.factory[14][16u + scale / 8u] >> (scale % 8u)) & 1u);
}
static int scale_favorite_set(uint32_t scale, int on)
{
    if (scale >= SCALE_FAV_CAP || scale_favorite(scale) == !!on) return 0;
    uint8_t *p = favorites.factory[14];
    if (!scale_fav_valid()) {
        memset(p + 16, 0, 12);
        p[28] = 'S'; p[29] = 'C'; p[30] = 'L'; p[31] = 1;
    }
    if (on) p[16u + scale / 8u] |= (uint8_t)(1u << (scale % 8u));
    else p[16u + scale / 8u] &= (uint8_t)~(1u << (scale % 8u));
    return 1;
}
static int p5_favorite_has(uint32_t slot,int factory);
static int p5_favorite_set(uint32_t slot,int on,int factory);
static int favorite_has(uint32_t engine, uint32_t preset)
{
    if(engine==ENGI_PROPHET)return p5_favorite_has(preset,1);
    if(engine==USER_NATIVE_P5 || engine==USER_NATIVE_FM || engine==USER_NATIVE_CZ)engine=USER_GENERAL;
    if (engine == USER_GENERAL)
        return up_favorite(preset);
    return engine < NENGINES && engine < 16u && preset < 256u &&
        ((favorites.factory[engine][preset / 8u] >> (preset % 8u)) & 1u);
}
static int favorite_set(uint32_t engine, uint32_t preset, int on)
{
    if(engine==ENGI_PROPHET)return p5_favorite_set(preset,on,1);
    if(engine==USER_NATIVE_P5 || engine==USER_NATIVE_FM || engine==USER_NATIVE_CZ)engine=USER_GENERAL;
    if (engine >= NENGINES || (engine == USER_GENERAL ? preset >= UP_SLOTS : engine >= 16u || preset >= 256u))
        return 0;
    if (favorite_has(engine, preset) == !!on) return 0;
    if (engine == USER_GENERAL) {
        return up_favorite_set(preset,on);
    } else {
        uint8_t *b = &favorites.factory[engine][preset / 8u];
        if (on) *b |= (uint8_t)(1u << (preset % 8u));
        else *b &= (uint8_t)~(1u << (preset % 8u));
    }
    return 1;
}
