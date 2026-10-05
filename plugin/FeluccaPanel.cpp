#include "FeluccaPanel.h"

#if FM1_FELUCCA

namespace {

// Felucca 1.0's per-track parameters (core.h P_*), in its own order, grouped as its
// pages group them. Ids beyond what a version has are skipped. Not here: P_ED_FX and the
// DIGITAL engine's operator envelopes (61-80), which 1.0 neither builds nor shows.
struct GroupDef { const char* title; int first, last; };
const GroupDef kTrackGroups[] = {
    {"Engine", -1, -1},   // the engine's eight: from firstEngineParam()
    {"Envelope", 1, 4},
    {"Envelope to", 5, 7},
    {"LFO", 9, 12},
    {"LFO to", 13, 16},
    {"Modulation", 49, 60},
    {"Voice", 37, 44},
    {"Chord", 81, 82},
    {"Arpeggiator", 17, 24},
    {"Scale", 25, 28},
    {"Sequencer", 29, 32},
    {"Sends", 33, 36},
    {"Slicer", 45, 48},
    {"Mix", 0, 0},
};
// the global settings worth editing here (the rest are the device's own pages and actions)
const int kGlobals[] = {0, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 24};

const juce::Colour kBg(0xff26262e), kBox(0xff30303a), kText(0xffe8e8ee), kDim(0xffa0a0b0), kAccent(0xff6fb7c9);

}  // namespace

// ---- Felucca's front panel -------------------------------------------------------------------

// An endless knob: dragging up or turning the wheel clockwise gives steps, as the encoders do.
struct FeluccaDeviceView::Knob : juce::Component {
    std::function<void(int)> turned;
    juce::String name;
    float angle = 0.0f, dragRest = 0.0f;
    int lastY = 0;
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        auto label = r.removeFromBottom(14.0f);
        const float d = std::min(r.getWidth(), r.getHeight()) - 4.0f;
        auto c = r.withSizeKeepingCentre(d, d);
        g.setColour(juce::Colour(0xff3a3a46));
        g.fillEllipse(c);
        g.setColour(juce::Colour(0xff6fb7c9));
        g.drawEllipse(c, 1.5f);
        const auto mid = c.getCentre();
        g.drawLine(mid.x, mid.y, mid.x + std::sin(angle) * d * 0.42f, mid.y - std::cos(angle) * d * 0.42f, 2.0f);
        g.setColour(juce::Colour(0xffa0a0b0));
        g.setFont(juce::FontOptions(10.0f));
        g.drawText(name, label, juce::Justification::centred);
    }
    void step(int n) {
        if (n == 0) return;
        angle += 0.2f * float(n);
        if (turned) turned(n);
        repaint();
    }
    void mouseDown(const juce::MouseEvent& e) override { lastY = e.y; dragRest = 0.0f; }
    void mouseDrag(const juce::MouseEvent& e) override {
        dragRest += float(lastY - e.y) / 6.0f;   // 6 pixels a step
        lastY = e.y;
        const int n = int(dragRest);
        dragRest -= float(n);
        step(n);
    }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override {
        step(w.deltaY > 0 ? 1 : w.deltaY < 0 ? -1 : 0);
    }
};

// One of the 27 keys (from F, as Felucca's key map has them): held while the mouse is down.
struct FeluccaDeviceView::Key : juce::Component {
    std::function<void(bool)> pressed;
    bool black = false, down = false;
    void paint(juce::Graphics& g) override {
        g.setColour(down ? juce::Colour(0xff6fb7c9) : black ? juce::Colour(0xff0e0e12) : juce::Colour(0xffd8d8e0));
        g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 3.0f);
        if (black) { g.setColour(juce::Colour(0xff50505c)); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 3.0f, 1.0f); }
    }
    void mouseDown(const juce::MouseEvent&) override { down = true; if (pressed) pressed(true); repaint(); }
    void mouseUp(const juce::MouseEvent&) override { down = false; if (pressed) pressed(false); repaint(); }
};

FeluccaDeviceView::FeluccaDeviceView(FM1Processor& p) : proc_(p) {}

