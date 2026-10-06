// Panels -- the editor's tabs: library and sync, FM editor, effects and
// envelope, sequencer, arpeggiator.

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"
#include "Firmwares.h"
#include "dexed_ui/DXLookNFeel.h"
#include "dexed_ui/GlobalPanel.h"
#include "dexed_ui/OperatorPanel.h"

// Drop-down lists open below their box, as a menu does. JUCE's default puts the selected item
// over the box, which on a small screen pushes the items above it behind a scroll arrow.
template <class Base>
struct DropDownLists : Base {
    juce::PopupMenu::Options getOptionsForComboBoxPopupMenu(juce::ComboBox& box, juce::Label& label) override {
        return juce::PopupMenu::Options().withTargetComponent(&box)
            .withInitiallySelectedItem(box.getSelectedId())
            .withMinimumWidth(box.getWidth())
            .withMaximumNumColumns(1)
            .withStandardItemHeight(label.getHeight())
            .withPreferredPopupDirection(juce::PopupMenu::Options::PopupDirection::downwards);
    }
};

// A page that scrolls when its content is taller than the window (a phone): the content is laid
// out at the window's width, then asked how tall it is (needed(); not taller than the window:
// no scrolling).
class ScrollPage : public juce::Component {
public:
    ScrollPage(juce::Component& content, std::function<int()> needed) : content_(content), needed_(std::move(needed)) {
        addAndMakeVisible(view_);
        view_.setViewedComponent(&content_, false);
        view_.setScrollBarsShown(true, false);
    }
    void resized() override {
        view_.setBounds(getLocalBounds());
        content_.setSize(getWidth(), getHeight());
        const int need = needed_();
        if (need > getHeight()) content_.setSize(getWidth() - view_.getScrollBarThickness(), std::max(getHeight(), needed_()));
    }

private:
    juce::Component& content_;
    std::function<int()> needed_;
    juce::Viewport view_;
};

// A grid of parameter controls (rotary sliders, or combo boxes for choices)
// bound to APVTS parameters.
class ParamGrid : public juce::Component {
public:
    ParamGrid(juce::AudioProcessorValueTreeState& apvts, const juce::StringArray& ids, const juce::StringArray& labels, int columns, int cellW = 64, int cellH = 74);
    void resized() override;
    int preferredHeight() const;
    int count() const { return int(cells_.size()); }
    int cellWidth() const { return cellW_; }
    void setColumns(int c) { columns_ = std::max(1, c); resized(); }   // to fit the width there is

private:
    struct Cell {
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::ComboBox> combo;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sa;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> ca;
    };
    std::vector<Cell> cells_;
    int columns_, cellW_, cellH_;
};

