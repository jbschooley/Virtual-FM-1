#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Modifications Copyright (C) 2026 Kerem Kilic (Ellic Studio)
# Modifications Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
"""P5's factory programs: assets/prophet5-factory/prophet5-v1.03.syx (Sequential's 200 Prophet-5 / 10 factory
programs v1.03, unchanged native dumps; assets/prophet5-factory/README.md) -> a header for eng_prophet_test.c
(FELUCCA_P5_FACTORY: a build with 0 does not include it).

Melodee's tools/gen_prophet_factory.py (Kerem Kilic, GPL-3.0-only; its v1.2.1: the packed form of commit 1bc4555),
without its web editor's block. ChoralRoot's additions: P5_FACTORY_PRESETS(X), the PRESETS names (12 characters, upper
case as the other engines' are; the full 20-character name stays in the program's bytes), and THE SHIPPED LIST: which
of the 200 are compiled in, and in which order, is a text file (docs/PROPHET.md "The shipped list").

  P5_FACTORY_ALL        200: Sequential's bank
  P5_FACTORY_N          the programs in this build (the list's length)
  P5_FACTORY_ID         per shipped program, in the pool's order: its Sequential program number 1..200. That number
                        is the program's IDENTITY (its group and program bytes in the .syx): it does not change when
                        the list does, and it is what a record of a part's sound holds (the program's own bytes 136,
                        137: eng_prophet_test.c p5_patch_id)
  P5_FBASE / P5_FWIDTH  per raw byte k of the 133: the smallest value among the shipped programs, the bits its range needs
  P5_FPACK              the programs, P5_FACTORY_BYTES each: raw byte k is P5_FBASE[k] + the next P5_FWIDTH[k] bits, LSB
                        first, a program starts on a byte boundary (eng_prophet_test.c p5_factory unpacks; checked here)
  P5_FACTORY_PRESETS(X) X(name) for each shipped program, in the pool's order

  tools/gen_prophet_factory.py OUT.h [LIST]      the header (written only when its text changes)
  tools/gen_prophet_factory.py --list [LIST]     the shipped programs: position, program number, U (UNISON on), name
  tools/gen_prophet_factory.py --pos N [LIST]    the pool position of program N (0: not shipped)

LIST: a file, or "all". Without it: the environment's P5_SHIPPED (the same), else assets/prophet5-factory/shipped.txt,
and when that file is not there all 200 in the bank's order. The file: one program number 1..200 a line, in the
pool's order; "#" starts a comment; empty lines are skipped; a number twice or out of range is an error, and so is an
empty list (INIT alone is FELUCCA_P5_FACTORY=0); the word "all" alone on a line is every program not yet named, in the
bank's order.
"""
import hashlib
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets/prophet5-factory/prophet5-v1.03.syx"
SHIPPED = ROOT / "assets/prophet5-factory/shipped.txt"
P5_UNISON = 52                                   # prophet_patch.c: the raw byte of the UNISON switch
SHA256 = "1d6e9dec2753a08ac6d07b98f0f4e755e33b41de11dc3bed46fa23217619d232"
# the programs' names where they do not fit the 12 characters of a PRESETS name (upper case)
SHORT = {
    "IT'S A PROPHET 5": "ITS PROPHET5", "FORMANT SERAPHIM": "F. SERAPHIM", "FANTASTIC FIVE": "FANTASTIC 5",
    "DIGI-RECORDER": "DIGI-RECORDR", "REEDY STRING PAD": "REEDY STRPAD", "CANNED HAPPINESS": "CANNED HAPPY",
    "BORN IN THE 80'S": "BORN IN 80S", "STICKY PLUCKS": "STICKY PLUCK", "WATERY PLUCKS": "WATERY PLUCK",
    "TREMBLING ORGAN": "TREMBL ORGAN", "UNICORN DREAMS": "UNICORN DRMS", "COCKTAIL HOUR": "COCKTAIL HR",
    "NICE N SMOOTH": "NICE SMOOTH", "TUMESCENT BASS": "TUMESC BASS", "BLINKER BELLS": "BLINKR BELLS",
    "PICKLE PINCHER": "PICKLE PINCH", "UNDERWATER TRILL": "UWATER TRILL", "TANGERINE CHRCH THM": "TANGERINE CH",
    "PIPE ORGANISATION": "PIPE ORGANIS", "FAT POLY BASS": "FAT POLYBASS", "CHORAL VOICES": "CHORAL VOICE",
    "ANOTHER OCEAN": "ANOTHR OCEAN", "PEDALING SWEEP": "PEDAL SWEEP", "HUM-ALONG SOLO": "HUM-ALONG",
    "FAT BASS STACK": "FAT BASS STK", "AFTERBLOW HORN": "AFTRBLW HORN", "NO PARKING SWELL": "NOPARK SWELL",
    "INSTABILITY PAD": "INSTABIL PAD", "VINTAGE WURLY": "VINTAGE WRLY", "ROBOT HAMSTERS": "ROBOT HAMSTR",
    "PRESSURE POINTS": "PRESSURE PTS", "80S HORROR STRINGS": "80S HORROR", "SOFT MOVING BRASS": "MOVING BRASS",
    "MORSE CODE LEAD": "MORSE LEAD", "UNDER THE ICE": "UNDER ICE", "DISTANT FUTURE": "DISTANT FUTR",
    "HOLLOW DREAMS": "HOLLOW DRMS", "MOUSE BLASTER": "MOUSE BLASTR", "PINK NEON BRASS": "PINK NEON BR",
    "SKREAMER SOLO": "SKREAMR SOLO", "DREAM OF ORGONON": "DRM ORGONON", "VELO RES LEAD 2": "VELO RES LD2",
    "REGAL MOVEMENT": "REGAL MOVMNT", "LOOSE SQUARES": "LOOSE SQUARE", "RUBBER BUMPER": "RUBBR BUMPER",
    "GALVANIZED BUCKET": "GALV BUCKET", "REVERBERANT STRINGS": "REVERB STRGS", "PLUCKITY DUCK": "PLUCKTY DUCK",
    "TUNED PERCUSSION": "TUNED PERC", "FUNKY REV1 BELLS": "FUNKY BELLS", "NYLON FINGERS": "NYLON FINGRS",
    "SPARKLE PONIES": "SPARKLE PONY", "MARRAKESH RADIO C": "MARRAKESH C", "FAINT MEMORIES": "FAINT MEMORY",
    "OLD STRING MACHINE": "OLD STR MACH", "SYNC SWELLPAD": "SYNC SWELLPD", "FEATURE MY BASS": "FEATURE BASS",
    "AFTER THE DENOUEMENT": "DENOUEMENT", "MANEATER PING": "MANEATR PING", "BULL BY THE HORNS": "BULL BY HORN",
    "BUSTED AND DUSTED": "BUSTED DUSTD", "TIGHT BASS (556?)": "TIGHT BASS", "80S SCI FI DROP": "SCI FI DROP",
    "CYBERIAN WINDS": "CYBERIAN WND", "OMINOUS BELLFALL": "OMINOUS BELL", "METAL DRONESPACE": "METAL DRONE",
    "MUTED CLAVINET": "MUTED CLAV", "PERCUSSIVE E PIANO": "PERC E PIANO", "PERCUSSIVE ORGAN": "PERC ORGAN",
    "UNISON GLIDE W RES": "UNISON GLIDE", "ORGAN W RESONANCE": "ORGAN W RES", "TRUMPET FLUTE": "TRUMPT FLUTE",
    "BASS IN FIFTHS": "BASS IN 5THS", "PIPE ORGAN FLUTES": "PIPE FLUTES", "ELECTRIC PIANO I": "ELEC PIANO I",
    "OCTAVE SAWTEETH": "OCTAVE SAWS", "RELEASE REPEAT": "REL REPEAT", "DELAYED HARMONIC": "DELAYED HARM",
    "PULSE WIDTH MOD": "PULSE W MOD", "SLOW SYNC SWEEP": "SLOW SYNC SW", "FOURTHS W RESONANCE": "4THS W RES",
    "SWEEPING HARMONICS": "SWEEP HARMS", "RANDOM ARPEGGIATOR": "RANDOM ARP", "SAWTOOTH ARPEGGIATOR": "SAWTOOTH ARP",
    "CLANGOROUS BELLS": "CLANG BELLS", "DESCENDING BELLS": "DESC BELLS", "DESCENDING PWM": "DESC PWM",
    "RESONANCE BELLS": "RES BELLS",
}
# .. and by program number (1..200) where two programs' names differ in their case only (105 "High Strings")
SHORT_AT = {180: "HIGH STRGS 2"}


