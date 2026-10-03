#include "Panels.h"

#if JUCE_IOS || JUCE_ANDROID
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

#include "Fm1Record.h"

namespace {
const juce::Colour kAccent(0xffe0a040), kDim(0xff3a3a44), kNoteCol(0xff4a8ad0), kPlaying(0xfff4f4f4), kSelected(0xffffffff);

juce::String noteName(int n) {
    static const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return juce::String(names[n % 12]) + juce::String(n / 12 - 2);
}
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
            auto f = fc.getResult();
            if (f == juce::File()) return;
            status(write(f) ? "Saved " + f.getFileName() : "Could not write " + f.getFileName());
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
    for (auto* c : std::initializer_list<juce::Component*>{&inPorts_, &outPorts_, &connect_, &autoConnect_, &identity_, &list_, &currentName_,
            &pullCurrent_, &pushCurrent_, &pullAll_, &pushChanged_, &pushAll_, &selectOnDevice_, &cancel_, &importFile_, &exportFile_, &status_,
            &sendEdit_, &live_, &fxChannel_})
        addAndMakeVisible(c);
    refreshPorts();
    connect_.onClick = [this] {
        auto ins = Fm1Link::inputs(), outs = Fm1Link::outputs();
        int i = inPorts_.getSelectedItemIndex(), o = outPorts_.getSelectedItemIndex();
        if (i < 0 || o < 0 || i >= ins.size() || o >= outs.size()) { setStatus("Choose an input and an output port."); return; }
        proc_.connect(ins[i].identifier, outs[o].identifier);
    };
    autoConnect_.onClick = [this] { refreshPorts(); if (proc_.autoConnect()) refreshPorts(); };
    pullCurrent_.onClick = [this] { proc_.pullCurrent(); };
    pushCurrent_.onClick = [this] { proc_.pushCurrent(); };
    sendEdit_.onClick = [this] { proc_.sendToFm1EditBuffer(); };
    live_.onClick = [this] { proc_.setLive(live_.getToggleState()); };
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
            auto f = fc.getResult();
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
    list_.selectRow(proc_.bank.currentSlot());
    currentName_.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    startTimerHz(10);
    refreshButtons();
}

LibraryPanel::~LibraryPanel() = default;

void LibraryPanel::refresh() {
    list_.updateContent();
    list_.repaint();
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
    live_.setEnabled(open);
    cancel_.setEnabled(busy);
    connect_.setEnabled(!busy);
    autoConnect_.setEnabled(!busy);
}

void LibraryPanel::timerCallback() {
    if (live_.getToggleState() != proc_.isLive()) live_.setToggleState(proc_.isLive(), juce::dontSendNotification);
    auto label = proc_.bank.slotLabel(proc_.bank.currentSlot()) + (proc_.isEdited() ? "  (edited)" : "");
    if (currentName_.getText() != label) currentName_.setText(label, juce::dontSendNotification);
    bool busy = proc_.session.busy();
    bool open = proc_.link.isOpen();
    if (busy != wasBusy_ || open != wasOpen_) { wasBusy_ = busy; wasOpen_ = open; refreshButtons(); refreshPorts(); }
    if (!busy && !proc_.link.isOpen() && identity_.getText().isNotEmpty()) identity_.setText("", juce::dontSendNotification);
}

