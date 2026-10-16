/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* USB0 full-speed isochronous service, included by usb.c after the SIE helpers.
 * The AC79 usb_phy.h defines bit 14 as ISOCHRONOUS in TX/RX CSR. As in the
 * SDK and the MIDI endpoints, MaxP is written as 0xFF and TX bit 13 (direction)
 * stays clear: an exact MaxP turns on double packet buffering, and then every
 * other OUT packet never reaches ua_rx. RX DMA keeps the same four-byte guard
 * as USB MIDI. All SIE access stays in TIMER5, including SET_INTERFACE and
 * reset handling; register access goes through hal/fm1_usb.h. */
#define UA_IF_PLAY 3u                           /* streaming interfaces in CFG_DESC (usb_audio_desc.h) */
#define UA_IF_CAP 4u
#define UA_IF_CONTROL 2u
#define UA_FEATURE_PLAY 5u
#define UA_OFF_OUT 1u                           /* ua_off: directions left out of the configuration */
#define UA_OFF_IN 2u
#define UA_NO_IF 0xFFu
static uint8_t ua_off;                          /* what the host is given (ua_cfg_build at usb_start) */
static uint8_t ua_tracks, ua_tracks_want;       /* 0 stereo master (default), 1 four mono stems */
static uint8_t ua_off_want;                     /* GLO > SYSTEM (ua_off_set); ua_off_apply makes it ua_off */
static uint32_t ua_off_ms;                      /* its last change */
static uint8_t ua_if_play = UA_IF_PLAY, ua_if_cap = UA_IF_CAP, ua_nif = 5;   /* as sent (ua_cfg_build) */
#define UA_DMA_PACKET ((UA_PACKET + 3u) & ~3u)
static uint8_t ua_tx[2][UA_DMA_PACKET] __attribute__((aligned(4)));
static uint32_t ua_tx_bytes;
static uint8_t ua_tx_slot;
static uint8_t ua_rx[((UA_PLAY_PACKET + 3u) & ~3u) + 4u] __attribute__((aligned(4)));
static uint8_t ua_fb[4] __attribute__((aligned(4)));
static uint16_t ua_frame;
static uint8_t ua_frame_valid, ua_paused;
static uint8_t ua_rate_pending, ua_rate_reply[3];
static uint8_t ua_feature_pending, ua_feature_reply[2];

/* One audio control interface owns both streams. Omitted directions lose their
 * streaming interface and terminal pair; the shared IAD and AC header follow
 * the remaining collection. Absent streams get UA_NO_IF. */
