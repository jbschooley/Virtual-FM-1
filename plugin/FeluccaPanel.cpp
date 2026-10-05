#include "FeluccaPanel.h"

#if FM1_FELUCCA

namespace {

// Felucca 0.9-beta's per-track parameters (core.h P_*), in its own order, grouped as
// its pages group them. Ids beyond what a version has are skipped.
struct GroupDef { const char* title; int first, last; bool parts, drums; };
const GroupDef kTrackGroups[] = {
    {"Engine", -1, -1, true, false},   // the engine's eight: from firstEngineParam()
    {"Envelope", 1, 4, true, false},
    {"Envelope to", 5, 7, true, false},
    {"LFO", 9, 12, true, false},
    {"LFO to", 13, 16, true, false},
    {"Voice", 37, 44, true, false},
    {"Arpeggiator", 17, 24, true, false},
    {"Scale", 25, 28, true, false},
    {"Sequencer", 29, 32, true, true},
    {"Sends", 33, 36, true, true},
    {"Slicer", 45, 48, true, true},
    {"Mix", 0, 0, true, true},
};
// the global settings worth editing here (the rest are the device's own pages and actions)
const int kGlobals[] = {0, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 24, 25, 26};

const juce::Colour kBg(0xff26262e), kBox(0xff30303a), kText(0xffe8e8ee), kDim(0xffa0a0b0), kAccent(0xff6fb7c9);

}  // namespace

FeluccaPanel::FeluccaPanel(FM1Processor& p) : proc_(p) {
    const char* names[4] = {"PART 1", "PART 2", "PART 3", "DRUMS"};
    for (int i = 0; i < 4; ++i) {
        auto& b = trackButtons_[i];
        b.setButtonText(names[i]);
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
        build();
    };
    presetBox_.setTooltip("Felucca's built-in presets for this engine");
    presetBox_.onChange = [this] {
        auto e = engine();
        if (loading_ || !e) return;
        e->applyPreset(track_, presetBox_.getSelectedId() - 1);
        loadValues();
    };
    hostTempo_.setTooltip("On: Felucca's tempo is the host's. Off: its own BPM (Global > BPM). Saved with the project.");
    hostTempo_.onClick = [this] {
        auto s = proc_.settings();
        s.hostTempo = hostTempo_.getToggleState();
        proc_.setSettings(s);
        updateTempoControl();
    };
    info_.setColour(juce::Label::textColourId, kDim);
    info_.setFont(juce::FontOptions(12.0f));
    info_.setText("MIDI channels 1-3 play the parts, channel 10 the drums, as on the FM-1 with Felucca.", juce::dontSendNotification);
    view_.setViewedComponent(&content_, false);
    view_.setScrollBarsShown(true, false);
    trackButtons_[0].setToggleState(true, juce::dontSendNotification);
    refresh();
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
    const bool drums = track_ >= f->parts();
    engineBox_.setEnabled(!drums);
    presetBox_.setEnabled(!drums);
    if (!drums) {
        for (int e = 0; e < f->engines(); ++e) engineBox_.addItem(f->engineName(e), e + 1);
        engineBox_.setSelectedId(f->engineOf(track_) + 1, juce::dontSendNotification);
        auto presets = f->presetNames(f->engineOf(track_));
        for (size_t i = 0; i < presets.size(); ++i) presetBox_.addItem(presets[i], int(i) + 1);
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
                if (global) e->setGlobal(id, v); else e->setParam(track_, id, v);
            };
            content_.addAndMakeVisible(*c.box);
        } else {
            c.slider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
            c.slider->setRange(d.min, d.max, 1.0);
            c.slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 18);
            c.slider->setDoubleClickReturnValue(true, d.def);
            auto* sl = c.slider.get();
            sl->onValueChange = [this, sl, id, global] {
                auto e = engine();
                if (loading_ || !e) return;
                const int v = int(std::lround(sl->getValue()));
                if (global) e->setGlobal(id, v); else e->setParam(track_, id, v);
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
        if ((drums && !def.drums) || (!drums && !def.parts)) continue;
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
    if (track_ < f->parts()) presetBox_.setSelectedId(f->presetOf(track_) + 1, juce::dontSendNotification);
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
