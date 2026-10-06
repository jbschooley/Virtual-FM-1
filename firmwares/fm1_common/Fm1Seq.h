// Fm1Seq -- the FM-1 Sequencer's pattern format and the SysEx that moves it.
//
// A port of baud girl's fm1seq.js. A pattern on the synth is 64 steps of 32
// bytes (notes at +0..+9, the step's note value at +10, velocities at +20..+29)
// plus one byte per pattern in the global settings block for gate, swing, rate,
// length and sound, and a halfword for tempo. The write message carries eight
// steps and the pattern settings; reading uses the memory-read request.
//
// See docs/FIRMWARE-GAPS.md section 2 for what the protocol cannot carry.
// The plugin keeps more per step than the protocol carries (ratchet, chance,
// gate, transpose, accent, slide). Those fields are plugin-side only until the
// firmware's layout for them is published; the codec ignores them.

#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "Fm1Codec.h"

namespace fm1::seq {

constexpr int kSteps = 64;
constexpr int kStockSteps = 16;
constexpr int kMaxNotes = 9;
constexpr int kPatterns = 16;
constexpr int kTicksPerQuarter = 192;   // the unit of kValueTicks (the firmware's table)
// stepTimes() durations come out at 96 ticks per quarter: a step is `sw` or
// `ticks - sw` of its value's kValueTicks, which sum to the value at 192 per
// quarter, so each half is the value at 96 per quarter (her MIDI export uses 96).
constexpr int kStepTicksPerQuarter = 96;

extern const char* const kNoteValueNames[10];   // "1/1" .. "1/32T"
extern const int kValueTicks[10];               // 768 .. 16, 192 = quarter

constexpr uint32_t kStepsRam = 0x01C14CD0;      // steps 1-16, 512 B a pattern
constexpr uint32_t kExtRam   = 0x01C79D30;      // steps 17-64, 1536 B a pattern (FM-1_082 on)
constexpr uint32_t kGsetRam  = 0x01C0E840 + 5816;
constexpr int kGsetLen = 137;
// FM-1_096's parameter locks (her fm1seq.js, firmware/src/seq_lock.h): "FMLK" at kLockRam once
// set up, then 512 B a pattern from kLockTabRam, 8 B a step: four locks of (what, value),
// what 0xFF = none. Earlier firmware has something else at that address.
constexpr uint32_t kLockRam    = 0x01C76CA0;
constexpr uint32_t kLockTabRam = kLockRam + 0x60;
constexpr int kLockBytes = 512;            // one pattern's
constexpr int kLocksPerStep = 4;

struct Note {
    int note = 60, vel = 100;
    bool tie = false;             // held into the next step (FM-1_092 "Tie & Slide" on a note); plugin-side
};

struct Step {
    int rate = 6;                 // 0..9 note value, see kNoteValueNames
    std::vector<Note> notes;      // up to 9
    // plugin-side extras (FM-1+VA features, not in the protocol)
    int ratchet = 1;              // 1 (off), 2, 3, 4
    int gate = 0;                 // 0 = pattern's gate, else 5..100
    int chance = 100;             // 5..100
    int transpose = 0;            // -24..24
    bool accent = false;
    bool slide = false;           // "Tie & Slide" for the whole step: every note held into the next
};

struct Pattern {
    int length = 16;              // 1..64
    int rate = 6;                 // 0..9
    int tempo = 120;              // 30..300
    int gate = 50;                // 5..100
    int swing = 50;               // 50..75
    int sound = -1;               // -1 none; the synth stopped storing a preset per pattern at FM-1_060
    int chain = -1;               // FM-1_093: what plays after this pattern, -1 Repeat, else 0..15
    int transpose = 0;            // plugin-side
    std::array<Step, kSteps> steps{};
    // FM-1_096: the pattern's parameter locks as the synth's table holds them (kLockBytes), or
    // empty when none were read. Not played here: kept so a Send gives them back.
    std::vector<uint8_t> locks;
};
bool hasLocks(const Pattern& p);          // any step holds a lock

Pattern normalise(const Pattern& p, bool withLocks = true);   // every value in range, notes unique, at most 9, vel 1..127
                                                             // (withLocks false: no locks, nothing allocated for them)

// Eight steps of a pattern: steps 8*part+1 .. 8*part+8, part 0..7. 177 bytes.
Bytes encodeWritePart(const Pattern& p, int pat, int part, bool save);
// The messages for a pattern: steps 1-16 always, then one for every eight
// steps its length reaches past 16; the last one saves if `save`.
// `locks` (FM-1_096 on): a pattern message clears the locks of the steps it writes, so for
// each eighth sent that holds a lock a lock message (0x21) follows, after all the pattern's
// own; the last message of all saves. Firmware before FM-1_096 refuses the lock message.
std::vector<Bytes> encodeWrite(const Pattern& p, int pat, bool save, bool locks = false);
// Steps 8*eighth+1 .. 8*eighth+8's locks, from the table's 64 bytes there. 74 bytes.
Bytes encodeLocksPart(const std::vector<uint8_t>& table, int pat, int eighth, bool save);

struct MemRequest { uint32_t addr; int n; };
// steps 1-16 (two), the settings block, then steps 17-64 (six)
std::vector<MemRequest> readRequests(int pat);
// FM-1_096: the locks' mark, then the pattern's 512 bytes of the table (two)
std::vector<MemRequest> lockRequests(int pat);
// Those answers -> the pattern's table, or empty when the mark is not there (before FM-1_096).
std::vector<uint8_t> decodeLocks(const Bytes& mark, const Bytes& a, const Bytes& b);

// Steps (32 B each from step 1: 512 B for 1-16 or 2048 B for all 64) + the
// settings block -> a pattern; steps not given are empty.
Pattern decodePattern(const Bytes& steps, const Bytes& gset, int pat);

// Timing as seq_tick does it: a step lasts its note value's ticks split by
// swing, alternating sw and ticks - sw.
struct StepTime { int start, dur, gate; };
struct Times { std::vector<StepTime> steps; int total = 0; };
Times stepTimes(const Pattern& p);

}  // namespace fm1::seq
