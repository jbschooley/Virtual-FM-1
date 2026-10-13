/* SPDX-License-Identifier: GPL-3.0-only */
/* The sound browser (the PRESETS knob, SAVE > PRESETS): one list of every sound, in ENGINE_ORDER each engine's factory
 * presets and then its native user slots, then the general user presets. LIST (KNOB 4 of PRESETS) shows all of it, the
 * favourites (FAV, favorites.filter as before), the sounds loaded lately (RECENT, kept in RAM) or one category.
 * Categories follow Felucca 1.4's category.c (Leo Kuroshita, Hügelton Instruments): its numbers 1..8, then Melodee's.
 * Factory presets have a letter each below; native and user slots take the category of a factory sound of their
 * engine with the same name, else the words in their name, cached until a user bank changes (up_gen).
 * A turn moves a pending place in the list, drawn at once; its sound loads when the knob rests (BROWSE_REST_MS) or at
 * once for a slow detent, so a fast turn does not load every sound it passes. Included by ui.c. */
static void preset_go(uint32_t n);

enum { CAT_NONE, CAT_BASS, CAT_LEAD, CAT_PAD, CAT_PLUCK, CAT_KEYS, CAT_DRUM, CAT_FX, CAT_OTHER,
       CAT_ORGAN, CAT_STRING, CAT_BRASS, CAT_WIND, CAT_BELL, CAT_N };
static const char *const CAT_NAME[CAT_N] = {"-", "BASS", "LEAD", "PAD", "PLUCK", "KEYS", "DRUM", "FX", "OTHER",
                                            "ORGAN", "STRING", "BRASS", "WIND", "BELL"};
static const char CAT_LETTER[CAT_N + 1u] = "?BLPUKDFOGSRWE";   /* a letter's index is its CAT_* */
static const uint8_t CAT_ORDER[] = {CAT_BASS, CAT_LEAD, CAT_PAD, CAT_KEYS, CAT_ORGAN, CAT_STRING, CAT_BRASS, CAT_WIND,
                                    CAT_PLUCK, CAT_BELL, CAT_DRUM, CAT_FX, CAT_OTHER};   /* LIST's order */

/* the factory presets' categories, a letter per preset in its engine's order; tests/ui_test.c checks each string's
 * length against its engine's preset count. An engine without one: cat_of_engine */
static const char *const PRESET_CAT[NENGINES] = {
#if MELODEE_FM4
    [ENGI_DIGITAL] = "KEBRGPEK",                        /* DIGITAL (MELODEE_FM4 builds): E.PIANO .. FUNK KEY */
#endif
#if MELODEE_LEGACY_EXTRAS                                /* (retired: PHASE VOICE NOISE) */
    [2] = "RGSLEU",                                     /* PHASE: BRASS ORGAN STRING RESO BELL WIRE */
    [5] = "PLBF",                                       /* VOICE: CHOIR AAH, VOX LEAD, WOW BASS, WHISPER */
    [11] = "FFFF",                                      /* NOISE */
#endif
    [3] = "OLBBBBBBLLLLLLLLPSPPGGKKUUERFFFF",          /* SID */
    [10] = "DD",                                        /* DRUM: the kits */
    [12] = "KEBRPEGUKRBEEKGSPLUEWEBEO",                 /* FM6, INIT VOICE */
    [ENGI_CZ] = "ORRRSSSSSUUUBBBBURWSWWWWUKKKKKKKKGGGGGPPPEEEEEDEESSULLLLFDDDEFFFF",   /* INIT TONE, Casio's 64 */
    [ENGI_PROPHET] =                                    /* INIT, Sequential's 200 */
        "OOOKBPROOOGKLPWPOLOUUBEOEGFPOOBOEOOODDDEOOGKSGBPEPPLBROOOOPOKPKOFOSOOEPRODFLFOOEOULWOPBOLOPKFORGLPOBULROOSKOO"
        "PSBUEFDEEEWEUOUEOOOOSERPUOBOOOBOFLGUGRLOOOBUFOOFEUFORSKKWKLGLGGKROGBGLKSLOOFOFOFLUUEFFEOFEPF",
};
static uint32_t cat_of_engine(uint32_t e) { return e == ENGI_DRUM ? CAT_DRUM : e == 11u ? CAT_FX : CAT_OTHER; }
static uint32_t preset_cat(uint32_t e, uint32_t k)           /* factory preset k of engine e */
{
    const char *s = e < NENGINES ? PRESET_CAT[e] : 0;
    uint32_t c;
    if (!s || !s[0] || k >= ENGINES[e]->npresets)
        return cat_of_engine(e);
    for (c = 1; c < CAT_N && CAT_LETTER[c] != s[k]; c++)
        ;
    return c < CAT_N ? c : cat_of_engine(e);
}