void LibraryPanel::resized() {
    auto r = getLocalBounds().reduced(10);
    auto top = r.removeFromTop(28);
    inPorts_.setBounds(top.removeFromLeft(230)); top.removeFromLeft(6);
    outPorts_.setBounds(top.removeFromLeft(230)); top.removeFromLeft(6);
    connect_.setBounds(top.removeFromLeft(90)); top.removeFromLeft(6);
    autoConnect_.setBounds(top.removeFromLeft(90)); top.removeFromLeft(6);
    identity_.setBounds(top);
    r.removeFromTop(8);
    status_.setBounds(r.removeFromBottom(24));
    r.removeFromBottom(6);
    list_.setBounds(r.removeFromLeft(360));
    r.removeFromLeft(12);
    currentName_.setBounds(r.removeFromTop(30));
    r.removeFromTop(10);
    auto row = [&](juce::Component& a, juce::Component* b = nullptr) {
        auto rr = r.removeFromTop(30);
        if (b) { a.setBounds(rr.removeFromLeft(rr.getWidth() / 2 - 3)); rr.removeFromLeft(6); b->setBounds(rr); }
        else a.setBounds(rr);
        r.removeFromTop(6);
    };
    row(pullCurrent_, &sendEdit_);
    row(live_, &fxChannel_);
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
    bool va = fm1::engineOf(s.sound.record) == fm1::Engine::VA;
    g.setColour(va ? juce::Colours::orange : juce::Colours::lightgreen);
    g.drawText(va ? "VA" : "FM", w - 80, 0, 24, h, juce::Justification::centred);
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
    addAndMakeVisible(*global_);
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
        + (fm1::engineOf(proc_.bank.current().record) == fm1::Engine::VA ? "   Virtual Analog" : "   FM")
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

void FmEditorPanel::resized() {
    int x0 = std::max(0, (getWidth() - 866) / 2), y0 = 4;
    for (int i = 0; i < 6; ++i) ops_[size_t(i)]->setBounds(x0 + 2 + (i % 3) * 288, y0 + (i / 3) * 218, 287, 218);
    global_->setBounds(x0 + 2, y0 + 436 + 4, 864, 144);
}

// ---- FxPanel -------------------------------------------------------------------------

FxPanel::FxPanel(FM1Processor& p) {
    auto& apvts = p.apvts;
    int y = 0;
    for (int e = 0; e < fm1::kEffects; ++e) {
        juce::StringArray ids, labels;
        ids.add(Params::fxOnId(e)); labels.add("On");
        if (fm1::kEffectTypeCount[e]) { ids.add(Params::fxTypeId(e)); labels.add("Type"); }
        for (int i = 0; i < 3; ++i)
            if (fm1::kEffectParamNames[e][i][0]) { ids.add(Params::fxParamId(e, i)); labels.add(fm1::kEffectParamNames[e][i]); }
        auto* h = makeLabel(headers_, *this, fm1::kEffectNames[e], 14.0f, true);
        h->setBounds(10, y + 10, 200, 20);
        auto g = std::make_unique<ParamGrid>(apvts, ids, labels, 5, 90, 74);
        g->setBounds(130, y, 5 * 90, g->preferredHeight());
        addAndMakeVisible(*g);
        grids_.push_back(std::move(g));
        y += 78;
    }
    {
        juce::StringArray fids, flabels;
        const char* names[10] = {"On", "Type", "Key Track", "Cutoff", "Resonance", "Envelope", "Decay", "Shape", "Velocity", "LFO > Cutoff"};
        for (int f = 0; f < 10; ++f) { fids.add(Params::filterId(f)); flabels.add(names[f]); }
        auto* fh = makeLabel(headers_, *this, "Filter (per note)", 14.0f, true);
        fh->setBounds(10, y + 10, 120, 20);
        auto fg = std::make_unique<ParamGrid>(apvts, fids, flabels, 10, 90, 74);
        fg->setBounds(130, y, 10 * 90, fg->preferredHeight());
        addAndMakeVisible(*fg);
        grids_.push_back(std::move(fg));
        y += 78;
    }
    juce::StringArray ids = {Params::kEnvOn, Params::envId(0), Params::envId(1), Params::envId(2), Params::envId(3)};
    juce::StringArray labels = {"On", "Attack", "Decay", "Sustain", "Release"};
    auto* h = makeLabel(headers_, *this, "Envelope", 14.0f, true);
    h->setBounds(10, y + 10, 200, 20);
    auto g = std::make_unique<ParamGrid>(apvts, ids, labels, 5, 90, 74);
    g->setBounds(130, y, 5 * 90, g->preferredHeight());
    addAndMakeVisible(*g);
    grids_.push_back(std::move(g));
    y += 84;
    note_.setText("Effects run in the preset's chain order. The filter (FM-1_092 and later) runs on each note after the operators. Both sound like the FM-1's approximately; the values sync exactly.", juce::dontSendNotification);
    note_.setFont(juce::FontOptions(12.0f));
    note_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.6f));
    note_.setBounds(10, y, 800, 20);
    addAndMakeVisible(note_);
}

void FxPanel::resized() {}

// ---- SeqPanel --------------------------------------------------------------------------

