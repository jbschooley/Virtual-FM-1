#include "FeluccaPanel.h"

#include "FeluccaDevice.h"

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
// SLOOP 2.3's (core.h P_*): Felucca 1.0's numbering up to the slicer (48), then CHORD (49) and
// the engine's eight; no modulation matrix
const GroupDef kSloopGroups[] = {
    {"Engine", -1, -1},
    {"Envelope", 1, 4},
    {"Envelope to", 5, 7},
    {"LFO", 9, 12},
    {"LFO to", 13, 16},
    {"Voice", 37, 44},
    {"Chord", 49, 49},
    {"Arpeggiator", 17, 24},
    {"Scale", 25, 28},
    {"Sequencer", 29, 32},
    {"Sends", 33, 36},
    {"Slicer", 45, 48},
    {"Mix", 0, 0},
};
// the global settings worth editing here (the rest are the device's own pages and actions)
const std::vector<int> kGlobals = {0, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 24};
// SLOOP's: the same up to the chorus, then its drum level and reverb and master bus (DUST, DUCK, FILT)
const std::vector<int> kSloopGlobals = {0, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 25, 26, 27, 28, 29};

const char* const kInfoText = "The keyboard plays the selected part. MIDI channels 1-4 play parts 1-4 (GLO > SYSTEM > ROUT SEL: every channel the selected part).";
const char* const kSloopInfoText = "The keyboard plays the selected part. MIDI channels 1-3 play parts 1-3, the drum channel (10; GLO) the drums, any other the selected part.";
bool isSloop(const FeluccaEngine* f) { return f != nullptr && f->flavor() == FeluccaEngine::Flavor::Sloop; }
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
        auto shown = name;   // narrow: ALGO, PRESET, K1 ...
        if (getWidth() < 64) shown = name.replace("ALGORITHM", "ALGO").replace("PRESETS", "PRESET").replace("KNOB ", "K");
        g.drawText(shown, label, juce::Justification::centred);
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
    g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);   // pixels stay sharp
    g.drawImage(screen_, screenArea_.toFloat(), juce::RectanglePlacement::stretchToFit);
    g.setColour(juce::Colour(0xff50505c));
    g.drawRect(screenArea_.expanded(1));
}

