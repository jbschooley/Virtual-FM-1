#include "FeluccaSync.h"
#include "FeluccaSeq.h"

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

}  // namespace

// The globals a mirror carries: tempo, swing, tuning, the effects (not the clock source, MIDI
// routing or the PROJECT / TOOLS actions, which are each synth's own). G_* 0..11 are numbered
// alike in both; Felucca's 24 is the reverb type, SLOOP's 25..29 its drum level and reverb and
// master bus (DUST, DUCK, FILT).
const Dialect& feluccaDialect() {
    // objects 0..9 (1.0.3: 8 the retired FM6 bank, always empty there, 9 the user presets' FM6
    // patches; 1.0.2: 0..8; 1.0 and 1.0.1: 0..7): 6 and 7 before 8 (an old archive's bank becomes
    // the presets' patches on 1.0.3), the music last
    static const Dialect d{"Felucca", kBackupList, kBackupGet, kBackupPut, true,
                           {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 32, 33, 34}, {2, 3, 4, 5, 6, 7, 8, 9, 1, 0}, 9, true, "felucca-backup",
                           {0, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 24}};
    return d;
}

// SLOOP 2.3's editor.c: BK_LIST 34, BK_GET 35, BK_PUT 36 (v6), objects 0..7 (no FM6 bank); its web
// editor restores 6, 7, 2..5, 0, 1 (editor.html BK.RESTORE). Its drum track (3) pushes STEP_CHANGED
// for a lane edit, but TRACK_STEP there is a four-lane view without levels or ratchets: DRUM_STEP
// (33: index -> index, lanes 3, levels 5, ratchets 5; the same args write them) carries it whole.
const Dialect& sloopDialect() {
    // 2.4: object 8, its FM6 patch bank (restored before the projects that name its slots), and a
    // fourth user sample slot (35)
    static const Dialect d{"SLOOP", 34, 35, 36, false,
                           {0, 1, 2, 3, 4, 5, 6, 7, 8, 32, 33, 34, 35}, {6, 7, 8, 2, 3, 4, 5, 0, 1}, 8, true, "sloop-backup",
                           {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 25, 26, 27, 28, 29, 30}, 3, 33};
    return d;
}

static bool mirroredGlobal(const Dialect& d, int id) {
    return std::find(d.mirroredGlobals.begin(), d.mirroredGlobals.end(), id) != d.mirroredGlobals.end();
}

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

// ---- the full backup as a file (Felucca's web editor: web/fm1backup.js; SLOOP's: editor.html) ----

bool readBackup(const juce::File& f, Objects& out, juce::String& error, const Dialect& d) {
    out.clear();
    auto v = juce::JSON::parse(f.loadFileAsString());
    auto* objs = v.getProperty("objects", {}).getArray();
    const auto& ids = d.ids;
    const bool sloop = isSloop(d);
    // (a Felucca archive: 13 objects from 1.0.3, 12 of 1.0.2 (no 9), 11 before FM6 (no 8 either);
    // SLOOP's editor writes the objects it has, and needs 0 and 1)
    const bool complete = objs && (sloop || (objs->size() >= int(ids.size()) - 2 && objs->size() <= int(ids.size())));
    // Melodee's editor writes "felucca-backup" files too, with its own objects inside: never restored here
    if (v.getProperty("firmware", {}).toString().containsIgnoreCase("MELODEE")) {
        error = "a Melodee backup, which the plugin does not support yet";
        return false;
    }
    if (v.getProperty("format", {}).toString() != d.fileFormat || int(v.getProperty("version", 0)) != 1 || !complete) {
        error = juce::String("not a complete ") + d.name + " backup";
        return false;
    }
    for (const auto& o : *objs) {
        const int id = o.getProperty("id", -1), size = o.getProperty(sloop ? "len" : "size", -1);
        const auto crc = uint32_t(juce::int64(o.getProperty("crc", -1)));
        juce::MemoryOutputStream data;
        if (std::find(ids.begin(), ids.end(), id) == ids.end() || size < 0 || size > (id >= 32 ? 81920 : 3840)
            || !juce::Base64::convertFromBase64(data, o.getProperty("data", {}).toString()) || int(data.getDataSize()) != size) {
            error = "object " + juce::String(id) + " is damaged";
            return false;
        }
        const auto* b = static_cast<const uint8_t*>(data.getData());
        std::vector<uint8_t> bytes(b, b + data.getDataSize());
        if (crc32(bytes) != crc) {
            error = "object " + juce::String(id) + " fails its checksum";
            return false;
        }
        out[id] = std::move(bytes);
    }
    if (out[0].empty() || out[1].empty()) {
        error = "the backup has no music or settings";
        return false;
    }
    return true;
}

