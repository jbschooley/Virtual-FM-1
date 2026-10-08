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
        a.push_back(uint8_t(std::clamp(s.chance, 0, 100)));
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

bool lockable(int id) {   // P_LEVEL..P_LD_AMP 0..16, P_SGATE 32, P_DIST..P_REV 33..36, P_GLIDE 38, P_PAN 39,
                          // P_DETUNE 44, P_SLCR..P_SLDEPTH 45..48, P_TFLT 50, P_E0..P_E7 53..60
    return (id >= 0 && id <= 16) || id == 32 || (id >= 33 && id <= 36) || id == 38 || id == 39 || id == 44
        || (id >= 45 && id <= 48) || id == 50 || (id >= 53 && id <= 60);
}

Bytes lockSet(int track, int step, int param, int value) {
    std::vector<uint8_t> a = {uint8_t(track), uint8_t(step), uint8_t(param)};
    v14(a, value);
    return frame(kLockSet, a);
}
Bytes lockDelete(int track, int step, int param) { return frame(kLockSet, {uint8_t(track), uint8_t(step), uint8_t(param)}); }
Bytes microSet(int track, int step, int nudge) {
    return frame(kMicroSet, {uint8_t(track), uint8_t(step), uint8_t(std::clamp(nudge, kMicroMin, kMicroMax) + 64)});
}
Bytes fillSet(int track, int step, int cond) { return frame(kFillSet, {uint8_t(track), uint8_t(step), uint8_t(std::clamp(cond, 0, 2))}); }

bool readExtras(Endpoint& e, int track, TrackPattern& p) {
    if (!isSloop(e.dialect()) || e.extras == 0) return false;
    if (e.extras < 0) {   // INFO's last byte: its protocol (7: locks and nudges, 8: fill conditions); asked once
        auto info = e.ask(frame(kInfo), kAsk);
        const auto ia = info ? argsOf(*info) : std::vector<uint8_t>{};
        if (ia.empty()) return false;
        e.extras = ia.back() >= 8 ? 1 : 0;
        if (!e.extras) return false;
    }
    auto lk = e.ask(frame(kLockGet, {uint8_t(track)}), kAsk);
    auto mc = lk ? e.ask(frame(kMicroGet, {uint8_t(track)}), kAsk) : std::nullopt;
    auto fl = mc ? e.ask(frame(kFillGet, {uint8_t(track)}), kAsk) : std::nullopt;
    if (!fl) return false;
    const auto la = argsOf(*lk), ma = argsOf(*mc), fa = argsOf(*fl);
    if (la.size() < 2 || la[0] != track || ma.size() < 1 + size_t(kSteps) || ma[0] != track || fa.size() < 1 || fa[0] != track) return false;
    p.locks.clear();
    for (size_t i = 2, k = 0; k < la[1] && i + 3 < la.size(); i += 4, ++k) p.locks.push_back({la[i], la[i + 1], r14(la, i + 2)});
    std::sort(p.locks.begin(), p.locks.end());
    p.micro.assign(size_t(kSteps), 0);
    for (int i = 0; i < kSteps; ++i) p.micro[size_t(i)] = std::clamp(int(ma[size_t(1 + i)]) - 64, kMicroMin, kMicroMax);
    // the conditions: 2 bits a step, NSTEP / 4 bytes, pack7 (a top-bits byte, then up to 7 bytes)
    std::vector<uint8_t> packed;
    for (size_t i = 1; i < fa.size();) {
        const uint8_t top = fa[i++];
        for (int k = 0; k < 7 && i < fa.size() && packed.size() < size_t(kSteps / 4); ++k) packed.push_back(uint8_t(fa[i++] | ((top >> k) & 1) << 7));
    }
    if (packed.size() < size_t(kSteps / 4)) return false;
    p.fill.assign(size_t(kSteps), 0);
    for (int i = 0; i < kSteps; ++i) p.fill[size_t(i)] = int(packed[size_t(i / 4)] >> (2 * (i % 4))) & 3;
    for (auto& c : p.fill) if (c == 3) c = kFillAlways;   // (core.h: 3 plays as FC_NORM)
    p.extras = true;
    return true;
}

