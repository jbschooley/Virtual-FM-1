# Stock presets

The 128-preset banks a new library starts from, and the Library tab's **Import** menu loads.
Each is a dump of an FM-1's presets as `fm1_probe dump` writes it: one FM-1+VA v4 message per
slot, A01 to D32.

- `stock-mvave.syx`: M-VAVE's stock presets, as an FM-1 held them on 2026-10-05 (read on
  baud girl's FM-1_093, which keeps the stock firmware's presets; the same bytes were read again
  after a fresh FM-1+VA 0.96 install). Not from M-VAVE directly: an early plugin test may have
  written some slots before that day. To be replaced by an official dump if M-VAVE provides one.
  sha1 `cb655cf9a20ed86d6b3ccd4db9d06e229e5845ba`.
- `stock-fm1va.syx`: the same with baud girl's VA preset packs in D01-D32 (every D slot but
  D17, which is the same in both), as loaded on 2026-10-06 and read unchanged on 2026-10-13.
  sha1 `d1973e0f3c6ca51e031b96815f9563d497303c91`.

The firmware packages do not carry these: the stock V15 `FM-1.fwsc` holds one template voice, and
an FM-1+VA install leaves the presets as they were (FM-1+VA 0.97 left them blank). Felucca, SLOOP
and Melodee build their factory sounds from their source (`engines/`), so they need nothing here.

The sounds are M-VAVE's and baud girl's; no licence is granted for them (THIRD_PARTY.md).
