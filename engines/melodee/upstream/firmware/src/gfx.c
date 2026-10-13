/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Small-canvas renderer (no full framebuffer). Draw text/lines into
 * an off-screen strip, then blit it in one DMA transfer. Pixels are stored
 * byte-swapped (the panel takes RGB565 big-endian).
 * Text and icons are 4-bit alpha bitmaps made at build time (tools/gen_aa_font.py: Rubik;
 * tools/gen_icons.py: Melodee's own drawings). A glyph is trimmed to its ink box, advances are in 1/16 px, and
 * each glyph is rasterised at 1 << psh horizontal phases (2: drawn at a pen of +0, +1/2 px);
 * cv_text takes the phase that puts it within a phase step of its true place. Blending uses a 16-entry
 * ramp per (ink, background) pair, cached, so a pixel costs a nibble read and a store. Colours come from the theme tokens. */
typedef struct { uint16_t off, adv; int8_t bx; uint8_t by, bw, bh; } aag_t;
typedef struct {
    uint8_t h, asc, first, last, nex;   /* line height, baseline; ASCII range; extras */
    uint8_t nkr;                        /* right kerning classes (0: no kerning) */
    const aag_t *g;
    const uint8_t *data;
    const uint8_t *kl, *kr;             /* each glyph's left / right kerning class, 0 = no pair */
    const int8_t *km;                   /* 1/16 px: [(left class - 1) * nkr + right class - 1] */
    const uint8_t *ex;                  /* codes of the extra glyphs */
    uint8_t psh;                        /* log2 of the phases per glyph: g[i << psh] .. g[(i << psh) + phases - 1] */
    const uint8_t *hc;                  /* data's Huffman code (cv_alpha_hc), 0 = 2 px per byte (cv_alpha) */
} aafont_t;
typedef struct { const char *name; uint16_t bg, surf, text, accent, trk[4]; } ui_pal_t;
typedef struct { uint16_t off; uint8_t w; const char *label; } kc_t;
#include "ui_fonts.h"                   /* AF_X 10 px / 500 (labels), AF_S 12 px / 400, AF_M 15 px / 500, AF_L 30 px / 500 */
#include "ui_palettes.h"
#include "ui_keycaps.h"                 /* KC_*: the keycaps / badges (tools/gen_aa_keycaps.py), KNOB_* arcs */
#define ELLIPSIS '\x85'                 /* the ellipsis glyph of AF_S and AF_M */

/* host tests hook in here (layout lint, draw cost); nothing in the firmware */
#ifndef GFX_HOOK_TEXT
#define GFX_HOOK_TEXT(x0, y0, x1, y1, s, flags) ((void)0)   /* an ink box; flags 1 ellipsised, 2 cut, 4 icon, 8 free text,
                                                              * 16 under a question (cv_dim), 32 in a scrolled view */
#define GFX_HOOK_BLIT(x, y, r0) ((void)0)
#define GFX_HOOK_BEGIN() ((void)0)
#define GFX_HOOK_PIXELS(n) ((void)0)
#endif
#ifndef GFX_HOOK_WHOLE
#define GFX_HOOK_WHOLE(x, y, r0, fill) ((void)0)   /* what the screen would hold sent whole: canvas rows r0.. at x, y (or a
                                                     * fill) (tests/ui_render.c: the diff checked against it) */
#endif
#ifndef GFX_HOOK_CELL
#define GFX_HOOK_CELL(x0, y0, x1, y1) ((void)0)    /* a cell, card, row or button (cv_rrect): a box touching it lies inside it */
#endif

#define CV_MAX (240u * 124u)      /* the graph strip is 240 x 124 */
/* Most regions (cards, header, footer) fit the small second canvas. Draw
 * there while SPI reads the first; large graphs reuse the full canvas only
 * after its transfer finishes. No second full-screen-sized allocation. */
#define CV_ALT_MAX (240u * 24u)                   /* (240 x 24: the diff's hashes took the rest, below) */
static uint16_t cv_primary[CV_MAX] __attribute__((section(".pool")));
static uint16_t cv_alt[CV_ALT_MAX] __attribute__((section(".pool")));
static uint16_t *cv_px = cv_primary;
static const uint16_t *cv_flight;
static uint32_t cv_w, cv_h;
static uint16_t cv_bg;           /* what cv_begin cleared the canvas to */
static uint8_t cv_scroll;        /* 1: drawing a scrolled view (its text may run past the canvas) */
static int32_t cv_oy;            /* y offset for graph drawing */
static int16_t cv_cy0, cv_cy1;   /* text clip: canvas rows cv_cy0 .. cv_cy1 - 1 (cv_begin: the whole canvas) */

#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

/* ------------------------------------------------------------- theme --- */
/* Five stored colours per palette, the rest derived with fixed blends (tools/gen_ui_palettes.py) */
static struct {
    uint16_t bg, surf, text, theme, accent;
    uint16_t mid, dim, line, sel, tint, ink, rec, raise, key, lane, grid;
    uint16_t panel, quiet, lift, sec;   /* the redesign's (docs/design): panel, a quiet lane, the selected lane, names */
    uint8_t light, mono;
    uint8_t pal, trk;            /* the palette, the track whose colour is THEME (palette_track) */
    uint32_t gen;                /* bumped by palette_set (the text ramps follow) */
} ux;
#define T_BG ux.bg               /* background */
#define T_SURF ux.surf           /* dialogs, menu rows, mixer strips */
#define T_TEXT ux.text           /* primary text */
#define T_THEME ux.theme         /* the selected track's colour: values, curves, the gauge's end line */
#define T_ACCENT ux.accent       /* the one active thing: hot knob, cursor, playhead */
#define T_MID ux.mid             /* labels, units, secondary text */
#define T_DIM ux.dim             /* inactive, empty */
#define T_LINE ux.line           /* 1 px dividers, gauge track */
#define T_SEL ux.sel             /* selection and gauge fill */
#define T_TINT ux.tint           /* a faint theme area */
#define T_INK ux.ink             /* text on T_SEL / T_THEME */
#define T_REC ux.rec             /* REC: fixed red (MONO: the accent) */
#define T_RAISE ux.raise         /* a raised area on a surface: stubs, guides, slots, chips, button wells */
#define T_KEY ux.key             /* a keycap's fill (its label: T_INK; unavailable: a T_DIM fill) */
#define T_LANE ux.lane           /* the piano roll: an in-scale row (SURF -> THEME 10 %) */
#define T_GRID ux.grid           /* the piano roll: a step line (SURF -> TEXT 6 %; beats and C rows: RAISE) */
#define T_PANEL ux.panel         /* a view's big area (BG -> SURF 55 %) */
#define T_QUIET ux.quiet         /* a lane not selected (BG -> SURF 25 %) */
#define T_LIFT ux.lift           /* the selected lane (SURF -> TEXT 3 %) */
#define T_SEC ux.sec             /* secondary names (BG -> TEXT 80 %) */
#define NPALETTES UI_NPALETTES
#ifdef UI_TEST_PALETTE
#define UI_PAL_ENTRIES (UI_NPALETTES + 1u)   /* + GRAY (host tests: every screen must stay gray in it) */
#else
#define UI_PAL_ENTRIES UI_NPALETTES
#endif
#define T_TRK(k) UI_PALETTES[ux.pal].trk[(k) & 3u]   /* track k's colour (the mixer, lanes, track chips) */

