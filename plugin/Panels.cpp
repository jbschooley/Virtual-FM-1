#include "Panels.h"
#include "ChosenFile.h"

#include "Fm1Record.h"

namespace {
const juce::Colour kAccent(0xffe0a040), kDim(0xff3a3a44), kNoteCol(0xff4a8ad0), kPlaying(0xfff4f4f4), kSelected(0xffffffff);

juce::Label* makeLabel(std::vector<std::unique_ptr<juce::Label>>& store, juce::Component& parent, const juce::String& text, float size = 13.0f, bool bold = false) {
    auto l = std::make_unique<juce::Label>(juce::String(), text);
    l->setFont(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain));
    l->setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.85f));
    parent.addAndMakeVisible(*l);
    store.push_back(std::move(l));
    return store.back().get();
}
void setupLinear(juce::Slider& s, double lo, double hi, double step = 1.0) {
    s.setSliderStyle(juce::Slider::LinearHorizontal);
    s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 18);
    s.setRange(lo, hi, step);
}
}  // namespace

// ---- ParamGrid ---------------------------------------------------------------------

ParamGrid::ParamGrid(juce::AudioProcessorValueTreeState& apvts, const juce::StringArray& ids, const juce::StringArray& labels, int columns, int cellW, int cellH)
    : columns_(columns), cellW_(cellW), cellH_(cellH) {
    for (int i = 0; i < ids.size(); ++i) {
        Cell c;
        c.label = std::make_unique<juce::Label>(juce::String(), labels[i]);
        c.label->setFont(juce::FontOptions(11.0f));
        c.label->setJustificationType(juce::Justification::centred);
        c.label->setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.75f));
        addAndMakeVisible(*c.label);
        auto* p = apvts.getParameter(ids[i]);
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(p)) {
            c.combo = std::make_unique<juce::ComboBox>();
            c.combo->setScrollWheelEnabled(false);
            for (int k = 0; k < choice->choices.size(); ++k) c.combo->addItem(choice->choices[k], k + 1);
            addAndMakeVisible(*c.combo);
            c.ca = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(apvts, ids[i], *c.combo);
        } else {
            c.slider = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
            c.slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 44, 16);
            c.slider->setColour(juce::Slider::rotarySliderFillColourId, kAccent);
            c.slider->setScrollWheelEnabled(false);   // let the wheel scroll the page, not the knob
            addAndMakeVisible(*c.slider);
            c.sa = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, ids[i], *c.slider);
        }
        cells_.push_back(std::move(c));
    }
    setSize(columns_ * cellW_, preferredHeight());
}

int ParamGrid::preferredHeight() const { return ((int(cells_.size()) + columns_ - 1) / columns_) * cellH_; }

void ParamGrid::resized() {
    for (size_t i = 0; i < cells_.size(); ++i) {
        int col = int(i) % columns_, row = int(i) / columns_;
        juce::Rectangle<int> r(col * cellW_, row * cellH_, cellW_, cellH_);
        cells_[i].label->setBounds(r.removeFromTop(14));
        if (cells_[i].slider) cells_[i].slider->setBounds(r.reduced(2));
        else cells_[i].combo->setBounds(r.reduced(3, 18));
    }
}

// ---- file helpers -------------------------------------------------------------------

namespace {

juce::File documents() { return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory); }

juce::String safeFileName(const juce::String& name) {
    auto n = juce::File::createLegalFileName(name.trim());
    return n.isEmpty() ? "preset" : n;
}

void saveAs(std::unique_ptr<juce::FileChooser>& chooser, const juce::String& title, const juce::String& fileName,
            std::function<bool(const juce::File&)> write, std::function<void(const juce::String&)> status) {
    chooser = std::make_unique<juce::FileChooser>(title, documents().getChildFile(fileName), "*" + juce::File(fileName).getFileExtension());
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
        [write, status](const juce::FileChooser& fc) {
            juce::String name;
            const bool ok = fm1ui::saveChosen(fc, {}, write, name);
            if (name.isNotEmpty()) status(ok ? "Saved " + name : "Could not write " + name);
        });
}

}  // namespace

static void showImportResult(juce::Component* near, const FM1Processor::ImportResult& r, std::function<void(const juce::String&)> status) {
    status(r.summary);
    if (r.errors.isEmpty()) return;
    juce::StringArray shown;
    for (int i = 0; i < std::min(20, r.errors.size()); ++i) shown.add(r.errors[i]);
    if (r.errors.size() > 20) shown.add("... and " + juce::String(r.errors.size() - 20) + " more");
    auto opts = juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::WarningIcon)
        .withTitle("Nothing was imported").withMessage(shown.joinIntoString("\n")).withButton("OK").withAssociatedComponent(near);
    juce::AlertWindow::showAsync(opts, nullptr);
}

// ---- LibraryPanel -------------------------------------------------------------------

