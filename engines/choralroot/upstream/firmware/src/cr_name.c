/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot FM-1: naming a sound with the root keys (mock-up state 23), Felucca's ui_name.c idea re-done small.
 * Upper case, at most 12 characters, and a CURSOR (0.22; the owner, 2026-10-13: "Select knob should select character,
 * knob 1 to change the letter at selected index, knob 4 CCW as a backspace, the other knobs 2 and 3 in the name editor
 * should not do anything"; and: "The cursor should always sit at the character you are editing, not the next
 * character"): cn.cur is always ON a character of the name, the last one when the page opens (the one KNOB 2 changed
 * until then). An empty name has no character and shows no cursor.
 *   SELECT              the cursor over the characters, a step a turn; right from the LAST character adds a
 *                       character ('A': the set's first is the space, which would show nothing) and the cursor
 *                       sits on it, up to 12, where it stops: how a name grows with the knobs
 *   KNOB 1              the character under the cursor through " A..Z 0..9 -." (an empty name: a first 'A')
 *   KNOB 4 left         backspace: the character UNDER the cursor goes, the cursor moves to the one before it (the
 *                       first deleted: it stays on the new first); right: nothing
 *   white roots D4..G5  phone style: ABC DEF GHI JKL MNO PQRS TUV WXYZ 0123 4567 89-. ; a tap types the group's
 *                       first character, another tap of the same key within 0.8 s the next one (cycling). A typed
 *                       character goes in AFTER the cursor and the cursor moves onto it: with the cursor on the
 *                       last character that is appending, as ever
 *   black roots         D#4 a space (typed the same way), F#4 the backspace (the others: nothing)
 * The name starts as the sound's own, "pristine" (grey): the first letter typed replaces it; F#4, a knob or the
 * cursor edit it. Until this cursor KNOB 2 changed the last character and the others kept their home jobs.
 * cr_ui.c opens it from SAVE's dialog (Save as new: the next free slot) and owns OCT+ and SAVE (save), OCT- and HOME
 * (cancel), and the knobs (cu_name_knob). */
#define CN_LEN 12u
#define CN_TAP_MS 800u
static const char *const CN_GROUP[11] = {"ABC", "DEF", "GHI", "JKL", "MNO", "PQRS", "TUV", "WXYZ", "0123",
                                         "4567", "89-."};
static const char CN_SET[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-.";
static struct {
    uint8_t len, key, tap, pristine;   /* key: the white root (place + 1) being cycled, 0 none */
    uint32_t t;
    char s[CN_LEN + 1u];
    uint8_t cur;                       /* the cursor: the character it is on, 0 .. len - 1 (an empty name: 0, none).
                                        * (After s: the struct stays 24 bytes) */
} cn;

static void cn_open(const char *prefill)
{
    str_cpy(cn.s, prefill, sizeof cn.s);
    cn.len = (uint8_t)str_len(cn.s);
    cn.cur = (uint8_t)(cn.len ? cn.len - 1u : 0u);
    cn.key = cn.tap = 0;
    cn.pristine = 1;
}
static void cn_touch(void)                         /* the first edit of the prefilled name */
{
    cn.pristine = 0;
}
static int cn_put(char c)                          /* c in after the cursor, the cursor onto it; 0: the name is full */
{
    uint32_t i, at = cn.len ? cn.cur + 1u : 0u;
    if (cn.len >= CN_LEN)
        return 0;
    for (i = cn.len; i > at; i--)
        cn.s[i] = cn.s[i - 1u];
    cn.s[at] = c;
    cn.s[++cn.len] = 0;
    cn.cur = (uint8_t)at;
    return 1;
}
static void cn_white(uint32_t place, uint32_t now) /* white root `place` (0 = D4) */
{
    const char *g = CN_GROUP[place % 11u];
    if (cn.pristine) {
        cn.len = cn.cur = 0;
        cn.s[0] = 0;
        cn_touch();
    }
    if (cn.key == place + 1u && now - cn.t < CN_TAP_MS && cn.len) {
        cn.tap = (uint8_t)((cn.tap + 1u) % str_len(g));
        cn.s[cn.cur] = g[cn.tap];
    } else {
        cn.tap = 0;
        cn.key = (uint8_t)(cn_put(g[0]) ? place + 1u : 0u);
    }
    cn.t = now;
}
static void cn_space(void)
{
    if (cn.pristine)
        cn_touch();
    cn.key = 0;
    (void)cn_put(' ');
}
static void cn_delete(void)                        /* F#4, KNOB 4 left: the character under the cursor */
{
    uint32_t i;
    cn_touch();
    cn.key = 0;
    if (!cn.len)
        return;
    for (i = cn.cur; i < cn.len; i++)              /* (the 0 at the end with them) */
        cn.s[i] = cn.s[i + 1u];
    cn.len--;
    if (cn.cur)
        cn.cur--;
}
static void cn_move(int32_t d)                     /* SELECT: the cursor; right from the last character: a new one */
{
    cn_touch();
    cn.key = 0;
    if (d < 0) {
        if (cn.cur)
            cn.cur--;
    } else if ((uint32_t)cn.cur + 1u < cn.len) {
        cn.cur++;
    } else {
        (void)cn_put('A');
    }
}
static void cn_knob(int32_t s)                     /* KNOB 1: the character under the cursor (no name: a first one) */
{
    uint32_t n = sizeof CN_SET - 1u, i = 0;
    cn_touch();
    cn.key = 0;
    if (!cn.len) {
        (void)cn_put('A');
        return;
    }
    while (i < n && CN_SET[i] != cn.s[cn.cur])
        i++;
    cn.s[cn.cur] = CN_SET[(i + n + (uint32_t)(s % (int32_t)n + (int32_t)n)) % n];
}
/* the name as it is saved: no spaces at either end (b holds 13); "" if empty */
static void cn_result(char *b)
{
    uint32_t i = 0, e = cn.len;
    while (i < e && cn.s[i] == ' ')
        i++;
    while (e > i && cn.s[e - 1u] == ' ')
        e--;
    str_cpy(b, cn.s + i, e - i + 1u);
}
/* the line shown: the name; the cursor is drawn under character cn.cur of it (cr_screen.h cr_line_t.cur: steady, no
 * blink: until the cursor a "_" blinked at the end, two redraws of the screen every 0.8 s) */
static void cn_line(char *b, uint32_t n, uint32_t now)
{
    (void)now;
    str_cpy(b, cn.s, n);
}
