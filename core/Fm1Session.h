// Fm1Session -- the sync operations, run on a worker thread:
//   identify          which firmware the synth runs
//   pull slot(s)      read presets from the synth into the plugin
//   push slot(s)      write presets to the synth, verified by reading back
//   select slot       make the synth show a preset (program change)
// What each operation can do and how it talks to the synth depends on the
// firmware, and comes from its profile (core/Firmware.h): FM-1+VA's read-back
// protocol, or stock M-VAVE firmware, which only takes DX7 data. This class does
// the threading, progress, cancelling, pacing and read-back checks around it.

#pragma once

#include <atomic>
#include <functional>
#include <mutex>
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

    enum class Op { None, Identify, Pull, Push, PullPatterns, PushPatterns, PullCurrent, SendEdit, ReadGlobals, Job };

    struct Progress {
        Op op = Op::None;
        int done = 0, total = 0;
        juce::String text;
        bool finished = false, failed = false;
    };

    // All callbacks arrive on the message thread.
    std::function<void(const fm1::Identity&)> onIdentity;
    // Asked on the session thread once a synth has identified itself: false stops
    // everything here (nothing is read or written) and onRejected is called on the
    // message thread, e.g. when the synth runs a firmware other than the instance's.
    std::function<bool(const fm1::Identity&)> acceptIdentity;
    std::function<void(const fm1::Identity&)> onRejected;
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
    // firmware's profile can (core/Firmware.h).
    using Globals = fm1::Globals;
    std::function<void(const Globals&)> onGlobals;


    bool busy() const { return isThreadRunning(); }
    void cancel() { cancel_ = true; }
    void stop() { cancel_ = true; stopThread(6000); }   // cancel and wait for it (an owner going away)

    void identify();
    bool readSettings();   // the GLOBE settings again (onGlobals); false when busy or not connected
    void pull(std::vector<int> slots);
    void push(std::vector<fm1::Sound> sounds, bool showLastOnDevice = false);   // each with its slot
    void select(int slot, int midiChannel = 0);  // program change, not queued; 0 = the synth's channel
    void setMidiChannel(int ch) { midiChannel_ = juce::jlimit(1, 16, ch); }   // where program changes go
    void pullPatterns(std::vector<int> pats);
    void pushPatterns(std::vector<std::pair<int, fm1::seq::Pattern>> pats, bool save);
    void pullCurrent();   // read whatever the synth is playing (FM-1+VA only)
    // Anything else a firmware's own sync does (Felucca's), on the session thread with its port,
    // once the synth is identified; the job's result is the session's last progress report.
    struct JobResult { bool ok = false; juce::String text; };
    bool job(const juce::String& startText, std::function<JobResult(fm1::Port&)> fn);
    // Put `s` into the synth's edit buffer without storing it: program change to
    // s.slot (when selectFirst), then parameter changes and CCs, then read back.
    void sendEdit(const fm1::Sound& s, fm1::edit::Channels ch, bool selectFirst);
    // Live editing: send what takes the synth's edit buffer from `from` to `to`, as
    // the firmware's profile says, straight away; not queued, no read-back. Sends
    // nothing until the synth has been identified.
    void sendChange(const fm1::Sound& from, const fm1::Sound& to, fm1::edit::Channels ch);

    std::optional<fm1::Identity> lastIdentity() const { std::lock_guard<std::mutex> l(identityLock_); return identity_; }

private:
    void run() override;
    bool start(Op op);
    void report(int done, int total, const juce::String& text, bool finished = false, bool failed = false);
    std::optional<fm1::Identity> doIdentify();
    juce::String noIdentityText() const;   // why there is no identity: no answer, or not accepted
    bool rejected_ = false;                // session thread
    juce::String rejectedName_;
    // The connected firmware's profile, made by doIdentify on the session thread.
    // sendChange reads it from the message thread, so replacing it takes firmwareLock_.
    std::unique_ptr<fm1::Firmware> firmware_;
    std::mutex firmwareLock_;
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
    juce::String jobText_;
    std::function<JobResult(fm1::Port&)> job_;
    std::optional<fm1::Identity> identity_;   // written by the session thread, under identityLock_
    mutable std::mutex identityLock_;
    std::atomic<bool> cancel_{false};
};
