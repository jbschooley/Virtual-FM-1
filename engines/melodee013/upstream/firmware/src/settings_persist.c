/* SPDX-License-Identifier: GPL-3.0-only */
/* Shared PER5 layout: each feature updates only its own fields, preserving
 * the other feature's saved preferences when either is built independently.
 * PER1/PER2 are upstream; PER3 added bold; PER4 added favorites (Felucca 1.0); PER5 added Melodee's ext block:
 * usb_off (the USB audio devices left out, USB AUDIO in the menu), boot (BOOT: 0 OFF, 1..4 the project power-on
 * loads), glo (CLK TUNE MIDI ROUT as last used, GLO_KEPT: restored at power-on, a project's own win), drumch
 * (GLO > SYSTEM DRUM: 0 channel 10, 1..16, 17 OFF), lights (the menu's LIGHTS: 0 MID, else OFF..FULL + 1); every
 * field 0 = the default, so spare words can take new ones. spare[1] holds tagged A4 concert pitch (0 = 440 Hz).
 * A saved template (project.c tmpl_t) follows the record.
 * palette: UI_PAL_TAG + index; an old id (below 20, earlier firmware) is migrated on import.
 * bold: no longer used (one font weight); kept as it was saved, unless it holds the HOLD setting (panel.c). */
typedef struct {
    uint32_t magic, palette, lowcut, zoom;
    panel_t panel;
    uint32_t bold;
    struct { uint8_t factory[16][32]; uint32_t user, filter; } favorites;
    struct { uint32_t usb_off, boot; int16_t glo[4]; uint32_t drumch, lights, spare[2]; } ext;   /* drumch: 0 = 10,
                                                                                                * 17 OFF; lights: 0 MID,
                                                                                                * else level + 1 */
} persist_t;
#define PERSIST_MAGIC 0x50455235u
#define PERSIST_MAGIC4 0x50455234u                  /* Felucca 1.0: no ext */
#define PERSIST_LEN4 (sizeof(persist_t) - sizeof(((persist_t *)0)->ext))
_Static_assert(sizeof(((persist_t *)0)->ext) == 32u, "ext: 8 words, new fields take spare ones");

static int16_t settings_glo[4];                     /* ext.glo: project.c GLO_KEPT (glo_restore, glo_poll) */
#define A4_TAG 0x41340000u                          /* "A4": old spare contents are not tuning */

/* Normalize in place; 1 = current, 2 = migrated, 0 = invalid. */
static int settings_import(persist_t *p, int n)
{
    int current = n == (int)sizeof *p && p->magic == PERSIST_MAGIC;
    int old4 = n == (int)PERSIST_LEN4 && p->magic == PERSIST_MAGIC4;
    int old3 = n == (int)(PERSIST_LEN4 - sizeof p->favorites) && p->magic == 0x50455233u;
    int old2 = n == (int)(PERSIST_LEN4 - sizeof p->favorites - sizeof p->bold) && p->magic == 0x50455232u;
    int old1 = n == (int)(8u + sizeof(panel_t)) && p->magic == 0x50455231u;
    if (!(current || old4 || old3 || old2 || old1)) return 0;
    if (old1) {
        panel_t old;
        memcpy(&old, (uint8_t *)p + 8, sizeof old);
        p->panel = old;
        p->lowcut = p->zoom = 0;
    }
    if (old1 || old2) p->bold = 0;
    if (!current && !old4) memset(&p->favorites, 0, sizeof p->favorites);
    if (!current) memset(&p->ext, 0, sizeof p->ext);
    p->ext.usb_off &= 3u;
    if (p->ext.boot > 4u)
        p->ext.boot = 0;
    settings_boot = (uint8_t)p->ext.boot;
    if (p->ext.drumch > 17u)
        p->ext.drumch = 0;
    settings_drumch = (uint8_t)(!p->ext.drumch ? 10u : p->ext.drumch == 17u ? 0u : p->ext.drumch);
    settings_lights = (uint8_t)(p->ext.lights && p->ext.lights <= LIGHTS_N ? p->ext.lights - 1u : LIGHTS_MID);
    {
        uint32_t hz = p->ext.spare[1] & 0xFFFFu;
        tuning_a4 = (int16_t)((p->ext.spare[1] & 0xFFFF0000u) == A4_TAG && hz >= A4_MIN && hz <= A4_MAX
                             ? hz : A4_DEFAULT);
    }
    memcpy(settings_glo, p->ext.glo, sizeof settings_glo);
#if MELODEE_USB_AUDIO
    ua_off = ua_off_want = (uint8_t)p->ext.usb_off;    /* (usb_start, after this, builds the configuration) */
#endif
    p->magic = PERSIST_MAGIC;
    p->palette = palette_to_stored(palette_from_stored(p->palette));
    settings.magic = SETTINGS_MAGIC;
    settings.palette = palette_from_stored(p->palette);
    settings.lowcut = p->lowcut;
    settings.zoom = p->zoom;
    settings_hold = (uint8_t)hold_from_stored(p->bold);
#ifdef MELODEE_FAVORITES
    memcpy(&favorites, &p->favorites, sizeof favorites);
    favorites_user_hi = p->ext.spare[0];
    favorites.filter = favorites.filter == 1u;
#if defined(FM4_NPRESETS) && !MELODEE_FM4
    {   /* DIGITAL's starred presets (engine 1, retired) -> the FM6 presets that cover them (fm4_convert.c) */
        uint32_t k;
        for (k = 0; k < FM4_NPRESETS; k++)
            if ((favorites.factory[ENGI_DIGITAL][0] >> k) & 1u)
                favorites.factory[ENGI_FM6][FM4_TO_FM6[k] / 8u] |= (uint8_t)(1u << (FM4_TO_FM6[k] % 8u));
        memset(favorites.factory[ENGI_DIGITAL], 0, sizeof favorites.factory[ENGI_DIGITAL]);
    }
#endif
#endif
    if (p->panel.magic == PANEL_MAGIC) panel = p->panel;
    return current ? 1 : 2;
}

/* Start with the last imported/saved object, including fields owned by a
 * feature absent from this build. No save-on-boot or extra flash writes. */
static void settings_export(persist_t *p)
{
    p->magic = PERSIST_MAGIC;
    p->palette = palette_to_stored(settings.palette);
    p->lowcut = settings.lowcut;
    p->zoom = settings.zoom;
    p->panel = panel;
    p->bold = hold_to_stored(p->bold, settings_hold);
#ifdef MELODEE_FAVORITES
    memcpy(&p->favorites, &favorites, sizeof favorites);
    p->ext.spare[0] = favorites_user_hi;
#endif
#if MELODEE_USB_AUDIO
    p->ext.usb_off = ua_off;
#endif
    p->ext.boot = settings_boot;
    p->ext.drumch = settings_drumch == 10u ? 0u : !settings_drumch ? 17u : settings_drumch;
    p->ext.lights = settings_lights == LIGHTS_MID ? 0u : settings_lights + 1u;
    p->ext.spare[1] = tuning_a4 == A4_DEFAULT ? 0u : A4_TAG | (uint32_t)tuning_a4;
    memcpy(p->ext.glo, settings_glo, sizeof settings_glo);
}
