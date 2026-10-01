#include "GlobalPanel.h"

#include <cstring>

#include "Params.h"

using namespace juce;

std::unique_ptr<DXSlider> GlobalPanel::knob(const String& name, int x, int y, std::unique_ptr<SA>& att, const String& paramId, int envPos) {
    auto s = std::make_unique<DXSlider>(name);
    s->setSliderStyle(Slider::RotaryVerticalDrag);
    s->setTextBoxStyle(Slider::NoTextBox, false, 80, 20);
    s->setPopupDisplayEnabled(true, true, this);
    s->setBounds(x, y, 34, 34);
    if (envPos >= 0) s->onDragStart = [this, envPos] { pitchEnv_->vPos = char(envPos); pitchEnv_->repaint(); };
    addAndMakeVisible(*s);
    att = std::make_unique<SA>(apvts_, paramId, *s);
    return s;
}

GlobalPanel::GlobalPanel(AudioProcessorValueTreeState& apvts, uint8_t* vced, DXLookNFeel& lnf)
    : vced_(vced), lnf_(lnf), apvts_(apvts) {
    setSize(864, 144);

    algoDisplay_ = std::make_unique<AlgoDisplay>();
    algoDisplay_->algo = &algoValue_;
    algoDisplay_->setBounds(335, 30, 152, 91);
    addAndMakeVisible(*algoDisplay_);
    algo_ = knob("Algorithm", 501, 22, algoA_, Params::vcedId(134));
    feedback_ = knob("Feedback", 501, 81, feedbackA_, Params::vcedId(135));

    lfoWave_ = std::make_unique<ComboBoxImage>();
    for (auto* t : {"TRIANGLE", "SAW DOWN", "SAW UP", "SQUARE", "SINE", "S&HOLD"}) lfoWave_->addItem(t, lfoWave_->getNumItems() + 1);
    lfoWave_->setImage(lnf_.imageLFO);
    lfoWave_->setBounds(583, 8, 36, 26);
    addAndMakeVisible(*lfoWave_);
    lfoWaveA_ = std::make_unique<CA>(apvts_, Params::vcedId(142), *lfoWave_);

    pms_ = knob("Pitch mod sens", 666, 5, pmsA_, Params::vcedId(143));
    lfoSpeed_ = knob("LFO speed", 564, 50, lfoSpeedA_, Params::vcedId(137));
    lfoDelay_ = knob("LFO delay", 603, 50, lfoDelayA_, Params::vcedId(138));
    lfoPmd_ = knob("LFO pitch mod depth", 646, 50, lfoPmdA_, Params::vcedId(139));
    lfoAmd_ = knob("LFO amp mod depth", 686, 50, lfoAmdA_, Params::vcedId(140));

    lfoSync_ = std::make_unique<ToggleButton>("LFO key sync");
    lfoSync_->setButtonText({});
    lfoSync_->setBounds(565, 96, 48, 26);
    lfoSync_->onStateChange = [this] { repaint(); };
    addAndMakeVisible(*lfoSync_);
    lfoSyncA_ = std::make_unique<BA>(apvts_, Params::vcedId(141), *lfoSync_);
    oscSync_ = std::make_unique<ToggleButton>("OSC key sync");
    oscSync_->setButtonText({});
    oscSync_->setBounds(650, 96, 48, 26);
    oscSync_->onStateChange = [this] { repaint(); };
    addAndMakeVisible(*oscSync_);
    oscSyncA_ = std::make_unique<BA>(apvts_, Params::vcedId(136), *oscSync_);

    pitchEnv_ = std::make_unique<PitchEnvDisplay>();
    pitchEnv_->pvalues = vced_ + 126;
    pitchEnv_->setBounds(751, 10, 93, 30);
    addAndMakeVisible(*pitchEnv_);
    const int lx[4] = {739, 767, 795, 823}, ly[4] = {57, 57, 56, 56};
    for (int i = 0; i < 4; ++i) {
        pegL_[i] = knob("Pitch EG level " + String(i + 1), lx[i], ly[i], pegLA_[i], Params::vcedId(130 + i), i);
        pegR_[i] = knob("Pitch EG rate " + String(i + 1), lx[i], 96, pegRA_[i], Params::vcedId(126 + i), i);
    }

    // this project's left area
    name.setBounds(16, 36, 190, 26);
    name.setInputRestrictions(10);
    name.setFont(FontOptions(18.0f));
    addAndMakeVisible(name);
    slotLabel.setBounds(16, 66, 206, 34);
    slotLabel.setJustificationType(juce::Justification::topLeft);
    slotLabel.setFont(FontOptions(13.0f));
    slotLabel.setColour(Label::textColourId, Colours::white.withAlpha(0.8f));
    addAndMakeVisible(slotLabel);
    transpose_ = knob("Transpose", 250, 44, transposeA_, Params::vcedId(144));
    storeButton.setBounds(16, 104, 58, 26);
    revertButton.setBounds(78, 104, 58, 26);
    sendButton.setBounds(140, 104, 96, 26);
    liveButton.setBounds(242, 104, 48, 26);
    storeButton.setTooltip("Save the editor's changes into this preset in the plugin's library");
    revertButton.setTooltip("Drop the editor's changes and go back to the stored preset");
    sendButton.setTooltip("Play this sound on the FM-1 without saving it there");
    liveButton.setTooltip("Send every change to the FM-1 as you edit, without saving it there");
    for (auto* c : std::initializer_list<juce::Component*>{&storeButton, &revertButton, &sendButton, &liveButton}) addAndMakeVisible(c);
    refresh();
}

void GlobalPanel::paint(Graphics& g) {
    g.setColour(DXLookNFeel::background);
    g.fillRoundedRectangle(0.0f, 0.0f, 320.0f, 144.0f, 8.0f);
    g.drawImage(lnf_.imageGlobal, 325, 0, 539, 144, 650, 0, 1078, 288);
    g.setColour(Colours::white);
    g.setFont(FontOptions(15.0f, Font::bold));
    g.drawText("Preset", 16, 10, 100, 20, Justification::centredLeft, true);
    g.setFont(FontOptions(14.0f));
    g.drawText("Transpose", 226, 82, 82, 16, Justification::centred, true);
    g.setFont(FontOptions(13.0f));
    g.setColour(Colours::white);
    g.drawText("Live", 292, 104, 30, 26, Justification::centredLeft, true);
    g.drawImage(lnf_.imageLight, 619, 102, 14, 14, 0, lfoSync_->getToggleState() ? 28 : 0, 28, 28);
    g.drawImage(lnf_.imageLight, 705, 102, 14, 14, 0, oscSync_->getToggleState() ? 28 : 0, 28, 28);
}

void GlobalPanel::refresh() {
    char a = char(vced_[134] & 31);
    if (a != algoValue_) { algoValue_ = a; algoDisplay_->repaint(); }
    if (std::memcmp(lastPeg_, vced_ + 126, 8) != 0) { std::memcpy(lastPeg_, vced_ + 126, 8); pitchEnv_->repaint(); }
}