static void ua_cfg_build(void)
{
    uint32_t i, k, n = 0, nif = 0, skip = 0;
    uint32_t original_if = UA_NO_IF, cap_width = 2u;
    uint32_t play = !(ua_off & UA_OFF_OUT), cap = !(ua_off & UA_OFF_IN), streams = play + cap;
    ua_cap_channels = ua_tracks ? 4u : 2u;
    /* Retire the split-device revisions. Give each duplex layout a distinct BCD revision so
     * the host reads the format and packet stride that ua_audio will produce.
     * Use the applied settings here; pending knob edits keep the old layout. */
    memcpy(ua_dev_desc, DEV_DESC, sizeof ua_dev_desc);
    ua_dev_desc[12] = (uint8_t)(0x30u + (ua_off & 3u) + (ua_tracks ? 4u : 0u));
    ua_if_play = play ? UA_IF_PLAY : UA_NO_IF;
    ua_if_cap = cap ? (play ? UA_IF_CAP : UA_IF_PLAY) : UA_NO_IF;
    for (i = 0; i < sizeof CFG_DESC; i += CFG_DESC[i]) {
        const uint8_t *d = CFG_DESC + i;
        uint8_t *o = ua_cfg + n;
        if (d[1] == 0x0Bu) {                    /* IAD: a function starts */
            skip = d[2] == 2u && !streams;
        } else if (d[1] == 4u) {
            original_if = d[2];
            skip = (original_if == 2u && !streams) || (original_if == UA_IF_PLAY && !play) ||
                   (original_if == UA_IF_CAP && !cap);
        }
        if (skip)
            continue;
        if (original_if == 2u && d[1] == 0x24u && (d[2] == 2u || d[2] == 3u) &&
            (d[3] <= 2u ? !play : !cap))
            continue;                           /* the disabled direction's terminal pair */
        if (original_if == UA_IF_CONTROL && d[1] == 0x24u && d[2] == 6u && !play)
            continue;                           /* playback volume disappears with playback */
        for (k = 0; k < d[0]; k++)
            o[k] = d[k];
        n += d[0];
        if (d[1] == 0x0Bu) {
            if (d[2] == 2u) o[3] = (uint8_t)(1u + streams);
        } else if (d[1] == 4u) {
            if (original_if == UA_IF_CAP) o[2] = ua_if_cap;
            nif += d[3] == 0u;
        } else if (original_if == 2u && d[1] == 0x24u && d[2] == 1u) {
            o[0] = (uint8_t)(8u + streams);
            o[5] = (uint8_t)(o[0] + streams * 21u + play * 10u); /* terminals and playback feature unit */
            o[7] = (uint8_t)streams;
            o[8] = play ? ua_if_play : ua_if_cap;
            if (streams == 2u) o[9] = ua_if_cap;
            n -= d[0] - o[0];
        }
        if (!ua_tracks) {
            if (original_if == 2u && d[1] == 0x24u && d[2] == 2u && d[3] == 3u) {
                o[7] = 2; o[8] = 3;             /* stereo L/R terminal */
            } else if (original_if == UA_IF_CAP && d[1] == 0x24u && d[2] == 2u) {
                o[4] = 2;                      /* PCM format channels */
                cap_width = d[5];
            } else if (original_if == UA_IF_CAP && d[1] == 5u) {
                uint32_t bytes = UA_MAX_FRAMES * 2u * cap_width;
                o[4] = (uint8_t)bytes; o[5] = (uint8_t)(bytes >> 8);
            }
        }
    }
    ua_cfg[2] = (uint8_t)n;
    ua_cfg[3] = (uint8_t)(n >> 8);
    ua_cfg[4] = (uint8_t)nif;
    ua_cfg_len = (uint16_t)n;
    ua_nif = (uint8_t)nif;
}

static void ua_play_config(void)
{
    fm1_usb_ep_rxbuf(2, ua_rx);
    sie_wr(S_INDEX, 2);
    sie_wr(S_RXMAXP, 0xFF);
    sie_wr(S_RXCSR1, 0x90);                     /* flush FIFO, clear toggle */
    sie_wr(S_RXCSR2, 0x40);                     /* ISOCHRONOUS */
    fm1_usb_ep_txbuf(3, ua_fb);
    sie_wr(S_INDEX, 3);
    sie_wr(S_TXMAXP, 0xFF);
    sie_wr(S_TXCSR1, 0x48);
    sie_wr(S_TXCSR2, 0x40);                     /* ISOCHRONOUS */
    sie_wr(S_INTRRX1E, sie_rd(S_INTRRX1E) | 0x04u);
    fm1_usb_ep_enable((1u << 2) | (1u << 3));
}

static void ua_cap_config(void)
{
    ua_tx_bytes = ua_tx_slot = 0;
    fm1_usb_ep_txbuf(2, ua_tx[0]);
    sie_wr(S_INDEX, 2);
    sie_wr(S_TXMAXP, 0xFF);
    sie_wr(S_TXCSR1, 0x48);
    sie_wr(S_TXCSR2, 0x40);
    fm1_usb_ep_enable(1u << 2);
}

static void ua_hw_stop(void)
{
    ua_reset();
    ua_rate_pending = 0;
    ua_feature_pending = 0;
    ua_frame_valid = ua_paused = 0;
    ua_tx_bytes = ua_tx_slot = 0;
    sie_wr(S_INTRRX1E, sie_rd(S_INTRRX1E) & ~0x04u);
    sie_wr(S_INDEX, 2);
    sie_wr(S_RXCSR1, 0x90);
    sie_wr(S_TXCSR1, 0x48);
    sie_wr(S_INDEX, 3);
    sie_wr(S_TXCSR1, 0x48);
}

/* Keep one packet queued on each IN endpoint. Prepare capture's next packet
 * while DMA owns the current one, keeping PCM packing out of the critical
 * path between noticing completion and arming the next IN transfer.
 * An empty IN response loses a millisecond of the host's audio clock. */
