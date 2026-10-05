// RateConverter -- streaming sample-rate conversion with a windowed-sinc filter,
// for running an engine at the FM-1's 44.1 kHz inside a host at another rate
// (Hardware character). Mono, float. Not real-time-allocating after prepare().
//
// The filter cuts off just below the lower of the two Nyquist frequencies, so
// converting 44.1 kHz up to 48 or 96 kHz adds nothing above 22 kHz, like
// playing a 44.1 kHz recording in a higher-rate session. Latency: kHalf input
// samples (about 0.36 ms at 44.1 kHz).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <vector>

class RateConverter {
public:
    static constexpr int kHalf = 16;          // taps each side of the centre
    static constexpr int kPhases = 512;       // kernel table resolution between input samples

    // inRate: the engine's rate; outRate: the host's (both rounded to whole Hz). maxOut: the largest block
    // process() will be asked for.
    void prepare(double inRate, double outRate, int maxOut) {
        // the read position moves by in/out input samples per output, kept as an
        // exact fraction (in units of 1/den_), so the output never depends on
        // how the stream is cut into blocks
        const int64_t in = std::llround(inRate), out = std::llround(outRate), g = std::gcd(in, out);
        num_ = in / g;
        den_ = out / g;
        step_ = double(num_) / double(den_);
        // cut off at 97% of the lower Nyquist, in units of the input rate
        const double cutoff = 0.97 * 0.5 * std::min(1.0, outRate / inRate);
        table_.assign(size_t(kPhases + 1) * size_t(2 * kHalf), 0.0f);
        for (int ph = 0; ph <= kPhases; ++ph) {
            const double frac = double(ph) / kPhases;
            for (int k = 0; k < 2 * kHalf; ++k) {
                const double x = double(k - kHalf + 1) - frac;   // distance from the output time
                table_[size_t(ph) * size_t(2 * kHalf) + size_t(k)] = float(2.0 * cutoff * sinc(2.0 * cutoff * x) * window(x / kHalf));
            }
        }
        hist_.assign(size_t(maxInputFor(maxOut) + 4 * kHalf + 8), 0.0f);
        reset();
    }

    void reset() {
        std::fill(hist_.begin(), hist_.end(), 0.0f);
        have_ = 2 * kHalf - 1;   // a run of silence before the first sample
        pos_ = int64_t(kHalf - 1) * den_;
    }

    // How many input samples the next process() of nOut outputs needs.
    int inputNeeded(int nOut) const {
        const int64_t last = (pos_ + int64_t(nOut - 1) * num_) / den_;
        const int need = int(last) + kHalf + 1 - have_;
        return std::max(0, need);
    }

    // The largest inputNeeded() can be for nOut outputs.
    int maxInputFor(int nOut) const { return int(std::ceil(double(nOut) * step_)) + 2; }

    // Latency in output samples.
    double latencyOut() const { return double(kHalf) / step_; }

    // in: exactly inputNeeded(nOut) samples; out: nOut samples.
    void process(const float* in, int nIn, float* out, int nOut) {
        std::copy(in, in + nIn, hist_.begin() + have_);
        have_ += nIn;
        for (int i = 0; i < nOut; ++i) {
            const int base = int(pos_ / den_);
            const double frac = double(pos_ % den_) / double(den_);
            const double fp = frac * kPhases;
            const int ph = int(fp);
            const float t = float(fp - ph);
            const float* k0 = &table_[size_t(ph) * size_t(2 * kHalf)];
            const float* k1 = k0 + 2 * kHalf;
            const float* x = &hist_[size_t(base - kHalf + 1)];
            float acc = 0.0f;
            for (int k = 0; k < 2 * kHalf; ++k) acc += x[k] * (k0[k] + t * (k1[k] - k0[k]));
            out[i] = acc;
            pos_ += num_;
        }
        // drop what no later output needs
        const int keepFrom = int(pos_ / den_) - kHalf + 1;
        if (keepFrom > 0) {
            std::copy(hist_.begin() + keepFrom, hist_.begin() + have_, hist_.begin());
            have_ -= keepFrom;
            pos_ -= int64_t(keepFrom) * den_;
        }
    }

private:
    static double sinc(double x) { return std::fabs(x) < 1e-9 ? 1.0 : std::sin(3.141592653589793 * x) / (3.141592653589793 * x); }
    static double window(double x) {   // Blackman, x in -1..1
        if (std::fabs(x) >= 1.0) return 0.0;
        const double a = 3.141592653589793 * (x + 1.0);
        return 0.42 - 0.5 * std::cos(a) + 0.08 * std::cos(2.0 * a);
    }

    int64_t num_ = 1, den_ = 1;   // input samples per output sample: num_/den_
    double step_ = 1.0;
    std::vector<float> table_, hist_;
    int have_ = 0;
    int64_t pos_ = 0;             // the next output's time, in input samples x den_
};