LibraryPanel::LibraryPanel(FM1Processor& p) : proc_(p) {
    for (auto* c : std::initializer_list<juce::Component*>{&inPorts_, &outPorts_, &connect_, &autoConnect_, &live_, &identity_})
        bar_.addAndMakeVisible(c);
    inPorts_.setTextWhenNothingSelected("MIDI in");
    outPorts_.setTextWhenNothingSelected("MIDI out");
    inPorts_.setTextWhenNoChoicesAvailable("No MIDI in");
    outPorts_.setTextWhenNoChoicesAvailable("No MIDI out");
    for (auto* c : std::initializer_list<juce::Component*>{&currentName_, &init_, &pullCurrent_, &pushCurrent_, &pullAll_, &pushChanged_,
            &pushAll_, &selectOnDevice_, &cancel_, &importFile_, &exportFile_, &sendEdit_, &fxChannel_})
        syncPage_.addAndMakeVisible(c);
    for (auto* c : std::initializer_list<juce::Component*>{&list_, &pages_, &status_})
        addAndMakeVisible(c);
    bar_.layout = [this] { layoutBar(); };
    syncPage_.layout = [this] { layoutSync(); };
    pages_.setOutline(0);
    vaPage_.setJustificationType(juce::Justification::centred);
    vaPage_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.75f));
    vaPage_.setText("The VA engine is baud girl's, and its source is not published yet.\n"
                    "Until it is, VA presets play through the FM engine here; their VA settings\n"
                    "are kept exactly as stored, and sync to the FM-1 unchanged.", juce::dontSendNotification);
    chipPage_.setJustificationType(juce::Justification::centred);
    chipPage_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.75f));
    chipPage_.setText("The 8-Bit engine (FM-1_096) is baud girl's, and its source is not published yet.\n"
                      "Until it is, 8-Bit presets are silent here; their settings are kept exactly\n"
                      "as stored, and sync to the FM-1 unchanged.", juce::dontSendNotification);
    init_.setTooltip("Start the current preset over from a blank sound (an unsaved edit until you store it)");
    init_.onClick = [this] { showInitMenu(); };
    refreshPorts();
    connect_.onClick = [this] {
        auto ins = Fm1Link::inputs(), outs = Fm1Link::outputs();
        int i = inPorts_.getSelectedItemIndex(), o = outPorts_.getSelectedItemIndex();
        if (i < 0 || o < 0 || i >= ins.size() || o >= outs.size()) { setStatus("Choose an input and an output port."); return; }
        proc_.connect(ins[i].identifier, outs[o].identifier);
    };
    connect_.setTooltip("Connect: open the MIDI in and out chosen beside it");
    autoConnect_.setTooltip("Find FM-1: look for an FM-1 (USB first, then Bluetooth) and connect to it");
    autoConnect_.onClick = [this] { refreshPorts(); if (proc_.autoConnect()) refreshPorts(); };
    pullCurrent_.onClick = [this] { proc_.pullCurrent(); };
    pushCurrent_.onClick = [this] { proc_.pushCurrent(); };
    sendEdit_.onClick = [this] { proc_.sendToFm1EditBuffer(); };
    // Live, in the top bar: what it carries depends on the firmware (refreshLive)
    live_.setClickingTogglesState(true);
    live_.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff2e9d55));
    live_.onClick = [this] {
       #if FM1_FELUCCA
        if (proc_.felucca()) { proc_.feluccaLive(live_.getToggleState()); refreshLive(); return; }
       #endif
        proc_.setLive(live_.getToggleState());
    };
    for (int ch = 1; ch <= 16; ++ch) fxChannel_.addItem("FX channel " + juce::String(ch), ch);
    fxChannel_.setSelectedId(proc_.channels.fx, juce::dontSendNotification);
    fxChannel_.onChange = [this] { proc_.channels.fx = fxChannel_.getSelectedId(); };
    fxChannel_.setTooltip("Must match the FM-1's GLOBE > MIDI > FX Channel (2 unless changed)");
    pullAll_.onClick = [this] { proc_.pullAll(); };
    pushChanged_.onClick = [this] { proc_.pushChanged(); };
    pushAll_.onClick = [this] {
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon, "Push all 128 presets?",
            "Every preset on the FM-1 will be replaced by the plugin's copy. This takes about 6 minutes.",
            "Push all", "Cancel", this, juce::ModalCallbackFunction::create([this](int r) { if (r == 1) proc_.pushAll(); }));
    };
    selectOnDevice_.onClick = [this] { proc_.selectOnDevice(); };
    cancel_.onClick = [this] { proc_.session.cancel(); };
    importFile_.onClick = [this] {
        chooser_ = std::make_unique<juce::FileChooser>("Import presets and patterns (.json), or an FM-1 backup, DX7 bank or voice (.syx)",
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.json;*.syx;*.SYX");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
            auto f = fm1ui::openedFile(fc);
            if (!f.existsAsFile()) return;
            if (f.hasFileExtension("json")) importJson(f);
            else setStatus(proc_.importSyx(f));
            refresh();
        });
    };
    importFile_.setTooltip("A .json file of presets and/or patterns, or a .syx of FM-1 presets or DX7 voices");
    exportFile_.onClick = [this] { showExportMenu(); };
    exportFile_.setTooltip("Save presets (the selected ones, a bank, all) and patterns. Shift- or Cmd-click the list to select several presets.");
    list_.setRowHeight(20);
    list_.setMultipleSelectionEnabled(true);
    addAndMakeVisible(selectMode_);
    for (auto* b : {&showList_, &showPages_}) {
        addChildComponent(b);
        b->setClickingTogglesState(true);
        b->setRadioGroupId(4802);
    }
    showList_.setToggleState(true, juce::dontSendNotification);
    showList_.onClick = [this] { if (showList_.getToggleState()) { showingPages_ = false; resized(); } };
    showPages_.onClick = [this] { if (showPages_.getToggleState()) { showingPages_ = true; resized(); } };
    selectMode_.setClickingTogglesState(true);
    selectMode_.setTooltip("Select several presets: each tap adds or removes one (for Push, Export...). On a computer, shift-click and cmd-click do it too.");
    selectMode_.onClick = [this] {
        const bool on = selectMode_.getToggleState();
        list_.setClickingTogglesRowSelection(on);
        if (!on) list_.selectRow(proc_.bank.currentSlot());   // back to the one playing
    };
    list_.selectRow(proc_.bank.currentSlot());
    currentName_.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    startTimerHz(10);
    refreshButtons();
}

LibraryPanel::~LibraryPanel() = default;

void LibraryPanel::refresh() {
    list_.updateContent();
    list_.repaint();
    showPagesFor(proc_.bank.currentSlot());
    if (!list_.isRowSelected(proc_.bank.currentSlot()) && list_.getNumSelectedRows() <= 1) list_.selectRow(proc_.bank.currentSlot(), true, true);
    currentName_.setText(proc_.bank.slotLabel(proc_.bank.currentSlot()), juce::dontSendNotification);
}

void LibraryPanel::refreshPorts() {
    inPorts_.clear(juce::dontSendNotification);
    outPorts_.clear(juce::dontSendNotification);
    auto ins = Fm1Link::inputs(), outs = Fm1Link::outputs();
    for (int i = 0; i < ins.size(); ++i) inPorts_.addItem(ins[i].name, i + 1);
    for (int i = 0; i < outs.size(); ++i) outPorts_.addItem(outs[i].name, i + 1);
    auto cur = proc_.link.ports();
    for (int i = 0; i < ins.size(); ++i) if (ins[i].identifier == cur.inputId) inPorts_.setSelectedItemIndex(i, juce::dontSendNotification);
    for (int i = 0; i < outs.size(); ++i) if (outs[i].identifier == cur.outputId) outPorts_.setSelectedItemIndex(i, juce::dontSendNotification);
}

void LibraryPanel::refreshButtons() {
    bool open = proc_.link.isOpen(), busy = proc_.session.busy();
    for (auto* b : {&pullCurrent_, &pushCurrent_, &pullAll_, &pushChanged_, &pushAll_, &selectOnDevice_, &sendEdit_}) b->setEnabled(open && !busy);
    refreshLive();
    cancel_.setEnabled(busy);
    connect_.setEnabled(!busy);
    autoConnect_.setEnabled(!busy);
}

