// Fm1Seq -- the FM-1 Sequencer's pattern format and the SysEx that moves it.
//
// A port of baud girl's fm1seq.js. A pattern on the synth is 64 steps of 32
// bytes (notes at +0..+9, the step's note value at +10, velocities at +20..+29)
// plus one byte per pattern in the global settings block for gate, swing, rate,
// length and sound, and a halfword for tempo. The write message carries eight
// steps and the pattern settings; reading uses the memory-read request.
//
// A step's 32 bytes also hold its accent and ratchet (FM-1_092 on), its own Gate, Chance and
// Transpose (FM-1_096) and the ends of notes that sound past their step (her fm1seq.js "a
// step's 32 bytes", from firmware/src/seq_carry.h): all read here. The pattern message (0x20)
// carries only notes, velocities and each step's note value, and clears the rest of a step on
// the synth; FM-1_096's whole-step message (0x22) carries a step's 32 bytes as they are.

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
    int len = 0;                  // the steps it sounds past its own: 0 ends on its step, 1 held into the
                                  // next ("Tie & Slide"), and so on (where the synth keeps its end)
};

struct Step {
    int rate = 6;                 // 0..9 note value, see kNoteValueNames
    std::vector<Note> notes;      // up to 9
    int ratchet = 1;              // 1 (off), 2, 3, 4 (FM-1_092 on)
    int gate = 0;                 // 0 = pattern's gate, else 5..100 (FM-1_096)
    int chance = 100;             // 5..95, 100 always (FM-1_096)
    int transpose = 0;            // -24..24 (FM-1_096)
    bool accent = false;          // (FM-1_092 on)
};

struct Pattern {
    int length = 16;              // 1..64
    int rate = 6;                 // 0..9
    int tempo = 120;              // 30..300
    int gate = 50;                // 5..100
    int swing = 50;               // 50..75
    int sound = -1;               // -1 none; the synth stopped storing a preset per pattern at FM-1_060
    int chain = -1;               // FM-1_093: what plays after this pattern, -1 Repeat, else 0..15
    int repeats = 1;              // with a chain: plays this many times first (kRepeats)
    int transpose = 0;            // plugin-side
    std::array<Step, kSteps> steps{};
    // FM-1_096: the pattern's parameter locks as the synth's table holds them (kLockBytes), or
    // empty when none were read. Not played here: kept so a Send gives them back.
    std::vector<uint8_t> locks;
    // the 64 steps' 32 bytes as read from the synth (FM-1_092 on), or empty: what goes back for a
    // pattern whose steps were not changed here, so that a Send gives exactly what was there
    std::vector<uint8_t> raw;
    int chainByte = -1;           // the settings' Chain byte as read (below 128: Repeat, kept as it was)
    int readFrom = 0;             // the firmware release it was read from (FM-1_0XX's XX), 0 if not read
};
constexpr int kStepBytes = 32;
extern const int kRepeats[8];             // 1, 2, 3, 4, 6, 8, 12, 16
bool hasLocks(const Pattern& p);          // any step holds a lock
// Patterns kept before notes had lengths marked a tie by repeating the note on the next step: a
// note there with len -1 ("tied") becomes one note held over the steps that repeated it (or into
// the next step, when that held another note: a slide).
void joinTies(Pattern& p);

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
// settings block -> a pattern; steps not given are empty. `full` (FM-1_092 on): the steps'
// accents, ratchets, options and note ends too, and their bytes kept (Pattern::raw).
Pattern decodePattern(const Bytes& steps, const Bytes& gset, int pat, bool full = true);

// FM-1_096's whole-step message (0x22): steps 4*part+1 .. 4*part+4 (part 0..15) with the
// pattern's five settings (Gate, Swing, Rate, Steps, Chain byte). 162 bytes. The synth answers
// it with a pattern reply whose arg is pat | part << 8 | 0x4000 (lock messages: | eighth << 8 | 0x8000).
Bytes encodeStepsPart(const std::array<uint8_t, 5>& settings, const uint8_t* steps128, int pat, int part, bool save);
// A pattern's 64 steps as the synth keeps them: Pattern::raw when its steps are what it holds,
// else built from the steps (as the synth's own step entry lays them out).
std::vector<uint8_t> stepBytes(const Pattern& p);
std::array<uint8_t, 5> settingsBytes(const Pattern& p);
// All of a pattern for FM-1_096: its 16 whole-step messages, then its 8 lock messages (every one,
// so the synth is left with exactly its locks: a step message clears its four steps' locks);
// the last saves if `save`.
std::vector<Bytes> encodeWholeSteps(const Pattern& p, int pat, bool save);
// The pattern reply's arg for message m of encodeWholeSteps or encodeWrite.
uint32_t replyArg(const Bytes& m);

// Timing as seq_tick does it: a step lasts its note value's ticks split by
// swing, alternating sw and ticks - sw.
struct StepTime { int start, dur, gate; };
struct Times { std::vector<StepTime> steps; int total = 0; };
Times stepTimes(const Pattern& p);

}  // namespace fm1::seq