void FeluccaDeviceView::resized() {
    auto r = getLocalBounds().reduced(8);
    const bool narrow = getWidth() < 600;
    auto keys = r.removeFromBottom(std::min(narrow ? 56 : 70, r.getHeight() / 5));
    r.removeFromBottom(8);
    const int cols = narrow ? 5 : 4, rows = (buttons_.size() + cols - 1) / cols;
    // the screen at a whole multiple of its 240 pixels on the display's own pixels (sharp at
    // any display scale): beside the controls, at most about 360 points so they keep their
    // room; on a phone above them, as wide as fits
    const float scale = std::max(1.0f, float(juce::Component::getApproximateScaleFactorForComponent(this))
                                         * float(juce::Desktop::getInstance().getDisplays().getDisplayForRect(getScreenBounds()) != nullptr
                                                     ? juce::Desktop::getInstance().getDisplays().getDisplayForRect(getScreenBounds())->scale : 1.0));
    const int controls = 10 + 64 + 8 + rows * 40;   // narrow: what goes below the screen
    const float room = narrow ? float(std::min(r.getWidth(), r.getHeight() - controls))
                              : float(std::min({r.getHeight(), r.getWidth() / 2, 360}));
    const int k = std::max(1, int(room * scale / 240.0f));
    const int side = int(std::round(240.0f * float(k) / scale));
    if (narrow) {
        screenArea_ = r.removeFromTop(side).withSizeKeepingCentre(side, side);
        r.removeFromTop(10);
    } else {
        screenArea_ = r.removeFromLeft(side).withHeight(side);
        r.removeFromLeft(12);
    }
    auto knobRow = r.removeFromTop(narrow ? 64 : std::min(90, r.getHeight() / 3));
    const int kw = knobRow.getWidth() / std::max(1, knobs_.size());
    for (auto* k : knobs_) k->setBounds(knobRow.removeFromLeft(kw).reduced(2));
    r.removeFromTop(8);

    const int bw = r.getWidth() / cols, bh = std::min(40, r.getHeight() / std::max(1, rows));
    for (int i = 0; i < buttons_.size(); ++i)
        buttons_[i]->setBounds(r.getX() + (i % cols) * bw + 2, r.getY() + (i / cols) * bh + 2, bw - 4, bh - 4);
    if (narrow)   // the keys take what is left, up to 100 high
        keys.setTop(std::max(keys.getBottom() - 100, r.getY() + rows * bh + 8));
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

// a tray with an arrow into it (down: to the FM-1) or out of it (up: from it)
static std::unique_ptr<juce::Drawable> syncIcon(bool down, juce::Colour c) {
    juce::Path p;
    p.startNewSubPath(4.0f, 15.0f); p.lineTo(4.0f, 20.0f); p.lineTo(20.0f, 20.0f); p.lineTo(20.0f, 15.0f);   // the tray
    p.startNewSubPath(12.0f, down ? 3.0f : 15.0f); p.lineTo(12.0f, down ? 15.0f : 3.0f);                    // the arrow's shaft
    const float tip = down ? 15.0f : 3.0f, back = down ? 10.0f : 8.0f;
    p.startNewSubPath(7.5f, back); p.lineTo(12.0f, tip); p.lineTo(16.5f, back);                             // its head
    auto d = std::make_unique<juce::DrawablePath>();
    d->setPath(p);
    d->setFill(juce::FillType());
    d->setStrokeFill(c);
    d->setStrokeType(juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    return d;
}

FeluccaSoundPage::FeluccaSoundPage(FM1Processor& p) : proc_(p) {
    for (auto* b : {&pullSound_, &sendSound_}) {
        const bool down = b == &sendSound_;
        auto on = syncIcon(down, kText), off = syncIcon(down, kDim.withAlpha(0.4f));
        b->setImages(on.get(), nullptr, nullptr, off.get());
        b->setColour(juce::DrawableButton::backgroundColourId, kBox);
        addAndMakeVisible(*b);
    }
    pullSound_.setTooltip("The selected part's sound from the connected FM-1 into this instance: its engine, preset, every value and FM6 patch");
    sendSound_.setTooltip("The selected part's sound from here to the connected FM-1: what it plays now, not saved there");
    pullSound_.onClick = [this] { proc_.feluccaPullSound(track_); };
    sendSound_.onClick = [this] { proc_.feluccaSendSound(track_); };
    for (int i = 0; i < 4; ++i) {
        auto& b = trackButtons_[i];
        b.setButtonText("PART " + juce::String(i + 1));
        b.setClickingTogglesState(true);
        b.setRadioGroupId(4701);
        b.onClick = [this, i] { if (trackButtons_[i].getToggleState()) selectTrack(i); };
        addAndMakeVisible(b);
    }
    for (auto* c : std::initializer_list<juce::Component*>{&engineBox_, &presetBox_, &hostTempo_, &info_, &view_}) addAndMakeVisible(c);
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
    info_.setText(infoText(), juce::dontSendNotification);
    view_.setViewedComponent(&content_, false);
    view_.setScrollBarsShown(true, false);
    trackButtons_[0].setToggleState(true, juce::dontSendNotification);
    refresh();
    startTimerHz(10);   // automation shows as it plays
}

juce::String FeluccaSoundPage::infoText() const { return isSloop(engine().get()) ? kSloopInfoText : kInfoText; }

void FeluccaSoundPage::setStatus(const juce::String& s) {
    info_.setText(s.isNotEmpty() ? s : infoText(), juce::dontSendNotification);
    statusUntil_ = juce::Time::getMillisecondCounter() + 8000;
}

void FeluccaSoundPage::visibilityChanged() {
    if (isVisible()) refresh();   // what the device (or the library) changed meanwhile
}

void FeluccaSoundPage::timerCallback() {
    if (statusUntil_ != 0 && juce::Time::getMillisecondCounter() > statusUntil_) {   // the hint again
        statusUntil_ = 0;
        info_.setText(infoText(), juce::dontSendNotification);
    }
    {   // the sound's sync: with an FM-1 running Felucca, not while another job (Live) runs
        const bool can = proc_.feluccaSynth() && !proc_.session.busy();
        pullSound_.setEnabled(can);
        sendSound_.setEnabled(can);
    }
    if (auto e = engine(); e && e->selected() != track_ && e->selected() < e->tracks()) {   // chosen on the device (its panel, a synced FM-1)
        track_ = e->selected();
        trackButtons_[track_].setToggleState(true, juce::dontSendNotification);
        if (onPartChanged) onPartChanged();
    }
    // the part, or its engine, changed (on the Device tab, by a loaded project or user preset,
    // or on a synced FM-1): the controls again, once they show
    if (auto e = engine(); e && isShowing() && (builtTrack_ != track_ || builtEngine_ != e->engineOf(track_))) refresh();
    if (isShowing() && !isMouseButtonDownAnywhere()) loadValues();
}

void FeluccaSoundPage::refresh() {
    hostTempo_.setToggleState(proc_.settings().hostTempo, juce::dontSendNotification);
    const auto scroll = view_.getViewPosition();
    build();
    view_.setViewPosition(scroll);
}

void FeluccaSoundPage::updateTempoControl() {
    for (auto& g : groups_)
        for (auto& c : g.controls)
            if (c.global && c.id == 0 && c.slider) {   // G_BPM
                c.slider->setEnabled(!proc_.settings().hostTempo);
                c.slider->setTooltip(proc_.settings().hostTempo ? "The host's tempo, while \"Tempo follows the host\" is on" : juce::String());
            }
}

void FeluccaSoundPage::selectTrack(int t) {
    track_ = t;
    if (auto e = engine()) e->select(t);   // Felucca's selected part too: the keys play it
    if (onPartChanged) onPartChanged();
    build();
}

void FeluccaSoundPage::build() {
    auto held = engine();
    auto* f = held.get();
    groups_.clear();
    content_.removeAllChildren();
    loading_ = true;
    engineBox_.clear(juce::dontSendNotification);
    presetBox_.clear(juce::dontSendNotification);
    if (f == nullptr) { loading_ = false; return; }
    if (track_ >= f->tracks()) track_ = 0;
    const bool sloop = isSloop(f);
    // SLOOP's fourth track is its drum track: no engine or preset to choose, its kit (E1) only
    const bool drums = track_ >= f->parts();
    for (int i = 0; i < 4; ++i) {
        trackButtons_[i].setButtonText(sloop && i >= f->parts() ? juce::String("DRUMS") : "PART " + juce::String(i + 1));
        trackButtons_[i].setVisible(i < f->tracks());
    }
    builtTrack_ = track_;
    builtEngine_ = f->engineOf(track_);
    engineBox_.setEnabled(!drums);
    presetBox_.setEnabled(!drums);
    if (!drums) {
        for (int e : f->enginesShown()) engineBox_.addItem(f->engineName(e), e + 1);   // Felucca's order
        engineBox_.setSelectedId(f->engineOf(track_) + 1, juce::dontSendNotification);
        auto presets = f->presetNames(f->engineOf(track_));
        for (size_t i = 0; i < presets.size(); ++i)
            if (!presets[i].empty()) presetBox_.addItem(presets[i], int(i) + 1);   // (an alias: not offered)
        presetBox_.setSelectedId(f->presetOf(track_) + 1, juce::dontSendNotification);
    }
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
    const std::vector<GroupDef> defs = sloop ? std::vector<GroupDef>(std::begin(kSloopGroups), std::end(kSloopGroups))
                                             : std::vector<GroupDef>(std::begin(kTrackGroups), std::end(kTrackGroups));
    for (const auto& def : defs) {
        // SLOOP's drum track: its kit, pattern and slicer (its level and reverb are globals, DRLVL
        // and DRREV; the device shows it no other track page, and its drums read nothing else)
        if (drums && juce::String(def.title) != "Engine" && juce::String(def.title) != "Sequencer" && juce::String(def.title) != "Slicer") continue;
        Group g;
        g.title = def.title;
        if (juce::String(def.title) == "Engine") {
            const int e = f->engineOf(track_);
            g.title = drums ? juce::String("Drums") : juce::String(f->engineName(e)) + ": " + f->enginePage(e, 0) + " / " + f->enginePage(e, 1);
        }
        addHeader(g);
        const int first = def.first >= 0 ? def.first : f->firstEngineParam();
        const int last = def.first >= 0 ? def.last : f->firstEngineParam() + (drums ? 0 : 7);
        for (int id = first; id <= last && id < f->paramCount(); ++id) addControl(g, id, f->paramDesc(track_, id));
        if (!g.controls.empty()) groups_.push_back(std::move(g));
    }
    {
        Group g;
        g.title = "Global";
        g.global = true;
        addHeader(g);
        for (int id : sloop ? kSloopGlobals : kGlobals) if (id < f->globalCount()) addControl(g, id, f->globalDesc(id));
        if (!g.controls.empty()) groups_.push_back(std::move(g));
    }
    loading_ = false;
    loadValues();
    updateTempoControl();
    layoutContent();
}

void FeluccaSoundPage::loadValues() {
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

void FeluccaSoundPage::paint(juce::Graphics& g) {
    g.fillAll(kBg);
}

void FeluccaSoundPage::resized() {
    auto r = getLocalBounds().reduced(10);
    if (getWidth() < kNarrow) {   // a phone or a narrow iPad: the top controls on three rows
        auto row = [&](int h = 30) { auto rr = r.removeFromTop(h); r.removeFromTop(4); return rr; };
        auto even = [](juce::Rectangle<int> rr, std::initializer_list<juce::Component*> cs) {
            const int w = rr.getWidth() / int(cs.size());
            for (auto* c : cs) c->setBounds(rr.removeFromLeft(w).reduced(2, 0));
        };
        even(row(), {&trackButtons_[0], &trackButtons_[1], &trackButtons_[2], &trackButtons_[3]});
        {
            auto rr = row();
            sendSound_.setBounds(rr.removeFromRight(30));
            rr.removeFromRight(4);
            pullSound_.setBounds(rr.removeFromRight(30));
            rr.removeFromRight(4);
            even(rr, {&engineBox_, &presetBox_});
        }
        hostTempo_.setBounds(row(26));
        info_.setBounds(row(30));
        view_.setBounds(r);
        layoutContent();
        return;
    }
    auto top = r.removeFromTop(30);
    for (auto& b : trackButtons_) { b.setBounds(top.removeFromLeft(84)); top.removeFromLeft(4); }
    top.removeFromLeft(12);
    engineBox_.setBounds(top.removeFromLeft(140));
    top.removeFromLeft(6);
    presetBox_.setBounds(top.removeFromLeft(170));
    top.removeFromLeft(10);
    pullSound_.setBounds(top.removeFromLeft(30));
    top.removeFromLeft(4);
    sendSound_.setBounds(top.removeFromLeft(30));
    r.removeFromTop(4);
    {
        auto row = r.removeFromTop(30);
        hostTempo_.setBounds(row.removeFromLeft(200));
        row.removeFromLeft(8);
        info_.setBounds(row);
    }
    r.removeFromTop(6);
    view_.setBounds(r);
    layoutContent();
}

// The groups in columns, as many as fit, each a header and a row per control.
void FeluccaSoundPage::layoutContent() {
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

// ---- the Sync page ---------------------------------------------------------------------------

FeluccaSyncPage::FeluccaSyncPage(FM1Processor& p) : proc_(p) {
    for (auto* c : std::initializer_list<juce::Component*>{&pullButton_, &sendButton_, &liveButton_, &about_, &problem_, &status_}) addAndMakeVisible(c);
    status_.setColour(juce::Label::textColourId, kText);
    status_.setFont(juce::FontOptions(13.0f));
    status_.setJustificationType(juce::Justification::topLeft);
    // in the firmware's own terms: Felucca's music and user presets with their FM6 patches, SLOOP's working project
    auto held = proc_.felucca();
    const juce::String name = held ? juce::String(felucca::dialectOf(*held).name) : juce::String("Felucca");
    const bool fm6 = !held || felucca::dialectOf(*held).fm6;
    const juce::String all = fm6 ? "the music, its four projects and the user presets (with their FM6 patches)" : "the working project, projects A-D and user presets";
    about_.setText("An FM-1 running " + name + ", connected with Find FM-1 (or the MIDI menus): pull everything from it, "
                   "send everything to it, or follow it live.", juce::dontSendNotification);
    about_.setColour(juce::Label::textColourId, kDim);
    about_.setFont(juce::FontOptions(13.0f));
    about_.setJustificationType(juce::Justification::topLeft);
    problem_.setColour(juce::Label::textColourId, juce::Colours::orange);
    problem_.setFont(juce::FontOptions(13.0f));
    problem_.setJustificationType(juce::Justification::topLeft);
    pullButton_.setTooltip("Everything from the connected FM-1 running " + name + " into this instance: " + all
                           + " (its backup is also kept in the library, " + name + "/Backups)");
    pullButton_.onClick = [this] { proc_.feluccaPull(); update(); };
    sendButton_.setTooltip("This instance's " + name + " to the connected FM-1: " + all + ", "
                           "replacing the synth's (not its settings or samples). Its own are backed up to the library first.");
    sendButton_.onClick = [this, name, all] {
        juce::Component::SafePointer<FeluccaSyncPage> self(this);
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon, "Send to the FM-1?",
            "The FM-1's " + all.fromFirstOccurrenceOf("the ", false, false) + " will be replaced by this instance's. "
            "They are backed up to the library (" + name + "/Backups) first. Its settings and user samples are not touched.",
            "Send", "Cancel", this, juce::ModalCallbackFunction::create([self](int ok) {
                if (ok && self) { self->proc_.feluccaSend(); self->update(); }
            }));
    };
    liveButton_.setClickingTogglesState(true);
    liveButton_.setTooltip("Live: what changes on the FM-1 changes here and the other way round, as " + name + "'s web editor follows it. "
                           "Pull or send first so both start the same.");
    liveButton_.onClick = [this] { proc_.feluccaLive(liveButton_.getToggleState()); update(); };
    proc_.onFeluccaLive = [this] { update(); };
    update();
    startTimerHz(4);
}

void FeluccaSyncPage::update() {
    const bool synth = proc_.feluccaSynth(), live = proc_.feluccaLiveOn(), busy = proc_.session.busy();
    pullButton_.setEnabled(synth && !busy);
    sendButton_.setEnabled(synth && !busy);
    liveButton_.setEnabled(synth && (live || !busy));
    liveButton_.setToggleState(live, juce::dontSendNotification);
    const auto problem = proc_.feluccaSynthProblem();   // a release too old: say why the buttons are off
    if (problem != problem_.getText()) problem_.setText(problem, juce::dontSendNotification);
}

void FeluccaSyncPage::resized() {
    auto r = getLocalBounds().reduced(12).withTrimmedRight(std::max(0, getWidth() - 640));
    about_.setBounds(r.removeFromTop(40));
    r.removeFromTop(8);
    auto row = r.removeFromTop(30);
    const int w = (row.getWidth() - 12) / 3;
    pullButton_.setBounds(row.removeFromLeft(w)); row.removeFromLeft(6);
    sendButton_.setBounds(row.removeFromLeft(w)); row.removeFromLeft(6);
    liveButton_.setBounds(row);
    r.removeFromTop(10);
    status_.setBounds(r.removeFromTop(60));
    problem_.setBounds(r.removeFromTop(60));
}

// ---- the library: user presets and projects ------------------------------------------------------

FeluccaLibraryList::FeluccaLibraryList(FM1Processor& p) : proc_(p) {
    for (auto* c : std::initializer_list<juce::Component*>{&list_, &nameLabel_, &name_, &loadButton_, &saveButton_, &renameButton_,
                                                           &eraseButton_, &status_}) addAndMakeVisible(c);
    list_.setRowHeight(22);
    list_.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff1e1e24));
    nameLabel_.setColour(juce::Label::textColourId, kDim);
    name_.setInputRestrictions(12, " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~");
    name_.setTextToShowWhenEmpty("(automatic)", kDim);
    name_.setTooltip("The user preset's name: up to 12 characters, upper case (as on the device). Empty: Felucca names it.");
    loadButton_.setTooltip("Load the selected user preset into the selected part (its sound; its pattern is offered by SEQ > PATTERNS "
                           "on the device), or the selected project into all four parts");
    saveButton_.setTooltip("Save the selected part's sound as the selected user preset, or the music (all four parts) as the selected "
                           "project. Felucca stops its sequencer to save, as on the device.");
    renameButton_.setTooltip("Give the selected user preset the name above (its sound is kept)");
    eraseButton_.setTooltip("Empty the selected user preset slot");
    loadButton_.onClick = [this] { load(list_.getSelectedRow()); };
    saveButton_.onClick = [this] { save(list_.getSelectedRow()); };
    renameButton_.onClick = [this] { rename(list_.getSelectedRow()); };
    eraseButton_.onClick = [this] { erase(list_.getSelectedRow()); };
    status_.setColour(juce::Label::textColourId, kDim);
    status_.setFont(juce::FontOptions(12.0f));
    status_.setJustificationType(juce::Justification::topLeft);
    reload();
    list_.selectRow(0);
    startTimer(1000);   // what the device's own SAVE pages or another instance saved
}

std::optional<std::vector<uint8_t>> FeluccaLibraryList::ask(int cmd, const std::vector<uint8_t>& args) {
    auto f = engine();
    if (!f) return std::nullopt;
    auto reply = f->ask(felucca::frame(cmd, args));
    if (!reply) return std::nullopt;
    return felucca::argsOf(*reply);
}

void FeluccaLibraryList::reload() {
    readUsers(0);
    readUsers(16);
    readProjects();
    list_.updateContent();
    list_.repaint();
    updateButtons();
}

void FeluccaLibraryList::readUsers(int start) {
    {   // UP_LIST: start, count -> start, count, total, (used, engine, name 0) each
        auto a = ask(felucca::kUpList, {uint8_t(start), 16});
        if (!a || a->size() < 3) return;
        size_t k = 3;
        for (int i = 0; i < (*a)[1] && start + i < kUser; ++i) {
            if (k + 2 > a->size()) break;
            auto& r = rows_[start + i];
            r.used = (*a)[k] != 0;
            r.engine = (*a)[k + 1];
            k += 2;
            juce::String n;
            while (k < a->size() && (*a)[k] != 0) n += juce::String::charToString(juce::juce_wchar((*a)[k++]));
            ++k;   // (its 0)
            r.name = n;
        }
    }
}

void FeluccaLibraryList::readProjects() {
    if (auto f = engine())
        for (int i = 0; i < kProjects; ++i) {   // the project slots: their names are their last 12 bytes before the hash
            auto& r = rows_[kUser + i];
            std::vector<uint8_t> b;
            r.used = f->object(2 + i, b) && b.size() > 16;
            juce::String n;
            for (size_t k = b.size() - std::min<size_t>(b.size(), 16); r.used && k < b.size() - 4 && b[k] != 0; ++k)
                n += juce::String::charToString(juce::juce_wchar(b[k] >= 32 && b[k] <= 126 ? b[k] : '?'));
            r.name = n;
        }
}

// one read a second, in turn (each request runs Felucca's main loop, which advances its clock
// when no audio has played since the last: two at once would run it ahead)
void FeluccaLibraryList::timerCallback() {
    if (!isShowing() || isMouseButtonDownAnywhere()) return;
    const int step = tick_++ % 3;
    if (step < 2) readUsers(16 * step); else readProjects();
    list_.updateContent();
    list_.repaint();
    updateButtons();
}

juce::String FeluccaLibraryList::partText() const {
    auto f = engine();
    return "PART " + juce::String(f ? f->selected() + 1 : 1);
}

void FeluccaLibraryList::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) {
    if (selected) g.fillAll(juce::Colour(0xff3a4a6a));
    if (row == kUser) {   // the projects start here
        g.setColour(kDim.withAlpha(0.5f));
        g.drawHorizontalLine(0, 0.0f, float(w));
    }
    const auto& r = rows_[row];
    const bool user = row < kUser;
    const juce::String number = user ? "U" + juce::String(row + 1).paddedLeft('0', 2) : "PROJ " + juce::String::charToString(juce::juce_wchar('A' + row - kUser));
    g.setFont(juce::FontOptions(14.0f));
    g.setColour(kDim);
    g.drawText(number, 8, 0, user ? 36 : 56, h, juce::Justification::centredLeft);
    g.setColour(r.used ? kText : kDim.withAlpha(0.6f));
    const juce::String name = !r.used ? "(empty)" : r.name.isNotEmpty() ? r.name : (user ? "(no name)" : "PROJECT " + juce::String::charToString(juce::juce_wchar('A' + row - kUser)));
    g.drawText(name, user ? 48 : 68, 0, w - (user ? 48 : 68) - 70, h, juce::Justification::centredLeft);
    if (user && r.used)
        if (auto f = engine()) {
            g.setColour(kAccent);
            g.setFont(juce::FontOptions(12.0f));
            g.drawText(f->engineName(r.engine), w - 74, 0, 66, h, juce::Justification::centredRight);
        }
}

