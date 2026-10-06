#include "FeluccaSeq.h"

namespace felucca {

namespace {
constexpr int kAsk = 400;
void v14(std::vector<uint8_t>& a, int v) { const int w = v + 8192; a.push_back(uint8_t(w & 127)); a.push_back(uint8_t((w >> 7) & 127)); }
int r14(const std::vector<uint8_t>& a, size_t at) { return at + 2 > a.size() ? 0 : (int(a[at]) | int(a[at + 1]) << 7) - 8192; }
}  // namespace

bool isSloop(const Dialect& d) { return d.drumStep >= 0; }
int stepBytes(const Dialect& d) { return isSloop(d) ? 11 : 12; }
bool isDrumTrack(const Dialect& d, int track) { return isSloop(d) && track == d.drumTrack; }

void DrumStep::set(int lane, bool hit, int level, int ratchet) {
    const uint32_t bit = 1u << lane, two = 3u << (2 * lane);
    on = hit ? (on | bit) : (on & ~bit);
    lvl = (lvl & ~two) | (uint32_t(hit ? level & 3 : 0) << (2 * lane));
    rat = (rat & ~two) | (uint32_t(hit ? ratchet & 3 : 0) << (2 * lane));
}

std::vector<uint8_t> stepArgs(const Dialect& d, const Step& s) {
    std::vector<uint8_t> a = {uint8_t(std::min(4, std::max(0, s.n)))};
    for (auto nt : s.note) a.push_back(uint8_t(nt & 127));
    a.push_back(uint8_t(s.time & 127));
    a.push_back(uint8_t(s.flags & 3));
    a.push_back(uint8_t(s.vel & 127));
    if (isSloop(d)) {
        a.push_back(uint8_t(s.lvl & 127));
        a.push_back(uint8_t(((s.lvl >> 7) & 1) | (((s.rat >> 7) & 1) << 1)));
        a.push_back(uint8_t(s.rat & 127));
    } else {
        a.push_back(uint8_t(s.hit & 127));
        a.push_back(uint8_t(s.acc & 127));
        a.push_back(uint8_t(((s.hit >> 7) & 1) | (((s.acc >> 7) & 1) << 1)));
        a.push_back(uint8_t(s.chance & 127));
    }
    return a;
}

std::optional<Step> stepFrom(const Dialect& d, const std::vector<uint8_t>& b) {
    if (int(b.size()) < stepBytes(d)) return std::nullopt;
    Step s;
    s.n = std::min<int>(4, b[0]);
    for (int k = 0; k < 4; ++k) s.note[size_t(k)] = b[size_t(1 + k)];
    s.time = b[5];
    s.flags = b[6];
    s.vel = b[7];
    if (isSloop(d)) {
        s.lvl = b[8] | ((b[9] & 1) << 7);
        s.rat = b[10] | (((b[9] >> 1) & 1) << 7);
    } else {
        s.hit = b[8] | ((b[10] & 1) << 7);
        s.acc = b[9] | (((b[10] >> 1) & 1) << 7);
        s.chance = b[11];
    }
    return s;
}

std::vector<uint8_t> drumArgs(const DrumStep& s) {
    std::vector<uint8_t> a = {uint8_t(s.on & 127), uint8_t((s.on >> 7) & 127), uint8_t((s.on >> 14) & 3)};
    for (int i = 0; i < 5; ++i) a.push_back(uint8_t((s.lvl >> (7 * i)) & 127));
    for (int i = 0; i < 5; ++i) a.push_back(uint8_t((s.rat >> (7 * i)) & 127));
    return a;
}

std::optional<DrumStep> drumFrom(const std::vector<uint8_t>& b) {
    if (b.size() < 13) return std::nullopt;
    DrumStep s;
    s.on = uint32_t(b[0]) | uint32_t(b[1]) << 7 | uint32_t(b[2] & 3) << 14;
    for (int i = 0; i < 5; ++i) s.lvl |= uint32_t(b[size_t(3 + i)]) << (7 * i);
    for (int i = 0; i < 5; ++i) s.rat |= uint32_t(b[size_t(8 + i)]) << (7 * i);
    return s;
}

Bytes stepWrite(const Dialect& d, int track, int index, const Step& s) {
    std::vector<uint8_t> a = {uint8_t(track), uint8_t(index)};
    auto b = stepArgs(d, s);
    a.insert(a.end(), b.begin(), b.end());
    return frame(kTrackStep, a);
}

Bytes drumWrite(const Dialect& d, int index, const DrumStep& s) {
    std::vector<uint8_t> a = {uint8_t(index)};
    auto b = drumArgs(s);
    a.insert(a.end(), b.begin(), b.end());
    return frame(d.drumStep, a);
}

Bytes paramWrite(int track, int id, int value) {
    std::vector<uint8_t> a = {uint8_t(track), uint8_t(id)};
    v14(a, value);
    return frame(kTrackParam, a);
}

std::optional<Step> readStep(Endpoint& e, int track, int index) {
    auto r = e.ask(frame(kTrackStep, {uint8_t(track), uint8_t(index)}), kAsk);
    if (!r) return std::nullopt;
    auto a = argsOf(*r);
    if (a.size() < 2 || a[0] != track || a[1] != index) return std::nullopt;
    return stepFrom(e.dialect(), std::vector<uint8_t>(a.begin() + 2, a.end()));
}

std::optional<DrumStep> readDrumStep(Endpoint& e, int index) {
    const auto& d = e.dialect();
    if (!isSloop(d)) return std::nullopt;
    auto r = e.ask(frame(d.drumStep, {uint8_t(index)}), kAsk);
    if (!r) return std::nullopt;
    auto a = argsOf(*r);
    if (a.size() < 14 || a[0] != index) return std::nullopt;
    return drumFrom(std::vector<uint8_t>(a.begin() + 1, a.end()));
}

std::optional<int> readParam(Endpoint& e, int track, int id) {
    auto r = e.ask(frame(kTrackParam, {uint8_t(track), uint8_t(id)}), kAsk);
    if (!r) return std::nullopt;
    auto a = argsOf(*r);
    if (a.size() < 4 || a[0] != track || a[1] != id) return std::nullopt;
    return r14(a, 2);
}

std::optional<TrackPattern> readPattern(Endpoint& e, int track, juce::String& error, int upTo) {
    TrackPattern p;
    int* fields[4] = {&p.len, &p.div, &p.swing, &p.gate};
    const int ids[4] = {kLen, kDiv, kSwing, kGate};
    for (int i = 0; i < 4; ++i) {
        auto v = readParam(e, track, ids[i]);
        if (!v) { error = "no answer to TRACK_PARAM"; return std::nullopt; }
        *fields[i] = *v;
    }
    const int n = std::min(upTo, kSteps);
    const bool drums = isDrumTrack(e.dialect(), track);
    for (int i = 0; i < n; ++i) {
        if (drums) {
            auto s = readDrumStep(e, i);
            if (!s) { error = "no answer to DRUM_STEP"; return std::nullopt; }
            p.drums.push_back(*s);
        } else {
            auto s = readStep(e, track, i);
            if (!s) { error = "no answer to TRACK_STEP"; return std::nullopt; }
            p.steps.push_back(*s);
        }
    }
    return p;
}

bool writePattern(Endpoint& e, int track, const TrackPattern& p, juce::String& error, const TrackPattern* known) {
    const auto& d = e.dialect();
    const int values[4] = {p.len, p.div, p.swing, p.gate}, ids[4] = {kLen, kDiv, kSwing, kGate};
    const int knownValues[4] = {known ? known->len : -9999, known ? known->div : -9999, known ? known->swing : -9999, known ? known->gate : -9999};
    for (int i = 0; i < 4; ++i)
        if (values[i] != knownValues[i] && !e.ask(paramWrite(track, ids[i], values[i]), kAsk)) { error = "no answer to TRACK_PARAM"; return false; }
    for (size_t i = 0; i < p.steps.size(); ++i) {
        if (known && i < known->steps.size() && known->steps[i] == p.steps[i]) continue;
        if (!e.ask(stepWrite(d, track, int(i), p.steps[i]), kAsk)) { error = "no answer to TRACK_STEP"; return false; }
    }
    for (size_t i = 0; i < p.drums.size(); ++i) {
        if (known && i < known->drums.size() && known->drums[i] == p.drums[i]) continue;
        if (!e.ask(drumWrite(d, int(i), p.drums[i]), kAsk)) { error = "no answer to DRUM_STEP"; return false; }
    }
    return true;
}

bool copyPatterns(Endpoint& from, Endpoint& to, int tracks, juce::String& error, bool onlyLength, const Progress& progress) {
    for (int t = 0; t < tracks; ++t) {
        int upTo = kSteps;
        if (onlyLength) {
            auto len = readParam(from, t, kLen);
            if (!len) { error = "no answer to TRACK_PARAM"; return false; }
            upTo = std::clamp(*len, 1, kSteps);
        }
        auto src = readPattern(from, t, error, upTo);
        if (!src) return false;
        auto dst = readPattern(to, t, error, upTo);
        if (!dst) return false;
        if (!writePattern(to, t, *src, error, &*dst)) return false;
        if (progress && !progress(t + 1, tracks, "Copying the patterns...")) { error = "cancelled"; return false; }
    }
    return true;
}

std::optional<Chain> readChain(Endpoint& e) {
    auto r = e.ask(frame(kSong, {0}), kAsk);
    if (!r) return std::nullopt;
    auto a = argsOf(*r);
    if (a.size() < 6) return std::nullopt;
    Chain c;
    c.running = a[3] != 0;
    c.row = a[4];
    c.remaining = a[5];
    for (size_t i = 0; i < a[2] && 6 + 2 * i + 1 < a.size(); ++i) c.rows.push_back({a[6 + 2 * i], a[7 + 2 * i]});
    return c;
}

Bytes chainWrite(const std::vector<std::pair<int, int>>& rows) {
    std::vector<uint8_t> a = {1, uint8_t(rows.size())};
    for (auto [slot, rep] : rows) { a.push_back(uint8_t(slot)); a.push_back(uint8_t(rep)); }
    return frame(kSong, a);
}

Bytes chainPlay(bool play) { return frame(kSong, {uint8_t(play ? 2 : 3)}); }

std::optional<Motion> readMotion(Endpoint& e, int track) {
    auto r = e.ask(frame(kMotion, {uint8_t(track)}), kAsk);
    if (!r) return std::nullopt;
    auto a = argsOf(*r);
    if (a.size() < 5 || a[0] != track) return std::nullopt;
    Motion m;
    m.on = a[2] != 0;
    m.max = a[4];
    for (size_t i = 5; i + 3 < a.size(); i += 4) m.events.push_back({a[i], a[i + 1], r14(a, i + 2)});
    return m;
}

Bytes motionOn(int track, bool on) { return frame(kMotion, {uint8_t(track), 1, uint8_t(on ? 1 : 0)}); }
Bytes motionClear(int track) { return frame(kMotion, {uint8_t(track), 2}); }
Bytes motionSet(int track, int step, int param, int value) {
    std::vector<uint8_t> a = {uint8_t(track), 3, uint8_t(step), uint8_t(param)};
    v14(a, value);
    return frame(kMotion, a);
}
Bytes motionDelete(int track, int step, int param) { return frame(kMotion, {uint8_t(track), 4, uint8_t(step), uint8_t(param)}); }

// Felucca 1.0.2's motion.c motion_param: the sound's parameters only
bool motionParam(int id) {
    return id >= 0 && id < 91 && (id <= 4 || (id >= 5 && id <= 16) || (id >= 33 && id <= 36) || id == 38 || id == 39
                                  || id == 44 || (id >= 61 && id <= 80) || id >= 83);
}

}  // namespace felucca
