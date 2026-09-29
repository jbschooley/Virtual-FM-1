#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "Panels.h"
#include "PluginProcessor.h"

class FM1Editor : public juce::AudioProcessorEditor,
                  private juce::MidiKeyboardState::Listener {
public:
    explicit FM1Editor(FM1Processor&);
    ~FM1Editor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void handleNoteOn(juce::MidiKeyboardState*, int ch, int note, float vel) override;
    void handleNoteOff(juce::MidiKeyboardState*, int ch, int note, float vel) override;

    FM1Processor& proc_;
    juce::TabbedComponent tabs_{juce::TabbedButtonBar::TabsAtTop};
    LibraryPanel library_;
    FmEditorPanel fm_;
    FxPanel fx_;
    SeqPanel seq_;
    ArpPanel arp_;
    juce::MidiKeyboardState keyState_;
    juce::MidiKeyboardComponent keyboard_{keyState_, juce::MidiKeyboardComponent::horizontalKeyboard};
};
