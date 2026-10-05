#include "PluginEditor.h"

FM1Editor::FM1Editor(FM1Processor& p)
    : AudioProcessorEditor(&p), proc_(p), library_(p), fm_(p), fx_(p), seq_(p), arp_(p), settings_(p) {
    setSize(1180, 830);
    auto bg = juce::Colour(0xff26262e);
    // presets and their editors in one window; what is not per preset in its own tab
    library_.setEditorPages(&fm_, &fx_);
    tabs_.addTab("Library", bg, &library_, false);
    tabs_.addTab("Sequencer", bg, &seq_, false);
    tabs_.addTab("Arpeggiator", bg, &arp_, false);
    tabs_.addTab("Settings", bg, &settings_, false);
    addAndMakeVisible(tabs_);
    addAndMakeVisible(keyboard_);
    addAndMakeVisible(library_.connectionBar());
    addAndMakeVisible(firmware_);
    addChildComponent(unsupported_);
    const auto& choices = fm1::firmwareChoices();
    for (size_t i = 0; i < choices.size(); ++i) firmware_.addItem(choices[i].name, int(i) + 1);
    firmware_.setTooltip("The firmware this instance plays and syncs with; saved with the project");
    firmware_.onChange = [this] {
        int i = firmware_.getSelectedItemIndex();
        if (i >= 0) chooseFirmware(fm1::firmwareChoices()[size_t(i)].id);
    };
    unsupported_.setJustificationType(juce::Justification::centred);
    unsupported_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.8f));
    unsupported_.setFont(juce::FontOptions(16.0f));
    proc_.onFirmwareChanged = [this] { showFirmware(); };
    proc_.onFirmwareMismatch = [this](const fm1::Identity& id) { offerSwitch(id); };
    showFirmware();
    if (auto m = proc_.pendingMismatch()) juce::Timer::callAfterDelay(300, [this, id = *m] { offerSwitch(id); });
    keyState_.addListener(this);

    proc_.onStatus = [this](const juce::String& s) { library_.setStatus(s); };
    proc_.bank.onChange = [this] { library_.refresh(); fm_.refreshName(); };
    proc_.session.onIdentity = [this](const fm1::Identity& id) {
        juce::String t = juce::String(id.name()) + "  (" + fm1::firmwareFor(id)->summary() + ")";
        library_.setIdentity(t);
    };
    proc_.onGlobals = [this] { library_.refreshFxChannel(); settings_.showSynth(); };
    proc_.onSettingsChanged = [this] { settings_.refresh(); applyKeyboardVelocity(); };
    applyKeyboardVelocity();
    proc_.bank.onChange();
    // debugging aid: FM1_TAB=n opens the editor on tab n
    tabs_.setComponentID("tabs");
    if (auto tab = juce::SystemStats::getEnvironmentVariable("FM1_TAB", ""); tab.isNotEmpty())
        tabs_.setCurrentTabIndex(juce::jlimit(0, tabs_.getNumTabs() - 1, tab.getIntValue()));
    // debugging aid: FM1_AUTOPULL=1 connects to the FM-1 and pulls every preset
    if (juce::SystemStats::getEnvironmentVariable("FM1_AUTOPULL", "").isNotEmpty())
        juce::Timer::callAfterDelay(500, [this] { if (proc_.link.isOpen() || proc_.autoConnect()) juce::Timer::callAfterDelay(1500, [this] {
            if (juce::SystemStats::getEnvironmentVariable("FM1_AUTOPULL", "") == "patterns") proc_.pullPatterns(); else proc_.pullAll(); }); });
}

FM1Editor::~FM1Editor() {
    keyState_.removeListener(this);
    proc_.onStatus = nullptr;
    proc_.bank.onChange = nullptr;
    proc_.session.onIdentity = nullptr;
    proc_.onGlobals = nullptr;
    proc_.onSettingsChanged = nullptr;
    proc_.onFirmwareChanged = nullptr;
    proc_.onFirmwareMismatch = nullptr;
}

void FM1Editor::showFirmware() {
    const auto& f = fm1::firmwareChoice(proc_.firmwareId().toStdString());
    const auto& choices = fm1::firmwareChoices();
    for (size_t i = 0; i < choices.size(); ++i)
        if (&choices[i] == &f) firmware_.setSelectedItemIndex(int(i), juce::dontSendNotification);
    library_.setFirmware(f);
    const bool isFelucca = juce::String(f.id) == "felucca";
    const bool plays = proc_.emulates();
   #if FM1_FELUCCA
    if (isFelucca && plays) {
        if (!felucca_) { felucca_ = std::make_unique<FeluccaPanel>(proc_); addAndMakeVisible(*felucca_); resized(); }
        else felucca_->refresh();
    } else felucca_.reset();
   #endif
    tabs_.setVisible(!isFelucca && plays);
    unsupported_.setVisible(!plays);
    juce::String why;
    if (isFelucca) {
       #if FM1_FELUCCA
        why = "Every Felucca copy in the plugin is in use by other instances, so this one is silent.\n"
              "Set another instance to a different firmware, or close one, then choose Felucca again.";
       #else
        why = "This build of the plugin does not include Felucca's engines (they need Clang).";
       #endif
    } else {
        why = juce::String(f.name) + " is not in the plugin yet. This instance is silent; choose another firmware above to play.";
    }
    unsupported_.setText(why, juce::dontSendNotification);
}

