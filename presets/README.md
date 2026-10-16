# Stock presets

The 128-preset banks a new library starts from, and the Library tab's **Import** menu loads.
Each is one FM-1+VA v4 message per slot, A01 to D32 (as `fm1_probe dump` writes them).

- `stock-mvave.syx`: M-VAVE's factory presets. Their voices are the factory sounds in M-VAVE's
  stock V15 package (`FM-1_015.fwsc`, sha256 `db1642b2...`, as published in
  [m-vave/fm1_open](https://github.com/m-vave/fm1_open) `sdk/firmware/`): its `USR` entry,
  decrypted as M-VAVE's restore tool does it (`sdk/WebUpdate.js`), whose first 16384 bytes are
  the four banks of DX7 packed voices (sha256 `ecad0b86...`, the hash that tool pins). The
  package carries voices only; each slot's FM-1 record (effects, envelope) is the default every
  factory slot held when read from an FM-1 on 2026-10-05.
  sha1 `5fd651ef86ef6772a22a49174e12fc311b621982`.
- `stock-fm1va.syx`: the same with baud girl's VA preset packs in D01-D32 (every D slot but D17,
  which keeps M-VAVE's), as loaded on an FM-1 on 2026-10-06.
  sha1 `321de0dd2555fcbfc049a26465722514bb1c9160`.

An earlier version of these files came from an FM-1's own dump. Five of its slots differ from
the factory voices: D17 (its name and record too: written by an early plugin test) and A26, B03,
B13, B23 (a few values each, cause unknown).

Felucca, SLOOP and Melodee build their factory sounds from their source (`engines/`), so they
need nothing here.

The sounds are M-VAVE's and baud girl's; no licence is granted for them (THIRD_PARTY.md).
