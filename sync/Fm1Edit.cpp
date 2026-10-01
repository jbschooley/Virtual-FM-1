#include "Fm1Edit.h"

#include <cmath>

namespace fm1::edit {

// CC numbers per effect: on, then its type (if any) and parameters in record order.
// From the FM-1+VA manual's FX channel table.
static const int kOnCc[kEffects] = {0, 4, 8, 12, 16, 20};
static const int kTypeCc[kEffects] = {1, 5, -1, -1, -1, -1};           // distortion type has no CC
static const int kParamCc[kEffects][3] = {{2, 3, -1}, {6, 7, -1}, {9, 10, 11}, {13, 14, 15}, {17, 18, 19}, {21, 22, 23}};
static const int kEnvCc[4] = {73, 75, 70, 72};                          // A D S R

Bytes paramChange(int param, int value) {
    return {0xF0, 0x43, 0x10, uint8_t((param >> 7) & 1), uint8_t(param & 0x7F), uint8_t(value & 0x7F), 0xF7};
}

Bytes controlChange(int ch, int cc, int value) {
    return {uint8_t(0xB0 | ((ch - 1) & 15)), uint8_t(cc & 0x7F), uint8_t(value & 0x7F)};
}

int envToCc(int v) {
    // the synth maps CC 0..127 onto 0..100 (64 -> 50, 127 -> 100); prefer a CC that
    // lands on v whether the synth truncates or rounds
    for (int cc = 0; cc < 128; ++cc)
        if (cc * 100 / 127 == v && int(std::lround(cc * 100.0 / 127.0)) == v) return cc;
    for (int cc = 0; cc < 128; ++cc)
        if (cc * 100 / 127 == v) return cc;
    return int(std::lround(v * 1.27));
}

static void voiceMessages(const Edit* from, const Edit& to, std::vector<Bytes>& out) {
    for (int i = 0; i < kEditBytes; ++i)
        if (!from || (*from)[size_t(i)] != to[size_t(i)]) out.push_back(paramChange(i, to[size_t(i)]));
}

static void recordMessages(const Record* from, const Record& to, Channels ch, std::vector<Bytes>& out) {
    FxChain a = from ? fxFromRecord(*from) : FxChain{}, b = fxFromRecord(to);
    for (int e = 0; e < kEffects; ++e) {
        const auto& x = a.fx[size_t(e)];
        const auto& y = b.fx[size_t(e)];
        // type and parameters before "on", so an effect switches on already set up
        if (kTypeCc[e] >= 0 && (!from || x.type != y.type)) out.push_back(controlChange(ch.fx, kTypeCc[e], y.type));
        for (int i = 0; i < 3; ++i)
            if (kParamCc[e][i] >= 0 && (!from || x.p[size_t(i)] != y.p[size_t(i)]))
                out.push_back(controlChange(ch.fx, kParamCc[e][i], y.p[size_t(i)]));
        if (!from || x.on != y.on) out.push_back(controlChange(ch.fx, kOnCc[e], y.on ? 127 : 0));
    }
    Envelope ea = from ? envFromRecord(*from) : Envelope{}, eb = envFromRecord(to);
    if (eb.on) {   // these CCs also switch the envelope on
        const int va[4] = {ea.a, ea.d, ea.s, ea.r}, vb[4] = {eb.a, eb.d, eb.s, eb.r};
        for (int i = 0; i < 4; ++i)
            if (!from || !ea.on || va[i] != vb[i]) out.push_back(controlChange(ch.midi, kEnvCc[i], envToCc(vb[i])));
    }
}

std::vector<Bytes> fullSound(const Sound& s, Channels ch) {
    std::vector<Bytes> out;
    voiceMessages(nullptr, unpackVoice(s.voice), out);
    recordMessages(nullptr, s.record, ch, out);
    return out;
}

std::vector<Bytes> delta(const Sound& from, const Sound& to, Channels ch) {
    std::vector<Bytes> out;
    Edit a = unpackVoice(from.voice), b = unpackVoice(to.voice);
    voiceMessages(&a, b, out);
    recordMessages(&from.record, to.record, ch, out);
    return out;
}

bool recordByteSettable(int index, const Record& target) {
    if (index >= 0 && index < 18) {                         // effect parameters
        int e = index / 3, i = index % 3;
        return kParamCc[e][i] >= 0;
    }
    if (index >= 27 && index < 27 + 3 * kEffects) {         // chain: on for all, type for filter and reverb
        int k = (index - 27) / 3, part = (index - 27) % 3;
        int e = target[size_t(27 + 3 * k)];
        if (e < 0 || e >= kEffects) return false;
        if (part == 1) return true;
        if (part == 2) return kTypeCc[e] >= 0;
        return false;
    }
    if (index >= 54 && index <= 57) return envFromRecord(target).on;
    return false;
}

}  // namespace fm1::edit
