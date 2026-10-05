// FeluccaEngine -- Felucca's synth (engines/felucca/felucca_core.c) for one plugin
// instance: one of the compiled copies, taken from a pool while the instance plays
// Felucca and given back after. No JUCE here.
//
// Felucca runs at 44.1 kHz in blocks of 32 samples (its control rate); render()
// takes any number of frames and keeps the rest of a block for next time. MIDI goes
// in as USB-MIDI packets, as the FM-1's USB port delivers them; Felucca reads them
// at the start of each 32-sample block, as on the device.
//
// Thread safety, as Felucca's own UI works with its audio interrupt: render() and midi()
// are for the audio thread. Reading (param, paramDesc, engineOf ...) and setting a
// single value (setParam, setGlobal) work from any thread without a lock: a value is
// one 16-bit store, read by the audio side at its next block. Changes that rewrite
// many values at once (setEngine, applyPreset, reset) take the lock the audio thread
// holds while it renders, so it never sees half of one; they are short and rare.
#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

extern "C" {
#include "felucca_core.h"
}

struct FeluccaCopy {
#define FEL_MEMBER(ret, name, params) ret(*name) params;
    FELUCCA_API(FEL_MEMBER)
#undef FEL_MEMBER
};

class FeluccaEngine {
public:
    static int copies();                       // how many instances can play Felucca at once
    static int copiesInUse();

    FeluccaEngine();                           // takes a free copy, if there is one
    ~FeluccaEngine();
    FeluccaEngine(const FeluccaEngine&) = delete;
    FeluccaEngine& operator=(const FeluccaEngine&) = delete;

    bool valid() const { return core_ != nullptr; }
    static constexpr double kRate = 44100.0;   // FS: Felucca renders at the FM-1's rate only

    // ---- audio thread ----
    void midi(const uint8_t* bytes, int size);   // a channel message (notes, CCs ...)
    void render(float* left, float* right, int frames);

    // ---- any thread ----
    struct Desc {
        std::string label, unit;
        int fmt = 0, min = 0, max = 0, def = 0;
        std::vector<std::string> names;        // for a list (fmt 8)
    };
    int tracks() const;                        // 4: three parts and the drum track
    int parts() const;                         // 3
    int engines() const;
    int paramCount() const;                    // per track (57)
    int firstEngineParam() const;              // P_E0: the engine's eight follow
    int globalCount() const;
    std::string engineName(int e) const;
    std::string enginePage(int e, int page) const;   // the titles of its two edit pages
    std::vector<std::string> presetNames(int e) const;
    Desc paramDesc(int track, int id) const;   // engine parameters are the track's engine's
    Desc globalDesc(int id) const;
    int param(int track, int id) const;
    void setParam(int track, int id, int value);
    int global(int id) const;
    void setGlobal(int id, int value);
    int engineOf(int track) const;
    int presetOf(int track) const;
    void setEngine(int track, int engine);     // its defaults and first preset
    void applyPreset(int track, int preset);
    void reset();                              // as the device powers on

private:
    const FeluccaCopy* core_ = nullptr;
    int index_ = -1;
    mutable std::mutex lock_;
    std::array<int32_t, 2 * 32> block_{};      // one control block, interleaved
    int blockPos_ = 32;                        // frames of block_ already given out
    int ctl_ = 32;
};
