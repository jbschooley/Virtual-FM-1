// Effects -- the FM-1's six effects, approximated with juce::dsp: filter,
// reverb, delay, distortion, chorus, phaser, in the chain order the sound's
// record gives. Mono in, mono out. Parameter scales follow the FM-1+VA manual
// (cutoff 100 Hz to 20 kHz over 0..107, delay 0.8 s to 0.1 s over 0..100,
// chorus 0.1 to 1 Hz, phaser 0.5 to 6 Hz). These are not the firmware's
// algorithms; they are meant to sit in the same places with the same ranges.

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "Fm1Record.h"

class Effects {
public:
    void prepare(double sampleRate, int maxBlock);
    void reset();
    void setChain(const fm1::FxChain& c);   // message or audio thread; applied at the next block
    void process(float* mono, int n);

private:
    void applyPending();
    void processOne(int effect, float* buf, int n);

    double sr_ = 44100.0;
    fm1::FxChain chain_;
    juce::SpinLock pendingLock_;
    fm1::FxChain pending_;
    bool hasPending_ = false;

    juce::dsp::StateVariableTPTFilter<float> filter_;
    juce::Reverb reverb_;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delay_{96000};
    float delaySamples_ = 4410.0f;
    juce::dsp::Chorus<float> chorus_;
    juce::dsp::Phaser<float> phaser_;
    float toneState_ = 0.0f;
    juce::AudioBuffer<float> scratch_;
};