static inline uint16_t ux_gray(uint32_t x5) { return (uint16_t)((x5 << 11) | (x5 << 6) | x5); }
/* a + (b - a) * pct / 100 per channel, rounded; MONO on the red channel, so it stays gray */
static uint16_t ux_mix(uint16_t a, uint16_t b, int32_t pct)
{
    static const uint8_t SH[3] = {11, 5, 0}, MK[3] = {31, 63, 31};
    uint32_t out = 0, k;
    if (ux.mono) {
        int32_t x = a >> 11, d = ((b >> 11) - x) * pct;
        return ux_gray((uint32_t)(x + (d + (d >= 0 ? 50 : -50)) / 100));
    }
    for (k = 0; k < 3u; k++) {
        int32_t x = (a >> SH[k]) & MK[k], d = (((b >> SH[k]) & MK[k]) - x) * pct;
        out |= (uint32_t)(x + (d + (d >= 0 ? 50 : -50)) / 100) << SH[k];
    }
    return (uint16_t)out;
}
static uint32_t ux_luma(uint16_t c)        /* 0..255 */
{
    return ((c >> 11) * 255u / 31u * 54u + ((c >> 5) & 63u) * 255u / 63u * 183u + (c & 31u) * 255u / 31u * 19u) >> 8;
}

static void palette_theme(void)                    /* THEME and what is mixed from it: the track's colour */
{
    const ui_pal_t *p = &UI_PALETTES[ux.pal];
    ux.theme = p->trk[ux.trk & 3u];
    ux.sel = ux_mix(p->bg, ux.theme, UI_SEL_PCT);
    ux.tint = ux_mix(p->bg, ux.theme, UI_TINT_PCT);
    ux.lane = ux_mix(p->surf, ux.theme, 10);
    ux.gen++;                                    /* the text ramps follow */
}
static void palette_set(uint32_t i)
{
    const ui_pal_t *p = &UI_PALETTES[ux.pal = (uint8_t)(i < UI_PAL_ENTRIES ? i : UI_DEFAULT_INDEX)];
#ifdef UI_TEST_PALETTE
    ux.mono = ux.pal == UI_GRAY_INDEX;
#else
    ux.mono = 0;
#endif
    ux.bg = p->bg; ux.surf = p->surf; ux.text = p->text; ux.accent = p->accent;
    ux.mid = ux_mix(p->bg, p->text, UI_MID_PCT);
    ux.dim = ux_mix(p->bg, p->text, UI_DIM_PCT);
    ux.line = ux_mix(p->bg, p->text, UI_LINE_PCTS[ux.pal]);     /* (NIGHT's fainter: the design's) */
    ux.ink = p->bg;
    ux.raise = ux_mix(p->surf, p->text, UI_RAISE_PCT);
    ux.key = ux_mix(p->bg, p->text, UI_KEY_PCT);
    ux.grid = ux_mix(p->surf, p->text, 6);
    ux.panel = ux_mix(p->bg, p->surf, 55);
    ux.quiet = ux_mix(p->bg, p->surf, 25);
    ux.lift = ux_mix(p->surf, p->text, 3);
    ux.sec = ux_mix(p->bg, p->text, 80);
    ux.light = ux_luma(p->bg) > 128u;
    ux.rec = ux.mono ? p->accent : ux.light ? UI_REC_LIGHT : UI_REC_DARK;
    palette_theme();
}
/* the selected track (ui_draw, every frame): THEME takes its colour; 1 = it changed (redraw everything) */
static int palette_track(uint32_t t)
{
    if (ux.trk == (t & 3u))
        return 0;
    ux.trk = (uint8_t)(t & 3u);
    palette_theme();
    return 1;
}

/* settings store a tagged id (UI_PAL_TAG + index); any other id reads as the default */
static int palette_stored_ok(uint32_t v) { return v >= UI_PAL_TAG && v - UI_PAL_TAG < NPALETTES; }
static uint32_t palette_from_stored(uint32_t v) { return palette_stored_ok(v) ? v - UI_PAL_TAG : UI_DEFAULT_INDEX; }
static uint32_t palette_to_stored(uint32_t i) { return UI_PAL_TAG + i % NPALETTES; }

static inline uint16_t swap16(uint32_t c) { return (uint16_t)(((c >> 8) & 0xFFu) | ((c & 0xFFu) << 8)); }

/* ------------------------------------------------------------ canvas --- */
static void cv_begin(uint32_t w, uint32_t h, uint16_t bg)
{
    uint32_t i, n;
    uint16_t s = swap16(bg);
    if (w * h > CV_MAX)
        h = CV_MAX / w;
    n = w * h;
    cv_px = cv_flight != cv_primary ? cv_primary : n <= CV_ALT_MAX ? cv_alt : cv_primary;
    if (cv_px == cv_flight) {
        lcd_sync();                 /* never overwrite a buffer still on SPI */
        cv_flight = 0;
    }
    GFX_HOOK_BEGIN();
    cv_w = w;
    cv_h = h;
    cv_cy0 = 0;
    cv_cy1 = (int16_t)h;
    cv_bg = bg;
    n = w * h;
    for (i = 0; i < n; i++)
        cv_px[i] = s;
}

/* cv_dim: a canvas goes out darkened (72 % toward BG): the page under a question (ui_draw.c); cv_under: the page under
 * a popup, dimmed or not (a knob's picker: not), its texts covered on purpose (the lint's flag 16) */
static uint8_t cv_dim, cv_under;
static uint16_t mix565(uint16_t bg, uint16_t fg, uint32_t a);
static void cv_dim_rows(uint32_t r0)
{
    uint32_t i;
    for (i = r0 * cv_w; i < cv_w * cv_h; i++)
        cv_px[i] = swap16(mix565(T_BG, swap16(cv_px[i]), 9u));
}
/* ------------------------------------------------------------- diff --- */
/* The SPI (12 MHz) is the UI's bottleneck: the whole screen takes ~77 ms. So a big canvas sent where it was sent
 * before (the same x, y, w, h) sends only what changed: DF_N such places keep 32-bit hashes of their 4-row bands and
 * 8-column blocks as last sent, and a blit sends the dirty band runs x the dirty block runs. Anything else written
 * over a place (another canvas, a fill: gfx_fill) marks the bands and blocks it covered dirty: sent again next time */
#define DF_N 9u                                         /* (tests/ui_render.c's LCD audit: as much sent as with 14) */
#ifndef DF_MIN
#define DF_MIN 2048u                                    /* (smaller canvases: sent whole) */
#endif
#define DF_BAND 4u
#define DF_BLK 8u
#define DF_NB (240u / DF_BAND)
#define DF_NK (240u / DF_BLK)
#define DF_SLOTS 76u                                    /* a canvas's bands, then its blocks: at most 60 + 16 (CV_MAX) */
#define DF_FITS() ((cv_h + DF_BAND - 1u) / DF_BAND + (cv_w + DF_BLK - 1u) / DF_BLK <= DF_SLOTS)
static struct { uint64_t pb; uint32_t pk; uint8_t x, y, w, h, on; uint16_t use; } df_key[DF_N];   /* pb, pk: bands and
                                                                                    * blocks drawn over since (dirty) */
static uint32_t df_sum[DF_N][DF_SLOTS] __attribute__((section(".pool")));   /* (16 bits: one changed block in 65536
                                                                             * collided, left stale) */
static uint32_t df_now[DF_SLOTS] __attribute__((section(".pool")));
static uint16_t df_clock;

