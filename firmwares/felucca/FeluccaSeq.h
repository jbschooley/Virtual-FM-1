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

// SLOOP 2.4 (its editor protocol v7, v8): a track's parameter locks (24 at most, a sound parameter's
// value on a step), each step's nudge (1/64 of a step, -32..31) and fill condition
constexpr int kLockGet = 37, kLockSet = 38, kMicroGet = 39, kMicroSet = 40, kFillGet = 41, kFillSet = 42;
constexpr int kLocksPerTrack = 24, kMicroMin = -32, kMicroMax = 31;
enum { kFillAlways = 0, kFillOnly = 1, kFillNot = 2 };   // core.h FC_NORM, FC_FILL, FC_NOFILL
struct Lock {
    int step = 0, param = 0, value = 0;
    bool operator==(const Lock&) const = default;
    bool operator<(const Lock& o) const { return step != o.step ? step < o.step : param < o.param; }
};
bool lockable(int param);   // SLOOP 2.4's numbering (seq.c p_lockable): the sound's parameters
Bytes lockSet(int track, int step, int param, int value);
Bytes lockDelete(int track, int step, int param);
Bytes microSet(int track, int step, int nudge);
Bytes fillSet(int track, int step, int cond);

// One track's pattern: its settings and steps (SLOOP's drum track: drums, not steps); SLOOP 2.4's
// locks, nudges and fill conditions when the side has them (extras)
struct TrackPattern {
    int len = 16, div = 2, swing = 0, gate = 64;
    std::vector<Step> steps;            // kSteps
    std::vector<DrumStep> drums;        // kSteps on SLOOP's drum track, else empty
    bool extras = false;                // locks, micro and fill read (SLOOP 2.4)
    std::vector<Lock> locks;            // sorted by step, then parameter
    std::vector<int> micro, fill;       // kSteps each when extras
    bool operator==(const TrackPattern&) const = default;
};
// A track's locks, nudges and fill conditions (SLOOP 2.4), or false if the side has none (2.3, Felucca)
bool readExtras(Endpoint& e, int track, TrackPattern& p);
// Step `step`'s locks, nudge and condition from one side to the other, if both have them
bool copyStepExtras(Endpoint& from, Endpoint& to, int track, int step, juce::String& error);
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

// Melodee: each track has 8 pattern banks (PATTERN, 73), and its song's rows name a bank per track
// (BANK_SONG, 74); SONG's rows set every track to one bank
constexpr int kPatternBanks = 8, kPatternCmd = 73, kBankSong = 74;
std::optional<int> readPatternBank(Endpoint& e, int track);         // the bank the track plays
// now (stopped), else false; playing, the switch is queued for the next bar: kept if keepQueued,
// else taken back
bool selectPatternBank(Endpoint& e, int track, int bank, bool keepQueued = false);
struct BankRow {
    std::array<int, 4> banks{};   // per track
    int repeat = 1;               // 1..16
    bool operator==(const BankRow&) const = default;
};
struct BankChain { bool running = false; std::vector<BankRow> rows; };
std::optional<BankChain> readBankChain(Endpoint& e);
Bytes bankChainWrite(const std::vector<BankRow>& rows);

// Pull or Send of the patterns, from -> to: every track's pattern (Melodee: the pattern bank it
// plays, the other side switched to it first), and Felucca's song chain or Melodee's bank song, and
// each track's motion (SLOOP: its patterns only)
bool copySequencer(Endpoint& from, Endpoint& to, int tracks, juce::String& err, const Progress& progress = {});
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
