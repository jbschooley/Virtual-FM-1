#include "Effects.h"

#include <cmath>

void Effects::prepare(double sampleRate, int maxBlock) {
    sr_ = sampleRate;
    maxBlock_ = std::max(1, maxBlock);
    juce::dsp::ProcessSpec spec{sampleRate, juce::uint32(std::max(1, maxBlock)), 1};
    filter_.prepare(spec);
    delay_.prepare(spec);
    delay_.setMaximumDelayInSamples(int(sampleRate * 1.0) + 1);
    chorus_.prepare(spec);
    phaser_.prepare(spec);
    reverb_.setSampleRate(sampleRate);
    scratch_.setSize(1, std::max(1, maxBlock));
    reset();
}

void Effects::reset() {
    filter_.reset();
    delay_.reset();
    chorus_.reset();
    phaser_.reset();
    reverb_.reset();
    toneState_ = 0.0f;
}

void Effects::setChain(const fm1::FxChain& c) {
    const juce::SpinLock::ScopedLockType l(pendingLock_);
    pending_ = c;
    hasPending_ = true;
}

void Effects::applyPending() {
    {
        const juce::SpinLock::ScopedTryLockType l(pendingLock_);
        if (!l.isLocked() || !hasPending_) return;
        chain_ = pending_;
        hasPending_ = false;
    }
    const auto& f = chain_.fx[fm1::FxFilter];
    filter_.setType(f.type == 1 ? juce::dsp::StateVariableTPTFilterType::bandpass
                  : f.type == 2 ? juce::dsp::StateVariableTPTFilterType::highpass
                                : juce::dsp::StateVariableTPTFilterType::lowpass);
    float cutoff = 100.0f * std::pow(200.0f, float(f.p[0]) / 107.0f);   // 100 Hz .. 20 kHz
    filter_.setCutoffFrequency(std::min(cutoff, float(sr_ * 0.45)));
    filter_.setResonance(0.7f + float(f.p[1]) * 0.9f);                   // Q 0.7 .. 9.7

    const auto& r = chain_.fx[fm1::FxReverb];
    juce::Reverb::Parameters rp;
    float base = r.type == 0 ? 0.35f : r.type == 1 ? 0.6f : 0.5f;         // room / hall / plate
    rp.roomSize = base + 0.4f * float(r.p[0]) / 100.0f;
    rp.damping = r.type == 2 ? 0.2f : 0.5f;
    rp.width = 1.0f;
    rp.wetLevel = float(r.p[1]) / 100.0f;
    rp.dryLevel = 1.0f - 0.5f * rp.wetLevel;
    reverb_.setParameters(rp);

    const auto& d = chain_.fx[fm1::FxDelay];
    delaySamples_ = float(sr_ * (0.8 - 0.7 * double(d.p[1]) / 100.0));   // 0.8 s .. 0.1 s

    const auto& c = chain_.fx[fm1::FxChorus];
    chorus_.setRate(0.1f + 0.9f * float(c.p[0]) / 100.0f);
    chorus_.setDepth(float(c.p[1]) / 100.0f);
    chorus_.setCentreDelay(7.0f);
    chorus_.setFeedback(0.0f);
    chorus_.setMix(float(c.p[2]) / 100.0f);

    const auto& p = chain_.fx[fm1::FxPhaser];
    phaser_.setRate(0.5f + 5.5f * float(p.p[0]) / 100.0f);
    phaser_.setDepth(float(p.p[1]) / 100.0f);
    phaser_.setCentreFrequency(800.0f);
    phaser_.setFeedback(0.3f);
    phaser_.setMix(float(p.p[2]) / 100.0f);
}

void Effects::process(float* buf, int n) {
    applyPending();
    // JUCE's chorus and phaser keep buffers sized for the block announced in
    // prepare(): a host that sends a larger block gets it in pieces
    for (int start = 0; start < n; start += maxBlock_) {
        const int m = std::min(maxBlock_, n - start);
        for (int k = 0; k < fm1::kEffects; ++k) {
            int e = chain_.order[size_t(k)];
            if (e >= 0 && e < fm1::kEffects && chain_.fx[size_t(e)].on) processOne(e, buf + start, m);
        }
    }
}

void Effects::processOne(int effect, float* buf, int n) {
    switch (effect) {
    case fm1::FxFilter:
        for (int i = 0; i < n; ++i) buf[i] = filter_.processSample(0, buf[i]);
        break;
    case fm1::FxReverb:
        reverb_.processMono(buf, n);
        break;
    case fm1::FxDelay: {
        const auto& d = chain_.fx[fm1::FxDelay];
        float fb = float(d.p[0]) / 100.0f * 0.95f, mix = float(d.p[2]) / 100.0f;
        for (int i = 0; i < n; ++i) {
            float wet = delay_.popSample(0, delaySamples_);
            delay_.pushSample(0, buf[i] + wet * fb);
            buf[i] = buf[i] * (1.0f - 0.5f * mix) + wet * mix;
        }
        break;
    }
    case fm1::FxDistortion: {
        const auto& d = chain_.fx[fm1::FxDistortion];
        float drive = 1.0f + 30.0f * float(d.p[0]) / 100.0f;
        float level = float(d.p[2]) / 100.0f;
        float toneHz = 500.0f * std::pow(40.0f, float(d.p[1]) / 100.0f);   // 500 Hz .. 20 kHz
        float a = std::exp(-2.0f * juce::MathConstants<float>::pi * toneHz / float(sr_));
        for (int i = 0; i < n; ++i) {
            float x = buf[i] * drive, y;
            if (d.type == 0) y = std::tanh(x);
            else if (d.type == 1) y = juce::jlimit(-1.0f, 1.0f, x);
            else { y = std::fmod(x + 1.0f, 4.0f); if (y < 0) y += 4.0f; y = y < 2.0f ? y - 1.0f : 3.0f - y; }
            toneState_ = a * toneState_ + (1.0f - a) * y;
            buf[i] = toneState_ * level / std::sqrt(drive) * 3.0f;
        }
        break;
    }
    case fm1::FxChorus: {
        juce::dsp::AudioBlock<float> block(&buf, 1, size_t(n));
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        chorus_.process(ctx);
        break;
    }
    case fm1::FxPhaser: {
        juce::dsp::AudioBlock<float> block(&buf, 1, size_t(n));
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        phaser_.process(ctx);
        break;
    }
    default: break;
    }
}
