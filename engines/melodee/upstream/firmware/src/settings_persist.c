#include "recording_preferences.h"
/* SPDX-License-Identifier: GPL-3.0-only */
/* Shared PER5 layout: each feature updates only its own fields, preserving
 * the other feature's saved preferences when either is built independently.
 * Only PER5 is read (1.2; older records are protected until replacement). Its ext block:
 * usb_off (the USB audio devices left out, USB AUDIO in the menu), boot (BOOT: 0 OFF, 1..4 the project power-on
 * loads), glo (CLK TUNE MIDI ROUT as last used, GLO_KEPT: restored at power-on, a project's own win), drumch
 * (GLO > SYSTEM DRUM: 0 channel 10, 1..16, 17 OFF), lights (the menu's LIGHTS: 0 MID, else OFF..FULL + 1); every
 * field 0 = the default, so spare words can take new ones. spare[1] holds tagged A4 concert pitch (0 = 440 Hz).
 * A saved template (project.c tmpl_t) follows the record.
 * palette: UI_PAL_TAG + index (any other id: the default).
 * favorites.factory[15][28]: tagged AUTOSAVE OFF; zero/unknown keeps autosave ON.
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
_Static_assert(sizeof(((persist_t *)0)->ext) == 32u, "ext: 8 words, new fields take spare ones");
_Static_assert(sizeof(persist_t) == 604u, "the record's layout: new settings take spare bytes (favorites.c)");

static int16_t settings_glo[4];                     /* ext.glo: project.c GLO_KEPT (glo_restore, glo_poll) */
#define RECORD_PREF_TAG 0x52500000u /* RP, high half of the old zoom word */
#define A4_TAG 0x41340000u                          /* "A4": old spare contents are not tuning */
#define AUTOSAVE_OFF_TAG 0xA1u                     /* CZ row byte 28: only this tag disables autosave */

/* Normalize in place; 1 = read, 0 = invalid. */
static int settings_import(persist_t *p, int n)
{
    if (n != (int)sizeof *p || p->magic != PERSIST_MAGIC) return 0;
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
    uint8_t tracks = p->favorites.factory[15][26] == 0xA1u;
    if (usb.up) ua_tracks_set(tracks, fm1_ms);    /* live backup restore: re-enumerate before changing stride */
    else ua_tracks = ua_tracks_want = tracks;
    ua_off = ua_off_want = (uint8_t)p->ext.usb_off;    /* (usb_start, after this, builds the configuration) */
#endif
    p->palette = palette_to_stored(palette_from_stored(p->palette));
    settings_screen=p->favorites.factory[15][27] < 8u ? p->favorites.factory[15][27] : 0u;
    settings_autosave = p->favorites.factory[15][28] != AUTOSAVE_OFF_TAG;
    settings.magic = SETTINGS_MAGIC;
    settings.palette = palette_from_stored(p->palette);
    settings.lowcut = 0;
    settings.zoom = p->zoom & 0xFFu;
    uint32_t rp = (p->zoom & 0xFFFF0000u) == RECORD_PREF_TAG ? (p->zoom >> 8) & 0xFFu : 0u;
    settings_click = (uint8_t)((rp & 3u) < 3u ? rp & 3u : 0u);
    uint32_t level = (rp >> 2) & 3u;
    settings_click_level = (uint8_t)(level == 1u ? 0u : level == 2u ? 2u : 1u);
    settings_countin = (uint8_t)(((rp >> 4) & 3u) < 3u ? (rp >> 4) & 3u : 0u);
    settings_preview = (uint8_t)((rp >> 6) & 1u);
    settings_chord_add = (uint8_t)((rp >> 7) & 1u);
    settings_hold = (uint8_t)hold_from_stored(p->bold);
#ifdef MELODEE_FAVORITES
    memcpy(&favorites, &p->favorites, sizeof favorites);
    favorites_user_hi = p->ext.spare[0];
    favorites.filter = favorites.filter == 1u;
#endif
    if (p->panel.magic == PANEL_MAGIC) panel = p->panel;
    return 1;
}

/* Start with the last imported/saved object, including fields owned by a
 * feature absent from this build. No save-on-boot or extra flash writes. */
static void settings_export(persist_t *p)
{
    p->magic = PERSIST_MAGIC;
    p->palette = palette_to_stored(settings.palette);
    p->lowcut = 0;
    uint32_t rp = settings_click % 3u | (settings_click_level == 0u ? 1u : settings_click_level == 2u ? 2u : 0u) << 2 |
                  (settings_countin % 3u) << 4 | (settings_preview != 0u) << 6 | (settings_chord_add != 0u) << 7;
    p->zoom = (settings.zoom & 0xFFu) | (rp ? RECORD_PREF_TAG | rp << 8 : 0u);
    p->panel = panel;
    p->bold = hold_to_stored(p->bold, settings_hold);
#ifdef MELODEE_FAVORITES
    memcpy(&p->favorites, &favorites, sizeof favorites);
    p->ext.spare[0] = favorites_user_hi;
#endif
#if MELODEE_USB_AUDIO
    p->ext.usb_off = ua_off;
    p->favorites.factory[15][26] = ua_tracks ? 0xA1u : 0u; /* tagged; old/unknown bytes default to stereo */
#endif
    p->favorites.factory[15][27]=settings_screen;
    p->favorites.factory[15][28] = settings_autosave ? 0u : AUTOSAVE_OFF_TAG;
    p->ext.boot = settings_boot;
    p->ext.drumch = settings_drumch == 10u ? 0u : !settings_drumch ? 17u : settings_drumch;
    p->ext.lights = settings_lights == LIGHTS_MID ? 0u : settings_lights + 1u;
    p->ext.spare[1] = tuning_a4 == A4_DEFAULT ? 0u : A4_TAG | (uint32_t)tuning_a4;
    memcpy(p->ext.glo, settings_glo, sizeof settings_glo);
}