// the top bar's Live: Felucca's and SLOOP's sync (a session job) or the FM-1's (stock, FM-1+VA)
void LibraryPanel::refreshLive() {
    bool on = proc_.isLive(), can = proc_.link.isOpen();
    juce::String tip = "Live: the sound you edit or pick here plays on the FM-1 as you change it (its edit buffer, not stored; "
                       "one way: the FM-1's own changes and the sequencer are not carried)";
   #if FM1_FELUCCA
    if (proc_.felucca()) {
        on = proc_.feluccaLiveOn();
        can = proc_.feluccaSynth() && (on || !proc_.session.busy());
        tip = "Live: what changes on the FM-1 changes here and the other way round, in every tab (sounds, the device, "
              "the sequencer). Pull or send first so both start the same.";
    }
   #endif
    live_.setEnabled(can);
    live_.setToggleState(on, juce::dontSendNotification);
    if (live_.getTooltip() != tip) live_.setTooltip(tip);
}

void LibraryPanel::timerCallback() {
    refreshLive();
    auto label = proc_.bank.slotLabel(proc_.bank.currentSlot()) + (proc_.isEdited() ? "  (edited)" : "");
    if (currentName_.getText() != label) currentName_.setText(label, juce::dontSendNotification);
    bool busy = proc_.session.busy();
    bool open = proc_.link.isOpen();
    if (busy != wasBusy_ || open != wasOpen_) { wasBusy_ = busy; wasOpen_ = open; refreshButtons(); refreshPorts(); }
    if (!busy && !proc_.link.isOpen() && identity_.getText().isNotEmpty()) identity_.setText("", juce::dontSendNotification);
}

void LibraryPanel::resized() {
    auto r = getLocalBounds().reduced(6);
    status_.setBounds(r.removeFromBottom(24));
    r.removeFromBottom(4);
    const bool narrow = getWidth() < 760;   // a phone: the list or the pages, at full width
    showList_.setVisible(narrow);
    showPages_.setVisible(narrow);
    if (narrow) {
        auto head = r.removeFromTop(30);
        showList_.setBounds(head.removeFromLeft(90));
        head.removeFromLeft(4);
        showPages_.setBounds(head.removeFromLeft(90));
        selectMode_.setBounds(head.removeFromRight(90));
        selectMode_.setVisible(!showingPages_);
        r.removeFromTop(4);
        list_.setVisible(!showingPages_);
        pages_.setVisible(showingPages_);
        list_.setBounds(r);
        pages_.setBounds(r);
        return;
    }
    selectMode_.setVisible(true);
    list_.setVisible(true);
    pages_.setVisible(true);
    auto left = r.removeFromLeft(270);
    selectMode_.setBounds(left.removeFromTop(26).removeFromRight(90));
    left.removeFromTop(4);
    list_.setBounds(left);
    r.removeFromLeft(8);
    pages_.setBounds(r);
}

void LibraryPanel::layoutBar() {
    auto top = bar_.getLocalBounds();
    const bool narrow = top.getWidth() < 600;   // a phone: a line of its own, without the identity text
    identity_.setVisible(!narrow);
    if (narrow) {
        live_.setBounds(top.removeFromRight(40)); top.removeFromRight(4);
        autoConnect_.setBounds(top.removeFromRight(40)); top.removeFromRight(4);
        connect_.setBounds(top.removeFromRight(40)); top.removeFromRight(4);
        inPorts_.setBounds(top.removeFromLeft(top.getWidth() / 2 - 2)); top.removeFromLeft(4);
        outPorts_.setBounds(top);
        return;
    }
    inPorts_.setBounds(top.removeFromLeft(200)); top.removeFromLeft(6);
    outPorts_.setBounds(top.removeFromLeft(200)); top.removeFromLeft(6);
    connect_.setBounds(top.removeFromLeft(44)); top.removeFromLeft(4);
    autoConnect_.setBounds(top.removeFromLeft(44)); top.removeFromLeft(4);
    live_.setBounds(top.removeFromLeft(44)); top.removeFromLeft(8);
    identity_.setBounds(top);
}

void LibraryPanel::layoutSync() {
    auto r = syncPage_.getLocalBounds().reduced(12).withTrimmedRight(std::max(0, syncPage_.getWidth() - 640));
    {
        auto head = r.removeFromTop(30);
        init_.setBounds(head.removeFromRight(90));
        currentName_.setBounds(head);
    }
    r.removeFromTop(10);
    auto row = [&](juce::Component& a, juce::Component* b = nullptr) {
        auto rr = r.removeFromTop(30);
        if (b) { a.setBounds(rr.removeFromLeft(rr.getWidth() / 2 - 3)); rr.removeFromLeft(6); b->setBounds(rr); }
        else a.setBounds(rr);
        r.removeFromTop(6);
    };
    row(pullCurrent_, &sendEdit_);
    row(fxChannel_);
    row(pushCurrent_, &selectOnDevice_);
    r.removeFromTop(10);
    row(pullAll_, &pushChanged_);
    row(pushAll_, &cancel_);
    r.removeFromTop(10);
    row(importFile_, &exportFile_);
}

void LibraryPanel::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) {
    if (selected) g.fillAll(juce::Colour(0xff3a4a6a));
    const auto& s = proc_.bank.slot(row);
    g.setColour(juce::Colours::white.withAlpha(selected ? 1.0f : 0.85f));
    g.setFont(juce::FontOptions(14.0f));
    g.drawText(BankModel::bankName(row) + "   " + juce::String(fm1::voiceName(s.sound.voice)).trimEnd(), 8, 0, w - 90, h, juce::Justification::centredLeft);
    const auto engine = fm1::engineOf(s.sound.record);
    g.setColour(engine == fm1::Engine::VA ? juce::Colours::orange : engine == fm1::Engine::EightBit ? juce::Colours::violet : juce::Colours::lightgreen);
    g.drawText(fm1::engineLabel(engine), w - 82, 0, 38, h, juce::Justification::centred);
    juce::String mark = !s.onDevice ? "?" : (s.synced() ? "=" : "*");
    g.setColour(mark == "=" ? juce::Colours::lightgreen : mark == "*" ? juce::Colours::orange : juce::Colours::grey);
    g.drawText(mark, w - 40, 0, 24, h, juce::Justification::centred);
}

void LibraryPanel::selectedRowsChanged(int row) {
    // Shift- or Cmd-click builds a group to export; only a single selection switches the preset
    if (row < 0 || row == proc_.bank.currentSlot() || list_.getNumSelectedRows() > 1) return;
    if (!proc_.isEdited()) { proc_.selectSlot(row); return; }
    int from = proc_.bank.currentSlot();
    auto opts = juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::QuestionIcon)
        .withTitle("Unsaved changes").withMessage(BankModel::bankName(from) + " has changes that are not stored.")
        .withButton("Store").withButton("Discard").withButton("Cancel").withAssociatedComponent(this);
    juce::AlertWindow::showAsync(opts, [this, row, from](int r) {
        if (r == 1) { proc_.store(); proc_.selectSlot(row); }
        else if (r == 2) proc_.selectSlot(row);
        else list_.selectRow(from, true, true);
    });
}