SeqPanel::SeqPanel(FM1Processor& p) : proc_(p) {
    for (auto* c : std::initializer_list<juce::Component*>{&enable_, &play_, &rec_, &clearStep_, &clearPattern_, &copyStep_, &pasteStep_, &pull_, &push_,
            &sync_, &overdub_, &pattern_, &rate_, &chainTo_, &stepRate_, &ratchet_, &length_, &tempo_, &gate_, &swing_, &sound_, &transpose_,
            &stepGate_, &stepChance_, &stepTranspose_, &accent_, &slide_, &stepNotes_, &info_})
        addAndMakeVisible(c);
    enable_.setClickingTogglesState(true);
    enable_.setColour(juce::TextButton::buttonOnColourId, kAccent);
    rec_.setClickingTogglesState(true);
    rec_.onClick = [this] { proc_.sequencer.recording = rec_.getToggleState() && proc_.sequencer.isPlaying(); };
    rec_.setColour(juce::TextButton::buttonOnColourId, juce::Colours::indianred);
    play_.setColour(juce::TextButton::buttonOnColourId, juce::Colours::seagreen);
    for (int i = 1; i <= Sequencer::kPatterns; ++i) pattern_.addItem("Pattern " + juce::String(i), i);
    for (int i = 0; i < 10; ++i) { rate_.addItem(fm1::seq::kNoteValueNames[i], i + 1); stepRate_.addItem(fm1::seq::kNoteValueNames[i], i + 1); }
    chainTo_.addItem("Repeat", 1);
    for (int i = 1; i <= Sequencer::kPatterns; ++i) chainTo_.addItem("then " + juce::String(i), i + 1);
    ratchet_.addItem("Off", 1); ratchet_.addItem("2", 2); ratchet_.addItem("3", 3); ratchet_.addItem("4", 4);
    setupLinear(length_, 1, 64); setupLinear(tempo_, 30, 300); setupLinear(gate_, 5, 100); setupLinear(swing_, 50, 75);
    setupLinear(sound_, 0, 128);
    sound_.textFromValueFunction = [](double v) { return v < 0.5 ? juce::String("None") : juce::String(int(v)); }; setupLinear(transpose_, -24, 24);
    setupLinear(stepGate_, 0, 100); setupLinear(stepChance_, 5, 100); setupLinear(stepTranspose_, -24, 24);
    stepGate_.textFromValueFunction = [](double v) { return v <= 0 ? juce::String("Pattern") : juce::String(int(v)); };
    stepChance_.textFromValueFunction = [](double v) { return v >= 100 ? juce::String("Always") : juce::String(int(v)); };
    const char* names[] = {"Length", "Tempo", "Gate %", "Swing", "Preset", "Transpose", "Note value", "Ratchet", "Step gate", "Chance", "Step transpose", "Step value"};
    for (const char* n : names) makeLabel(labels_, *this, n, 12.0f);
    sync_.setToggleState(proc_.sequencer.syncToHost, juce::dontSendNotification);
    overdub_.setToggleState(proc_.sequencer.overdub, juce::dontSendNotification);
    enable_.setToggleState(proc_.sequencer.enabled, juce::dontSendNotification);
    pattern_.setSelectedId(proc_.sequencer.selected + 1, juce::dontSendNotification);

    enable_.onClick = [this] { proc_.sequencer.enabled = enable_.getToggleState(); if (!enable_.getToggleState()) proc_.sequencer.stop(); };
    play_.onClick = [this] {
        if (proc_.sequencer.isPlaying()) proc_.sequencer.stop();
        else { proc_.sequencer.enabled = true; enable_.setToggleState(true, juce::dontSendNotification); proc_.sequencer.play(); }
    };
    sync_.onClick = [this] { proc_.sequencer.syncToHost = sync_.getToggleState(); };
    overdub_.onClick = [this] { proc_.sequencer.overdub = overdub_.getToggleState(); };
    pattern_.onChange = [this] { proc_.sequencer.selected = pattern_.getSelectedId() - 1; selectedStep_ = 0; loadPatternControls(); loadStepControls(); repaint(); };
    auto applyP = [this] { applyPatternControls(); };
    rate_.onChange = applyP; chainTo_.onChange = applyP;
    for (auto* s : {&length_, &tempo_, &gate_, &swing_, &transpose_}) s->onValueChange = applyP;
    // the FM-1's sequencer plays whatever preset is selected; there is no preset per pattern
    sound_.setVisible(false);
    labels_[4]->setVisible(false);
    auto applyS = [this] { applyStepControls(); };
    stepRate_.onChange = applyS; ratchet_.onChange = applyS;
    for (auto* s : {&stepGate_, &stepChance_, &stepTranspose_}) s->onValueChange = applyS;
    accent_.onClick = applyS; slide_.onClick = applyS;
    clearStep_.onClick = [this] { { const juce::SpinLock::ScopedLockType l(proc_.sequencer.lock); pattern().steps[size_t(selectedStep_)] = fm1::seq::Step{}; pattern().steps[size_t(selectedStep_)].rate = pattern().rate; } loadStepControls(); repaint(); };
    addAndMakeVisible(importPatterns_);
    addAndMakeVisible(exportPatterns_);
    addAndMakeVisible(fileStatus_);
    fileStatus_.setFont(juce::FontOptions(13.0f));
    fileStatus_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.7f));
    importPatterns_.setTooltip("Load patterns from a .json file (presets in it are left alone), or a MIDI file into this pattern");
    exportPatterns_.setTooltip("Save this pattern or all 16 as .json, with every step setting, or as a MIDI file");
    importPatterns_.onClick = [this] {
        chooser_ = std::make_unique<juce::FileChooser>("Import patterns (.json), or a MIDI file into this pattern (.mid)", documents(), "*.json;*.mid;*.midi");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
            auto f = fc.getResult();
            if (!f.existsAsFile()) return;
            if (f.hasFileExtension("mid;midi")) fileStatus_.setText(proc_.importPatternMidi(f), juce::dontSendNotification);
            else showImportResult(this, proc_.importJson(f, false, true), [this](const juce::String& m) { fileStatus_.setText(m, juce::dontSendNotification); });
            loadPatternControls();
            loadStepControls();
            repaint();
        });
    };
    exportPatterns_.onClick = [this] { showExportMenu(); };
    clearPattern_.onClick = [this] { { const juce::SpinLock::ScopedLockType l(proc_.sequencer.lock); int r = pattern().rate; for (auto& s : pattern().steps) { s = fm1::seq::Step{}; s.rate = r; } } loadStepControls(); repaint(); };
    copyStep_.onClick = [this] { const juce::SpinLock::ScopedLockType l(proc_.sequencer.lock); clipboard_ = pattern().steps[size_t(selectedStep_)]; };
    pasteStep_.onClick = [this] { if (!clipboard_) return; { const juce::SpinLock::ScopedLockType l(proc_.sequencer.lock); pattern().steps[size_t(selectedStep_)] = *clipboard_; } loadStepControls(); repaint(); };
    pull_.onClick = [this] { proc_.pullPatterns(); };
    push_.onClick = [this] { proc_.pushPatterns(true); };
    info_.setFont(juce::FontOptions(12.0f));
    info_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.6f));
    info_.setText("Click a step to select it. Rec while stopped: notes go into the selected step and it advances. Rec while playing: notes land on the nearest step, held notes are tied (~). Ties, ratchet, chance and accent stay in the plugin.", juce::dontSendNotification);
    stepNotes_.setFont(juce::FontOptions(14.0f));
    for (int i = 0; i < fm1::seq::kMaxNotes; ++i) {
        auto r = std::make_unique<NoteRow>();
        r->name.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        r->velocity.setSliderStyle(juce::Slider::LinearHorizontal);
        r->velocity.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 18);
        r->velocity.setRange(1, 127, 1);
        r->velocity.setScrollWheelEnabled(false);
        r->velocity.onValueChange = [this, i] { applyNoteRow(i); };
        r->tie.onClick = [this, i] { applyNoteRow(i); };
        r->remove.onClick = [this, i] {
            {
                const juce::SpinLock::ScopedLockType l(proc_.sequencer.lock);
                auto& notes = pattern().steps[size_t(selectedStep_)].notes;
                if (i < int(notes.size())) notes.erase(notes.begin() + i);
            }
            loadStepControls(); repaint();
        };
        for (auto* c : std::initializer_list<juce::Component*>{&r->name, &r->velocity, &r->tie, &r->remove}) addChildComponent(c);
        noteRows_[size_t(i)] = std::move(r);
    }
    allVelocity_.setSliderStyle(juce::Slider::LinearHorizontal);
    allVelocity_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 18);
    allVelocity_.setRange(1, 127, 1);
    allVelocity_.setScrollWheelEnabled(false);
    allVelocity_.onValueChange = [this] {
        if (loading_) return;
        {
            const juce::SpinLock::ScopedLockType l(proc_.sequencer.lock);
            for (auto& n : pattern().steps[size_t(selectedStep_)].notes) n.vel = int(allVelocity_.getValue());
        }
        loadStepControls();
    };
    addChildComponent(allVelLabel_);
    addChildComponent(allVelocity_);
    loadPatternControls();
    loadStepControls();
    startTimerHz(20);
}

