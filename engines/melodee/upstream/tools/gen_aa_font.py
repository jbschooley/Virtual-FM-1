#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
# Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Pre-rasterise TTF fonts into anti-aliased 4-bit alpha glyph tables for the UI (run by
tools/build.py generate() with --preset rubik).

  gen_aa_font.py OUT.h --preset rubik [--tracking EM] [--gamma G] [--kern-min N]
  gen_aa_font.py OUT.h --face NAME=FONT[#index]:PX:FIRST-LAST[+0xNN,...] ...   (explicit faces)

The preset's faces, the large type's roles (docs/design/README.md "Large type"):
  rubik       assets/fonts/Rubik[wght].ttf (SIL OFL 1.1, Google Fonts), Melodee's face: Rubik 500 at 16 / 20 / 28 / 48
              px (LB labels, TX text, VL values, HR the hero; VL and HR one phase, HR the characters a value or a name
              needs), and the same roles in Melodee Pixel (assets/fonts/melodee_pixel.txt: a 5 x 7 1-bit face drawn at
              2 / 2 / 3 / 5 times, AP_*), each on its Rubik role's line height and baseline so a layout holds in both.

Output (a C header; the glyph / font types live in the renderer, firmware/src/gfx.c):
  AF_<N>_DATA   4-bit alpha, trimmed to the ink box, rows back to back, each glyph a byte-aligned bit stream
                (MSB first) of canonical Huffman codes (AF_<N>_HC); a symbol is a value and a repeat count:
                a run of 1..16 zeros or 15s, or one value 1..14 (huff_pack; about 28 % less than 2 px per byte)
  AF_<N>_HC     the code: the number of codes of each length 1..15, then the symbols in code order
                ((count - 1) << 4 | value)
  AF_<N>_G      aag_t {off, adv (1/16 px), bx, by, bw, bh} per glyph and phase (PHASES of LB TX, one of VL HR:
                the glyph drawn at a pen of +0, +1/phases .. px, next to each other), ASCII 32..126, then the
                extras (Latin-1 by their code; the ellipsis U+2026 as code 0x85, as in Windows-1252)
  AF_KM, AF_KL / _KR  kerning (GPOS of the weight), one table for every face: the pairs at KERN_PX in 1/16 px as
                classes (each glyph's left and right class, 0 = none; the matrix [(left - 1) * right classes +
                right - 1]); a face scales them by its ks (Q8: px * 256 / KERN_PX). A face whose glyphs are in
                another order has its own AF_<N>_KL / _KR
  AF_<N>        aafont_t (its last fields: log2 of the phases, the Huffman code, the capitals' height, 0 = not a pixel face,
                the kerning's scale)
The alpha is stored in LEVELS steps (4 of the 16: 0, 5, 10, 15; a third less data than all 16).
  AP_G, AP_DATA, AP_<role>  Melodee Pixel (preset rubik): one 1-bit table, a face per role (px: its multiple)
Prints the byte cost of every face.
"""
import argparse
import heapq
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import aa_raster as ar  # noqa: E402

FONTS = Path(__file__).resolve().parents[1] / "fonts"
EXTRAS = [0xA9, 0xB0, 0xB7, 0xC4, 0xD6, 0xDC, 0xE4, 0xF6, 0xFC, 0x2026]  # (c) deg . A: O: U: a: o: u: (Latin-1), ...
CODE = {0x2026: 0x85}                  # characters outside Latin-1 -> their one-byte code in the firmware
PHASES = 2                             # horizontal phases per glyph (aa_raster.raster_font): LB and TX
LEVELS = [4]                           # alpha steps kept of the 16 (--levels)
KERN_PX = 28                           # the shared kerning's size (VL's: exact there, within 1/16 px at 16 / 20 px)
# faces stored Huffman-coded (--huff): all four (the pixel faces are 1 bit and need no code)
HUFF = [("LB", "TX", "VL", "HR")]
# HR (the hero, 48 px) draws a value or a name: every letter, the digits and the signs names and values use (the
# factory names' ' ( ) too); the space as its range and the rest as extras. A string with another character is drawn
# in VL (gfx.c text_has)
HR_CHARS = " #'()+./-_0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz%&*,:<>"
PIXEL = Path(__file__).resolve().parents[1] / "assets" / "fonts" / "melodee_pixel.txt"


def preset(name):
    if name == "rubik":
        v = str(FONTS.parent / "assets" / "fonts" / "Rubik[wght].ttf")       # SIL OFL 1.1, Google Fonts
        return [("LB", v + "@500", 16, (32, 126), EXTRAS, 0.0, PHASES),             # the large type's roles
                ("TX", v + "@500", 20, (32, 126), EXTRAS, 0.0, PHASES),
                ("VL", v + "@500", 28, (32, 126), EXTRAS, 0.0, 1),
                ("HR", v + "@500", 48, (32, 32), [ord(c) for c in HR_CHARS[1:]], 0.0, 1),
                ("PIX", str(PIXEL), 0, (32, 126), EXTRAS, None, 1, [("LB", 2, 1), ("TX", 2, 0), ("VL", 3, 0), ("HR", 5, 0)])]
    raise SystemExit(f"unknown preset {name}")


def parse_face(s):
    name, _, rest = s.partition("=")
    font, px, rng = rest.rsplit(":", 2)
    extras = []
    if "+" in rng:
        rng, ex = rng.split("+", 1)
        extras = [int(x, 0) for x in ex.split(",") if x]
    a, b = rng.split("-")
    return (name, font, int(px), (int(a), int(b)), extras, None, PHASES)


HC_MAXLEN = 15


def huff_symbols(v):
    """a glyph's alpha values -> symbols (count - 1) << 4 | value: runs of 1..16 zeros or 15s, single others"""
    out, i = [], 0
    while i < len(v):
        j = i + 1
        if v[i] in (0, 15):
            while j < len(v) and v[j] == v[i] and j - i < 16:
                j += 1
        out.append((j - i - 1) << 4 | v[i])
        i = j
    return out


def huff_lengths(count):
    """symbol -> code length (Huffman), at most HC_MAXLEN"""
    heap = [(c, k, (s,)) for k, (s, c) in enumerate(sorted(count.items()))]
    heapq.heapify(heap)
    length = {s: 0 for s in count}
    if len(heap) == 1:
        return {s: 1 for s in count}
    k = len(heap)
    while len(heap) > 1:
        a, b = heapq.heappop(heap), heapq.heappop(heap)
        for s in a[2] + b[2]:
            length[s] += 1
        k += 1
        heapq.heappush(heap, (a[0] + b[0], k, a[2] + b[2]))
    assert max(length.values()) <= HC_MAXLEN, "code too long: limit the lengths"
    return length


def huff_pack(glyph_values):
    """[alpha values per glyph] -> (hc table bytes, [bytes per glyph]): canonical Huffman, MSB first"""
    syms = [huff_symbols(v) for v in glyph_values]
    count = {}
    for g in syms:
        for s in g:
            count[s] = count.get(s, 0) + 1
    length = huff_lengths(count)
    order = sorted(count, key=lambda s: (length[s], s))
    code, c, prev = {}, 0, length[order[0]]
    for s in order:                                    # canonical: by length, then symbol
        c <<= length[s] - prev
        prev = length[s]
        code[s] = c
        c += 1
    hc = [sum(1 for s in order if length[s] == n) for n in range(1, HC_MAXLEN + 1)] + order
    out = []
    for g in syms:
        acc, nb, b = 0, 0, bytearray()
        for s in g:
            acc, nb = (acc << length[s]) | code[s], nb + length[s]
            while nb >= 8:
                nb -= 8
                b.append((acc >> nb) & 255)
        if nb:
            b.append((acc << (8 - nb)) & 255)
        out.append(bytes(b))
    return hc, out


def huff_unpack(hc, data, n):
    """the decoder (gfx.c cv_alpha_hc), for the self-check: n values from the bit stream data"""
    out, pos = [], 0
    while len(out) < n:
        code = first = idx = 0
        for ln in range(HC_MAXLEN):
            code |= (data[pos >> 3] >> (7 - (pos & 7))) & 1
            pos += 1
            if code - first < hc[ln]:
                break
            idx += hc[ln]
            first = (first + hc[ln]) << 1
            code <<= 1
        s = hc[HC_MAXLEN + idx + code - first]
        out += [s & 15] * ((s >> 4) + 1)
    assert len(out) == n
    return out


def cap_px(font, px):
    """the capitals' height in px (OS/2 sCapHeight): gfx.c centres a pill's or a row's capitals with it"""
    from fontTools.ttLib import TTFont
    f = TTFont(font.split("#")[0].split("@")[0])
    return round(f["OS/2"].sCapHeight * px / f["head"].unitsPerEm)


def quantise(b, n):
    """packed nibbles (2 px per byte) -> the same with each alpha rounded to LEVELS steps of 0..15"""
    lv = LEVELS[0] - 1
    q = [round(round(v * lv / 15) * 15 / lv) for byte in b for v in (byte >> 4, byte & 15)][:n]
    return ar.pack(q)


def shared_kern(faces, kern_min):
    """the kerning of every face as one table (KERN_PX): (C text, {ch: left class}, {ch: right class}, right classes,
    the order of AF_KL / AF_KR, bytes)"""
    fonts = {f[1] for f in faces}
    assert len(fonts) == 1, "one kerning table: every face of one font and weight"
    order = []
    for f in sorted(faces, key=lambda f: -(f[3][1] - f[3][0] + 1 + len(f[4]))):
        for c in [chr(c) for c in range(f[3][0], f[3][1] + 1)] + [chr(c) for c in f[4]]:
            if c not in order:
                order.append(c)
    pairs = {(a, b): d for (a, b), d in ar.kern_pairs(fonts.pop(), KERN_PX, order, True, kern_min).items()
             if ord(a) < 256 and ord(b) < 256 and d}
    lc, rc, rows, cols = {}, {}, {}, {}
    for a in order:
        row = tuple(pairs.get((a, b), 0) for b in order)
        if any(row):
            lc[a] = rows.setdefault(row, len(rows) + 1)
    for b in order:
        col = tuple(pairs.get((a, b), 0) for a in order)
        if any(col):
            rc[b] = cols.setdefault(col, len(cols) + 1)
    km = [0] * (len(rows) * len(cols))
    for (a, b), d in pairs.items():
        km[(lc[a] - 1) * len(cols) + rc[b] - 1] = d
    for a in order:
        for b in order:
            got = km[(lc[a] - 1) * len(cols) + rc[b] - 1] if a in lc and b in rc else 0
            assert got == pairs.get((a, b), 0), f"kerning classes lose {a!r}{b!r}"
    assert all(-128 <= d < 128 for d in km)
    text = "\n".join([f"/* kerning: {len(pairs)} pairs at {KERN_PX} px in 1/16 px, {len(rows)} x {len(cols)} classes */",
                      ar.c_array("AF_KM", "int8_t", km, 24),
                      ar.c_array("AF_KL", "uint8_t", [lc.get(c, 0) for c in order], 24),
                      ar.c_array("AF_KR", "uint8_t", [rc.get(c, 0) for c in order], 24)]) + "\n"
    return text, lc, rc, len(cols), order, len(km) + 2 * len(order), len(pairs)


def emit(face, tracking, gamma, kern):
    name, font, px, (first, last), extras, ftrack, phases, *lift = face
    lift = lift[0] if lift else 0              # rows the glyphs move up in the line box (and off its baseline, height)
    chars = [chr(c) for c in range(first, last + 1)] + [chr(c) for c in extras]
    r = ar.raster_font(font, px, chars, tracking if ftrack is None else ftrack, gamma, True, 0, phases)
    for ch, (adv16, ph) in r["glyphs"].items():   # (the alpha in LEVELS steps)
        r["glyphs"][ch] = (adv16, [(bx, by, bw, bh, quantise(b, bw * bh) if bh else b) for bx, by, bw, bh, b in ph])
    if lift:
        for ch, (adv16, ph) in r["glyphs"].items():
            assert all(by >= lift for bx, by, bw, bh, b in ph if bh), f"{name} {ch!r}: no room above to lift it"
            r["glyphs"][ch] = (adv16, [(bx, by - lift if bh else by, bw, bh, b) for bx, by, bw, bh, b in ph])
        r["asc"] -= lift
        r["h"] -= lift
    data, table, lines = bytearray(), [], []
    for ch in chars:
        adv16, ph = r["glyphs"][ch]
        off = len(data)
        for bx, by, bw, bh, b in ph:
            table.append((len(data), adv16, bx, by, bw, bh))
            data += b
        table += [(off,) + table[-1][1:]] * (phases - len(ph))   # one phase (aa_raster.FIXED): the same bitmap
    raw, hc = bytes(data), []
    if name in HUFF[0]:
        # Huffman-code every distinct bitmap (phases may share one), byte-aligned per glyph
        offs = sorted({e[0] for e in table if e[4]})
        vals = []
        for off in offs:
            e = next(t for t in table if t[0] == off and t[4])
            v = []
            for byte in raw[off:off + (e[4] * e[5] + 1) // 2]:
                v += [byte >> 4, byte & 15]
            vals.append(v[:e[4] * e[5]])
        hc, packed = huff_pack(vals)
        data, newoff = bytearray(), {}
        for off, v, b in zip(offs, vals, packed):
            assert huff_unpack(hc, b, len(v)) == v
            newoff[off] = len(data)
            data += b
        table = [(newoff.get(off, 0),) + t[1:] for t in table for off in (t[0],)]
        data += bytes(2)                               # gfx.c cv_alpha_hc reads up to 2 bytes ahead
    assert len(data) < 65536, "glyph data over 64 KiB: split the face"
    # kerning: the shared table's classes (shared_kern), this face's order of them
    kl = [kern[0].get(c, 0) for c in chars] if kern else []
    kr = [kern[1].get(c, 0) for c in chars] if kern else []
    own = kern and chars != kern[3][:len(chars)]       # (in another order than AF_KL / AF_KR: its own)
    psh = phases.bit_length() - 1
    lines.append(f"/* {name}: {os.path.basename(font.split('#')[0])} {px}px, h {r['h']}, baseline {r['asc']}, "
                 f"{len(chars)} glyphs x {phases} phases, {len(data)} B data"
                 + (f" (Huffman; {len(raw)} B as nibbles) */" if hc else " */"))
    lines.append(ar.c_array(f"AF_{name}_DATA", "uint8_t", list(data), 24, "0x{:02x}"))
    if hc:
        lines.append(ar.c_array(f"AF_{name}_HC", "uint8_t", hc, 24))
    lines.append(f"static const aag_t AF_{name}_G[{len(table)}] = {{")
    for off, adv, bx, by, bw, bh in table:
        lines.append(f"    {{{off}, {adv}, {bx}, {by}, {bw}, {bh}}},")
    lines.append("};")
    if own:
        lines.append(ar.c_array(f"AF_{name}_KL", "uint8_t", kl, 24))
        lines.append(ar.c_array(f"AF_{name}_KR", "uint8_t", kr, 24))
    ex = ", ".join(str(CODE.get(c, c)) for c in extras) or "0"
    lines.append(f"static const uint8_t AF_{name}_EX[{max(1, len(extras))}] = {{{ex}}};")
    kt = (f"AF_{name}_KL, AF_{name}_KR, AF_KM" if own else "AF_KL, AF_KR, AF_KM") if kern else "0, 0, 0"
    lines.append(f"static const aafont_t AF_{name} = {{{r['h']}, {r['asc']}, {first}, {last}, {len(extras)}, "
                 f"{kern[2] if kern else 0}, AF_{name}_G, AF_{name}_DATA, {kt}, AF_{name}_EX, {psh}, "
                 f"{'AF_%s_HC' % name if hc else '0'}, {cap_px(font, px)}, 0, {round(px * 256 / KERN_PX)}}};")
    kbytes = 2 * len(chars) if own else 0
    pairs = [(a, b) for a in chars for b in chars if kern and a in kern[0] and b in kern[1]]
    cost = {"data": len(data) + len(hc), "glyph_table": 8 * len(table), "kern": kbytes, "extras": len(extras),
            "glyphs": len(chars), "phases": phases, "pairs": len(pairs), "h": r["h"], "asc": r["asc"], "px": px,
            "font": os.path.basename(font.split("#")[0]), "adv_digit": r["glyphs"]["0"][0] / 16}
    cost["total"] = cost["data"] + cost["glyph_table"] + cost["kern"] + cost["extras"]
    return "\n".join(lines) + "\n", cost


def pixel_glyphs(path, narrow=False):
    """assets/fonts/melodee_pixel.txt -> {char: (top row, [rows])}: its glyphs, or (narrow) its condensed set"""
    glyphs, cur = {}, None
    for line in Path(path).read_text().splitlines():
        if line.startswith("# ") or line == "#!":     # (a comment; a glyph's rows hold no space)
            continue
        if line.startswith("glyph U+"):
            w = line.split()
            top = int(w[w.index("top") + 1]) if "top" in w else 0
            cur = glyphs.setdefault(chr(int(w[1][2:], 16)), (top, [])) if ("narrow" in w) == narrow else None
        elif line.strip() and cur is not None:
            assert set(line) <= {"#", "."}, line
            cur[1].append(line.split()[0])
        else:
            cur = None
    for ch, (top, rows) in glyphs.items():
        assert rows and all(len(r) == len(rows[0]) for r in rows), f"pixel glyph {ch!r}: ragged rows"
        assert top - 2 >= -4 and top + len(rows) <= 9, f"pixel glyph {ch!r} leaves the 11-row box"
    return glyphs


def emit_pixel(face, metrics):
    """Melodee Pixel: 1-bit glyph tables (AP_G; AP_GN the condensed set's: its glyphs where it has them, else the
    regular ones), their bitmaps (AP_DATA), and a face per role, drawn at a whole multiple (aafont_t px) on its Rubik
    role's line height and baseline (roles: (role, multiple, condensed)). A glyph's box starts two font rows above the
    capital's top (gfx.c cv_pix: row by of the box at y + asc - 9 px + by px); adv in 1/16 font px, times px drawn"""
    name, font, _, (first, last), extras, _, _, roles = face
    g, gn = pixel_glyphs(font), pixel_glyphs(font, True)
    chars = [chr(c) for c in range(first, last + 1)] + [chr(c) for c in extras]
    missing = [c for c in chars if c not in g]
    assert not missing, f"melodee_pixel.txt lacks {missing}"
    data, made = bytearray(), {}

    def entry(top, rows):
        key = (top, tuple(rows))
        if key in made:
            return made[key]
        lit = [r for r, row in enumerate(rows) if "#" in row]
        w = len(rows[0])
        if not lit:
            made[key] = (0, (w + 1) * 16, 0, 0, 0, 0)
            return made[key]
        r0, r1 = lit[0], lit[-1] + 1
        cols = [c for c in range(w) if any(rows[r][c] == "#" for r in range(r0, r1))]
        c0, c1 = cols[0], cols[-1] + 1
        bits = [rows[r][c] == "#" for r in range(r0, r1) for c in range(c0, c1)]
        off = len(data)
        for i in range(0, len(bits), 8):
            data.append(sum(1 << (7 - k) for k, b in enumerate(bits[i:i + 8]) if b))
        made[key] = (off, (w + 1) * 16, c0, top + r0 + 2, c1 - c0, r1 - r0)
        return made[key]

    table = [entry(*g[ch]) for ch in chars]
    ntable = [entry(*(gn.get(ch) or g[ch])) for ch in chars]
    assert len(data) < 65536
    lines = [f"/* Melodee Pixel ({os.path.basename(font)}): {len(chars)} glyphs (+ {len(gn)} condensed), 1 bit a pixel, "
             f"{len(data)} B; drawn at a whole multiple per role */",
             ar.c_array("AP_DATA", "uint8_t", list(data), 24, "0x{:02x}")]
    for tname, t in (("AP_G", table), ("AP_GN", ntable)):
        lines.append(f"static const aag_t {tname}[{len(t)}] = {{")
        lines += [f"    {{{off}, {adv}, {bx}, {by}, {bw}, {bh}}}," for off, adv, bx, by, bw, bh in t]
        lines.append("};")
    ex = ", ".join(str(CODE.get(c, c)) for c in extras) or "0"
    lines.append(f"static const uint8_t AP_EX[{max(1, len(extras))}] = {{{ex}}};")
    for role, scale, narrow in roles:
        h, asc = metrics[role]
        assert asc >= 7 * scale and h >= asc + 2 * scale, f"pixel {role}: {scale}x does not fit {h}/{asc}"
        lines.append(f"static const aafont_t AP_{role} = {{{h}, {asc}, {first}, {last}, {len(extras)}, 0, "
                     f"{'AP_GN' if narrow else 'AP_G'}, AP_DATA, 0, 0, 0, AP_EX, 0, 0, {7 * scale}, {scale}}};")
    cost = len(data) + 16 * len(table) + len(extras)
    return "\n".join(lines) + "\n", cost, len(chars)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out")
    ap.add_argument("--preset", choices=["rubik"])
    ap.add_argument("--face", action="append", default=[])
    ap.add_argument("--tracking", type=float, default=0.0, help="extra advance per glyph in em")
    ap.add_argument("--gamma", type=float, default=1.0, help="coverage curve; <1 thickens light-on-dark text")
    ap.add_argument("--kern-min", type=int, default=1, help="smallest kerning pair kept, in 1/16 px (0 pairs = no kerning)")
    ap.add_argument("--huff", default=",".join(HUFF[0]), help="faces stored Huffman-coded (comma list, '' = none)")
    ap.add_argument("--levels", type=int, default=LEVELS[0], help="alpha steps kept (2..16)")
    a = ap.parse_args()
    HUFF[0] = tuple(x for x in a.huff.split(",") if x)
    LEVELS[0] = max(2, min(16, a.levels))
    faces = ([parse_face(s) for s in a.face] if a.face else []) or (preset(a.preset) if a.preset else None)
    if not faces:
        ap.error("--preset or --face is required")
    out = ["/* generated by tools/gen_aa_font.py: 4-bit alpha glyphs (Rubik, SIL OFL 1.1; Melodee Pixel; see "
           "assets/fonts) */",
           "#pragma once", "#include <stdint.h>", ""]
    total, metrics, kern = 0, {}, None
    aa = [f for f in faces if f[0] != "PIX"]
    if aa and a.kern_min:
        text, *kern = shared_kern(aa, a.kern_min)
        out.append(text)
        total += kern[4]
        print(f"kerning: {kern[5]} pairs at {KERN_PX} px, shared = {kern[4]} B")
    for f in faces:
        if f[0] == "PIX":
            text, cost, n = emit_pixel(f, metrics)
            out.append(text)
            total += cost
            print(f"face PIX: {os.path.basename(f[1])} {n} glyphs, 1 bit, roles {[r[:2] for r in f[7]]} = {cost} B")
            continue
        text, c = emit(f, a.tracking, a.gamma, kern)
        metrics[f[0]] = (c["h"], c["asc"])
        out.append(text)
        total += c["total"]
        print(f"face {f[0]}: {c['font']} {c['px']}px h{c['h']} base{c['asc']}  {c['glyphs']} glyphs x {c['phases']}  "
              f"data {c['data']} B + table {c['glyph_table']} B + kern classes {c['kern']} B "
              f"= {c['total']} B   digit adv {c['adv_digit']:.2f}px")
    Path(a.out).write_text("\n".join(out))
    print(f"fonts total {total} B  -> {a.out}")


if __name__ == "__main__":
    main()
