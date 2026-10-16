/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* UAC1 stereo playback and selectable stereo master / four-track capture.
 * Callers serialize USB service and each short audio block. Capture precedes
 * computer playback, so DAW monitoring cannot feed itself through capture.
 *
 * The I2S clock is independent of USB SOF. A low-pass ring-fill servo adjusts
 * capture packet lengths and the playback endpoint's explicit 10.14 feedback.
 * USB and I2S both run at the native 44.1 kHz rate; no resampler is needed. */
#include <stdint.h>
#define UA_RATE 44100u
#define UA_CAP_CHANNELS 4u                    /* maximum; active stride follows the enumerated mode */
static uint8_t ua_cap_channels = 2u;
#define UA_MAX_FRAMES 45u                      /* ceil(44.1 + maximum feedback correction) */
#define UA_PLAY_PACKET (UA_MAX_FRAMES * 2u * 3u)
#define UA_PACKET (UA_MAX_FRAMES * UA_CAP_CHANNELS * 3u)
#define UA_RING 1024u
#define UA_TARGET 512u
#define UA_NOMINAL ((UA_RATE * 16384u) / 1000u)
#ifdef __APPLE__
#define UA_POOL                                 /* (the host tests: Mach-O has no such section) */
#else
#define UA_POOL __attribute__((section(".pool")))   /* the rings, 12 KiB: the pool (zeroed at boot), not RAM */
#endif

static struct {
    uint8_t play_alt, cap_alt, play_ready, cap_ready;
    uint32_t pw, pr, cw, cr, cap_frac;
    int32_t play_fill_q8, cap_fill_q8;
    uint32_t play_underruns, play_overruns, cap_underruns, cap_overruns, bad_packets;
    uint32_t rx_packets, tx_packets, missed_frames;
    uint32_t poll_max_ticks, service_max_ticks;
    int16_t play[UA_RING * 2u], cap[UA_RING * UA_CAP_CHANNELS];
} ua UA_POOL;

#define UA_VOLUME_MIN (-64 * 256)
/* Standard UAC1 volume: signed 1/256 dB, -64..0 dB in one-dB steps.
 * Q15 gain table: round(32768 * 10^(-dB/20)); no floating point in USB service. */
static const uint16_t ua_volume_gain[65] = {
    32768, 29205, 26029, 23198, 20675, 18427, 16423, 14637,
    13045, 11627, 10362, 9235, 8231, 7336, 6538, 5827,
    5193, 4629, 4125, 3677, 3277, 2920, 2603, 2320,
    2068, 1843, 1642, 1464, 1305, 1163, 1036, 924,
    823, 734, 654, 583, 519, 463, 413, 368,
    328, 292, 260, 232, 207, 184, 164, 146,
    130, 116, 104, 92, 82, 73, 65, 58,
    52, 46, 41, 37, 33, 29, 26, 23,
    21,
};
static int16_t ua_host_volume_q8;
static uint8_t ua_host_mute;
static int32_t ua_host_target_q15 = 32768, ua_host_gain_q15 = 32768;

static void ua_host_volume_reset(void)
{
    ua_host_volume_q8 = ua_host_mute = 0;
    ua_host_target_q15 = ua_host_gain_q15 = 32768;
}

static void ua_host_volume_set(int16_t volume)
{
    if (volume == (int16_t)0x8000) {         /* UAC1 CUR's explicit -infinity code */
        ua_host_volume_q8 = volume;
        ua_host_target_q15 = 0;
        return;
    }
    int32_t attenuation = volume >= 0 ? 0 : volume <= UA_VOLUME_MIN ? 64 : (-(int32_t)volume + 128) / 256;
    ua_host_volume_q8 = (int16_t)(-attenuation * 256);
    ua_host_target_q15 = ua_volume_gain[attenuation];
}

static int32_t ua_clip(int32_t x)
{
    return x > 32767 ? 32767 : x < -32768 ? -32768 : x;
}

static uint32_t ua_sample_bytes(uint8_t alt) { return alt == 2u ? 3u : 2u; }

/* Packed little-endian PCM24 uses the same full-scale level as PCM16. The
 * low byte is discarded on playback; recording pads it with zero. */
static int16_t ua_decode(const uint8_t *p, uint32_t width)
{
    p += width - 2u;
    return (int16_t)(p[0] | (uint16_t)p[1] << 8);
}

static void ua_encode(uint8_t *p, uint32_t width, int16_t sample)
{
    if (width == 3u)
        *p++ = 0;
    p[0] = (uint8_t)sample;
    p[1] = (uint8_t)((uint16_t)sample >> 8);
}

static void ua_play_reset(void)
{
    ua.pw = ua.pr = 0;
    ua.play_ready = 0;
    ua_host_gain_q15 = ua_host_mute ? 0 : ua_host_target_q15;
    ua.play_fill_q8 = UA_TARGET * 256;
}

static void ua_cap_reset(void)
{
    ua.cw = ua.cr = ua.cap_frac = 0;
    ua.cap_ready = 0;
    ua.cap_fill_q8 = UA_TARGET * 256;
}

static void ua_reset(void)
{
    ua.play_alt = ua.cap_alt = 0;
    ua_play_reset();
    ua_cap_reset();
}