def records():
    data = SOURCE.read_bytes()
    if hashlib.sha256(data).hexdigest() != SHA256 or len(data) != 200 * 159:
        raise ValueError("Sequential v1.03 source checksum or bank length changed")
    result = []
    for k in range(200):
        frame = data[k * 159:(k + 1) * 159]
        if frame[:4] != bytes([240, 1, 50, 2]) or frame[-1] != 247 or any(x > 127 for x in frame[1:-1]):
            raise ValueError(f"Invalid native frame {k + 1}")
        if frame[4] != k // 40 or frame[5] != k % 40:
            raise ValueError(f"Unexpected native address {k + 1}")
        packed = frame[6:-1]
        raw = bytes(packed[i + j + 1] | ((packed[i] >> j & 1) << 7)
                    for i in range(0, 152, 8) for j in range(7))
        result.append(raw + bytes([133, 50, 2, frame[4], frame[5]]))
    return result


def shipped(arg=None):
    """the shipped list: program numbers 1..200 in the pool's order (the module's text, above)"""
    arg = arg or os.environ.get("P5_SHIPPED") or (str(SHIPPED) if SHIPPED.exists() else "all")
    if arg == "all":
        return list(range(1, 201))
    ids = []
    for no, line in enumerate(Path(arg).read_text().splitlines(), 1):
        word = line.split("#", 1)[0].split()
        if not word:
            continue
        if word[0] == "all" and len(word) == 1:
            ids += [k for k in range(1, 201) if k not in ids]
            continue
        if len(word) != 1 or not word[0].isdigit() or not 1 <= int(word[0]) <= 200:
            raise ValueError(f"{arg}:{no}: {line.strip()!r}: one program number 1..200 a line (comments after #)")
        if int(word[0]) in ids:
            raise ValueError(f"{arg}:{no}: program {word[0]} is in the list twice")
        ids.append(int(word[0]))
    if not ids:
        raise ValueError(f"{arg}: no program (INIT alone: FELUCCA_P5_FACTORY=0)")
    return ids