void FeluccaLibraryList::selectedRowsChanged(int row) {
    if (row >= 0 && row < kUser + kProjects) name_.setText(rows_[row].used && row < kUser ? rows_[row].name : juce::String(), false);
    updateButtons();
}

// a click selects (to save over, rename or erase it without touching the part's sound);
// Load or a double click loads
void FeluccaLibraryList::listBoxItemDoubleClicked(int row, const juce::MouseEvent&) { load(row); }

void FeluccaLibraryList::updateButtons() {
    const int row = list_.getSelectedRow();
    const bool ok = row >= 0 && row < kUser + kProjects, user = ok && row < kUser, used = ok && rows_[row].used;
    loadButton_.setEnabled(used);
    saveButton_.setEnabled(ok);
    renameButton_.setEnabled(user && used);
    eraseButton_.setEnabled(user && used);
    name_.setEnabled(user);
    saveButton_.setButtonText(user ? "Save " + partText() : "Save");
    loadButton_.setButtonText(user ? "Load to " + partText() : "Load");
}

void FeluccaLibraryList::say(const juce::String& text, bool problem) {
    status_.setColour(juce::Label::textColourId, problem ? juce::Colours::orange : kDim);
    status_.setText(text, juce::dontSendNotification);
}

juce::String FeluccaLibraryList::cleanName(const juce::String& s) {
    juce::String out;
    for (auto c : s.toUpperCase().trim())
        if (c >= 32 && c <= 126 && out.length() < 12) out += juce::String::charToString(c);
    return out;
}

