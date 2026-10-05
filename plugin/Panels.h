// Panels -- the editor's tabs: library and sync, FM editor, effects and
// envelope, sequencer, arpeggiator.

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"
#include "Firmwares.h"
#include "dexed_ui/DXLookNFeel.h"
#include "dexed_ui/GlobalPanel.h"
#include "dexed_ui/OperatorPanel.h"

// A grid of parameter controls (rotary sliders, or combo boxes for choices)
// bound to APVTS parameters.
class ParamGrid : public juce::Component {
public:
    ParamGrid(juce::AudioProcessorValueTreeState& apvts, const juce::StringArray& ids, const juce::StringArray& labels, int columns, int cellW = 64, int cellH = 74);
    void resized() override;
    int preferredHeight() const;

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
    void setIdentity(const juce::String& s) { identity_.setText(s, juce::dontSendNotification); }
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
    juce::Label vaPage_;
    bool vaEngine_ = true;
    int pagesSlot_ = -1;
    juce::ComboBox inPorts_, outPorts_;
    juce::TextButton connect_{"Connect"}, autoConnect_{"Find FM-1"};
    juce::Label identity_;
    juce::ListBox list_{"presets", this};
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
    DXLookNFeel lnf_;
    std::array<uint8_t, 156> vced_{};   // the editor's copy of the sound, from the parameters
    std::unique_ptr<OperatorPanel> ops_[6];
    std::unique_ptr<GlobalPanel> global_;
    char opStatus_[7] = "111111";
    int tick_ = 0;
};

class FxPanel : public juce::Component {
public:
    explicit FxPanel(FM1Processor&);
    void resized() override;

private:
    std::vector<std::unique_ptr<juce::Label>> headers_;
    std::vector<std::unique_ptr<ParamGrid>> grids_;
    juce::Label note_;
};

class SeqPanel : public juce::Component, private juce::Timer {
public:
    explicit SeqPanel(FM1Processor&);
    void resized() override;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void loadPatternControls();
    void loadStepControls();
    void applyPatternControls();
    void applyStepControls();
    juce::Rectangle<int> gridBounds() const;
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
    juce::TextButton audioSettings_{"Audio/MIDI settings..."};
    juce::Label note_, synth_;
    std::vector<std::unique_ptr<juce::Label>> labels_, headers_;
    bool loading_ = false;
};
