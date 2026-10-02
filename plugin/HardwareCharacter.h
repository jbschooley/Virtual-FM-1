// HardwareCharacter -- the FM-1's output as heard over its USB audio, applied
// after the plugin's engine and effects. Measured on FM-1_093 (docs/HARDWARE-NOTES.md):
//
//  - 16 bits, at the level of the FM-1 at full volume, which is 2.5 dB below
//    the plugin's own (rounded; the FM-1 sends exact silence between notes),
//    or as far below full as setVolumeDb says: the plugin's level stays the
//    same, and the rounding is as coarse as the quieter FM-1's would be once
//    brought back up (the grain heard on decays and tails at lower volumes)
//  - a gentle high-frequency roll-off: about 1 dB at 6-10 kHz, 2 dB at
//    10-14 kHz, 3.5 dB at 14-20 kHz below the plugin, which a one-pole
//    low-pass fits (at 44.1 kHz, coefficient for 11.4 kHz)
//  - at host rates above 44.1 kHz, a 4th-order low-pass at 20 kHz, toward the
//    bandwidth of 44.1 kHz audio (it rolls off what lies above; it is not a brick wall)
//
// Not included: the USB stream's occasional dropped or repeated blocks, which
// are a transport fault rather than the synth's sound, and anything inside the
// engine that only the firmware source can tell (docs/FIRMWARE-GAPS.md).

#pragma once

#include <atomic>
#include <cmath>

class HardwareCharacter {
public:
    void prepare(double sampleRate) {
        sr_ = sampleRate;
        // the one-pole whose loss at 12 kHz equals the FM-1's (the 44.1 kHz fit)
        const double target = lossDb(coefficientFor(11400.0, 44100.0), 12000.0, 44100.0);
        double lo = 0.0001, hi = 0.9999;
        for (int i = 0; i < 60; ++i) {
            double mid = 0.5 * (lo + hi);
            (lossDb(mid, 12000.0, sr_) > target ? lo : hi) = mid;   // a larger coefficient loses less
        }
        a_ = float(0.5 * (lo + hi));
        // above 44.1 kHz: a 4th-order Butterworth low-pass at 20 kHz (two biquads)
        band_ = sr_ > 46000.0;
        if (band_) {
            const double q[2] = {0.54119610, 1.30656296};
            for (int k = 0; k < 2; ++k) design(bq_[k], 20000.0, q[k]);
        }
        reset();
    }
    // How far below full the FM-1's volume is (0..-40 dB). Any thread; read per block.
    void setVolumeDb(float db) { volumeDb_.store(db); }

    void reset() {
        y_ = 0.0f;
        for (auto& b : bq_) b.z1 = b.z2 = 0.0;
    }
    void process(float* x, int n) {
        // 16-bit steps at the FM-1's level: full volume is 2.5 dB below the plugin
        const float level = kLevel * std::pow(10.0f, -volumeDb_.load() / 20.0f);
        const float scale = 32768.0f / level;
        for (int i = 0; i < n; ++i) {
            float v = x[i];
            if (band_)
                for (auto& b : bq_) {
                    double out = b.b0 * v + b.z1;
                    b.z1 = b.b1 * v - b.a1 * out + b.z2;
                    b.z2 = b.b2 * v - b.a2 * out;
                    v = float(out);
                }
            y_ += a_ * (v - y_);
            // 16 bits at the FM-1's level, clipping where it would
            float s = std::round(y_ * scale);
            s = s > 32767.0f ? 32767.0f : (s < -32768.0f ? -32768.0f : s);
            x[i] = s / scale;
        }
    }

private:
    static constexpr float kLevel = 1.3335f;   // 10^(2.5/20): plugin level of FM-1 full scale at full volume
    struct Biquad { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0; };

    static double coefficientFor(double fc, double sr) { return 1.0 - std::exp(-2.0 * 3.141592653589793 * fc / sr); }
    static double lossDb(double a, double f, double sr) {
        double w = 2.0 * 3.141592653589793 * f / sr;
        double re = 1.0 - (1.0 - a) * std::cos(w), im = (1.0 - a) * std::sin(w);
        return -20.0 * std::log10(a / std::sqrt(re * re + im * im));
    }
    void design(Biquad& b, double fc, double q) {
        double w = 2.0 * 3.141592653589793 * fc / sr_, cw = std::cos(w), alpha = std::sin(w) / (2.0 * q);
        double a0 = 1.0 + alpha;
        b.b0 = (1.0 - cw) / 2.0 / a0;
        b.b1 = (1.0 - cw) / a0;
        b.b2 = (1.0 - cw) / 2.0 / a0;
        b.a1 = -2.0 * cw / a0;
        b.a2 = (1.0 - alpha) / a0;
    }

    double sr_ = 44100.0;
    float a_ = 1.0f, y_ = 0.0f;
    bool band_ = false;
    std::atomic<float> volumeDb_{0.0f};
    Biquad bq_[2];
};