static void df_damage(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t keep)   /* (keep: a place spared) */
{
    uint32_t i, j;
    for (i = 0; i < DF_N; i++) {
        uint32_t ex = df_key[i].x, ey = df_key[i].y, ex1 = ex + df_key[i].w, ey1 = ey + df_key[i].h, a0, a1, b0, b1;
        if (i == keep || !df_key[i].on || x >= ex1 || x + w <= ex || y >= ey1 || y + h <= ey)
            continue;
        a0 = (y > ey ? y : ey) - ey;                    /* the canvas rows a0 .. a1-1, columns b0 .. b1-1 covered */
        a1 = (y + h < ey1 ? y + h : ey1) - ey;
        b0 = (x > ex ? x : ex) - ex;
        b1 = (x + w < ex1 ? x + w : ex1) - ex;
        if (!a0 && a1 == ey1 - ey && !b0 && b1 == ex1 - ex) {
            df_key[i].on = 0;                           /* (all of it: forgotten) */
            continue;
        }
        for (j = a0 / DF_BAND; j <= (a1 - 1u) / DF_BAND; j++)
            df_key[i].pb |= 1ull << j;
        for (j = b0 / DF_BLK; j <= (b1 - 1u) / DF_BLK; j++)
            df_key[i].pk |= 1u << j;
    }
}
static uint32_t df_find(uint32_t x, uint32_t y)        /* the canvas's place (DF_N: none) */
{
    uint32_t i;
    for (i = 0; i < DF_N; i++)
        if (df_key[i].on && df_key[i].x == x && df_key[i].y == y && df_key[i].w == cv_w && df_key[i].h == cv_h)
            return i;
    return DF_N;
}
static void df_hash(void)                               /* the canvas's bands, then its blocks, into df_now */
{
    uint32_t nb = (cv_h + DF_BAND - 1u) / DF_BAND, nk = (cv_w + DF_BLK - 1u) / DF_BLK, r, k, i, band = 2166136261u;
    uint32_t blk[DF_NK];
    for (k = 0; k < nk; k++)
        blk[k] = 2166136261u;
    for (r = 0; r < cv_h; r++) {
        const uint16_t *p = cv_px + r * cv_w;
        for (k = 0; k < nk; k++) {
            uint32_t h = 2166136261u, e = (k + 1u) * DF_BLK < cv_w ? (k + 1u) * DF_BLK : cv_w;
            for (i = k * DF_BLK; i < e; i++)
                h = (h ^ p[i]) * 16777619u;
            blk[k] = (blk[k] ^ h) * 16777619u;
            band = (band ^ h) * 16777619u;
        }
        if (r % DF_BAND == DF_BAND - 1u || r + 1u == cv_h) {
            df_now[r / DF_BAND] = band;
            band = 2166136261u;
        }
    }
    for (k = 0; k < nk; k++)
        df_now[nb + k] = blk[k];
}
static void df_keep(uint32_t x, uint32_t y)             /* the canvas just sent whole: its place remembered */
{
    uint32_t i, at = 0;
    for (i = 0; i < DF_N; i++) {                        /* a free place, else the one used longest ago */
        if (!df_key[i].on) {
            at = i;
            break;
        }
        if ((uint16_t)(df_clock - df_key[i].use) > (uint16_t)(df_clock - df_key[at].use))
            at = i;
    }
    df_key[at].x = (uint8_t)x; df_key[at].y = (uint8_t)y; df_key[at].w = (uint8_t)cv_w; df_key[at].h = (uint8_t)cv_h;
    df_key[at].on = 1;
    df_key[at].pb = 0;
    df_key[at].pk = 0;
    df_key[at].use = ++df_clock;
    for (i = 0; i < DF_SLOTS; i++)
        df_sum[at][i] = df_now[i];
}
/* canvas columns c0 .. c0+w-1 of rows r0 .. r0+h-1 to the screen: whole rows straight from the canvas, else gathered
 * into the small canvas (the canvas is it, or they do not fit: whole rows) */
static void df_send(uint32_t x, uint32_t y, uint32_t c0, uint32_t w, uint32_t r0, uint32_t h, uint32_t keep)
{
    uint32_t r, i;
    if (w < cv_w && (cv_px == cv_alt || w * h > CV_ALT_MAX)) {
        c0 = 0;
        w = cv_w;
    }
    df_damage(x + c0, y + r0, w, h, keep);
    if (w == cv_w) {
        lcd_blit(x, y + r0, w, h, cv_px + r0 * cv_w);
        cv_flight = cv_px;
        return;
    }
    if (cv_flight == cv_alt) {
        lcd_sync();
        cv_flight = 0;
    }
    for (r = 0; r < h; r++)
        for (i = 0; i < w; i++)
            cv_alt[r * w + i] = cv_px[(r0 + r) * cv_w + c0 + i];
    lcd_blit(x + c0, y + r0, w, h, cv_alt);
    cv_flight = cv_alt;
}
/* runs of the dirty marks d[0..n-1] (gaps up to gap joined), up to 6: run k from a[k] to b[k] */
static uint32_t df_runs(const uint8_t *d, uint32_t n, uint32_t gap, uint8_t *a, uint8_t *b)
{
    uint32_t i, nr = 0;
    for (i = 0; i < n; i++) {
        if (!d[i])
            continue;
        if (nr && i - b[nr - 1u] <= gap + 1u)
            b[nr - 1u] = (uint8_t)i;
        else if (nr < 6u) {
            a[nr] = b[nr] = (uint8_t)i;
            nr++;
        } else
            b[nr - 1u] = (uint8_t)i;
    }
    return nr;
}
/* the canvas (hashed: df_now) sent where it was before: only what changed. 0: no place known (send it whole) */
static int df_diff(uint32_t x, uint32_t y)
{
    uint32_t e = df_find(x, y), nb = (cv_h + DF_BAND - 1u) / DF_BAND, nk = (cv_w + DF_BLK - 1u) / DF_BLK, i, j, nr, nc;
    uint32_t anyb = 0, anyk = 0;
    uint8_t db[DF_NB], dk[DF_NK], ra[6], rb[6], ca[6], cb[6];
    if (e >= DF_N)
        return 0;
    for (i = 0; i < nb; i++)
        anyb |= db[i] = df_now[i] != df_sum[e][i] || ((df_key[e].pb >> i) & 1u);
    for (i = 0; i < nk; i++)
        anyk |= dk[i] = df_now[nb + i] != df_sum[e][nb + i] || ((df_key[e].pk >> i) & 1u);
    df_key[e].use = ++df_clock;
    df_key[e].pb = 0;
    df_key[e].pk = 0;
    if (!anyb && !anyk)
        return 1;
    for (i = 0; i < nb && !anyb; i++)                   /* (one way's hashes collided: all of it that way) */
        db[i] = 1;
    for (i = 0; i < nk && !anyk; i++)
        dk[i] = 1;
    for (i = 0; i < DF_SLOTS; i++)
        df_sum[e][i] = df_now[i];
    nr = df_runs(db, nb, 1u, ra, rb);
    nc = df_runs(dk, nk, 2u, ca, cb);
    if (nr * nc > 6u) {                                 /* scattered: their bounding box */
        rb[0] = rb[nr - 1u]; cb[0] = cb[nc - 1u];
        nr = nc = 1;
    }
    for (j = 0; j < nc; j++) {
        uint32_t c0 = ca[j] * DF_BLK, c1 = (cb[j] + 1u) * DF_BLK < cv_w ? (cb[j] + 1u) * DF_BLK : cv_w;
        if ((c1 - c0) * 4u >= cv_w * 3u) {              /* (most of the width: whole rows, no gathering) */
            c0 = 0;
            c1 = cv_w;
        }
        for (i = 0; i < nr; i++) {
            uint32_t r0 = ra[i] * DF_BAND, r1 = (rb[i] + 1u) * DF_BAND < cv_h ? (rb[i] + 1u) * DF_BAND : cv_h;
            df_send(x, y, c0, c1 - c0, r0, r1 - r0, e);
        }
        if (c0 == 0 && c1 == cv_w)
            break;
    }
    return 1;
}

/* While a frame is drawn (ui_draw: gfx_frame) fills wait: a canvas sent over one later takes its place out of it, and
 * what is left is filled when the frame ends. A page drawn again whole (ui.force: its background filled, then its
 * canvases) so sends what changed, not the screen */