// the controls, by Felucca's own names, once there is a Felucca to ask
void FeluccaDeviceView::build() {
    auto f = engine();
    if (!f || !buttons_.isEmpty()) return;
    const auto buttonNames = f ? f->buttonNames() : std::vector<std::string>{};
    for (size_t i = 0; i < buttonNames.size(); ++i) {
        auto* b = buttons_.add(new juce::TextButton(buttonNames[i]));
        const int label = int(i);
        // held while the mouse is down: HOME, SAVE, SEQ and REC mean something else held
        b->onStateChange = [this, b, label] {
            const bool down = b->isDown();
            if (down == b->getProperties()["down"].operator bool()) return;
            b->getProperties().set("down", down);
            if (auto e = engine()) e->button(label, down);
        };
        addAndMakeVisible(b);
    }
    const auto knobNames = f ? f->knobNames() : std::vector<std::string>{};
    for (size_t i = 0; i < knobNames.size(); ++i) {
        auto* k = knobs_.add(new Knob());
        k->name = knobNames[i];
        const int role = int(i);
        k->turned = [this, role](int n) { if (auto e = engine()) e->knob(role, n); };
        addAndMakeVisible(k);
    }
    for (int i = 0; i < 27; ++i) {
        auto* k = keys_.add(new Key());
        k->black = ((0x54A >> ((i + 5) % 12)) & 1) != 0;   // seq.c key_black: key 0 is an F
        k->pressed = [this, i](bool down) { if (auto e = engine()) e->key(i, down); };
        addAndMakeVisible(k);
    }
    resized();
}

FeluccaDeviceView::~FeluccaDeviceView() { stopTimer(); }

void FeluccaDeviceView::visibilityChanged() {
    if (isVisible()) { build(); startTimerHz(30); } else stopTimer();
}

void FeluccaDeviceView::timerCallback() {
    auto f = engine();
    if (!f) return;
    f->draw(px_);
    juce::Image::BitmapData d(screen_, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < 240; ++y)
        for (int x = 0; x < 240; ++x) {
            const uint16_t v = px_[size_t(y * 240 + x)];
            d.setPixelColour(x, y, juce::Colour(uint8_t(((v >> 11) & 31) * 255 / 31), uint8_t(((v >> 5) & 63) * 255 / 63), uint8_t((v & 31) * 255 / 31)));
        }
    repaint(screenArea_);
}

void FeluccaDeviceView::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff26262e));
    g.drawImage(screen_, screenArea_.toFloat(), juce::RectanglePlacement::stretchToFit);
    g.setColour(juce::Colour(0xff50505c));
    g.drawRect(screenArea_.expanded(1));
}

void FeluccaDeviceView::resized() {
    auto r = getLocalBounds().reduced(8);
    auto keys = r.removeFromBottom(std::min(70, r.getHeight() / 5));
    r.removeFromBottom(8);
    // the screen at a whole multiple of 240 that fits, the controls beside it
    const int side = std::max(240, std::min(r.getHeight(), r.getWidth() * 3 / 5) / 240 * 240);
    screenArea_ = r.removeFromLeft(side).withHeight(side);
    r.removeFromLeft(12);
    auto knobRow = r.removeFromTop(std::min(90, r.getHeight() / 3));
    const int kw = knobRow.getWidth() / std::max(1, knobs_.size());
    for (auto* k : knobs_) k->setBounds(knobRow.removeFromLeft(kw).reduced(2));
    r.removeFromTop(8);
    const int cols = 4, rows = (buttons_.size() + cols - 1) / cols;
    const int bw = r.getWidth() / cols, bh = std::min(40, r.getHeight() / std::max(1, rows));
    for (int i = 0; i < buttons_.size(); ++i)
        buttons_[i]->setBounds(r.getX() + (i % cols) * bw + 2, r.getY() + (i / cols) * bh + 2, bw - 4, bh - 4);
    // a keyboard: the white keys side by side, each black one over the gap before the next white
    int whites = 0;
    for (auto* k : keys_) whites += k->black ? 0 : 1;
    const float ww = float(keys.getWidth()) / float(std::max(1, whites));
    int w = 0;
    for (auto* k : keys_) {
        if (k->black) {
            const int x = keys.getX() + int(float(w) * ww - ww * 0.3f);
            k->setBounds(x, keys.getY(), int(ww * 0.6f), keys.getHeight() * 3 / 5);
            k->toFront(false);
        } else {
            k->setBounds(keys.getX() + int(float(w) * ww), keys.getY(), int(ww), keys.getHeight());
            ++w;
        }
    }
}

