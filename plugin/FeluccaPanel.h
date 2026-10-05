// FeluccaPanel -- the editor for an instance set to Felucca: a track's engine, preset
// and every parameter, and the global settings, built from what Felucca says about
// them (names, ranges, value names), so a new Felucca version's changes show up
// without changes here. The groups follow Felucca 1.0's parameter order.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

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

class FeluccaPanel : public juce::Component, private juce::Timer {
public:
    explicit FeluccaPanel(FM1Processor&);
    ~FeluccaPanel() override { proc_.onFeluccaLive = nullptr; }
    void resized() override;
    void paint(juce::Graphics&) override;
    void refresh();   // the engine, presets and values again (after a project loads, say)

private:
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
    void showDevice(bool on);

    FM1Processor& proc_;
    int track_ = 0;
    juce::TextButton trackButtons_[4];
    juce::ComboBox engineBox_, presetBox_;
    juce::ToggleButton hostTempo_{"Tempo follows the host"};
    juce::TextButton deviceButton_{"DEVICE"};   // Felucca's own front panel instead of the parameters
    juce::TextButton pullButton_{"Pull from FM-1"}, sendButton_{"Send to FM-1"}, liveButton_{"Live"};   // a synth running Felucca
    void updateSyncButtons();
    juce::String syncProblem_;
    FeluccaDeviceView device_;
    juce::Label info_;
    juce::Viewport view_;
    juce::Component content_;
    std::vector<Group> groups_;
    bool loading_ = false;
};

#endif
