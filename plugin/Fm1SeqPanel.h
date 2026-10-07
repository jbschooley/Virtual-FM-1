// Fm1SeqPanel -- the Sequencer tab of an instance set to the stock firmware or baud girl's FM-1+VA:
// the pattern as FM-1_096's Pattern screen shows it (sixteen steps a page, a lane for each part:
// Notes and Locks, or an 8-Bit preset's Lead, Bass, SFX, Drums and Locks; a lane opens into a
// row for each note), larger for a desktop, an iPad or a phone. Below the lanes, the selected
// step's settings, notes and locks, as the FM-1's step list has them.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

#include "IconButton.h"
#include "PluginProcessor.h"

class Fm1SeqPage : public juce::Component, private juce::Timer {
public:
    explicit Fm1SeqPage(FM1Processor&);
    ~Fm1SeqPage() override;
    void resized() override;
    void paint(juce::Graphics&) override;
    static constexpr int kNarrow = 1040;         // narrower (a phone, an iPad upright): one column, taller than the screen
    int contentHeight() const { return contentBottom_ + 10; }
    void refresh();                              // the pattern read again (the tab does it on its own 20 times a second)
    std::function<void()> onHeightChanged;       // narrow: the page's height changed

    // the lanes: 0..n-1 as shown (the last is Locks); -1 all of them (the overview)
    int laneCount() const;
    juce::String laneName(int lane) const;
    void openLane(int lane);
    int openedLane() const { return opened_; }
    bool chipLanes() const;                      // an 8-Bit preset's lanes
    // edits, as the lanes and the step's controls make them (also used by the tests)
    void selectStep(int step);
    int selectedStep() const { return sel_; }
    void toggleNote(int step, int note);         // add (velocity 100) or take off
    void setHold(int step, int note, int steps); // a note's length past its own step
    void addLock(int step, int what);            // starts mid-range; at most four a step
    void setLock(int step, int what, int value);
    void removeLock(int step, int what);
    int page() const { return page_; }
    void showPage(int p);

    // a lock's name and the values it may hold, in the FM-1's own words (baud girl's 096 app,
    // from her firmware's ui_knobs.cpp): empty / {0, -1} for a code the synth does not lock
    static juce::String lockName(int what);
    static std::pair<int, int> lockRange(int what);

private:
    // a lane: its name and the notes it holds (pitched: a roll of notes; else twelve named keys)
    struct Lane { const char* name; int lo, hi; bool pitched; };
    Lane laneAt(int lane) const;
    int playingStep() const;                     // in this pattern, -1 if it is not playing
    class Lanes;
    struct NoteRow;
    struct LockRow;
    void timerCallback() override;
    fm1::seq::Pattern& pattern();                // under the sequencer's lock, by the caller
    void edit(const std::function<void(fm1::seq::Pattern&)>& f);
    void readPattern();                          // snap_ from the processor's pattern
    void loadSettings();
    void loadStep();
    void applySettings();
    void applyStep();
    void layoutStep(juce::Rectangle<int> r);
    void layoutNarrow();
    std::vector<std::pair<int, int>> locksOf(const fm1::seq::Pattern& p, int step) const;
    void putLocks(fm1::seq::Pattern& p, int step, const std::vector<std::pair<int, int>>& locks);
    void showExportMenu();
    void say(const juce::String& text);

    FM1Processor& proc_;
    fm1::seq::Pattern snap_;                     // what the lanes draw
    int sel_ = 0, page_ = 0, opened_ = -1, rollLow_ = 48, contentBottom_ = 0, seenVersion_ = -1;
    int lanesMode_ = 0;                          // 0 as the notes say, 1 FM / VA, 2 8-Bit
    bool loading_ = false, follow_ = true, wasPlaying_ = false;
    int lastPlaying_ = -2;
    juce::uint32 sayUntil_ = 0;
    std::unique_ptr<Lanes> lanes_;

    IconButton play_{"Play", IconButton::Icon::Play};
    juce::TextButton enable_{"SEQ"}, rec_{"Rec"}, pull_{"Pull patterns"}, push_{"Push patterns"},
        import_{"Import..."}, export_{"Export..."}, pageButtons_[4], back_{"All lanes"}, octDown_{"Oct -"}, octUp_{"Oct +"},
        copyStep_{"Copy step"}, pasteStep_{"Paste step"}, clearStep_{"Clear step"}, clearPattern_{"Clear pattern"};
    juce::ToggleButton sync_{"Sync to host"}, overdub_{"Overdub"}, accent_{"Accent"}, slide_{"Tie & Slide"};
    juce::ComboBox pattern_, steps_, rate_, chain_, repeats_, lanesMode_box_, stepRate_, ratchet_, addLock_;
    juce::Slider swing_, gate_, tempo_, transpose_, stepGate_, stepChance_, stepTranspose_, allVel_;
    juce::Label allVelL_;
    juce::Label stepsL_, rateL_, swingL_, gateL_, chainL_, repeatsL_, tempoL_, transposeL_, stepTitle_, stepRateL_, ratchetL_,
        stepGateL_, stepChanceL_, stepTransposeL_, notesTitle_, locksTitle_, info_;
    std::vector<std::unique_ptr<NoteRow>> noteRows_;
    std::vector<std::unique_ptr<LockRow>> lockRows_;
    std::unique_ptr<juce::FileChooser> chooser_;
    std::optional<fm1::seq::Step> clipboard_;
    std::vector<std::pair<int, int>> clipboardLocks_;
    // step recording: notes played within a moment are one chord
    std::vector<fm1::seq::Note> recChord_;
    double recLastMs_ = 0;
};
