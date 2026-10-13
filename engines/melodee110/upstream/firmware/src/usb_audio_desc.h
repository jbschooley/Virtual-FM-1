/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Two UAC1 functions: stereo playback, four mono capture tracks; PCM16/24 at 44.1 kHz.
 * Hosts list them as two devices, each on its own clock: macOS times a single
 * duplex device from its recording packets, so a late one there cost playback.
 * IF2 control + IF3 streaming: playback "Melodee Out", EP2 OUT, EP3 IN explicit feedback.
 * IF4 control + IF5 streaming: recording "Melodee In", EP2 IN.
 * Fixed native sample rate; alternate 1 = 16 bit, 2 = 24 bit. */
    8, 0x0B, 2, 2, 1, 1, 0, 3,                      /* IAD: playback (IF 2-3) */
    9, 4, 2, 0, 0, 1, 1, 0, 3,
    9, 0x24, 1, 0x00, 0x01, 30, 0, 1, 3,
    12, 0x24, 2, 1, 0x01, 0x01, 0, 2, 3, 0, 0, 0,  /* USB streaming input */
    9, 0x24, 3, 2, 0x01, 0x03, 0, 1, 0,             /* speaker output */

    9, 4, 3, 0, 0, 1, 2, 0, 0,
    9, 4, 3, 1, 2, 1, 2, 0, 0,
    7, 0x24, 1, 1, 1, 1, 0,                         /* PCM */
    11, 0x24, 2, 1, 2, 2, 16, 1, 0x44, 0xAC, 0,
    9, 5, 0x02, 0x05, 180, 0, 1, 0, 131,
    7, 0x25, 1, 1, 0, 0, 0,                         /* sampling-frequency control */
    9, 5, 0x83, 0x11, 3, 0, 1, 4, 0,               /* explicit 10.14 feedback */
    9, 4, 3, 2, 2, 1, 2, 0, 0,
    7, 0x24, 1, 1, 1, 1, 0,                         /* PCM */
    11, 0x24, 2, 1, 2, 3, 24, 1, 0x44, 0xAC, 0,
    9, 5, 0x02, 0x05, 14, 1, 1, 0, 131,
    7, 0x25, 1, 1, 0, 0, 0,                         /* sampling-frequency control */
    9, 5, 0x83, 0x11, 3, 0, 1, 4, 0,               /* explicit 10.14 feedback */

    8, 0x0B, 4, 2, 1, 1, 0, 4,                      /* IAD: recording (IF 4-5) */
    9, 4, 4, 0, 0, 1, 1, 0, 4,
    9, 0x24, 1, 0x00, 0x01, 30, 0, 1, 5,
    12, 0x24, 2, 3, 0x13, 0x07, 0, 4, 0, 0, 0, 0,  /* four non-spatial synth/drum channels */
    9, 0x24, 3, 4, 0x01, 0x01, 0, 3, 0,             /* USB streaming output */

    9, 4, 5, 0, 0, 1, 2, 0, 0,
    9, 4, 5, 1, 1, 1, 2, 0, 0,
    7, 0x24, 1, 4, 1, 1, 0,                         /* PCM */
    11, 0x24, 2, 1, 4, 2, 16, 1, 0x44, 0xAC, 0,
    9, 5, 0x82, 0x05, 104, 1, 1, 0, 0,
    7, 0x25, 1, 1, 0, 0, 0,                         /* sampling-frequency control */
    9, 4, 5, 2, 1, 1, 2, 0, 0,
    7, 0x24, 1, 4, 1, 1, 0,                         /* PCM */
    11, 0x24, 2, 1, 4, 3, 24, 1, 0x44, 0xAC, 0,
    9, 5, 0x82, 0x05, 28, 2, 1, 0, 0,
    7, 0x25, 1, 1, 0, 0, 0,                         /* sampling-frequency control */
