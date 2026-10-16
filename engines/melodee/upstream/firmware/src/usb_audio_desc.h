/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* One UAC1 duplex function: stereo playback, selectable capture; PCM16/24 at 44.1 kHz.
 * This template has four tracks; ua_cfg_build patches capture to stereo by default.
 * IF2 control groups IF3 playback and IF4 recording into one "Melodee" device.
 * EP2 OUT plays stereo, EP3 IN supplies explicit feedback, EP2 IN records.
 * Playback goes through FU5, advertising standard master mute and volume.
 * Capture packets are prepared ahead of the refill deadline (usb_audio.c).
 * Fixed native sample rate; alternate 1 = 16 bit, 2 = 24 bit. */
    8, 0x0B, 2, 3, 1, 1, 0, 2,                      /* IAD: duplex audio (IF 2-4) */
    9, 4, 2, 0, 0, 1, 1, 0, 2,
    10, 0x24, 1, 0x00, 0x01, 62, 0, 2, 3, 4,
    12, 0x24, 2, 1, 0x01, 0x01, 0, 2, 3, 0, 0, 0,  /* USB streaming input */
    10, 0x24, 6, 5, 1, 1, 3, 0, 0, 0,              /* FU5: master mute/volume, no per-channel controls */
    9, 0x24, 3, 2, 0x01, 0x03, 0, 5, 0,             /* speaker output from FU5 */
    12, 0x24, 2, 3, 0x13, 0x07, 0, 4, 0, 0, 0, 0,  /* four non-spatial synth/drum channels */
    9, 0x24, 3, 4, 0x01, 0x01, 0, 3, 0,             /* USB streaming output */

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

    9, 4, 4, 0, 0, 1, 2, 0, 0,
    9, 4, 4, 1, 1, 1, 2, 0, 0,
    7, 0x24, 1, 4, 1, 1, 0,                         /* PCM */
    11, 0x24, 2, 1, 4, 2, 16, 1, 0x44, 0xAC, 0,
    9, 5, 0x82, 0x05, 104, 1, 1, 0, 0,
    7, 0x25, 1, 1, 0, 0, 0,                         /* sampling-frequency control */
    9, 4, 4, 2, 1, 1, 2, 0, 0,
    7, 0x24, 1, 4, 1, 1, 0,                         /* PCM */
    11, 0x24, 2, 1, 4, 3, 24, 1, 0x44, 0xAC, 0,
    9, 5, 0x82, 0x05, 28, 2, 1, 0, 0,
    7, 0x25, 1, 1, 0, 0, 0,                         /* sampling-frequency control */