// The library window: the preset list, and beside it the pages for the selected
// preset (Sync, and the editors its engine has). The MIDI port controls live in
// connectionBar(), which the editor puts in its top bar.
class LibraryPanel : public juce::Component, private juce::ListBoxModel, private juce::Timer {
public:
    explicit LibraryPanel(FM1Processor&);
    ~LibraryPanel() override;
    void resized() override;
    void refresh();
    void setStatus(const juce::String& s) { status_.setText(s, juce::dontSendNotification); }
    void setIdentity(const juce::String& s) { identity_.setText(s, juce::dontSendNotification); identity_.setTooltip(s); }   // (the whole line, if cut)
    void refreshFxChannel();   // after the FM-1's GLOBE settings are read
    juce::Component& connectionBar() { return bar_; }
    // The editor pages for presets, and which of them the firmware has.
    void setEditorPages(juce::Component* fm, juce::Component* fx);
    void setFirmware(const fm1::FirmwareChoice& f);

private:
    int getNumRows() override { return BankModel::kSlots; }
    void paintListBoxItem(int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged(int row) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;
    void timerCallback() override;
    void refreshPorts();
    void refreshButtons();
    void showExportMenu();
    std::vector<int> selectedSlots() const;
    void importJson(const juce::File& f);

    // A component whose children are laid out by its owner.
    struct Holder : juce::Component {
        std::function<void()> layout;
        void resized() override { if (layout) layout(); }
    };
    void layoutBar();
    void layoutSync();
    void showPagesFor(int slot);   // the engine's editor tab for the selected preset
    void showInitMenu();

    FM1Processor& proc_;
    Holder bar_, syncPage_;
    juce::TabbedComponent pages_{juce::TabbedButtonBar::TabsAtTop};
    juce::Component* fmPage_ = nullptr;
    juce::Component* fxPage_ = nullptr;
    juce::Label vaPage_, chipPage_;
    bool vaEngine_ = true;
    int pagesSlot_ = -1;
    int pagesEngine_ = -1;     // the tabs shown are for this fm1::Engine, or not built (-1)
    juce::ComboBox inPorts_, outPorts_;
    juce::TextButton connect_{"Connect"}, autoConnect_{"Find FM-1"};
    juce::Label identity_;
    juce::ListBox list_{"presets", this};
    juce::TextButton selectMode_{"Select"};   // touch: each tap adds or removes a preset (shift / cmd-click on a desktop)
    // narrow (a phone): the list or the preset's pages, one at a time
    juce::TextButton showList_{"Presets"}, showPages_{"Sound"};
    bool showingPages_ = false;
    juce::Label currentName_;
    juce::TextButton init_{"Init..."};
    juce::TextButton sendEdit_{"Send to FM-1 (not saved)"};
    juce::ToggleButton live_{"Live: send every change"};
    juce::ComboBox fxChannel_;
    juce::TextButton pullCurrent_{"Pull what the FM-1 is playing"}, pushCurrent_{"Store to FM-1 (saves the preset)"},
                     pullAll_{"Pull all 128"}, pushChanged_{"Push changed"}, pushAll_{"Push all 128"},
                     selectOnDevice_{"Show on FM-1"}, cancel_{"Stop"},
                     importFile_{"Import..."}, exportFile_{"Export..."};
    juce::Label status_;
    std::unique_ptr<juce::FileChooser> chooser_;
    bool wasBusy_ = false;
    bool wasOpen_ = false;
};

class FmEditorPanel : public juce::Component, private juce::Timer {
public:
    explicit FmEditorPanel(FM1Processor&);
    ~FmEditorPanel() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void refreshName();

private:
    void timerCallback() override;

    FM1Processor& proc_;
    DropDownLists<DXLookNFeel> lnf_;
    std::array<uint8_t, 156> vced_{};   // the editor's copy of the sound, from the parameters
    std::unique_ptr<OperatorPanel> ops_[6];
    std::unique_ptr<GlobalPanel> global_;
    // narrow (a phone): the preset strip, then one page at a time, scaled to fit: Dexed's three
    // global boxes (Algorithm, LFO, Pitch EG) and OP1..OP6
    juce::TextButton pageButtons_[9];
    int page_ = 3;   // 0..2 a global box, 3..8 OP1..OP6
    void layoutWide();
    void layoutCompact();
    char opStatus_[7] = "111111";
    int tick_ = 0;
};

class FxPanel : public juce::Component {
public:
    explicit FxPanel(FM1Processor&);
    void resized() override;

private:
    // each group a header and its controls: beside it when the width allows, else under it,
    // wrapped to the width; the page scrolls when it is taller than the window
    juce::Viewport view_;
    juce::Component content_;
    std::vector<std::unique_ptr<juce::Label>> headers_;
    std::vector<std::unique_ptr<ParamGrid>> grids_;
    juce::Label note_;
};

class SeqPanel : public juce::Component, private juce::Timer {
public:
    explicit SeqPanel(FM1Processor&);
    void resized() override;
    // narrow (a phone): everything stacked under the grid, this tall (the editor scrolls it)
    static constexpr int kSeqNarrow = 1040;   // narrower: one column, taller than the screen (a phone, an iPad upright)
    int contentHeight() const { return contentBottom_ + 10; }
    std::function<void()> onHeightChanged;   // narrow: the step's rows changed the page's height
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void loadPatternControls();
    void loadStepControls();
    void applyPatternControls();
    void applyStepControls();
    juce::Rectangle<int> gridBounds() const;
    int gridY_ = 80, contentBottom_ = 0;
    void layoutNarrow();
    juce::Rectangle<int> cellBounds(int step) const;
    fm1::seq::Pattern& pattern();   // under lock by caller

