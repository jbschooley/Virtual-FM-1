// OperatorPanel -- one operator in Dexed's layout (287 x 218), bound to this
// plugin's parameters. The coordinates and background image are Dexed's; the
// binding and the operator on/off switch are this project's.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "DXComponents.h"
#include "DXLookNFeel.h"

class OperatorPanel : public juce::Component {
public:
    // `op` is 1..6 as printed; the VCED block is (6 - op) * 21.
    OperatorPanel(juce::AudioProcessorValueTreeState& apvts, int op, uint8_t* vced, DXLookNFeel& lnf);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void refresh();   // from the panel's timer: frequency readout and envelope display

    bool enabled = true;
    std::function<void(int op, bool on)> onEnabledChanged;

private:
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<DXSlider> knob(const juce::String& name, int x, int y, std::unique_ptr<SA>& att, const juce::String& paramId, int envPos = -1);

    int op_;
    int base_;
    uint8_t* vced_;
    DXLookNFeel& lnf_;
    juce::AudioProcessorValueTreeState& apvts_;

    std::unique_ptr<DXSlider> egl_[4], egr_[4], level_, fine_, coarse_, detune_, lDepth_, rDepth_, rateScale_, keyVel_, ampMod_;
    std::unique_ptr<SA> eglA_[4], egrA_[4], levelA_, fineA_, coarseA_, detuneA_, lDepthA_, rDepthA_, rateScaleA_, keyVelA_, ampModA_, brkA_;
    std::unique_ptr<juce::Slider> breakPoint_;
    std::unique_ptr<juce::ToggleButton> opMode_;
    std::unique_ptr<BA> opModeA_;
    std::unique_ptr<ComboBoxImage> lCurve_, rCurve_;
    std::unique_ptr<CA> lCurveA_, rCurveA_;
    std::unique_ptr<EnvDisplay> env_;
    juce::Label freq_;
    juce::String lastFreq_;
    uint8_t lastEnv_[8] = {0};
};