#define FQ_N 40u
static struct { uint8_t x, y, w, h; uint16_t c; } fq[FQ_N] __attribute__((section(".pool")));
static uint32_t fq_n;
static uint8_t fq_on;
static void fq_flush(void)
{
    uint32_t i;
    for (i = 0; i < fq_n; i++) {
        df_damage(fq[i].x, fq[i].y, fq[i].w, fq[i].h, DF_N);
        lcd_fill(fq[i].x, fq[i].y, fq[i].w, fq[i].h, fq[i].c);
    }
    fq_n = 0;
}
static void fq_cover(uint32_t x, uint32_t y, uint32_t w, uint32_t h)   /* a canvas about to go to (x, y, w, h) */
{
    uint32_t i = 0, j;
    while (i < fq_n) {
        uint32_t fx = fq[i].x, fy = fq[i].y, fx1 = fx + fq[i].w, fy1 = fy + fq[i].h, n = 0, m0, m1;
        uint8_t p[4][4];
        uint16_t c = fq[i].c;
        if (x >= fx1 || x + w <= fx || y >= fy1 || y + h <= fy) {
            i++;
            continue;
        }
        m0 = y > fy ? y : fy;                           /* the rows they share */
        m1 = y + h < fy1 ? y + h : fy1;
        if (y > fy) { p[n][0] = (uint8_t)fx; p[n][1] = (uint8_t)fy; p[n][2] = (uint8_t)(fx1 - fx); p[n][3] = (uint8_t)(y - fy); n++; }
        if (y + h < fy1) { p[n][0] = (uint8_t)fx; p[n][1] = (uint8_t)(y + h); p[n][2] = (uint8_t)(fx1 - fx); p[n][3] = (uint8_t)(fy1 - y - h); n++; }
        if (x > fx) { p[n][0] = (uint8_t)fx; p[n][1] = (uint8_t)m0; p[n][2] = (uint8_t)(x - fx); p[n][3] = (uint8_t)(m1 - m0); n++; }
        if (x + w < fx1) { p[n][0] = (uint8_t)(x + w); p[n][1] = (uint8_t)m0; p[n][2] = (uint8_t)(fx1 - x - w); p[n][3] = (uint8_t)(m1 - m0); n++; }
        if (fq_n - 1u + n > FQ_N) {                     /* (too many pieces: all the fills now, before the canvas) */
            fq_flush();
            return;
        }
        if (!n)                                         /* the pieces in its place (the order of the fills kept) */
            for (j = i + 1u; j < fq_n; j++)
                fq[j - 1u] = fq[j];
        else
            for (j = fq_n; j > i + 1u; j--)
                fq[j - 2u + n] = fq[j - 1u];
        for (j = 0; j < n; j++) {
            fq[i + j].x = p[j][0]; fq[i + j].y = p[j][1]; fq[i + j].w = p[j][2]; fq[i + j].h = p[j][3]; fq[i + j].c = c;
        }
        fq_n = fq_n - 1u + n;
        i += n;
    }
}
static void gfx_frame(int on)                           /* a frame begins (1) / ends (0: what fills are left, sent) */
{
    if (!on)
        fq_flush();
    fq_on = (uint8_t)on;
}
static void gfx_forget(void)                            /* the panel's memory not to be trusted: all sent whole next */
{
    uint32_t i;
    for (i = 0; i < DF_N; i++)
        df_key[i].on = 0;
}

static void cv_blit(uint32_t x, uint32_t y)
{
    uint32_t big = cv_w * cv_h >= DF_MIN && DF_FITS();
    if (fq_n)
        fq_cover(x, y, cv_w, cv_h);
    GFX_HOOK_BLIT(x, y, 0u);
    if (cv_dim)
        cv_dim_rows(0);
    GFX_HOOK_WHOLE(x, y, 0u, 0);
    if (big) {
        df_hash();
        if (df_diff(x, y))
            return;
    }
    df_damage(x, y, cv_w, cv_h, DF_N);
    lcd_blit(x, y, cv_w, cv_h, cv_px);
    cv_flight = cv_px;
    if (big)
        df_keep(x, y);
}

/* canvas rows r0 .. cv_h-1 only, to screen row y + r0 */
static void cv_blit_from(uint32_t x, uint32_t y, uint32_t r0)
{
    if (fq_n && r0 < cv_h)
        fq_cover(x, y + r0, cv_w, cv_h - r0);
    GFX_HOOK_BLIT(x, y, r0);
    if (cv_dim && r0 < cv_h)
        cv_dim_rows(r0);
    GFX_HOOK_WHOLE(x, y, r0, 0);
    if (r0 < cv_h) {
        df_damage(x, y + r0, cv_w, cv_h - r0, DF_N);
        lcd_blit(x, y + r0, cv_w, cv_h - r0, cv_px + r0 * cv_w);
        cv_flight = cv_px;
    }
}

/* the caller knows only columns a0 .. a0+aw-1 and b0 .. b0+bw-1 changed (bw 0: one span; a playhead moved over a big
 * view): only they are sent, faster than df_diff finds them (8-column blocks) */
static void cv_blit_spans(uint32_t x, uint32_t y, uint32_t a0, uint32_t aw, uint32_t b0, uint32_t bw)
{
    uint32_t e;
    if (cv_dim || cv_w * cv_h < DF_MIN || !DF_FITS()) {
        cv_blit(x, y);
        return;
    }
    if (fq_n)
        fq_cover(x, y, cv_w, cv_h);
    GFX_HOOK_WHOLE(x, y, 0u, 0);
    df_hash();
    e = df_find(x, y);
    if (e >= DF_N) {                                    /* (nothing known there: all of it) */
        GFX_HOOK_BLIT(x, y, 0u);
        df_damage(x, y, cv_w, cv_h, DF_N);
        lcd_blit(x, y, cv_w, cv_h, cv_px);
        cv_flight = cv_px;
        df_keep(x, y);
        return;
    }
    GFX_HOOK_BLIT(x, y, 0u);
    if (df_key[e].pb || df_key[e].pk) {                 /* (drawn over since: what changed, found) */
        df_diff(x, y);
        return;
    }
    for (uint32_t i = 0; i < DF_SLOTS; i++)
        df_sum[e][i] = df_now[i];
    df_key[e].use = ++df_clock;
    if (a0 < cv_w && aw)
        df_send(x, y, a0, aw < cv_w - a0 ? aw : cv_w - a0, 0, cv_h, e);
    if (b0 < cv_w && bw)
        df_send(x, y, b0, bw < cv_w - b0 ? bw : cv_w - b0, 0, cv_h, e);
}

static void gfx_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)   /* every fill from here on */
{
    if (!w || !h || x >= 240u || y >= 240u)
        return;
    if (w > 240u - x)
        w = 240u - x;
    if (h > 240u - y)
        h = 240u - y;
    GFX_HOOK_WHOLE(x, y, w | h << 16, c | 1u << 16);
    if (!fq_on) {
        df_damage(x, y, w, h, DF_N);
        lcd_fill(x, y, w, h, c);
        return;
    }
    if (fq_n == FQ_N)
        fq_flush();
    fq[fq_n].x = (uint8_t)x; fq[fq_n].y = (uint8_t)y; fq[fq_n].w = (uint8_t)w; fq[fq_n].h = (uint8_t)h; fq[fq_n].c = c;
    fq_n++;
}
#define lcd_fill gfx_fill

static inline void cv_pset(int32_t x, int32_t y, uint16_t c)
{
    y += cv_oy;
    if ((uint32_t)x < cv_w && (uint32_t)y < cv_h)
        cv_px[(uint32_t)y * cv_w + (uint32_t)x] = swap16(c);
}

static void cv_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    int32_t x1 = x + w, y1 = y + h + cv_oy, i;
    uint16_t sc = swap16(c);
    y += cv_oy;                             /* clipped once, then filled row by row */
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x1 > (int32_t)cv_w)
        x1 = (int32_t)cv_w;
    if (y1 > (int32_t)cv_h)
        y1 = (int32_t)cv_h;
    for (; y < y1; y++)
        for (i = x; i < x1; i++)
            cv_px[(uint32_t)y * cv_w + (uint32_t)i] = sc;
}

/* a 1 px outline */
static void cv_frame(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    cv_rect(x, y, w, 1, c);
    cv_rect(x, y + h - 1, w, 1, c);
    cv_rect(x, y, 1, h, c);
    cv_rect(x + w - 1, y, 1, h, c);
}

