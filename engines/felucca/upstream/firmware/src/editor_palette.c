/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* CUSTOM, the user's palette, for the editor (1.6, #244, Discussion #239; EDITOR_PROTOCOL.md "CUSTOM palette"; INFO 45 01 5):
 * PALETTE_GET reads it, PALETTE_SET shows five colours at once (a preview, RAM only), stores them (the palette in use from
 * then on, saved with the settings), removes it or goes back to the stored one. The colours are the five a palette stores
 * (BG SURF TEXT THEME ACCENT), each as three 7-bit bytes (red 0..31, green 0..63, blue 0..31 of RGB565); the rest of the
 * tokens is derived as for every palette (gfx.c palette_set). Colours that are hard to read are not refused: the editor warns
 * (its rule, EDITOR_PROTOCOL.md), the device keeps what it is given. A way back is always there: PALETTE_SET op 2, or MENU >
 * DISPLAY > COLOR, the first row of the first tab */
enum { ED_PALETTE_GET = 80, ED_PALETTE_SET };
#define ED_PAL_N 5u
/* flags (bit 0: one is stored, bit 1: it is in use), the count, then the colours: the stored ones, or with none stored the
 * palette in use (a place to start from) */
static void ed_palette_body(void)
{
    const uint16_t *tok = &ux.bg;                      /* (BG SURF TEXT THEME ACCENT in a row) */
    uint32_t i, stored = (ui_cpal_flags & CPAL_STORED) != 0u;
    ed_b(ui_cpal_flags & (CPAL_STORED | CPAL_ACTIVE));
    ed_b(ED_PAL_N);
    for (i = 0; i < ED_PAL_N; i++) {
        uint32_t c = stored ? (uint32_t)(ui_cpal[2u * i] | ui_cpal[2u * i + 1u] << 8) : tok[i];
        ed_b(c >> 11); ed_b((c >> 5) & 63u); ed_b(c & 31u);
    }
}
static int ed_palette_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    uint32_t rc = 1, i;
    uint16_t c[ED_PAL_N];
    if (cmd == ED_PALETTE_GET) {
        ed_palette_body();
        return 1;
    }
    if (cmd != ED_PALETTE_SET)
        return 0;
    if (n == 1u + 3u * ED_PAL_N && a[0] <= 1u) {       /* 0 preview, 1 store: op, then 5 x (r, g, b) */
        rc = 0;
        for (i = 0; i < ED_PAL_N; i++) {
            if (a[1 + 3 * i] > 31u || a[2 + 3 * i] > 63u || a[3 + 3 * i] > 31u)
                rc = 1;
            c[i] = (uint16_t)(a[1 + 3 * i] << 11 | a[2 + 3 * i] << 5 | a[3 + 3 * i]);
        }
        if (!rc && a[0] == 0u) {                       /* preview: gfx.c's copy only, nothing stored */
            ux_cust.bg = c[0]; ux_cust.surf = c[1]; ux_cust.text = c[2]; ux_cust.theme = c[3]; ux_cust.accent = c[4];
            palette_set(NPALETTES);
            ui.force = 1;
        } else if (!rc) {
            cpal_store(c);
            ui_cpal_flags |= CPAL_ACTIVE;
            palette_apply();
            ui.force = 1;
            rc = ed_ui_save();                         /* as UI_SET: 0 saved, 3 RAM only, 4 queued until STOP */
        }
    } else if (n == 1u && a[0] == 2u) {                /* 2: removed (the built-in palette in use before is back) */
        memset(ui_cpal, 0, 2u * ED_PAL_N);
        ui_cpal_flags = 0;
        palette_apply();
        ui.force = 1;
        rc = ed_ui_save();
    } else if (n == 1u && a[0] == 3u) {                /* 3: the preview dropped (the palette in use again) */
        palette_apply();
        ui.force = 1;
        rc = 0;
    }
    ed_b(rc);
    ed_palette_body();
    return 1;
}