static void ua_receive(const uint8_t *p, uint32_t bytes)
{
    uint32_t i, width = ua_sample_bytes(ua.play_alt), frame = width * 2u;
    uint32_t n = bytes / frame;
    if (!ua.play_alt)
        return;
    if (n > UA_MAX_FRAMES || bytes % frame) {
        ua.bad_packets++;
        return;
    }
    if (ua.pw - ua.pr + n > UA_RING) {
        ua.play_overruns++;
        ua_play_reset();                       /* re-prime, never replay stale data */
    }
    for (i = 0; i < n; i++) {
        uint32_t at = (ua.pw & (UA_RING - 1u)) * 2u;
        ua.play[at] = ua_decode(p + frame * i, width);
        ua.play[at + 1u] = ua_decode(p + frame * i + width, width);
        ua.pw++;
    }
    ua.rx_packets++;
}

/* Called once per observed USB frame, not once per poll or audio callback. */
static void ua_sof(void)
{
    if (ua.play_ready)
        ua.play_fill_q8 += ((int32_t)(ua.pw - ua.pr) * 256 - ua.play_fill_q8) / 32;
    if (ua.cap_ready)
        ua.cap_fill_q8 += ((int32_t)(ua.cw - ua.cr) * 256 - ua.cap_fill_q8) / 32;
}

static uint32_t ua_rate(int32_t error_q8)
{
    int32_t correction = error_q8 / 16;         /* fill error / 1024, in 10.14 */
    if (correction > 8192)
        correction = 8192;
    if (correction < -8192)
        correction = -8192;
    return (uint32_t)((int32_t)UA_NOMINAL + correction);
}

static uint32_t ua_feedback(void)
{
    return ua_rate(UA_TARGET * 256 - ua.play_fill_q8);
}

static uint32_t ua_transmit(uint8_t *p)
{
    uint32_t i, n, take = 0, width = ua_sample_bytes(ua.cap_alt);
    ua.cap_frac += ua_rate(ua.cap_fill_q8 - UA_TARGET * 256);
    n = ua.cap_frac >> 14;
    ua.cap_frac &= 16383u;
    if (!ua.cap_ready && ua.cw - ua.cr >= UA_TARGET)
        ua.cap_ready = 1;
    if (ua.cap_ready) {
        if (ua.cw - ua.cr >= n)
            take = 1;
        else {
            ua.cap_underruns++;
            ua_cap_reset();
        }
    }
    for (i = 0; i < n; i++) {
        int16_t sample[UA_CAP_CHANNELS] = {0};
        uint32_t ch;
        if (take) {
            uint32_t at = (ua.cr++ & (UA_RING - 1u)) * ua_cap_channels;
            for (ch = 0; ch < ua_cap_channels; ch++)
                sample[ch] = ua.cap[at + ch];
        }
        for (ch = 0; ch < ua_cap_channels; ch++)
            ua_encode(p + (i * ua_cap_channels + ch) * width, width, sample[ch]);
    }
    return n * width * ua_cap_channels;
}

/* Stereo Q15 post-FX/master/limiter mix, or four pre-master mono stems.
 * Stereo copies the finished synth output; playback follows MASTER and the
 * host's standard volume/mute controls, with a short gain ramp.
 * Call in blocks of CTL (32), with USB service excluded during this copy.
 * In track mode a null stem pointer skips capture for a block collected while
 * capture was disabled. Stereo requires no stems or additional DSP pass. */
static void ua_audio(int32_t *out, const int32_t *tracks, uint32_t n, uint32_t master_q12)
{
    uint32_t i, capture = ua.cap_alt && (ua_cap_channels == 2u || tracks), playback = 0;
    int32_t gain = ua_host_gain_q15, target = ua_host_mute ? 0 : ua_host_target_q15;
    uint32_t play_gain_q15 = (master_q12 * (uint32_t)gain) >> 12;
    if (capture && ua.cw - ua.cr + n > UA_RING) {
        ua.cap_overruns++;
        ua_cap_reset();
    }
    if (ua.play_alt) {
        if (!ua.play_ready && ua.pw - ua.pr >= UA_TARGET)
            ua.play_ready = 1;
        if (ua.play_ready) {
            if (ua.pw - ua.pr >= n)
                playback = 1;
            else {
                ua.play_underruns++;
                ua_play_reset();
            }
        }
    }
    for (i = 0; i < n; i++) {
        uint32_t ci = ((ua.cw + i) & (UA_RING - 1u)) * ua_cap_channels;
        uint32_t pi = ((ua.pr + i) & (UA_RING - 1u)) * 2u;
        int32_t l = out[2u * i], r = out[2u * i + 1u];
        if (capture) {
            uint32_t ch;
            if (ua_cap_channels == 2u) {
                ua.cap[ci] = (int16_t)l;
                ua.cap[ci + 1u] = (int16_t)r;
            } else
                for (ch = 0; ch < UA_CAP_CHANNELS; ch++)
                    ua.cap[ci + ch] = (int16_t)ua_clip(tracks[i * UA_CAP_CHANNELS + ch]);
        }
        if (playback) {
            if (gain != target) {            /* at most 128 samples (~3 ms) for a full-scale change */
                int32_t delta = target - gain;
                gain += delta > 256 ? 256 : delta < -256 ? -256 : delta;
                play_gain_q15 = (master_q12 * (uint32_t)gain) >> 12;
            }
            l += (ua.play[pi] * (int32_t)play_gain_q15) >> 15;
            r += (ua.play[pi + 1u] * (int32_t)play_gain_q15) >> 15;
            out[2u * i] = ua_clip(l);
            out[2u * i + 1u] = ua_clip(r);
        }
    }
    if (capture)
        ua.cw += n;
    if (playback)
        ua.pr += n;
    ua_host_gain_q15 = playback ? gain : target; /* opening a muted stream never starts at unity */
}
