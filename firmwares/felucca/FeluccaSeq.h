// FeluccaSeq -- the sequencers of Felucca and SLOOP through their editor protocol (FeluccaSync.h):
// a track's 64 steps, its pattern settings, SLOOP's drum lanes, Felucca's song chain and motion.
// The Sequencer tab edits the plugin's own device with these, and the same requests reach a real
// FM-1 (Pull, Send, Live).
//
// A step's bytes, as TRACK_STEP (30) carries them after track and index:
//   Felucca 1.0.2 (12): n, note0..3, time, flags, vel, hit&127, acc&127, hit>>7 | (acc>>7)<<1, chance
//   SLOOP 2.3     (11): n, note0..3, time, flags, vel, lvl&127, lvl>>7 | (rat>>7)<<1, rat&127
// time: 0 NOTE, 1 TIE, 2 REST; flags: 1 ACCENT, 2 SLIDE; vel 0 = 96. Felucca's hit/acc are 8 drum
// lanes, chance the percent of passes the step plays (100 always, 0 never; nothing above 100: the
// device refuses the request). SLOOP's lvl/rat are 2 bits a note (level
// keep/ghost/soft/hard, ratchet x1..x4); its drum track's steps go by DRUM_STEP (33): 16 lanes,
// each 2 bits of level and of ratchet.
#pragma once

#include <array>
#include <optional>
#include <vector>

#include "FeluccaSync.h"

namespace felucca {

constexpr int kSteps = 64;
enum StepTime { kNote = 0, kTie = 1, kRest = 2 };
enum StepFlag { kAccent = 1, kSlide = 2 };
// the pattern settings (per track, both firmwares): P_SLEN 29, P_SDIV 30, P_SSWING 31, P_SGATE 32
enum PatternParam { kLen = 29, kDiv = 30, kSwing = 31, kGate = 32 };

struct Step {
    int n = 0;
    std::array<uint8_t, 4> note{};
    int time = kRest, flags = 0, vel = 0;
    int hit = 0, acc = 0, chance = 100; // Felucca: its drum lanes, accents, chance (percent)
    int lvl = 0, rat = 0;               // SLOOP: 2 bits a note
    bool operator==(const Step&) const = default;
};
struct DrumStep {                       // SLOOP's drum track
    uint32_t on = 0, lvl = 0, rat = 0;  // a bit a lane; 2 bits a lane
    bool operator==(const DrumStep&) const = default;
    bool has(int lane) const { return (on >> lane) & 1u; }
    int level(int lane) const { return int((lvl >> (2 * lane)) & 3u); }
    int ratchet(int lane) const { return int((rat >> (2 * lane)) & 3u); }
    void set(int lane, bool hit, int level = 0, int ratchet = 0);
};

bool isSloop(const Dialect& d);
int stepBytes(const Dialect& d);                 // 12 or 11
std::vector<uint8_t> stepArgs(const Dialect& d, const Step& s);
std::optional<Step> stepFrom(const Dialect& d, const std::vector<uint8_t>& body);   // the body only
std::vector<uint8_t> drumArgs(const DrumStep& s);                                     // after the index
std::optional<DrumStep> drumFrom(const std::vector<uint8_t>& body);

// TRACK_STEP / DRUM_STEP / TRACK_PARAM requests (to send with ask, or to the plugin's own device)
Bytes stepWrite(const Dialect& d, int track, int index, const Step& s);
Bytes drumWrite(const Dialect& d, int index, const DrumStep& s);
Bytes paramWrite(int track, int id, int value);

std::optional<Step> readStep(Endpoint& e, int track, int index);
std::optional<DrumStep> readDrumStep(Endpoint& e, int index);
std::optional<int> readParam(Endpoint& e, int track, int id);
bool isDrumTrack(const Dialect& d, int track);

// One track's pattern: its settings and steps (SLOOP's drum track: drums, not steps)
struct TrackPattern {
    int len = 16, div = 2, swing = 0, gate = 64;
    std::vector<Step> steps;            // kSteps
    std::vector<DrumStep> drums;        // kSteps on SLOOP's drum track, else empty
    bool operator==(const TrackPattern&) const = default;
};
std::optional<TrackPattern> readPattern(Endpoint& e, int track, juce::String& error, int upTo = kSteps);
bool writePattern(Endpoint& e, int track, const TrackPattern& p, juce::String& error, const TrackPattern* known = nullptr);

// Every track's pattern from one side to the other (Pull / Send of the patterns): only what
// differs is written. Steps past a track's length are copied too unless onlyLength.
bool copyPatterns(Endpoint& from, Endpoint& to, int tracks, juce::String& error, bool onlyLength = false,
                  const Progress& progress = {});

// Felucca's song chain (SONG, 33): rows of {project slot 0..3, repeat 1..16}
struct Chain {
    std::vector<std::pair<int, int>> rows;
    bool running = false;
    int row = 0, remaining = 0;
};
std::optional<Chain> readChain(Endpoint& e);
Bytes chainWrite(const std::vector<std::pair<int, int>>& rows);
Bytes chainPlay(bool play);

// Felucca's motion (MOTION, 64): a track's recorded parameter events
struct Motion {
    bool on = false;
    int max = 64;
    struct Event { int step, param, value; };
    std::vector<Event> events;
};
std::optional<Motion> readMotion(Endpoint& e, int track);
Bytes motionOn(int track, bool on);
Bytes motionClear(int track);
Bytes motionSet(int track, int step, int param, int value);
Bytes motionDelete(int track, int step, int param);
bool motionParam(int id);                      // a parameter Felucca's motion records (motion.c)

}  // namespace felucca
