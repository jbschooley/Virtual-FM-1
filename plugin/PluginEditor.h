#pragma once

#include <array>

#include <juce_audio_utils/juce_audio_utils.h>

#include "Panels.h"
#include "FeluccaPanel.h"
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

    void showFirmware();                                  // the editor for the instance's firmware
    void chooseFirmware(const juce::String& id);         // from the dropdown
    void offerSwitch(const fm1::Identity& synth);        // a synth with another firmware connected

    FM1Processor& proc_;
    DropDownLists<juce::LookAndFeel_V4> lnf_;   // (declared before the components that use it)
    juce::TooltipWindow tooltips_{this, 600};
    bool askingSwitch_ = false;
    juce::ComboBox firmware_;
    juce::TextButton audioSettings_{"Audio/MIDI"};   // the app only (in a host, the host chooses): for every firmware
    void showAudioSettings();
    juce::Label unsupported_;
   #if FM1_FELUCCA
    std::unique_ptr<FeluccaPanel> felucca_;   // while set to Felucca
   #endif
    juce::TabbedComponent tabs_{juce::TabbedButtonBar::TabsAtTop};
    LibraryPanel library_;
    FmEditorPanel fm_;
    FxPanel fx_;
    SeqPanel seq_;
    ArpPanel arp_;
    SettingsPanel settings_;
    void applyKeyboardVelocity();
    juce::MidiKeyboardState keyState_;
    std::array<int, 128> noteChannel_{};   // the channel each held key's note-on went to (Felucca: the selected part's)
    juce::MidiKeyboardComponent keyboard_{keyState_, juce::MidiKeyboardComponent::horizontalKeyboard};
};