void LibraryPanel::listBoxItemDoubleClicked(int row, const juce::MouseEvent&) {
    proc_.selectSlot(row);
    proc_.selectOnDevice();
}

// A .json file: when any of its presets carries a slot number, ask whether
// they go back to their own slots or in from the selected slot.
void LibraryPanel::importJson(const juce::File& f) {
    auto status = [this](const juce::String& m) { setStatus(m); };
    auto pv = proc_.previewJson(f);
    if (!pv.errors.isEmpty()) {
        FM1Processor::ImportResult r;
        r.summary = "Nothing imported: " + f.getFileName() + " has " + juce::String(pv.errors.size()) + (pv.errors.size() == 1 ? " problem" : " problems");
        r.errors = pv.errors;
        showImportResult(this, r, status);
        return;
    }
    auto run = [this, f, status](FM1Processor::Placement where) {
        showImportResult(this, proc_.importJson(f, true, true, where), status);
        refresh();
    };
    if (pv.slots.empty()) { run(FM1Processor::Placement::FromSelected); return; }
    juce::String selected = BankModel::bankName(proc_.bank.currentSlot());
    juce::StringArray own;
    for (size_t i = 0; i < pv.slots.size() && i < 6; ++i) own.add(BankModel::bankName(pv.slots[i]));
    if (pv.slots.size() > 6) own.add("...");
    juce::String message;
    if (pv.presets == 1)
        message = "This preset was saved from " + own[0] + ".";
    else if (int(pv.slots.size()) == pv.presets)
        message = "These " + juce::String(pv.presets) + " presets were saved from their own slots (" + own.joinIntoString(", ") + ").";
    else
        message = juce::String(int(pv.slots.size())) + " of these " + juce::String(pv.presets) + " presets have their own slots ("
                  + own.joinIntoString(", ") + "). With their own slots, the others fill the free slots from " + selected + " on.";
    if (pv.patterns > 0) message << "\n\nThe file's " << pv.patterns << (pv.patterns == 1 ? " pattern goes" : " patterns go") << " to its own number.";
    auto opts = juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::QuestionIcon)
        .withTitle("Where should the presets go?").withMessage(message)
        .withButton(pv.presets == 1 ? "Into " + own[0] : "Their own slots")
        .withButton(pv.presets == 1 ? "Into " + selected + " (selected)" : "From " + selected + " (selected) on")
        .withButton("Cancel").withAssociatedComponent(this);
    juce::AlertWindow::showAsync(opts, [run](int r) {
        if (r == 1) run(FM1Processor::Placement::OwnSlots);
        else if (r == 2) run(FM1Processor::Placement::FromSelected);
    });
}

void LibraryPanel::refreshFxChannel() {
    fxChannel_.setSelectedId(proc_.channels.fx, juce::dontSendNotification);
}

void LibraryPanel::setEditorPages(juce::Component* fm, juce::Component* fx) {
    fmPage_ = fm;
    fxPage_ = fx;
    setFirmware(fm1::firmwareChoice(proc_.firmwareId().toStdString()));
}

void LibraryPanel::setFirmware(const fm1::FirmwareChoice& f) {
    vaEngine_ = f.vaEngine;
    pagesSlot_ = -1;
    pagesEngine_ = -1;   // build them again
    showPagesFor(proc_.bank.currentSlot());
}

// The tabs for the selected preset: Sync, its engine's editor (FM, or VA or 8-Bit in the same
// place), Effects & Envelope. The editor tab stays open when the engine changes with the preset.
void LibraryPanel::showPagesFor(int slot) {
    pagesSlot_ = slot;   // (the engine may change with the slot staying: a pull, an import)
    const auto engine = vaEngine_ ? fm1::engineOf(proc_.bank.slot(slot).sound.record) : fm1::Engine::FM;
    if (int(engine) == pagesEngine_) return;
    pagesEngine_ = int(engine);
    auto bg = juce::Colour(0xff26262e);
    juce::String was = pages_.getNumTabs() > 0 ? pages_.getCurrentTabName() : juce::String("Sync");
    if (was == "FM" || was == "VA" || was == "8-Bit") was = fm1::engineLabel(engine);
    pages_.clearTabs();
    pages_.addTab("Sync", bg, &syncPage_, false);
    if (engine == fm1::Engine::VA) pages_.addTab("VA", bg, &vaPage_, false);
    else if (engine == fm1::Engine::EightBit) pages_.addTab("8-Bit", bg, &chipPage_, false);
    else if (fmPage_ != nullptr) pages_.addTab("FM", bg, fmPage_, false);
    if (fxPage_ != nullptr) pages_.addTab("Effects & Envelope", bg, fxPage_, false);
    const int keep = pages_.getTabNames().indexOf(was);
    pages_.setCurrentTabIndex(keep >= 0 ? keep : 0, false);
}

void LibraryPanel::showInitMenu() {
    juce::PopupMenu m;
    m.addSectionHeader("Start this preset over from");
    m.addItem(1, "FM: INIT VOICE");
    if (vaEngine_) m.addItem(2, "VA (needs baud girl's VA engine source)", false);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&init_), [this](int r) {
        if (r == 1) { proc_.initCurrent(); refresh(); setStatus("A blank FM sound, not stored yet: Store keeps it, Revert drops it."); }
    });
}

std::vector<int> LibraryPanel::selectedSlots() const {
    std::vector<int> out;
    auto rows = list_.getSelectedRows();
    for (int i = 0; i < rows.size(); ++i) out.push_back(rows[i]);
    if (out.empty()) out.push_back(proc_.bank.currentSlot());
    return out;
}

