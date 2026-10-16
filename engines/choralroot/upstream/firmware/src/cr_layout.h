/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot FM-1: a compiled layout's records (docs/LAYOUT-TOOL.md "Step 4, the firmware: what was built"). An
 * engine's editor screens as the owner designed them in the layout tool, compiled in (rule 4: never loaded on a unit).
 * tools/gen_layout.py writes the tables (build/gen/cr_layouts.h) from the designs of its BUILD list, checked against
 * design/layouts/<engine>.json; nothing here is written by hand. Two forms:
 *
 * - a CLASSIC layout (cr_layc_t): the engine's editor as a screen plan (core.h eng_screen_t) over pages of its own
 *   (eng_page_t: per column a copy of the built-in descriptor it binds and that column's built-in (page, col)).
 *   firmware/src/cr_pages.c cp_deep serves it as the engine's pages, get / set / desc / mod_dst going through to the
 *   built-in (page, col): the editor (cr_edit.c) shows the plan's screens; the blob, SysEx and the parameters stay the
 *   engine's. P5's rows (design/layouts/designs/p5-rows.json).
 * - FREE screens (cr_layout_t, cr_lay_screen_t, cr_lay_elem_t, one string pool: packed, no pointers: indices into the
 *   tables and offsets into the pool): placed elements, drawn by cr_draw.c CR_K_FREE, edited by cr_edit.c CE_S_FREE.
 *   Compiled into a unit only when a built design has them (CR_LAYOUTS_FREE 1); the layout tool's preview has them
 *   always (CR_LAYOUT_PREVIEW: its lay_load shows a design's free screens through them).
 *
 * firmware/src/cr_layout.c holds the one copy of the tables and the free screens' accessors. */
#pragma once
#include <stdint.h>

/* an element's type (web/fm1layout.js ELEMENT_TYPES, in its order) */
enum { CR_E_FRAME, CR_E_LABEL, CR_E_KNOB, CR_E_SWITCH, CR_E_LED, CR_E_BAR, CR_E_GLYPH, CR_E_BAND, CR_E_N };
/* an led's icon: one cycle of its wave (fm1layout.js LED_GLYPHS, in its order, after none) */
enum { CR_LI_NONE, CR_LI_SAW, CR_LI_SQUARE, CR_LI_TRIANGLE, CR_LI_SINE, CR_LI_NOISE };
/* a pair's half (an led's "pair") */
enum { CR_LP_NONE, CR_LP_LEFT, CR_LP_RIGHT };

/* the flags (cr_lay_elem_t.flags) */
#define CR_LF_ICON(f)   ((unsigned)(f) & 7u)           /* an led: CR_LI_* */
#define CR_LF_LEDS2     0x0008u                         /* an led of two LEDs (min red, max yellow) */
#define CR_LF_PAIR(f)   (((unsigned)(f) >> 4) & 3u)    /* an led: CR_LP_* */
#define CR_LF_BELOW     0x0040u                         /* an led: the label under the cap (resolved: "labelAt", or
                                                         * below for two LEDs) */
#define CR_LF_ALIGN(f)  (((unsigned)(f) >> 7) & 3u)    /* a label: 0 left, 1 centre, 2 right */
#define CR_LF_POS(f)    (((unsigned)(f) >> 9) & 3u)    /* a switch: its positions, 2 or 3 */
#define CR_LF_BOLD      0x0800u                         /* a label: bold (the other elements' texts always are) */
#define CR_LF_NAMES     0x1000u                         /* value names follow the label in the pool: NUL-terminated
                                                         * strings up to an empty one (a switch's positions, a two-LED
                                                         * button's two in capitals, a knob's enum names) */

#define CR_LAY_PART  0xFFu      /* cr_lay_elem_t.page: a part parameter, col = its P_ id */
#define CR_LAY_NONE  0xFEu      /* cr_lay_elem_t.page: bound to nothing (a frame, a label, an unbound glyph) */
#define CR_LAY_NOSTR 0xFFFFu    /* a string offset: none */
#define CR_LAY_EMPTY 0xFFu      /* cr_lay_screen_t.lane: an empty slot */
#define CR_LAY_LANES 8u         /* lanes a screen (SELECT), four slots a lane (KNOB 1..4) */
#define CR_LAY_SLOTS 4u

/* an element, 14 B. Frames come first in a screen (they are drawn first) */
typedef struct {
    uint8_t type;               /* CR_E_* */
    uint8_t x, y, w, h;         /* px, on the 4 px grid, inside rows 28 .. 239 (under the title line) */
    uint8_t page, col;          /* the built-in address: an engine page's column, CR_LAY_PART, CR_LAY_NONE */
    uint8_t size;               /* a knob: its dial (28 .. 44); a label: its px; a glyph: its CR_G_* (cr_screen.h) */
    uint8_t r;                  /* a frame: its corners' radius, 0 .. 12 */
    uint8_t colour;             /* CR_COL_* (cr_screen.h); CR_COL_NONE = auto (the lane's knob colour, dim, text) */
    uint16_t flags;             /* CR_LF_* */
    uint16_t label;             /* the label (a frame's or a label's text) in the pool, CR_LAY_NOSTR none */
} cr_lay_elem_t;

/* a screen, 38 B: an editor group's screens are its records in order */
typedef struct {
    uint8_t group;              /* the editor's group (0 OSC 1 FILT 2 ENV 3 LFO 4 MOD) */
    uint8_t first, count;       /* its elements: the engine's elements from first (cr_layout_t.elem0 + first) */
    uint8_t nlanes;             /* 1 .. 8 */
    uint16_t title;             /* the title line's right text in the pool */
    uint8_t lane[CR_LAY_LANES][CR_LAY_SLOTS];   /* an element within the screen, CR_LAY_EMPTY none */
} cr_lay_screen_t;

/* an engine's layout, 8 B */
typedef struct {
    uint8_t engine;             /* engines.c ENGINES[] index */
    uint8_t nscreens, screen0;  /* its screens: CR_LAY_SCREENS[screen0 ..] */
    uint8_t rsv;
    uint16_t elem0, nelems;     /* its elements: CR_LAY_ELEMS[elem0 ..] */
} cr_layout_t;

_Static_assert(sizeof(cr_lay_elem_t) == 14u, "cr_lay_elem_t: 14 B");
_Static_assert(sizeof(cr_lay_screen_t) == 38u, "cr_lay_screen_t: 38 B");
_Static_assert(sizeof(cr_layout_t) == 8u, "cr_layout_t: 8 B");

/* a classic layout (the generated CR_LAYC[]): an engine's pages CR_LAYC_PAGES[page0 ..] with their built-in sources
 * CR_LAYC_SRC[page0 ..], its screen plan CR_LAYC_SCREENS[screen0 ..], its sections (eng_deep_t.section) */
typedef struct {
    uint8_t engine;             /* engines.c ENGINES[] index */
    uint8_t npages, page0;
    uint8_t nscreens, screen0;
    uint8_t section[8];
    const char *design;         /* the design file it was compiled from */
} cr_layc_t;
