/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Optional device preferences, advertised in INFO. No preset format changes. The font weight
 * (ED_UI_FONT) is gone: one weight; UI_SET of it answers rc 2 (not supported), UI_STATE 127. */
enum { ED_UI_PALETTE = 1, ED_UI_FONT = 2, ED_UI_MONITOR = 4, ED_UI_FAVORITES = 8 };
static uint32_t ed_ui_caps(void)
{
    uint32_t caps = ED_UI_PALETTE;
#ifdef MELODEE_MONITOR
    caps |= ED_UI_MONITOR;
#endif
#ifdef MELODEE_FAVORITES
    caps |= ED_UI_FAVORITES;
#endif
    return caps;
}
static void ed_u28(uint32_t n)
{
    for (uint32_t i = 0; i < 4; i++) ed_b((n >> (i * 7)) & 127u);
}
static void ed_ui_state(void)
{
    uint32_t sig = 0;
    ed_b(ed_ui_caps());
    ed_b(settings.palette);
    ed_b(127);                                         /* font: not supported */
#ifdef MELODEE_MONITOR
    ed_b(settings.monitor);
#else
    ed_b(127);
#endif
#ifdef MELODEE_FAVORITES
    ed_b(favorites.filter);
    sig = 2166136261u;
    for (uint32_t i = 0; i < sizeof favorites; i++)
        sig = (sig ^ ((const uint8_t *)&favorites)[i]) * 16777619u;
    for (uint32_t i = 0; i < sizeof favorites_user_hi; i++)
        sig = (sig ^ ((const uint8_t *)&favorites_user_hi)[i]) * 16777619u;
#else
    ed_b(127);
#endif
    for(uint32_t bank=0;bank<5u;bank++){if(!p5_meta_ready[bank])p5_user_bank(bank);sig=(sig^p5_meta[bank].favorites)*16777619u;}
    ed_u28(sig);
    ed_u28(up_gen);
}
/* Writes use the same flash path as the panel. rc 3 means applied in RAM,
 * but not saved (absent flash or a failed write); rc 4 means queued until STOP.
 * Repeated unchanged writes
 * do not wear flash. */
static uint32_t ed_ui_save(void)
{
    settings_save();
#if MELODEE_FLASH
    if (!flash_ok || persist_pending == 2u) return 3;
    if (persist_pending == 1u) return 4;
    if (palette_from_stored(persist_saved.palette) != settings.palette) return 3;
#ifdef MELODEE_MONITOR
    if (persist_saved.monitor != settings.monitor) return 3;
#endif
#ifdef MELODEE_FAVORITES
    if (memcmp(&persist_saved.favorites, &favorites, sizeof favorites)) return 3;
#endif
    return 0;
#else
    return 3;
#endif
}
static uint32_t ed_ui_set(const uint8_t *a, uint32_t n)
{
    if (n != 2u || a[0] > 3u) return 1;
    if (!(ed_ui_caps() & (1u << a[0]))) return 2;
    if (a[1] >= (a[0] == 0 ? NPALETTES : a[0] == 2 ? 3u : 2u)) return 1;
    switch (a[0]) {
    case 0:
        settings.palette = a[1]; palette_set(a[1]); break;
#ifdef MELODEE_MONITOR
    case 2:
        fm1_irq_off();
        settings.monitor = a[1]; monitor_mode = a[1]; monitor_event.valid = 0;
        fm1_irq_on();
        break;
#endif
#ifdef MELODEE_FAVORITES
    case 3:
        favorites.filter = a[1]; break;
#endif
    }
    ui.force = 1;
    return ed_ui_save();
}
static int ed_ui_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    switch (cmd) {
    case ED_UI_STATE:
        ed_ui_state(); return 1;
    case ED_UI_SET:
        ed_b(ed_ui_set(a, n));
        ed_b(n ? a[0] : 127u); ed_b(n > 1u ? a[1] : 127u);
        ed_ui_state(); return 1;
    case ED_UI_PALETTES:
        ed_b(NPALETTES);
        for (uint32_t i = 0; i < NPALETTES; i++) ed_str(UI_PALETTES[i].name, 12);
        return 1;
    case ED_FAV_GET:
#ifdef MELODEE_FAVORITES
        if (n == 4u && a[0] <= USER_NATIVE_P5) {
            int32_t start = ed_rv(a + 1);
            uint32_t limit = (a[0]==USER_NATIVE_FM || a[0]==USER_NATIVE_CZ || a[0]==USER_NATIVE_P5) ? native_limit(a[0]==USER_NATIVE_P5?ENGI_PROPHET:a[0]==USER_NATIVE_FM?ENGI_FM6:ENGI_CZ) : a[0] == USER_GENERAL ? UP_SLOTS : ENGINES[a[0]]->npresets;
            if (start >= 0 && a[3] && a[3] <= 32u && (uint32_t)start + a[3] <= limit) {
                ed_b(0); ed_b(a[0]); ed_v(start); ed_b(a[3]);
                for (uint32_t i = 0; i < a[3]; i++) ed_b(favorite_has(a[0], (uint32_t)start + i));
                return 1;
            }
        }
        ed_b(1);
#else
        ed_b(2);
#endif
        return 1;
    case ED_FAV_SET:
#ifdef MELODEE_FAVORITES
        if (n == 4u && a[0] <= USER_NATIVE_P5 && a[3] <= 1u) {
            int32_t preset = ed_rv(a + 1);
            uint32_t limit = (a[0]==USER_NATIVE_FM || a[0]==USER_NATIVE_CZ || a[0]==USER_NATIVE_P5) ? native_limit(a[0]==USER_NATIVE_P5?ENGI_PROPHET:a[0]==USER_NATIVE_FM?ENGI_FM6:ENGI_CZ) : a[0] == USER_GENERAL ? UP_SLOTS : ENGINES[a[0]]->npresets;
            if (preset >= 0 && (uint32_t)preset < limit &&
                (a[0] != USER_GENERAL || !a[3] || up_used((uint32_t)preset)) &&
                ((a[0]!=USER_NATIVE_FM && a[0]!=USER_NATIVE_CZ && a[0]!=USER_NATIVE_P5) || !a[3] || native_used(a[0]==USER_NATIVE_P5?ENGI_PROPHET:a[0]==USER_NATIVE_FM?ENGI_FM6:ENGI_CZ,(uint32_t)preset))) {
                int p5=a[0]==ENGI_PROPHET || a[0]==USER_NATIVE_P5;
                int before=favorite_has(a[0],(uint32_t)preset);
                int changed=favorite_set(a[0], (uint32_t)preset, a[3]);
                if(p5 && !changed && before!=a[3]){ed_b(transport_busy()?4u:3u);ed_b(a[0]);ed_v(preset);ed_b(before);return 1;}
                ui.force = 1;
                ed_b(p5?0u:ed_ui_save()); ed_b(a[0]); ed_v(preset); ed_b(a[3]);
                return 1;
            }
        }
        ed_b(1);
#else
        ed_b(2);
#endif
        return 1;
    default: return 0;
    }
}