static void ua_tx_fill(void)
{
    uint32_t n;
    if (ua.play_alt) {
        sie_wr(S_INDEX, 3);
        if (!(sie_rd(S_TXCSR1) & 1u)) {
            n = ua_feedback();
            ua_fb[0] = (uint8_t)n;
            ua_fb[1] = (uint8_t)(n >> 8);
            ua_fb[2] = (uint8_t)(n >> 16);
            fm1_usb_ep_send(3, ua_fb, 3);
            sie_wr(S_TXCSR1, 1);
        }
    }
    if (ua.cap_alt) {
        sie_wr(S_INDEX, 2);
        if (!(sie_rd(S_TXCSR1) & 1u)) {
            if (!ua_tx_bytes)                   /* first packet after a stream reset */
                ua_tx_bytes = ua_transmit(ua_tx[ua_tx_slot]);
            fm1_usb_ep_send(2, ua_tx[ua_tx_slot], ua_tx_bytes);
            sie_wr(S_TXCSR1, 1);
            ua.tx_packets++;
            ua_tx_slot ^= 1u;
            ua_tx_bytes = 0;
        }
    }
}

static void ua_tx_prepare(void)
{
    if (ua.cap_alt && !ua_tx_bytes)
        ua_tx_bytes = ua_transmit(ua_tx[ua_tx_slot]);
}

static int ua_set_interface(uint16_t interface, uint16_t alt)
{
    if (!usb.config || alt > 2u || interface == UA_NO_IF || (interface != ua_if_play && interface != ua_if_cap))
        return 0;
    if (interface == ua_if_play) {
        ua_play_reset();
        ua.play_alt = (uint8_t)alt;
        if (alt)
            ua_play_config();
        else {
            sie_wr(S_INTRRX1E, sie_rd(S_INTRRX1E) & ~0x04u);
            sie_wr(S_INDEX, 2);
            sie_wr(S_RXCSR1, 0x90);
            sie_wr(S_INDEX, 3);
            sie_wr(S_TXCSR1, 0x48);
        }
    } else {
        ua_cap_reset();
        ua.cap_alt = (uint8_t)alt;
        if (alt)
            ua_cap_config();
        else {
            sie_wr(S_INDEX, 2);
            sie_wr(S_TXCSR1, 0x48);
            ua_tx_bytes = ua_tx_slot = 0;
        }
    }
    if (alt) {
        ua_tx_fill();                           /* ready for the host's first IN token */
        ua_tx_prepare();
    }
    return 1;
}

/* Standard master mute and signed-dB volume on playback's Feature Unit. */
static int ua_feature_setup(const uint8_t *s)
{
    uint32_t selector = s[3], bytes = selector == 1u ? 1u : 2u;
    int16_t value;
    if (!usb.config || ua_if_play == UA_NO_IF || s[4] != UA_IF_CONTROL || s[5] != UA_FEATURE_PLAY ||
        (s[2] != 0u && s[2] != 0xFFu) || (selector != 1u && selector != 2u) || s[6] != bytes || s[7])
        return 0;
    if (s[0] == 0x21u && s[1] == 1u) {       /* SET_CUR: master mute or volume */
        ua_feature_pending = (uint8_t)selector;
        sie_wr(S_INDEX, 0);
        sie_wr(S_CSR0, 0x40);
        return 1;
    }
    if (s[0] != 0xA1u) return 0;
    if (selector == 1u) {
        if (s[1] != 0x81u) return 0;         /* mute has only CUR */
        value = ua_host_mute;
    } else switch (s[1]) {
    case 0x81: value = ua_host_volume_q8; break;
    case 0x82: value = UA_VOLUME_MIN; break;
    case 0x83: value = 0; break;
    case 0x84: value = 256; break;            /* one dB, signed 1/256-dB units */
    default: return 0;
    }
    ua_feature_reply[0] = (uint8_t)value;
    ua_feature_reply[1] = (uint8_t)((uint16_t)value >> 8);
    e0_send(ua_feature_reply, bytes, bytes);
    return 1;
}

/* Endpoint rate control remains available alongside interface feature controls.
 * Depth is selected by the AS alternate (1 = PCM16, 2 = packed PCM24). */