void LibraryPanel::showExportMenu() {
    auto sel = selectedSlots();
    int bankStart = (proc_.bank.currentSlot() / fm1::kBankSlots) * fm1::kBankSlots;
    juce::String bankLetter = BankModel::bankName(bankStart).substring(0, 1);
    juce::PopupMenu m;
    m.addSectionHeader("JSON (readable, editable)");
    m.addItem(1, sel.size() == 1 ? "This preset: " + proc_.bank.slotLabel(sel[0]) + "..." : "Selected presets (" + juce::String(int(sel.size())) + ")...");
    m.addItem(2, "Bank " + bankLetter + " (32 presets)...");
    m.addItem(3, "All 128 presets...");
    m.addItem(4, "Everything: 128 presets and 16 patterns...");
    m.addSectionHeader("SysEx (FM-1+VA)");
    m.addItem(6, sel.size() == 1 ? "This preset as .syx..." : "Selected presets (" + juce::String(int(sel.size())) + ") as .syx...");
    m.addItem(5, "All 128 presets as .syx...");
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&exportFile_), [this, sel, bankStart, bankLetter](int id) {
        auto status = [this](const juce::String& msg) { setStatus(msg); };
        std::vector<int> all(BankModel::kSlots), pats(Sequencer::kPatterns);
        for (int i = 0; i < BankModel::kSlots; ++i) all[size_t(i)] = i;
        for (int i = 0; i < Sequencer::kPatterns; ++i) pats[size_t(i)] = i;
        auto json = [this, status](const juce::String& title, const juce::String& name, std::vector<int> slots, std::vector<int> patterns) {
            saveAs(chooser_, title, name, [this, slots, patterns](const juce::File& f) { return proc_.exportJson(f, slots, patterns); }, status);
        };
        if (id == 1) {
            juce::String name = sel.size() == 1 ? safeFileName(BankModel::bankName(sel[0]) + " " + juce::String(fm1::voiceName(proc_.bank.slot(sel[0]).sound.voice)).trim())
                                               : "fm1-presets-selected";
            json("Export presets as JSON", name + ".json", sel, {});
        } else if (id == 2) {
            std::vector<int> bank;
            for (int i = 0; i < fm1::kBankSlots; ++i) bank.push_back(bankStart + i);
            json("Export bank " + bankLetter + " as JSON", "fm1-bank-" + bankLetter + ".json", bank, {});
        } else if (id == 3) {
            json("Export all presets as JSON", "fm1-presets.json", all, {});
        } else if (id == 4) {
            json("Export everything as JSON", "fm1-everything.json", all, pats);
        } else if (id == 6) {
            juce::String name = sel.size() == 1 ? safeFileName(BankModel::bankName(sel[0]) + " " + juce::String(fm1::voiceName(proc_.bank.slot(sel[0]).sound.voice)).trim())
                                                : "fm1-presets-selected";
            saveAs(chooser_, "Export presets as .syx", name + ".syx", [this, sel](const juce::File& f) { return proc_.exportSyx(f, sel); }, status);
        } else if (id == 5) {
            saveAs(chooser_, "Export all 128 presets as .syx", "fm1-presets.syx", [this](const juce::File& f) { return proc_.exportSyx(f); }, status);
        }
    });
}

// ---- FmEditorPanel ------------------------------------------------------------------

FmEditorPanel::FmEditorPanel(FM1Processor& p) : proc_(p) {
    setLookAndFeel(&lnf_);
    proc_.params.fillVced(vced_.data());
    for (int op = 1; op <= 6; ++op) {
        auto panel = std::make_unique<OperatorPanel>(proc_.apvts, op, vced_.data(), lnf_);
        panel->onEnabledChanged = [this](int opNum, bool on) {
            int idx = 6 - opNum;
            proc_.opEnabled[size_t(idx)] = on;
            opStatus_[idx] = on ? '1' : '0';
            global_->repaint();
        };
        ops_[size_t(op - 1)] = std::move(panel);
        addAndMakeVisible(*ops_[size_t(op - 1)]);
    }
    global_ = std::make_unique<GlobalPanel>(proc_.apvts, vced_.data(), lnf_);
    global_->setOpStatus(opStatus_);
    addAndMakeVisible(global_->presetPart());
    for (int b = 0; b < 3; ++b) addAndMakeVisible(global_->dexedPart(b));
    const char* boxNames[3] = {"Algorithm", "LFO", "Pitch EG"};
    for (int i = 0; i < 9; ++i) {
        auto& b = pageButtons_[i];
        b.setButtonText(i < 3 ? juce::String(boxNames[i]) : "OP" + juce::String(i - 2));
        b.setClickingTogglesState(true);
        b.setRadioGroupId(4801);
        b.setLookAndFeel(&juce::LookAndFeel::getDefaultLookAndFeel());   // (Dexed's draws no toggled state)
        b.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffe0a040));   // the page shown
        b.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
        b.onClick = [this, i] { if (pageButtons_[i].getToggleState()) { page_ = i; resized(); } };
        addChildComponent(b);
    }
    pageButtons_[page_].setToggleState(true, juce::dontSendNotification);
    auto rename = [this] { proc_.setCurrentName(global_->name.getText()); };
    global_->name.onReturnKey = rename;
    global_->name.onFocusLost = rename;
    global_->storeButton.onClick = [this] { proc_.store(); refreshName(); };
    global_->revertButton.onClick = [this] { proc_.revert(); refreshName(); };
    global_->sendButton.onClick = [this] { proc_.sendToFm1EditBuffer(); };
    global_->liveButton.setToggleState(proc_.isLive(), juce::dontSendNotification);
    global_->liveButton.onClick = [this] { proc_.setLive(global_->liveButton.getToggleState()); };
    refreshName();
    startTimerHz(15);
}

FmEditorPanel::~FmEditorPanel() {
    stopTimer();
    global_.reset();
    for (auto& o : ops_) o.reset();
    setLookAndFeel(nullptr);
}

void FmEditorPanel::refreshName() {
    if (!global_->name.hasKeyboardFocus(true))
        global_->name.setText(proc_.editName(), juce::dontSendNotification);
    bool edited = proc_.isEdited();
    global_->slotLabel.setText(BankModel::bankName(proc_.bank.currentSlot())
        + juce::String(fm1::engineOf(proc_.bank.current().record) == fm1::Engine::VA ? "   Virtual Analog"
                       : fm1::engineOf(proc_.bank.current().record) == fm1::Engine::EightBit ? "   8-Bit" : "   FM")
        + (edited ? "\nedited, not stored" : ""), juce::dontSendNotification);
    global_->slotLabel.setColour(juce::Label::textColourId, edited ? juce::Colour(0xffe0a040) : juce::Colours::white.withAlpha(0.8f));
    global_->storeButton.setEnabled(edited);
    global_->revertButton.setEnabled(edited);
    global_->sendButton.setEnabled(proc_.link.isOpen());
    global_->liveButton.setEnabled(proc_.link.isOpen());
    if (global_->liveButton.getToggleState() != proc_.isLive()) global_->liveButton.setToggleState(proc_.isLive(), juce::dontSendNotification);
}

void FmEditorPanel::timerCallback() {
    proc_.params.fillVced(vced_.data());
    if (++tick_ % 5 == 0) refreshName();
    for (auto& o : ops_) o->refresh();
    global_->refresh();
}

void FmEditorPanel::paint(juce::Graphics& g) { g.fillAll(DXLookNFeel::lightBackground.darker(0.4f)); }

// Wide enough for Dexed's layout (866 x 584): the six operators and the global strip as
// Dexed has them. Narrower (a phone): the preset strip, a row of pages, one page at a time.
void FmEditorPanel::resized() {
    if (getWidth() >= 866 && getHeight() >= 584) layoutWide();
    else layoutCompact();
}

