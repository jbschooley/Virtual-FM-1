# SCL catalogue

The 16 original scale IDs remain unchanged. New scales are appended in the order below;
IDs are stored in projects, presets and editor files, so never reorder existing entries.

The Scala-derived numerical tunings and their original descriptions are from the
[Huygens–Fokker Scala archive, version 94, March 2026](https://www.huygens-fokker.org/scala/downloads.html).
The archive was collected primarily by John H. Chalmers and Manuel Op de Coul; the individual
files identify their authors and historical sources. Equal divisions are generated directly
as 1200 × degree / division. The six maqam models use idealised quarter tones and an octave repeat (including the simplified
SABA keyboard model), informed by
[Maqam World](https://www.maqamworld.com/en/maqam.php); actual regional intonation and melodic
practice vary. These selections describe pitch collections, rather than full maqam performance rules.
The gamelan selections are specific measured or just-intonation examples, not universal tunings.

QNT OFF retains the normal 12-tone chromatic keyboard. With a microtonal selection and QNT enabled,
MIDI note address 60 is the root at C4 + ROOT + TRN. ALL advances one degree per key, WHITE advances
one degree per white key, MPC advances one degree per pad, and SNAP finds the scale tone below a
conventional input pitch. ROOT and TRN remain semitone offsets; octave buttons and ARP OCT advance
one scale period. Bohlen–Pierce repeats at 3:1, and the measured SLENDR period is 1208 cents.
DIA3/DIA7 stack every other degree; fixed chord shapes choose the nearest available degrees to
their semitone intervals. Voicings move notes by whole scale periods.

The existing 0–127 note addresses remain the storage and MIDI identity limit. Microtonal degree
addresses outside that range are silent. ALL and MPC also leave out pitches outside the conventional
MIDI pitch range; WHITE clamps acoustic pitch at the endpoints, like the legacy white layout.
Dense tunings therefore have a narrower accessible register. MIDI OUT sends these degree addresses
without tuning messages or per-note pitch bends. An external receiver must share the same tuning
and root mapping. The internal synthesizers use fractional pitch, including FM6 ratio operators;
FM6 fixed-frequency operators retain their fixed pitch. A4 reference tuning, pitch bend and
semitone transpose still apply.

Scale, QNT and MPC DEG are shared across tracks; ROOT and TRN remain per track. A voice retains the
pitch it started with when scale settings change while held or releasing. Playback interprets saved
degrees using the currently selected scale and root. The original IDs, project formats and 12-tone
behavior are preserved. Older firmware does not understand the added scale IDs.

The SCALES browser groups selections into Western, equal divisions, just intonation,
harmonic, historical, regional, non-octave and maqam families. ALL shows the complete list;
FAV shows saved selections. Device favorites use tagged bytes 16–31 in retired engine 14's
PER5 favorites row, leaving its former preset bits 0–127 and all file layouts unchanged.
Web favorites are stored separately in the browser by stable short scale name.

`catalog.json` is the source of truth. To update checked-in firmware tables and the web editor's
mock catalogue, run `python3 tools/gen_scales.py`; `--check` verifies that both are current.
Builds and host tests perform that check without requiring a network connection.

| ID | SCL | Family | Definition |
| --- | --- | --- | --- |
| 0 | CHR | Original | 12-tone scale |
| 1 | MAJ | Original | 12-tone scale |
| 2 | MIN | Original | 12-tone scale |
| 3 | DOR | Original | 12-tone scale |
| 4 | MIX | Original | 12-tone scale |
| 5 | PEN | Original | 12-tone scale |
| 6 | MPEN | Original | 12-tone scale |
| 7 | HARM | Original | 12-tone scale |
| 8 | PHRY | Original | 12-tone scale |
| 9 | LYD | Original | 12-tone scale |
| 10 | LOC | Original | 12-tone scale |
| 11 | MEL | Original | 12-tone scale |
| 12 | BLUES | Original | 12-tone scale |
| 13 | WHOLE | Original | 12-tone scale |
| 14 | DIMHW | Original | 12-tone scale |
| 15 | DIMWH | Original | 12-tone scale |
| 16 | 5EDO | Equal divisions | 5 divisions |
| 17 | 7EDO | Equal divisions | 7 divisions |
| 18 | 9EDO | Equal divisions | 9 divisions |
| 19 | 10EDO | Equal divisions | 10 divisions |
| 20 | 13EDO | Equal divisions | 13 divisions |
| 21 | 14EDO | Equal divisions | 14 divisions |
| 22 | 15EDO | Equal divisions | 15 divisions |
| 23 | 16EDO | Equal divisions | 16 divisions |
| 24 | 17EDO | Equal divisions | 17 divisions |
| 25 | 18EDO | Equal divisions | 18 divisions |
| 26 | 19EDO | Equal divisions | 19 divisions |
| 27 | 20EDO | Equal divisions | 20 divisions |
| 28 | 21EDO | Equal divisions | 21 divisions |
| 29 | 22EDO | Equal divisions | 22 divisions |
| 30 | 23EDO | Equal divisions | 23 divisions |
| 31 | 24EDO | Equal divisions | 24 divisions |
| 32 | 26EDO | Equal divisions | 26 divisions |
| 33 | 27EDO | Equal divisions | 27 divisions |
| 34 | 29EDO | Equal divisions | 29 divisions |
| 35 | 31EDO | Equal divisions | 31 divisions |
| 36 | 34EDO | Equal divisions | 34 divisions |
| 37 | 36EDO | Equal divisions | 36 divisions |
| 38 | 41EDO | Equal divisions | 41 divisions |
| 39 | 53EDO | Equal divisions | 53 divisions |
| 40 | JI-MAJ | Just intonation | ptolemy.scl |
| 41 | JI-MIN | Just intonation | ptolemy_diat.scl |
| 42 | JI-12 | Just intonation | ji_12.scl |
| 43 | JI-7L | Just intonation | ji_12a.scl |
| 44 | JI-7 | Just intonation | ji_7.scl |
| 45 | HARM8 | Harmonic | harm8.scl |
| 46 | HARM16 | Harmonic | harm16.scl |
| 47 | PARTCH | Just intonation | partch_43.scl |
| 48 | HARRIS | Just intonation | harrison_8.scl |
| 49 | PYTH | Historical | pyth_12.scl |
| 50 | MT-1/4 | Historical | meanquar.scl |
| 51 | MT-1/3 | Historical | meanthird.scl |
| 52 | MT-1/6 | Historical | meansixth.scl |
| 53 | WERCK3 | Historical | werck3.scl |
| 54 | KIRN3 | Historical | kirnberger3.scl |
| 55 | VALLOT | Historical | vallotti.scl |
| 56 | YOUNG2 | Historical | young2.scl |
| 57 | RAST-J | Regional | rast_11-limit.scl |
| 58 | SABA-J | Regional | saba_sup.scl |
| 59 | SHRUTI | Regional | indian.scl |
| 60 | SLENDR | Regional | slendro_av.scl |
| 61 | PELOG | Regional | alves_pelog.scl |
| 62 | BP-ET | Non-octave | bohlen-p_et.scl |
| 63 | BP-JI | Non-octave | bohlen-p.scl |
| 64 | RAST | Maqam models | Quarter-tone model |
| 65 | BAYATI | Maqam models | Quarter-tone model |
| 66 | HIJAZ | Maqam models | Quarter-tone model |
| 67 | SABA | Maqam models | Quarter-tone model |
| 68 | HUSAYN | Maqam models | Quarter-tone model |
| 69 | SIKAH | Maqam models | Quarter-tone model |