void FeluccaLibraryList::load(int row) {
    if (row < 0 || row >= kUser + kProjects || !rows_[row].used) return;
    auto f = engine();
    if (!f) return;
    if (row < kUser) {
        const int part = f->selected();
        auto a = ask(felucca::kUpLoad, {uint8_t(row)});   // -> slot, rc (0 loaded)
        if (!a || a->size() < 2 || (*a)[1] != 0) { say("U" + juce::String(row + 1).paddedLeft('0', 2) + " did not load.", true); return; }
        proc_.feluccaChanged(part);   // the host's parameters follow
        say(rows_[row].name + " loaded into PART " + juce::String(part + 1) + ".");
        if (onLoaded) onLoaded();
        return;
    }
    const int slot = row - kUser;
    juce::Component::SafePointer<FeluccaLibraryList> self(this);
    juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, "Load the project?",
        "The music now (all four parts, their patterns and the song) is replaced by project " + juce::String::charToString(juce::juce_wchar('A' + slot)) + ".",
        "Load", "Cancel", this, juce::ModalCallbackFunction::create([self, slot](int ok) {
            if (!ok || !self) return;
            auto a = self->ask(felucca::kProject, {0, uint8_t(slot)});   // -> 0, slot, used
            if (!a || a->size() < 3 || (*a)[2] == 0) { self->say("The project did not load.", true); return; }
            self->proc_.feluccaChanged(-1);
            self->say("Project " + juce::String::charToString(juce::juce_wchar('A' + slot)) + " loaded.");
            if (self->onLoaded) self->onLoaded();
        }));
}