void FmEditorPanel::layoutWide() {
    for (auto& b : pageButtons_) b.setVisible(false);
    int x0 = std::max(0, (getWidth() - 866) / 2), y0 = 4;
    for (int i = 0; i < 6; ++i) {
        ops_[size_t(i)]->setTransform({});
        ops_[size_t(i)]->setVisible(true);
        ops_[size_t(i)]->setBounds(x0 + 2 + (i % 3) * 288, y0 + (i / 3) * 218, 287, 218);
    }
    auto& preset = global_->presetPart();
    preset.setTransform({});
    preset.setBounds(x0 + 2, y0 + 440, GlobalPanel::kPresetW, GlobalPanel::kH);
    for (int b = 0; b < 3; ++b) {   // Dexed's strip, its three boxes side by side
        auto& box = global_->dexedPart(b);
        box.setTransform({});
        box.setVisible(true);
        box.setBounds(x0 + 2 + 325 + GlobalPanel::kDexedX[b], y0 + 440, GlobalPanel::kDexedX[b + 1] - GlobalPanel::kDexedX[b], GlobalPanel::kH);
    }
}

void FmEditorPanel::layoutCompact() {
    // a component drawn at its own size, scaled into a place
    auto place = [](juce::Component& c, int w, int h, juce::Rectangle<int> into, float maxScale) {
        const float s = std::max(0.1f, std::min({maxScale, float(into.getWidth()) / float(w), float(into.getHeight()) / float(h)}));
        c.setBounds(0, 0, w, h);
        c.setTransform(juce::AffineTransform::scale(s).translated(float(into.getX()) + (float(into.getWidth()) - float(w) * s) / 2.0f,
                                                                 float(into.getY())));
        c.setVisible(true);
        return int(std::ceil(float(h) * s));
    };
    auto r = getLocalBounds().reduced(4);
    // the preset strip gives way to the page when the room is short (a phone on its side)
    const int presetH = place(global_->presetPart(), GlobalPanel::kPresetW, GlobalPanel::kH,
                              r.withHeight(std::max(40, r.getHeight() - 2 * 36 - 6 - 180)), 1.0f);
    r.removeFromTop(presetH + 6);
    auto buttons = [&](int from, int n) {   // a row of page buttons
        auto row = r.removeFromTop(32);
        const int bw = row.getWidth() / n;
        for (int i = from; i < from + n; ++i) {
            pageButtons_[i].setVisible(true);
            pageButtons_[i].setBounds(row.removeFromLeft(i == from + n - 1 ? row.getWidth() : bw).reduced(1, 0));
        }
        r.removeFromTop(4);
    };
    buttons(0, 3);
    buttons(3, 6);
    r.removeFromTop(2);
    for (int i = 0; i < 6; ++i) ops_[size_t(i)]->setVisible(false);
    for (int b = 0; b < 3; ++b) global_->dexedPart(b).setVisible(false);
    if (page_ < 3) place(global_->dexedPart(page_), GlobalPanel::kDexedX[page_ + 1] - GlobalPanel::kDexedX[page_], GlobalPanel::kH, r, 2.0f);
    else place(*ops_[size_t(page_ - 3)], 287, 218, r, 2.0f);
}

// ---- FxPanel -------------------------------------------------------------------------

FxPanel::FxPanel(FM1Processor& p) {
    auto& apvts = p.apvts;
    addAndMakeVisible(view_);
    view_.setViewedComponent(&content_, false);
    view_.setScrollBarsShown(true, false);
    auto group = [&](const juce::String& title, const juce::StringArray& ids, const juce::StringArray& labels) {
        makeLabel(headers_, content_, title, 14.0f, true);
        auto g = std::make_unique<ParamGrid>(apvts, ids, labels, ids.size(), 90, 74);
        content_.addAndMakeVisible(*g);
        grids_.push_back(std::move(g));
    };
    for (int e = 0; e < fm1::kEffects; ++e) {
        juce::StringArray ids, labels;
        ids.add(Params::fxOnId(e)); labels.add("On");
        if (fm1::kEffectTypeCount[e]) { ids.add(Params::fxTypeId(e)); labels.add("Type"); }
        for (int i = 0; i < 3; ++i)
            if (fm1::kEffectParamNames[e][i][0]) { ids.add(Params::fxParamId(e, i)); labels.add(fm1::kEffectParamNames[e][i]); }
        group(fm1::kEffectNames[e], ids, labels);
    }
    {
        juce::StringArray fids, flabels;
        const char* names[10] = {"On", "Type", "Key Track", "Cutoff", "Resonance", "Envelope", "Decay", "Shape", "Velocity", "LFO > Cutoff"};
        for (int f = 0; f < 10; ++f) { fids.add(Params::filterId(f)); flabels.add(names[f]); }
        group("Filter (per note)", fids, flabels);
    }
    group("Envelope", {Params::kEnvOn, Params::envId(0), Params::envId(1), Params::envId(2), Params::envId(3)},
          {"On", "Attack", "Decay", "Sustain", "Release"});
    note_.setText("Effects run in the preset's chain order. The filter (FM-1_092 and later) runs on each note after the operators. Both sound like the FM-1's approximately; the values sync exactly.", juce::dontSendNotification);
    note_.setFont(juce::FontOptions(12.0f));
    note_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.6f));
    note_.setJustificationType(juce::Justification::topLeft);
    content_.addAndMakeVisible(note_);
}

void FxPanel::resized() {
    view_.setBounds(getLocalBounds());
    const int w = std::max(200, view_.getWidth() - view_.getScrollBarThickness());
    const int headerW = 130;
    int y = 0;
    for (size_t i = 0; i < grids_.size(); ++i) {
        auto& g = *grids_[i];
        const int cell = g.cellWidth();
        if (w >= headerW + g.count() * cell) {   // header beside its controls, in one row
            headers_[i]->setBounds(10, y + 10, headerW - 10, 20);
            g.setColumns(g.count());
            g.setBounds(headerW, y, g.count() * cell, g.preferredHeight());
        } else {                                  // header above, the controls wrapped to the width
            headers_[i]->setBounds(10, y + 4, w - 20, 20);
            y += 24;
            const int cols = std::max(1, (w - 10) / cell);
            g.setColumns(cols);
            g.setBounds(10, y, std::min(g.count(), cols) * cell, g.preferredHeight());
        }
        y += g.preferredHeight() + 4;
    }
    y += 6;
    note_.setBounds(10, y, w - 20, 48);
    content_.setSize(w, y + 56);
}

// ---- ArpPanel -----------------------------------------------------------------------

