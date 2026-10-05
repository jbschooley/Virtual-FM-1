#include "Fm1Record.h"

#include <algorithm>

namespace fm1 {

const char* const kEffectNames[kEffects] = {"Filter", "Reverb", "Delay", "Distortion", "Chorus", "Phaser"};
const char* const kEffectParamNames[kEffects][3] = {
    {"Cutoff", "Resonance", ""}, {"Decay", "Mix", ""}, {"Feedback", "Rate", "Mix"},
    {"Gain", "Tone", "Level"}, {"Rate", "Depth", "Mix"}, {"Rate", "Depth", "Mix"}};
const int kEffectParamMax[kEffects][3] = {
    {107, 10, 0}, {100, 100, 0}, {100, 100, 100}, {100, 100, 100}, {100, 100, 100}, {100, 100, 100}};
const int kEffectTypeCount[kEffects] = {3, 3, 0, 3, 0, 0};
const char* const kEffectTypeNames[kEffects][3] = {
    {"Low Pass", "Band Pass", "High Pass"}, {"Room", "Hall", "Plate"}, {"", "", ""},
    {"Soft Clip", "Hard Clip", "Foldback"}, {"", "", ""}, {"", "", ""}};

const char* const kFilterTypeNames[4] = {"LP12", "LP24", "BP", "HP"};

VaFilter filterFromRecord(const Record& r) {
    VaFilter f;
    auto get = [&r](int i, int dflt) { return (r[size_t(i)] & 0x80) ? std::min(int(r[size_t(i)] & 0x7F), 100) : dflt; };
    f.cutoff = get(23, 100); f.resonance = get(24, 0); f.envelope = get(25, 0);
    f.velocity = get(47, 0); f.shape = get(49, 0); f.lfo = get(50, 0); f.decay = get(51, 0);
    if (r[26] & 0x80) { f.type = r[26] & 3; f.keyTrack = (r[26] >> 2) & 3; f.on = (r[26] & 0x10) != 0; }
    if (r[18] == kMarkVA) f.on = true;
    return f;
}

FxChain fxFromRecord(const Record& r) {
    FxChain c;
    bool seen[kEffects] = {false};
    for (int k = 0; k < kEffects; ++k) {
        int id = r[size_t(27 + 3 * k)];
        if (id < 0 || id >= kEffects || seen[id]) id = -1;
        if (id >= 0) {
            seen[id] = true;
            c.order[size_t(k)] = id;
            c.fx[size_t(id)].on = r[size_t(28 + 3 * k)] != 0;
            int types = kEffectTypeCount[id];
            c.fx[size_t(id)].type = types ? std::min(int(r[size_t(29 + 3 * k)]), types - 1) : 0;
        } else {
            c.order[size_t(k)] = -1;
        }
    }
    // repair a damaged order: fill the gaps with the effects not seen
    for (int k = 0; k < kEffects; ++k)
        if (c.order[size_t(k)] < 0)
            for (int e = 0; e < kEffects; ++e)
                if (!seen[e]) { c.order[size_t(k)] = e; seen[e] = true; break; }
    for (int e = 0; e < kEffects; ++e)
        for (int i = 0; i < 3; ++i)
            c.fx[size_t(e)].p[size_t(i)] = std::min(int(r[size_t(3 * e + i)]), std::max(kEffectParamMax[e][i], 0));
    return c;
}

void fxToRecord(const FxChain& c, Record& r) {
    for (int k = 0; k < kEffects; ++k) {
        int id = c.order[size_t(k)];
        r[size_t(27 + 3 * k)] = uint8_t(id);
        r[size_t(28 + 3 * k)] = c.fx[size_t(id)].on ? 1 : 0;
        r[size_t(29 + 3 * k)] = uint8_t(c.fx[size_t(id)].type);
    }
    for (int e = 0; e < kEffects; ++e)
        for (int i = 0; i < 3; ++i)
            r[size_t(3 * e + i)] = uint8_t(c.fx[size_t(e)].p[size_t(i)]);
}

Envelope envFromRecord(const Record& r) {
    Envelope e;
    e.a = std::min<int>(r[54], 100);
    e.d = std::min<int>(r[55], 100);
    e.s = std::min<int>(r[56], 100);
    e.r = std::min<int>(r[57], 100);
    e.on = r[58] != 0;
    return e;
}

void envToRecord(const Envelope& e, Record& r) {
    r[54] = uint8_t(e.a); r[55] = uint8_t(e.d); r[56] = uint8_t(e.s); r[57] = uint8_t(e.r);
    r[58] = e.on ? 1 : 0;
}

}  // namespace fm1
