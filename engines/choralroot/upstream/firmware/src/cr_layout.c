/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot FM-1: the compiled layouts (docs/LAYOUT-TOOL.md "Step 4"): the one copy of the tables
 * (build/gen/cr_layouts.h, written by tools/gen_layout.py) and what the editor (cr_edit.c CE_S_FREE) and the renderer
 * (cr_draw.c CR_K_FREE) read a free layout by. Included once, after core.h and before cr_draw.c, by the unity builds
 * (choralroot.c, the emulator's emu_firmware.h) and by tests/cr_draw_test.c. A classic layout (CR_LAYC[]) is served by
 * cr_pages.c cp_deep. A free layout is named by its index + 1 (cr_screen_t.fl_eng), a screen by its index in its
 * layout + 1 (fl_scr); 0 is none. */
#include "cr_layout.h"
#include "cr_layouts.h"

/* the free screens' editor and renderer are in this build: a built design has free screens (tools/gen_layout.py), or
 * the layout tool's preview (its lay_load shows a design's free screens); else none of their code is compiled */
#if CR_LAYOUTS_FREE || defined(CR_LAYOUT_PREVIEW)
#define CR_LAY_FREE 1
#else
#define CR_LAY_FREE 0
#endif

#if CR_LAY_FREE
#ifdef CR_LAYOUT_PREVIEW
/* (the layout tool's preview only) the tables the accessors read: the compiled ones, or a design the tool loaded
 * (tools/layout/layout_web.c lay_load). cr_layout_hook(eng) sets them and answers engine eng's layout in them (its
 * index + 1; 0: none, the engine's classic pages are shown) */
typedef struct {
    const cr_layout_t *lay;
    uint32_t nlay;
    const cr_lay_screen_t *scr;
    const cr_lay_elem_t *el;
    const char *pool;
    uint32_t npool;
} cr_lay_tables_t;
static const cr_lay_tables_t CR_LAY_COMPILED = {CR_LAYOUTS, CR_LAY_NENGINES, CR_LAY_SCREENS, CR_LAY_ELEMS, CR_LAY_POOL,
                                                CR_LAY_POOL_SIZE};
static cr_lay_tables_t cr_lt = {CR_LAYOUTS, CR_LAY_NENGINES, CR_LAY_SCREENS, CR_LAY_ELEMS, CR_LAY_POOL, CR_LAY_POOL_SIZE};
static uint32_t (*cr_layout_hook)(uint32_t eng);
#define CR_LT_LAYOUTS cr_lt.lay
#define CR_LT_NENGINES cr_lt.nlay
#define CR_LT_SCREENS cr_lt.scr
#define CR_LT_ELEMS cr_lt.el
#define CR_LT_POOL cr_lt.pool
#define CR_LT_POOL_SIZE cr_lt.npool
#else
#define CR_LT_LAYOUTS CR_LAYOUTS          /* (a unit: the compiled tables, always) */
#define CR_LT_NENGINES CR_LAY_NENGINES
#define CR_LT_SCREENS CR_LAY_SCREENS
#define CR_LT_ELEMS CR_LAY_ELEMS
#define CR_LT_POOL CR_LAY_POOL
#define CR_LT_POOL_SIZE CR_LAY_POOL_SIZE
#endif

/* engine eng's compiled layout (engines.c ENGINES[] index): its index + 1, 0 none */
static uint32_t cr_lay_find(uint32_t eng)
{
    uint32_t i;
#ifdef CR_LAYOUT_PREVIEW
    if (cr_layout_hook)
        return cr_layout_hook(eng);
#endif
    for (i = 0; i < CR_LT_NENGINES; i++)
        if (CR_LT_LAYOUTS[i].engine == eng)
            return i + 1u;
    return 0;
}
static const cr_layout_t *cr_lay(uint32_t li) { return li && li <= CR_LT_NENGINES ? &CR_LT_LAYOUTS[li - 1u] : 0; }
/* layout li's screen si (index + 1), 0 none */
static const cr_lay_screen_t *cr_lay_scr(uint32_t li, uint32_t si)
{
    const cr_layout_t *L = cr_lay(li);
    return L && si && si <= L->nscreens ? &CR_LT_SCREENS[L->screen0 + si - 1u] : 0;
}
/* the screens of group g (0 OSC .. 4 MOD) in layout li: how many; *si: the k-th's index + 1 (0: none) */
static uint32_t cr_lay_group(uint32_t li, uint32_t g, uint32_t k, uint32_t *si)
{
    const cr_layout_t *L = cr_lay(li);
    uint32_t i, n = 0;
    *si = 0;
    for (i = 0; L && i < L->nscreens; i++)
        if (CR_LT_SCREENS[L->screen0 + i].group == g) {
            if (n == k)
                *si = i + 1u;
            n++;
        }
    return n;
}
/* element i of screen s of layout li (0: none) */
static const cr_lay_elem_t *cr_lay_el(uint32_t li, const cr_lay_screen_t *s, uint32_t i)
{
    const cr_layout_t *L = cr_lay(li);
    return L && s && i < s->count ? &CR_LT_ELEMS[L->elem0 + s->first + i] : 0;
}
/* the element on lane `lane`, KNOB slot + 1: its index in the screen, CR_LAY_EMPTY none */
static uint32_t cr_lay_slot(const cr_lay_screen_t *s, uint32_t lane, uint32_t slot)
{
    return s && lane < s->nlanes && lane < CR_LAY_LANES && slot < CR_LAY_SLOTS ? s->lane[lane][slot] : CR_LAY_EMPTY;
}
/* the lane and slot an element is on: lane * 4 + slot, CR_LAY_EMPTY in none */
static uint32_t cr_lay_where(const cr_lay_screen_t *s, uint32_t i)
{
    uint32_t k;
    for (k = 0; s && k < (uint32_t)s->nlanes * CR_LAY_SLOTS && k < CR_LAY_LANES * CR_LAY_SLOTS; k++)
        if (s->lane[k / CR_LAY_SLOTS][k % CR_LAY_SLOTS] == i)
            return k;
    return CR_LAY_EMPTY;
}
/* a string of the pool ("" for CR_LAY_NOSTR) */
static const char *cr_lay_str(uint32_t off) { return off < CR_LT_POOL_SIZE ? CR_LT_POOL + off : ""; }
/* the value names after an element's label (CR_LF_NAMES): the first, 0 none; the next by cr_lay_next */
static const char *cr_lay_names(const cr_lay_elem_t *e)
{
    const char *p;
    if (!(e->flags & CR_LF_NAMES) || e->label >= CR_LT_POOL_SIZE)
        return 0;
    for (p = CR_LT_POOL + e->label; *p; p++)
        ;
    return p[1] ? p + 1 : 0;
}
static const char *cr_lay_next(const char *n)
{
    while (*n)
        n++;
    return n[1] ? n + 1 : 0;
}
#endif /* CR_LAY_FREE */
