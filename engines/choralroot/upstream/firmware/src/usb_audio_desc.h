/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * ChoralRoot changes (six capture channels, PCM16 capture, the channel names, the playback function removed; 0.19:
 * the second sampling frequency)
 * Copyright (C) 2026 ChoralRoot FM-1 contributors */
/* Melodee's UAC1 recording function (usb_audio_desc.h, Melodee 0.11.1), 44.1 and 48 kHz. ChoralRoot has no playback
 * function (Melodee's "Out": the computer's audio through the FM-1, removed in 0.14).
 * IF2 control + IF3 streaming: recording "ChoralRoot In" (string 3), EP2 IN, alternate 1 = PCM16 (six channels:
 * master L R, CHORD L R, BASS L R; their names are strings 4..9, the input terminal's iChannelNames).
 * Included inside usb.c's CFG_DESC (after the MIDI function), whose length is CFG_LEN. */
    8, 0x0B, 2, 2, 1, 1, 0, 3,                      /* IAD: recording (IF 2-3), "ChoralRoot In" */
    9, 4, 2, 0, 0, 1, 1, 0, 3,
    9, 0x24, 1, 0x00, 0x01, 30, 0, 1, 3,            /* AC header: one stream, IF 3 */
    12, 0x24, 2, 3, 0x13, 0x07, 0, 6, 0, 0, 4, 0,  /* synthesizer: six non-spatial channels, names from string 4 */
    9, 0x24, 3, 4, 0x01, 0x01, 0, 3, 0,             /* USB streaming output */

    9, 4, 3, 0, 0, 1, 2, 0, 0,
    9, 4, 3, 1, 1, 1, 2, 0, 0,
    7, 0x24, 1, 4, 1, 1, 0,                         /* PCM */
    14, 0x24, 2, 1, 6, 2, 16, 2, 0x44, 0xAC, 0x00, 0x80, 0xBB, 0x00,   /* type I: 6 ch, 16 bit, two discrete rates:
                                                     * 44100 and 48000 Hz (0.19; SLOOP 2.5's / Felucca 1.1.5's form) */
    9, 5, 0x82, 0x05, UA_EP_MAXP & 0xFF, UA_EP_MAXP >> 8, 1, 0, 0,   /* EP2 IN isochronous async: packets of up to
                                                     * 49 x 12 = 588 bytes (48 kHz; 540 at 44.1), 688 reserved
                                                     * (UA_EP_MAXP: bit stuffing) */
    7, 0x25, 1, 1, 0, 0, 0,                         /* sampling-frequency control: the host's SET_CUR picks the rate */