// what to send so that `have` (a side's extras) holds `want`'s, for the steps in [from, to)
static bool writeExtras(Endpoint& e, int track, const TrackPattern& want, const TrackPattern& have, int from, int to, juce::String& error) {
    for (int i = from; i < to; ++i) {
        if (have.micro[size_t(i)] != want.micro[size_t(i)] && !e.ask(microSet(track, i, want.micro[size_t(i)]), kAsk)) { error = "no answer to MICRO_SET"; return false; }
        if (have.fill[size_t(i)] != want.fill[size_t(i)] && !e.ask(fillSet(track, i, want.fill[size_t(i)]), kAsk)) { error = "no answer to FILL_SET"; return false; }
    }
    auto inRange = [&](const Lock& l) { return l.step >= from && l.step < to; };
    for (const auto& l : have.locks)   // (first the ones that go: the track has room for 24)
        if (inRange(l) && std::none_of(want.locks.begin(), want.locks.end(), [&](const Lock& w) { return w.step == l.step && w.param == l.param; })
            && !e.ask(lockDelete(track, l.step, l.param), kAsk)) { error = "no answer to LOCK_SET"; return false; }
    for (const auto& l : want.locks)
        if (inRange(l) && std::find(have.locks.begin(), have.locks.end(), l) == have.locks.end()) {
            auto r = e.ask(lockSet(track, l.step, l.param, l.value), kAsk);
            if (!r) { error = "no answer to LOCK_SET"; return false; }
            const auto a = argsOf(*r);
            if (a.size() >= 4 && a[3] == 3) { error = "the track has no room for another lock (24 at most)"; return false; }
        }
    return true;
}

