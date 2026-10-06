// FeluccaPanel -- the editor for an instance set to Felucca: its library (the user presets
// and projects), a part's engine, preset and every parameter, and the global settings, built
// from what Felucca says about them (names, ranges, value names), so a new Felucca version's
// changes show up without changes here. The groups follow Felucca 1.0's parameter order.
#pragma once

#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

#include "FeluccaSeqPanel.h"

#if FM1_FELUCCA

// Felucca's own front panel: its screen, buttons, knobs and keys, played as on the device.
class FeluccaDeviceView : public juce::Component, private juce::Timer {
public:
    explicit FeluccaDeviceView(FM1Processor&);
    ~FeluccaDeviceView() override;
    void resized() override;
    void paint(juce::Graphics&) override;
    void visibilityChanged() override;
    void refreshScreen() { timerCallback(); }   // the screen now (it follows 30 times a second while shown)

private:
    struct Knob;
    struct Key;
    void build();
    void timerCallback() override;
    std::shared_ptr<FeluccaEngine> engine() const { return proc_.felucca(); }
    FM1Processor& proc_;
    juce::Image screen_{juce::Image::RGB, 240, 240, true};
    std::vector<uint16_t> px_;
    juce::Rectangle<int> screenArea_;
    juce::OwnedArray<juce::TextButton> buttons_;
    juce::OwnedArray<Knob> knobs_;
    juce::OwnedArray<Key> keys_;
};

// One part's sound and the global settings: the part, its engine and preset, every
// parameter. (The Library tab's Sound page.)
class FeluccaSoundPage : public juce::Component, private juce::Timer {
public:
    static constexpr int kNarrow = 720;   // narrower: the top controls on rows (a phone, an iPad upright)
    explicit FeluccaSoundPage(FM1Processor&);
    void resized() override;
    void paint(juce::Graphics&) override;
    void visibilityChanged() override;
    void refresh();   // the engine, presets and values again (after a project or user preset loads, say)
    std::function<void()> onPartChanged;   // another part selected (here or on the device)
    void setStatus(const juce::String& s);   // in the info line for a while (the sync's progress and outcome)

private:
    juce::String infoText() const;            // the hint for Felucca or SLOOP
    struct Control {
        int id = 0;
        bool global = false;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::ComboBox> box;
    };
    struct Group {
        juce::String title;
        std::vector<int> ids;
        bool global = false;
        std::unique_ptr<juce::Label> header;
        std::vector<Control> controls;
    };

    std::shared_ptr<FeluccaEngine> engine() const { return proc_.felucca(); }   // held for the call
    void updateTempoControl();   // Global BPM is the host's while the tempo follows it
    void timerCallback() override;
    void selectTrack(int t);
    void build();          // the groups and their controls for the selected track
    void loadValues();
    void layoutContent();

    FM1Processor& proc_;
    int track_ = 0;
    int builtTrack_ = -1, builtEngine_ = -1;
    juce::uint32 statusUntil_ = 0;   // the info line shows a status until then, then the keyboard hint   // what the controls are for (the device can change either)
    juce::TextButton trackButtons_[4];
    juce::ComboBox engineBox_, presetBox_;
    // the selected part's sound from the connected FM-1 (up), and to it (down; not saved there)
    juce::DrawableButton pullSound_{"From FM-1", juce::DrawableButton::ImageFitted}, sendSound_{"To FM-1", juce::DrawableButton::ImageFitted};
    juce::ToggleButton hostTempo_{"Tempo follows the host"};
    juce::Label info_;
    juce::Viewport view_;
    juce::Component content_;
    std::vector<Group> groups_;
    bool loading_ = false;
};