fm1::seq::Pattern& SeqPanel::pattern() { return proc_.sequencer.patterns[size_t(proc_.sequencer.selected.load())]; }

void SeqPanel::loadPatternControls() {
    loading_ = true;
    const juce::SpinLock::ScopedLockType l(proc_.sequencer.lock);
    const auto& p = pattern();
    rate_.setSelectedId(p.rate + 1, juce::dontSendNotification);
    chainTo_.setSelectedId(proc_.sequencer.chain[size_t(proc_.sequencer.selected.load())] + 2, juce::dontSendNotification);
    length_.setValue(p.length, juce::dontSendNotification); tempo_.setValue(p.tempo, juce::dontSendNotification);
    gate_.setValue(p.gate, juce::dontSendNotification); swing_.setValue(p.swing, juce::dontSendNotification);
    transpose_.setValue(p.transpose, juce::dontSendNotification);
    loading_ = false;
}

void SeqPanel::loadStepControls() {
    loading_ = true;
    const juce::SpinLock::ScopedLockType l(proc_.sequencer.lock);
    const auto& s = pattern().steps[size_t(selectedStep_)];
    stepRate_.setSelectedId(s.rate + 1, juce::dontSendNotification);
    ratchet_.setSelectedId(s.ratchet, juce::dontSendNotification);
    stepGate_.setValue(s.gate, juce::dontSendNotification); stepChance_.setValue(s.chance, juce::dontSendNotification);
    stepTranspose_.setValue(s.transpose, juce::dontSendNotification);
    accent_.setToggleState(s.accent, juce::dontSendNotification); slide_.setToggleState(s.slide, juce::dontSendNotification);
    stepNotes_.setText("Step " + juce::String(selectedStep_ + 1) + (s.notes.empty() ? ":  (empty)" : ""), juce::dontSendNotification);
    for (int i = 0; i < fm1::seq::kMaxNotes; ++i) {
        auto& r = *noteRows_[size_t(i)];
        bool used = i < int(s.notes.size());
        for (auto* c : std::initializer_list<juce::Component*>{&r.name, &r.velocity, &r.tie, &r.remove}) c->setVisible(used);
        if (!used) continue;
        r.name.setText(noteName(s.notes[size_t(i)].note), juce::dontSendNotification);
        r.velocity.setValue(s.notes[size_t(i)].vel, juce::dontSendNotification);
        r.tie.setToggleState(s.notes[size_t(i)].tie, juce::dontSendNotification);
    }
    bool several = s.notes.size() > 1;
    allVelLabel_.setVisible(several);
    allVelocity_.setVisible(several);
    if (several) allVelocity_.setValue(s.notes[0].vel, juce::dontSendNotification);
    layoutNoteRows(noteArea_);
    loading_ = false;
}