void FM1Editor::chooseFirmware(const juce::String& id) {
    if (id == proc_.firmwareId()) return;
    auto synth = proc_.session.lastIdentity();
    if (proc_.link.isOpen() && synth && juce::String(fm1::firmwareIdFor(*synth)) != id) {
        // the connected synth runs something else: it will not sync with the new choice
        auto name = fm1::firmwareChoice(id.toStdString()).name;
        auto opts = juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::WarningIcon)
            .withTitle("Switch to " + juce::String(name) + "?")
            .withMessage("The connected FM-1 runs " + fm1::firmwareFor(*synth)->name() + ". It will not sync with an instance set to "
                         + name + ", and the plugin will disconnect from it.")
            .withButton("Switch").withButton("Cancel").withAssociatedComponent(this);
        juce::AlertWindow::showAsync(opts, [this, id](int r) {
            if (r == 1) proc_.setFirmware(id);
            else showFirmware();   // the dropdown back to the current choice
        });
        return;
    }
    proc_.setFirmware(id);
}

void FM1Editor::offerSwitch(const fm1::Identity& synth) {
    proc_.clearPendingMismatch();
    if (askingSwitch_) return;   // one question at a time, however often Find FM-1 is pressed
    askingSwitch_ = true;
    const juce::String theirs = fm1::firmwareIdFor(synth);
    const auto& mine = fm1::firmwareChoice(proc_.firmwareId().toStdString());
    const auto& other = fm1::firmwareChoice(theirs.toStdString());
    auto opts = juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::QuestionIcon)
        .withTitle("The FM-1 runs " + juce::String(other.name))
        .withMessage("This instance is set to " + juce::String(mine.name) + ". Switch it to " + other.name
                     + " to connect? If not, the plugin stays disconnected and syncs nothing.")
        .withButton("Switch to " + juce::String(other.name)).withButton("Keep " + juce::String(mine.name)).withAssociatedComponent(this);
    juce::AlertWindow::showAsync(opts, [this, theirs, mine = juce::String(mine.name)](int r) {
        askingSwitch_ = false;
        if (r == 1) { proc_.setFirmware(theirs); proc_.autoConnect(); }
        else library_.setStatus("Not connected: the FM-1 runs another firmware than this instance (" + mine + "). Find FM-1 to try again.");
    });
}

void FM1Editor::applyKeyboardVelocity() {
    const auto& s = proc_.settings();
    keyboard_.setVelocity(s.fixedVelocity ? float(s.velocity) / 127.0f : 1.0f, !s.fixedVelocity);
}

void FM1Editor::handleNoteOn(juce::MidiKeyboardState*, int ch, int note, float vel) {
   #if FM1_FELUCCA
    // Felucca: the keys play the selected part (as its own keys do), on that part's channel;
    // the note-off goes where its note-on went
    if (auto f = proc_.felucca()) ch = f->selected() + 1;
    noteChannel_[size_t(note & 127)] = ch;
   #endif
    auto m = juce::MidiMessage::noteOn(ch, note, vel);
    m.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
    proc_.keyboardMidi.addMessageToQueue(m);
}

void FM1Editor::handleNoteOff(juce::MidiKeyboardState*, int ch, int note, float vel) {
   #if FM1_FELUCCA
    if (noteChannel_[size_t(note & 127)] > 0) ch = noteChannel_[size_t(note & 127)];
    noteChannel_[size_t(note & 127)] = 0;
   #endif
    auto m = juce::MidiMessage::noteOff(ch, note, vel);
    m.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
    proc_.keyboardMidi.addMessageToQueue(m);
}

void FM1Editor::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff1e1e24)); }

void FM1Editor::resized() {
    auto r = getLocalBounds().reduced(8);
    auto top = r.removeFromTop(28);
    firmware_.setBounds(top.removeFromLeft(190));
    top.removeFromLeft(10);
    library_.connectionBar().setBounds(top);
    r.removeFromTop(6);
    keyboard_.setBounds(r.removeFromBottom(64));
    r.removeFromBottom(6);
    tabs_.setBounds(r);
    unsupported_.setBounds(r);
   #if FM1_FELUCCA
    if (felucca_) felucca_->setBounds(r);
   #endif
}