juce::String backupJson(const Objects& objects, const juce::String& firmware, const Dialect& d) {
    const bool sloop = isSloop(d);
    juce::Array<juce::var> list;
    for (int id : d.ids) {
        auto it = objects.find(id);
        const std::vector<uint8_t> none;
        const auto& b = it != objects.end() ? it->second : none;
        auto* o = new juce::DynamicObject();
        if (sloop && id >= 32 && it == objects.end()) continue;   // (SLOOP: a sample slot not read is left out)
        o->setProperty("id", id);
        o->setProperty(sloop ? "len" : "size", int(b.size()));
        o->setProperty("crc", b.empty() ? juce::int64(0) : juce::int64(crc32(b)));
        o->setProperty("data", juce::Base64::toBase64(b.data(), b.size()));
        list.add(juce::var(o));
    }
    auto* root = new juce::DynamicObject();
    root->setProperty("format", juce::String(d.fileFormat));
    root->setProperty("version", 1);
    root->setProperty("firmware", firmware);
    root->setProperty(sloop ? "date" : "created", juce::Time::getCurrentTime().toISO8601(true));
    root->setProperty("objects", list);
    return juce::JSON::toString(juce::var(root), false);
}

// ---- a real synth ----

LinkEndpoint::LinkEndpoint(Fm1Link& link, const Dialect& d) : link_(link), dialect_(d) {
    link_.setSysexListener([this](const Bytes& f) {
        if (!isPush(commandOf(f))) return;
        std::lock_guard<std::mutex> g(lock_);
        if (pushes_.size() < 4096) pushes_.push_back(f);
    });
}

LinkEndpoint::~LinkEndpoint() { link_.setSysexListener(nullptr); }

