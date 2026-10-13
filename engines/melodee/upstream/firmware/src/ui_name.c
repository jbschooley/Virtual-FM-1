/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Melodee UI: NAME, naming a user preset or a project on the device (included by ui_input.c).
 *
 * Where: SAVE > USER, SAVE (KNOB 4, OCT+) and SAVE > PROJECT, SAVE: after the OVERWRITE? dialog when the slot is
 * used, the NAME screen opens before anything is written; OCT+ writes, OCT- cancels (back to the page, nothing
 * written). EDIT on those pages renames the selected slot (only its name is written). The name is prefilled:
 * USER the sound's own (the user preset it came from) or the automatic one ("DIGITAL 14"), PROJECT the current
 * project's (as loaded or last saved; none: "PROJECT A"), so SAVE, OCT+, OCT+ saves with the default name.
 * Writing needs the transport stopped (STOP TO SAVE; the screen stays).
 *
 * The keys type and never sound, record or send MIDI (seq.c keyboard_block: song.grid 2). Upper case, at most 12:
 *   white keys   ABC (letters): AB CD EF GH IJK LM NO PQ RS TU VW XYZ, then 123 456 789 0-. ; phone style: a tap
 *                types the group's first character, another tap of the same key within 0.8 s the next one
 *                (cycling); another key or 0.8 s without a tap keeps it and moves on.
 *                123 (digits and symbols): 1 2 3 4 5 6 7 8 9 0 - . _ / # + , one tap each.
 *   black keys   by name, in both octaves: F# cursor left, G# SPACE, A# cursor right, C# DELETE (the character
 *                before the cursor; held: repeats, as the arrows do), D# ABC / 123.
 *   KNOB 1       the cursor; KNOB 2 the character at the cursor (SPACE A..Z 0..9 - . _ / # +; at the end: a new one).
 *   LEDs         every key that types or edits is lit; the key whose letters are being cycled blinks.
 * Spaces at either end are dropped when it is written; an empty name: USER the automatic one, PROJECT none. */
enum { NK_NONE, NK_USER_SAVE, NK_USER_RENAME, NK_PROJ_SAVE, NK_PROJ_RENAME, NK_CZ_NAME };
#define NM_LEN 12u
#define NM_TAP_MS 800u                                 /* multi-tap: the next letter within this */
#define NM_REP_MS 450u                                 /* a held arrow / DELETE repeats after this, */
#define NM_RATE_MS 90u                                 /* .. every this */
enum { NB_LEFT, NB_SPACE, NB_RIGHT, NB_DEL, NB_MODE, NB_NONE };
static const char *const NM_ABC[16] = {"AB", "CD", "EF", "GH", "IJK", "LM", "NO", "PQ", "RS", "TU", "VW", "XYZ",
                                       "123", "456", "789", "0-."};
static const char NM_NUM[17] = "1234567890-._/#+";
static const char NM_SET[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._/#+";   /* KNOB 2 */
static struct {
    uint8_t kind;                                      /* NK_*: NK_NONE = closed */
    uint8_t slot;
    uint8_t len, cur;                                  /* the name's length; the cursor 0..len */
    uint8_t num;                                       /* the white keys: 0 ABC, 1 123 */
    uint8_t key;                                       /* the white key (place + 1) of the letter being cycled, 0 none */
    uint8_t tap;                                       /* .. that letter's index in its group */
    uint8_t rep;                                       /* the key (+ 1) of a held arrow / DELETE, 0 none */
    uint32_t t, rep_t;                                 /* fm1_ms of the last tap; of the next repeat */
    uint32_t sig;                                      /* drawn-state cache */
    char s[P5_NAME_LEN + 1u];
    char ph[P5_NAME_LEN + 1u];                              /* what an empty name saves as / shows ("PROJECT A") */
} nm __attribute__((section(".pool")));                /* (zero-initialised; main loop only: off the audio code's .bss) */

static uint32_t name_limit(void)
{
    if(nm.kind==NK_CZ_NAME)return CZ_NAME_LEN;
    if((nm.kind==NK_USER_SAVE || nm.kind==NK_USER_RENAME) && native_limit(TSEL->eng_req))return TSEL->eng_req==ENGI_PROPHET?P5_NAME_LEN:TSEL->eng_req==ENGI_FM6?10u:CZ_NAME_LEN;
    return NM_LEN;
}
static int name_on(void) { return nm.kind != NK_NONE; }
static void name_close(void)
{
    if (nm.kind)
        ui.force = 1;
    nm.kind = NK_NONE; ui.name_active = 0;
}
/* the keys: 2 = NAME's (silent), 1 = the DRUM grid, 0 = notes (seq.c keyboard_block reads song.grid) */
static uint32_t keys_mode(void) { return name_on() ? 2u : designer_on() ? 0u : (uint32_t)grid_on(); }

static const char *nm_group(uint32_t p)                /* white key place p's characters */
{
    static char one[2];
    if (!nm.num)
        return NM_ABC[p & 15u];
    one[0] = NM_NUM[p & 15u];
    return one;
}
static uint32_t nm_black(uint32_t k)                   /* black key k's function: by its name (F# .. D#) */
{
    switch ((k + 5u) % 12u) {
    case 6: return NB_LEFT;
    case 8: return NB_SPACE;
    case 10: return NB_RIGHT;
    case 1: return NB_DEL;
    case 3: return NB_MODE;
    default: return NB_NONE;
    }
}

static void name_open(uint32_t kind, uint32_t slot)
{
    char b[P5_NAME_LEN+1u];
    nm.kind = (uint8_t)kind; ui.name_active = 1;
    nm.slot = (uint8_t)slot;
    nm.num = nm.key = nm.rep = 0;
    b[0] = 0;
    if(kind==NK_CZ_NAME){
        memcpy(b,cz_patch[slot].raw+128,CZ_NAME_LEN);b[CZ_NAME_LEN]=0;
        for(uint32_t k=0;k<CZ_NAME_LEN;k++)if(b[k]<' ' || b[k]>'~')b[k]=' ';   /* (LCD custom characters) */
        for(uint32_t k=CZ_NAME_LEN;k && b[k-1]==' ';k--)b[k-1]=0;
        str_cpy(nm.ph,"CZ TONE",sizeof nm.ph);
    }else if (kind == NK_USER_SAVE || kind == NK_USER_RENAME) {
        uint32_t e = kind == NK_USER_RENAME && !native_limit(TSEL->eng_req) ? up_engine(slot) : TSEL->eng_req;
        up_auto_name(nm.ph, e, slot);
        uint32_t origin=kind==NK_USER_RENAME?slot:user_of(TSEL);
        if(origin<USER_NONE && (kind==NK_USER_RENAME || TSEL->user_native) && native_limit(e)){
            uint32_t len=e==ENGI_PROPHET?20u:e==ENGI_FM6?10u:16u,off=e==ENGI_PROPHET?P5_NAME:e==ENGI_FM6?118u:128u;
            memcpy(b,native_raw(e,origin)+off,len);b[len]=0;
            for(uint32_t i=0;i<len;i++)if(b[i]<' ' || b[i]>'~')b[i]=' ';
            for(uint32_t i=len;i && b[i-1]==' ';i--)b[i-1]=0;
        }
        else if (origin < UP_SLOTS)
            up_name(origin, b);
        else if(native_limit(e)){
            if(e==ENGI_PROPHET)p5_patch_name(b,p5_patch_of(TSEL));else if(e==ENGI_FM6)fm6_name(b,fm6_patch[song.sel]);else {memcpy(b,cz_patch[song.sel].raw+128,16);b[16]=0;}
        }
        else
            str_cpy(b, nm.ph, sizeof b);
    } else {
        str_cpy(nm.ph, "PROJECT A", sizeof nm.ph);
        nm.ph[8] = (char)('A' + (slot & 3u));
        if (kind == NK_PROJ_RENAME)
            project_name(slot, b);
        else
            project_cur_name(b);
    }
    str_cpy(nm.s, b, sizeof nm.s);
    nm.len = nm.cur = (uint8_t)str_len(nm.s);
    ui.force = 1;
}

/* SAVE > USER / PROJECT, EDIT: rename the selected slot */
static void name_rename(void)
{
    int user = cur_page()->graph == GR_USER;
    uint32_t k = user ? ui.uslot : (uint32_t)song.g[G_SLOT] - 1u;
    if (!user && song.g[G_SLOT] == PROJ_TMPL)
        return;                                       /* (the template has no name) */
    if (!(user ? user_used(k) : project_used(k)))
        ui_message("EMPTY SLOT");
    else if (transport_busy())
        ui_message("STOP TO SAVE");
    else
        name_open(user ? NK_USER_RENAME : NK_PROJ_RENAME, k);
}

static void nm_commit(void)                            /* the letter being cycled is kept: the cursor past it */
{
    if (nm.key) {
        nm.key = 0;
        nm.cur++;
    }
}
static int nm_insert(char c)                           /* at the cursor (it stays on it); 0 = full */
{
    uint32_t i;
    if (nm.len >= name_limit()) {
        ui_message("NAME FULL");
        return 0;
    }
    for (i = nm.len; i > nm.cur; i--)
        nm.s[i] = nm.s[i - 1u];
    nm.s[nm.cur] = c;
    nm.s[++nm.len] = 0;
    return 1;
}
static void nm_white(uint32_t p)
{
    const char *g = nm_group(p);
    uint32_t n = str_len(g);
    if (nm.key == p + 1u && fm1_ms - nm.t < NM_TAP_MS) {   /* the same key again: its next character */
        nm.tap = (uint8_t)((nm.tap + 1u) % n);
        nm.s[nm.cur] = g[nm.tap];
        nm.t = fm1_ms;
        return;
    }
    nm_commit();
    if (!nm_insert(g[0]))
        return;
    if (n > 1u) {
        nm.key = (uint8_t)(p + 1u);
        nm.tap = 0;
        nm.t = fm1_ms;
    } else {
        nm.cur++;
    }
}
static void nm_do(uint32_t f)                          /* a black key's function */
{
    uint32_t i;
    nm_commit();
    switch (f) {
    case NB_LEFT:
        if (nm.cur)
            nm.cur--;
        break;
    case NB_RIGHT:
        if (nm.cur < nm.len)
            nm.cur++;
        break;
    case NB_SPACE:
        if (nm_insert(' '))
            nm.cur++;
        break;
    case NB_DEL:
        if (!nm.cur)
            break;
        for (i = --nm.cur; i < nm.len; i++)
            nm.s[i] = nm.s[i + 1u];
        nm.len--;
        break;
    case NB_MODE:
        nm.num ^= 1u;
        break;
    default:
        break;
    }
}
static void nm_knob(uint32_t k, int32_t s)             /* KNOB 1 the cursor, KNOB 2 the character there */
{
    int32_t i, n = (int32_t)sizeof NM_SET - 1;
    nm_commit();
    if (k == 0u) {
        nm.cur = (uint8_t)clamp((int32_t)nm.cur + s, 0, nm.len);
        return;
    }
    if (nm.cur == nm.len && !nm_insert(' '))           /* at the end: a new character (from SPACE) */
        return;
    for (i = 0; i < n && NM_SET[i] != nm.s[nm.cur]; i++)
        ;
    i = i < n ? i : 0;
    nm.s[nm.cur] = NM_SET[((i + s) % n + n) % n];
}

/* OCT+: the name (spaces at its ends dropped) is written; the screen stays while that is refused (playing) */
static void name_ok(void)
{
    char b[P5_NAME_LEN + 1u];
    uint32_t a = 0, z;
    nm_commit();
    for (z = nm.len; z && nm.s[z - 1u] == ' '; z--)
        ;
    while (a < z && nm.s[a] == ' ')
        a++;
    for (z -= a, b[z] = 0; z--;)
        b[z] = nm.s[a + z];
    if (transport_busy()) {
        ui_message("STOP TO SAVE");
        return;
    }
    switch (nm.kind) {
    case NK_CZ_NAME:
        cz_compare_take(nm.slot);memset(cz_patch[nm.slot].raw+128,' ',CZ_NAME_LEN);memcpy(cz_patch[nm.slot].raw+128,b,str_len(b));
        ui_message("TONE NAMED");break;
    case NK_USER_SAVE:
        user_ui_named(2, nm.slot, b);
        break;
    case NK_USER_RENAME:
        user_ui_named(3, nm.slot, b);
        break;
    case NK_PROJ_SAVE:
        project_save_as(nm.slot, b);
        break;
    default:
        project_rename(nm.slot, b);
        break;
    }
    name_close();
}

/* one UI frame of NAME (ui_input.c): note edges, OCT taps (bit 0 OCT-, bit 1 OCT+) */
static void name_input(uint32_t notes, uint32_t oct)
{
    uint32_t k;
    int32_t s;
    song.grid = 2;                                     /* (already: keys_mode) */
    if (nm.key && fm1_ms - nm.t >= NM_TAP_MS)          /* 0.8 s without a tap: the letter is kept */
        nm_commit();
    for (k = 0; k < 27u; k++) {
        if (!((notes >> k) & 1u))
            continue;
        if (!key_black(k)) {
            nm_white(key_place(k));
            continue;
        }
        nm_do(nm_black(k));
        if (nm_black(k) == NB_LEFT || nm_black(k) == NB_RIGHT || nm_black(k) == NB_DEL) {
            nm.rep = (uint8_t)(k + 1u);
            nm.rep_t = fm1_ms + NM_REP_MS;
        }
    }
    if (nm.rep && !((fm1_in.notes >> (nm.rep - 1u)) & 1u))
        nm.rep = 0;
    else if (nm.rep && (int32_t)(fm1_ms - nm.rep_t) >= 0) {
        nm_do(nm_black(nm.rep - 1u));
        nm.rep_t = fm1_ms + NM_RATE_MS;
    }
    for (k = 0; k < 2u; k++)
        if ((s = panel_enc(EN_K1 + k)) != 0)
            nm_knob(k, s);
    enc_drop();                                        /* the other knobs do nothing here */
    if (oct & 2u)
        name_ok();
    else if (oct & 1u)
        name_close();                                  /* cancel: nothing written, back to the page */
}

/* the key LEDs: every key that does something lit, the key whose letters are cycling blinks */
static uint32_t name_leds(void)
{
    uint32_t k, m = 0, blink = ((fm1_ms / 250u) & 1u) == 0u;
    for (k = 0; k < 27u; k++) {
        uint32_t on = key_black(k) ? nm_black(k) != NB_NONE : nm.key != key_place(k) + 1u || blink;
        m |= on << k;
    }
    return m;
}

/* ----------------------------------------------------------- drawing --- */
/* The header stays. The card band is the name field: a SURF card, "SAVE U07" / "NAME PROJECT A" and the length,
 * then 12 cells (M): the cursor an ACCENT box, the letter being cycled a THEME cell (INK), a space a DIM dot, an
 * empty name its placeholder in DIM. The panel: the cycling key's letters (the one typed THEME, the next ACCENT)
 * or a hint, and ABC / 123; the white keys as two rows of 8 pills (the left octave, then the right one; the
 * cycling key THEME); the black keys' functions as five KEY cells under their names. The footer: OCT+ / OCT-,
 * then the knobs and the keys */
static void nm_title(char *b)
{
    str_cpy(b, nm.kind == NK_USER_SAVE || nm.kind == NK_PROJ_SAVE ? "SAVE " : "NAME ", 8);
    if(nm.kind==NK_CZ_NAME){str_cpy(b,"NAME CZ TONE",24);return;}
    if (nm.kind <= NK_USER_RENAME)
        user_label(b + 5, nm.slot);
    else
        str_cpy(b + 5, nm.ph, 12);                     /* "PROJECT A" */
}
/* NAME (docs/design: mock/r6_pages NAME): what it names and the count; the name large (the cursor's letter on the
 * track's colour; at the end a bar where the next one goes), a line under it; the characters around the cursor's
 * (KNOB 2 turns through them: the cursor's in a ring); ABC / 123 (what the white keys type, D#); the keys: OCT-
 * back, OCT+ Save (or Rename; dim while it cannot write) */
static void nm_draw_name(void)
{
    char b[24], c[2] = {0, 0};
    uint32_t i, empty = nm.len == 0u, first = 0, n = empty ? str_len(nm.ph) : nm.len;
    const aafont_t *f = name_limit() <= NM_LEN ? &AF_L : &AF_M;
    int32_t x, w = 0;
    cv_begin(240, 18, T_BG);                            /* "Save U07", "7 / 12" */
    nm_title(b);
    for (i = 1, x = 0; b[i]; i++) {                     /* "SAVE PROJECT C" -> "Save project C"; U07, CZ kept */
        uint32_t e = i, alpha = 1;
        if (b[i - 1] != ' ' || b[i] == ' ')
            continue;
        while (b[e] && b[e] != ' ') alpha &= b[e] >= 'A' && b[e] <= 'Z', e++;
        if (alpha && e - i > 2u)
            for (; i < e; i++) b[i] = (char)(b[i] + 32);
    }
    for (i = 1; b[i] && b[i] != ' '; i++)
        if (b[i] >= 'A' && b[i] <= 'Z') b[i] = (char)(b[i] + 32);
    cv_text_on(10, 3, &AF_X, b, T_MID, T_BG);
    fmt_int(b, nm.len);
    str_cpy(b + str_len(b), " / ", 4);
    fmt_int(b + str_len(b), (int32_t)name_limit());
    cv_text_r(230, 3, &AF_X, b, nm.len >= name_limit() ? T_THEME : T_MID, T_BG);
    cv_blit(0, 26);
    for (i = nm.cur; i > 0u; i--) {                     /* (the window: the cursor's letter in the 210 px) */
        c[0] = i - 1u < n ? (empty ? nm.ph[i - 1u] : nm.s[i - 1u]) : ' ';
        w += text_w(f, c) + 2;
        if (w > 204) { first = i; break; }
    }
    cv_begin(240, 52, T_BG);
    for (i = first, x = 12; i <= n && i <= nm.cur + 12u && x < 228; i++) {
        int on = !empty && i == nm.cur && i < n;
        c[0] = i < n ? (empty ? nm.ph[i] : nm.s[i]) : 0;
        if (!c[0]) {                                    /* at the end: where the next one goes */
            if (i == nm.cur)
                cv_rrect(x + 1, f == &AF_L ? 8 : 18, 3, f == &AF_L ? 32 : 18, 1, T_THEME, T_BG);
            break;
        }
        w = text_w(f, c);
        if (on)
            cv_rrect(x - 1, f == &AF_L ? 6 : 16, w + 2, f == &AF_L ? 36 : 20, 4, T_THEME, T_BG);
        cv_text_on(x, f == &AF_L ? 6 : 17, f, c, on ? T_INK : empty ? T_DIM : T_TEXT, on ? T_THEME : T_BG);
        x += w + 2;
    }
    cv_rect(10, 48, 220, 1, T_LINE);
    cv_blit(0, 46);
    {                                                   /* the characters around the cursor's */
        uint32_t at = 0, len = sizeof NM_SET - 1u;
        int32_t d;
        char ch = !empty && nm.cur < nm.len ? nm.s[nm.cur] : ' ';
        while (at < len && NM_SET[at] != ch) at++;
        if (at >= len) at = 0;
        cv_begin(240, 30, T_BG);
        for (d = -4; d <= 4; d++) {
            uint32_t j = (uint32_t)(((int32_t)at + d + (int32_t)len) % (int32_t)len);
            int32_t cx = 120 + d * 24;
            c[0] = NM_SET[j];
            if (!d) {
                cv_rrect(cx - 13, 1, 26, 28, 6, T_THEME, T_BG);
                cv_rrect(cx - 11, 3, 22, 24, 5, T_LIFT, T_THEME);
            }
            if (c[0] == ' ')
                cv_disc(cx, 15, 3, d ? T_DIM : T_TEXT, 0);
            else if (!d)
                cv_text_c(cx, 6, &AF_M, c, T_TEXT, T_LIFT);
            else
                cv_text_c(cx, 9, &AF_S, c, d == 1 || d == -1 ? T_SEC : d == 2 || d == -2 ? T_MID : T_DIM, T_BG);
        }
        cv_blit(0, 114);
    }
    cv_begin(240, 18, T_BG);                            /* ABC / 123 */
    cv_text_c(104, 3, &AF_X, "ABC", nm.num ? T_MID : T_THEME, T_BG);
    cv_text_c(136, 3, &AF_X, "123", nm.num ? T_THEME : T_MID, T_BG);
    cv_blit(0, 156);
    {                                                   /* the keys */
        int ok = !transport_busy();
        const char *go = nm.kind == NK_USER_SAVE || nm.kind == NK_PROJ_SAVE ? "Save" : "Rename";
        int32_t gw = text_w(&AF_S, go) + 30;
        cv_begin(240, 24, T_BG);
        cv_rrect(6, 2, 40, 20, 5, T_SURF, T_BG);
        cv_icon_mid(20, 12, 12, ICON_X_UNDO, T_MID, T_SURF);
        cv_rrect(234 - gw, 2, gw, 20, 5, ok ? T_THEME : T_SURF, T_BG);
        cv_icon_mid(234 - gw + 8, 12, 12, ICON_X_CHECK, ok ? T_INK : T_DIM, ok ? T_THEME : T_SURF);
        cv_text_on(234 - gw + 22, 4, &AF_S, go, ok ? T_INK : T_DIM, ok ? T_THEME : T_SURF);
        cv_blit(0, 212);
    }
}
static void name_draw(void)
{
    uint8_t st[7] = {nm.kind, nm.slot, nm.len, nm.cur, nm.num, nm.key, nm.tap};
    uint32_t sig = fnv(fnv(2166136261u, st, sizeof st), nm.s, sizeof nm.s) + ux.gen * 7919u +
                   (uint32_t)transport_busy() * 104729u;
    if (ui.force) {
        lcd_fill(0, H_HEAD, 240, 240 - H_HEAD, T_BG);
        nm.sig = ~sig;
    }
    draw_head();
    if (sig != nm.sig) {
        nm.sig = sig;
        nm_draw_name();
    }
    if (ui.msg_t && !--ui.msg_t && ui.msg2[0]) {
        str_cpy(ui.msg, ui.msg2, sizeof ui.msg);
        ui.msg2[0] = 0;
        ui.msg_t = 60;
    }
    if (ui.bpm_t)
        ui.bpm_t--;
    ui.force = 0;
}
