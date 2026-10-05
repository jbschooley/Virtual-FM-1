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
#include "HardwareCharacter.h"
#include "RateConverter.h"
#include "Firmwares.h"
#include "LibraryStore.h"
#if FM1_FELUCCA
 #include "FeluccaEngine.h"
 #include "FeluccaDevice.h"
#endif
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
    std::shared_ptr<felparams::TextSource> felText_ = std::make_shared<felparams::TextSource>();   // before apvts
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
    void initCurrent();                               // a blank FM sound, as an unsaved edit
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

    // The firmware this instance is set to (firmwares/Firmwares.h): what the editor
    // shows, and the only firmware it syncs with. Saved with the project.
    juce::String firmwareId() const;
    void setFirmware(const juce::String& id);           // message thread
    bool emulates() const;                               // false: the plugin cannot play this firmware yet
   #if FM1_FELUCCA
    // While set to Felucca (null if no copy was free). Shared: a caller holding it keeps the
    // engine alive while a switch away (perhaps on another thread) lets go of it.
    std::shared_ptr<FeluccaEngine> felucca() const { return std::atomic_load(&felucca_); }
    // Felucca's sound changed other than by automation (its editor, a preset, a project):
    // the host's parameters follow (track -1: all). Message thread.
    void feluccaChanged(int track = -1);
    // The host parameter of Felucca's parameter (track -1: a global), or null: for change gestures.
    juce::RangedAudioParameter* feluccaParam(int track, int index) const {
        const int e = felparams::entryFor(track, index);
        return e >= 0 ? felParams_[size_t(e)] : nullptr;
    }
   #endif
    std::function<void()> onFirmwareChanged;            // message thread
    // A synth connected that runs another firmware: the connection was closed
    // before anything was read. The editor offers to switch (message thread).
    std::function<void(const fm1::Identity&)> onFirmwareMismatch;
    std::optional<fm1::Identity> pendingMismatch() const { return pendingMismatch_; }
    void clearPendingMismatch() { pendingMismatch_.reset(); }

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

    // Standard MIDI files (plugin/SeqMidi.h): export patterns (0..15) as the
    // sequencer plays them; import a file's notes into the selected pattern.
    bool exportPatternsMidi(const juce::File& f, const std::vector<int>& patterns);
    juce::String importPatternMidi(const juce::File& f);

    std::function<void(const juce::String&)> onStatus;   // message thread
    juce::MidiMessageCollector keyboardMidi;             // notes from the editor's keyboard

    // Operator on/off switches from the editor (index 0 = OP6 .. 5 = OP1); not saved with the preset.
    std::array<std::atomic<bool>, 6> opEnabled{true, true, true, true, true, true};

    // Note-ons the plugin received, for step recording in the editor (popped on the message thread).
    struct NoteEvent { int note, vel; };
    bool popNoteOn(NoteEvent& e);

private:
    std::atomic<int> firmwareIndex_{1};               // into fm1::firmwareChoices()
   #if FM1_FELUCCA
    std::shared_ptr<FeluccaEngine> felucca_;          // swapped with the audio callback held off
    juce::ValueTree feluccaSaved_;                    // Felucca's state while it has no engine: kept, saved
    std::vector<float> felApplied_;                   // audio thread: the host values last given to Felucca
    std::atomic<bool> felResync_{true};               // next block: take the host's values as given, apply none
    bool felHostPlaying_ = false;                     // audio thread: the host's transport, last block
    felucca::DeviceStore felDevice_;                  // the device's stored objects, in the library
    std::mutex felDeviceLock_;
    RateConverter felL_, felR_;                       // its 44.1 kHz to the host's rate
    juce::AudioBuffer<float> felBuf_;
    bool felConvert_ = false;
    void prepareFelucca();
    void renderFelucca(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, const juce::AudioPlayHead::PositionInfo* pos);
    juce::ValueTree feluccaState() const;
    void setFeluccaState(const juce::ValueTree& t);
    static void applyFeluccaState(FeluccaEngine& f, const juce::ValueTree& t);
    void applyHostToFelucca(FeluccaEngine& f);       // audio thread: automation into Felucca
    void status(const juce::String& text);           // onStatus, on the message thread
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    std::vector<juce::RangedAudioParameter*> felParams_;   // felparams::entries(), in order
   #endif
    std::optional<fm1::Identity> pendingMismatch_;
    std::optional<Fm1Session::Globals> globals_;
    PluginSettings settings_;
    std::atomic<int> bendUp_{12}, bendDown_{12};   // semitones, for the audio thread
    std::atomic<int> inputChannel_{0};             // 0 = every channel
    std::atomic<bool> hardwareCharacter_{false};
    HardwareCharacter hwChar_;                     // audio thread
    bool hwCharWasOn_ = false;                     // audio thread
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
    static juce::File libraryFile();     // the single file earlier versions kept (moved into store_ once)
    LibraryStore store_;
    bool loadLibrary();          // false when there is no shared library yet
    void saveLibrary();
    std::atomic<bool> libraryDirty_{false};
    void reportLibrary();
    juce::String lastLibraryReport_;
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
    // What the editor's parameters edit: the current slot, or the project's own copy of it
    // when the library's has changed since the project was saved (until stored or reverted).
    const fm1::Sound& base() const { return projectBase_ ? *projectBase_ : bank.current(); }
    std::optional<fm1::Sound> projectBase_;        // changed under nameLock_
    fm1::Record baseRecord_{};                     // the base's record for the audio thread
    void prepareEngine();
    void renderEngine(float* out, int numSamples, const juce::MidiBuffer& events);

    static constexpr double kFm1Rate = 44100.0;    // the FM-1's own sample rate
    double hostRate_ = 44100.0;
    int hostBlock_ = 512;
    bool prepared_ = false;
    bool resampling_ = false;                      // engine at 44.1 kHz, converted (Hardware character)
    RateConverter toHost_;
    juce::AudioBuffer<float> engineBuf_;           // the engine's output at its own rate
    juce::MidiBuffer engineEvents_;

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
