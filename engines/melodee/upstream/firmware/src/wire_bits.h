/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#ifndef MELODEE_WIRE_BITS_H
#define MELODEE_WIRE_BITS_H
#include <stdint.h>

/* Little-endian fields, up to 32 bits. Callers provide enough storage and
 * zero the destination before packing: writes OR into the existing bytes.
 * Only 32-bit shifts, including fields that cross byte boundaries. */
static void wire_bits_put(uint8_t *b, uint32_t *pos, uint32_t v, uint32_t n)
{
    while (n--) {
        b[*pos >> 3] |= (uint8_t)((v & 1u) << (*pos & 7u));
        v >>= 1;
        ++*pos;
    }
}

static uint32_t wire_bits_get(const uint8_t *b, uint32_t *pos, uint32_t n)
{
    uint32_t v = 0;
    for (uint32_t i = 0; i < n; i++, ++*pos)
        v |= (uint32_t)((b[*pos >> 3] >> (*pos & 7u)) & 1u) << i;
    return v;
}
#endif
