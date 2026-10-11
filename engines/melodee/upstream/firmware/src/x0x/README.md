# x0x native fp32 and 909 sources

Imported from Charles Vestal's fm1-x0x, commit
`201e5c5c1ac056a028a006bd1eabb23ac1bb174d`:
https://github.com/charlesvestal/fm1-x0x/tree/201e5c5c1ac056a028a006bd1eabb23ac1bb174d

GPL-3.0-only. x0x credits 9W9 by athousanddetails, derived from ER99 by
Matthew Cieplak, for the 909 models. Preserve the original notices in these files.
`fastmath.h` contains x0x's pi32v2-friendly math substrate. The build enables
`-mfprev1`; both cores disable floating-point exception traps while retaining
stack protection, matching x0x's arithmetic requirements.

Melodee changes the instrument identifiers, integrates its shared eight-voice
allocator, adds per-sound and per-hit tuning/decay, and replaces the sampled
metallic voices with oscillator/noise synthesis. The latter is a timbral
approximation, not a reproduction of the original 909 sample ROM. Generated
tables contain mathematical parameter curves and tanh values only.
