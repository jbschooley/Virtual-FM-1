#include "Fm1OpenPanel.h"

#include "PluginProcessor.h"

#if FM1_FELUCCA   // (FM1 Open is built with the Felucca family's cores: Clang only)

namespace {
const juce::Colour kBg(0xff26262e), kText(0xffe8e8ee), kDim(0xffa0a0b0), kAccent(0xff6fb7c9);   // (FeluccaPanel's)

void head(juce::Label& l, const juce::String& text) {
    l.setText(text, juce::dontSendNotification);
    l.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    l.setColour(juce::Label::textColourId, kAccent);
}
}  // namespace

Fm1OpenSyncPage::Fm1OpenSyncPage(FM1Processor& p) : proc_(p) {
    about_.setText("An FM-1 running FM1 Open, connected above: its project, a track's sound, its user slots and its sample bank, "
                   "either way. What a Send replaces on the FM-1 is saved in the library first (FM1 Open/Backups). "
                   "No Live: FM1 Open has no message for one value changed, and a sound sent whole would cut what plays.",
                   juce::dontSendNotification);
    about_.setColour(juce::Label::textColourId, kDim);
    about_.setFont(juce::FontOptions(13.0f));
    about_.setJustificationType(juce::Justification::topLeft);
    head(projectHead_, "The project it plays");
    head(soundHead_, "A track's sound");
    head(slotsHead_, "Its project slots");
    head(samplesHead_, "Its sample bank (every project there uses it)");
    for (int s = 1; s <= 16; ++s) saveSlot_.addItem("Slot " + juce::String(s), s);
    saveSlot_.setSelectedId(1, juce::dontSendNotification);
    for (int t = 1; t <= 8; ++t) track_.addItem("Track " + juce::String(t), t);
    track_.setSelectedId(1, juce::dontSendNotification);
    for (int s = 1; s <= 16; ++s) slot_.addItem("Slot " + juce::String(s), s);
    slot_.setSelectedId(1, juce::dontSendNotification);
    for (auto* c : std::initializer_list<juce::Component*>{&about_, &projectHead_, &soundHead_, &slotsHead_, &samplesHead_, &pull_, &send_,
                                                           &sendSave_, &saveSlot_, &track_, &soundFrom_, &soundTo_, &listSlots_, &loadSlot_,
                                                           &slot_, &samplesFrom_, &samplesTo_})
        addAndMakeVisible(c);
    pull_.setTooltip("The project the FM-1 plays, into this instance (its project is replaced)");
    send_.setTooltip("This instance's project to the FM-1: it plays it (not kept in a slot)");
    sendSave_.setTooltip("This instance's project to the FM-1, kept in the slot chosen beside (what that slot held is replaced)");
    pull_.onClick = [this] { proc_.fm1openPull(); update(); };
    send_.onClick = [this] { proc_.fm1openSend(); update(); };
    sendSave_.onClick = [this] { proc_.fm1openSend(saveSlot_.getSelectedId() - 1); update(); };
    soundFrom_.onClick = [this] { proc_.fm1openPullSound(track_.getSelectedId() - 1); update(); };
    soundTo_.onClick = [this] { proc_.fm1openSendSound(track_.getSelectedId() - 1); update(); };
    listSlots_.onClick = [this] { proc_.fm1openListSlots(); update(); };
    loadSlot_.onClick = [this] { proc_.fm1openPullSlot(slot_.getSelectedId() - 1); update(); };
    samplesFrom_.onClick = [this] { proc_.fm1openPullSamples(); update(); };
    samplesTo_.onClick = [this] {
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon, "Send the sample bank?",
            "The FM-1's whole sample bank (used by every project on it) is replaced by this instance's. Its bank is saved in the library first. "
            "This takes a few minutes.",
            "Send", "Cancel", this, juce::ModalCallbackFunction::create([this](int r) { if (r == 1) proc_.fm1openSendSamples(); }));
    };
    proc_.onFm1OpenSlots = [this] { showSlots(); };
    showSlots();
    update();
}

Fm1OpenSyncPage::~Fm1OpenSyncPage() { proc_.onFm1OpenSlots = nullptr; }

// the slot lists with the names last read from the FM-1 ("" an empty slot)
void Fm1OpenSyncPage::showSlots() {
    const auto names = proc_.fm1openSlotNames();
    for (auto* box : {&slot_, &saveSlot_})
        for (int s = 0; s < 16; ++s) {
            juce::String text = "Slot " + juce::String(s + 1);
            if (size_t(s) < names.size()) text << (names[size_t(s)].empty() ? juce::String(" (empty)") : ": " + juce::String(names[size_t(s)]));
            box->changeItemText(s + 1, text);
        }
}

void Fm1OpenSyncPage::update() {
    const bool on = proc_.fm1openSynth() && !proc_.session.busy();
    for (auto* b : {&pull_, &send_, &sendSave_, &soundFrom_, &soundTo_, &listSlots_, &loadSlot_, &samplesFrom_, &samplesTo_}) b->setEnabled(on);
}

void Fm1OpenSyncPage::visibilityChanged() {
    if (isVisible()) { update(); startTimerHz(4); }
    else stopTimer();
}

void Fm1OpenSyncPage::paint(juce::Graphics& g) { g.fillAll(kBg); }

void Fm1OpenSyncPage::resized() {
    auto r = getLocalBounds().reduced(12).withTrimmedRight(std::max(0, getWidth() - 680));
    about_.setBounds(r.removeFromTop(64));
    auto row = [&](juce::Label& h, std::initializer_list<juce::Component*> cs) {
        r.removeFromTop(8);
        h.setBounds(r.removeFromTop(20));
        auto line = r.removeFromTop(30);
        const int w = (line.getWidth() - 6 * (int(cs.size()) - 1)) / int(cs.size());
        for (auto* c : cs) { c->setBounds(line.removeFromLeft(w)); line.removeFromLeft(6); }
    };
    row(projectHead_, {&pull_, &send_, &sendSave_, &saveSlot_});
    row(soundHead_, {&track_, &soundFrom_, &soundTo_});
    row(slotsHead_, {&listSlots_, &slot_, &loadSlot_});
    row(samplesHead_, {&samplesFrom_, &samplesTo_});
}

Fm1OpenPanel::Fm1OpenPanel(FM1Processor& p) {
    tabs_.addTab("Device", kBg, new FeluccaDeviceView(p), true);
    tabs_.addTab("FM-1", kBg, new Fm1OpenSyncPage(p), true);
    tabs_.setComponentID("fm1open tabs");
    addAndMakeVisible(tabs_);
}

void Fm1OpenPanel::resized() { tabs_.setBounds(getLocalBounds()); }
#endif