    FM1Processor& proc_;
    int selectedStep_ = 0;
    juce::TextButton importPatterns_{"Import..."}, exportPatterns_{"Export..."};
    juce::Label fileStatus_;
    std::unique_ptr<juce::FileChooser> chooser_;
    void showExportMenu();
    juce::TextButton enable_{"SEQ"}, play_{"Play"}, rec_{"Rec"}, clearStep_{"Clear step"}, clearPattern_{"Clear pattern"},
                     copyStep_{"Copy step"}, pasteStep_{"Paste step"}, pull_{"Pull patterns from FM-1"}, push_{"Push patterns to FM-1"};
    juce::ToggleButton sync_{"Sync to host"}, overdub_{"Overdub"};
    juce::ComboBox pattern_, rate_, chainTo_, stepRate_, ratchet_;
    juce::Slider length_, tempo_, gate_, swing_, sound_, transpose_, stepGate_, stepChance_, stepTranspose_;
    juce::ToggleButton accent_{"Accent"}, slide_{"Tie & Slide"};
    juce::Label stepNotes_, info_;
    // the selected step's notes, one row each, as on the FM-1's step list:
    // name, velocity, Tie & Slide for that note, remove
    struct NoteRow {
        juce::Label name;
        juce::Slider velocity;
        juce::ToggleButton tie{"Tie"};
        juce::TextButton remove{"Remove"};
    };
    std::array<std::unique_ptr<NoteRow>, fm1::seq::kMaxNotes> noteRows_;
    juce::Label allVelLabel_{{}, "All notes"};
    juce::Slider allVelocity_;
    void applyNoteRow(int i);
    void layoutNoteRows(juce::Rectangle<int> area);
    juce::Rectangle<int> noteArea_;
    std::vector<std::unique_ptr<juce::Label>> labels_;
    bool loading_ = false;
    std::optional<fm1::seq::Step> clipboard_;
    // FM-1_096's parameter locks of a step (8 bytes of the pattern's table, 0xFF = none), under
    // the lock: read, or set (nullptr: none). They go with the step when it is cleared or pasted.
    std::array<uint8_t, 8> stepLocks(int step);
    void stepLocks(int step, const std::array<uint8_t, 8>* locks);
    std::array<uint8_t, 8> clipboardLocks_{};
    // step recording: notes within a short window form one chord
    std::vector<fm1::seq::Note> recChord_;
    int seenVersion_ = 0;
    double recLastMs_ = 0;
};

class ArpPanel : public juce::Component {
public:
    explicit ArpPanel(FM1Processor&);
    void resized() override;

private:
    FM1Processor& proc_;
    juce::TextButton enable_{"ARP"};
    juce::ComboBox mode_, rate_;
    juce::Slider octaves_, tempo_, gate_, swing_;
    juce::ToggleButton latch_{"Latch"}, sync_{"Sync to host"};
    std::vector<std::unique_ptr<juce::Label>> labels_;
};

// How this instance plays: bend range, MIDI input channel, on-screen keyboard
// velocity. Changes apply at once and are saved with the project; Save as
// default makes them what new instances start with.
class SettingsPanel : public juce::Component {
public:
    explicit SettingsPanel(FM1Processor&);
    void resized() override;
    void refresh();      // from the processor's settings
    void showSynth();    // the FM-1's GLOBE settings, once read
    void visibilityChanged() override;   // shown: read the FM-1's settings again

private:
    bool copyPending_ = false;
    void apply();        // the controls into the processor's settings
    FM1Processor& proc_;
    juce::Slider bendUp_, bendDown_, velocity_;
    juce::ComboBox channel_, velocityMode_;
    juce::ToggleButton hardware_{"Hardware character"};
    juce::Slider volumeDb_;
    juce::Label hardwareNote_;
    juce::TextButton save_{"Save as default"}, revert_{"Revert to default"}, copy_{"Copy from FM-1"};
    // the standalone app's audio and MIDI settings: on iOS and Android, JUCE's
    // window has no title bar, so no Options button to open them
    juce::ToggleButton embedBank_{"Keep the whole bank in projects"};
    juce::Label* libraryHeader_ = nullptr;
    juce::Label libraryPath_;
    juce::TextButton showLibrary_{"Show the library"};
    juce::Label note_, synth_;
    std::vector<std::unique_ptr<juce::Label>> labels_, headers_;
    bool loading_ = false;
};