std::optional<Bytes> LinkEndpoint::ask(const Bytes& request, int timeoutMs) {
    const int cmd = commandOf(request);
    if (cmd < 0) { jassertfalse; return std::nullopt; }   // never anything but the editor protocol
    // once: a write sent twice is not the same as once (a backup piece at the wrong offset)
    // the reply repeats what names the request (SET / TRACK_PARAM: scope or track and id; TRACK_STEP:
    // track and index; SLOOP's DRUM_STEP: index; TRACK: the track selected): a late reply to an
    // earlier request of the same command (asked again after no answer) is not taken for this one's
    const auto q = argsOf(request);
    // (SLOOP 2.4: LOCK_SET track, step, param; MICRO_SET / FILL_SET track, step; their GETs the track)
    size_t echo = cmd == kSet || cmd == kTrackParam || cmd == kTrackStep ? 2 : cmd == kTrack || cmd == dialect_.drumStep ? 1 : 0;
    if (isSloop(dialect_))
        echo = cmd == kLockSet ? 3 : cmd == kMicroSet || cmd == kFillSet ? 2 : cmd == kLockGet || cmd == kMicroGet || cmd == kFillGet ? 1 : echo;
    echo = std::min(echo, q.size());
    return link_.ask<Bytes>(request, [cmd, q, echo](const Bytes& f) -> std::optional<Bytes> {
        if (commandOf(f) != cmd || isPush(cmd)) return std::nullopt;
        const auto r = argsOf(f);
        if (r.size() < echo || !std::equal(q.begin(), q.begin() + long(echo), r.begin())) return std::nullopt;
        return f;
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
    const auto& d = from.dialect();
    auto list = from.ask(frame(d.backupList), kFlash);   // (Felucca stops the transport first)
    auto a = list ? argsOf(*list) : std::vector<uint8_t>{};
    const size_t h = d.listVersion ? 1 : 0;   // Felucca: version 1, rc, count; SLOOP: rc, count
    if (a.size() < h + 2 || (d.listVersion && a[0] != 1))
        { error = d.listVersion ? "the synth did not list its objects (Felucca 1.0 or later has the full backup)" : "the synth did not list its objects"; return std::nullopt; }
    if (a[h] != 0) { error = a[h] == 3 ? "the synth could not stop playing" : "the synth could not list its objects (rc " + juce::String(a[h]) + ")"; return std::nullopt; }
    struct Entry { int id; uint32_t size, crc; };
    std::vector<Entry> entries;
    for (size_t i = 0; i < a[h + 1]; ++i) {
        const size_t at = h + 2 + i * 11;
        if (at + 11 > a.size()) { error = "the synth's object list is incomplete"; return std::nullopt; }
        entries.push_back({a[at], r32(a, at + 1), r32(a, at + 6)});
    }
    uint32_t total = 0, done = 0;
    for (auto& e : entries) if (e.id <= d.lastObject) total += e.size;
    Objects out;
    for (auto& e : entries) {
        if (e.id > d.lastObject) continue;   // user sample slots: not ours to copy
        std::vector<uint8_t> bytes;
        while (bytes.size() < e.size) {
            const uint32_t n = std::min<uint32_t>(256, e.size - uint32_t(bytes.size()));
            std::vector<uint8_t> q = {uint8_t(e.id)};
            u32(q, uint32_t(bytes.size()));
            q.push_back(uint8_t(n & 127)); q.push_back(uint8_t(n >> 7));
            auto r = from.ask(frame(d.backupGet, q), kAsk);
            auto g = r ? argsOf(*r) : std::vector<uint8_t>{};
            if (g.size() < 9 || g[0] != e.id || g[1] != 0 || r32(g, 2) != bytes.size()) {
                error = "the synth stopped sending object " + juce::String(e.id) + (g.size() > 1 && g[1] == 5 ? " (it changed meanwhile: try again)" : "");
                return std::nullopt;
            }
            const size_t before = bytes.size();
            if (!unpack7(g, 9, bytes) || bytes.size() - before != n) { error = "object " + juce::String(e.id) + " arrived damaged"; return std::nullopt; }
            done += n;
            if (progress && !progress(int(done), int(total), juce::String("Reading ") + d.name + "'s objects...")) { error = "cancelled"; return std::nullopt; }
        }
        if (crc32(bytes) != e.crc && e.size) { error = "object " + juce::String(e.id) + " changed while it was read: try again"; return std::nullopt; }
        out[e.id] = std::move(bytes);
    }
    if (out[0].empty() || out[1].empty()) { error = "the synth gave no music or settings"; return std::nullopt; }
    return out;
}

// What a BACKUP_PUT piece carries. Felucca takes up to 256 bytes, but on macOS JUCE sends a SysEx
// message as UMPs in event lists of one packet (64 words: 192 bytes of SysEx at most), so a longer
// one goes out in several MIDISendEventList calls, and an FM-1 running Felucca 1.0 refused such
// split pieces (19 of 20 rounds; sent in one call, 0 of 80). A 128-byte piece (about 160 bytes as
// sent) fits in one call.
static constexpr size_t kPutPiece = 128;

bool restore(Endpoint& to, const Objects& objects, const Progress& progress, juce::String& error) {
    const auto& dl = to.dialect();
    uint32_t total = 0, done = 0;
    for (auto& [id, b] : objects) total += uint32_t(b.size());
    auto put = [&](const std::vector<uint8_t>& q, int timeout) -> int {
        auto r = to.ask(frame(dl.backupPut, q), timeout);
        auto g = r ? argsOf(*r) : std::vector<uint8_t>{};
        // the reply names what it answers (step, object): another one's (late) is not this one's
        return g.size() >= 3 && g[0] == q[0] && g[1] == q[1] ? int(g[2]) : -1;
    };
    auto say = [&dl](int rc) {
        switch (rc) {
            case -1: return juce::String("no answer");
            case 2: return juce::String("it failed validation");
            case 3: return juce::String(dl.listVersion ? "the synth could not stop playing" : "the synth is playing: stop it first");
            case 4: return juce::String("its flash write failed");
            case 5: return juce::String("the synth started over");
            default: return "rc " + juce::String(rc);
        }
    };
    for (int id : dl.restoreOrder) {
        auto it = objects.find(id);
        if (it == objects.end()) continue;
        const auto& b = it->second;
        // A piece the synth refused (rc 1: it arrived incomplete, as when its MIDI queue was full)
        // or did not answer, or a commit that failed validation (rc 2: the last piece came in
        // short, and was taken) or was not answered: the object starts over, up to three times.
        // Nothing of it is written before a commit that passes, a refused piece never moves the
        // synth's write position, and writing an object again writes the same.
        const uint32_t doneBefore = done;
        int pieceRc = 0, commitRc = 0, attempts = 0;
        size_t failedAt = 0;
        for (int attempt = 0; attempt < 3; ++attempt) {
            if (attempt > 0) put({3, uint8_t(id)}, kAsk);   // abort what was staged
            done = doneBefore;
            attempts = attempt + 1;
            std::vector<uint8_t> q = {0, uint8_t(id)};
            u32(q, uint32_t(b.size()));
            u32(q, b.empty() ? 0u : crc32(b));
            if (int rc = put(q, kFlash); rc != 0) { error = "the synth refused object " + juce::String(id) + " (" + say(rc) + ")"; return false; }
            pieceRc = 0;
            for (size_t off = 0; off < b.size() && pieceRc == 0; off += kPutPiece) {
                const size_t n = std::min<size_t>(kPutPiece, b.size() - off);
                std::vector<uint8_t> d = {1, uint8_t(id)};
                u32(d, uint32_t(off));
                pack7(d, b.data() + off, n);
                if ((pieceRc = put(d, kAsk)) != 0) { failedAt = off; break; }
                done += uint32_t(n);
                if (progress && !progress(int(done), int(total), juce::String("Writing ") + dl.name + "'s objects...")) {
                    put({3, uint8_t(id)}, kAsk);   // abort: nothing of it is written
                    error = "cancelled";
                    return false;
                }
            }
            if (pieceRc == 0) {
                commitRc = put({2, uint8_t(id)}, kFlash);
                if (commitRc != 2 && commitRc != -1) break;   // taken, or a failure starting over cannot mend
                continue;
            }
            if (pieceRc != 1 && pieceRc != -1) break;
        }
        if (pieceRc != 0) {
            put({3, uint8_t(id)}, kAsk);
            error = "the synth refused a piece of object " + juce::String(id) + " (" + say(pieceRc) + ", at byte "
                    + juce::String(int(failedAt)) + ", " + juce::String(attempts) + (attempts == 1 ? " try)" : " tries)");
            return false;
        }
        if (commitRc != 0) {
            error = "the synth did not take object " + juce::String(id) + " (" + say(commitRc) + ", "
                    + juce::String(attempts) + (attempts == 1 ? " try)" : " tries)");
            return false;
        }
    }
    return true;
}

// ---- live ----

// INFO: version string, NENGINES, P_COUNT, G_COUNT, NSTEP, P_E0, NENGINES engine names, NTRK,
// CHAIN_ROWS, then tagged blocks (tag, 01, a payload of a length the tag gives); older firmware ends
// earlier. 53 01 caps: the live-sync capabilities.
uint8_t liveCaps(const std::vector<uint8_t>& a) {
    size_t k = 0;
    auto string = [&] { while (k < a.size() && a[k] != 0) ++k; ++k; };
    string();                                  // the version
    if (k + 5 > a.size()) return 0;
    const int engines = a[k];
    k += 5;
    for (int e = 0; e < engines && k < a.size(); ++e) string();
    k += 2;                                    // NTRK, CHAIN_ROWS
    while (k + 2 <= a.size()) {
        const uint8_t tag = a[k];
        size_t len = 0;
        switch (tag) {
            case 0x55: case 0x42: case 0x53: case 0x50: len = 1; break;   // UI caps, backup caps, live sync caps, FM6 v2 (1.0.3)
            case 0x4D: case 0x46: len = 2; break;              // motion, FM6 patches (factory, bank count)
            default: return 0;                                 // a block this does not know: stop
        }
        if (a[k + 1] != 1 || k + 2 + len > a.size()) return 0;
        if (tag == 0x53) return a[k + 2];
        k += 2 + len;
    }
    return 0;
}

// INFO: version string, then NENGINES, P_COUNT, G_COUNT, NSTEP, P_E0 (and its live-sync caps)
static std::optional<std::vector<uint8_t>> layout(Endpoint& ep, uint8_t* caps = nullptr) {
    auto r = ep.ask(frame(kInfo), kAsk);
    if (!r) return std::nullopt;
    auto a = argsOf(*r);
    size_t at = 0;
    while (at < a.size() && a[at] != 0) ++at;
    if (at + 6 > a.size()) return std::nullopt;
    if (caps) *caps = liveCaps(a);
    return std::vector<uint8_t>(a.begin() + long(at) + 1, a.begin() + long(at) + 6);
}

bool Mirror::start(juce::String& error) {
    // values go by parameter number: both must number them alike (the same Felucca version)
    auto la = layout(a_.ep, &a_.caps), lb = layout(b_.ep, &b_.caps);
    if (!la || !lb) { error = "a synth did not say what it is"; return false; }
    if ((*la)[1] != (*lb)[1] || (*la)[2] != (*lb)[2] || (*la)[4] != (*lb)[4]) {
        error = "the FM-1 runs another version of " + juce::String(a_.ep.dialect().name) + " than the plugin (" + juce::String((*la)[1]) + " parameters, "
                "the plugin's " + juce::String((*lb)[1]) + "); pull or send still work";
        return false;
    }
    for (Side* s : {&a_, &b_}) {
        auto w = s->ep.ask(frame(kWatch, {3}), kAsk);
        auto g = w ? argsOf(*w) : std::vector<uint8_t>{};
        if (g.empty() || !(g[0] & 1)) { error = "a synth did not start watching"; return false; }
        auto t = s->ep.ask(frame(kTrack), kAsk);
        auto ta = t ? argsOf(*t) : std::vector<uint8_t>{};
        if (ta.empty()) { error = "a synth did not say which track is selected"; return false; }
        s->sel = ta[0];
        if (auto d = s->ep.ask(frame(kTrackDump, {uint8_t(s->sel)}), kAsk); d && argsOf(*d).size() >= 3)   // (engine, preset)
            s->lastReload = frame(kReload, {argsOf(*d)[1], argsOf(*d)[2], uint8_t(s->sel)});
        s->pinged = juce::Time::getMillisecondCounter();
        s->ep.pushes();   // what was pending before: not ours to carry
    }
    return true;
}

// A request asked again once if no answer comes: another program talking to the FM-1 at the same
// moment (its web editor, a second instance) can make it drop one. Only what may be sent twice and
// whose reply names what it answers (LinkEndpoint::ask), so a late reply to the first try is never
// taken for another request's: PING / WATCH, TRACK, and the step and value reads and writes (SET,
// TRACK_PARAM, TRACK_STEP, DRUM_STEP).
static std::optional<Bytes> askAgain(Endpoint& ep, const Bytes& request) {
    if (auto r = ep.ask(request, kAsk)) return r;
    return ep.ask(request, kAsk);
}

bool Mirror::forward(const Bytes& request, juce::String& error) {
    if (a_.ep.ask(request, kFlash)) return true;
    error = "the synth did not take an edit";
    return false;
}

// Felucca: whether a song chain plays (SONG) and whether the selected track's motion plays
// (MOTION): neither is pushed. About once a second.
void Mirror::poll(Side& s) {
    if (isSloop(s.ep.dialect()) || !s.ep.dialect().fm6) return;
    chainPlays(s);
    if (auto m = readMotion(s.ep, s.sel)) {
        s.selMotion.clear();
        if (m->on)
            for (auto& e : m->events) s.selMotion.push_back(e.param);
    }
}

// A chain starting swaps the steps in at once and pushes them all (and LEN..GATE) before a poll
// would see it, and stopping pushes the pattern back at once: so a step or pattern push from a
// Felucca side asks each time (one request)
bool Mirror::chainPlays(Side& s) {
    if (isSloop(s.ep.dialect()) || !s.ep.dialect().fm6) return false;
    if (auto c = readChain(s.ep)) s.chainRunning = c->running;
    return s.chainRunning;
}

void Mirror::stop() {
    for (Side* s : {&a_, &b_}) s->ep.ask(frame(kWatch, {0}), kAsk);
}

bool Mirror::tick(juce::String& error) {
    const auto now = juce::Time::getMillisecondCounter();
    for (Side* s : {&a_, &b_}) {
        // Watching ends 3 s (of the synth's clock) after its last request: a PING keeps it. After a
        // longer gap (a slow tick, a host rendering faster than real time) it may have ended, and
        // only WATCH starts it again (which takes what the synth has now as known: what changed in
        // the gap is not carried).
        const bool lapsed = now - s->pinged > 2000;
        if (now - s->pinged >= 250) {
            bool ok;
            if (lapsed) {
                auto w = askAgain(s->ep, frame(kWatch, {3}));
                auto g = w ? argsOf(*w) : std::vector<uint8_t>{};
                ok = !g.empty() && (g[0] & 1);
            } else {
                ok = askAgain(s->ep, frame(kPing)).has_value();
            }
            if (!ok) { error = s == &a_ ? juce::String("the synth stopped answering") : "the plugin's " + juce::String(s->ep.dialect().name) + " stopped answering"; return false; }
            s->pinged = now;
        }
        if (now - s->polled >= 1000) { s->polled = now; poll(*s); }
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
            if (!mirroredGlobal(to.ep.dialect(), a[1])) return true;
            std::vector<uint8_t> q = {1, a[1]};
            v14(q, r14(a, 2));
            return must(askAgain(to.ep, frame(kSet, q)), "SET");
        }
        // Felucca's motion playing on that track sets the parameters it has events for: those are
        // not carried (a knob turn on them is not either: the firmware cannot tell the two apart);
        // while a chain plays, LEN..GATE are the chain's
        if (std::find(from.selMotion.begin(), from.selMotion.end(), int(a[1])) != from.selMotion.end()) return true;
        if (a[1] >= kLen && a[1] <= kGate && chainPlays(from)) return true;
        std::vector<uint8_t> q = {uint8_t(from.sel), a[1]};
        v14(q, r14(a, 2));
        return must(askAgain(to.ep, frame(kTrackParam, q)), "TRACK_PARAM");
    }
    if (cmd == kTrackChanged && a.size() >= 4) {   // track, id, value: another track's mix
        std::vector<uint8_t> q = {a[0], a[1]};
        v14(q, r14(a, 2));
        return must(askAgain(to.ep, frame(kTrackParam, q)), "TRACK_PARAM");
    }
    if (cmd == kStepChanged && a.size() >= 2) {   // index, track
        if (chainPlays(from)) return true;   // Felucca: a chain's steps, not the pattern's
        const auto& d = from.ep.dialect();
        if (d.drumStep >= 0 && a[1] == d.drumTrack) {   // SLOOP's drum lanes, whole
            auto r = askAgain(from.ep, frame(d.drumStep, {a[0]}));
            if (!must(r, "DRUM_STEP")) return false;
            auto step = argsOf(*r);
            if (step.size() < 14) return true;
            if (!must(askAgain(to.ep, frame(d.drumStep, step)), "DRUM_STEP")) return false;
            return copyStepExtras(from.ep, to.ep, a[1], a[0], error);   // SLOOP 2.4: its locks, nudge, condition
        }
        auto r = askAgain(from.ep, frame(kTrackStep, {a[1], a[0]}));
        if (!must(r, "TRACK_STEP")) return false;
        auto step = argsOf(*r);
        if (step.size() < 10) return true;
        if (!must(askAgain(to.ep, frame(kTrackStep, step)), "TRACK_STEP")) return false;
        return copyStepExtras(from.ep, to.ep, a[1], a[0], error);
    }
    if (cmd == kReload && a.size() >= 3) {   // engine, preset, the selected track: a load or a new selection
        auto& echoes = from.echoes;
        for (auto it = echoes.begin(); it != echoes.end(); ++it)
            if (it->first == push) { echoes.erase(it); from.lastReload = push; return true; }   // our own load, coming back
        const bool same = push == from.lastReload;   // nothing it names changed: steps did (SLOOP)
        from.lastReload = push;
        from.sel = a[2];
        if (to.sel != a[2]) {
            if (!must(askAgain(to.ep, frame(kTrack, {a[2]})), "TRACK")) return false;
            to.sel = a[2];
        }
        if (!copyTrackSound(from, to, a[2], error)) return false;
        // SLOOP pushes RELOAD, not STEP_CHANGED, for step edits on its SEQ layer and drum screen,
        // undo and section switches: the patterns again (up to each track's length; only what
        // differs is written)
        if (same && isSloop(from.ep.dialect())) return copyPatterns(from.ep, to.ep, 4, error, true);
        return true;
    }
    return true;
}

bool Mirror::copyTrackSound(Side& from, Side& to, int track, juce::String& error) {
    Loaded loaded;
    const bool ok = copySound(from.ep, to.ep, track, error, &loaded);
    if (loaded.did && !(to.caps & kCapNoEcho))   // a load: the other side's RELOAD is expected (before 1.0.2), not carried back
        to.echoes.push_back({frame(kReload, {loaded.engine, loaded.preset, uint8_t(track)}), juce::Time::getMillisecondCounter()});
    return ok;
}

// A track's sound from one side to the other: its engine and preset (a load), then every value
// that differs, then its FM6 patch.
bool copySound(Endpoint& from, Endpoint& to, int track, juce::String& error, Loaded* loaded) {
    auto dump = [&](Endpoint& s) { auto r = s.ask(frame(kTrackDump, {uint8_t(track)}), kAsk); return r ? argsOf(*r) : std::vector<uint8_t>{}; };
    const auto src = dump(from);
    if (src.size() < 5) { error = "no answer to TRACK_DUMP"; return false; }
    auto dst = dump(to);
    if (dst.size() < 3) { error = "no answer to TRACK_DUMP"; return false; }
    if (dst[1] != src[1] || dst[2] != src[2]) {
        // PRESET loads into the selected part: that track is selected first
        if (!askAgain(to, frame(kTrack, {uint8_t(track)}))) { error = "no answer to TRACK"; return false; }
        if (!to.ask(frame(kPreset, {src[1], src[2]}), kFlash)) { error = "no answer to PRESET"; return false; }
        if (loaded) *loaded = {true, src[1], src[2]};
        dst = dump(to);
    }
    // a release that added parameters adds them just before the engine's eight (core.h; SLOOP 2.3's
    // P_E0 is 50, 2.4's 53): from an older side, those eight go to the newer one's numbers
    const int srcN = int(src.size() - 3) / 2, dstN = int(dst.size() - 3) / 2;
    const int added = dstN > srcN && srcN >= 8 ? dstN - srcN : 0;
    for (size_t i = 3; i + 1 < src.size(); i += 2) {
        int id = int(i - 3) / 2;
        if (added && id >= srcN - 8) id += added;
        const size_t at = 3 + size_t(id) * 2;
        if (at + 1 < dst.size() && src[i] == dst[at] && src[i + 1] == dst[at + 1]) continue;
        std::vector<uint8_t> q = {uint8_t(track), uint8_t(id), src[i], src[i + 1]};
        if (!askAgain(to, frame(kTrackParam, q))) { error = "no answer to TRACK_PARAM"; return false; }
    }
    if (!from.dialect().fm6 || !to.dialect().fm6) return true;
    if (isSloop(from.dialect()) && track >= 3) return true;   // (SLOOP 2.4: the synth parts have an FM6 patch, the drums none)
    auto p = from.ask(frame(kFm6Get, {0, uint8_t(track)}), kAsk);
    auto pa = p ? argsOf(*p) : std::vector<uint8_t>{};
    if (pa.size() == 3 + 128 && pa[2] == 0) {
        std::vector<uint8_t> q = {0, uint8_t(track)};
        q.insert(q.end(), pa.begin() + 3, pa.end());
        if (!to.ask(frame(kFm6Put, q), kAsk)) { error = "no answer to FM6_PUT"; return false; }
    }
    return true;
}

}  // namespace felucca
