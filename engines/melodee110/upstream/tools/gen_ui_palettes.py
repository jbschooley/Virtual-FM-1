#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""The UI palettes (run by tools/build.py generate()).

  gen_ui_palettes.py OUT.h [--report]

Melodee's palettes: NIGHT (the default: warm ink), DAY (light) and CONTRAST (black and white). A palette is BG
(background), SURF (surface: cards, dialogs, menu rows), TEXT, ACCENT (the one active thing: hot knob, cursor,
playhead) and four track colours: the selected track's is THEME (values, curves, gauges, selection), so the screen
takes the colour of the track ALGORITHM picks (src/gfx.c palette_track). The firmware derives the rest with fixed
blends (src/gfx.c, the same integer maths as mix() here):
  MID  = mix(BG, TEXT, 70 %)    labels, units, secondary text
  DIM  = mix(BG, TEXT, 42 %)    inactive, empty steps, disabled
  LINE = mix(BG, TEXT, 18 %)    1 px dividers, the faint gauge track (NIGHT: 10 %, the design's #2E2939; LINE_PCT)
  SEL  = mix(BG, THEME, 80 %)   selection fill and gauge fill
  TINT = mix(BG, THEME, 12 %)   a faint area (the selected drum lane)
  RAISE = mix(SURF, TEXT, 12 %) a raised area on a surface: stubs, guides, slots, chips, button wells
  KEY  = mix(BG, TEXT, 78 %)    a keycap's fill
  INK  = BG: text on a selection or THEME fill
Outside the palette: REC, a fixed red; the QR code's fixed black and white; the crash screen's fixed red.
GRAY (pure grayscale, R = G = B in every colour and tint) is compiled into the host tests only (UI_TEST_PALETTE):
every screen drawn in it must stay gray, which proves the drawing takes every colour from the tokens.
Saved settings name palettes by a tagged id (PAL_TAG + index); earlier ids map by UI_PALETTE_MIGRATE (Felucca's 20
palettes, ids 0..19) and UI_PALETTE_MIGRATE64 (Melodee 0.13's eight, ids 64..71).
--report prints the WCAG contrast of every pairing, with each track colour as THEME. The tool fails on any miss.
"""
import argparse
import sys
from pathlib import Path

# name, bg, surf, text, accent, (track 1, 2, 3, 4) (8-bit RGB; stored as RGB565)
PALETTES = [
    ("NIGHT",    (23, 20, 31),    (36, 32, 46),    (243, 238, 248), (255, 255, 255),
     ((255, 122, 92), (255, 193, 69), (70, 217, 176), (157, 140, 255))),
    ("DAY",      (246, 242, 234), (231, 224, 212), (30, 26, 38),    (20, 16, 28),
     ((160, 42, 22), (116, 68, 0), (0, 92, 72), (78, 56, 184))),
    ("CONTRAST", (0, 0, 0),       (34, 34, 34),    (255, 255, 255), (255, 255, 255),
     ((255, 128, 96), (255, 214, 0), (0, 236, 188), (176, 160, 255))),
]
TEST_GRAY = ("GRAY", (14, 14, 14), (36, 36, 36), (204, 204, 204), (255, 255, 255),
             ((232, 232, 232), (220, 220, 220), (208, 208, 208), (244, 244, 244)))
# Felucca's 20 palettes (ids 0..19) and Melodee 0.13's eight (ids 64..71) -> the new palette
OLD = ["GREEN", "AMBER", "CYAN", "RED", "MONO", "VIOLET", "PINK", "ICE", "WARM", "OCEAN", "DUSK", "HI-CON",
       "LIGHT", "PAPER", "SKY", "MINT", "LILAC", "ROSE", "SAND", "L-HICON"]
OLD_LIGHT = {"LIGHT", "PAPER", "SKY", "MINT", "LILAC", "ROSE", "SAND"}
OLD64 = ["MONO", "GREEN", "AMBER", "ICE", "VIOLET", "ROSE", "PAPER", "HI-CON"]


def old_to_new(name, light_rose):
    if name in ("HI-CON", "L-HICON"):
        return "CONTRAST"
    if name == "PAPER" or (light_rose and name in OLD_LIGHT):
        return "DAY"
    return "NIGHT"


PAL_TAG = 96                       # stored id = PAL_TAG + index (Melodee 0.13: 64 + its index)
REC_DARK, REC_LIGHT = (255, 72, 72), (190, 24, 40)
QR_LIGHT, QR_DARK, CRASH_BG, CRASH_INK = (255, 255, 255), (0, 0, 0), (160, 0, 0), (255, 255, 255)
PCT = {"MID": 70, "DIM": 42, "LINE": 18, "SEL": 80, "TINT": 12, "KEY": 78}
RAISE_PCT = 12                     # SURF -> TEXT
LINE_PCT = {"NIGHT": 10}           # a palette's own LINE (else PCT["LINE"]): NIGHT's dividers as faint as the design's


def to565(c):
    r, g, b = c
    if r == g == b:                     # a gray stays on the RGB565 gray axis (G = 2 R)
        return ((r >> 3) << 11) | ((r >> 3) << 6) | (r >> 3)
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def from565(v):
    return ((v >> 11) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)


def cdiv(a, b):
    return a // b if a >= 0 else -((-a) // b)


def mix(a, b, pct, mono=False):
    """src/gfx.c ux_mix: a + (b - a) * pct / 100 per channel, rounded; MONO on the 5-bit red channel"""
    if mono:
        x, y = a >> 11, b >> 11
        d = (y - x) * pct
        v = x + cdiv(d + (50 if d >= 0 else -50), 100)
        return (v << 11) | (v << 6) | v
    out = 0
    for sh, mask in ((11, 31), (5, 63), (0, 31)):
        x, y = (a >> sh) & mask, (b >> sh) & mask
        d = (y - x) * pct
        out |= (x + cdiv(d + (50 if d >= 0 else -50), 100)) << sh
    return out


def luma(c):
    """src/gfx.c ux_luma: 0..255, integer"""
    r, g, b = c >> 11, (c >> 5) & 63, c & 31
    return (r * 255 // 31 * 54 + g * 255 // 63 * 183 + b * 255 // 31 * 19) >> 8


def lum(c565):
    def lin(v):
        v /= 255.0
        return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
    r, g, b = from565(c565)
    return 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b)


def contrast(a, b):
    la, lb = lum(a), lum(b)
    return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)


def derive(p, t):
    """the derived colours of palette p with track t's colour as THEME"""
    name, bg, surf, text, accent, trk = p
    bg, surf, text, accent, theme = to565(bg), to565(surf), to565(text), to565(accent), to565(trk[t])
    mono = name == "GRAY"
    d = dict(bg=bg, surf=surf, text=text, theme=theme, accent=accent,
             mid=mix(bg, text, PCT["MID"], mono), dim=mix(bg, text, PCT["DIM"], mono),
             line=mix(bg, text, LINE_PCT.get(name, PCT["LINE"]), mono), sel=mix(bg, theme, PCT["SEL"], mono),
             tint=mix(bg, theme, PCT["TINT"], mono), raise_=mix(surf, text, RAISE_PCT, mono),
             key=mix(bg, text, PCT["KEY"], mono))
    d["ink"] = bg
    d["light"] = luma(bg) > 128
    d["rec"] = accent if mono else to565(REC_LIGHT if d["light"] else REC_DARK)
    return f"{name}/T{t + 1}", d


# (foreground, background, minimum contrast, why)
CHECKS = [("text", "bg", 12.0, "body text"), ("text", "surf", 9.0, "body text on a surface"),
          ("mid", "bg", 5.0, "labels"), ("mid", "surf", 4.0, "labels on a surface"),
          ("dim", "bg", 2.2, "inactive (not read)"), ("line", "bg", 1.25, "1 px dividers"),
          ("theme", "bg", 4.5, "values, curves"), ("theme", "surf", 4.5, "values on a surface"),
          ("accent", "bg", 4.5, "hot value, cursor, playhead"), ("accent", "surf", 4.5, "hot on a surface"),
          ("ink", "sel", 4.5, "text on a selection"), ("ink", "theme", 4.5, "text on a THEME fill"),
          ("sel", "bg", 1.8, "gauge fill vs track"), ("rec", "bg", 3.0, "REC (graphic)"),
          ("text", "raise_", 7.0, "text on a RAISE chip or button"), ("ink", "rec", 4.5, "text on the REC chip"),
          ("ink", "key", 4.5, "a keycap's label"), ("key", "bg", 3.0, "a keycap on the background"),
          ("key", "surf", 2.5, "a keycap on a surface"), ("ink", "dim", 1.8, "an unavailable keycap's label"),
          ("ink", "accent", 4.5, "text on the ARM badge")]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out", nargs="?")
    ap.add_argument("--report", action="store_true")
    a = ap.parse_args()
    fails = []
    assert PALETTES[0][0] == "NIGHT" and len(OLD) == 20 and len(OLD64) == 8 and len(PALETTES) <= 31
    rows = [derive(p, t) for p in PALETTES + [TEST_GRAY] for t in range(4)]
    for name, d in rows:
        if name.startswith("GRAY"):
            for k, v in d.items():
                if k == "light":
                    continue
                if not (v >> 11 == v & 31 and ((v >> 5) & 63) == (v >> 11) << 1):
                    fails.append(f"GRAY is not grayscale: {k} = {v:#06x}")
        for f, b, lim, why in CHECKS:
            c = contrast(d[f], d[b])
            if c < lim:
                fails.append(f"{name}: {f}/{b} {c:.2f} < {lim} ({why})")
    if a.report:
        print(f"{'palette':12s} " + " ".join(f"{f}/{b}".ljust(12) for f, b, _, _ in CHECKS))
        for name, d in rows:
            print(f"{name:12s} " + " ".join(f"{contrast(d[f], d[b]):5.1f}".ljust(12) for f, b, _, _ in CHECKS))
        print("minimum      " + " ".join(f"{lim:<12.2f}" for _, _, lim, _ in CHECKS))
    if a.out:
        names = [p[0] for p in PALETTES]

        def row(p):
            v = [to565(c) for c in p[1:5]]
            t = [to565(c) for c in p[5]]
            return (f'    {{"{p[0]}", ' + ", ".join(f"0x{x:04x}" for x in v) +
                    ", {" + ", ".join(f"0x{x:04x}" for x in t) + "}},")
        out = ["/* generated by tools/gen_ui_palettes.py: the UI palettes (4 colours and 4 track colours each, the rest",
               " * derived) */", "#pragma once", "#include <stdint.h>", "",
               "/* ui_pal_t {name, bg, surf, text, accent, trk[4]} (src/gfx.c); GRAY: host tests only */",
               "static const ui_pal_t UI_PALETTES[] = {"]
        out += [row(p) for p in PALETTES]
        out += ["#ifdef UI_TEST_PALETTE", row(TEST_GRAY), "#endif", "};",
                f"#define UI_NPALETTES {len(PALETTES)}u", "#define UI_DEFAULT_INDEX 0u",
                f"#define UI_GRAY_INDEX {len(PALETTES)}u      /* (UI_TEST_PALETTE only) */",
                f"#define UI_PAL_TAG {PAL_TAG}u          /* stored id = UI_PAL_TAG + index */"]
        out += [f"#define UI_{k}_PCT {v}" for k, v in PCT.items()] + [f"#define UI_RAISE_PCT {RAISE_PCT}   /* SURF -> TEXT */"]
        out += ["/* each palette's LINE (BG -> TEXT %), in UI_PALETTES' order */",
                "static const uint8_t UI_LINE_PCTS[] = {" +
                ", ".join(str(LINE_PCT.get(p[0], PCT["LINE"])) for p in PALETTES) + ",",
                "#ifdef UI_TEST_PALETTE", f"    {LINE_PCT.get(TEST_GRAY[0], PCT['LINE'])},", "#endif", "};"]
        out += [f"#define UI_REC_DARK 0x{to565(REC_DARK):04x}u", f"#define UI_REC_LIGHT 0x{to565(REC_LIGHT):04x}u",
                f"#define UI_QR_LIGHT 0x{to565(QR_LIGHT):04x}u", f"#define UI_QR_DARK 0x{to565(QR_DARK):04x}u",
                f"#define UI_CRASH_BG 0x{to565(CRASH_BG):04x}u", f"#define UI_CRASH_INK 0x{to565(CRASH_INK):04x}u", "",
                "/* Felucca's 20 palettes: " + " ".join(OLD) + " */",
                "static const uint8_t UI_PALETTE_MIGRATE[20] = {" +
                ", ".join(str(names.index(old_to_new(n, True))) for n in OLD) + "};",
                "/* Melodee 0.13's eight (stored 64 + index): " + " ".join(OLD64) + " */",
                "static const uint8_t UI_PALETTE_MIGRATE64[8] = {" +
                ", ".join(str(names.index(old_to_new(n, False))) for n in OLD64) + "};", ""]
        Path(a.out).write_text("\n".join(out))
    for f in fails:
        print("FAIL", f)
    if fails:
        sys.exit(1)


if __name__ == "__main__":
    main()
