// FeluccaEngine -- Felucca (engines/felucca/felucca_core.c) for one plugin instance. No JUCE.
//
// Felucca keeps its state in one compiled copy's variables, and there are copies() of them
// (FELUCCA_COPIES). Each instance plays in one: alone while there are no more instances than
// copies, else sharing it with others, its state saved out when another plays there and put
// back before it plays again (about 0.9 MB each way: CPU, not a limit). An instance always
// plays in the copy it started in.
//
// Felucca runs at 44.1 kHz in blocks of 32 samples (its control rate); render()
// takes any number of frames and keeps the rest of a block for next time. MIDI goes
// in as USB-MIDI packets, as the FM-1's USB port delivers them; Felucca reads them
// at the start of each 32-sample block, as on the device.
//
// Thread safety: every call that reads or changes the instance's state takes its copy's lock
// (render holds it for a block), so calls from any thread see whole changes. What does not
// depend on the state (names, counts, presets' names) takes no lock.
#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
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
    // Which firmware: Felucca, or SLOOP (engines/sloop, a fork of Felucca behind the same API,
    // with its own copies: SLOOP_COPIES)
    enum class Flavor { Felucca, Sloop };
    static int copies(Flavor f = Flavor::Felucca);       // the compiled copies (instances beyond them share)
    static int copiesInUse(Flavor f = Flavor::Felucca);  // copies with at least one instance
    static int instances(Flavor f = Flavor::Felucca);    // instances, in all copies

    explicit FeluccaEngine(Flavor f = Flavor::Felucca);   // in the copy with the fewest instances
    Flavor flavor() const { return flavor_; }
    ~FeluccaEngine();
    FeluccaEngine(const FeluccaEngine&) = delete;
    FeluccaEngine& operator=(const FeluccaEngine&) = delete;

    bool valid() const { return core_ != nullptr; }   // (always, now there is no limit)
    long swaps() const { return swaps_; }      // times its state was put back into its copy
    static constexpr double kRate = 44100.0;   // FS: Felucca renders at the FM-1's rate only

    // ---- audio thread ----
    void midi(const uint8_t* bytes, int size);   // a channel message (notes, CCs ...)
    void render(float* left, float* right, int frames);

    // ---- the virtual device: any thread (each takes the lock) ----
    // A whole SysEx message (F0 .. F7) into its USB port: the editor protocol (F0 7D 46 4C ..).
    // The update and boot loader messages are taken and ignored, as nothing here acts on them.
    void sysex(const uint8_t* bytes, int size);
    // What it sent out since the last call: whole SysEx messages (the editor's replies and pushes).
    std::vector<std::vector<uint8_t>> takeSysex();
    // A whole editor request, answered now (not at the next main loop pass): what came out.
    std::vector<std::vector<uint8_t>> request(const std::vector<uint8_t>& sysex);
    // The same, but only its reply (the first message of its command); what else came out
    // (pushes) stays for takeSysex. Atomic: two threads asking never get each other's replies.
    std::optional<std::vector<uint8_t>> ask(const std::vector<uint8_t>& sysex);
    // The front panel, by Felucca's own labels (FX, SCL ... OCT+) and knob roles (SELECT ...
    // KNOB 4); the 27 keys from the lowest. The main loop sees them within 16 ms of audio.
    std::vector<std::string> buttonNames() const;
    std::vector<std::string> knobNames() const;
    void button(int label, bool down);
    void key(int index, bool down);
    void knob(int role, int steps);
    // The screen, after drawing what changed: 240 x 240, RGB565 (native byte order).
    static constexpr int kScreen = 240;
    void draw(std::vector<uint16_t>& rgb565);
    void transport(bool play);                 // PLAY: the next block starts or stops it
    int selected() const;                      // the selected part: Felucca's keys play it
    void select(int track);                    // as ALGORITHM does on the device
    bool playing() const;
    // The device's stored objects, as Felucca's full backup carries them: 0 the music now
    // (a FUN8 project), 1 settings, 2..5 the project slots, 6 and 7 the user presets, 8 the
    // FM6 bank. get: false if there is no such object (an empty one gives no bytes); put:
    // Felucca's rc (0 ok, 1 invalid, 2 failed validation, 4 storage).
    bool object(int id, std::vector<uint8_t>& out);
    int putObject(int id, const std::vector<uint8_t>& bytes);

    // ---- any thread ----
    struct Desc {
        std::string label, unit;
        int fmt = 0, min = 0, max = 0, def = 0;
        std::vector<std::string> names;        // for a list (fmt 8)
    };
    std::string version() const;               // the Felucca release built in ("v1.0")
    int tracks() const;                        // 4 parts (Felucca 1.0: each with any engine)
    int parts() const;                         // the same
    int engines() const;
    int paramCount() const;                    // per track (91 in 1.0)
    int firstEngineParam() const;              // P_E0: the engine's eight follow
    int globalCount() const;
    std::string engineName(int e) const;
    std::string enginePage(int e, int page) const;   // the titles of its two edit pages
    std::vector<std::string> presetNames(int e) const;   // by number; "" for an alias (not offered)
    std::vector<int> enginesShown() const;     // the engines one can pick, in Felucca's order
    int fm6Engine() const;                     // FM6's engine number
    // FM6's patch of a track: the 155-byte DX7 single-voice layout (the FM-1's VCED)
    std::array<uint8_t, 155> fm6Patch(int track) const;
    void setFm6Patch(int track, const std::array<uint8_t, 155>& v);   // after PTCH (E7): it keeps this patch
    Desc paramDesc(int track, int id) const;   // engine parameters are the track's engine's
    bool paramRange(int track, int id, int& min, int& max) const;   // without allocating (audio thread)
    bool globalRange(int id, int& min, int& max) const;
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
    std::unique_lock<std::mutex> bind() const; // its state in its copy, the copy's lock held
    void feed(const uint8_t* bytes, int size); // (bind() held) a SysEx message into its USB port
    void drain();                              // (bind() held) what it sent, into sxDone_
    Flavor flavor_ = Flavor::Felucca;
    const FeluccaCopy* core_ = nullptr;
    int index_ = -1;
    mutable std::vector<uint8_t> saved_;       // its state while another instance plays in its copy
    mutable bool fresh_ = true;                // never played yet: the copy starts afresh for it
    mutable long swaps_ = 0;
    std::array<int32_t, 2 * 32> block_{};      // one control block, interleaved
    std::vector<uint8_t> sxOut_;               // SysEx coming out, until a message is whole
    std::vector<std::vector<uint8_t>> sxDone_;
    int blockPos_ = 32;                        // frames of block_ already given out
    int ctl_ = 32;
};