static int ua_control_setup(const uint8_t *s)
{
    uint32_t rate;
    if (s[0] == 0x21u || s[0] == 0xA1u) return ua_feature_setup(s);
    if (!usb.config || s[2] || s[3] != 1u || s[5] || s[6] != 3u || s[7] ||
        (s[4] != 0x02u && s[4] != 0x82u) || (s[4] == 0x02u ? ua_if_play : ua_if_cap) == UA_NO_IF)
        return 0;                            /* (an endpoint of a function left out) */
    if (s[0] == 0x22u && s[1] == 1u) {       /* SET_CUR: three-byte OUT stage */
        ua_rate_pending = s[4];
        sie_wr(S_INDEX, 0);
        sie_wr(S_CSR0, 0x40);
        return 1;
    }
    if (s[0] != 0xA2u)
        return 0;
    switch (s[1]) {
    case 0x81:                               /* GET_CUR */
    case 0x82:                               /* GET_MIN */
    case 0x83: rate = UA_RATE; break;          /* GET_MAX */
    case 0x84: rate = 0; break;                /* GET_RES: fixed frequency */
    default: return 0;
    }
    ua_rate_reply[0] = (uint8_t)rate;
    ua_rate_reply[1] = (uint8_t)(rate >> 8);
    ua_rate_reply[2] = (uint8_t)(rate >> 16);
    e0_send(ua_rate_reply, 3, 3);
    return 1;
}

static int ua_control_data(const uint8_t *p, uint32_t n)
{
    uint8_t ep = ua_rate_pending;
    uint8_t feature = ua_feature_pending;
    ua_rate_pending = 0;
    ua_feature_pending = 0;
    if (feature) {
        if (!usb.config || ua_if_play == UA_NO_IF) return 0;
        if (feature == 1u) {
            if (n != 1u || p[0] > 1u) return 0;
            ua_host_mute = p[0];
        } else {
            if (n != 2u) return 0;
            ua_host_volume_set((int16_t)(p[0] | (uint16_t)p[1] << 8));
        }
        return 1;
    }
    if (!usb.config || !ep || n != 3u)
        return 0;
    /* UAC1 rounds unsupported values to the closest discrete rate: 44100.
     * Do not flush an already running stream for this no-op control. */
    return 1;
}

static void ua_hw_poll(void)
{
    uint32_t csr, n, f;
    if (!usb.config)
        return;
    if (usb.suspended) {
        if (!ua_paused) {
            ua_play_reset();
            ua_cap_reset();
            if (ua.play_alt)
                ua_play_config();
            if (ua.cap_alt)
                ua_cap_config();
            ua_paused = 1;
            ua_frame_valid = 0;
        }
        return;
    }
    if (ua_paused) {
        if (ua.play_alt)
            ua_play_config();
        if (ua.cap_alt)
            ua_cap_config();
        ua_paused = 0;
    }
    if (!ua.play_alt && !ua.cap_alt) {
        ua_frame_valid = 0;                     /* idle frames are not missed ones */
        return;
    }
    ua_tx_fill();                              /* arm IN before unpacking playback */
    if (ua.play_alt) {
        sie_wr(S_INDEX, 2);
        csr = sie_rd(S_RXCSR1);
        if (csr & 1u) {
            n = sie_rd(S_RXCOUNT1) | sie_rd(S_RXCOUNT2) << 8;
            fm1_usb_rx_sync();
            if (csr & 0x0Cu)
                ua.bad_packets++;
            else
                ua_receive(ua_rx, n);
            sie_wr(S_RXCSR1, 0x10);             /* discard errors, release DMA packet */
        }
    }
    /* FRAME1 may roll over between reads: retry with a stable high byte. */
    do {
        n = sie_rd(S_FRAME2) & 7u;
        f = sie_rd(S_FRAME1);
    } while (n != (sie_rd(S_FRAME2) & 7u) && usb.up);
    f |= n << 8;
    if (!ua_frame_valid || f != ua_frame) {
        if (ua_frame_valid) {
            uint32_t gap = (f - ua_frame) & 2047u;
            if (gap > 1u)
                ua.missed_frames += gap - 1u;
        }
        ua_frame = (uint16_t)f;
        ua_frame_valid = 1;
        ua_sof();
    }
    ua_tx_fill();
    ua_tx_prepare();
}
