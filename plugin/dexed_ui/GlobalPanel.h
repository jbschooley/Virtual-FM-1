// GlobalPanel -- algorithm, feedback, LFO and pitch envelope in Dexed's
// layout (the right 539 px of Dexed's global strip), plus this project's own
// left area with the preset name and transpose. 864 x 144.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "AlgoDisplay.h"
#include "DXComponents.h"
#include "DXLookNFeel.h"

class GlobalPanel : public juce::Component {
public:
    GlobalPanel(juce::AudioProcessorValueTreeState& apvts, uint8_t* vced, DXLookNFeel& lnf);

    void paint(juce::Graphics& g) override;
    void refresh();

    juce::TextEditor name;       // the preset name; the owner binds onReturnKey/onFocusLost
    juce::Label slotLabel;
    void setOpStatus(const char* s) { algoDisplay_->opStatus = s; }

private:
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<DXSlider> knob(const juce::String& name, int x, int y, std::unique_ptr<SA>& att, const juce::String& paramId, int envPos = -1);

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
