#include "PluginEditor.h"

#if JUCE_IOS || JUCE_ANDROID
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif
#if JUCE_IOS
std::optional<juce::BorderSize<int>> fm1SafeArea(juce::Component&);   // SafeArea_ios.mm
#endif

FM1Editor::FM1Editor(FM1Processor& p)
    : AudioProcessorEditor(&p), proc_(p), library_(p), fm_(p), fx_(p), seq_(p), arp_(p), settings_(p) {
    setLookAndFeel(&lnf_);
    setSize(1180, 830);
    auto bg = juce::Colour(0xff26262e);
    // presets and their editors in one window; what is not per preset in its own tab
    library_.setEditorPages(&fm_, &fx_);
    tabs_.addTab("Library", bg, &library_, false);
    tabs_.addTab("Sequencer", bg, &seqPage_, false);
    seq_.onHeightChanged = [this] { seqPage_.resized(); };
    tabs_.addTab("Arpeggiator", bg, &arp_, false);
    tabs_.addTab("Settings", bg, &settings_, false);
    addAndMakeVisible(tabs_);
    addAndMakeVisible(keyboard_);
    addAndMakeVisible(library_.connectionBar());
    addAndMakeVisible(firmware_);
   #if JUCE_IOS || JUCE_ANDROID
    if (juce::StandalonePluginHolder::getInstance() != nullptr) {
        addAndMakeVisible(audioSettings_);
        audioSettings_.setTooltip("The audio output, sample rate, buffer size and the MIDI inputs that play the synth");
        audioSettings_.onClick = [this] { showAudioSettings(); };
    }
   #endif
    addChildComponent(unsupported_);
    const auto& choices = fm1::firmwareChoices();
    for (size_t i = 0; i < choices.size(); ++i) firmware_.addItem(choices[i].label(), int(i) + 1);
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
    if (auto said = proc_.takePendingStatus(); said.isNotEmpty()) library_.setStatus(said);   // (said before the editor opened)
    proc_.bank.onChange = [this] { library_.refresh(); fm_.refreshName(); };
    proc_.session.onIdentity = [this](const fm1::Identity& id) {
        // the release, against the ones the plugin knows: a newer, older or retired one says so
        // (in the identity line, which stays: the status line is the session's next report)
        const auto check = fm1::checkVersion(id);
        const bool say = check.newer || check.known == nullptr || check.support == fm1::Support::Older || check.support == fm1::Support::Deprecated
                         || (check.known != nullptr && *check.known->note);   // (a tested beta says so)
        library_.setIdentity(say ? juce::String(check.text) : juce::String(id.name()) + "  (" + fm1::firmwareFor(id)->summary() + ")");
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
    seq_.onHeightChanged = nullptr;
    setLookAndFeel(nullptr);
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
        why = "Felucca could not start in this instance, so it is silent.\n"
              "Choose another firmware and then Felucca again.";
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
    if (!fm1::isFirmwareChoice(theirs.toStdString())) {   // a firmware no instance can play (Sloop): nothing to switch to
        const auto v = fm1::checkVersion(synth);
        auto info = juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::InfoIcon)
            .withTitle("The FM-1 runs " + fm1::firmwareFor(synth)->name())
            .withMessage(juce::String(v.text) + ". The plugin stays disconnected from it.")
            .withButton("OK").withAssociatedComponent(this);
        juce::AlertWindow::showAsync(info, [this](int) { askingSwitch_ = false; });
        library_.setStatus("Not connected: the FM-1 runs " + fm1::firmwareFor(synth)->name() + ", which the plugin does not support yet.");
        return;
    }
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

void FM1Editor::showAudioSettings() {
   #if JUCE_IOS || JUCE_ANDROID
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
   #endif
}

void FM1Editor::resized() {
    auto r = getLocalBounds();
   #if JUCE_IOS
    // full screen on a phone or tablet: clear of the notch or Dynamic Island, the rounded
    // corners and the home indicator
    // (the app only: an AUv3's host keeps its view clear itself)
    auto* window = getTopLevelComponent();
    if (juce::JUCEApplicationBase::isStandaloneApp() && window != nullptr && window->getBounds() == getScreenBounds()) {
        const auto safe = fm1SafeArea(*window);
        if (safe) r = safe->subtractedFrom(r);
        if ((!safe || safe->getTop() + safe->getBottom() == 0) && safeRetries_++ < 50)   // not in its window (or laid out) yet
            juce::Timer::callAfterDelay(100, [s = juce::Component::SafePointer<FM1Editor>(this)] { if (s) s->resized(); });
    }
   #endif
    r = r.reduced(8);
    auto top = r.removeFromTop(28);
    const bool narrow = getWidth() < 1040;   // a phone or an iPad upright: the FM-1 connection on a line of its own
    if (audioSettings_.isVisible()) { audioSettings_.setBounds(top.removeFromRight(narrow ? 90 : 100)); top.removeFromRight(narrow ? 4 : 8); }
    if (narrow) {
        firmware_.setBounds(top);
        r.removeFromTop(6);
        library_.connectionBar().setBounds(r.removeFromTop(28));
    } else {
        firmware_.setBounds(top.removeFromLeft(260));   // wide enough for "FM-1+VA (baud girl) 0.93"
        top.removeFromLeft(10);
        library_.connectionBar().setBounds(top);
    }
    r.removeFromTop(6);
    keyboard_.setBounds(r.removeFromBottom(64));
    r.removeFromBottom(6);
    tabs_.setBounds(r);
    unsupported_.setBounds(r);
   #if FM1_FELUCCA
    if (felucca_) felucca_->setBounds(r);
   #endif
}
