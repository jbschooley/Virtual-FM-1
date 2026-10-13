#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Melodee's icons, drawn here (no icon font): 4-bit alpha cells for the UI (run by tools/build.py generate()).

  gen_icons.py OUT.h [--sizes 12,16,24] [--sheet PNG]

Every icon is a few strokes and fills on a 24-unit grid (DRAW below), round caps and joins, the stroke 2 units at
24 px and never under 1.15 px, rasterised 8x and box-filtered. The enum keeps the names the firmware uses
(assets/icons.json, then X_*: the UI's own), so ICON_<NAME> ids are unchanged; a name without a drawing is not
rasterised at any size (cv_icon_on draws nothing for it). Parameter icons are off in Melodee (icons.c
MELODEE_ICONS 0): only the few the screens still draw by name have drawings.

Output (as tools/gen_aa_icons.py's, src/icons.c reads it): per size S
  AI<S>_DATA   cells of S x S px, 4-bit alpha, 2 px per byte, rows back to back
  AI<S>_IDX    the cell of every icon at that size, 0xFF = none (12 px: every drawing; 16 / 24 px: BIG / HUGE)
--sheet writes a PNG specimen of every drawing at every size (white on the NIGHT background).
"""
import argparse
import json
import math
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, str(Path(__file__).resolve().parent))
import aa_raster as ar  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
RUBIK = ROOT / "assets" / "fonts" / "Rubik[wght].ttf"
SS = 8                                   # supersampling

# the UI's own icons, after the legacy names (their order is the enum's: keep it, append new ones at the end)
EXTRA = ["x_play", "x_stop", "x_pause", "x_rec", "x_rec_o", "x_play_o", "x_trk1", "x_trk2", "x_trk3", "x_trk4",
         "x_trk1_o", "x_trk2_o", "x_trk3_o", "x_trk4_o", "x_usb", "x_star", "x_star_o", "x_check", "x_cog", "x_lock",
         "x_power", "x_folder", "x_doc", "x_plus", "x_minus", "x_undo", "x_redo", "x_warn", "x_palette", "x_speaker",
         "x_info", "x_back", "x_down", "x_up", "x_left", "x_right", "x_hugelton", "x_mixer", "x_pattern", "x_song",
         "x_motion", "x_motion_rec", "x_motion_del", "x_doctor", "x_bat0", "x_bat1", "x_bat3", "x_bat4", "x_bat_chg",
         "x_fx", "x_timer", "x_hpf", "x_repeat", "x_reverse", "x_tstop", "x_freeze", "x_oct_up", "x_oct_dn"]
BIG = ["x_play", "x_stop", "x_pause", "x_rec", "x_rec_o", "x_trk1", "x_trk2", "x_trk3", "x_trk4", "x_trk1_o",
       "x_trk2_o", "x_trk3_o", "x_trk4_o", "x_usb", "x_star", "x_star_o", "x_check", "x_cog", "x_power", "x_warn",
       "x_palette", "x_speaker", "x_info", "x_doc", "tempo", "x_song", "x_motion_del", "x_doctor", "x_fx", "x_timer",
       "x_bat0", "x_bat1", "x_bat3", "x_bat4", "x_bat_chg", "mute"]
HUGE = ["x_bat0", "x_bat1", "x_bat3", "x_bat4", "x_bat_chg", "x_usb", "x_repeat", "x_reverse", "cutoff", "x_hpf",
        "x_tstop", "x_freeze", "x_oct_up", "x_oct_dn"]


class Pen:
    """strokes and fills on the 24-unit grid of one cell; ink 255, erase 0"""

    def __init__(self, px):
        self.n = px * SS
        self.k = self.n / 24.0
        self.w = max(1.15 * SS, 2.0 * self.k * px / 24.0)       # stroke width in supersampled px
        self.img = Image.new("L", (self.n, self.n), 0)
        self.d = ImageDraw.Draw(self.img)

    def p(self, x, y):
        return (x * self.k, y * self.k)

    def line(self, *pts, w=1.0, ink=255):
        q = [self.p(x, y) for x, y in pts]
        sw = self.w * w
        self.d.line(q, fill=ink, width=max(1, round(sw)), joint="curve")
        for x, y in q:                                          # round caps
            self.d.ellipse((x - sw / 2, y - sw / 2, x + sw / 2, y + sw / 2), fill=ink)

    def poly(self, *pts, ink=255):
        self.d.polygon([self.p(x, y) for x, y in pts], fill=ink)

    def loop(self, *pts, w=1.0):
        self.line(*pts, pts[0], w=w)

    def disc(self, cx, cy, r, ink=255):
        x, y = self.p(cx, cy)
        r *= self.k
        self.d.ellipse((x - r, y - r, x + r, y + r), fill=ink)

    def ring(self, cx, cy, r, w=1.0):
        x, y = self.p(cx, cy)
        r *= self.k
        sw = self.w * w
        self.d.ellipse((x - r - sw / 2, y - r - sw / 2, x + r + sw / 2, y + r + sw / 2), fill=255)
        self.d.ellipse((x - r + sw / 2, y - r + sw / 2, x + r - sw / 2, y + r - sw / 2), fill=0)

    def arc(self, cx, cy, r, a0, a1, w=1.0, steps=24):
        pts = [(cx + r * math.cos(math.radians(a0 + (a1 - a0) * i / steps)),
                cy + r * math.sin(math.radians(a0 + (a1 - a0) * i / steps))) for i in range(steps + 1)]
        self.line(*pts, w=w)

    def box(self, x0, y0, x1, y1, r=0.0, ink=255):
        self.d.rounded_rectangle((*self.p(x0, y0), *self.p(x1, y1)), radius=r * self.k, fill=ink)

    def frame(self, x0, y0, x1, y1, r=0.0, w=1.0):
        sw = self.w * w
        a, b = self.p(x0, y0), self.p(x1, y1)
        self.d.rounded_rectangle((a[0] - sw / 2, a[1] - sw / 2, b[0] + sw / 2, b[1] + sw / 2),
                                 radius=r * self.k + sw / 2, fill=255)
        self.d.rounded_rectangle((a[0] + sw / 2, a[1] + sw / 2, b[0] - sw / 2, b[1] - sw / 2),
                                 radius=max(0.0, r * self.k - sw / 2), fill=0)

    def digit(self, ch, ink):
        font = ImageFont.truetype(str(RUBIK), int(self.n * 0.62))
        font.set_variation_by_axes([600])
        self.d.text((self.n / 2, self.n / 2 + self.n * 0.02), ch, font=font, fill=ink, anchor="mm")


def arrow(pn, x0, y0, x1, y1, head=4.0, w=1.0):
    pn.line((x0, y0), (x1, y1), w=w)
    a = math.atan2(y1 - y0, x1 - x0)
    for s in (2.5, -2.5):
        pn.line((x1, y1), (x1 - head * math.cos(a + s / 3.6), y1 - head * math.sin(a + s / 3.6)), w=w)


def star(pn, fill):
    pts = []
    for i in range(10):
        r = 9.0 if i % 2 == 0 else 4.0
        a = math.radians(-90 + i * 36)
        pts.append((12 + r * math.cos(a), 12.6 + r * math.sin(a)))
    if fill:
        pn.poly(*pts)
        pn.loop(*pts, w=0.6)
    else:
        pn.loop(*pts, w=0.9)


def battery(pn, level, charging=False):
    pn.frame(3, 8, 18.5, 16, r=2.0)
    pn.box(19.5, 10.5, 21.5, 13.5, r=0.8)
    if level:
        pn.box(5.5, 10.5, 5.5 + 10.5 * level, 13.5, r=0.8)
    if charging:
        pn.poly((12, 6.5), (8, 12.5), (11, 12.5), (10, 17.5), (14.5, 11), (11.5, 11))


def trk(pn, n, filled):
    if filled:
        pn.box(3, 3, 21, 21, r=5)
        pn.digit(str(n), 0)
    else:
        pn.frame(4, 4, 20, 20, r=4.5, w=0.9)
        pn.digit(str(n), 255)


def wave(pn, x0, x1, y, amp, cycles, w=1.0):
    pts = [(x0 + (x1 - x0) * i / 40, y - amp * math.sin(2 * math.pi * cycles * i / 40)) for i in range(41)]
    pn.line(*pts, w=w)


DRAW = {
    "x_play": lambda p: (p.poly((7, 4.5), (19.5, 12), (7, 19.5)), p.loop((7, 4.5), (19.5, 12), (7, 19.5), w=0.8)),
    "x_play_o": lambda p: p.loop((7.5, 5), (19, 12), (7.5, 19)),
    "x_stop": lambda p: p.box(6, 6, 18, 18, r=2.5),
    "x_pause": lambda p: (p.box(6.5, 5.5, 10, 18.5, r=1.2), p.box(14, 5.5, 17.5, 18.5, r=1.2)),
    "x_rec": lambda p: p.disc(12, 12, 7),
    "x_rec_o": lambda p: p.ring(12, 12, 6.5),
    "x_trk1": lambda p: trk(p, 1, True), "x_trk2": lambda p: trk(p, 2, True),
    "x_trk3": lambda p: trk(p, 3, True), "x_trk4": lambda p: trk(p, 4, True),
    "x_trk1_o": lambda p: trk(p, 1, False), "x_trk2_o": lambda p: trk(p, 2, False),
    "x_trk3_o": lambda p: trk(p, 3, False), "x_trk4_o": lambda p: trk(p, 4, False),
    "x_usb": lambda p: (p.frame(3.5, 8.5, 20.5, 15.5, r=3.5), p.line((8, 12), (16, 12), w=0.9)),
    "x_star": lambda p: star(p, True),
    "x_star_o": lambda p: star(p, False),
    "x_check": lambda p: p.line((5, 12.5), (10, 17.5), (19, 7)),
    "x_cog": lambda p: ([p.line((12 + 6.5 * math.cos(math.radians(a)), 12 + 6.5 * math.sin(math.radians(a))),
                                (12 + 9.5 * math.cos(math.radians(a)), 12 + 9.5 * math.sin(math.radians(a))), w=1.2)
                         for a in range(0, 360, 45)], p.ring(12, 12, 5.5), p.disc(12, 12, 1.6)),
    "x_lock": lambda p: (p.arc(12, 10, 4.5, 180, 360), p.line((7.5, 10), (7.5, 11)), p.line((16.5, 10), (16.5, 11)),
                         p.box(5.5, 11, 18.5, 20, r=2)),
    "x_power": lambda p: (p.arc(12, 13, 7, -55, 235), p.line((12, 4), (12, 11))),
    "x_folder": lambda p: p.loop((4, 7), (10, 7), (12, 9), (20, 9), (20, 18), (4, 18)),
    "x_doc": lambda p: (p.loop((6, 4), (14, 4), (18, 8), (18, 20), (6, 20)), p.line((14, 4), (14, 8), (18, 8)),
                        p.line((9, 13), (15, 13), w=0.8), p.line((9, 16.5), (14, 16.5), w=0.8)),
    "x_plus": lambda p: (p.line((12, 5), (12, 19)), p.line((5, 12), (19, 12))),
    "x_minus": lambda p: p.line((5, 12), (19, 12)),
    "x_undo": lambda p: (p.arc(13, 13, 6, -90, 150), arrow(p, 13.5, 7, 6.5, 7, 0)),
    "x_redo": lambda p: (p.arc(11, 13, 6, 30, 270), arrow(p, 10.5, 7, 17.5, 7, 0)),
    "x_warn": lambda p: (p.loop((12, 4), (21, 19.5), (3, 19.5)), p.line((12, 9.5), (12, 13.5)), p.disc(12, 16.7, 1.3)),
    "x_palette": lambda p: (p.ring(12, 12, 8), p.disc(9, 9, 1.8), p.disc(15, 9, 1.8), p.disc(9, 15, 1.8),
                            p.disc(15, 15, 1.8)),
    "x_speaker": lambda p: (p.poly((4, 9.5), (8, 9.5), (13, 5), (13, 19), (8, 14.5), (4, 14.5)),
                            p.arc(13, 12, 4, -45, 45), p.arc(13, 12, 7.5, -50, 50)),
    "x_info": lambda p: (p.ring(12, 12, 8.5), p.line((12, 11), (12, 16.5)), p.disc(12, 7.8, 1.3)),
    "x_back": lambda p: (p.line((5, 10), (14, 10)), p.arc(14, 14, 4, -90, 90), p.line((14, 18), (10, 18)),
                         p.line((5, 10), (9, 6)), p.line((5, 10), (9, 14))),
    "x_down": lambda p: p.line((6, 9.5), (12, 15.5), (18, 9.5)),
    "x_up": lambda p: p.line((6, 14.5), (12, 8.5), (18, 14.5)),
    "x_left": lambda p: p.line((14.5, 6), (8.5, 12), (14.5, 18)),
    "x_right": lambda p: p.line((9.5, 6), (15.5, 12), (9.5, 18)),
    "x_mixer": lambda p: ([p.line((x, 4.5), (x, 19.5), w=0.8) for x in (6, 12, 18)],
                          [p.box(x - 2.5, y - 1.5, x + 2.5, y + 1.5, r=1) for x, y in ((6, 14), (12, 8), (18, 12))]),
    "x_pattern": lambda p: [p.box(4 + 6 * i, 4 + 6 * j, 8.5 + 6 * i, 8.5 + 6 * j, r=1.2) for i in range(3)
                            for j in range(3)],
    "x_song": lambda p: [p.line((x0, y), (x1, y), w=1.4) for x0, x1, y in ((4, 13, 6.5), (8, 20, 12), (4, 16, 17.5))],
    "x_motion": lambda p: (wave(p, 3, 21, 12, 5, 1.0), p.disc(21, 12, 1.4)),
    "x_motion_rec": lambda p: (wave(p, 3, 15, 12, 5, 0.75), p.disc(18.5, 12, 3)),
    "x_motion_del": lambda p: (wave(p, 3, 14, 12, 5, 0.75), p.line((16, 8.5), (21, 15.5)), p.line((21, 8.5), (16, 15.5))),
    "x_doctor": lambda p: (p.ring(12, 12, 6.5), p.line((12, 2.5), (12, 8)), p.line((12, 16), (12, 21.5)),
                           p.line((2.5, 12), (8, 12)), p.line((16, 12), (21.5, 12))),
    "x_bat0": lambda p: battery(p, 0), "x_bat1": lambda p: battery(p, 0.3),
    "x_bat3": lambda p: battery(p, 0.7), "x_bat4": lambda p: battery(p, 1.0),
    "x_bat_chg": lambda p: (battery(p, 0), p.box(8, 8.8, 14, 15.2, ink=0), battery(p, 0, True)),
    "x_fx": lambda p: (p.poly((12, 3), (14, 10), (21, 12), (14, 14), (12, 21), (10, 14), (3, 12), (10, 10)),),
    "x_timer": lambda p: (p.ring(12, 13.5, 7.5), p.line((12, 13.5), (12, 9)), p.line((10, 3.5), (14, 3.5)),
                          p.line((12, 3.5), (12, 6))),
    "x_hpf": lambda p: p.line((3, 19), (7, 12), (11, 7.5), (21, 7)),
    "cutoff": lambda p: p.line((3, 7), (13, 7.5), (17, 12), (21, 19)),
    "x_repeat": lambda p: (p.arc(12, 12, 7, -160, 160), arrow(p, 4.6, 9.6, 4.8, 13.5, 3.5)),
    "x_reverse": lambda p: (p.poly((11.5, 6), (11.5, 18), (4, 12)), p.poly((20, 6), (20, 18), (12.5, 12))),
    "x_tstop": lambda p: (p.line((3, 7), (8, 8), (13, 11), (17, 15.5), (21, 20)), p.box(15, 4, 21, 10, r=1.4)),
    "x_freeze": lambda p: [p.line((12 + 9 * math.cos(math.radians(a)), 12 + 9 * math.sin(math.radians(a))),
                                  (12 - 9 * math.cos(math.radians(a)), 12 - 9 * math.sin(math.radians(a))), w=0.9)
                           for a in (90, 30, -30)],
    "x_oct_up": lambda p: (arrow(p, 12, 20, 12, 5), p.line((6, 20), (18, 20), w=0.8)),
    "x_oct_dn": lambda p: (arrow(p, 12, 4, 12, 19), p.line((6, 4), (18, 4), w=0.8)),
    # the parameter icons the screens still draw by name
    "generic": lambda p: (p.ring(12, 12, 7.5), p.line((12, 12), (12, 5))),
    "tempo": lambda p: (p.loop((8, 20), (16, 20), (13.5, 4), (10.5, 4)), p.line((12, 16), (17, 8))),
    "mute": lambda p: (p.poly((3.5, 9.5), (7, 9.5), (11.5, 5.5), (11.5, 18.5), (7, 14.5), (3.5, 14.5)),
                       p.line((15, 9), (21, 15)), p.line((21, 9), (15, 15))),
    "pitch": lambda p: (arrow(p, 8, 19, 8, 5, 3.5), arrow(p, 16, 5, 16, 19, 3.5)),
    "gate": lambda p: p.line((3, 18), (7, 18), (7, 6), (15, 6), (15, 18), (21, 18)),
    "slide": lambda p: (p.line((5, 18), (19, 6)), p.disc(5, 18, 2), p.disc(19, 6, 2)),
    "slice": lambda p: [p.line((x, 20 - h), (x, 20)) for x, h in ((5, 12), (9.5, 16), (14.5, 9), (19, 14))],
    "prob": lambda p: (p.frame(4, 4, 20, 20, r=4), p.disc(8.5, 8.5, 1.5), p.disc(12, 12, 1.5), p.disc(15.5, 15.5, 1.5)),
    "mix": lambda p: (p.ring(9, 12, 5.5, w=0.9), p.ring(15, 12, 5.5, w=0.9)),
    "feedback": lambda p: (p.arc(12, 12, 7, -60, 240), arrow(p, 15.6, 6.1, 17.5, 8.9, 3.5)),
    "delay": lambda p: (p.disc(5, 12, 3), p.disc(12, 12, 2.2), p.disc(18, 12, 1.4)),
    "bits": lambda p: p.line((3, 19), (7, 19), (7, 14), (12, 14), (12, 9), (17, 9), (17, 5), (21, 5)),
    "accent": lambda p: p.line((6, 6), (18, 12), (6, 18)),
}


def raster(name, px):
    pn = Pen(px)
    DRAW[name](pn)
    img = pn.img.reduce(SS)
    return ar.pack([ar.alpha4(v) for v in img.tobytes()]), img


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out")
    ap.add_argument("--json", default=str(ROOT / "assets" / "icons.json"), help="legacy icon names (order = enum)")
    ap.add_argument("--sizes", default="12,16,24")
    ap.add_argument("--sheet")
    a = ap.parse_args()
    legacy = json.loads(Path(a.json).read_text())["names"]
    names = list(legacy) + EXTRA
    unknown = [n for n in list(DRAW) + BIG + HUGE if n not in names]
    if unknown:
        raise SystemExit(f"icons: not in the enum: {unknown}")
    sizes = [int(s) for s in a.sizes.split(",")]
    out = ["/* generated by tools/gen_icons.py: Melodee's icons, drawn there (no icon font) */",
           "#pragma once", "#include <stdint.h>", "enum {"]
    out += [f"    ICON_{n.upper()}," for n in names]
    out += ["    ICON_COUNT", "};", ""]
    tot, sheet = 0, []
    for s in sizes:
        subset = [n for n in names if n in DRAW and (s == min(sizes) or n in (HUGE if s == 24 else BIG))]
        data, idx = [], [0xFF] * len(names)
        for n in subset:
            b, img = raster(n, s)
            assert img.getbbox(), f"{n} is empty at {s}px"
            idx[names.index(n)] = len(data)
            data.append(b)
            sheet.append((s, n, img))
        flat = [v for b in data for v in b]
        out += [f"#define AI{s}_N {len(data)}", ar.c_array(f"AI{s}_DATA", "uint8_t", flat, 24, "0x{:02x}"),
                ar.c_array(f"AI{s}_IDX", "uint8_t", idx, 24), ""]
        tot += len(flat) + len(idx)
        print(f"icons {s}px: {len(data)} cells, {len(flat) + len(idx)} B")
    Path(a.out).write_text("\n".join(out))
    print(f"icons total {tot} B -> {a.out}   ({len(DRAW)} drawings)")
    if a.sheet:
        cols = len(DRAW)
        im = Image.new("RGB", (cols * 30 + 8, len(sizes) * 30 + 8), (23, 20, 31))
        for s, n, img in sheet:
            col, row = list(DRAW).index(n), sizes.index(s)
            im.paste((243, 238, 248), (4 + col * 30, 4 + row * 30), img)
        im.resize((im.width * 3, im.height * 3), Image.NEAREST).save(a.sheet)


if __name__ == "__main__":
    main()
