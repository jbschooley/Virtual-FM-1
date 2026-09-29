#include "PluginEditor.h"

FM1Editor::FM1Editor(FM1Processor& p)
    : AudioProcessorEditor(&p), proc_(p), library_(p), fm_(p), fx_(p), seq_(p), arp_(p) {
    setSize(1100, 720);
    auto bg = juce::Colour(0xff26262e);
    tabs_.addTab("Library & Sync", bg, &library_, false);
    tabs_.addTab("FM Editor", bg, &fm_, false);
    tabs_.addTab("Effects & Envelope", bg, &fx_, false);
    tabs_.addTab("Sequencer", bg, &seq_, false);
    tabs_.addTab("Arpeggiator", bg, &arp_, false);
    addAndMakeVisible(tabs_);
    addAndMakeVisible(keyboard_);
    keyState_.addListener(this);

    proc_.onStatus = [this](const juce::String& s) { library_.setStatus(s); };
    proc_.bank.onChange = [this] { library_.refresh(); fm_.refreshName(); };
    proc_.session.onIdentity = [this](const fm1::Identity& id) {
        juce::String t = juce::String(id.name());
        t += id.isStock() ? "  (M-VAVE firmware: DX7 dumps only, no read-back)" : "  (FM-1+VA: full two-way sync)";
        library_.setIdentity(t);
    };
    proc_.bank.onChange();
    // debugging aid: FM1_TAB=n opens the editor on tab n
    if (auto tab = juce::SystemStats::getEnvironmentVariable("FM1_TAB", ""); tab.isNotEmpty())
        tabs_.setCurrentTabIndex(juce::jlimit(0, tabs_.getNumTabs() - 1, tab.getIntValue()));
}

FM1Editor::~FM1Editor() {
    keyState_.removeListener(this);
    proc_.onStatus = nullptr;
    proc_.bank.onChange = nullptr;
    proc_.session.onIdentity = nullptr;
}

void FM1Editor::handleNoteOn(juce::MidiKeyboardState*, int ch, int note, float vel) {
    auto m = juce::MidiMessage::noteOn(ch, note, vel);
    m.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
    proc_.keyboardMidi.addMessageToQueue(m);
}

void FM1Editor::handleNoteOff(juce::MidiKeyboardState*, int ch, int note, float vel) {
    auto m = juce::MidiMessage::noteOff(ch, note, vel);
    m.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
    proc_.keyboardMidi.addMessageToQueue(m);
}

void FM1Editor::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff1e1e24)); }

void FM1Editor::resized() {
    auto r = getLocalBounds().reduced(8);
    keyboard_.setBounds(r.removeFromBottom(64));
    r.removeFromBottom(6);
    tabs_.setBounds(r);
}