void SeqPanel::applyPatternControls() {
    if (loading_) return;
    const juce::SpinLock::ScopedLockType l(proc_.sequencer.lock);
    auto& p = pattern();
    int newRate = rate_.getSelectedId() - 1;
    if (newRate != p.rate) { for (auto& s : p.steps) if (s.rate == p.rate) s.rate = newRate; p.rate = newRate; }
    proc_.sequencer.chain[size_t(proc_.sequencer.selected.load())] = chainTo_.getSelectedId() - 2;
    p.length = int(length_.getValue()); p.tempo = int(tempo_.getValue()); p.gate = int(gate_.getValue());
    p.swing = int(swing_.getValue());
    p.transpose = int(transpose_.getValue());
    repaint();
}

void SeqPanel::applyNoteRow(int i) {
    if (loading_) return;
    const juce::SpinLock::ScopedLockType l(proc_.sequencer.lock);
    auto& notes = pattern().steps[size_t(selectedStep_)].notes;
    if (i >= int(notes.size())) return;
    notes[size_t(i)].vel = int(noteRows_[size_t(i)]->velocity.getValue());
    notes[size_t(i)].tie = noteRows_[size_t(i)]->tie.getToggleState();
}

void SeqPanel::layoutNoteRows(juce::Rectangle<int> area) {
    int rows = allVelocity_.isVisible() ? 1 : 0;
    for (auto& row : noteRows_) rows += row->name.isVisible() ? 1 : 0;
    // nine notes plus "All notes" must fit under the grid: rows shrink when they would not
    const int rowH = rows ? juce::jlimit(16, 24, area.getHeight() / rows - 2) : 24;
    auto a = area;
    if (allVelocity_.isVisible()) {
        auto r = a.removeFromTop(rowH);
        allVelLabel_.setBounds(r.removeFromLeft(80));
        allVelocity_.setBounds(r.removeFromLeft(std::min(240, r.getWidth())));
        a.removeFromTop(4);
    }
    for (auto& row : noteRows_) {
        if (!row->name.isVisible()) continue;
        auto r = a.removeFromTop(rowH);
        row->name.setBounds(r.removeFromLeft(56));
        row->remove.setBounds(r.removeFromRight(70));
        r.removeFromRight(6);
        row->tie.setBounds(r.removeFromRight(56));
        row->velocity.setBounds(r);
        a.removeFromTop(2);
    }
}