// ---- the parameters ---------------------------------------------------------------------------

FeluccaPanel::FeluccaPanel(FM1Processor& p) : proc_(p), device_(p) {
    for (int i = 0; i < 4; ++i) {
        auto& b = trackButtons_[i];
        b.setButtonText("PART " + juce::String(i + 1));
        b.setClickingTogglesState(true);
        b.setRadioGroupId(4701);
        b.onClick = [this, i] { if (trackButtons_[i].getToggleState()) selectTrack(i); };
        addAndMakeVisible(b);
    }
    for (auto* c : std::initializer_list<juce::Component*>{&engineBox_, &presetBox_, &hostTempo_, &info_, &view_, &deviceButton_}) addAndMakeVisible(c);
    addChildComponent(device_);
    deviceButton_.setClickingTogglesState(true);
    deviceButton_.setTooltip("Felucca's own screen, buttons, knobs and keys: its sequencer, projects and user presets as on the device");
    deviceButton_.onClick = [this] { showDevice(deviceButton_.getToggleState()); };
    engineBox_.setTooltip("The part's engine: its own defaults and first preset");
    engineBox_.onChange = [this] {
        auto e = engine();
        if (loading_ || !e) return;
        e->setEngine(track_, engineBox_.getSelectedId() - 1);
        proc_.feluccaChanged(track_);
        build();
    };
    presetBox_.setTooltip("Felucca's built-in presets for this engine");
    presetBox_.onChange = [this] {
        auto e = engine();
        if (loading_ || !e) return;
        e->applyPreset(track_, presetBox_.getSelectedId() - 1);
        proc_.feluccaChanged(track_);
        loadValues();
    };
    hostTempo_.setTooltip("On: Felucca's tempo is the host's, and the host's PLAY and STOP start and stop its sequencer. Off: its own BPM (Global > BPM) and PLAY. Saved with the project.");
    hostTempo_.onClick = [this] {
        auto s = proc_.settings();
        s.hostTempo = hostTempo_.getToggleState();
        proc_.setSettings(s);
        updateTempoControl();
    };
    info_.setColour(juce::Label::textColourId, kDim);
    info_.setFont(juce::FontOptions(12.0f));
    info_.setText("MIDI channels 1-4 play the parts, as on the FM-1 with Felucca.", juce::dontSendNotification);
    view_.setViewedComponent(&content_, false);
    view_.setScrollBarsShown(true, false);
    trackButtons_[0].setToggleState(true, juce::dontSendNotification);
    refresh();
    startTimerHz(10);   // automation shows as it plays
}

void FeluccaPanel::showDevice(bool on) {
    device_.setVisible(on);
    view_.setVisible(!on);
    info_.setVisible(!on);
    for (auto& b : trackButtons_) b.setVisible(!on);
    engineBox_.setVisible(!on);
    presetBox_.setVisible(!on);
    if (!on) refresh();   // what the device changed
}

void FeluccaPanel::timerCallback() {
    if (isShowing() && !isMouseButtonDownAnywhere()) loadValues();
}

void FeluccaPanel::refresh() {
    hostTempo_.setToggleState(proc_.settings().hostTempo, juce::dontSendNotification);
    const auto scroll = view_.getViewPosition();
    build();
    view_.setViewPosition(scroll);
}

