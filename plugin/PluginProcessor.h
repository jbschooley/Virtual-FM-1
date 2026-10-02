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
#include "Settings.h"

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
    std::atomic<int> patternsVersion{0};          // bumped when patterns arrive from the synth
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

    // The FM-1's GLOBE settings, once read from it (FM-1_093). Reading them sets
    // the MIDI and FX channels used to talk to it; copyGlobalsToSettings() takes
    // the playing settings (bend range, key velocity) on request.
    std::optional<Fm1Session::Globals> synthGlobals() const { return globals_; }
    std::function<void()> onGlobals;              // message thread

    // How this instance plays (message thread). A change takes effect at once and
    // is saved with the project; new instances start from the shared defaults.
    const PluginSettings& settings() const { return settings_; }
    void setSettings(const PluginSettings& s);
    bool saveSettingsAsDefault();                 // this instance's settings become the defaults
    void revertSettingsToDefault();
    static PluginSettings defaultSettings();      // the shared file, or the factory values
    static juce::File defaultSettingsFile();
    // Read the FM-1's GLOBE settings again; with `thenCopy`, take its bend range
    // and key velocity into the settings once they arrive. False when not connected or busy.
    bool readSynthSettings(bool thenCopy = false);
    std::function<void()> onSettingsChanged;      // message thread

    bool connect(const juce::String& inputId, const juce::String& outputId, bool quiet = false);
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
    bool exportSyx(const juce::File& f, const std::vector<int>& slots = {});   // empty: all 128

    // JSON (docs/JSON-FORMAT.md): any mix of presets and patterns. Export writes
    // the given slots (0..127; the current one with its unsaved edits) and
    // patterns (0..15). Import applies the presets and/or patterns in the file.
    // Presets go to their own slots (those without one fill the free slots from
    // the selected one on, skipping slots the file uses) or, FromSelected, all
    // in file order from the selected slot on. Patterns go to their numbers.
    bool exportJson(const juce::File& f, const std::vector<int>& slots, const std::vector<int>& patterns);
    struct JsonPreview { int presets = 0, patterns = 0; std::vector<int> slots; juce::StringArray errors; };   // slots: those given
    JsonPreview previewJson(const juce::File& f) const;
    enum class Placement { OwnSlots, FromSelected };
    struct ImportResult { bool ok = false; juce::String summary; juce::StringArray errors; };
    ImportResult importJson(const juce::File& f, bool presets, bool patterns, Placement placement = Placement::OwnSlots);

    std::function<void(const juce::String&)> onStatus;   // message thread
    juce::MidiMessageCollector keyboardMidi;             // notes from the editor's keyboard

    // Operator on/off switches from the editor (index 0 = OP6 .. 5 = OP1); not saved with the preset.
    std::array<std::atomic<bool>, 6> opEnabled{true, true, true, true, true, true};

    // Note-ons the plugin received, for step recording in the editor (popped on the message thread).
    struct NoteEvent { int note, vel; };
    bool popNoteOn(NoteEvent& e);

private:
    std::optional<Fm1Session::Globals> globals_;
    PluginSettings settings_;
    std::atomic<int> bendUp_{12}, bendDown_{12};   // semitones, for the audio thread
    std::atomic<int> inputChannel_{0};             // 0 = every channel
    std::atomic<bool> settingsNotify_{false};
    bool copyPending_ = false;
    void copyGlobalsToSettings();
    void loadCurrentIntoParams();
    void writeDiagnostics();

    // Background work on the message thread every second: connect to an FM-1 when
    // one appears, notice when it goes away, and keep the shared library in step
    // with the other instances (the Standalone and every plugin in every host).
    struct Background : juce::Timer {
        FM1Processor& p;
        explicit Background(FM1Processor& owner) : p(owner) {}
        void timerCallback() override { p.backgroundTick(); }
    };
    void backgroundTick();
    static juce::File libraryFile();
    bool loadLibrary();          // false when there is no shared library yet
    void saveLibrary();
    bool libraryDirty_ = false;
    juce::Time libraryLoadedTime_;
    bool autoConnect_ = true;    // off after the user disconnects by hand
    Background background_{*this};
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

    juce::MidiBuffer generated_, synthEvents_, filtered_;
    juce::AudioBuffer<float> mono_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FM1Processor)
};
