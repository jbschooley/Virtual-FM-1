// Fm1Session -- the sync operations, run on a worker thread:
//   identify          which firmware the synth runs
//   pull slot(s)      read presets from the synth into the plugin
//   push slot(s)      write presets to the synth, verified by reading back
//   select slot       make the synth show a preset (program change)
// What each operation can do and how it talks to the synth depends on the
// firmware, and comes from its profile (sync/Firmware.h): FM-1+VA's read-back
// protocol, or stock M-VAVE firmware, which only takes DX7 data. This class does
// the threading, progress, cancelling, pacing and read-back checks around it.

#pragma once

#include <atomic>
#include <functional>
#include <vector>

#include <juce_core/juce_core.h>

#include "Fm1Codec.h"
#include "Fm1Link.h"
#include "Fm1Edit.h"
#include "Fm1Seq.h"
#include "Firmware.h"

class Fm1Session : private juce::Thread {
public:
    Fm1Session(Fm1Link& link);
    ~Fm1Session() override;

    enum class Op { None, Identify, Pull, Push, PullPatterns, PushPatterns, PullCurrent, SendEdit, ReadGlobals };

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
    // The synth's GLOBE settings the plugin uses, read with identify when the
    // firmware's profile can (sync/Firmware.h).
    using Globals = fm1::Globals;
    std::function<void(const Globals&)> onGlobals;


    bool busy() const { return isThreadRunning(); }
    void cancel() { cancel_ = true; }

    void identify();
    bool readSettings();   // the GLOBE settings again (onGlobals); false when busy or not connected
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

    std::optional<fm1::Identity> lastIdentity() const { return identity_; }

private:
    void run() override;
    bool start(Op op);
    void report(int done, int total, const juce::String& text, bool finished = false, bool failed = false);
    std::optional<fm1::Identity> doIdentify();
    // The connected firmware's profile, made by doIdentify (session thread only).
    std::unique_ptr<fm1::Firmware> firmware_;
    fm1::Port port();
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