void FeluccaPanel::updateTempoControl() {
    for (auto& g : groups_)
        for (auto& c : g.controls)
            if (c.global && c.id == 0 && c.slider) {   // G_BPM
                c.slider->setEnabled(!proc_.settings().hostTempo);
                c.slider->setTooltip(proc_.settings().hostTempo ? "The host's tempo, while \"Tempo follows the host\" is on" : juce::String());
            }
}

void FeluccaPanel::selectTrack(int t) {
    track_ = t;
    build();
}

void FeluccaPanel::build() {
    auto held = engine();
    auto* f = held.get();
    groups_.clear();
    content_.removeAllChildren();
    loading_ = true;
    engineBox_.clear(juce::dontSendNotification);
    presetBox_.clear(juce::dontSendNotification);
    if (f == nullptr) { loading_ = false; return; }
    if (track_ >= f->parts()) track_ = 0;
    for (int e : f->enginesShown()) engineBox_.addItem(f->engineName(e), e + 1);   // Felucca's order
    engineBox_.setSelectedId(f->engineOf(track_) + 1, juce::dontSendNotification);
    auto presets = f->presetNames(f->engineOf(track_));
    for (size_t i = 0; i < presets.size(); ++i)
        if (!presets[i].empty()) presetBox_.addItem(presets[i], int(i) + 1);   // (an alias: not offered)
    presetBox_.setSelectedId(f->presetOf(track_) + 1, juce::dontSendNotification);
    auto addControl = [&](Group& g, int id, const FeluccaEngine::Desc& d) {
        if (d.label.empty() || d.max <= d.min) return;   // fixed or unused
        Control c;
        c.id = id;
        c.global = g.global;
        c.label = std::make_unique<juce::Label>();
        c.label->setText(juce::String(d.label) + (d.unit.empty() ? "" : " (" + juce::String(d.unit) + ")"), juce::dontSendNotification);
        c.label->setColour(juce::Label::textColourId, kText);
        c.label->setFont(juce::FontOptions(12.0f));
        content_.addAndMakeVisible(*c.label);
        const bool global = g.global;
        if (!d.names.empty()) {
            c.box = std::make_unique<juce::ComboBox>();
            for (size_t i = 0; i < d.names.size(); ++i) c.box->addItem(d.names[i].empty() ? juce::String(d.min + int(i)) : juce::String(d.names[i]), int(i) + 1);
            const int min = d.min;
            auto* box = c.box.get();
            box->onChange = [this, box, id, min, global] {
                auto e = engine();
                if (loading_ || !e) return;
                const int v = min + box->getSelectedId() - 1;
                auto* hp = proc_.feluccaParam(global ? -1 : track_, id);
                if (hp) hp->beginChangeGesture();
                if (global) e->setGlobal(id, v); else e->setParam(track_, id, v);
                proc_.feluccaChanged(global ? -1 : track_);   // the host's parameter follows
                if (hp) hp->endChangeGesture();
            };
            content_.addAndMakeVisible(*c.box);
        } else {
            c.slider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
            c.slider->setRange(d.min, d.max, 1.0);
            c.slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 18);
            c.slider->setDoubleClickReturnValue(true, d.def);
            auto* sl = c.slider.get();
            // a drag is one change for the host (automation in Touch or Latch mode records it)
            sl->onDragStart = [this, id, global] { if (auto* hp = proc_.feluccaParam(global ? -1 : track_, id)) hp->beginChangeGesture(); };
            sl->onDragEnd = [this, id, global] { if (auto* hp = proc_.feluccaParam(global ? -1 : track_, id)) hp->endChangeGesture(); };
            sl->onValueChange = [this, sl, id, global] {
                auto e = engine();
                if (loading_ || !e) return;
                const int v = int(std::lround(sl->getValue()));
                if (global) e->setGlobal(id, v); else e->setParam(track_, id, v);
                proc_.feluccaChanged(global ? -1 : track_);   // the host's parameter follows
            };
            content_.addAndMakeVisible(*c.slider);
        }
        g.controls.push_back(std::move(c));
    };
    auto addHeader = [&](Group& g) {
        g.header = std::make_unique<juce::Label>();
        g.header->setText(g.title, juce::dontSendNotification);
        g.header->setFont(juce::FontOptions(13.0f, juce::Font::bold));
        g.header->setColour(juce::Label::textColourId, kAccent);
        content_.addAndMakeVisible(*g.header);
    };
    for (const auto& def : kTrackGroups) {
        Group g;
        g.title = def.title;
        if (juce::String(def.title) == "Engine") {
            const int e = f->engineOf(track_);
            g.title = juce::String(f->engineName(e)) + ": " + f->enginePage(e, 0) + " / " + f->enginePage(e, 1);
        }
        addHeader(g);
        const int first = def.first >= 0 ? def.first : f->firstEngineParam();
        const int last = def.first >= 0 ? def.last : f->firstEngineParam() + 7;
        for (int id = first; id <= last && id < f->paramCount(); ++id) addControl(g, id, f->paramDesc(track_, id));
        if (!g.controls.empty()) groups_.push_back(std::move(g));
    }
    {
        Group g;
        g.title = "Global";
        g.global = true;
        addHeader(g);
        for (int id : kGlobals) if (id < f->globalCount()) addControl(g, id, f->globalDesc(id));
        if (!g.controls.empty()) groups_.push_back(std::move(g));
    }
    loading_ = false;
    loadValues();
    updateTempoControl();
    layoutContent();
}

