/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The few libc routines the compiler may call implicitly (struct copies,
 * zeroing). Freestanding: no other libc.
 * 3y50: a word at a time where both ends allow it (the pointers aligned to 4: the voice and mix buffers, the engines'
 * states and the struct copies are), byte by byte for the rest and for anything unaligned: the same bytes written,
 * in the same forward order of words (a copy whose two areas overlap is undefined, as in any libc; none here does).
 * Words are written through a may_alias type: no aliasing assumption about the caller's objects */
#include "cr_ram.h"                /* (CR_HOT_VA: a marker, nothing for the compiler) */
typedef unsigned int __attribute__((may_alias)) cr_w32;
CR_HOT_VA void *memset(void *d, int c, unsigned n)   /* (in RAM with va_render, which clears 48 bytes with it every
                                                     * voice and block; its other callers then come through a veneer) */
{
    unsigned char *p = d;
    if (!((unsigned)(unsigned long)p & 3u) && n >= 4u) {
        unsigned w = (unsigned char)c * 0x01010101u;
        cr_w32 *q = (cr_w32 *)p;
        for (; n >= 4u; n -= 4u)
            *q++ = w;
        p = (unsigned char *)q;
    }
    while (n--)
        *p++ = (unsigned char)c;
    return d;
}

void *memcpy(void *d, const void *s, unsigned n)
{
    unsigned char *p = d;
    const unsigned char *q = s;
    if (!(((unsigned)(unsigned long)p | (unsigned)(unsigned long)q) & 3u)) {
        cr_w32 *a = (cr_w32 *)p;
        const cr_w32 *b = (const cr_w32 *)q;
        for (; n >= 4u; n -= 4u)
            *a++ = *b++;
        p = (unsigned char *)a;
        q = (const unsigned char *)b;
    }
    while (n--)
        *p++ = *q++;
    return d;
}

int memcmp(const void *a, const void *b, unsigned n)
{
    const unsigned char *p = a, *q = b;
    if (!(((unsigned)(unsigned long)p | (unsigned)(unsigned long)q) & 3u))
        for (; n >= 4u && *(const cr_w32 *)p == *(const cr_w32 *)q; n -= 4u, p += 4, q += 4)
            ;                                       /* (equal words skipped; the first that differs: by its bytes) */
    for (; n; n--, p++, q++)
        if (*p != *q)
            return *p - *q;
    return 0;
}

static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static unsigned str_len(const char *s)
{
    unsigned n = 0;
    while (s[n])
        n++;
    return n;
}

static void str_cpy(char *d, const char *s, unsigned max)
{
    unsigned i = 0;
    while (s[i] && i + 1 < max) {
        d[i] = s[i];
        i++;
    }
    d[i] = 0;
}

/* decimal with an optional fixed point: fmt_fix(buf, -63, 1) -> "-6.3" */
static void fmt_fix(char *b, int32_t v, int decimals)
{
    char t[12];
    int n = 0, neg = v < 0, i;
    uint32_t u = (uint32_t)(neg ? -v : v);
    do {
        t[n++] = (char)('0' + u % 10u);
        u /= 10u;
        if (n == decimals)
            t[n++] = '.';
    } while (u || n <= decimals);
    if (neg)
        t[n++] = '-';
    for (i = 0; i < n; i++)
        b[i] = t[n - 1 - i];
    b[n] = 0;
}

static void fmt_int(char *b, int32_t v) { fmt_fix(b, v, 0); }

static uint32_t rng_state = 0x1234567u;
static uint32_t rng(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}