void FeluccaLibraryList::save(int row) {
    if (row < 0 || row >= kUser + kProjects) return;
    const bool user = row < kUser;
    const juce::String name = cleanName(name_.getText());
    const juce::String what = user ? "U" + juce::String(row + 1).paddedLeft('0', 2) : "project " + juce::String::charToString(juce::juce_wchar('A' + row - kUser));
    auto doSave = [this, row, user, name, what] {
        std::optional<std::vector<uint8_t>> a;
        if (user) {
            std::vector<uint8_t> args{uint8_t(row)};
            for (auto c : name) args.push_back(uint8_t(c));
            args.push_back(0);   // (empty: Felucca's automatic name)
            a = ask(felucca::kUpStore, args);   // -> slot, rc (0 saved; 2 storage, or the sequencer would not stop)
        } else {
            a = ask(felucca::kProject, {1, uint8_t(row - kUser)});   // -> 1, slot, used; no reply: not saved
        }
        const bool ok = user ? (a && a->size() >= 2 && (*a)[1] == 0) : (a && a->size() >= 3);
        reload();
        if (ok) say((user ? partText() + "'s sound" : juce::String("The music")) + " saved as " + what + ".");
        else say(what + " was not saved (Felucca could not stop its sequencer, or could not write).", true);
    };
    if (!rows_[row].used) { doSave(); return; }
    juce::Component::SafePointer<FeluccaLibraryList> self(this);
    juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, "Save over " + what + "?",
        (rows_[row].name.isNotEmpty() ? rows_[row].name : what) + " is replaced.",
        "Save", "Cancel", this, juce::ModalCallbackFunction::create([self, doSave](int ok) { if (ok && self) doSave(); }));
}

