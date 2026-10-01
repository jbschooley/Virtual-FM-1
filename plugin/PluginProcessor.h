#pragma once

#include <array>
#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Arpeggiator.h"
#include "BankModel.h"
#include "Effects.h"
#include "Fm1Link.h"
#include "Fm1Session.h"
#include "FmSynth.h"
#include "Params.h"
#include "Sequencer.h"

class FM1Processor : public juce::AudioProcessor, private juce::Timer {
public:
    FM1Processor();
    ~FM1Processor() override;

    // ---- AudioProcessor ------------------------------------------------------
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return BankModel::kSlots; }
    int getCurrentProgram() override { return bank.currentSlot(); }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // ---- model -------------------------------------------------------------------
    juce::AudioProcessorValueTreeState apvts;
    Params params;
    BankModel bank;
    Fm1Link link;
    Fm1Session session;
    Sequencer sequencer;
    Arpeggiator arp;

    // The editor is an edit buffer over the current slot, like the FM-1's own:
    // changes stay here until store() writes them into the slot.
    fm1::Sound& commitCurrent();                  // same as store(), returns the slot's sound
    void store();
    void revert();                                // reload the slot, dropping edits
    bool isEdited() const;
    fm1::Sound editedSound() const;               // the slot with the editor's changes applied
    juce::String editName() const { return editName_; }
    void setCurrentName(const juce::String& name);   // an edit, like any other setting
    void setCurrentSound(const fm1::Sound& s);   // replace the current slot's sound and load it
    void selectSlot(int slot);                    // switch and load; unsaved edits are dropped

    // The synth side: send the editor's sound to the FM-1's edit buffer without
    // saving, and optionally keep sending every change (Live).
    void sendToFm1EditBuffer();
    void setLive(bool on);
    bool isLive() const { return live_; }
    fm1::edit::Channels channels;                 // FX channel must match the synth's GLOBE setting

    bool connect(const juce::String& inputId, const juce::String& outputId);
    bool autoConnect();
    void disconnect();

    void pullCurrent();
    void pushCurrent();
    void pullAll();
    void pushChanged();
    void pushAll();
    void selectOnDevice();
    void pullPatterns();
    void pushPatterns(bool save);

    juce::String importSyx(const juce::File& f);
    bool exportSyx(const juce::File& f);

    std::function<void(const juce::String&)> onStatus;   // message thread
    juce::MidiMessageCollector keyboardMidi;             // notes from the editor's keyboard

    // Operator on/off switches from the editor (index 0 = OP6 .. 5 = OP1); not saved with the preset.
    std::array<std::atomic<bool>, 6> opEnabled{true, true, true, true, true, true};

    // Note-ons the plugin received, for step recording in the editor (popped on the message thread).
    struct NoteEvent { int note, vel; };
    bool popNoteOn(NoteEvent& e);

private:
    void loadCurrentIntoParams();
    void timerCallback() override;               // live sending
    void applyEditName();
    juce::String editName_;
    bool live_ = false;
    fm1::Sound lastSent_;
    bool haveLastSent_ = false;
    void handleDx7Sysex(const uint8_t* data, int size);
    void applyParamsToEngine();
    void pushNoteOn(int note, int vel);

    FmSynth synth_;
    Effects fx_;
    std::array<uint8_t, fm1::kEditBytes> vced_{};   // the engine's patch, name bytes from the slot
    juce::SpinLock nameLock_;
    std::atomic<int> pendingProgram_{-1};
    int loadedSlot_ = -1;

    juce::AbstractFifo noteFifo_{64};
    std::array<NoteEvent, 64> noteFifoData_{};

    juce::MidiBuffer generated_, synthEvents_;
    juce::AudioBuffer<float> mono_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FM1Processor)
};
