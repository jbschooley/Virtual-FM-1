#include "OperatorPanel.h"

#include <cmath>

#include "Params.h"

using namespace juce;

std::unique_ptr<DXSlider> OperatorPanel::knob(const String& name, int x, int y, std::unique_ptr<SA>& att, const String& paramId, int envPos) {
    auto s = std::make_unique<DXSlider>(name);
    s->setSliderStyle(Slider::RotaryVerticalDrag);
    s->setTextBoxStyle(Slider::NoTextBox, false, 80, 20);
    s->setPopupDisplayEnabled(true, true, this);
    s->setBounds(x, y, 34, 34);
    if (envPos >= 0) s->onDragStart = [this, envPos] { env_->vPos = char(envPos); env_->repaint(); };
    addAndMakeVisible(*s);
    att = std::make_unique<SA>(apvts_, paramId, *s);
    return s;
}

OperatorPanel::OperatorPanel(AudioProcessorValueTreeState& apvts, int op, uint8_t* vced, DXLookNFeel& lnf)
    : op_(op), base_((6 - op) * 21), vced_(vced), lnf_(lnf), apvts_(apvts) {
    setSize(287, 218);
    auto id = [this](int off) { return Params::vcedId(base_ + off); };

    for (int i = 0; i < 4; ++i) {
        egl_[i] = knob("EG level " + String(i + 1), 5 + 28 * i, 128, eglA_[i], id(4 + i), i + 1);
        egr_[i] = knob("EG rate " + String(i + 1), 5 + 28 * i, 169, egrA_[i], id(i), i + 1);
    }
    level_ = knob("Level", 245, 76, levelA_, id(16));
    fine_ = knob("Fine", 78, 24, fineA_, id(19));
    coarse_ = knob("Coarse", 43, 24, coarseA_, id(18));
    detune_ = knob("Detune", 6, 24, detuneA_, id(20));
    lDepth_ = knob("L depth", 131, 115, lDepthA_, id(9));
    rDepth_ = knob("R depth", 241, 115, rDepthA_, id(10));
    rateScale_ = knob("Rate scaling", 186, 179, rateScaleA_, id(13));
    keyVel_ = knob("Key velocity", 204, 76, keyVelA_, id(15));
    ampMod_ = knob("Amp mod sens", 140, 76, ampModA_, id(14));

    breakPoint_ = std::make_unique<Slider>("Break point");
    breakPoint_->setSliderStyle(Slider::LinearHorizontal);
    breakPoint_->setTextBoxStyle(Slider::NoTextBox, false, 80, 20);
    breakPoint_->setPopupDisplayEnabled(true, true, this);
    breakPoint_->setScrollWheelEnabled(false);
    breakPoint_->setBounds(178, 130, 54, 24);
    addAndMakeVisible(*breakPoint_);
    brkA_ = std::make_unique<SA>(apvts_, id(8), *breakPoint_);

    opMode_ = std::make_unique<ToggleButton>("Fixed");
    opMode_->setButtonText({});
    opMode_->setBounds(146, 19, 48, 26);
    opMode_->onStateChange = [this] { repaint(); };
    addAndMakeVisible(*opMode_);
    opModeA_ = std::make_unique<BA>(apvts_, id(17), *opMode_);

    lCurve_ = std::make_unique<ComboBoxImage>();
    for (auto* t : {"-LN", "-EX", "+EX", "+LN"}) lCurve_->addItem(t, lCurve_->getNumItems() + 1);
    int posLeft[] = {0, 5, 4, 3};
    lCurve_->setImage(lnf_.imageScaling, posLeft);
    lCurve_->setBounds(128, 170, 36, 26);
    addAndMakeVisible(*lCurve_);
    lCurveA_ = std::make_unique<CA>(apvts_, id(11), *lCurve_);

    rCurve_ = std::make_unique<ComboBoxImage>();
    for (auto* t : {"-LN", "-EX", "+EX", "+LN"}) rCurve_->addItem(t, rCurve_->getNumItems() + 1);
    int posRight[] = {3, 2, 1, 0};
    rCurve_->setImage(lnf_.imageScaling, posRight);
    rCurve_->setBounds(240, 170, 36, 26);
    addAndMakeVisible(*rCurve_);
    rCurveA_ = std::make_unique<CA>(apvts_, id(12), *rCurve_);

    env_ = std::make_unique<EnvDisplay>();
    env_->pvalues = vced_ + base_;
    env_->setBounds(16, 83, 94, 30);
    addAndMakeVisible(*env_);

    freq_.setBounds(15, 10, 95, 10);
    freq_.setFont(FontOptions(10.0f));
    freq_.setJustificationType(Justification::centred);
    freq_.setColour(Label::textColourId, Colours::white);
    freq_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(freq_);
    refresh();
}

void OperatorPanel::paint(Graphics& g) {
    g.drawImage(lnf_.imageOperator, 0, 0, 287, 218, 0, 0, 574, 436);
    g.setColour(enabled ? Colours::white : DXLookNFeel::roundBackground);
    g.setFont(FontOptions(30.0f));
    g.drawText(String(op_), 250, 14, 30, 30, Justification::centred, true);
    bool fixed = opMode_->getToggleState();
    g.drawImage(lnf_.imageLight, 127, 24, 14, 14, 0, fixed ? 0 : 28, 28, 28);
    g.drawImage(lnf_.imageLight, 198, 24, 14, 14, 0, !fixed ? 0 : 28, 28, 28);
}

void OperatorPanel::mouseDown(const MouseEvent& e) {
    if (Rectangle<int>(226, 8, 60, 40).contains(e.getPosition())) {
        enabled = !enabled;
        if (onEnabledChanged) onEnabledChanged(op_, enabled);
        repaint();
    }
}

void OperatorPanel::refresh() {
    const uint8_t* p = vced_ + base_;
    float coarse = p[18], fine = p[19];
    int det = int(p[20]) - 7;
    String txt;
    if (p[17] == 0) {
        if (coarse == 0) coarse = 0.5f;
        txt << "f = " << (coarse + (coarse * (fine / 100)));
    } else {
        float f = std::pow(10.0f, float(int(coarse) & 3));
        f = f * std::exp(float(M_LN10) * (fine / 100));
        txt << f << " Hz";
    }
    if (det > 0) txt << " +" << det;
    else if (det < 0) txt << " " << det;
    if (txt != lastFreq_) { lastFreq_ = txt; freq_.setText(txt, dontSendNotification); }
    if (std::memcmp(lastEnv_, p, 8) != 0) { std::memcpy(lastEnv_, p, 8); env_->repaint(); }
}
