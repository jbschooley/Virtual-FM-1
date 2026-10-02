// Sequencer -- the FM-1+VA step sequencer in the plugin: 16 patterns of up to
// 64 steps, up to nine notes a step, per-step note value, ratchet, gate,
// chance, transpose, accent and slide, pattern chaining, swing. Timing follows
// the firmware's seq_tick rule (Fm1Seq::stepTimes). Runs on the audio thread,
// emitting MIDI into a buffer; clocked from the host when synced, or from the
// pattern's own tempo.

#pragma once

#include <array>
#include <atomic>
#include <random>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "Fm1Seq.h"

class Sequencer {
public:
    static constexpr int kPatterns = fm1::seq::kPatterns;

    Sequencer();

    // Pattern data. Edit under `lock` (the audio thread skips a block it cannot lock).
    juce::SpinLock lock;
    std::array<fm1::seq::Pattern, kPatterns> patterns;
    std::array<int, kPatterns> chain;   // pattern index to play after this one, -1 = Repeat (FM-1_093 per-pattern Chain)

    // Settings (atomics: set from the UI, read by the audio thread)
    std::atomic<bool> enabled{false};      // the SEQ button
    std::atomic<bool> syncToHost{true};    // follow the host transport and tempo
    std::atomic<int> selected{0};          // the pattern to play / edit
    std::atomic<bool> overdub{false};

    // Transport
    void play();
    void stop();
    bool isPlaying() const { return playing_; }
    int playingStep() const { return playingStep_; }       // -1 when stopped
    int playingPattern() const { return playingPattern_; }
    double currentTempo() const { return tempo_; }

    void prepare(double sampleRate);
    // Generates note on/off (channel 1) into `out` at sample offsets; `pos` may be null.
    void process(const juce::AudioPlayHead::PositionInfo* pos, int numSamples, juce::MidiBuffer& out);
    void panic(juce::MidiBuffer& out);   // note-offs for everything sounding

    std::atomic<int> patternSoundRequest{-1};   // the pattern's sound (preset) when a pattern starts

    // Real-time recording (FM-1_092): while playing and `recording`, played notes go to
    // the nearest step with their velocity; a held note is tied across the steps it spans.
    std::atomic<bool> recording{false};
    void recordNoteOn(int note, int vel);   // audio thread
    void recordNoteOff(int note);           // audio thread

    // The notes step `stepIx` of `p` plays when it fires at `atTick` (ratchet,
    // gate, accent, transpose, ties and slide applied; chance not), in the
    // pattern's ticks (fm1::seq::stepTimes). Used for playback and MIDI export.
    struct Event { double tick; int note; bool on; int vel; };
    static void stepEvents(const fm1::seq::Pattern& p, const fm1::seq::Times& t, int stepIx, double atTick, std::vector<Event>& out);

private:
    using Pending = Event;
    struct Held { int note, vel, step; };
    std::vector<Held> recHeld_;
    std::array<bool, fm1::seq::kSteps> recTouched_{};   // steps replaced in this pass
    double lastStepTick_ = 0.0;
    int lastStep_ = -1;
    int nearestStep(double tick) const;
    void startPattern(int pat, double atTick, bool first);
    void fireStep(double atTick);
    void flush(juce::MidiBuffer& out, int upToSample, double tickAtBlockStart, double ticksPerSample, int blockStartSample);

    double sr_ = 44100.0;
    std::atomic<bool> playing_{false};
    std::atomic<bool> startRequest_{false}, stopRequest_{false};
    std::atomic<int> playingStep_{-1}, playingPattern_{0};
    double tempo_ = 120.0;
    bool hostWasPlaying_ = false;

    double absTick_ = 0.0;         // monotonic
    double nextStepTick_ = 0.0;
    int stepIx_ = 0;
    int pat_ = 0;
    fm1::seq::Pattern cur_;        // a copy of the playing pattern
    fm1::seq::Times times_;
    std::vector<Pending> pending_;
    std::mt19937 rng_{12345};
    std::array<int, 128> sounding_{};   // note-on counts, for panic
};