void FeluccaLibraryList::rename(int row) {
    if (row < 0 || row >= kUser || !rows_[row].used) return;
    const juce::String name = cleanName(name_.getText());
    if (name.isEmpty()) { say("Type a name first.", true); return; }
    auto f = engine();
    auto a = ask(felucca::kUpGet, {uint8_t(row)});   // -> slot, used, engine, name 0, values, pattern, kind [, 16 hi]
    if (!f || !a || a->size() < 4 || (*a)[1] == 0) { say("The user preset could not be read.", true); return; }
    size_t k = 3;
    while (k < a->size() && (*a)[k] != 0) ++k;
    std::vector<uint8_t> rest(a->begin() + std::ptrdiff_t(std::min(a->size(), k + 1)), a->end());
    // PUT takes the same values and pattern, with the kind only for a drum grid (kind 1 and its 16 bytes)
    const size_t plain = size_t(2 * f->paramCount() + 32);
    if (rest.size() == plain + 1) rest.pop_back();
    std::vector<uint8_t> put{uint8_t(row), (*a)[2]};
    for (auto c : name) put.push_back(uint8_t(c));
    put.push_back(0);
    put.insert(put.end(), rest.begin(), rest.end());
    auto r = ask(felucca::kUpPut, put);   // -> slot, rc
    reload();
    if (r && r->size() >= 2 && (*r)[1] == 0) say("Renamed " + name + ".");
    else say("It was not renamed (Felucca could not stop its sequencer, or could not write).", true);
}

