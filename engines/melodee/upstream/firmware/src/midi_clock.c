/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Adapted from MIDI clock contributions by ChanceTheMaker and keremimo (2026).
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Audio ISR state. Musical phase is exactly 1024 units per MIDI pulse; changing
 * the measured tempo cannot move steps, gates, ties or the arpeggiator. */
static struct {
    uint32_t pos, rendered, last_ms, start_ms, last_time, interval_time, tempo_time;
    uint8_t mode, selected, have_pulse, tempo_valid, tempo_n;
} midi_clock;

/* Preserve fractional sequencer/ARP phase when external transport takes over. */
static void midi_clock_mode(uint32_t mode)
{
    uint32_t old = seq_beat_samples();
    midi_sync_active = (uint8_t)mode;
    uint32_t next = seq_beat_samples();
    for (uint32_t i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        if (t->seq_pos < 0x7FFFFFFFu) t->seq_pos = (uint32_t)((uint64_t)t->seq_pos * next / old);
        if (t->arp_pos < 0xFFFFFFFu) t->arp_pos = (uint32_t)((uint64_t)t->arp_pos * next / old);
        t->arp_off = (uint32_t)((uint64_t)t->arp_off * next / old);
    }
    beat_pos = (uint32_t)((uint64_t)beat_pos * next / old);
    midi_clock.mode = (uint8_t)mode;
}

static __attribute__((noinline)) void midi_clock_transport(uint32_t status, uint32_t ms)
{
    if (status == 0xFAu) {
        midi_clock.pos = midi_clock.rendered = 0;
        midi_clock.have_pulse = midi_clock.tempo_valid = 0;
        midi_clock.interval_time = 0;
        midi_clock.start_ms = ms;
        seq_start();
    } else if (status == 0xFBu) {
        cin_left = 0;
        midi_clock.pos = midi_clock.rendered;
        midi_clock.have_pulse = midi_clock.tempo_valid = 0;
        midi_clock.interval_time = 0;
        midi_clock.start_ms = ms;
        if (!song.playing) {
            motion_begin();
            song.rec_wait = song.rec;
            song.rec_pass = 0;
        }
        song.playing = 1;
    } else if (status == 0xFCu) {
        seq_stop();
    }
}

static __attribute__((noinline)) void midi_clock_pulse(uint32_t ms, uint32_t time)
{
    if (!midi_clock.tempo_valid) {
        midi_clock.tempo_valid = 1;
        midi_clock.tempo_time = time;
        midi_clock.tempo_n = 0;
    } else if (++midi_clock.tempo_n == 6u) {
        uint32_t dt = time - midi_clock.tempo_time;
        midi_clock.tempo_time = time;
        midi_clock.tempo_n = 0;
        if (dt >= 62u * MIDI_TICKS_PER_MS && dt <= 375u * MIDI_TICKS_PER_MS) {
            midi_beat_samples = (uint32_t)(((uint64_t)FS * dt) / (250u * MIDI_TICKS_PER_MS));
            song.g[G_BPM] = (int16_t)clamp((int32_t)((15000u * MIDI_TICKS_PER_MS + dt / 2u) / dt), 40, 240);
        }
    }
    if (song.playing) {
        if (midi_clock.have_pulse) {
            uint32_t interval = time - midi_clock.last_time;
            if (interval >= 8u * MIDI_TICKS_PER_MS && interval <= 80u * MIDI_TICKS_PER_MS)
                midi_clock.interval_time = interval;
            midi_clock.pos += MIDI_BEAT_UNITS / 24u;
        }
        midi_clock.have_pulse = 1;
    }
    if (!midi_clock.interval_time)
        midi_clock.interval_time = 60000u * MIDI_TICKS_PER_MS / ((uint32_t)song.g[G_BPM] * 24u);
    midi_clock.last_ms = ms;
    midi_clock.last_time = time;
}

static uint32_t midi_clock_advance(uint32_t now)
{
    uint32_t target, elapsed, offset, n;
    if (!midi_clock.have_pulse)
        return 0;
    elapsed = now - midi_clock.last_time;
    /* The render timeline can precede a freshly received pulse. */
    if ((int32_t)elapsed < 0)
        elapsed = 0;
    if (elapsed > midi_clock.interval_time)
        elapsed = midi_clock.interval_time;
    offset = elapsed * (MIDI_BEAT_UNITS / 24u) / midi_clock.interval_time;
    if (offset >= MIDI_BEAT_UNITS / 24u)
        offset = MIDI_BEAT_UNITS / 24u - 1u;
    target = midi_clock.pos + offset;
    n = (int32_t)(target - midi_clock.rendered) > 0 ? target - midi_clock.rendered : 0u;
    midi_clock.rendered += n;
    return n;
}
