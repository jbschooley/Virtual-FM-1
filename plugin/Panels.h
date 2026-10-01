// Panels -- the editor's tabs: library and sync, FM editor, effects and
// envelope, sequencer, arpeggiator.

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"
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

class LibraryPanel : public juce::Component, private juce::ListBoxModel, private juce::Timer {
public:
    explicit LibraryPanel(FM1Processor&);
    ~LibraryPanel() override;
    void resized() override;
    void refresh();
    void setStatus(const juce::String& s) { status_.setText(s, juce::dontSendNotification); }
    void setIdentity(const juce::String& s) { identity_.setText(s, juce::dontSendNotification); }

private:
    int getNumRows() override { return BankModel::kSlots; }
    void paintListBoxItem(int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged(int row) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;
    void timerCallback() override;
    void refreshPorts();
    void refreshButtons();

    FM1Processor& proc_;
    juce::ComboBox inPorts_, outPorts_;
    juce::TextButton connect_{"Connect"}, autoConnect_{"Find FM-1"};
    juce::Label identity_;
    juce::ListBox list_{"presets", this};
    juce::Label currentName_;
    juce::TextButton sendEdit_{"Send to FM-1 (not saved)"};
    juce::ToggleButton live_{"Live: send every change"};
    juce::ComboBox fxChannel_;
    juce::TextButton pullCurrent_{"Pull what the FM-1 is playing"}, pushCurrent_{"Store to FM-1 (saves the preset)"},
                     pullAll_{"Pull all 128"}, pushChanged_{"Push changed"}, pushAll_{"Push all 128"},
                     selectOnDevice_{"Show on FM-1"}, cancel_{"Stop"},
                     importSyx_{"Import .syx..."}, exportSyx_{"Export .syx..."};
    juce::Label status_;
    std::unique_ptr<juce::FileChooser> chooser_;
    bool wasBusy_ = false;
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
    juce::TextButton enable_{"SEQ"}, play_{"Play"}, rec_{"Rec"}, clearStep_{"Clear step"}, clearPattern_{"Clear pattern"},
                     copyStep_{"Copy step"}, pasteStep_{"Paste step"}, pull_{"Pull patterns from FM-1"}, push_{"Push patterns to FM-1"};
    juce::ToggleButton sync_{"Sync to host"}, overdub_{"Overdub"};
    juce::ComboBox pattern_, rate_, chainTo_, stepRate_, ratchet_;
    juce::Slider length_, tempo_, gate_, swing_, sound_, transpose_, stepGate_, stepChance_, stepTranspose_;
    juce::ToggleButton accent_{"Accent"}, slide_{"Slide"};
    juce::Label stepNotes_, info_;
    std::vector<std::unique_ptr<juce::Label>> labels_;
    bool loading_ = false;
    std::optional<fm1::seq::Step> clipboard_;
    // step recording: notes within a short window form one chord
    std::vector<fm1::seq::Note> recChord_;
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