void FeluccaLibraryList::erase(int row) {
    if (row < 0 || row >= kUser || !rows_[row].used) return;
    juce::Component::SafePointer<FeluccaLibraryList> self(this);
    juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon, "Erase " + rows_[row].name + "?",
        "The user preset slot U" + juce::String(row + 1).paddedLeft('0', 2) + " is emptied.",
        "Erase", "Cancel", this, juce::ModalCallbackFunction::create([self, row](int ok) {
            if (!ok || !self) return;
            auto a = self->ask(felucca::kUpErase, {uint8_t(row)});   // -> slot, rc
            self->reload();
            if (a && a->size() >= 2 && (*a)[1] == 0) self->say("Erased.");
            else self->say("It was not erased (Felucca could not stop its sequencer, or could not write).", true);
        }));
}

void FeluccaLibraryList::paint(juce::Graphics& g) { g.fillAll(kBg); }

void FeluccaLibraryList::resized() {
    auto r = getLocalBounds();
    status_.setBounds(r.removeFromBottom(34));
    auto buttons2 = r.removeFromBottom(28);
    r.removeFromBottom(4);
    auto buttons1 = r.removeFromBottom(28);
    r.removeFromBottom(4);
    auto nameRow = r.removeFromBottom(28);
    r.removeFromBottom(6);
    list_.setBounds(r);
    nameLabel_.setBounds(nameRow.removeFromLeft(48));
    name_.setBounds(nameRow);
    const int w = (buttons1.getWidth() - 4) / 2;
    loadButton_.setBounds(buttons1.removeFromLeft(w)); buttons1.removeFromLeft(4); saveButton_.setBounds(buttons1);
    renameButton_.setBounds(buttons2.removeFromLeft(w)); buttons2.removeFromLeft(4); eraseButton_.setBounds(buttons2);
}

