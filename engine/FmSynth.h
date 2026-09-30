// FmSynth -- a 12-voice DX7-style synth on the msfa engine, as the FM-1 runs it.
//
// Takes the 155-byte DX7 edit buffer (VCED) as its patch, like the firmware.
// Renders mono float; the host layer copies to both channels.

#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include "msfa/controllers.h"
#include "msfa/dx7note.h"
#include "msfa/fm_core.h"
#include "msfa/lfo.h"
#include "msfa/tuning.h"

class FmSynth {
public:
    static constexpr int kVoices = 12;
    static constexpr int kPatchBytes = 155;

    FmSynth();
    ~FmSynth();

    void prepare(double sampleRate);
    void reset();

    // Copies the patch; live voices pick up the change (msfa's update path).
    void setPatch(const uint8_t* vced155);
    const uint8_t* patch() const { return patch_.data(); }

    void noteOn(int midiNote, int velocity);
    void noteOff(int midiNote);
    void allNotesOff();
    void allSoundOff();

    void setPitchBend(int value14);     // 0..16383, 8192 = centre
    void setPitchBendRange(int up, int down);
    void setModWheel(int v);
    void setBreath(int v);
    void setFoot(int v);
    void setAftertouch(int v);
    void setSustain(bool down);
    void setMasterTune(int cents);     // -100..100 (msfa scale)

    // The FM-1's global envelope: a per-voice A D S R on top of the operator
    // envelopes (all 0..100). Off = the operator envelopes alone shape the note.
    void setEnvelope(bool on, int a, int d, int s, int r);

    // Operator on/off (not part of the patch). `vcedIndex` 0 = OP6 .. 5 = OP1, as msfa's opSwitch.
    void setOperatorEnabled(int vcedIndex, bool on);

    int activeVoices() const;

    // Mono output, added is false: overwrites `out`.
    void render(float* out, int numSamples);

private:
    struct VoiceSlot {
        std::unique_ptr<Dx7Note> note;
        int midiNote = -1;       // the key that started the voice (for note-off)
        int playedNote = -1;     // after the patch's transpose
        int velocity = 0;
        bool keydown = false;
        bool sustained = false;
        bool live = false;
        uint32_t keydownSeq = 0;
        // global envelope state
        int envStage = 0;        // 0 idle, 1 attack, 2 decay, 3 sustain, 4 release
        float envLevel = 0.0f;
    };

    void renderBlock();  // fills block_ with N samples

    std::shared_ptr<TuningState> tuning_;
    std::array<uint8_t, 156> patch_{};
    std::array<VoiceSlot, kVoices> voices_;
    Lfo lfo_;
    Controllers controllers_;
    FmCore core_;
    bool sustain_ = false;
    uint32_t seq_ = 0;
    double sampleRate_ = 44100.0;

    bool envOn_ = false;
    float envAttackInc_ = 1.0f, envDecayCoef_ = 0.0f, envSustain_ = 1.0f, envReleaseCoef_ = 0.0f;
    void envKeyDown(VoiceSlot& v);
    void envKeyUp(VoiceSlot& v);
    void updateEnvCoefs();
    int envA_ = 0, envD_ = 0, envS_ = 100, envR_ = 0;

    // msfa renders 64-sample blocks; keep the unconsumed tail between calls.
    std::array<float, 64> block_{};
    int blockPos_ = 64;
};
