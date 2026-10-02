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
#include "Fm1Edit.h"
#include "Fm1Seq.h"

class Fm1Session : private juce::Thread {
public:
    Fm1Session(Fm1Link& link);
    ~Fm1Session() override;

    enum class Op { None, Identify, Pull, Push, PullPatterns, PushPatterns, PullCurrent, SendEdit };

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
    // The sound the synth is playing now: its slot and its live edit buffer (unsaved
    // voice edits included), with the slot's stored settings record.
    // `live` has the edit buffer's voice and the live settings record; `stored` is the slot as saved.
    std::function<void(const fm1::Sound& live, const fm1::Sound& stored)> onCurrentRead;
    // The synth's GLOBE settings the plugin uses, read with identify on firmware
    // builds whose addresses are known (see knownAddrs).
    struct Globals {
        int midiChannel = 0;           // 0 = All, else 1..16
        int fxChannel = 2;             // 1..16
        int bendUp = 12, bendDown = 12;   // semitones
        int keyVelocity = 90;          // Keyboard > Velocity
        int glideTime = 0;             // 0..100
        bool glideFingered = false;    // Glide mode: Full Time or Fingered
    };
    std::function<void(const Globals&)> onGlobals;

    static constexpr int kPaceMs = 3000;

    bool busy() const { return isThreadRunning(); }
    void cancel() { cancel_ = true; }

    void identify();
    void pull(std::vector<int> slots);
    void push(std::vector<fm1::Sound> sounds, bool showLastOnDevice = false);   // each with its slot
    void select(int slot, int midiChannel = 0);  // program change, not queued; 0 = the synth's channel
    void setMidiChannel(int ch) { midiChannel_ = juce::jlimit(1, 16, ch); }   // where program changes go
    void pullPatterns(std::vector<int> pats);
    void pushPatterns(std::vector<std::pair<int, fm1::seq::Pattern>> pats, bool save);
    void pullCurrent();   // read whatever the synth is playing (FM-1+VA only)
    // Put `s` into the synth's edit buffer without storing it: program change to
    // s.slot (when selectFirst), then parameter changes and CCs, then read back.
    void sendEdit(const fm1::Sound& s, fm1::edit::Channels ch, bool selectFirst);
    // Send messages straight away (live editing); not queued, no read-back.
    void sendNow(const std::vector<fm1::Bytes>& msgs);

    // Where the live edit buffer (155-byte VCED), the current preset number and
    // the GLOBE settings live in RAM for a firmware version, if known.
    struct CurrentAddrs { uint32_t editBuffer; uint32_t slotByte; uint32_t globals; };
    static std::optional<CurrentAddrs> knownAddrs(int version);

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
    bool readBlock(uint32_t addr, int n, fm1::Bytes& out, juce::String& error);
    std::optional<uint32_t> discoverEditBuffer(int& slotOut, juce::String& error);
    std::optional<uint32_t> discoveredEditBuffer_;
    int discoveredForVersion_ = -1;
    // The edit buffer's address for this synth (known table, cached search, or a new search).
    std::optional<uint32_t> editBufferAddr(juce::String& err, int* slotOut);
    void readGlobals();
    fm1::Sound editSound_;
    fm1::edit::Channels editCh_;
    bool editSelect_ = true;
    std::atomic<int> midiChannel_{1};

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
