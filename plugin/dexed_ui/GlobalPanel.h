// GlobalPanel -- algorithm, feedback, LFO and pitch envelope in Dexed's
// layout (Dexed's global strip, 539 x 144, as its three boxes: dexedPart(0..2) =
// algorithm and feedback, LFO, pitch envelope), and this project's own area with
// the preset name, transpose and Store / Revert / Send / Live (320 x 144:
// presetPart()). Each is its own component, so the owner can place them side by
// side (Dexed's strip) or apart (a phone); the GlobalPanel itself is not shown.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "AlgoDisplay.h"
#include "DXComponents.h"
#include "DXLookNFeel.h"

class GlobalPanel {
public:
    GlobalPanel(juce::AudioProcessorValueTreeState& apvts, uint8_t* vced, DXLookNFeel& lnf);
    ~GlobalPanel();
    static constexpr int kPresetW = 320, kDexedW = 539, kH = 144;
    static constexpr int kDexedX[4] = {0, 228, 407, 539};   // where each box of the strip starts and the strip ends
    juce::Component& presetPart() { return preset_; }
    juce::Component& dexedPart(int box) { return dexed_[box]; }
    void repaint() { preset_.repaint(); for (auto& d : dexed_) d.repaint(); }
    void refresh();

    juce::TextEditor name;       // the preset name; the owner binds onReturnKey/onFocusLost
    juce::Label slotLabel;
    juce::TextButton storeButton{"Store"}, revertButton{"Revert"}, sendButton{"Send to FM-1"};
    juce::ToggleButton liveButton{"Live"};
    void setOpStatus(const char* s) { algoDisplay_->opStatus = s; }

private:
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<DXSlider> knob(juce::Component& part, const juce::String& name, int x, int y, std::unique_ptr<SA>& att, const juce::String& paramId, int envPos = -1);

    // a part: its children are the controls, its paint the owner's drawing for it
    struct Part : juce::Component {
        std::function<void(juce::Graphics&)> painter;
        void paint(juce::Graphics& g) override { if (painter) painter(g); }
    };
    Part preset_, dexed_[3];
    void paintPreset(juce::Graphics& g);
    void paintDexed(juce::Graphics& g, int box);

    uint8_t* vced_;
    DXLookNFeel& lnf_;
    juce::AudioProcessorValueTreeState& apvts_;

    std::unique_ptr<AlgoDisplay> algoDisplay_;
    std::unique_ptr<PitchEnvDisplay> pitchEnv_;
    std::unique_ptr<DXSlider> algo_, feedback_, lfoSpeed_, lfoDelay_, lfoPmd_, lfoAmd_, pms_, transpose_, pegR_[4], pegL_[4];
    std::unique_ptr<SA> algoA_, feedbackA_, lfoSpeedA_, lfoDelayA_, lfoPmdA_, lfoAmdA_, pmsA_, transposeA_, pegRA_[4], pegLA_[4];
    std::unique_ptr<ComboBoxImage> lfoWave_;
    std::unique_ptr<CA> lfoWaveA_;
    std::unique_ptr<juce::ToggleButton> lfoSync_, oscSync_;
    std::unique_ptr<BA> lfoSyncA_, oscSyncA_;
    char algoValue_ = 0;
    uint8_t lastPeg_[8] = {0};
};