ArpPanel::ArpPanel(FM1Processor& p) : proc_(p) {
    for (auto* c : std::initializer_list<juce::Component*>{&enable_, &mode_, &rate_, &octaves_, &tempo_, &gate_, &swing_, &latch_, &sync_}) addAndMakeVisible(c);
    enable_.setClickingTogglesState(true);
    enable_.setColour(juce::TextButton::buttonOnColourId, kAccent);
    for (int i = 0; i < Arpeggiator::kModes; ++i) mode_.addItem(Arpeggiator::kModeNames[i], i + 1);
    for (int i = 0; i < 10; ++i) rate_.addItem(fm1::seq::kNoteValueNames[i], i + 1);
    setupLinear(octaves_, 1, 4); setupLinear(tempo_, 30, 300); setupLinear(gate_, 0, 100); setupLinear(swing_, 50, 75);
    const char* names[] = {"Pattern", "Rate", "Octaves", "Tempo", "Gate %", "Swing"};
    for (const char* n : names) makeLabel(labels_, *this, n, 12.0f);
    auto& a = proc_.arp;
    enable_.setToggleState(a.enabled, juce::dontSendNotification);
    mode_.setSelectedId(a.mode + 1, juce::dontSendNotification); rate_.setSelectedId(a.rate + 1, juce::dontSendNotification);
    octaves_.setValue(a.octaves, juce::dontSendNotification); tempo_.setValue(a.tempo, juce::dontSendNotification);
    gate_.setValue(a.gate, juce::dontSendNotification); swing_.setValue(a.swing, juce::dontSendNotification);
    latch_.setToggleState(a.latch, juce::dontSendNotification); sync_.setToggleState(a.syncToHost, juce::dontSendNotification);
    enable_.onClick = [this] { proc_.arp.enabled = enable_.getToggleState(); };
    mode_.onChange = [this] { proc_.arp.mode = mode_.getSelectedId() - 1; };
    rate_.onChange = [this] { proc_.arp.rate = rate_.getSelectedId() - 1; };
    octaves_.onValueChange = [this] { proc_.arp.octaves = int(octaves_.getValue()); };
    tempo_.onValueChange = [this] { proc_.arp.tempo = int(tempo_.getValue()); };
    gate_.onValueChange = [this] { proc_.arp.gate = int(gate_.getValue()); };
    swing_.onValueChange = [this] { proc_.arp.swing = int(swing_.getValue()); };
    latch_.onClick = [this] { proc_.arp.latch = latch_.getToggleState(); };
    sync_.onClick = [this] { proc_.arp.syncToHost = sync_.getToggleState(); };
}

void ArpPanel::resized() {
    auto r = getLocalBounds().reduced(10);
    auto top = r.removeFromTop(28);
    enable_.setBounds(top.removeFromLeft(60)); top.removeFromLeft(12);
    latch_.setBounds(top.removeFromLeft(80)); sync_.setBounds(top.removeFromLeft(120));
    r.removeFromTop(12);
    auto area = r.removeFromLeft(420);
    auto rowOf = [&](juce::Label& l, juce::Component& c, int w = 320) { auto rr = area.removeFromTop(26); l.setBounds(rr.removeFromLeft(90)); c.setBounds(rr.removeFromLeft(w)); area.removeFromTop(6); };
    rowOf(*labels_[0], mode_, 140); rowOf(*labels_[1], rate_, 100); rowOf(*labels_[2], octaves_); rowOf(*labels_[3], tempo_); rowOf(*labels_[4], gate_); rowOf(*labels_[5], swing_);
}

// ---- SettingsPanel ------------------------------------------------------------------

SettingsPanel::SettingsPanel(FM1Processor& p) : proc_(p) {
    for (auto* c : std::initializer_list<juce::Component*>{&bendUp_, &bendDown_, &velocity_, &channel_, &velocityMode_, &save_, &revert_, &copy_, &note_, &synth_,
                                                           &hardware_, &hardwareNote_, &volumeDb_})
        addAndMakeVisible(c);
    makeLabel(headers_, *this, "Playing", 14.0f, true);
    makeLabel(headers_, *this, "FM-1", 14.0f, true);
    for (const char* n : {"Pitch bend up", "Pitch bend down", "MIDI input channel", "On-screen keyboard", "Sound"})
        makeLabel(labels_, *this, n, 13.0f);
    setupLinear(bendUp_, 0, 24);
    setupLinear(bendDown_, 0, 24);
    bendUp_.setTextValueSuffix(" semitones");
    bendDown_.setTextValueSuffix(" semitones");
    bendUp_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 100, 20);
    bendDown_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 100, 20);
    setupLinear(velocity_, 1, 127);
    channel_.addItem("Every channel", 1);
    for (int ch = 1; ch <= 16; ++ch) channel_.addItem("Channel " + juce::String(ch), ch + 1);
    channel_.setTooltip("Which of the host's MIDI channels the plugin plays. The on-screen keyboard always plays. "
                        "This is the plugin's own setting, not the FM-1's MIDI channel.");
    velocityMode_.addItem("Velocity by where a key is clicked", 1);
    velocityMode_.addItem("Fixed velocity", 2);
    velocityMode_.setTooltip("The velocity of notes played and step-recorded with the on-screen keyboard, "
                             "like the FM-1's Keyboard > Velocity for its own keys");
    for (auto* sl : {&bendUp_, &bendDown_, &velocity_}) sl->onValueChange = [this] { apply(); };
    channel_.onChange = [this] { apply(); };
    hardware_.onClick = [this] { apply(); };
    for (auto* c : std::initializer_list<juce::Component*>{&embedBank_, &libraryPath_, &showLibrary_}) addAndMakeVisible(c);
    libraryHeader_ = makeLabel(headers_, *this, "Library", 14.0f, true);
    embedBank_.setTooltip("Projects always keep the sound they use. With this on they also keep all 128 presets, "
                          "to open them on a computer that does not have your library.");
    embedBank_.onClick = [this] { apply(); };
    libraryPath_.setFont(juce::FontOptions(13.0f));
    libraryPath_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.7f));
    libraryPath_.setText("Your presets are in " + LibraryStore::root().getFullPathName()
                         + " (one file per preset, in Banks/FM-1). Every instance shares them.", juce::dontSendNotification);
    showLibrary_.onClick = [] {
        auto dir = LibraryStore::root();
        dir.createDirectory();
        dir.revealToUser();
    };
    setupLinear(volumeDb_, -40, 0);
    volumeDb_.setTextValueSuffix(" dB");
    volumeDb_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 20);
    volumeDb_.onValueChange = [this] { apply(); };
    volumeDb_.setTooltip("How far below full the FM-1's volume is. Lower is grainier on decays and quiet passages, "
                         "as the FM-1's USB audio is at lower MASTER settings; the plugin's own level does not change.");
    hardware_.setTooltip("Sound like the FM-1's USB audio: 16-bit output at its level and its slightly softer high end. "
                         "Saved with the project; nothing is sent to the synth.");
    velocityMode_.onChange = [this] { apply(); };
    for (auto* l : {&note_, &synth_, &hardwareNote_}) {
        l->setFont(juce::FontOptions(13.0f));
        l->setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.7f));
        l->setJustificationType(juce::Justification::topLeft);
    }
    note_.setText("Changes take effect now and are saved with your project. New instances start from the defaults.", juce::dontSendNotification);
    hardwareNote_.setText("Off: the engine at full resolution. On: like the FM-1's own output, measured from its USB audio "
                          "(16 bits, a gentle high-frequency roll-off). The dB slider sets how far below full the FM-1's volume is: "
                          "lower is grainier. Close, not exact: some details need the firmware's source.",
                          juce::dontSendNotification);
    save_.onClick = [this] {
        note_.setText(proc_.saveSettingsAsDefault() ? "Saved as the defaults for new instances."
                                                    : "Could not write " + FM1Processor::defaultSettingsFile().getFullPathName(),
                      juce::dontSendNotification);
    };
    revert_.onClick = [this] {
        proc_.revertSettingsToDefault();
        note_.setText("Back to the defaults.", juce::dontSendNotification);
    };
    copy_.setTooltip("Copies the FM-1's pitch-bend range and its Keyboard > Velocity into these settings");
    copy_.onClick = [this] {
        copyPending_ = proc_.readSynthSettings(true);
        note_.setText(copyPending_ ? "Reading the FM-1's settings..." : "The FM-1 is busy or not connected; try again in a moment.",
                      juce::dontSendNotification);
    };
    refresh();
    showSynth();
}

