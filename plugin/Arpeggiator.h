// Arpeggiator -- the FM-1's arpeggiator: Up, Down, Inclusive, Exclusive,
// Random, Order and Repeat over 1..4 octaves, note value 1/1..1/32T, gate,
// swing, latch, and sync to the host clock. Held notes come from the MIDI
// input; the arp plays them on the audio thread into a MIDI buffer.

#pragma once

#include <array>
#include <atomic>
#include <random>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

class Arpeggiator {
public:
    enum Mode { Up, Down, Inclusive, Exclusive, Random, Order, Repeat, kModes };
    static const char* const kModeNames[kModes];

    std::atomic<bool> enabled{false};
    std::atomic<int> mode{Up};
    std::atomic<int> octaves{1};       // 1..4
    std::atomic<int> rate{6};          // 0..9 note value (Fm1Seq::kValueTicks)
    std::atomic<int> tempo{120};       // 30..300, used when not synced
    std::atomic<int> gate{50};         // 0..100
    std::atomic<int> swing{50};        // 50..75
    std::atomic<bool> latch{false};
    std::atomic<bool> syncToHost{true};

    void prepare(double sampleRate);
    // Called for every note on/off the plugin receives while enabled (audio thread).
    void noteOn(int note, int vel);
    void noteOff(int note);
    void allOff();
    void process(const juce::AudioPlayHead::PositionInfo* pos, int numSamples, juce::MidiBuffer& out);
    void panic(juce::MidiBuffer& out);
    int stepIndex() const { return stepIx_; }

private:
    struct Held { int note, vel; };
    struct Pending { double tick; int note; bool on; int vel; };
    void rebuild();

    double sr_ = 44100.0;
    std::vector<Held> held_;         // in the order played
    bool anyKeyDown_ = false;
    std::vector<Held> sequence_;
    bool dirty_ = false;
    int stepIx_ = 0;
    double absTick_ = 0.0, nextTick_ = 0.0;
    int parity_ = 0;
    bool running_ = false;
    std::vector<Pending> pending_;
    std::mt19937 rng_{777};
    std::array<int, 128> sounding_{};
};
