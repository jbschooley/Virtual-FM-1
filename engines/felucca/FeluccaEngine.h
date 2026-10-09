// FeluccaEngine -- Felucca (engines/felucca/felucca_core.c), SLOOP or Melodee for one plugin
// instance. No JUCE.
//
// Each firmware is compiled once; its state is a block each instance owns (fel_state.py:
// felucca_core.h's bind / state_size / state_init), about 0.6 to 1.1 MB. Every call into the
// firmware points the code at this instance's block first, on the calling thread, under the
// instance's lock: instances are independent, and any number of them play at once, on any threads.
//
// Felucca runs at 44.1 kHz in blocks of 32 samples (its control rate); render()
// takes any number of frames and keeps the rest of a block for next time. MIDI goes
// in as USB-MIDI packets, as the FM-1's USB port delivers them; Felucca reads them
// at the start of each 32-sample block, as on the device.
//
// Thread safety: every call into the firmware takes the instance's lock (render holds it for a
// block), so calls from any thread see whole changes.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
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
    // Which firmware: Felucca, SLOOP (engines/sloop) or Melodee (engines/melodee), both built from
    // Felucca and behind the same API
    enum class Flavor { Felucca, Sloop, Melodee };
    static int instances(Flavor f = Flavor::Felucca);    // instances alive

    explicit FeluccaEngine(Flavor f = Flavor::Felucca);
    Flavor flavor() const { return flavor_; }
    ~FeluccaEngine();
    FeluccaEngine(const FeluccaEngine&) = delete;
    FeluccaEngine& operator=(const FeluccaEngine&) = delete;

    bool valid() const { return core_ != nullptr; }   // false only if fel_state's layout check failed
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
    int stepOf(int track) const;               // the step it plays now (0-based), -1 while stopped
    unsigned armed() const;                    // live recording armed: a bit per track
    unsigned scaleMask(int track) const;       // its scale, 12 bits from its ROOT (bit 0: the root)
    // the panel's LEDs: the 14 buttons by label, the 27 keys, PLAY's green LED (Felucca); each
    // 0 dark, 1 the background glow, 2 dim (SLOOP's guide), 3 lit
    std::vector<uint8_t> leds() const;
    std::vector<std::string> laneNames(int track) const;   // its drum lanes (Felucca: 8 on any track; SLOOP: the drum track's 16)
    // SLOOP's live sections, song and solo (sloop_core.c FEL(arr_do) / arr_state / arr_chain);
    // Felucca: -1, nothing
    struct Arrangement {
        int playing = -1, queued = -1;     // the section playing, the one asked for (-1 none)
        unsigned stored = 0;               // sections A-D stored, a bit each
        bool songMode = false, songPlays = false, loop = false;
        int songRec = 0, entry = 0, bar = 0;   // SONG REC 0 off, 1 armed, 2 recording; the song's place
        unsigned solo = 0;
        std::vector<std::pair<int, int>> chain;   // (section, bars)
    };
    std::optional<Arrangement> arrangement() const;
    int arrangementDo(int op, int arg);    // FEL(arr_do)
    int setChain(const std::vector<std::pair<int, int>>& chain, bool loop);
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
    std::unique_lock<std::mutex> bind() const; // its lock held, the firmware pointed at its state
    void feed(const uint8_t* bytes, int size); // (bind() held) a SysEx message into its USB port
    void drain();                              // (bind() held) what it sent, into sxDone_
    Flavor flavor_ = Flavor::Felucca;
    const FeluccaCopy* core_ = nullptr;
    struct Free { void operator()(void* p) const; };
    std::unique_ptr<void, Free> state_;        // the firmware's state, this instance's (16-aligned)
    mutable std::mutex lock_;
    std::array<int32_t, 2 * 32> block_{};      // one control block, interleaved
    std::vector<uint8_t> sxOut_;               // SysEx coming out, until a message is whole
    std::vector<std::vector<uint8_t>> sxDone_;
    int blockPos_ = 32;                        // frames of block_ already given out
    int ctl_ = 32;
};
