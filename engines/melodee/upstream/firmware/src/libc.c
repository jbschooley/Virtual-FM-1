/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The few libc routines the compiler may call implicitly (struct copies,
 * zeroing). Freestanding: no other libc. */
#if defined(__GNUC__) && !defined(__attribute__)
/* may_alias preserves memcpy/memset semantics for float/struct objects.
 * Host synth fixtures strip attributes; their scalar path remains valid.
 * The standalone libc test exercises this exact aligned target path. */
#define MELODEE_LIBC_WORDS 1
typedef uint32_t libc_word_t __attribute__((may_alias));
#else
#define MELODEE_LIBC_WORDS 0
#endif
void *memset(void *d, int c, unsigned n)
{
    unsigned char *p = d;
#if MELODEE_LIBC_WORDS
    while(n && ((uintptr_t)p&3u)){*p++=(unsigned char)c;n--;}
    libc_word_t *w=(libc_word_t *)p;uint32_t value=(uint32_t)(unsigned char)c*0x01010101u;
    while(n>=4u){*w++=value;n-=4u;}p=(unsigned char *)w;
#endif
    while (n--)
        *p++ = (unsigned char)c;
    return d;
}

void *memcpy(void *d, const void *s, unsigned n)
{
    unsigned char *p = d;
    const unsigned char *q = s;
#if MELODEE_LIBC_WORDS
    if(!(((uintptr_t)p^(uintptr_t)q)&3u)){
        while(n && ((uintptr_t)p&3u)){*p++=*q++;n--;}
        libc_word_t *w=(libc_word_t *)p;const libc_word_t *v=(const libc_word_t *)q;
        while(n>=4u){*w++=*v++;n-=4u;}p=(unsigned char *)w;q=(const unsigned char *)v;
    }
#endif
    while (n--)
        *p++ = *q++;
    return d;
}

int memcmp(const void *a, const void *b, unsigned n)
{
    const unsigned char *p = a, *q = b;
#if MELODEE_LIBC_WORDS
    if(!(((uintptr_t)p|(uintptr_t)q)&3u)){
        while(n>=4u && *(const libc_word_t *)p==*(const libc_word_t *)q){p+=4u;q+=4u;n-=4u;}
    }
#endif
    for (; n; n--, p++, q++)
        if (*p != *q)
            return *p - *q;
    return 0;
}

/* Seeded FNV-1a over bytes, shared by history and persistent checksums. */
static uint32_t fnv(uint32_t h, const void *p, uint32_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    while (n--)
        h = (h ^ *b++) * 16777619u;
    return h;
}

static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int str_prefix(const char *s,const char *prefix)
{
    while(*prefix)if(*s++!=*prefix++)return 0;
    return 1;
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
