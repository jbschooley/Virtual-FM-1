// Params -- every sound setting as a host parameter, bound to its byte in the
// DX7 edit buffer (VCED) or in the FM-1's settings record. The parameters are
// the source of truth for the sound being edited; the bank slot is written
// from them (commit) when the sound is saved, pushed or switched away from,
// and they are loaded from the slot when a preset is selected.

#pragma once

#include <atomic>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Fm1Codec.h"
#include "Fm1Record.h"

class Params : private juce::AudioProcessorValueTreeState::Listener {
public:
    enum class Kind { Vced, FxParam, FxOn, FxType, Env, EnvOn, Filter };

    struct Binding {
        juce::String id;
        Kind kind;
        int a = 0, b = 0;   // Vced: offset; Fx*: effect id, param index; Env: 0..3
        juce::RangedAudioParameter* param = nullptr;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    explicit Params(juce::AudioProcessorValueTreeState& apvts);
    ~Params() override;

    // Parameter ids
    static juce::String vcedId(int offset);                 // "v<offset>"
    static juce::String fxParamId(int fx, int i);           // "fx<fx>p<i>"
    static juce::String fxOnId(int fx);                     // "fx<fx>on"
    static juce::String fxTypeId(int fx);                   // "fx<fx>type"
    static juce::String envId(int i);                       // "env0".."env3"
    static constexpr const char* kEnvOn = "envon";
    // Filter fields: 0 on, 1 type, 2 key tracking, then record bytes 23 cutoff, 24 resonance,
    // 25 envelope, 51 decay, 49 shape, 47 velocity, 50 LFO to cutoff.
    static juce::String filterId(int field);   // "flt0".."flt9"

    juce::AudioProcessorValueTreeState& apvts;
    const std::vector<Binding>& bindings() const { return bindings_; }

    // Load the parameters from a sound (message thread). Does not fire `changed`.
    void load(const fm1::Sound& s);
    // Write the parameters into a sound's voice and record. Only settings whose
    // value differs from what load() (or the previous commit) saw are written,
    // so bytes the editor does not understand, such as the firmware's "unset"
    // markers, survive a load/commit round trip untouched.
    void commit(fm1::Sound& s);

    // True when any setting differs from what load() (or the last commit) saw.
    bool isEdited() const;
    std::vector<int> snapshot() const { return committed_; }
    void restoreSnapshot(const std::vector<int>& v) { committed_ = v; }
    // Set the parameters to another version of the loaded sound (the synth's live
    // edit buffer, say) without moving the baseline, so the differences count as edits.
    void applyEdit(const fm1::Sound& s);

    // For the audio thread: fill a VCED from the parameters (name bytes left as given).
    void fillVced(uint8_t* vced155) const;
    fm1::FxChain fxChain(const fm1::FxChain& orderFrom) const;   // keeps `orderFrom`'s order
    fm1::Envelope envelope() const;
    fm1::VaFilter filter() const;

    // Set by any parameter change (any thread); cleared by whoever consumes it.
    std::atomic<bool> changed{false};

private:
    void parameterChanged(const juce::String&, float) override;
    std::vector<int> valuesFrom(const fm1::Sound& s) const;   // per binding, clamped to its range
    std::vector<Binding> bindings_;
    std::vector<int> committed_;   // per binding: the value last loaded or committed
    bool loading_ = false;
};