/* a name's category from its words: the first rule whose word starts a word of the name ('|': and ends it) */
static const struct { uint8_t cat; const char *w; } CAT_WORDS[] = {
    {CAT_KEYS, "PIANO"}, {CAT_KEYS, "EP|"}, {CAT_KEYS, "E.P"}, {CAT_KEYS, "TINE"}, {CAT_KEYS, "WURL"},
    {CAT_KEYS, "CLAV"}, {CAT_KEYS, "HARPSI"}, {CAT_KEYS, "KEYS"}, {CAT_KEYS, "HONKY"}, {CAT_KEYS, "RHODES"},
    {CAT_ORGAN, "ORGAN"}, {CAT_ORGAN, "DRAWBAR"}, {CAT_ORGAN, "HARMONIUM"}, {CAT_ORGAN, "ACCORDION"},
    {CAT_ORGAN, "LESLIE"}, {CAT_ORGAN, "CHURCH"},
    {CAT_BELL, "BELL"}, {CAT_BELL, "STEEL DRUM"}, {CAT_BELL, "TUNED"}, {CAT_BELL, "MARIMBA"}, {CAT_BELL, "XYLO"},
    {CAT_BELL, "VIBRAPHONE"}, {CAT_BELL, "VIBES"}, {CAT_BELL, "MALLET"}, {CAT_BELL, "KALIMBA"}, {CAT_BELL, "GAMELAN"},
    {CAT_BELL, "MUSIC BOX"}, {CAT_BELL, "TUBULAR"}, {CAT_BELL, "GONG"}, {CAT_BELL, "CHIME"}, {CAT_BELL, "CARILLON"},
    {CAT_BELL, "GLOCK"}, {CAT_BELL, "CELESTA"},
    {CAT_DRUM, "DRUM"}, {CAT_DRUM, "KIT|"}, {CAT_DRUM, "PERC"}, {CAT_DRUM, "CONGA"}, {CAT_DRUM, "BONGO"},
    {CAT_DRUM, "TAIKO"}, {CAT_DRUM, "HAT"}, {CAT_DRUM, "TIMPAN"}, {CAT_DRUM, "TOM|"}, {CAT_DRUM, "SNARE"},
    {CAT_DRUM, "KICK"}, {CAT_DRUM, "CLAP"}, {CAT_DRUM, "808"}, {CAT_DRUM, "909"},
    {CAT_BASS, "BASS"}, {CAT_BASS, "SUB|"},
    {CAT_LEAD, "LEAD"}, {CAT_LEAD, "LD|"}, {CAT_LEAD, "SOLO"}, {CAT_LEAD, "SYNC"}, {CAT_LEAD, "GLIDE"},
    {CAT_PAD, "PAD"}, {CAT_PAD, "CHOIR"}, {CAT_PAD, "CHORAL"}, {CAT_PAD, "VOICE"}, {CAT_PAD, "VOCAL"},
    {CAT_PAD, "VOX"}, {CAT_PAD, "AAH"}, {CAT_PAD, "SWELL"}, {CAT_PAD, "DREAM"}, {CAT_PAD, "DRONE"}, {CAT_PAD, "ATMOS"},
    {CAT_STRING, "STRING"}, {CAT_STRING, "CELLO"}, {CAT_STRING, "VIOL"}, {CAT_STRING, "ORCHESTRA"},
    {CAT_STRING, "ENSEMBLE"},
    {CAT_BRASS, "BRASS"}, {CAT_BRASS, "HORN"}, {CAT_BRASS, "TRUMPET"}, {CAT_BRASS, "TROMBONE"}, {CAT_BRASS, "TUBA"},
    {CAT_BRASS, "STAB"},
    {CAT_WIND, "FLUTE"}, {CAT_WIND, "SAX"}, {CAT_WIND, "CLARINET"}, {CAT_WIND, "REED"}, {CAT_WIND, "RECORDER"},
    {CAT_WIND, "HARMONICA"}, {CAT_WIND, "WHISTLE"}, {CAT_WIND, "OBOE"}, {CAT_WIND, "WINDS"},
    {CAT_PLUCK, "PLUCK"}, {CAT_PLUCK, "GUITAR"}, {CAT_PLUCK, "GTR"}, {CAT_PLUCK, "HARP|"}, {CAT_PLUCK, "KOTO"},
    {CAT_PLUCK, "SITAR"}, {CAT_PLUCK, "NYLON"}, {CAT_PLUCK, "PIZZ"}, {CAT_PLUCK, "ARP"}, {CAT_PLUCK, "BANJO"},
    {CAT_FX, "NOISE"}, {CAT_FX, "SWEEP"}, {CAT_FX, "ALIEN"}, {CAT_FX, "SIREN"}, {CAT_FX, "LASER"}, {CAT_FX, "RISER"},
    {CAT_FX, "FX"}, {CAT_FX, "SFX"}, {CAT_FX, "WIND|"}, {CAT_FX, "RAIN"}, {CAT_FX, "JET|"}, {CAT_FX, "ROBOT"},
};
static uint32_t up_ch(char c) { return (uint32_t)(c >= 'a' && c <= 'z' ? c - 32 : c); }
static int word_ch(char c)
{
    uint32_t u = up_ch(c);
    return (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9');
}
static int name_has_word(const char *nm, const char *w)
{
    uint32_t i, j;
    for (i = 0; nm[i]; i++) {
        if (i && word_ch(nm[i - 1u]))
            continue;
        for (j = 0; w[j] && w[j] != '|' && nm[i + j] && up_ch(nm[i + j]) == (uint32_t)w[j]; j++)
            ;
        if (w[j] == '|' ? !word_ch(nm[i + j]) : !w[j])
            return 1;
    }
    return 0;
}
static int name_same(const char *a, const char *b)           /* the first 12 characters, any case */
{
    uint32_t i;
    for (i = 0; i < 12u && (a[i] || b[i]); i++)
        if (up_ch(a[i]) != up_ch(b[i]))
            return 0;
    return 1;
}
static uint32_t cat_guess(uint32_t e, const char *nm)
{
    const engine_t *en = ENGINES[e % NENGINES];
    uint32_t k;
    if (e == ENGI_DRUM || e == 11u)
        return cat_of_engine(e);
    for (k = 0; k < en->npresets; k++)
        if (name_same(en->presets[k].name, nm))
            return preset_cat(e % NENGINES, k);
    for (k = 0; k < NELEM(CAT_WORDS); k++)
        if (name_has_word(nm, CAT_WORDS[k].w))
            return CAT_WORDS[k].cat;
    return CAT_OTHER;
}

/* list entries: src is the engine of a factory preset, else USER_NATIVE_P5 / _FM / _CZ or USER_GENERAL; k the preset
 * or the slot. The slots' categories: a nibble each (0: not worked out yet), dropped when up_gen moves */
#define BRW_P5 128u                                    /* (native_presets.c, prophet_user.c, core.h assert these) */
#define BRW_FM 64u
#define BRW_CZ 128u
#define BRW_DYN (BRW_P5 + BRW_FM + BRW_CZ + UP_SLOTS)
static uint8_t cat_dyn[BRW_DYN / 2u];
static uint32_t cat_dyn_gen = 0xFFFFFFFFu;
static int src_slot(uint32_t src)
{
    return src == USER_GENERAL || src == USER_NATIVE_P5 || src == USER_NATIVE_FM || src == USER_NATIVE_CZ;
}
static uint32_t src_engine(uint32_t src, uint32_t k)       /* the engine that plays entry src, k */
{
    return src == USER_NATIVE_P5 ? ENGI_PROPHET : src == USER_NATIVE_FM ? ENGI_FM6 : src == USER_NATIVE_CZ ? ENGI_CZ :
           src == USER_GENERAL ? up_engine(k) % NENGINES : src % NENGINES;
}
static uint32_t entry_cat(uint32_t src, uint32_t k)
{
    uint32_t i, c;
    char nm[16];
    if (!src_slot(src))
        return preset_cat(src % NENGINES, k);
    i = src == USER_NATIVE_P5 ? k : src == USER_NATIVE_FM ? BRW_P5 + k : src == USER_NATIVE_CZ ? BRW_P5 + BRW_FM + k :
        BRW_P5 + BRW_FM + BRW_CZ + k;
    if (i >= BRW_DYN)
        return CAT_OTHER;
    if (cat_dyn_gen != up_gen) {
        memset(cat_dyn, 0, sizeof cat_dyn);
        cat_dyn_gen = up_gen;
    }
    c = (cat_dyn[i / 2u] >> (i % 2u * 4u)) & 15u;
    if (!c) {
        if (src == USER_GENERAL)
            up_name(k, nm);
        else
            native_name(src_engine(src, k), k, nm);
        c = cat_guess(src_engine(src, k), nm);
        cat_dyn[i / 2u] |= (uint8_t)(c << (i % 2u * 4u));
    }
    return c;
}
static int entry_ok(uint32_t src, uint32_t k)              /* (a RECENT entry may have been erased since) */
{
    if (src == USER_GENERAL)
        return up_used(k);
    if (src_slot(src))
        return native_used(src_engine(src, k), k);
    return src < NENGINES && eng_ok(src) && k < ENGINES[src]->npresets;
}

/* the entry the selected track plays (USER_GENERAL, UP_SLOTS: none of the list's) */
static void cur_entry(uint32_t *src, uint32_t *k)
{
    uint32_t u = user_of(TSEL), e = TSEL->eng_req % NENGINES, np = ENGINES[e]->npresets;
    if (u < USER_NONE && TSEL->user_native) {
        *src = e == ENGI_PROPHET ? USER_NATIVE_P5 : e == ENGI_FM6 ? USER_NATIVE_FM : USER_NATIVE_CZ;
        *k = u;
    } else if (u < UP_SLOTS) {
        *src = USER_GENERAL;
        *k = u;
    } else {
        *src = e;
        *k = np ? TSEL->preset % np : 0u;
    }
}

/* RECENT: the sounds loaded by browsing, newest first (src << 8 | k; RAM only) */
#define RECENT_N 8u
static uint16_t recent[RECENT_N];
static uint8_t recent_n;
static void recent_push(uint32_t src, uint32_t k)
{
    uint16_t v = (uint16_t)(src << 8 | (k & 0xFFu));
    uint32_t i, j = recent_n < RECENT_N ? recent_n : RECENT_N - 1u;
    for (i = 0; i < recent_n; i++)
        if (recent[i] == v) {
            j = i;
            break;
        }
    if (j == recent_n)
        recent_n++;
    for (; j; j--)
        recent[j] = recent[j - 1u];
    recent[0] = v;
}

/* LIST: 0 ALL, 1 FAV, 2 RECENT, then a category (CAT_ORDER). The category is kept with the settings in a byte no
 * engine uses (lcat: CAT_*, 0 none; earlier firmware ignores it and keeps showing ALL / FAV as favorites.filter says,
 * as do the editor and backups); RECENT is not kept */
#define list_lcat (favorites.factory[15][29])
enum { LM_ALL, LM_FAV, LM_RECENT, LM_CAT };
#define LM_N (LM_CAT + NELEM(CAT_ORDER))
static uint8_t list_recent;
static struct { uint8_t on, trk, mode, x; uint16_t n; uint32_t t; } brw;   /* the pending place: list index n; x the
                                                                             * turn's acceleration (the browser's badge) */
static uint32_t list_mode(void)
{
    uint32_t i;
    if (favorites.filter)
        return LM_FAV;
    if (list_recent)
        return LM_RECENT;
    for (i = 0; i < NELEM(CAT_ORDER); i++)
        if (CAT_ORDER[i] == list_lcat)
            return LM_CAT + i;
    return LM_ALL;
}
static void list_set(uint32_t m)
{
    favorites.filter = m == LM_FAV;
    list_recent = m == LM_RECENT;
    list_lcat = (uint8_t)(m >= LM_CAT && m < LM_N ? CAT_ORDER[m - LM_CAT] : 0u);
    brw.on = 0;
}
static const char *list_name(uint32_t m)
{
    return m == LM_ALL ? "ALL" : m == LM_FAV ? "FAV" : m == LM_RECENT ? "RECENT" :
           CAT_NAME[m - LM_CAT < NELEM(CAT_ORDER) ? CAT_ORDER[m - LM_CAT] : CAT_OTHER];
}
static int list_keep(uint32_t m, uint32_t src, uint32_t k)
{
    return m == LM_ALL ? 1 : m == LM_FAV ? favorite_has(src, k) :
           m >= LM_CAT && m < LM_N ? entry_cat(src, k) == CAT_ORDER[m - LM_CAT] : 0;
}

/* one pass over the list LIST keeps: *total its length; the place of entry (ws, wk) (total: not in it); entry n in
 * *as, *ak (USER_GENERAL, UP_SLOTS past the end). The segments: 2 per engine in ENGINE_ORDER (factory, native), then
 * the user presets */
#define LSEG_USER (2u * NENG_SHOWN)
static uint32_t lseg_src(uint32_t seg)
{
    uint32_t e;
    if (seg >= LSEG_USER)
        return USER_GENERAL;
    e = eng_vis(seg / 2u);
    return !(seg & 1u) ? e : e == ENGI_PROPHET ? USER_NATIVE_P5 : e == ENGI_FM6 ? USER_NATIVE_FM : USER_NATIVE_CZ;
}
static uint32_t lseg_len(uint32_t seg)
{
    uint32_t e;
    if (seg >= LSEG_USER)
        return UP_SLOTS;
    e = eng_vis(seg / 2u);
    return seg & 1u ? native_limit(e) : preset_shown(e);
}
static uint32_t list_scan(uint32_t m, uint32_t ws, uint32_t wk, uint32_t n, uint32_t *as, uint32_t *ak,
                          uint32_t *total)
{
    uint32_t seg, src, k, i, c = 0, pos = 0xFFFFFFFFu;
    *as = USER_GENERAL;
    *ak = UP_SLOTS;
    if (m == LM_RECENT) {
        for (i = 0; i < recent_n; i++) {
            src = recent[i] >> 8;
            k = recent[i] & 0xFFu;
            if (!entry_ok(src, k))
                continue;
            if (src == ws && k == wk)
                pos = c;
            if (c == n)
                *as = src, *ak = k;
            c++;
        }
    } else {
        for (seg = 0; seg <= LSEG_USER; seg++) {
            uint32_t len = lseg_len(seg), slot = seg >= LSEG_USER || (seg & 1u), init;
            src = lseg_src(seg);
            init = slot || !len ? 0u : preset_init(src);   /* (an engine's INIT first: FM6's comes after F24) */
            for (i = 0; i < len; i++) {
                k = (i + init) % len;
                if (slot && !(src == USER_GENERAL ? up_used(k) : native_used(src_engine(src, k), k)))
                    continue;
                if (!list_keep(m, src, k))
                    continue;
                if (src == ws && k == wk)
                    pos = c;
                if (c == n)
                    *as = src, *ak = k;
                c++;
            }
        }
    }
    *total = c;
    return pos == 0xFFFFFFFFu ? c : pos;
}
static uint32_t list_cur(uint32_t *total)                   /* the loaded sound's place (total: not in the list) */
{
    uint32_t s, k, as, ak;
    cur_entry(&s, &k);
    return list_scan(list_mode(), s, k, 0xFFFFFFFFu, &as, &ak, total);
}
/* the whole list (ALL), whatever LIST shows: the loaded sound's place, entry n */
static uint32_t preset_all_pos(uint32_t *total)
{
    uint32_t s, k, as, ak;
    cur_entry(&s, &k);
    return list_scan(LM_ALL, s, k, 0xFFFFFFFFu, &as, &ak, total);
}
static uint32_t preset_all_at(uint32_t n, uint32_t *k)
{
    uint32_t total, src;
    list_scan(LM_ALL, 0xFFFFFFFFu, 0, n, &src, k, &total);
    return src;
}
static int browse_pending(void) { return brw.on && brw.trk == song.sel && brw.mode == list_mode(); }
static void recent_loaded(void)                            /* after a browsing load: the sound joins RECENT */
{
    uint32_t src, k;
    if (list_mode() == LM_RECENT)                           /* (browsing RECENT does not reorder it under the knob) */
        return;
    cur_entry(&src, &k);
    recent_push(src, k);
}

/* the place the list shows: the pending one while browsing, else the loaded sound's */
static uint32_t preset_pos(uint32_t *total)
{
    uint32_t cur = list_cur(total);
    return browse_pending() && brw.n < *total ? brw.n : cur;
}
static uint32_t preset_at(uint32_t n, uint32_t *k)         /* list entry n: its src, *k */
{
    uint32_t total, src;
    list_scan(list_mode(), 0xFFFFFFFFu, 0, n, &src, k, &total);
    return src;
}
/* the entry the PRESETS page and HOME show as the sound: the pending one, else the loaded one */
static uint32_t browse_shown(uint32_t *k)
{
    uint32_t src;
    if (browse_pending())
        return preset_at(brw.n, k);
    cur_entry(&src, k);
    return src;
}
static void browse_commit(void)                             /* load the pending sound now */
{
    uint32_t total, n = brw.n, cur;
    if (!brw.on)
        return;
    brw.on = 0;
    if (brw.trk != song.sel || brw.mode != list_mode())
        return;
    cur = list_cur(&total);
    if (n < total && n != cur)
        preset_go(n);
    ui.force = 1;
}
#define BROWSE_REST_MS 120u
static void browse_poll(void)
{
    if (brw.on && fm1_ms - brw.t >= BROWSE_REST_MS)
        browse_commit();
}

/* a turn of the knob role by s detents: the list moves by its acceleration (list_accel), wrapping at its ends on a
 * slow single detent as before; a slow detent loads at once, a fast turn waits for the knob to rest */
static int32_t list_accel(uint32_t role, int32_t s, uint32_t total, uint32_t *fast);
static void browse_turn(uint32_t role, int32_t s)
{
    uint32_t total, loaded, cur, n, fast, m = list_mode();
    int32_t d;
    if (brw.on && !browse_pending())
        brw.on = 0;
    loaded = list_cur(&total);
    if (!total) {
        ui_message(m == LM_FAV ? "NO FAVORITES" : m == LM_RECENT ? "NO RECENT SOUNDS" : "NO SOUNDS IN LIST");
        return;
    }
    d = list_accel(role, s, total, &fast);
    brw.x = (uint8_t)clamp((d < 0 ? -d : d) / (s < 0 ? -s : s), 1, 99);
    cur = brw.on && brw.n < total ? brw.n : loaded;
    if (cur >= total)                                       /* the sound is not in the list: its first or last */
        n = d > 0 ? 0u : total - 1u;
    else if (!fast && (d == 1 || d == -1))
        n = (cur + (d > 0 ? 1u : total - 1u)) % total;
    else
        n = (uint32_t)clamp((int32_t)cur + d, 0, (int32_t)total - 1);
    if (fast) {
        brw.on = 1;
        brw.trk = song.sel;
        brw.mode = (uint8_t)m;
        brw.n = (uint16_t)n;
        brw.t = fm1_ms;
        return;
    }
    brw.on = 0;
    if (n != loaded)
        preset_go(n);
}
