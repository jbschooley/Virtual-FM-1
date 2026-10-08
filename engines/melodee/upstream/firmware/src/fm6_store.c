/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modifications Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* FM6 patches outside the engine (eng_fm6.c): DX7 SysEx from USB-MIDI and back (fm6_service: main loop), and the
 * actions of the STORE page, which saves a native FM6 user preset.
 *
 * DX7 SysEx accepted on any channel n:
 *   F0 43 0n 00 01 1B <155 bytes> <checksum> F7    a voice (VCED) -> the FM6 track's patch
 *   F0 43 0n 09 20 00 <4096 bytes> <checksum> F7   32 voices (VMEM) -> F001..F032 or F033..F064 (STORE slot)
 *   F0 43 1n gg pp dd F7                           a voice parameter pp + 128 gg (155: the six operator
 *                                                  switches, OP1 = bit 5) -> the track
 *   F0 43 1n 08 pp dd F7                           a function parameter (64 mono, 65 bend range, 66 step,
 *                                                  68 glissando, 69 portamento time, 70..77 wheel / foot /
 *                                                  breath / aftertouch range and target) -> the track's
 *                                                  function settings (saved with the project)
 *   F0 43 2n 00 F7 / F0 43 2n 09 F7                dump requests: the track's voice / selected 32-slot bank
 * The FM6 track: the selected track when it plays FM6, else track n + 1, else the first FM6 track.
 * Dexed or any DX7 librarian can edit a track live; SAVE keeps its whole voice. */
static uint8_t fm6_buf[FM6_RX] __attribute__((section(".pool")));   /* main loop: dumps out */

static int fm6_is(uint32_t k) { return k < NPART && trk[k].eng_req == ENGI_FM6; }

static int fm6_sx_track(uint32_t ch)                     /* the FM6 track a DX7 message on channel ch is for, -1 */
{
    uint32_t k;
    if (fm6_is(song.sel))
        return (int)song.sel;
    if (fm6_is(ch))
        return (int)ch;
    for (k = 0; k < NPART; k++)
        if (fm6_is(k))
            return (int)k;
    return -1;
}

static uint8_t fm6_chk(const uint8_t *p, uint32_t n)    /* DX7 checksum: data + it = 0 mod 128 */
{
    uint32_t s = 0;
    while (n--)
        s += *p++;
    return (uint8_t)(-s & 0x7Fu);
}

static uint32_t fm6_sx_first(void) { return fm6_bslot < 32u ? 0u : 32u; }

/* ------------------------------------------------------------- out --- */
#if MELODEE_OTA
static int fm6_send_voice(uint32_t tr, uint32_t ch)       /* VCED: the track's patch */
{
    static const uint8_t H[6] = {0xF0, 0x43, 0x00, 0x00, 0x01, 0x1B};
    if (!usb.config) return -1;
    memcpy(fm6_buf, H, 6);
    fm6_buf[2] = (uint8_t)(ch & 15u);
    memcpy(fm6_buf + 6, fm6_patch[tr % NTRK], 155);
    fm6_buf[161] = fm6_chk(fm6_buf + 6, 155);
    fm6_buf[162] = 0xF7;
    return ota_wire_send(fm6_buf, 163);
}

static int fm6_send_bank(uint32_t ch)
{
    static const uint8_t H[6] = {0xF0, 0x43, 0x00, 0x09, 0x20, 0x00};
    if (!usb.config) return -1;
    uint32_t first = fm6_sx_first();
    memcpy(fm6_buf, H, 6);
    fm6_buf[2] = (uint8_t)(ch & 15u);
    for (uint32_t k = 0; k < 32u; k++)
        memcpy(fm6_buf + 6u + k * FM6_PACKED,
               native_used(ENGI_FM6, first + k) ? native_raw(ENGI_FM6, first + k) : FM6_INIT, FM6_PACKED);
    fm6_buf[4102] = fm6_chk(fm6_buf + 6, 4096);
    fm6_buf[4103] = 0xF7;
    return ota_wire_send(fm6_buf, FM6_RX);
}
#endif

