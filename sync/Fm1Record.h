// Fm1Record -- the parts of a sound's 59-byte settings record this project
// interprets: the effects chain and the global envelope.
//
// Layout, inferred from baud girl's comments in fm1sound.js and from the 16
// presets of her Virtual Analog pack (see README "Record layout"):
//   [3e .. 3e+2]   effect e's three parameters (e = 0 filter, 1 reverb,
//                  2 delay, 3 distortion, 4 chorus, 5 phaser)
//   [18]           engine marker: 0xA5 FM, 0x5A Virtual Analog
//   [19 .. 22]     Virtual Analog oscillator settings (not interpreted here)
//   [23 .. 26]     Virtual Analog filter (not interpreted here)
//   [27+3k .. +2]  chain position k: effect id, on (0/1), type
//   [54 .. 57]     envelope A D S R, 0..100
//   [58]           envelope on (0/1)
// Parameter meanings per effect follow the manual's CC table: filter cutoff
// 0..107 and resonance 0..10; reverb decay, mix; delay feedback, rate, mix;
// distortion gain, tone, level; chorus rate, depth, mix; phaser rate, depth,
// mix (0..100). Types: filter LP/BP/HP, reverb Room/Hall/Plate, distortion
// Soft/Hard/Foldback.

#pragma once

#include <array>

#include "Fm1Codec.h"

namespace fm1 {

enum Effect { FxFilter = 0, FxReverb, FxDelay, FxDistortion, FxChorus, FxPhaser, kEffects };

struct EffectState {
    bool on = false;
    int type = 0;
    std::array<int, 3> p{0, 0, 0};
};

struct FxChain {
    std::array<int, kEffects> order{0, 1, 2, 3, 4, 5};   // chain position -> effect id
    std::array<EffectState, kEffects> fx{};              // by effect id
};

struct Envelope {
    bool on = false;
    int a = 0, d = 0, s = 100, r = 0;   // 0..100
};

FxChain fxFromRecord(const Record& r);
void fxToRecord(const FxChain& c, Record& r);        // leaves every other byte alone

Envelope envFromRecord(const Record& r);
void envToRecord(const Envelope& e, Record& r);

extern const char* const kEffectNames[kEffects];
extern const char* const kEffectParamNames[kEffects][3];   // "" where unused
extern const int kEffectParamMax[kEffects][3];
extern const int kEffectTypeCount[kEffects];              // 0 = no type
extern const char* const kEffectTypeNames[kEffects][3];

}  // namespace fm1