// A synth running Felucca: pull everything from it, send everything to it, or follow it live.
// (The Library tab's Sync page.)
class FeluccaSyncPage : public juce::Component, private juce::Timer {
public:
    explicit FeluccaSyncPage(FM1Processor&);
    ~FeluccaSyncPage() override { proc_.onFeluccaLive = nullptr; }
    void resized() override;
    void setStatus(const juce::String& s) { status_.setText(s, juce::dontSendNotification); }   // the sync's progress and outcome

private:
    void timerCallback() override { update(); }
    void update();
    FM1Processor& proc_;
    juce::TextButton pullButton_{"Pull from FM-1"}, sendButton_{"Send to FM-1"}, liveButton_{"Live"};
    juce::Label about_, problem_, status_;
};

// The virtual FM-1's stored sounds and songs: its 32 user presets and four projects (shared by
// every instance set to Felucca), loaded, saved, renamed and erased through Felucca's editor
// protocol, as its web editor does (and as SAVE > USER and SAVE > PROJECT do on the device).
class FeluccaLibraryList : public juce::Component, private juce::ListBoxModel, private juce::Timer {
public:
    static constexpr int kUser = 32, kProjects = 4;   // rows: the user presets, then the projects
    explicit FeluccaLibraryList(FM1Processor&);
    void resized() override;
    void paint(juce::Graphics&) override;
    std::function<void()> onLoaded;   // a user preset or project was loaded: the sound changed
    void reload();                    // the names again, from the device
    void updateButtons();             // (their part number: after another part is selected)

private:
    struct Row { bool used = false; int engine = 0; juce::String name; };
    int getNumRows() override { return kUser + kProjects; }
    void paintListBoxItem(int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;
    void selectedRowsChanged(int row) override;
    void timerCallback() override;
    void readUsers(int start);         // 16 user presets' names (UP_LIST takes at most 16)
    void readProjects();
    int tick_ = 0;
    std::shared_ptr<FeluccaEngine> engine() const { return proc_.felucca(); }
    std::optional<std::vector<uint8_t>> ask(int cmd, const std::vector<uint8_t>& args);   // the reply's arguments
    juce::String partText() const;     // "PART 2": what Save stores, what Load loads into
    void load(int row);
    void save(int row);
    void rename(int row);
    void erase(int row);
    void say(const juce::String& text, bool problem = false);
    static juce::String cleanName(const juce::String&);   // what Felucca takes: upper case ASCII, at most 12

    FM1Processor& proc_;
    Row rows_[kUser + kProjects];
    juce::ListBox list_{"felucca library", this};
    juce::Label nameLabel_{{}, "Name"};
    juce::TextEditor name_;
    juce::TextButton loadButton_{"Load"}, saveButton_{"Save"}, renameButton_{"Rename"}, eraseButton_{"Erase"};
    juce::Label status_;
};

// The editor for an instance set to Felucca: a Library tab (the stored sounds and songs, and
// the sound being edited) and a Device tab (Felucca's own front panel).
class FeluccaPanel : public juce::Component {
public:
    static constexpr int kNarrow = 760;   // narrower (a phone): the list or the pages, one at a time
    explicit FeluccaPanel(FM1Processor&);
    void resized() override;
    void refresh() { sound_.refresh(); list_.reload(); seq_.refresh(); }   // after a project loads, say
    void setStatus(const juce::String& s) { sync_.setStatus(s); sound_.setStatus(s); }

private:
    FM1Processor& proc_;
    FeluccaSoundPage sound_;
    FeluccaSyncPage sync_;
    FeluccaLibraryList list_;
    juce::TabbedComponent pages_{juce::TabbedButtonBar::TabsAtTop};
    struct Holder : juce::Component {   // its children laid out by the panel
        std::function<void()> layout;
        void resized() override { if (layout) layout(); }
    } library_;
    juce::TextButton showList_{"Presets"}, showPages_{"Sound"};
    bool showingPages_ = false;
    FeluccaDeviceView device_;
    FeluccaSeqPage seq_;
    juce::TabbedComponent tabs_{juce::TabbedButtonBar::TabsAtTop};
    void layoutLibrary();
};

#endif
