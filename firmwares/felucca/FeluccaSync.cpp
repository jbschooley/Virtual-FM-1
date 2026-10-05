#include "FeluccaSync.h"

#include "Fm1Link.h"

namespace felucca {

namespace {

constexpr int kAsk = 400;         // ms: a reply comes in 10-50 ms (EDITOR_PROTOCOL.md)
constexpr int kFlash = 4000;      // a flash write (a commit, a project slot): up to about 2 s

void u32(std::vector<uint8_t>& a, uint32_t n) { for (int i = 0; i < 5; ++i) a.push_back(uint8_t((n >> (7 * i)) & 127u)); }
uint32_t r32(const std::vector<uint8_t>& a, size_t at) {
    return at + 5 > a.size() ? 0 : uint32_t(a[at]) | uint32_t(a[at + 1]) << 7 | uint32_t(a[at + 2]) << 14 | uint32_t(a[at + 3]) << 21 | uint32_t(a[at + 4]) << 28;
}
void v14(std::vector<uint8_t>& a, int v) { const int w = v + 8192; a.push_back(uint8_t(w & 127)); a.push_back(uint8_t((w >> 7) & 127)); }
int r14(const std::vector<uint8_t>& a, size_t at) { return at + 2 > a.size() ? 0 : (int(a[at]) | int(a[at + 1]) << 7) - 8192; }

uint32_t crc32(const std::vector<uint8_t>& b) {   // zlib's, as Felucca's st_crc32
    uint32_t c = 0xFFFFFFFFu;
    for (uint8_t v : b) { c ^= v; for (int k = 0; k < 8; ++k) c = (c >> 1) ^ ((c & 1u) ? 0xEDB88320u : 0u); }
    return ~c;
}

void pack7(std::vector<uint8_t>& a, const uint8_t* p, size_t n) {
    while (n) {
        const size_t k = n > 7 ? 7 : n;
        uint8_t mask = 0;
        for (size_t i = 0; i < k; ++i) mask = uint8_t(mask | ((p[i] >> 7) << i));
        a.push_back(mask);
        for (size_t i = 0; i < k; ++i) a.push_back(uint8_t(p[i] & 127u));
        p += k; n -= k;
    }
}

bool unpack7(const std::vector<uint8_t>& a, size_t at, std::vector<uint8_t>& out) {
    while (at < a.size()) {
        const uint8_t mask = a[at++];
        const size_t k = std::min<size_t>(7, a.size() - at);
        if (k == 0 || (mask >> k)) return false;
        for (size_t j = 0; j < k; ++j) out.push_back(uint8_t(a[at++] | (((mask >> j) & 1u) << 7)));
    }
    return true;
}

// the globals a mirror carries: tempo, swing, tuning, the effects (not the clock source, MIDI
// routing or the PROJECT / TOOLS actions, which are each synth's own)
bool mirroredGlobal(int id) { return id == 0 || id == 1 || (id >= 3 && id <= 11) || id == 24; }

}  // namespace

Bytes frame(int cmd, const std::vector<uint8_t>& args) {
    Bytes f = {0xF0, 0x7D, 0x46, 0x4C, uint8_t(cmd & 127)};
    for (auto b : args) f.push_back(uint8_t(b & 127));
    f.push_back(0xF7);
    return f;
}

int commandOf(const Bytes& f) {
    if (f.size() < 6 || f[0] != 0xF0 || f[1] != 0x7D || f[2] != 0x46 || f[3] != 0x4C || f.back() != 0xF7) return -1;
    return f[4];
}

std::vector<uint8_t> argsOf(const Bytes& f) {
    if (commandOf(f) < 0) return {};
    return std::vector<uint8_t>(f.begin() + 5, f.end() - 1);
}

bool isPush(int cmd) { return cmd == kChanged || cmd == kReload || cmd == kStepChanged || cmd == kTrackChanged; }

// ---- a real synth ----

LinkEndpoint::LinkEndpoint(Fm1Link& link) : link_(link) {
    link_.onSysex = [this](const Bytes& f) {
        if (!isPush(commandOf(f))) return;
        std::lock_guard<std::mutex> g(lock_);
        if (pushes_.size() < 4096) pushes_.push_back(f);
    };
}

LinkEndpoint::~LinkEndpoint() { link_.onSysex = nullptr; }

std::optional<Bytes> LinkEndpoint::ask(const Bytes& request, int timeoutMs) {
    const int cmd = commandOf(request);
    if (cmd < 0) { jassertfalse; return std::nullopt; }   // never anything but the editor protocol
    // once: a write sent twice is not the same as once (a backup piece at the wrong offset)
    return link_.ask<Bytes>(request, [cmd](const Bytes& f) -> std::optional<Bytes> {
        return commandOf(f) == cmd && !isPush(cmd) ? std::optional<Bytes>(f) : std::nullopt;
    }, timeoutMs, 1);
}

std::vector<Bytes> LinkEndpoint::pushes() {
    std::lock_guard<std::mutex> g(lock_);
    std::vector<Bytes> out(pushes_.begin(), pushes_.end());
    pushes_.clear();
    return out;
}

// ---- backup and restore ----

std::optional<Objects> backup(Endpoint& from, const Progress& progress, juce::String& error) {
    auto list = from.ask(frame(kBackupList), kFlash);   // (it stops the transport first)
    auto a = list ? argsOf(*list) : std::vector<uint8_t>{};
    if (a.size() < 3 || a[0] != 1) { error = "the synth did not list its objects (Felucca 1.0 or later has the full backup)"; return std::nullopt; }
    if (a[1] != 0) { error = a[1] == 3 ? "the synth could not stop playing" : "the synth could not list its objects (rc " + juce::String(a[1]) + ")"; return std::nullopt; }
    struct Entry { int id; uint32_t size, crc; };
    std::vector<Entry> entries;
    for (size_t i = 0; i < a[2]; ++i) {
        const size_t at = 3 + i * 11;
        if (at + 11 > a.size()) { error = "the synth's object list is incomplete"; return std::nullopt; }
        entries.push_back({a[at], r32(a, at + 1), r32(a, at + 6)});
    }
    uint32_t total = 0, done = 0;
    for (auto& e : entries) if (e.id <= 8) total += e.size;
    Objects out;
    for (auto& e : entries) {
        if (e.id > 8) continue;   // user sample slots: not ours to copy
        std::vector<uint8_t> bytes;
        while (bytes.size() < e.size) {
            const uint32_t n = std::min<uint32_t>(256, e.size - uint32_t(bytes.size()));
            std::vector<uint8_t> q = {uint8_t(e.id)};
            u32(q, uint32_t(bytes.size()));
            q.push_back(uint8_t(n & 127)); q.push_back(uint8_t(n >> 7));
            auto r = from.ask(frame(kBackupGet, q), kAsk);
            auto g = r ? argsOf(*r) : std::vector<uint8_t>{};
            if (g.size() < 9 || g[0] != e.id || g[1] != 0 || r32(g, 2) != bytes.size()) {
                error = "the synth stopped sending object " + juce::String(e.id) + (g.size() > 1 && g[1] == 5 ? " (it changed meanwhile: try again)" : "");
                return std::nullopt;
            }
            const size_t before = bytes.size();
            if (!unpack7(g, 9, bytes) || bytes.size() - before != n) { error = "object " + juce::String(e.id) + " arrived damaged"; return std::nullopt; }
            done += n;
            if (progress && !progress(int(done), int(total), "Reading Felucca's objects...")) { error = "cancelled"; return std::nullopt; }
        }
        if (crc32(bytes) != e.crc && e.size) { error = "object " + juce::String(e.id) + " changed while it was read: try again"; return std::nullopt; }
        out[e.id] = std::move(bytes);
    }
    if (out[0].empty() || out[1].empty()) { error = "the synth gave no music or settings"; return std::nullopt; }
    return out;
}

bool restore(Endpoint& to, const Objects& objects, const Progress& progress, juce::String& error) {
    uint32_t total = 0, done = 0;
    for (auto& [id, b] : objects) total += uint32_t(b.size());
    auto put = [&](const std::vector<uint8_t>& q, int timeout) -> int {
        auto r = to.ask(frame(kBackupPut, q), timeout);
        auto g = r ? argsOf(*r) : std::vector<uint8_t>{};
        return g.size() >= 3 ? int(g[2]) : -1;
    };
    auto say = [](int rc) {
        switch (rc) {
            case -1: return juce::String("no answer");
            case 2: return juce::String("it failed validation");
            case 3: return juce::String("the synth could not stop playing");
            case 4: return juce::String("its flash write failed");
            case 5: return juce::String("the synth started over");
            default: return "rc " + juce::String(rc);
        }
    };
    for (int id : {2, 3, 4, 5, 6, 7, 8, 1, 0}) {
        auto it = objects.find(id);
        if (it == objects.end()) continue;
        const auto& b = it->second;
        std::vector<uint8_t> q = {0, uint8_t(id)};
        u32(q, uint32_t(b.size()));
        u32(q, b.empty() ? 0u : crc32(b));
        if (int rc = put(q, kFlash); rc != 0) { error = "the synth refused object " + juce::String(id) + " (" + say(rc) + ")"; return false; }
        for (size_t off = 0; off < b.size(); off += 256) {
            const size_t n = std::min<size_t>(256, b.size() - off);
            std::vector<uint8_t> d = {1, uint8_t(id)};
            u32(d, uint32_t(off));
            pack7(d, b.data() + off, n);
            if (int rc = put(d, kAsk); rc != 0) { error = "the synth refused a piece of object " + juce::String(id) + " (" + say(rc) + ")"; return false; }
            done += uint32_t(n);
            if (progress && !progress(int(done), int(total), "Writing Felucca's objects...")) {
                put({3, uint8_t(id)}, kAsk);   // abort: nothing of it is written
                error = "cancelled";
                return false;
            }
        }
        if (int rc = put({2, uint8_t(id)}, kFlash); rc != 0) { error = "the synth did not take object " + juce::String(id) + " (" + say(rc) + ")"; return false; }
    }
    return true;
}

// ---- live ----

bool Mirror::start(juce::String& error) {
    for (Side* s : {&a_, &b_}) {
        auto w = s->ep.ask(frame(kWatch, {3}), kAsk);
        auto g = w ? argsOf(*w) : std::vector<uint8_t>{};
        if (g.empty() || !(g[0] & 1)) { error = "a synth did not start watching (Felucca 0.6 or later has live sync)"; return false; }
        auto t = s->ep.ask(frame(kTrack), kAsk);
        auto ta = t ? argsOf(*t) : std::vector<uint8_t>{};
        if (ta.empty()) { error = "a synth did not say which track is selected"; return false; }
        s->sel = ta[0];
        s->pinged = juce::Time::getMillisecondCounter();
        s->ep.pushes();   // what was pending before: not ours to carry
    }
    return true;
}

void Mirror::stop() {
    for (Side* s : {&a_, &b_}) s->ep.ask(frame(kWatch, {0}), kAsk);
}

bool Mirror::tick(juce::String& error) {
    const auto now = juce::Time::getMillisecondCounter();
    for (Side* s : {&a_, &b_}) {
        if (now - s->pinged >= 1000) {   // watching ends 3 s after the last request
            if (!s->ep.ask(frame(kPing), kAsk)) { error = s == &a_ ? "the synth stopped answering" : "the plugin's Felucca stopped answering"; return false; }
            s->pinged = now;
        }
        auto& echoes = s->echoes;
        echoes.erase(std::remove_if(echoes.begin(), echoes.end(), [now](auto& e) { return now - e.second > 2000; }), echoes.end());
    }
    for (auto [from, to] : {std::pair<Side*, Side*>{&a_, &b_}, std::pair<Side*, Side*>{&b_, &a_}})
        for (const auto& p : from->ep.pushes())
            if (!carry(*from, *to, p, error)) return false;
    return true;
}

// A push from one side, done on the other as its editor would do it. The editor's own
// SET / TRACK_PARAM / TRACK_STEP push nothing back; a load (PRESET) does push RELOAD, which
// is expected and dropped.
bool Mirror::carry(Side& from, Side& to, const Bytes& push, juce::String& error) {
    const int cmd = commandOf(push);
    const auto a = argsOf(push);
    auto must = [&](std::optional<Bytes> r, const char* what) {
        if (!r) error = juce::String("no answer to ") + what;
        return r.has_value();
    };
    if (cmd == kChanged && a.size() >= 4) {   // scope, id, value: of the selected track, or a global
        if (a[0] == 1) {
            if (!mirroredGlobal(a[1])) return true;
            std::vector<uint8_t> q = {1, a[1]};
            v14(q, r14(a, 2));
            return must(to.ep.ask(frame(kSet, q), kAsk), "SET");
        }
        std::vector<uint8_t> q = {uint8_t(from.sel), a[1]};
        v14(q, r14(a, 2));
        return must(to.ep.ask(frame(kTrackParam, q), kAsk), "TRACK_PARAM");
    }
    if (cmd == kTrackChanged && a.size() >= 4) {   // track, id, value: another track's mix
        std::vector<uint8_t> q = {a[0], a[1]};
        v14(q, r14(a, 2));
        return must(to.ep.ask(frame(kTrackParam, q), kAsk), "TRACK_PARAM");
    }
    if (cmd == kStepChanged && a.size() >= 2) {   // index, track
        auto r = from.ep.ask(frame(kTrackStep, {a[1], a[0]}), kAsk);
        if (!must(r, "TRACK_STEP")) return false;
        auto step = argsOf(*r);
        if (step.size() < 10) return true;
        return must(to.ep.ask(frame(kTrackStep, step), kAsk), "TRACK_STEP");
    }
    if (cmd == kReload && a.size() >= 3) {   // engine, preset, the selected track: a load or a new selection
        auto& echoes = from.echoes;
        for (auto it = echoes.begin(); it != echoes.end(); ++it)
            if (it->first == push) { echoes.erase(it); return true; }   // our own load, coming back
        from.sel = a[2];
        if (to.sel != a[2]) {
            if (!must(to.ep.ask(frame(kTrack, {a[2]}), kAsk), "TRACK")) return false;
            to.sel = a[2];
        }
        return copyTrackSound(from, to, a[2], error);
    }
    return true;
}

// A track's sound from one side to the other: its engine and preset (a load: the other side's
// RELOAD is expected), then every value that differs, then its FM6 patch.
bool Mirror::copyTrackSound(Side& from, Side& to, int track, juce::String& error) {
    auto dump = [&](Side& s) { auto r = s.ep.ask(frame(kTrackDump, {uint8_t(track)}), kAsk); return r ? argsOf(*r) : std::vector<uint8_t>{}; };
    const auto src = dump(from);
    if (src.size() < 5) { error = "no answer to TRACK_DUMP"; return false; }
    auto dst = dump(to);
    if (dst.size() < 3) { error = "no answer to TRACK_DUMP"; return false; }
    if (dst[1] != src[1] || dst[2] != src[2]) {
        if (!to.ep.ask(frame(kPreset, {src[1], src[2]}), kFlash)) { error = "no answer to PRESET"; return false; }
        to.echoes.push_back({frame(kReload, {src[1], src[2], uint8_t(track)}), juce::Time::getMillisecondCounter()});
        dst = dump(to);
    }
    for (size_t i = 3; i + 1 < src.size(); i += 2) {
        if (i + 1 < dst.size() && src[i] == dst[i] && src[i + 1] == dst[i + 1]) continue;
        std::vector<uint8_t> q = {uint8_t(track), uint8_t((i - 3) / 2), src[i], src[i + 1]};
        if (!to.ep.ask(frame(kTrackParam, q), kAsk)) { error = "no answer to TRACK_PARAM"; return false; }
    }
    auto p = from.ep.ask(frame(kFm6Get, {0, uint8_t(track)}), kAsk);
    auto pa = p ? argsOf(*p) : std::vector<uint8_t>{};
    if (pa.size() == 3 + 128 && pa[2] == 0) {
        std::vector<uint8_t> q = {0, uint8_t(track)};
        q.insert(q.end(), pa.begin() + 3, pa.end());
        if (!to.ep.ask(frame(kFm6Put, q), kAsk)) { error = "no answer to FM6_PUT"; return false; }
    }
    return true;
}

}  // namespace felucca
