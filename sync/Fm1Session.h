// Fm1Session -- the sync operations, run on a worker thread:
//   identify          which firmware the synth runs
//   pull slot(s)      read presets from the synth into the plugin
//   push slot(s)      write presets to the synth, verified by reading back
//   select slot       make the synth show a preset (program change)
// Reads and writes use FM-1+VA's own read-back protocol; stock M-VAVE
// firmware only accepts DX7 dumps and cannot be read, which identify reports.
//
// Writes are paced 3 s apart, as her bank.js does: each write rebuilds the
// effects and writes flash, and closer spacing was heard as crackling.

#pragma once

#include <atomic>
#include <functional>
#include <vector>

#include <juce_core/juce_core.h>

#include "Fm1Codec.h"
#include "Fm1Link.h"
#include "Fm1Seq.h"

class Fm1Session : private juce::Thread {
public:
    Fm1Session(Fm1Link& link);
    ~Fm1Session() override;

    enum class Op { None, Identify, Pull, Push, PullPatterns, PushPatterns };

    struct Progress {
        Op op = Op::None;
        int done = 0, total = 0;
        juce::String text;
        bool finished = false, failed = false;
    };

    // All callbacks arrive on the message thread.
    std::function<void(const fm1::Identity&)> onIdentity;
    std::function<void(const fm1::Sound&)> onSoundRead;       // after a successful read
    std::function<void(const fm1::Sound&)> onSoundWritten;    // after a verified write
    std::function<void(const Progress&)> onProgress;
    std::function<void(int pat, const fm1::seq::Pattern&)> onPatternRead;
    std::function<void(int pat)> onPatternWritten;

    static constexpr int kPaceMs = 3000;

    bool busy() const { return isThreadRunning(); }
    void cancel() { cancel_ = true; }

    void identify();
    void pull(std::vector<int> slots);
    void push(std::vector<fm1::Sound> sounds, bool showLastOnDevice = false);   // each with its slot
    void select(int slot, int midiChannel = 1);  // program change, not queued
    void pullPatterns(std::vector<int> pats);
    void pushPatterns(std::vector<std::pair<int, fm1::seq::Pattern>> pats, bool save);

    std::optional<fm1::Identity> lastIdentity() const { return identity_; }

private:
    void run() override;
    bool start(Op op);
    void report(int done, int total, const juce::String& text, bool finished = false, bool failed = false);
    std::optional<fm1::Identity> doIdentify();
    // nullopt when nothing usable came back; `error` set when the synth refused
    std::optional<fm1::Sound> readSound(int slot, juce::String& error);
    std::optional<fm1::Bytes> readMem(uint32_t addr, int n, juce::String& error);
    bool writePatternPart(const fm1::Bytes& msg, juce::String& error);

    Fm1Link& link_;
    Op op_ = Op::None;
    std::vector<int> pullSlots_;
    std::vector<fm1::Sound> pushSounds_;
    bool showLast_ = false;
    std::vector<int> pullPats_;
    std::vector<std::pair<int, fm1::seq::Pattern>> pushPats_;
    bool savePats_ = false;
    std::optional<fm1::Identity> identity_;
    std::atomic<bool> cancel_{false};
};
