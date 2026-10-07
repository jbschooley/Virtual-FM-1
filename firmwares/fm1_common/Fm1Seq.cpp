#include "Fm1Seq.h"

#include <algorithm>
#include <set>

namespace fm1::seq {

const char* const kNoteValueNames[10] = {"1/1", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T"};
const int kValueTicks[10] = {768, 384, 192, 128, 96, 64, 48, 32, 24, 16};
const int kRepeats[8] = {1, 2, 3, 4, 6, 8, 12, 16};

static int clamp(int v, int lo, int hi) { return std::min(hi, std::max(lo, v)); }

// ---- a step's 32 bytes, as the synth keeps them (FM-1_092 on) -----------------------------
// From baud girl's description of the layout (her fm1seq.js, which follows the firmware's
// seq_carry.h and seq_play.cpp; written here from that description, not copied):
//   +0..8 notes, +20..28 their velocities, +30 how many, +10 the step's note value (above 9:
//   the pattern's); +29 flags when (b & 0xC0) == 0x40: bit 0 accent, bits 2-3 ratchet - 1,
//   bit 5 extended. A note's end is a pitch in a list on the step it ends on (the first step at
//   or after its own whose list holds it): a plain step's list is +11.. with +31 entries (at most
//   9); an extended step's is +14..+19 (+31 is 9 while it holds any), and +11, +12, +13 are its
//   Gate (0x80 | 5..100), Chance (0x80 | 5..95) and Transpose (0x80 | t + 64), 0xFF = none.
namespace {
bool marked(const uint8_t* s) { return (s[29] & 0xC0) == 0x40; }
bool extended(const uint8_t* s) { return marked(s) && (s[29] & 0x20) != 0; }
int noteCount(const uint8_t* s) { return std::min<int>(s[30], kMaxNotes); }
int option(const uint8_t* s, int at) { return extended(s) && s[at] != 0xFF ? s[at] & 0x7F : -1; }
void endList(const uint8_t* s, int& at, int& cap) {
    if (extended(s)) { at = 14; cap = 6; } else { at = 11; cap = std::min<int>(s[31], kMaxNotes); }
}
bool endsHere(const uint8_t* s, int note) {
    int at, cap;
    endList(s, at, cap);
    for (int j = 0; j < cap; ++j) if (s[at + j] == note) return true;
    return false;
}
bool addEnd(uint8_t* s, int note) {           // false: the list is full
    if (endsHere(s, note)) return true;
    if (extended(s)) {
        for (int j = 0; j < 6; ++j) if (s[14 + j] >= 128) { s[14 + j] = uint8_t(note); s[31] = kMaxNotes; return true; }
        return false;
    }
    const int n = std::min<int>(s[31], kMaxNotes);
    if (n >= kMaxNotes) return false;
    s[11 + n] = uint8_t(note);
    s[31] = uint8_t(n + 1);
    return true;
}
uint8_t* stepAt(std::vector<uint8_t>& P, int k) { return P.data() + k * kStepBytes; }
const uint8_t* stepAt(const std::vector<uint8_t>& P, int k) { return P.data() + k * kStepBytes; }
int endOf(const std::vector<uint8_t>& P, int k, int note) {
    for (; k < kSteps; ++k) if (endsHere(stepAt(P, k), note)) return k;
    return -1;
}
// a note's end on step e, or the nearest step before it (down to its own) with room, else after
void putEnd(std::vector<uint8_t>& P, int from, int note, int e) {
    for (int j = e; j >= from; --j) if (addEnd(stepAt(P, j), note)) return;
    for (int j = e + 1; j < kSteps; ++j) if (addEnd(stepAt(P, j), note)) return;
}
void emptyStep(uint8_t* s) { std::fill(s, s + kStepBytes, uint8_t(0xFF)); s[30] = 0; s[31] = 0; }
std::vector<uint8_t> emptySteps() {
    std::vector<uint8_t> P(size_t(kSteps * kStepBytes));
    for (int k = 0; k < kSteps; ++k) emptyStep(P.data() + k * kStepBytes);
    return P;
}

// the steps as the plugin keeps them, from the synth's bytes (the pattern's length and rate set)
void stepsFromBytes(Pattern& p, const std::vector<uint8_t>& P) {
    const int last = std::max(0, p.length - 1);
    for (int k = 0; k < kSteps; ++k) {
        const uint8_t* s = stepAt(P, k);
        Step st;
        st.rate = s[10] <= 9 ? s[10] : p.rate;
        for (int j = 0; j < noteCount(s); ++j) {
            if (s[j] >= 128) continue;
            int e = endOf(P, k, s[j]);
            if (e < 0) e = k < last ? last : k;   // (no end kept: it sounds to the pattern's end)
            st.notes.push_back({s[j], clamp(s[20 + j], 1, 127), e - k});
        }
        if (marked(s)) {
            st.accent = (s[29] & 1) != 0;
            st.ratchet = ((s[29] >> 2) & 3) + 1;
        }
        const int g = option(s, 11), c = option(s, 12), t = option(s, 13);
        st.gate = g < 0 ? 0 : clamp(g, 5, 100);
        st.chance = c < 0 ? 100 : clamp(c, 5, 95);
        st.transpose = t < 0 ? 0 : clamp(t - 64, -24, 24);
        p.steps[size_t(k)] = st;
    }
}

bool sameSteps(const Pattern& a, const Pattern& b) {
    for (int k = 0; k < kSteps; ++k) {
        const Step& x = a.steps[size_t(k)];
        const Step& y = b.steps[size_t(k)];
        if (x.rate != y.rate || x.ratchet != y.ratchet || x.gate != y.gate || x.chance != y.chance ||
            x.transpose != y.transpose || x.accent != y.accent || x.notes.size() != y.notes.size()) return false;
        for (size_t j = 0; j < x.notes.size(); ++j)
            if (x.notes[j].note != y.notes[j].note || x.notes[j].vel != y.notes[j].vel || x.notes[j].len != y.notes[j].len) return false;
    }
    return true;
}

// a pattern's steps laid out as the synth's own step entry would leave them: options make a
// step extended, notes go in order, and each note's end goes on the step it ends on (the
// notes that end on their own step first, as the synth's step-option conversion does)
std::vector<uint8_t> buildSteps(const Pattern& p) {
    auto P = emptySteps();
    for (int k = 0; k < kSteps; ++k) {
        const Step& st = p.steps[size_t(k)];
        uint8_t* s = stepAt(P, k);
        if (st.rate != p.rate) s[10] = uint8_t(clamp(st.rate, 0, 9));
        const bool ext = st.gate != 0 || st.chance < 100 || st.transpose != 0;
        if (ext || st.accent || st.ratchet > 1)
            s[29] = uint8_t(0x40 | (st.accent ? 1 : 0) | ((clamp(st.ratchet, 1, 4) - 1) << 2) | (ext ? 0x30 : 0));
        if (ext) {
            s[11] = st.gate == 0 ? 0xFF : uint8_t(0x80 | clamp(st.gate, 5, 100));
            s[12] = st.chance >= 100 ? 0xFF : uint8_t(0x80 | clamp(st.chance, 5, 95));
            s[13] = st.transpose == 0 ? 0xFF : uint8_t(0x80 | (clamp(st.transpose, -24, 24) + 64));
        }
        const int n = std::min<int>(int(st.notes.size()), kMaxNotes);
        for (int j = 0; j < n; ++j) {
            s[j] = uint8_t(clamp(st.notes[size_t(j)].note, 0, 127));
            s[20 + j] = uint8_t(clamp(st.notes[size_t(j)].vel, 1, 127));
        }
        s[30] = uint8_t(n);
    }
    for (int pass = 0; pass < 2; ++pass)
        for (int k = 0; k < kSteps; ++k)
            for (const Note& nt : p.steps[size_t(k)].notes)
                if ((nt.len != 0) == (pass != 0))
                    putEnd(P, k, clamp(nt.note, 0, 127), std::min(kSteps - 1, k + std::max(0, nt.len)));
    return P;
}
}  // namespace

Pattern normalise(const Pattern& p, bool withLocks) {
    Pattern out;
    out.length = clamp(p.length, 1, kSteps);
    out.rate = clamp(p.rate, 0, 9);
    out.tempo = clamp(p.tempo, 30, 300);
    out.gate = clamp(p.gate, 5, 100);
    out.swing = clamp(p.swing, 50, 75);
    out.sound = p.sound < 0 ? -1 : clamp(p.sound, 0, 127);
    out.chain = p.chain < 0 ? -1 : clamp(p.chain, 0, kPatterns - 1);
    out.repeats = 1;
    for (int r : kRepeats) if (r <= p.repeats) out.repeats = r;
    if (withLocks && p.raw.size() == size_t(kSteps * kStepBytes)) out.raw = p.raw;   // (what is sent, not what plays)
    out.chainByte = p.chainByte;
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
            o.notes.push_back({note, clamp(n.vel, 1, 127), clamp(n.len, 0, kSteps - 1 - i)});
        }
        o.rate = clamp(s.rate, 0, 9);
        o.ratchet = clamp(s.ratchet, 1, 4);
        o.gate = s.gate == 0 ? 0 : clamp(s.gate, 5, 100);
        o.chance = s.chance >= 100 ? 100 : clamp(s.chance, 5, 95);
        o.transpose = clamp(s.transpose, -24, 24);
        o.accent = s.accent;
        out.steps[size_t(i)] = o;
    }
    // The synth keeps a note's end as its pitch on the step it ends on, and finds a note's end as
    // the first such step at or after its own: notes of one pitch that overlap share the earliest
    // end at or after each one's start (as its own entry does, a note played while the same note
    // still sounds taking that note's end)
    for (int pitch = 0; pitch < 128; ++pitch) {
        std::vector<int> ends;
        for (int i = 0; i < kSteps; ++i)
            for (const Note& n : out.steps[size_t(i)].notes) if (n.note == pitch) ends.push_back(i + n.len);
        if (ends.size() < 2) continue;
        for (int i = 0; i < kSteps; ++i)
            for (Note& n : out.steps[size_t(i)].notes) {
                if (n.note != pitch) continue;
                int e = kSteps;
                for (int x : ends) if (x >= i && x < e) e = x;
                n.len = e - i;
            }
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

void joinTies(Pattern& p) {
    for (int k = 0; k < kSteps; ++k)
        for (Note& n : p.steps[size_t(k)].notes) {
            if (n.len >= 0) continue;
            int len = 0;
            for (int j = k + 1; j < kSteps; ++j) {
                auto& next = p.steps[size_t(j)].notes;
                auto it = std::find_if(next.begin(), next.end(), [&](const Note& m) { return m.note == n.note; });
                if (it == next.end()) break;
                const bool on = it->len < 0;
                next.erase(it);
                ++len;
                if (!on) break;
            }
            n.len = std::max(1, len);
        }
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

Pattern decodePattern(const Bytes& steps, const Bytes& gset, int pat, bool full) {
    Pattern p;
    if (gset.size() < size_t(kGsetLen)) throw CodecError("settings block is short");
    size_t u = size_t(pat);
    p.length = gset[98 + u];
    p.rate = gset[50 + u];
    p.tempo = gset[66 + 2 * u] | (gset[67 + 2 * u] << 8);
    p.gate = gset[18 + u];
    p.swing = gset[34 + u];
    const int v = gset[118 + u];
    p.sound = v < 128 ? v : -1;
    p.chain = v >= 128 ? (v & 15) : -1;       // FM-1_093: 128 + repeats index << 4 + the next pattern
    p.repeats = v >= 128 ? kRepeats[(v >> 4) & 7] : 1;
    p.chainByte = v;
    auto P = emptySteps();
    std::copy_n(steps.begin(), std::min(steps.size(), P.size()), P.begin());
    if (full) {
        stepsFromBytes(p, P);
        p.raw = P;
        return p;
    }
    for (int i = 0; i < kSteps; ++i) {   // (before FM-1_092: ten note slots, 0xFF none; note values)
        Step st;
        const uint8_t* s = stepAt(P, i);
        for (int j = 0; j < 10 && int(st.notes.size()) < kMaxNotes; ++j)
            if (s[j] < 128) st.notes.push_back({s[j], clamp(s[20 + j], 1, 127), 0});
        st.rate = s[10] > 9 ? p.rate : s[10];
        p.steps[size_t(i)] = st;
    }
    return p;
}

std::array<uint8_t, 5> settingsBytes(const Pattern& p) {
    uint8_t chain;
    if (p.chain < 0) chain = p.chainByte >= 0 && p.chainByte < 128 ? uint8_t(p.chainByte) : 0;
    else {
        int idx = 0;
        for (int i = 0; i < 8; ++i) if (kRepeats[i] <= p.repeats) idx = i;
        chain = uint8_t(128 | (idx << 4) | (p.chain & 15));
    }
    return {uint8_t(clamp(p.gate, 5, 100)), uint8_t(clamp(p.swing, 50, 75)), uint8_t(clamp(p.rate, 0, 9)),
            uint8_t(clamp(p.length, 1, kSteps)), chain};
}

std::vector<uint8_t> stepBytes(const Pattern& pattern) {
    const Pattern p = normalise(pattern);
    if (p.raw.size() == size_t(kSteps * kStepBytes)) {   // what was read, if the steps are still what it holds
        Pattern was = p;
        stepsFromBytes(was, p.raw);
        if (sameSteps(normalise(was), p)) return p.raw;
    }
    return buildSteps(p);
}

Pattern fitToSynth(const Pattern& pattern) {
    Pattern p = normalise(pattern);
    const auto P = stepBytes(p);
    stepsFromBytes(p, P);
    p.raw = P;
    return p;
}

Bytes encodeStepsPart(const std::array<uint8_t, 5>& set, const uint8_t* steps128, int pat, int part, bool save) {
    std::vector<uint8_t> data(set.begin(), set.end());
    data.insert(data.end(), steps128, steps128 + 4 * kStepBytes);
    Bytes body = {0x22, uint8_t(pat & 15), uint8_t(part & 15), uint8_t(save ? 1 : 0)};
    const Bytes packed = pack87(data.data(), data.size());
    body.insert(body.end(), packed.begin(), packed.end());
    Bytes m = {0xF0, 0x43, 0x00, kSubId};
    m.insert(m.end(), body.begin(), body.end());
    m.push_back(ysum(body));
    m.push_back(0xF7);
    return m;
}

std::vector<Bytes> encodeWholeSteps(const Pattern& pattern, int pat, bool save) {
    const Pattern p = normalise(pattern);
    const auto P = stepBytes(p);
    const auto set = settingsBytes(p);
    std::vector<Bytes> out;
    for (int q = 0; q < 16; ++q) out.push_back(encodeStepsPart(set, P.data() + q * 4 * kStepBytes, pat, q, false));
    for (int e = 0; e < 8; ++e) out.push_back(encodeLocksPart(p.locks, pat, e, save && e == 7));
    return out;
}

uint32_t replyArg(const Bytes& m) {
    if (m.size() < 8) return 0;
    const uint32_t kind = m[4] == 0x22 ? 0x4000u : m[4] == 0x21 ? 0x8000u : 0u;
    return uint32_t(m[5] & 15) | (uint32_t(m[6]) << 8) | kind;
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