void FeluccaPanel::loadValues() {
    auto held = engine();
    auto* f = held.get();
    if (!f) return;
    loading_ = true;
    for (auto& g : groups_)
        for (auto& c : g.controls) {
            const int v = c.global ? f->global(c.id) : f->param(track_, c.id);
            if (c.slider) c.slider->setValue(v, juce::dontSendNotification);
            if (c.box) {
                auto d = c.global ? f->globalDesc(c.id) : f->paramDesc(track_, c.id);
                c.box->setSelectedId(v - d.min + 1, juce::dontSendNotification);
            }
        }
    presetBox_.setSelectedId(f->presetOf(track_) + 1, juce::dontSendNotification);
    loading_ = false;
}

void FeluccaPanel::paint(juce::Graphics& g) {
    g.fillAll(kBg);
}

void FeluccaPanel::resized() {
    auto r = getLocalBounds().reduced(10);
    auto top = r.removeFromTop(30);
    for (auto& b : trackButtons_) { b.setBounds(top.removeFromLeft(84)); top.removeFromLeft(4); }
    top.removeFromLeft(12);
    engineBox_.setBounds(top.removeFromLeft(140));
    top.removeFromLeft(6);
    presetBox_.setBounds(top.removeFromLeft(170));
    top.removeFromLeft(12);
    hostTempo_.setBounds(top.removeFromLeft(200));
    deviceButton_.setBounds(top.removeFromRight(84));
    device_.setBounds(r.withTrimmedTop(6));
    r.removeFromTop(4);
    info_.setBounds(r.removeFromTop(18));
    r.removeFromTop(6);
    view_.setBounds(r);
    layoutContent();
}

// The groups in columns, as many as fit, each a header and a row per control.
void FeluccaPanel::layoutContent() {
    const int width = std::max(300, view_.getWidth() - 12);
    const int colW = 280, rowH = 24, gap = 14;
    const int cols = std::max(1, width / colW);
    std::vector<int> colY(size_t(cols), 0);
    for (auto& g : groups_) {
        const int c = int(std::min_element(colY.begin(), colY.end()) - colY.begin());
        int x = c * colW, y = colY[size_t(c)];
        g.header->setBounds(x, y, colW - 10, 20);
        y += 22;
        for (auto& ctl : g.controls) {
            ctl.label->setBounds(x, y, 96, rowH - 2);
            juce::Rectangle<int> area(x + 98, y, colW - 108, rowH - 2);
            if (ctl.slider) ctl.slider->setBounds(area);
            if (ctl.box) ctl.box->setBounds(area);
            y += rowH;
        }
        colY[size_t(c)] = y + gap;
    }
    content_.setSize(width, *std::max_element(colY.begin(), colY.end()) + 10);
}

#endif
