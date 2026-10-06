#include "Fm1Seq.h"

#include <algorithm>
#include <set>

namespace fm1::seq {

const char* const kNoteValueNames[10] = {"1/1", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T"};
const int kValueTicks[10] = {768, 384, 192, 128, 96, 64, 48, 32, 24, 16};

static int clamp(int v, int lo, int hi) { return std::min(hi, std::max(lo, v)); }

Pattern normalise(const Pattern& p, bool withLocks) {
    Pattern out;
    out.length = clamp(p.length, 1, kSteps);
    out.rate = clamp(p.rate, 0, 9);
    out.tempo = clamp(p.tempo, 30, 300);
    out.gate = clamp(p.gate, 5, 100);
    out.swing = clamp(p.swing, 50, 75);
    out.sound = p.sound < 0 ? -1 : clamp(p.sound, 0, 127);
    out.chain = p.chain < 0 ? -1 : clamp(p.chain, 0, kPatterns - 1);
    out.transpose = clamp(p.transpose, -24, 24);
    if (withLocks && p.locks.size() == size_t(kLockBytes)) out.locks = p.locks;   // as the synth's table holds them
    for (int i = 0; i < kSteps; ++i) {
        const Step& s = p.steps[size_t(i)];
        Step o;
        std::set<int> seen;
        for (const Note& n : s.notes) {
            int note = clamp(n.note, 0, 127);
            if (seen.count(note) || int(o.notes.size()) >= kMaxNotes) continue;
            seen.insert(note);
            o.notes.push_back({note, clamp(n.vel, 1, 127), n.tie});
        }
        o.rate = clamp(s.rate, 0, 9);
        o.ratchet = clamp(s.ratchet, 1, 4);
        o.gate = s.gate == 0 ? 0 : clamp(s.gate, 5, 100);
        o.chance = clamp(s.chance, 5, 100);
        o.transpose = clamp(s.transpose, -24, 24);
        o.accent = s.accent;
        o.slide = s.slide;
        out.steps[size_t(i)] = o;
    }
    return out;
}

Bytes encodeWritePart(const Pattern& pattern, int pat, int part, bool save) {
    Pattern p = normalise(pattern);
    Bytes body = {0x20, uint8_t(pat & 0x0F), uint8_t(part & 7), uint8_t(save ? 1 : 0),
                  uint8_t(p.length), uint8_t(p.rate), uint8_t(p.tempo & 0x7F), uint8_t(p.tempo >> 7),
                  uint8_t(p.gate), uint8_t(p.swing), uint8_t(std::max(0, p.sound))};   // not stored since FM-1_060
    for (int i = 0; i < 8; ++i) {
        const Step& s = p.steps[size_t((part & 7) * 8 + i)];
        uint8_t notes[kMaxNotes] = {0}, vels[kMaxNotes] = {0};
        for (size_t j = 0; j < s.notes.size(); ++j) { notes[j] = uint8_t(s.notes[j].note); vels[j] = uint8_t(s.notes[j].vel); }
        body.push_back(uint8_t(s.rate));
        body.push_back(uint8_t(s.notes.size()));
        body.insert(body.end(), notes, notes + kMaxNotes);
        body.insert(body.end(), vels, vels + kMaxNotes);
    }
    Bytes m = {0xF0, 0x43, 0x00, kSubId};
    m.insert(m.end(), body.begin(), body.end());
    m.push_back(ysum(body));
    m.push_back(0xF7);
    return m;
}

bool hasLocks(const Pattern& p) {
    return std::any_of(p.locks.begin(), p.locks.end(), [](uint8_t b) { return b != 0xFF; });
}

static bool eighthHasLocks(const std::vector<uint8_t>& t, int eighth) {
    if (t.size() != size_t(kLockBytes)) return false;
    for (int i = 0; i < 64; i += 2) if (t[size_t(eighth * 64 + i)] != 0xFF) return true;
    return false;
}

std::vector<Bytes> encodeWrite(const Pattern& pattern, int pat, bool save, bool locks) {
    Pattern p = normalise(pattern);
    int parts = std::max(2, (p.length + 7) / 8);
    std::vector<int> lk;
    if (locks)
        for (int k = 0; k < parts; ++k) if (eighthHasLocks(p.locks, k)) lk.push_back(k);
    std::vector<Bytes> out;
    for (int k = 0; k < parts; ++k) out.push_back(encodeWritePart(p, pat, k, save && lk.empty() && k == parts - 1));
    for (size_t i = 0; i < lk.size(); ++i) out.push_back(encodeLocksPart(p.locks, pat, lk[i], save && i == lk.size() - 1));
    return out;
}

Bytes encodeLocksPart(const std::vector<uint8_t>& t, int pat, int eighth, bool save) {
    Bytes body = {0x21, uint8_t(pat & 0x0F), uint8_t(eighth & 7), uint8_t(save ? 1 : 0)};
    for (int i = 0; i < 64; i += 2) {
        const size_t at = size_t((eighth & 7) * 64 + i);
        const bool none = t.size() != size_t(kLockBytes) || t[at] == 0xFF;
        body.push_back(none ? 0x7F : uint8_t(t[at] & 0x7F));
        body.push_back(none ? 0x7F : uint8_t(t[at + 1] & 0x7F));
    }
    Bytes m = {0xF0, 0x43, 0x00, kSubId};
    m.insert(m.end(), body.begin(), body.end());
    m.push_back(ysum(body));
    m.push_back(0xF7);
    return m;
}

std::vector<MemRequest> lockRequests(int pat) {
    const uint32_t t = kLockTabRam + uint32_t(pat) * uint32_t(kLockBytes);
    return {{kLockRam, 4}, {t, 256}, {t + 256, 256}};
}

std::vector<uint8_t> decodeLocks(const Bytes& mark, const Bytes& a, const Bytes& b) {
    if (mark.size() < 4 || mark[0] != 'F' || mark[1] != 'M' || mark[2] != 'L' || mark[3] != 'K') return {};
    if (a.size() < 256 || b.size() < 256) throw CodecError("lock table is short");
    std::vector<uint8_t> t(a.begin(), a.begin() + 256);
    t.insert(t.end(), b.begin(), b.begin() + 256);
    return t;
}

std::vector<MemRequest> readRequests(int pat) {
    uint32_t base = kStepsRam + uint32_t(pat) * 512;
    uint32_t ext = kExtRam + uint32_t(pat) * 1536;
    std::vector<MemRequest> r = {{base, 256}, {base + 256, 256}, {kGsetRam, kGsetLen}};
    for (uint32_t k = 0; k < 6; ++k) r.push_back({ext + 256 * k, 256});
    return r;
}

Pattern decodePattern(const Bytes& steps, const Bytes& gset, int pat) {
    Pattern p;
    if (gset.size() < size_t(kGsetLen)) throw CodecError("settings block is short");
    size_t u = size_t(pat);
    p.length = gset[98 + u];
    p.rate = gset[50 + u];
    p.tempo = gset[66 + 2 * u] | (gset[67 + 2 * u] << 8);
    p.gate = gset[18 + u];
    p.swing = gset[34 + u];
    int v = gset[118 + u];
    p.sound = v < 128 ? v : -1;
    p.chain = (v >= 128 && v < 128 + kPatterns) ? v - 128 : -1;
    for (int i = 0; i < kSteps; ++i) {
        Step st;
        st.rate = p.rate;
        if (size_t(i + 1) * 32 <= steps.size()) {
            const uint8_t* s = steps.data() + i * 32;
            for (int j = 0; j < 10; ++j)
                if (s[j] < 128) st.notes.push_back({s[j], s[20 + j], false});
            st.rate = s[10] > 9 ? p.rate : s[10];
        }
        p.steps[size_t(i)] = st;
    }
    return p;
}

Times stepTimes(const Pattern& p) {
    Times t;
    int at = 0;
    for (int i = 0; i < p.length; ++i) {
        int ticks = kValueTicks[clamp(p.steps[size_t(i)].rate, 0, 9)];
        int sw = p.swing * ticks / 100;
        int dur = (i % 2) ? ticks - sw : sw;
        t.steps.push_back({at, dur, std::max(1, p.gate * dur / 100)});
        at += dur;
    }
    t.total = at;
    return t;
}

}  // namespace fm1::seq