/* ------------------------------------------------------------- in --- */
static void fm6_sysex(const uint8_t *b, uint32_t n)
{
    if (n < 5u || b[0] != 0xF0u || b[1] != 0x43u || b[n - 1u] != 0xF7u) return;
    for (uint32_t j = 2; j + 1u < n; j++) if (b[j] > 127u) return;
    uint32_t st = b[2] & 0xF0u, ch = b[2] & 15u;
    int tr = fm6_sx_track(ch);
    char nm[12];
    if (n == 163u && st == 0x00u && b[3] == 0x00 && b[4] == 0x01 && b[5] == 0x1B &&
        fm6_chk(b + 6, 155) == b[161]) {                 /* one voice */
        if (tr < 0) {
            ui_message("FM6: NO FM6 TRACK");
            return;
        }
        track_t *t = &trk[tr];
        load_begin(t, UNDO_SOUND);
        t->user = t->user_native = 0;                   /* an incoming voice is an unsaved track patch */
        fm6_put_patch((uint32_t)tr, b + 6, 1);           /* a new voice: the notes stop, as in Dexed */
        load_end(t); sync_reload = 1;
        fm6_name(nm, fm6_patch[tr]);
        ui_say("FM6 VOICE ", nm);
        ui.force = 1;
    } else if (n == FM6_RX && st == 0x00u && b[3] == 0x09 && b[4] == 0x20 && b[5] == 0x00 &&
               fm6_chk(b + 6, 4096) == b[4102]) {        /* 32 voices */
        if (transport_busy()) { ui_message("STOP TO IMPORT"); return; }
        uint32_t first = fm6_sx_first(), saved;
        int rc = native_fm_import32(first, b + 6, &saved);
        if (rc == 1 || rc == 2) {
            ui_message("FM6 BANK SAVE ERROR");
            if (saved) str_cpy(ui.msg2, "16 PATCHES SAVED", sizeof ui.msg2);
            return;
        }
        if (tr >= 0) native_load(ENGI_FM6, first, (uint32_t)tr);
        ui_message(rc == 3 ? "FM6 BANK IN RAM" : "FM6 BANK RECEIVED");
        str_cpy(ui.msg2, first ? "F033-F064" : "F001-F032", sizeof ui.msg2);
        ui.force = 1;
    } else if (n == 7u && st == 0x10u && !(b[3] >> 2) && tr >= 0) {   /* a voice parameter */
        uint32_t k = (uint32_t)(b[3] & 3u) << 7 | b[4];
        if (k < 155u) {
            uint8_t v[FP_SIZE + 1u];
            memcpy(v, fm6_patch[tr], FP_SIZE);
            v[k] = b[5];
            fm6_put_patch((uint32_t)tr, v, 0);
        } else if (k == 155u) {
            fm6_on[tr] = b[5] & FM6_ON_ALL;              /* bit 0 OP6 .. bit 5 OP1, as fm6_on */
        }
        ui.force = 1;
    } else if (n == 7u && st == 0x10u && b[3] == 0x08u && tr >= 0) {   /* a function parameter: the track's */
        uint32_t v = b[5], f = (uint32_t)tr;
        switch (b[4]) {
        case 64:                                         /* MONO: Dexed's (legato, the highest key) */
            trk[tr].p[P_VOICE] = v ? V_LEGATO : V_POLY;
            if (v)
                trk[tr].p[P_PRIO] = 2;
            break;
        case 65:
            fm6_fn_set(f, FN_PBUP, (int32_t)v);
            fm6_fn_set(f, FN_PBDN, (int32_t)v);
            break;
        case 66: fm6_fn_set(f, FN_PBSTEP, (int32_t)v); break;
        case 68: fm6_fn_set(f, FN_GLISS, (int32_t)v); break;
        case 69: fm6_fn_set(f, FN_PTIME, (int32_t)(v > 99u ? 99u : v) * 127 / 99); break;   /* 0..99 -> CC 5 */
        default:
            if (b[4] >= 70u && b[4] <= 77u)              /* range, target: wheel, foot, breath, aftertouch */
                fm6_fn_set(f, FN_MWR + (uint32_t)(b[4] - 70u), (int32_t)v);
            break;
        }
        ui.force = 1;
    } else if (n == 5u && st == 0x20u) {                 /* dump requests */
#if MELODEE_OTA
        int rc = 0;
        if (b[3] == 0x00 && tr >= 0) rc = fm6_send_voice((uint32_t)tr, ch);
        else if (b[3] == 0x09) rc = fm6_send_bank(ch);
        if (rc) ui_message("SYSEX SEND ERROR");
#endif
    }
}

static void fm6_service(void)                            /* main loop: a DX7 frame from USB-MIDI */
{
    if (fm6_rx_ready) {
        RING_PUBLISH();                                  /* read the frame only after the flag */
        fm6_sysex(fm6_rx, fm6_rx_n);
        RING_PUBLISH();
        fm6_rx_ready = 0;
    }
    upf_release();
}

/* --------------------------------------------------------- STORE page --- */
/* STORE saves only the DX7 voice into the native FM6 user preset slot. */
static void fm6_store(uint32_t k)
{
    char nm[12];
    if (!fm6_is(song.sel) || k >= UP_SLOTS) return;
    fm6_name(nm, fm6_patch[song.sel]);
    user_ui_named(2, k, nm);
}

static void fm6_init_voice(void)                         /* INIT: the selected track starts from the init voice */
{
    uint8_t v[FP_SIZE + 1u];
    if (!fm6_is(song.sel))
        return;
    fm6_unpack(FM6_INIT, v);
    fm6_put_patch(song.sel, v, 1);
    ui_message("INIT VOICE");
}

static void fm6_send(void)                               /* SEND: the selected track's patch as a DX7 dump */
{
#if MELODEE_OTA
    if (fm6_is(song.sel)) {
        ui_message(fm6_send_voice(song.sel, song.sel) ? "SYSEX SEND ERROR" : "VOICE SENT");
    }
#else
    ui_message("NO SYSEX OUT");
#endif
}