bool copyStepExtras(Endpoint& from, Endpoint& to, int track, int step, juce::String& error) {
    TrackPattern a, b;
    if (!readExtras(from, track, a) || !readExtras(to, track, b)) return true;   // (a side without them: nothing to carry)
    return writeExtras(to, track, a, b, step, step + 1, error);
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
    readExtras(e, track, p);   // (SLOOP 2.4's locks, nudges and conditions, if it has them)
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
    if (p.extras) {   // SLOOP 2.4: to a side that has them too (what it holds: known, or read now)
        TrackPattern have;
        if (known && known->extras) have = *known;
        else if (!readExtras(e, track, have)) return true;
        if (!writeExtras(e, track, p, have, 0, kSteps, error)) return false;
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

// PATTERN: track, op (0 ask, 1 switch to a bank, 2 copy one to another) [, bank [, to]] ->
// track, op, rc, bank, the bank queued (127 none), NPAT. A switch while playing is queued.
std::optional<int> readPatternBank(Endpoint& e, int track) {
    auto r = e.ask(frame(kPatternCmd, {uint8_t(track), 0}), kAsk);
    const auto a = r ? argsOf(*r) : std::vector<uint8_t>{};
    if (a.size() < 6 || a[0] != track) return std::nullopt;
    return int(a[3]);
}
bool selectPatternBank(Endpoint& e, int track, int bank) {
    auto r = e.ask(frame(kPatternCmd, {uint8_t(track), 1, uint8_t(bank)}), kAsk);
    const auto a = r ? argsOf(*r) : std::vector<uint8_t>{};
    return a.size() >= 6 && a[0] == track && a[2] == 0 && a[3] == bank;
}

// BANK_SONG: op (0 ask, 1 set, 2 play, 3 stop) [, count, count x (4 banks, repeat)] -> op, rc,
// count, running, row, remaining, count x (4 banks, repeat)
std::optional<BankChain> readBankChain(Endpoint& e) {
    auto r = e.ask(frame(kBankSong, {0}), kAsk);
    const auto a = r ? argsOf(*r) : std::vector<uint8_t>{};
    if (a.size() < 6 || a[0] != 0) return std::nullopt;
    BankChain c;
    c.running = a[3] != 0;
    for (size_t i = 0; i < a[2] && 6 + 5 * i + 4 < a.size(); ++i) {
        BankRow row;
        for (int k = 0; k < 4; ++k) row.banks[size_t(k)] = a[6 + 5 * i + size_t(k)];
        row.repeat = a[6 + 5 * i + 4];
        c.rows.push_back(row);
    }
    return c;
}
Bytes bankChainWrite(const std::vector<BankRow>& rows) {
    std::vector<uint8_t> a = {1, uint8_t(rows.size())};
    for (const auto& row : rows) {
        for (int b : row.banks) a.push_back(uint8_t(b));
        a.push_back(uint8_t(row.repeat));
    }
    return frame(kBankSong, a);
}

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

// Copies the patterns (Felucca: and its chain and motion) between the two, from -> to
bool copySequencer(Endpoint& from, Endpoint& to, int tracks, juce::String& err, const Progress& progress) {
    const bool sloop = isSloop(from.dialect());
    std::optional<Chain> chain;
    if (!sloop) {   // Felucca: while a song chain plays, its steps are the chain's and a write is not taken
        chain = readChain(from);
        auto there = readChain(to);
        if (!chain || !there) { err = "no answer to SONG"; return false; }
        if (chain->running || there->running) { err = "a song chain is playing: stop it first"; return false; }
    }
    const bool melodee = isMelodee(from.dialect());
    if (melodee)   // Melodee: each track plays one of its 8 pattern banks; the other side's goes to the same first
        for (int t = 0; t < tracks; ++t) {
            const auto bank = readPatternBank(from, t), there = readPatternBank(to, t);
            if (!bank || !there) { err = "no answer to PATTERN"; return false; }
            if (*there != *bank && !selectPatternBank(to, t, *bank)) {
                err = "part " + juce::String(t + 1) + " could not switch to pattern " + juce::String(*bank + 1) + " (stop the transport first)";
                return false;
            }
        }
    if (!copyPatterns(from, to, tracks, err, false, progress)) return false;
    if (sloop) return true;   // (SLOOP's chain is in its settings: not carried)
    auto rc = [](const std::optional<fm1::Bytes>& r) { auto a = r ? argsOf(*r) : std::vector<uint8_t>{}; return a.size() >= 2 ? int(a[1]) : -1; };
    if (melodee) {   // its song: a bank for each track on each row (SONG would set every track to one)
        auto song = readBankChain(from);
        if (!song) { err = "no answer to BANK_SONG"; return false; }
        if (int c = rc(to.ask(bankChainWrite(song->rows), 4000)); c != 0) { err = c < 0 ? "no answer to BANK_SONG" : "the song was refused (rc " + juce::String(c) + ")"; return false; }
    } else if (int c = rc(to.ask(chainWrite(chain->rows), 4000)); c != 0) {
        err = c < 0 ? "no answer to SONG" : "the song chain was refused (rc " + juce::String(c) + ")";
        return false;
    }
    for (int t = 0; t < tracks; ++t) {
        auto src = readMotion(from, t), dst = readMotion(to, t);
        if (!src || !dst) { err = "no answer to MOTION"; return false; }
        if (src->events.size() == dst->events.size() && src->on == dst->on
            && std::equal(src->events.begin(), src->events.end(), dst->events.begin(),
                          [](auto& x, auto& y) { return x.step == y.step && x.param == y.param && x.value == y.value; }))
            continue;
        auto motion = [&](const fm1::Bytes& q) {
            const int c = rc(to.ask(q, 4000));
            if (c != 0) err = c < 0 ? juce::String("no answer to MOTION") : "motion was refused (rc " + juce::String(c) + (c == 2 ? ": its 64 events are taken" : "") + ")";
            return c == 0;
        };
        if (!motion(motionClear(t))) return false;
        for (auto& e : src->events)
            if (!motion(motionSet(t, e.step, e.param, e.value))) return false;
        if (!motion(motionOn(t, src->on))) return false;
    }
    return true;
}

}  // namespace felucca