void SeqPanel::applyStepControls() {
    if (loading_) return;
    const juce::SpinLock::ScopedLockType l(proc_.sequencer.lock);
    auto& s = pattern().steps[size_t(selectedStep_)];
    s.rate = stepRate_.getSelectedId() - 1; s.ratchet = ratchet_.getSelectedId();
    s.gate = int(stepGate_.getValue()); s.chance = int(stepChance_.getValue()); s.transpose = int(stepTranspose_.getValue());
    s.accent = accent_.getToggleState(); s.slide = slide_.getToggleState();
}

juce::Rectangle<int> SeqPanel::gridBounds() const { return {10, 80, 8 * 46, 8 * 46}; }
juce::Rectangle<int> SeqPanel::cellBounds(int step) const {
    auto g = gridBounds();
    return {g.getX() + (step % 8) * 46, g.getY() + (step / 8) * 46, 42, 42};
}

void SeqPanel::paint(juce::Graphics& g) {
    int playing = proc_.sequencer.isPlaying() && proc_.sequencer.playingPattern() == proc_.sequencer.selected ? proc_.sequencer.playingStep() : -1;
    int length; std::array<bool, 64> hasNotes{}; std::array<bool, 64> accent{};
    {
        const juce::SpinLock::ScopedLockType l(proc_.sequencer.lock);
        const auto& p = pattern();
        length = p.length;
        for (int i = 0; i < 64; ++i) { hasNotes[size_t(i)] = !p.steps[size_t(i)].notes.empty(); accent[size_t(i)] = p.steps[size_t(i)].accent; }
    }
    for (int i = 0; i < 64; ++i) {
        auto c = cellBounds(i);
        juce::Colour col = i >= length ? kDim.withAlpha(0.5f) : hasNotes[size_t(i)] ? (accent[size_t(i)] ? kAccent : kNoteCol) : kDim;
        if (i == playing) col = kPlaying;
        g.setColour(col);
        g.fillRoundedRectangle(c.toFloat(), 5.0f);
        if (i == selectedStep_) { g.setColour(kSelected); g.drawRoundedRectangle(c.toFloat().reduced(1.0f), 5.0f, 2.0f); }
        g.setColour(i == playing ? juce::Colours::black : juce::Colours::white.withAlpha(0.7f));
        g.setFont(juce::FontOptions(11.0f));
        g.drawText(juce::String(i + 1), c, juce::Justification::centred);
    }
}

void SeqPanel::mouseDown(const juce::MouseEvent& e) {
    for (int i = 0; i < 64; ++i)
        if (cellBounds(i).contains(e.getPosition())) { selectedStep_ = i; loadStepControls(); repaint(); return; }
}

void SeqPanel::timerCallback() {
    if (int v = proc_.patternsVersion.load(); v != seenVersion_) {   // patterns pulled from the synth
        seenVersion_ = v;
        loadPatternControls(); loadStepControls(); repaint();
    }
    static int lastPlaying = -2;
    int playing = proc_.sequencer.playingStep();
    bool isPlaying = proc_.sequencer.isPlaying();
    play_.setButtonText(isPlaying ? "Stop" : "Play");
    play_.setToggleState(isPlaying, juce::dontSendNotification);
    if (playing != lastPlaying) { lastPlaying = playing; repaint(gridBounds().expanded(4)); }
    if (enable_.getToggleState() != proc_.sequencer.enabled) enable_.setToggleState(proc_.sequencer.enabled, juce::dontSendNotification);
    if (pattern_.getSelectedId() != proc_.sequencer.selected + 1) { pattern_.setSelectedId(proc_.sequencer.selected + 1, juce::dontSendNotification); loadPatternControls(); loadStepControls(); repaint(); }

    // step recording
    FM1Processor::NoteEvent ev;
    double now = juce::Time::getMillisecondCounterHiRes();
    bool got = false;
    if (rec_.getToggleState() && proc_.sequencer.isPlaying() != proc_.sequencer.recording.load())
        proc_.sequencer.recording = proc_.sequencer.isPlaying();       // real-time while playing, step recording while stopped
    if (!rec_.getToggleState()) proc_.sequencer.recording = false;
    while (proc_.popNoteOn(ev)) {
        if (!rec_.getToggleState() || proc_.sequencer.isPlaying()) continue;
        if (!recChord_.empty() && now - recLastMs_ > 120.0) {   // a new chord: commit the previous one first
            const juce::SpinLock::ScopedLockType l(proc_.sequencer.lock);
            auto& s = pattern().steps[size_t(selectedStep_)];
            if (!overdub_.getToggleState()) s.notes.clear();
            for (const auto& n : recChord_) s.notes.push_back(n);
            recChord_.clear();
            selectedStep_ = (selectedStep_ + 1) % std::max(1, pattern().length);
        }
        recChord_.push_back({ev.note, ev.vel});
        recLastMs_ = now;
        got = true;
    }
    if (!recChord_.empty() && now - recLastMs_ > 150.0) {
        {
            const juce::SpinLock::ScopedLockType l(proc_.sequencer.lock);
            auto& s = pattern().steps[size_t(selectedStep_)];
            if (!overdub_.getToggleState()) s.notes.clear();
            for (const auto& n : recChord_) s.notes.push_back(n);
            recChord_.clear();
            selectedStep_ = (selectedStep_ + 1) % std::max(1, pattern().length);
        }
        got = true;
    }
    if (got) { loadStepControls(); repaint(); }
}