def near(ids, patches, most=8):
    """pairs of shipped programs that differ in at most `most` of the 55 panel bytes (a note, not an error)"""
    return [(a, b, n) for i, a in enumerate(ids) for b in ids[i + 1:]
            for n in [sum(patches[a - 1][k] != patches[b - 1][k] for k in range(55))] if n <= most]


def pack(ids, bank):
    """The 133 raw bytes of every shipped program, packed (eng_prophet_test.c p5_factory unpacks them): raw byte k is
    P5_FBASE[k] plus the next P5_FWIDTH[k] bits, LSB first; each program starts on a byte boundary. The
    trailer (size, model, command, group, program) follows from the program's number (P5_FACTORY_ID)."""
    for n, p in enumerate(bank):
        if p[133:] != bytes([133, 50, 2, n // 40, n % 40]):
            raise ValueError(f"program {n + 1}: unexpected trailer")
    patches = [bank[i - 1] for i in ids]
    base = [min(p[k] for p in patches) for k in range(133)]
    width = [(max(p[k] for p in patches) - base[k]).bit_length() for k in range(133)]
    per = (sum(width) + 7) // 8
    data = bytearray()
    for n, p in enumerate(patches):
        bits, pos = 0, 0
        for k in range(133):
            bits |= (p[k] - base[k]) << pos
            pos += width[k]
        data += bits.to_bytes(per, "little")
    for n, p in enumerate(patches):                      # the firmware's unpacking, checked here
        bits, pos = int.from_bytes(data[n * per:(n + 1) * per], "little"), 0
        for k in range(133):
            if base[k] + (bits >> pos & ((1 << width[k]) - 1)) != p[k]:
                raise ValueError(f"program {ids[n]}: packing does not round-trip")
            pos += width[k]
    rows = lambda v: ["    " + ",".join(map(str, v[i:i + 32])) + "," for i in range(0, len(v), 32)]
    return ([f"#define P5_FACTORY_BYTES {per}u",
             "static const uint8_t P5_FBASE[133] = {"] + rows(base) + ["};",
             "static const uint8_t P5_FWIDTH[133] = {"] + rows(width) + ["};",
             f"static const uint8_t P5_FPACK[{len(data)}] = {{"] + rows(list(data)) + ["};"])


def names(patches):
    """the PRESETS names: the program's name in upper case, SHORT's where that is over 12 characters; all different"""
    out = []
    for n, p in enumerate(patches):
        full = bytes(p[65:85]).decode("ascii").rstrip().upper()
        name = SHORT_AT.get(n + 1, SHORT.get(full, full))
        if not name or len(name) > 12 or any(not 32 <= ord(c) <= 126 or c in '"\\' for c in name):
            raise ValueError(f"program {n + 1} {full!r}: no PRESETS name of 1..12 characters ({name!r})")
        out.append(name)
    if len(set(out)) != len(out):
        raise ValueError("two programs share a PRESETS name: " + ", ".join(sorted(x for x in set(out) if out.count(x) > 1)))
    unused = set(SHORT) - {bytes(p[65:85]).decode("ascii").rstrip().upper() for p in patches}
    if unused:
        raise ValueError("SHORT names no program: " + ", ".join(sorted(unused)))
    return out


def main(path, arg=None):
    bank = records()
    ids = shipped(arg)
    name = names(bank)                                   # (of all 200: a name does not depend on the list)
    lines = ["/* generated by tools/gen_prophet_factory.py from assets/prophet5-factory/prophet5-v1.03.syx:",
             " * Sequential's 200 Prophet-5 / 10 factory programs v1.03, unchanged native data (the rights stay with",
             " * Sequential: assets/prophet5-factory/README.md, LICENSING.md); the programs of the shipped list",
             " * (assets/prophet5-factory/shipped.txt, docs/PROPHET.md \"The shipped list\") in the pool's order */",
             "#pragma once",
             f"#define P5_FACTORY_ALL {len(bank)}u",
             f"#define P5_FACTORY_N {len(ids)}u",
             f"static const uint8_t P5_FACTORY_ID[{len(ids)}] = {{"]
    lines += ["    " + ",".join(map(str, ids[i:i + 32])) + "," for i in range(0, len(ids), 32)] + ["};"]
    lines += pack(ids, bank)
    lines.append("#define P5_FACTORY_PRESETS(X) \\")
    lines += [f'    X("{name[i - 1]}") \\' for i in ids]
    lines.append("")
    text = "\n".join(lines) + "\n"
    out = Path(path)
    out.parent.mkdir(parents=True, exist_ok=True)
    if not out.exists() or out.read_text() != text:
        out.write_text(text)
    uni = sum(bank[i - 1][P5_UNISON] != 0 for i in ids)
    print(f"prophet factory: {len(ids)} of {len(bank)} programs ({uni} with UNISON) -> {path}")
    for a, b, n in near(ids, bank):
        print(f"prophet factory: note: programs {a} {name[a - 1]} and {b} {name[b - 1]} differ in {n} of the 55 panel bytes")


def listing(arg=None):
    bank = records()
    name = names(bank)
    for pos, i in enumerate(shipped(arg), 1):
        print(f"{pos:3d} {i:3d} {'U' if bank[i - 1][P5_UNISON] else '-'} {name[i - 1]}")


if __name__ == "__main__":
    a = sys.argv[1:]
    try:
        if a and a[0] == "--list" and len(a) <= 2:
            listing(*a[1:])
        elif a and a[0] == "--pos" and 2 <= len(a) <= 3 and a[1].isdigit():
            ids = shipped(*a[2:])
            print(ids.index(int(a[1])) + 1 if int(a[1]) in ids else 0)
        elif 1 <= len(a) <= 2 and not a[0].startswith("-"):
            main(*a)
        else:
            sys.exit(__doc__)
    except (ValueError, OSError) as e:
        sys.exit(f"gen_prophet_factory.py: {e}")