void SettingsPanel::refresh() {
    loading_ = true;
    const auto& s = proc_.settings();
    bendUp_.setValue(s.bendUp, juce::dontSendNotification);
    bendDown_.setValue(s.bendDown, juce::dontSendNotification);
    channel_.setSelectedId(s.midiChannel + 1, juce::dontSendNotification);
    velocityMode_.setSelectedId(s.fixedVelocity ? 2 : 1, juce::dontSendNotification);
    velocity_.setValue(s.velocity, juce::dontSendNotification);
    velocity_.setEnabled(s.fixedVelocity);
    hardware_.setToggleState(s.hardwareCharacter, juce::dontSendNotification);
    embedBank_.setToggleState(s.embedBank, juce::dontSendNotification);
    volumeDb_.setValue(s.fm1VolumeDb, juce::dontSendNotification);
    volumeDb_.setEnabled(s.hardwareCharacter);
    loading_ = false;
}

void SettingsPanel::apply() {
    if (loading_) return;
    PluginSettings s = proc_.settings();
    s.bendUp = int(bendUp_.getValue());
    s.bendDown = int(bendDown_.getValue());
    s.midiChannel = channel_.getSelectedId() - 1;
    s.fixedVelocity = velocityMode_.getSelectedId() == 2;
    s.velocity = int(velocity_.getValue());
    s.hardwareCharacter = hardware_.getToggleState();
    s.embedBank = embedBank_.getToggleState();
    s.fm1VolumeDb = int(volumeDb_.getValue());
    proc_.setSettings(s);
}

void SettingsPanel::visibilityChanged() {
    if (isShowing()) proc_.readSynthSettings();
}

void SettingsPanel::showSynth() {
    if (copyPending_) {
        copyPending_ = false;
        note_.setText("Copied the FM-1's bend range and key velocity. Save as default to keep them for new instances.", juce::dontSendNotification);
    }
    auto g = proc_.synthGlobals();
    copy_.setEnabled(g.has_value());
    if (!g) {
        synth_.setText("Not read yet. With an FM-1 running FM-1_093 or FM-1_094 connected, the plugin reads its GLOBE settings and "
                       "uses its MIDI and FX channels to talk to it.", juce::dontSendNotification);
        return;
    }
    synth_.setText("Read from the FM-1's GLOBE settings: MIDI channel " + (g->midiChannel == 0 ? juce::String("All") : juce::String(g->midiChannel))
        + ", FX channel " + juce::String(g->fxChannel) + ", pitch bend +" + juce::String(g->bendUp) + " / -" + juce::String(g->bendDown)
        + " semitones, key velocity " + juce::String(g->keyVelocity) + ", glide " + (g->glideFingered ? "Fingered" : "Full Time") + " "
        + juce::String(g->glideTime) + ", drive " + (g->driveMinus6 ? "-6" : "0") + ", CC7 volume " + (g->cc7Volume ? "on" : "off")
        + ", overdub rec " + (g->overdubRec ? "on" : "off") + ".\nThe plugin talks to the FM-1 on its MIDI and FX channels. "
        "It has no glide or drive yet.",
        juce::dontSendNotification);
}

void SettingsPanel::resized() {
    auto r = getLocalBounds().reduced(14);
    auto row = [&](int labelIndex, juce::Component& c, juce::Component* extra = nullptr) {
        auto rr = r.removeFromTop(28);
        labels_[size_t(labelIndex)]->setBounds(rr.removeFromLeft(180));
        c.setBounds(rr.removeFromLeft(extra ? 300 : 420));
        if (extra) { rr.removeFromLeft(10); extra->setBounds(rr.removeFromLeft(240)); }
        r.removeFromTop(8);
    };
    headers_[0]->setBounds(r.removeFromTop(24));
    r.removeFromTop(6);
    row(0, bendUp_);
    row(1, bendDown_);
    row(2, channel_);
    row(3, velocityMode_, &velocity_);
    row(4, hardware_, &volumeDb_);
    hardwareNote_.setBounds(r.removeFromTop(48).withTrimmedLeft(180));
    r.removeFromTop(4);
    r.removeFromTop(4);
    note_.setBounds(r.removeFromTop(22));
    r.removeFromTop(6);
    {
        auto rr = r.removeFromTop(28);
        save_.setBounds(rr.removeFromLeft(160));
        rr.removeFromLeft(8);
        revert_.setBounds(rr.removeFromLeft(160));
    }
    r.removeFromTop(28);
    headers_[1]->setBounds(r.removeFromTop(24));
    r.removeFromTop(6);
    synth_.setBounds(r.removeFromTop(40));
    r.removeFromTop(6);
    copy_.setBounds(r.removeFromTop(28).removeFromLeft(160));
    if (libraryHeader_ != nullptr) {
        r.removeFromTop(24);
        libraryHeader_->setBounds(r.removeFromTop(24));
        r.removeFromTop(6);
        libraryPath_.setBounds(r.removeFromTop(20));
        r.removeFromTop(4);
        auto row = r.removeFromTop(28);
        showLibrary_.setBounds(row.removeFromLeft(160));
        row.removeFromLeft(12);
        embedBank_.setBounds(row.removeFromLeft(420));
    }
}
