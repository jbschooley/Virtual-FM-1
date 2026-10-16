// FM1 Open's editor: its front panel (Device) and syncing with an FM-1 running it (FM-1): the project
// it plays, a track's sound, its 16 user slots and its sample bank, both ways (FM1Processor::fm1open*).
// No Live: FM1 Open has no message for one value changed.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "FeluccaPanel.h"

class FM1Processor;

class Fm1OpenSyncPage : public juce::Component, private juce::Timer {
public:
    explicit Fm1OpenSyncPage(FM1Processor&);
    ~Fm1OpenSyncPage() override;
    void resized() override;
    void paint(juce::Graphics&) override;
    void visibilityChanged() override;

private:
    void timerCallback() override { update(); }
    void update();
    void showSlots();
    FM1Processor& proc_;
    juce::Label about_, projectHead_, soundHead_, slotsHead_, samplesHead_;
    juce::TextButton pull_{"Pull from FM-1"}, send_{"Send to FM-1"}, sendSave_{"Send and save in slot"};
    juce::ComboBox saveSlot_, track_;
    juce::TextButton soundFrom_{"Sound from FM-1"}, soundTo_{"Sound to FM-1"};
    juce::TextButton listSlots_{"Read the slots"}, loadSlot_{"Load on FM-1 and pull"};
    juce::ComboBox slot_;
    juce::TextButton samplesFrom_{"Pull sample bank"}, samplesTo_{"Send sample bank"};
};

class Fm1OpenPanel : public juce::Component {
public:
    explicit Fm1OpenPanel(FM1Processor&);
    void resized() override;
private:
    juce::TabbedComponent tabs_{juce::TabbedButtonBar::TabsAtTop};
};