void SeqPanel::resized() {
    auto r = getLocalBounds().reduced(10);
    auto top = r.removeFromTop(28);
    enable_.setBounds(top.removeFromLeft(60)); top.removeFromLeft(6);
    play_.setBounds(top.removeFromLeft(70)); top.removeFromLeft(6);
    rec_.setBounds(top.removeFromLeft(60)); top.removeFromLeft(12);
    pattern_.setBounds(top.removeFromLeft(120)); top.removeFromLeft(6);
    chainTo_.setBounds(top.removeFromLeft(100)); top.removeFromLeft(12);
    sync_.setBounds(top.removeFromLeft(110)); overdub_.setBounds(top.removeFromLeft(90)); top.removeFromLeft(12);
    pull_.setBounds(top.removeFromLeft(170)); top.removeFromLeft(6);
    push_.setBounds(top.removeFromLeft(170));
    r.removeFromTop(8);
    info_.setBounds(r.removeFromTop(18));
    // grid occupies the left; pattern settings in the middle; step settings on the right
    auto right = getLocalBounds().reduced(10).withTrimmedTop(80).withTrimmedLeft(gridBounds().getRight() + 20);
    auto mid = right.removeFromLeft(330);
    auto rowOf = [](juce::Rectangle<int>& area, juce::Label& label, juce::Component& c) {
        auto rr = area.removeFromTop(26);
        label.setBounds(rr.removeFromLeft(96));
        c.setBounds(rr);
        area.removeFromTop(4);
    };
    rowOf(mid, *labels_[0], length_); rowOf(mid, *labels_[1], tempo_); rowOf(mid, *labels_[2], gate_);
    rowOf(mid, *labels_[3], swing_); rowOf(mid, *labels_[5], transpose_);
    auto rr = mid.removeFromTop(26); labels_[6]->setBounds(rr.removeFromLeft(96)); rate_.setBounds(rr.removeFromLeft(90));
    mid.removeFromTop(10);
    rr = mid.removeFromTop(26); clearPattern_.setBounds(rr.removeFromLeft(120));
    mid.removeFromTop(6);
    rr = mid.removeFromTop(26); importPatterns_.setBounds(rr.removeFromLeft(100)); rr.removeFromLeft(6); exportPatterns_.setBounds(rr.removeFromLeft(100));
    fileStatus_.setBounds(mid.removeFromTop(40));
    fileStatus_.setMinimumHorizontalScale(1.0f);
    right.removeFromLeft(20);
    stepNotes_.setBounds(right.removeFromTop(26)); right.removeFromTop(4);
    {   // the step's notes, under the grid
        auto g = gridBounds();
        noteArea_ = juce::Rectangle<int>(g.getX(), g.getBottom() + 12, g.getWidth(), getHeight() - g.getBottom() - 16);
        layoutNoteRows(noteArea_);
    }
    rr = right.removeFromTop(26); labels_[7]->setBounds(rr.removeFromLeft(96)); ratchet_.setBounds(rr.removeFromLeft(80)); right.removeFromTop(4);
    rowOf(right, *labels_[8], stepGate_); rowOf(right, *labels_[9], stepChance_); rowOf(right, *labels_[10], stepTranspose_);
    rr = right.removeFromTop(26); labels_[11]->setBounds(rr.removeFromLeft(96)); stepRate_.setBounds(rr.removeFromLeft(90)); right.removeFromTop(4);
    rr = right.removeFromTop(26); accent_.setBounds(rr.removeFromLeft(90)); slide_.setBounds(rr.removeFromLeft(90)); right.removeFromTop(8);
    rr = right.removeFromTop(26); clearStep_.setBounds(rr.removeFromLeft(90)); rr.removeFromLeft(6); copyStep_.setBounds(rr.removeFromLeft(90)); rr.removeFromLeft(6); pasteStep_.setBounds(rr.removeFromLeft(90));
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

void SeqPanel::showExportMenu() {
    int current = proc_.sequencer.selected.load();
    juce::PopupMenu m;
    m.addSectionHeader("JSON (every setting)");
    m.addItem(1, "This pattern (" + juce::String(current + 1) + ")...");
    m.addItem(2, "All 16 patterns...");
    m.addSectionHeader("MIDI file (the notes as they play)");
    m.addItem(3, "This pattern (" + juce::String(current + 1) + ") as .mid...");
    m.addItem(4, "All 16 patterns as .mid, one track each...");
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&exportPatterns_), [this, current](int id) {
        if (id == 0) return;
        std::vector<int> pats;
        if (id == 1 || id == 3) pats.push_back(current);
        else for (int i = 0; i < Sequencer::kPatterns; ++i) pats.push_back(i);
        if (id >= 3) {
            juce::String name = id == 3 ? "fm1-pattern-" + juce::String(current + 1) + ".mid" : "fm1-patterns.mid";
            saveAs(chooser_, "Export patterns as a MIDI file", name, [this, pats](const juce::File& f) { return proc_.exportPatternsMidi(f, pats); },
                   [this](const juce::String& msg) { fileStatus_.setText(msg, juce::dontSendNotification); });
            return;
        }
        juce::String name = id == 1 ? "fm1-pattern-" + juce::String(current + 1) + ".json" : "fm1-patterns.json";
        saveAs(chooser_, "Export patterns as JSON", name, [this, pats](const juce::File& f) { return proc_.exportJson(f, {}, pats); },
               [this](const juce::String& msg) { fileStatus_.setText(msg, juce::dontSendNotification); });
    });
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
   #if JUCE_IOS || JUCE_ANDROID
    // only in the app: in a host (AUv3), the host chooses the audio and MIDI devices
    if (juce::StandalonePluginHolder::getInstance() != nullptr) {
        makeLabel(headers_, *this, "Audio and MIDI", 14.0f, true);
        addAndMakeVisible(audioSettings_);
        audioSettings_.setTooltip("The audio output, sample rate, buffer size and the MIDI inputs that play the synth");
        audioSettings_.onClick = [] {
            auto* holder = juce::StandalonePluginHolder::getInstance();
            if (holder == nullptr) return;
            // JUCE's showAudioSettingsDialog asks for a native title bar, which iOS
            // does not draw, leaving no way to close it; this is the same settings with
            // JUCE's own title bar and close button
            int outs = 2;
            if (auto* bus = holder->processor->getBus(false, 0)) outs = std::max(0, bus->getDefaultLayout().size());
            // the selector JUCE's own window shows: outputs, rate, buffer, MIDI inputs (and Bluetooth MIDI)
            auto content = std::make_unique<juce::AudioDeviceSelectorComponent>(holder->deviceManager, 0, 0, 0, outs, true, false, true, false);
            content->setSize(500, 550);
            juce::DialogWindow::LaunchOptions o;
            o.content.setOwned(content.release());
            o.dialogTitle = "Audio/MIDI Settings";
            o.dialogBackgroundColour = o.content->getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId);
            o.escapeKeyTriggersCloseButton = true;
            o.useNativeTitleBar = false;
            o.resizable = false;
            if (auto* window = o.launchAsync())
                // saved now: iOS can end the app without the shutdown that would save it
                juce::ModalComponentManager::getInstance()->attachCallback(window, juce::ModalCallbackFunction::create([](int) {
                    if (auto* h = juce::StandalonePluginHolder::getInstance()) h->saveAudioDeviceState();
                }));
        };
    }
   #endif
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
        synth_.setText("Not read yet. With an FM-1 running FM-1_093 connected, the plugin reads its GLOBE settings and "
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
    if (headers_.size() > 2) {
        r.removeFromTop(28);
        headers_[2]->setBounds(r.removeFromTop(24));
        r.removeFromTop(6);
        audioSettings_.setBounds(r.removeFromTop(28).removeFromLeft(200));
    }
}
