// FeluccaSeqPanel -- the Sequencer tab of an instance set to Felucca or SLOOP: its tracks' steps
// on a grid (a piano roll for the notes, lanes for drum hits), the selected step's details, each
// track's pattern settings, and (Felucca) its song chain and motion. It edits the instance's own
// device through the firmware's editor protocol (FeluccaSeq), so the same edits reach a connected
// FM-1: Pull and Send of the patterns on demand, and every edit while Live runs.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#if FM1_FELUCCA

#include "FeluccaSeq.h"

class FM1Processor;
class FeluccaEngine;

class FeluccaSeqPage : public juce::Component, private juce::Timer {
public:
    explicit FeluccaSeqPage(FM1Processor&);
    ~FeluccaSeqPage() override;
    void resized() override;
    void paint(juce::Graphics&) override;
    void visibilityChanged() override;
    void refresh();                            // read the device again (after a project loads, say)

    // what the grid shows, for the grid and the tests
    int track() const { return track_; }
    int selectedStep() const { return sel_; }
    bool drumsView() const;
    int columns() const { return cols_; }
    int firstStep() const { return page_ * cols_; }

    // edits, as the grid and the controls make them (also used by the tests)
    void toggleNote(int step, int note);
    void toggleLane(int step, int lane, bool accent);
    void selectTrack(int t);
    void selectStep(int step);
    void setStepTime(int time);
    void setStepFlag(int flag, bool on);
    void setStepVelocity(int v);
    void setStepChance(int c);
    void setNoteLevel(int k, int level);       // SLOOP
    void setNoteRatchet(int k, int ratchet);   // SLOOP
    void setPatternParam(int id, int value);
    const felucca::TrackPattern& pattern() const { return pat_; }
    void showView(int v);                      // 0 pattern, 1 song, 2 motion

private:
    class Grid;
    class SongView;
    class MotionView;
    class SloopSongView;
    void timerCallback() override;
    std::shared_ptr<FeluccaEngine> engine() const;
    const felucca::Dialect& dialect() const;
    bool sloop() const;
    void readPattern();                        // the track's settings and steps from the device
    void readContext();                        // scale, lanes, root
    void fitNotes();                           // the piano roll where the track's notes are
    void writeStep(int step);                  // pat_'s step to the device (and the FM-1 while Live)
    void writeDrum(int step);
    void loadControls();                       // the controls from pat_ and the selected step
    void layoutControls(juce::Rectangle<int> r);
    juce::Colour trackColour(int t) const;

    FM1Processor& proc_;
    int track_ = 0, sel_ = 0, page_ = 0, cols_ = 16, lowNote_ = 48, view_ = 0, playhead_ = -1, tick_ = 0;
    bool loading_ = false, forceDrums_ = false, drumsChoice_ = false;
    unsigned scaleMask_ = 0xFFFu;
    int root_ = 0;
    std::vector<std::string> lanes_;
    felucca::TrackPattern pat_;

    juce::TextButton trackButtons_[4], play_{"Play"}, pageButtons_[4], octDown_{"-"}, octUp_{"+"}, notesView_{"Notes"}, drumsView_{"Drums"},
        patternTab_{"Pattern"}, songTab_{"Song"}, motionTab_{"Motion"}, pull_{"Pull patterns"}, send_{"Send patterns"};
    juce::ComboBox len_, div_, time_;
    juce::Slider swing_, gate_, vel_, chance_;
    juce::Label lenLabel_, divLabel_, swingLabel_, gateLabel_, stepLabel_, velLabel_, chanceLabel_, info_;
    juce::ToggleButton accent_{"Accent"}, slide_{"Slide"};
    juce::ComboBox paintLevel_, paintRatchet_;  // SLOOP's drum hits: the level and ratchet a click paints
    juce::ComboBox noteLevel_[4], noteRatchet_[4];
    juce::Label noteName_[4];
    std::unique_ptr<Grid> grid_;
    std::unique_ptr<SongView> song_;
    std::unique_ptr<MotionView> motion_;
    std::unique_ptr<SloopSongView> sloopSong_;
};

#endif