// ---- the editor: Library and Device tabs ---------------------------------------------------------

FeluccaPanel::FeluccaPanel(FM1Processor& p) : proc_(p), sound_(p), sync_(p), list_(p), device_(p), seq_(p) {
    const auto bg = kBg;
    pages_.addTab("Sound", bg, &sound_, false);
    pages_.addTab("Sync", bg, &sync_, false);
    library_.addAndMakeVisible(list_);
    library_.addAndMakeVisible(pages_);
    for (auto* b : {&showList_, &showPages_}) {
        library_.addChildComponent(b);
        b->setClickingTogglesState(true);
        b->setRadioGroupId(4803);
    }
    showList_.setToggleState(true, juce::dontSendNotification);
    showList_.onClick = [this] { if (showList_.getToggleState()) { showingPages_ = false; layoutLibrary(); } };
    showPages_.onClick = [this] { if (showPages_.getToggleState()) { showingPages_ = true; layoutLibrary(); } };
    library_.layout = [this] { layoutLibrary(); };
    list_.onLoaded = [this] { sound_.refresh(); };
    sound_.onPartChanged = [this] { list_.updateButtons(); };
    tabs_.addTab("Library", bg, &library_, false);
    tabs_.addTab("Device", bg, &device_, false);
    tabs_.addTab("Sequencer", bg, &seq_, false);
    tabs_.setComponentID("felucca tabs");
    addAndMakeVisible(tabs_);
}

void FeluccaPanel::resized() { tabs_.setBounds(getLocalBounds()); }

void FeluccaPanel::layoutLibrary() {
    auto r = library_.getLocalBounds().reduced(6);
    const bool narrow = library_.getWidth() < kNarrow;   // a phone: the list or the pages, at full width
    showList_.setVisible(narrow);
    showPages_.setVisible(narrow);
    if (narrow) {
        auto head = r.removeFromTop(30);
        showList_.setBounds(head.removeFromLeft(90));
        head.removeFromLeft(4);
        showPages_.setBounds(head.removeFromLeft(90));
        r.removeFromTop(4);
        list_.setVisible(!showingPages_);
        pages_.setVisible(showingPages_);
        list_.setBounds(r);
        pages_.setBounds(r);
        return;
    }
    list_.setVisible(true);
    pages_.setVisible(true);
    list_.setBounds(r.removeFromLeft(270));
    r.removeFromLeft(6);
    pages_.setBounds(r);
}

#endif