static void cv_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)
{
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int32_t dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy, guard = 2000;
    while (guard--) {                       /* bounded: a line is never longer than 480 px */
        int32_t e2 = 2 * err;               /* both tests use the same error value */
        cv_pset(x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/* a line `thick` px tall (the scope's trace: 2) */
static void cv_line_t(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c, int32_t thick)
{
    int32_t t;
    for (t = 0; t < thick; t++)
        cv_line(x0, y0 + t, x1, y1 + t, c);
}

/* coverage (0..16) of corner pixel (i, j) of a radius-r corner, (0, 0) the outermost: 4 x 4 samples */
static uint32_t rr_cov(int32_t r, int32_t i, int32_t j)
{
    int32_t sx, sy, n = 0;
    for (sy = 0; sy < 4; sy++)
        for (sx = 0; sx < 4; sx++) {
            int32_t dx = r * 8 - ((i * 4 + sx) * 2 + 1), dy = r * 8 - ((j * 4 + sy) * 2 + 1);   /* 1/8 px */
            n += dx * dx + dy * dy <= r * r * 64;
        }
    return (uint32_t)n;
}
/* a filled rectangle with rounded corners (r <= 8), the corners anti-aliased against `under` (what lies
 * behind it): the flat look's cards, panels, rows, bars and buttons */
static void cv_rrect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c, uint16_t under)
{
    int32_t i, j;
    GFX_HOOK_CELL(x, y + cv_oy, x + w, y + h + cv_oy);   /* (the lint: what is drawn on it stays inside it) */
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r < 1) {
        cv_rect(x, y, w, h, c);
        return;
    }
    cv_rect(x + r, y, w - 2 * r, h, c);
    cv_rect(x, y + r, r, h - 2 * r, c);
    cv_rect(x + w - r, y + r, r, h - 2 * r, c);
    for (j = 0; j < r; j++)
        for (i = 0; i < r; i++) {
            uint32_t a = rr_cov(r, i, j);
            uint16_t px = a >= 16u ? c : a == 0u ? under : ux_mix(under, c, (int32_t)(a * 100u / 16u));
            cv_pset(x + i, y + j, px);
            cv_pset(x + w - 1 - i, y + j, px);
            cv_pset(x + i, y + h - 1 - j, px);
            cv_pset(x + w - 1 - i, y + h - 1 - j, px);
        }
}

/* a disc of diameter d (px) centred on (cx, cy) (odd d: a pixel's centre; even: its corner), anti-aliased over
 * what is on the canvas (4 x 4 samples a pixel); ring: only its outer 1 px (the patterns' track numbers) */
static uint16_t mix565(uint16_t bg, uint16_t fg, uint32_t a);
static void cv_disc(int32_t cx, int32_t cy, int32_t d, uint16_t c, int ring)
{
    int32_t qx = cx * 8 + ((d & 1) ? 4 : 0), qy = (cy + cv_oy) * 8 + ((d & 1) ? 4 : 0), r = d * 4, ri = r - 8, x, y;
    for (y = cy + cv_oy - d / 2 - 1; y <= cy + cv_oy + d / 2 + 1; y++)
        for (x = cx - d / 2 - 1; x <= cx + d / 2 + 1; x++) {
            int32_t n = 0, i, j;
            if ((uint32_t)x >= cv_w || (uint32_t)y >= cv_h) continue;
            for (j = 0; j < 4; j++)
                for (i = 0; i < 4; i++) {
                    int32_t dx = x * 8 + 1 + 2 * i - qx, dy = y * 8 + 1 + 2 * j - qy, q = dx * dx + dy * dy;
                    n += q <= r * r && !(ring && q < ri * ri);
                }
            if (n) {
                uint16_t *p = &cv_px[(uint32_t)y * cv_w + (uint32_t)x];
                *p = swap16(mix565(swap16(*p), c, (uint32_t)n * 2u));
            }
        }
}
static void cv_circle(int32_t cx, int32_t cy, int32_t d, uint16_t c, uint16_t under) { (void)under; cv_disc(cx, cy, d, c, 0); }
static void cv_ring(int32_t cx, int32_t cy, int32_t d, uint16_t c) { cv_disc(cx, cy, d, c, 1); }
/* column x from y a to y b (1/16 px, either order) 2 px thick, its ends anti-aliased, blended over the canvas: a
 * smooth trace drawn a column at a time (Stage's waveform, docs/design) */
static void cv_vspan_aa(int32_t x, int32_t a, int32_t b, uint16_t c)
{
    int32_t lo = a < b ? a : b, hi = (a < b ? b : a) + 32, y;
    if ((uint32_t)x >= cv_w)
        return;
    for (y = lo >> 4; y <= (hi - 1) >> 4; y++) {
        int32_t py = y + cv_oy, top = y * 16, cov = (hi < top + 16 ? hi : top + 16) - (lo > top ? lo : top);
        uint16_t *p;
        if ((uint32_t)py >= cv_h || cov <= 0) continue;
        p = &cv_px[(uint32_t)py * cv_w + (uint32_t)x];
        *p = swap16(mix565(swap16(*p), c, (uint32_t)cov * 2u));
    }
}
/* a 1 px dashed outline, 3 on, 2 off (a pattern waiting: the design's dashed chip), its corners rounded (r) and solid */
static void cv_dashed(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c)
{
    int32_t i;
    for (i = r; i < w - r; i++)
        if ((i - r) % 5 < 3) { cv_pset(x + i, y, c); cv_pset(x + i, y + h - 1, c); }
    for (i = r; i < h - r; i++)
        if ((i - r) % 5 < 3) { cv_pset(x, y + i, c); cv_pset(x + w - 1, y + i, c); }
    for (i = 0; i < r; i++) {                       /* the corners: the quarter circle's pixels */
        int32_t j = r - 1 - i;
        cv_pset(x + i, y + j, c); cv_pset(x + w - 1 - i, y + j, c);
        cv_pset(x + i, y + h - 1 - j, c); cv_pset(x + w - 1 - i, y + h - 1 - j, c);
    }
}

/* ------------------------------------------------- 4-bit alpha blit --- */
/* coverage curves: a little heavier for light ink on dark, lighter for dark ink on light */
static const uint8_t CURVE_DARK[16] = {0, 24, 43, 62, 80, 97, 114, 130, 147, 163, 178, 194, 210, 225, 240, 255};
static const uint8_t CURVE_LIGHT[16] = {0, 12, 27, 42, 58, 75, 91, 109, 126, 144, 162, 180, 199, 217, 236, 255};
#define NRAMP 6u
static struct {
    uint16_t fg[NRAMP], bg[NRAMP], v[NRAMP][16];   /* v: byte-swapped panel values */
    uint32_t gen;
    uint8_t next, n;
} rc;

static const uint16_t *ramp(uint16_t fg, uint16_t bg)
{
    static const uint8_t SH[3] = {11, 5, 0}, MK[3] = {31, 63, 31};
    const uint8_t *cv = ux.light ? CURVE_LIGHT : CURVE_DARK;
    uint32_t i, a, k;
    uint16_t *v;
    if (rc.gen != ux.gen) {
        rc.gen = ux.gen;
        rc.n = rc.next = 0;
    }
    for (i = 0; i < rc.n; i++)
        if (rc.fg[i] == fg && rc.bg[i] == bg)
            return rc.v[i];
    i = rc.next;
    rc.next = (uint8_t)((i + 1u) % NRAMP);
    if (rc.n < NRAMP)
        rc.n++;
    rc.fg[i] = fg;
    rc.bg[i] = bg;
    v = rc.v[i];
    for (a = 0; a < 16u; a++) {
        uint32_t out = 0;
        if (ux.mono) {
            int32_t x = bg >> 11;
            out = ux_gray((uint32_t)(x + ((((fg >> 11) - x) * (int32_t)cv[a] + 128) >> 8)));
        } else {
            for (k = 0; k < 3u; k++) {
                int32_t x = (bg >> SH[k]) & MK[k], y = (fg >> SH[k]) & MK[k];
                out |= (uint32_t)(x + (((y - x) * (int32_t)cv[a] + 128) >> 8)) << SH[k];
            }
        }
        v[a] = swap16(out);
    }
    return v;
}

/* text over a picture (the Stage's notes over the waveform): with cv_over set, a glyph's pixel blends cv_over_fg into
 * the pixel already on the canvas, with the ramps' coverage curve, instead of taking a ramp's colour over a flat bg */
static uint8_t cv_over;
static uint16_t cv_over_fg;
/* fg over bg, a of 32 (0..32): the three channels at once, spread apart in a word (no division); MONO on the red
 * channel, so it stays gray */
static uint16_t mix565(uint16_t bg, uint16_t fg, uint32_t a)
{
    uint32_t b, f;
    if (ux.mono) {
        int32_t x = bg >> 11;
        return ux_gray((uint32_t)(x + ((((fg >> 11) - x) * (int32_t)a) >> 5)));
    }
    b = (bg | (uint32_t)bg << 16) & 0x07E0F81Fu;
    f = (fg | (uint32_t)fg << 16) & 0x07E0F81Fu;
    b = (b + (((f - b) * a) >> 5)) & 0x07E0F81Fu;
    return (uint16_t)(b | b >> 16);
}
static uint16_t cv_over_px(uint16_t under, uint32_t v)       /* under: the canvas pixel (byte-swapped), v: 1..15 */
{
    return swap16(mix565(swap16(under), cv_over_fg, ((ux.light ? CURVE_LIGHT : CURVE_DARK)[v & 15u] + 4u) >> 3));
}

/* w x h nibbles, rows back to back, at canvas (x, y) */
static void cv_alpha(int32_t x, int32_t y, uint32_t w, uint32_t h, const uint8_t *d, const uint16_t *rv)
{
    uint32_t k = 0, gx, gy;
    y += cv_oy;
    for (gy = 0; gy < h; gy++) {
        int32_t py = y + (int32_t)gy;
        uint16_t *row;
        if (py < cv_cy0 || py >= cv_cy1) {
            k += w;
            continue;
        }
        row = cv_px + (uint32_t)py * cv_w;
        for (gx = 0; gx < w; gx++, k++) {
            uint32_t v = (d[k >> 1] >> ((k & 1u) ? 0 : 4)) & 15u;
            int32_t px = x + (int32_t)gx;
            if (v && (uint32_t)px < cv_w)
                row[px] = cv_over ? cv_over_px(row[px], v) : rv[v];
        }
    }
    GFX_HOOK_PIXELS(w * h);
}

/* the same from a glyph's Huffman-coded bit stream (tools/gen_aa_font.py huff_pack): canonical codes, MSB
 * first; hc = the number of codes of each length 1..15, then the symbols in code order, each a value and
 * its repeat count ((count - 1) << 4 | value: runs of zeros and 15s). Codes of up to 8 bits come from a
 * 256-entry table made once per font in RAM (symbol | length << 8; 0 = a longer code: bit by bit). The
 * stream is read in order, so rows outside the clip are decoded and dropped; a run spans rows */
#define HC_FONTS 2                       /* the coded faces: M and L (gen_aa_font.py HUFF) */
static struct { const uint8_t *hc; uint16_t lut[256]; } hc_tab[HC_FONTS];
static const uint16_t *hc_lut(const uint8_t *hc)
{
    uint32_t k, len, code = 0, sym = 15, i;
    for (k = 0; k < HC_FONTS - 1u && hc_tab[k].hc && hc_tab[k].hc != hc; k++)
        ;
    if (hc_tab[k].hc != hc) {                        /* (more fonts than slots: the last slot is rebuilt) */
        hc_tab[k].hc = hc;
        for (i = 0; i < 256u; i++)
            hc_tab[k].lut[i] = 0;
        for (len = 1; len <= 8u; len++, code <<= 1)
            for (i = 0; i < hc[len - 1u]; i++, code++, sym++) {
                uint32_t a = code << (8u - len), n = 1u << (8u - len);
                while (n--)
                    hc_tab[k].lut[a + n] = (uint16_t)(hc[sym] | len << 8);
            }
    }
    return hc_tab[k].lut;
}

static void cv_alpha_hc(int32_t x, int32_t y, uint32_t w, uint32_t h, const uint8_t *d, const uint8_t *hc,
                        const uint16_t *rv)
{
    const uint16_t *lut = hc_lut(hc);
    uint32_t gy, v = 0, run = 0, acc = 0, nb = 0;
    y += cv_oy;
    for (gy = 0; gy < h; gy++) {
        int32_t py = y + (int32_t)gy;
        uint16_t *row = py < cv_cy0 || py >= cv_cy1 ? 0 : cv_px + (uint32_t)py * cv_w;
        uint32_t gx = 0;
        while (gx < w) {
            uint32_t n;
            if (!run) {                              /* the next symbol */
                uint32_t e, s;
                while (nb < 16u) {                   /* (the data ends with 2 spare bytes) */
                    acc = acc << 8 | *d++;
                    nb += 8;
                }
                e = lut[(acc >> (nb - 8u)) & 255u];
                if (e) {
                    nb -= e >> 8;
                    s = e & 255u;
                } else {                             /* a code over 8 bits */
                    uint32_t code = 0, first = 0, idx = 0, len = 0;
                    for (;;) {
                        code |= (acc >> --nb) & 1u;
                        if (code - first < hc[len])
                            break;
                        idx += hc[len];
                        first = (first + hc[len]) << 1;
                        code <<= 1;
                        len++;
                    }
                    s = hc[15u + idx + code - first];
                }
                v = s & 15u;
                run = (s >> 4) + 1u;
            }
            n = run < w - gx ? run : w - gx;
            if (v && row) {
                int32_t px = x + (int32_t)gx;
                uint32_t k;
                for (k = 0; k < n; k++, px++)
                    if ((uint32_t)px < cv_w)
                        row[px] = cv_over ? cv_over_px(row[px], v) : rv[v];
            }
            gx += n;
            run -= n;
        }
    }
    GFX_HOOK_PIXELS(w * h);
}

/* ------------------------------------------------------------- text --- */
/* the glyph number of ch in f, -1 = not in the face */
static int32_t glyph_at(const aafont_t *f, uint32_t ch)
{
    uint32_t i;
    if (ch >= f->first && ch <= f->last)
        return (int32_t)(ch - f->first);
    for (i = 0; i < f->nex; i++)
        if (f->ex[i] == ch)
            return (int32_t)(f->last - f->first + 1u + i);
    return -1;
}

/* the table entry of ch at phase 0 (its other phases follow it). Not in the face: the ellipsis as '.',
 * else '?', else the face's first glyph (L is sparse: its space) */
static uint32_t glyph(const aafont_t *f, uint32_t ch)
{
    int32_t k;
    if (ch >= 'a' && ch <= 'z' && f->last < 'a' && glyph_at(f, ch) < 0)
        ch -= 32u;                                   /* (a face without the small letter: its capital) */
    if ((k = glyph_at(f, ch)) < 0 && (ch != (uint8_t)ELLIPSIS || (k = glyph_at(f, '.')) < 0) &&
        (k = glyph_at(f, '?')) < 0)
        k = 0;
    return (uint32_t)k << f->psh;
}

static int32_t kern(const aafont_t *f, uint32_t a, uint32_t b)       /* 1/16 px */
{
    int32_t i, j;
    uint32_t l, r;
    if (!f->nkr || (i = glyph_at(f, a)) < 0 || (j = glyph_at(f, b)) < 0 || !(l = f->kl[i]) || !(r = f->kr[j]))
        return 0;
    return f->km[(l - 1u) * f->nkr + r - 1u];
}

static uint32_t fold(const aafont_t *f, uint32_t c)
{
    return c >= 'a' && c <= 'z' && f->last < 'a' && glyph_at(f, c) < 0 ? c - 32u : c;
}

static int32_t text_w(const aafont_t *f, const char *s)
{
    int32_t pen = 0;
    uint32_t prev = 0;
    for (; *s; s++) {
        uint32_t c = fold(f, (uint8_t)*s);
        if (prev)
            pen += kern(f, prev, c);
        pen += f->g[glyph(f, c)].adv;
        prev = c;
    }
    return (pen + 8) >> 4;
}

/* y = top of the line box (the baseline is y + f->asc); returns the pen x at the end.
 * fg on bg: bg is what lies under the text (the canvas, a card, a selection).
 * Placement: the pen (1/16 px) lies between two phase positions a step (16 >> psh) apart; the glyph takes the
 * one whose offset from the pen is nearer the previous glyph's offset, so the space between neighbours stays
 * truest and no glyph is a whole step from its true place (tools/aa_raster.py, text_spacing_test.py).
 * A glyph with one phase (its phases share one bitmap: figures and .) sits at the rounded pen, as when it
 * is drawn alone at text_w of what precedes it (ui_draw.c roll_text). */
static int32_t cv_text_flags(int32_t x, int32_t y, const aafont_t *f, const char *s, uint16_t fg, uint16_t bg,
                             uint32_t flags)
{
    const uint16_t *rv = ramp(fg, bg);
    const char *s0 = s;
    int32_t pen = 0, err = 0, x0 = 0x7FFF, y0 = 0x7FFF, x1 = -0x7FFF, y1 = -0x7FFF;
    const int32_t step = 16 >> f->psh;
    uint32_t prev = 0;
    cv_over_fg = fg;                                 /* (cv_over: blended over the canvas, bg unused) */
    for (; *s; s++) {
        uint32_t c = fold(f, (uint8_t)*s);
        const aag_t *g;
        int32_t pos, d0, d1;
        if (prev)
            pen += kern(f, prev, c);
        g = &f->g[glyph(f, c)];
        if (f->psh && g[1].off != g->off) {
            pos = pen - (pen & (step - 1));          /* the phase position at or left of the pen */
            d0 = pos - pen - err;                    /* .. and the one right of it: which keeps the gap truer */
            d1 = d0 + step;
            if ((d0 < 0 ? -d0 : d0) > (d1 < 0 ? -d1 : d1))
                pos += step;
            g += ((uint32_t)pos >> (4u - f->psh)) & ((1u << f->psh) - 1u);
        } else {
            pos = (pen + 8) & ~15;                   /* one phase (figures, ., blanks): the rounded pen */
        }
        err = pos - pen;
        if (g->bw) {
            int32_t gx = x + (pos >> 4) + g->bx, gy = y + g->by;
            if (f->hc)
                cv_alpha_hc(gx, gy, g->bw, g->bh, f->data + g->off, f->hc, rv);
            else
                cv_alpha(gx, gy, g->bw, g->bh, f->data + g->off, rv);
            if (gx < x0) x0 = gx;
            if (gy < y0) y0 = gy;
            if (gx + g->bw > x1) x1 = gx + g->bw;
            if (gy + g->bh > y1) y1 = gy + g->bh;
        }
        pen += g->adv;
        prev = c;
    }
    if (x1 > x0) {
        if (x0 < 0 || x1 > (int32_t)cv_w || y0 + cv_oy < cv_cy0 || y1 + cv_oy > cv_cy1)
            flags |= 2u;                             /* cut by the canvas */
        if (cv_scroll) {                             /* a scrolled view: what lands on the screen is its visible part */
            flags |= 32u;
            if (y0 + cv_oy < cv_cy0) y0 = cv_cy0 - cv_oy;
            if (y1 + cv_oy > cv_cy1) y1 = cv_cy1 - cv_oy;
        }
        if (cv_dim || cv_under)                      /* (the page under a question: covered on purpose) */
            flags |= 16u;
        GFX_HOOK_TEXT(x0, y0 + cv_oy, x1, y1 + cv_oy, s0, flags);
    }
    (void)s0;
    return x + ((pen + 8) >> 4);
}
static int32_t cv_text_on(int32_t x, int32_t y, const aafont_t *f, const char *s, uint16_t fg, uint16_t bg)
{
    return cv_text_flags(x, y, f, s, fg, bg, 0);
}
/* .. on the canvas background */
static int32_t cv_text(int32_t x, int32_t y, const aafont_t *f, const char *s, uint16_t fg)
{
    return cv_text_flags(x, y, f, s, fg, cv_bg, 0);
}
/* a scrim: the canvas inside (x, y, w, h) a of 32 of the way to c, the last f px towards each edge less (feathered): a
 * picture calmed behind text (cv_over) and still seen through it. Per pixel a blend of the row's or the column's
 * alpha (mix565), nothing else */
static void cv_scrim(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c, uint32_t a, int32_t f)
{
    int32_t i, j, x0 = x < 0 ? 0 : x, x1 = x + w < (int32_t)cv_w ? x + w : (int32_t)cv_w;
    y += cv_oy;
    for (j = y < 0 ? 0 : y; j < y + h && j < (int32_t)cv_h; j++) {
        int32_t dy = j - y < y + h - 1 - j ? j - y : y + h - 1 - j;
        uint16_t *row = &cv_px[(uint32_t)j * cv_w];
        for (i = x0; i < x1; i++) {
            int32_t d = i - x < x + w - 1 - i ? i - x : x + w - 1 - i;
            if (dy < d) d = dy;
            row[i] = swap16(mix565(swap16(row[i]), c, d >= f ? a : a * (uint32_t)(d + 1) / (uint32_t)(f + 1)));
        }
    }
}
/* right-aligned: the advance ends at xr */
static int32_t cv_text_r(int32_t xr, int32_t y, const aafont_t *f, const char *s, uint16_t fg, uint16_t bg)
{
    int32_t w = text_w(f, s);
    cv_text_on(xr - w, y, f, s, fg, bg);
    return xr - w;
}
static int32_t cv_text_c(int32_t xc, int32_t y, const aafont_t *f, const char *s, uint16_t fg, uint16_t bg)
{
    int32_t w = text_w(f, s);
    return cv_text_on(xc - w / 2, y, f, s, fg, bg);
}

/* s into d (n bytes), at most maxw px wide: never a smaller font, the end ellipsised. 1 = cut */
static int text_fit(char *d, uint32_t n, const char *s, const aafont_t *f, int32_t maxw)
{
    uint32_t k = 0;
    while (s[k] && k + 1u < n) {
        d[k] = s[k];
        k++;
    }
    d[k] = 0;
    if (!s[k] && text_w(f, d) <= maxw)
        return 0;
    if (k + 2u > n)
        k = n - 2u;
    for (;;) {                                       /* drop characters until "text..." fits */
        while (k && d[k - 1u] == ' ')
            k--;
        d[k] = ELLIPSIS;
        d[k + 1u] = 0;
        if (!k || text_w(f, d) <= maxw)
            return 1;
        k--;
    }
}
/* text in maxw px, ellipsised when it is longer: a label or a value should always fit (the host
 * layout lint reports a cut one), free text (cv_free_text: a preset name, a message) may be cut */
static int32_t cv_text_fit(int32_t x, int32_t y, const aafont_t *f, const char *s, uint16_t fg, uint16_t bg,
                           int32_t maxw)
{
    char b[48];
    int cut = text_fit(b, sizeof b, s, f, maxw);
    return cv_text_flags(x, y, f, b, fg, bg, cut ? 1u : 0u);
}
static int32_t cv_free_text(int32_t x, int32_t y, const aafont_t *f, const char *s, uint16_t fg, uint16_t bg,
                            int32_t maxw)
{
    char b[48];
    int cut = text_fit(b, sizeof b, s, f, maxw);
    return cv_text_flags(x, y, f, b, fg, bg, cut ? 9u : 8u);
}

/* ---------------------------------------------------------- keycaps --- */
/* a pre-rendered pill (tools/gen_aa_keycaps.py): nibble 0 outside, 1..4 the edge over `under`, 5 the fill,
 * 6..15 the label over the fill; the 16 colours per (under, fill, ink) are cached like the text ramps */
#define NKRAMP 4u
static struct {
    uint16_t key[NKRAMP][3], v[NKRAMP][16];
    uint32_t gen;
    uint8_t next, n;
} kr __attribute__((section(".pool")));      /* (zero-initialised; outside .bss) */
static const uint16_t *kc_ramp(uint16_t under, uint16_t fill, uint16_t ink)
{
    uint32_t i, a;
    uint16_t *v;
    if (kr.gen != ux.gen) {
        kr.gen = ux.gen;
        kr.n = kr.next = 0;
    }
    for (i = 0; i < kr.n; i++)
        if (kr.key[i][0] == under && kr.key[i][1] == fill && kr.key[i][2] == ink)
            return kr.v[i];
    i = kr.next;
    kr.next = (uint8_t)((i + 1u) % NKRAMP);
    if (kr.n < NKRAMP)
        kr.n++;
    kr.key[i][0] = under;
    kr.key[i][1] = fill;
    kr.key[i][2] = ink;
    v = kr.v[i];
    v[0] = swap16(under);
    for (a = 1; a < 5u; a++)
        v[a] = swap16(ux_mix(under, fill, (int32_t)a * 20));
    for (a = 5; a < 16u; a++)
        v[a] = swap16(ux_mix(fill, ink, (int32_t)(a - 5u) * 10));
    return v;
}
static int32_t kc_w(uint32_t id) { return id < KC_COUNT ? KC[id].w : 0; }
/* keycap / badge `id` with its top-left at (x, y), KC_H tall: fill and label tinted with tokens, `under` what lies
 * behind its corners; returns its right edge */
static int32_t cv_keycap(int32_t x, int32_t y, uint32_t id, uint16_t fill, uint16_t ink, uint16_t under)
{
    const kc_t *k = &KC[id < KC_COUNT ? id : 0u];
    const uint16_t *rv = kc_ramp(under, fill, ink);
    cv_alpha(x, y, k->w, KC_H, KC_DATA + k->off, rv);
    if (x < 0 || x + k->w > (int32_t)cv_w || y + cv_oy < 0 || y + KC_H + cv_oy > (int32_t)cv_h)
        GFX_HOOK_TEXT(x, y + cv_oy, x + k->w, y + KC_H + cv_oy, k->label, 6u | (cv_dim || cv_under ? 16u : 0u));   /* cut */
    else
        GFX_HOOK_TEXT(x, y + cv_oy, x + k->w, y + KC_H + cv_oy, k->label, 4u | (cv_dim || cv_under ? 16u : 0u));
    return x + k->w;
}

/* a key hint: the keycap, then what it does (S, TEXT); unavailable: a DIM keycap and a DIM word. The word sits on
 * the keycap's centre line (S caps 9 rows, the keycap's 7). act may be 0 (the keycap alone); returns the end x */
#define KH_GAP 4                                  /* keycap -> its word */
static int32_t kh_w(uint32_t id, const char *act) { return kc_w(id) + (act && act[0] ? KH_GAP + text_w(&AF_S, act) : 0); }
static int32_t cv_key_hint(int32_t x, int32_t y, uint32_t id, const char *act, int on, uint16_t under)
{
    x = cv_keycap(x, y, id, on ? T_KEY : T_DIM, T_INK, under);
    if (act && act[0])
        x = cv_text_on(x + KH_GAP, y - 1, &AF_S, act, on ? T_TEXT : T_DIM, under);
    return x;
}
/* a row of key hints from x0 to x1: the first at x0, the last ending at x1, the space shared between them. When
 * the row is too wide the action words go first, from the last hint back (the keycaps never shrink).
 * on: a bit per hint (1 = available) */
typedef struct { uint8_t key; const char *act; } khint_t;
static void cv_key_row(int32_t x0, int32_t x1, int32_t y, const khint_t *h, uint32_t n, uint32_t on, uint16_t under)
{
    uint32_t i, words = n;                         /* hints 0 .. words-1 keep their word */
    int32_t w, gap, x = x0;
    for (;;) {
        w = 0;
        for (i = 0; i < n; i++)
            w += kh_w(h[i].key, i < words ? h[i].act : 0);
        if (w + 6 * (int32_t)(n - 1u) <= x1 - x0 || !words)
            break;
        words--;
    }
    gap = n > 1u ? (x1 - x0 - w) / (int32_t)(n - 1u) : 0;
    for (i = 0; i < n; i++) {
        if (n > 1u && i + 1u == n)
            x = x1 - kh_w(h[i].key, i < words ? h[i].act : 0);   /* the last one flush right */
        x = cv_key_hint(x, y, h[i].key, i < words ? h[i].act : 0, (on >> i) & 1u, under) + gap;
    }
}

/* "[OCT+]..." -> the keycap's id and the length of the bracket, else -1 (a bracketed word that is no keycap is text) */
static int32_t kc_tag(const char *s, uint32_t *len)
{
    uint32_t i, n;
    if (s[0] != '[')
        return -1;
    for (n = 1; s[n] && s[n] != ']'; n++)
        if (n > 8u)
            return -1;
    if (s[n] != ']')
        return -1;
    for (i = 0; i < KC_COUNT; i++) {
        const char *l = KC[i].label;
        uint32_t k = 0;
        while (k + 1u < n && l[k] == s[k + 1u])
            k++;
        if (k + 1u == n && !l[k]) {
            *len = n + 1u;
            return (int32_t)i;
        }
    }
    return -1;
}
/* free text (S) that may start with a keycap, "[SAVE] HOLD TO UNDO", or have one after its first word,
 * "HOLD [GLO] QUICK": the word, the keycap, then the rest, cut to maxw */
static int32_t cv_free_hint(int32_t x, int32_t y, const char *s, uint16_t fg, uint16_t bg, int32_t maxw)
{
    uint32_t n, i;
    int32_t id = kc_tag(s, &n), x0 = x;
    for (i = 0; id < 0 && i < 7u && s[i] && s[i] != ' '; i++)
        ;
    if (id < 0 && i && s[i] == ' ' && (id = kc_tag(s + i + 1u, &n)) >= 0) {
        char w[8] = {0};
        uint32_t j;
        for (j = 0; j < i; j++)
            w[j] = s[j];
        x = cv_text_on(x, y, &AF_S, w, fg, bg) + KH_GAP;
        s += i + 1u;
    }
    if (id >= 0 && kc_w((uint32_t)id) + KH_GAP < maxw - (x - x0)) {
        x = cv_keycap(x, y + 1, (uint32_t)id, T_KEY, T_INK, bg) + KH_GAP;
        s += n;
        while (*s == ' ')
            s++;
        if (!*s)
            return x - KH_GAP;
    }
    return cv_free_text(x, y, &AF_S, s, fg, bg, maxw - (x - x0));
}

/* one-shot: a line of text in a box of colour bg, blitted (align: 0 left, 1 centre, 2 right) */
static void draw_text_line(uint32_t x, uint32_t y, uint32_t w, const aafont_t *f, const char *s,
                           uint16_t c, uint16_t bg, int align)
{
    int32_t tw = text_w(f, s), tx = 0;
    cv_begin(w, f->h, bg);
    if (align == 1)
        tx = ((int32_t)w - tw) / 2;
    else if (align == 2)
        tx = (int32_t)w - tw;
    cv_text(tx, 0, f, s, c);
    cv_blit(x, y);
    lcd_sync();                     /* one-shots (boot, crash, UBOOT, update) finish here */
}
static void draw_text_box(uint32_t x, uint32_t y, uint32_t w, const aafont_t *f, const char *s,
                          uint16_t c, int align)
{
    draw_text_line(x, y, w, f, s, c, T_BG, align);
}
