/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#include "audio_output.h"
#include "fm1_engine_control.h"
#include "fm1_audio_io.h"

static unsigned output_gain;
static int32_t usb_pcm[FM1_BLOCK_WORDS] __attribute__((aligned(32)));
static int16_t bluetooth_pcm[FM1_BLOCK_WORDS] __attribute__((aligned(32)));

void fm1_output_reset(void) { output_gain = 0; }
static inline __attribute__((always_inline)) int32_t limit(int64_t value)
{
    return value > 8388607 ? 8388607 : value < -8388608 ? -8388608 : (int32_t)value;
}
FM1_AUDIO_CODE void fm1_output_mix(int32_t *output)
{
    if (!output) return;
    unsigned target = fm1_engine_output_level();
    if (target > 127U) target = 127U;
    if (output_gain < target) output_gain += target - output_gain > 8U ? 8U : target - output_gain;
    else if (output_gain > target) output_gain -= output_gain - target > 8U ? 8U : output_gain - target;

    uint16_t usb_gain[2] = {0, 0};
    int usb_frames = fm1_usb_audio_read(usb_pcm, FM1_BLOCK_FRAMES, usb_gain);
    unsigned left = ((unsigned)usb_gain[0] * output_gain) >> 7;
    unsigned right = ((unsigned)usb_gain[1] * output_gain) >> 7;
    /* Accumulate USB before clipping, then add Bluetooth.
     * Discard the block if the USB stream resets. */
    for (unsigned i = 0; i < FM1_BLOCK_WORDS; ++i) {
        int64_t value = ((int64_t)output[i] * output_gain) >> 7;
        if (usb_frames == FM1_BLOCK_FRAMES)
            value += ((int64_t)usb_pcm[i] * ((i & 1U) ? right : left)) >> 7;
        output[i] = usb_frames == -2 ? 0 : limit(value);
    }
    if (fm1_bluetooth_audio_read(bluetooth_pcm, FM1_BLOCK_FRAMES) == FM1_BLOCK_FRAMES) {
        for (unsigned i = 0; i < FM1_BLOCK_WORDS; ++i)
            output[i] = limit((int64_t)output[i] + (int32_t)bluetooth_pcm[i] * (int32_t)(output_gain * 2U));
    }
    (void)fm1_usb_audio_write(output, FM1_BLOCK_FRAMES);
}
