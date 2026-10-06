// sloop_test -- SLOOP's engines as the plugin drives them (engines/sloop/sloop_core.c through
// FeluccaEngine, Flavor::Sloop): its pool of copies, sound, independence of instances,
// parameters, engines, presets, the editor protocol and its objects.

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include "FeluccaEngine.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, msg) do { if (cond) ++g_pass; else { ++g_fail; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } } while (0)

constexpr auto kSloop = FeluccaEngine::Flavor::Sloop;
using Sloop = FeluccaEngine;

static void noteOn(FeluccaEngine& f, int ch, int note, int vel) { uint8_t m[3] = {uint8_t(0x90 | (ch - 1)), uint8_t(note), uint8_t(vel)}; f.midi(m, 3); }
static void noteOff(FeluccaEngine& f, int ch, int note) { uint8_t m[3] = {uint8_t(0x80 | (ch - 1)), uint8_t(note), 0}; f.midi(m, 3); }

static uint64_t hash(uint64_t h, const std::vector<float>& x) {
    for (float v : x) { uint32_t u; std::memcpy(&u, &v, 4); h ^= u; h *= 1099511628211ull; }
    return h;
}

// a few seconds of a part playing a chord, in blocks of `block` frames; returns the hash
static uint64_t play(FeluccaEngine& f, int ch, int block, int blocks, double* rms = nullptr) {
    std::vector<float> l(static_cast<size_t>(block)), r(static_cast<size_t>(block));
    uint64_t h = 1469598103934665603ull;
    double sum = 0;
    for (int k = 0; k < blocks; ++k) {
        if (k == 0) { noteOn(f, ch, 48, 100); noteOn(f, ch, 55, 90); noteOn(f, ch, 64, 80); }
        if (k == blocks / 2) { noteOff(f, ch, 48); noteOff(f, ch, 55); noteOff(f, ch, 64); }
        f.render(l.data(), r.data(), block);
        h = hash(hash(h, l), r);
        for (int i = 0; i < block; ++i) sum += double(l[size_t(i)]) * l[size_t(i)];
    }
    if (rms) *rms = std::sqrt(sum / double(block * blocks));
    return h;
}

// Sloop's parameters and factory presets, as tests/sloop-frozen.txt keeps them: a later Sloop
// that changes or removes any is not backwards compatible (docs/ADDING-A-FIRMWARE.md, Versions).
// Keys go by label, not number.
static std::string describe(const FeluccaEngine::Desc& d) {
    std::ostringstream o;
    o << d.min << ".." << d.max << " def " << d.def << " fmt " << d.fmt;
    for (const auto& n : d.names) o << " " << n;
    return o.str();
}

static std::map<std::string, std::string> frozenFacts() {
    std::map<std::string, std::string> out;
    FeluccaEngine f{kSloop};
    std::map<std::string, int> seen;
    for (int id = 0; id < f.firstEngineParam(); ++id) {
        const auto d = f.paramDesc(0, id);
        if (d.label.empty()) continue;
        out["param " + d.label + "#" + std::to_string(++seen[d.label])] = describe(d);
    }
    seen.clear();
    for (int id = 0; id < f.globalCount(); ++id) {
        const auto d = f.globalDesc(id);
        if (d.label.empty() || d.label == "-") continue;
        out["global " + d.label + "#" + std::to_string(++seen[d.label])] = describe(d);
    }
    std::vector<float> l(64), r(64);
    for (int e : f.enginesShown()) {
        const std::string en = f.engineName(e);
        f.setEngine(0, e);
        for (int k = 0; k < 8; ++k) out["engine " + en + " E" + std::to_string(k + 1)] = f.paramDesc(0, f.firstEngineParam() + k).label + " " + describe(f.paramDesc(0, f.firstEngineParam() + k));
        const auto names = f.presetNames(e);
        for (size_t i = 0; i < names.size(); ++i) {
            if (names[i].empty()) continue;   // an alias: plays another
            f.applyPreset(0, int(i));
            uint64_t h = 1469598103934665603ull;
            for (int id = 0; id < f.paramCount(); ++id) { h ^= uint64_t(uint16_t(f.param(0, id))); h *= 1099511628211ull; }
            std::ostringstream o;
            o << names[i] << " " << std::hex << h;
            out["preset " + en + " " + std::to_string(i)] = o.str();
        }
    }
    return out;
}

static void checkFrozen() {
    const std::string path = std::string(SLOOP_FROZEN);
    const auto now = frozenFacts();
    std::map<std::string, std::string> kept;
    {
        std::ifstream in(path);
        std::string line;
        while (std::getline(in, line)) {
            const auto tab = line.find('\t');
            if (tab != std::string::npos) kept[line.substr(0, tab)] = line.substr(tab + 1);
        }
    }
    int changed = 0, gone = 0, added = 0;
    for (const auto& [k, v] : kept) {
        auto it = now.find(k);
        if (it == now.end()) { ++gone; std::printf("  gone: %s\n", k.c_str()); }
        else if (it->second != v) { ++changed; std::printf("  changed: %s\n    was %s\n    now %s\n", k.c_str(), v.c_str(), it->second.c_str()); }
    }
    std::vector<std::string> fresh;
    for (const auto& [k, v] : now) if (!kept.count(k)) { ++added; fresh.push_back(k + "\t" + v); }
    CHECK(changed == 0 && gone == 0, "Sloop's parameters and presets are as tests/sloop-frozen.txt keeps them "
                                     "(a change or removal: keep the old release beside this one, docs/ADDING-A-FIRMWARE.md)");
    if (added && std::getenv("SLOOP_APPEND_FROZEN")) {
        std::ofstream out(path, std::ios::app);
        for (const auto& line : fresh) out << line << "\n";
        std::printf("  appended %d to %s\n", added, path.c_str());
        added = 0;
    }
    if (added) std::printf("  %d new (SLOOP_APPEND_FROZEN=1 sloop_test appends them)\n", added);
    CHECK(added == 0, "nothing new missing from tests/sloop-frozen.txt");
}

int main() {
    checkFrozen();
    uint64_t fresh = 0;
    { Sloop f{kSloop}; fresh = play(f, 1, 128, 300); }

    // ---- the pool: its own copies, apart from Felucca's; beyond them instances share one ----
    CHECK(FeluccaEngine::copies(kSloop) >= 2, "there are Sloop copies to play");
    {
        const int copies = FeluccaEngine::copies(kSloop), n = copies + 3;
        const int engines[3] = {0, 4, 6};                             // ANALOG, SAMPLE, TRIO
        uint64_t alone[3];
        for (int e = 0; e < 3; ++e) { Sloop x{kSloop}; x.setEngine(0, engines[e]); alone[e] = play(x, 1, 256, 120); }
        std::vector<std::unique_ptr<FeluccaEngine>> all;
        for (int i = 0; i < n; ++i) { all.push_back(std::make_unique<FeluccaEngine>(kSloop)); all.back()->setEngine(0, engines[i % 3]); }
        CHECK(FeluccaEngine::instances(kSloop) == n && FeluccaEngine::copiesInUse(kSloop) == copies && FeluccaEngine::instances() == 0,
              "more instances than copies: every one plays, in Sloop's copies (none of Felucca's)");
        std::vector<uint64_t> h(size_t(n), 1469598103934665603ull);
        std::vector<float> l(256), r(256);
        for (int k = 0; k < 120; ++k)
            for (int i = 0; i < n; ++i) {
                auto& f = *all[size_t(i)];
                if (k == 0) { noteOn(f, 1, 48, 100); noteOn(f, 1, 55, 90); noteOn(f, 1, 64, 80); }
                if (k == 60) { noteOff(f, 1, 48); noteOff(f, 1, 55); noteOff(f, 1, 64); }
                f.render(l.data(), r.data(), 256);
                h[size_t(i)] = hash(hash(h[size_t(i)], l), r);
            }
        bool same = true;
        long swapped = 0;
        for (int i = 0; i < n; ++i) {
            same = same && h[size_t(i)] == alone[i % 3];
            if (h[size_t(i)] != alone[i % 3]) std::printf("  instance %d plays differently\n", i);
            swapped += all[size_t(i)]->swaps();
        }
        CHECK(same && swapped > 100, "instances sharing a copy, played in turn, each sound as alone (their states swapped)");
        all.clear();
        for (int i = 0; i <= copies; ++i) all.push_back(std::make_unique<FeluccaEngine>(kSloop));
        all[0]->setEngine(0, engines[1]);
        all[size_t(copies)]->setEngine(0, engines[2]);
        uint64_t t0 = 0, t1 = 0;
        std::thread a([&] { t0 = play(*all[0], 1, 256, 120); });
        std::thread b([&] { t1 = play(*all[size_t(copies)], 1, 256, 120); });
        a.join(); b.join();
        CHECK(t0 == alone[1] && t1 == alone[2] && all[size_t(copies)]->swaps() > 1, "and on two threads at once");
    }
    CHECK(FeluccaEngine::copiesInUse(kSloop) == 0 && FeluccaEngine::instances(kSloop) == 0, "every copy is given back");
    {   // a Felucca and a Sloop side by side: each its own
        FeluccaEngine fel, slp{kSloop};
        CHECK(fel.version() == "v1.0.3" && slp.version() == "SLOOP 2.3" && fel.engines() == 14 && slp.engines() == 9,
              "a Felucca instance and a Sloop instance at once, each its own firmware");
    }

    // ---- what it is ----
    Sloop a{kSloop};
    CHECK(a.tracks() == 4 && a.parts() == 3, "three synth parts and the drum track");
    CHECK(a.engines() == 9 && a.enginesShown().size() == 9, "nine engines, all to pick");
    CHECK(a.engineName(0) == "ANALOG" && a.engineName(8) == "GRAIN" && a.fm6Engine() == a.engines(), "engines by name; no FM6");
    std::printf("  engines:");
    for (int e : a.enginesShown()) std::printf(" %s(%zu)", a.engineName(e).c_str(), a.presetNames(e).size());
    std::printf("\n");
    CHECK(a.engineName(a.engineOf(0)) == "ANALOG" && a.engineName(a.engineOf(1)) == "DIGITAL" && a.engineName(a.engineOf(2)) == "SAMPLE",
          "power-on: ANALOG, DIGITAL, SAMPLE");
    {
        const auto kit = a.paramDesc(3, a.firstEngineParam());
        CHECK(kit.label == "KIT" && !kit.names.empty(), "the drum track's first engine parameter is its kit");
        const int before = a.engineOf(3);
        a.setEngine(3, 0);
        CHECK(a.engineOf(3) == before, "the drum track's engine is not changed");
    }

    // ---- sound ----
    double rms = 0;
    play(a, 1, 256, 400, &rms);
    std::printf("  part 1 chord: rms %.4f\n", rms);
    CHECK(rms > 0.001, "a chord on part 1 sounds");
    {
        Sloop d{kSloop};
        double drums = 0;
        play(d, 10, 256, 200, &drums);
        std::printf("  channel 10: rms %.4f\n", drums);
        CHECK(drums > 0.001, "notes on channel 10 play the drums");
    }
    a.reset();
    double silent = 0;
    { std::vector<float> l(4096), r(4096); a.render(l.data(), r.data(), 4096); for (float v : l) silent = std::max(silent, double(std::fabs(v))); }
    CHECK(silent == 0.0, "silent with no notes");
    {   // two instances in turn sound as each alone; any block size gives the same samples
        Sloop x{kSloop}, y{kSloop};
        y.setEngine(0, 3);
        Sloop xs{kSloop}, ys{kSloop};
        ys.setEngine(0, 3);
        const uint64_t xAlone = play(xs, 1, 256, 100), yAlone = play(ys, 1, 256, 100);
        std::vector<float> l(256), r(256);
        uint64_t hx = 1469598103934665603ull, hy = hx;
        for (int k = 0; k < 100; ++k) {
            if (k == 0) { noteOn(x, 1, 48, 100); noteOn(x, 1, 55, 90); noteOn(x, 1, 64, 80); noteOn(y, 1, 48, 100); noteOn(y, 1, 55, 90); noteOn(y, 1, 64, 80); }
            if (k == 50) { noteOff(x, 1, 48); noteOff(x, 1, 55); noteOff(x, 1, 64); noteOff(y, 1, 48); noteOff(y, 1, 55); noteOff(y, 1, 64); }
            x.render(l.data(), r.data(), 256); hx = hash(hash(hx, l), r);
            y.render(l.data(), r.data(), 256); hy = hash(hash(hy, l), r);
        }
        CHECK(hx == xAlone && hy == yAlone && xAlone != yAlone, "two instances rendered in turn sound as each alone");
        Sloop p{kSloop}, q{kSloop};
        CHECK(play(p, 1, 37, 1000) != 0 && play(q, 1, 256, 1000 * 37 / 256) != 0, "odd block sizes render");
    }

    // ---- parameters ----
    {
        Sloop f{kSloop};
        CHECK(f.paramCount() == 58 && f.firstEngineParam() == 50 && f.globalCount() == 32, "58 parameters per track, 8 of them the engine's; 32 globals");
        const auto lvl = f.paramDesc(0, 0);
        f.setParam(0, 0, lvl.max + 50);
        CHECK(f.param(0, 0) == lvl.max, "values are kept in range");
        auto labels = [&] { std::string s; for (int k = 0; k < 8; ++k) s += f.paramDesc(0, f.firstEngineParam() + k).label + "|"; return s; };
        const std::string before = labels();
        f.setEngine(0, 6);
        CHECK(f.engineOf(0) == 6 && labels() != before, "the engine switches, and its parameters' names follow");
        f.applyPreset(0, 2);
        CHECK(f.presetOf(0) == 2, "a preset applies");
        int enums = 0;
        for (int id = 0; id < f.paramCount(); ++id) enums += !f.paramDesc(0, id).names.empty();
        CHECK(enums > 5, "list parameters carry their value names");
    }

    // ---- the virtual device ----
    {
        Sloop f{kSloop};
        std::vector<float> l(256), r(256);
        auto settle = [&](int blocks) { for (int k = 0; k < blocks; ++k) f.render(l.data(), r.data(), 256); };
        auto request = [&](std::vector<uint8_t> body) {
            std::vector<uint8_t> m = {0xF0, 0x7D, 0x46, 0x4C};
            m.insert(m.end(), body.begin(), body.end());
            m.push_back(0xF7);
            f.sysex(m.data(), int(m.size()));
            settle(8);
            auto got = f.takeSysex();
            return got.empty() ? std::vector<uint8_t>{} : got.back();
        };
        auto info = request({1});
        const std::string version(info.size() > 5 ? reinterpret_cast<const char*>(info.data() + 5) : "");
        const size_t at = 5 + version.size() + 1;
        std::printf("  INFO: \"%s\"\n", version.c_str());
        CHECK(info.size() > at + 5 && info[4] == 1 && version == "FELUCCA SLOOP 2.3" && info[at] == 9 && info[at + 1] == 58 && info[at + 4] == 50,
              "INFO answers, as SLOOP 2.3 (9 engines, 58 parameters, P_E0 50)");
        auto set = request({3, 0, 0, uint8_t((50 + 8192) & 127), uint8_t((50 + 8192) >> 7)});
        CHECK(!set.empty() && f.param(0, 0) == 50, "SET through the editor protocol changes the sound");
        {
            const std::vector<uint8_t> identity = {0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7};
            f.sysex(identity.data(), int(identity.size()));
            settle(8);
            f.takeSysex();
            CHECK(request({25}).size() == 7, "after SysEx not its own, the editor still answers PING");
        }
        f.setEngine(2, 6);
        f.setParam(2, 0, 77);
        std::vector<uint8_t> runtime;
        CHECK(f.object(0, runtime) && runtime.size() > 1000, "the working project as an object");
        Sloop g{kSloop};
        CHECK(g.putObject(0, runtime) == 0 && g.engineOf(2) == 6 && g.param(2, 0) == 77, "and restores into another instance");
        std::vector<uint8_t> empty;
        CHECK(g.object(2, empty) && empty.empty(), "an empty project slot has no bytes");
        CHECK(g.putObject(2, runtime) == 0 && g.object(2, empty) && empty.size() == runtime.size(),
              "a project slot takes a project, right after the working project was put (its stop done first)");
        std::vector<uint8_t> settingsObj;
        CHECK(g.object(1, settingsObj) && !settingsObj.empty(), "the settings object");
        CHECK(!g.object(8, empty), "no object 8 (Sloop has no FM6 bank)");

        const auto names = f.buttonNames();
        const int play = int(std::find(names.begin(), names.end(), std::string("PLAY")) - names.begin());
        CHECK(play < int(names.size()) && !f.playing(), "a PLAY button; stopped at power-on");
        f.transport(true); settle(2);
        CHECK(f.playing(), "the plugin starts the transport");
        f.transport(false); settle(2);
        CHECK(!f.playing(), "and stops it");
        f.transport(true); settle(2);
        auto stored = request({19, 0, 'T', 'S', 'T', 0});   // UP_STORE slot 1, named TST
        CHECK(stored.size() >= 7 && stored[4] == 19, "UP_STORE while playing answers (no hang)");
        f.transport(false); settle(2);

        std::vector<uint16_t> screen;
        f.draw(screen);
        int lit = 0;
        for (auto v : screen) if (v != screen[0]) ++lit;
        CHECK(screen.size() == 240u * 240u && lit > 1000, "the screen draws");
    }

    // ---- live sections, the song and solo (its SONG and GLO layers) ----
    {
        Sloop f{kSloop};
        std::vector<float> l(256), r(256);
        auto run = [&](int blocks) { for (int k = 0; k < blocks; ++k) f.render(l.data(), r.data(), 256); };
        auto a = f.arrangement();
        CHECK(a && a->playing == -1 && a->stored == 0 && !a->songMode && !a->chain.empty(), "no section stored at power-on; a default song");
        CHECK(f.arrangementDo(0, 1) == 1, "an empty section does not play");
        f.setParam(0, 0, 50);
        CHECK(f.arrangementDo(1, 0) == 0 && (f.arrangement()->stored & 1u), "the loop stored as section A");
        f.setParam(0, 0, 90);
        CHECK(f.arrangementDo(1, 1) == 0 && f.arrangement()->stored == 3u, "and as B");
        CHECK(f.arrangementDo(0, 0) == 0 && f.arrangement()->playing == 0 && f.param(0, 0) == 50, "stopped, A played becomes the loop");
        f.transport(true);
        run(4);
        CHECK(f.arrangementDo(0, 1) == 0 && f.arrangement()->queued == 1 && f.arrangement()->playing == 0, "playing, B asked for waits for the bar");
        run(500);   // (two seconds and more: a bar at 90 BPM)
        a = f.arrangement();
        CHECK(a && a->playing == 1 && a->queued == -1 && f.param(0, 0) == 90, "and plays from the next bar");
        f.transport(false);
        run(4);
        CHECK(f.setChain({{1, 2}, {0, 1}}, true) == 0, "a song set");
        a = f.arrangement();
        const std::vector<std::pair<int, int>> want = {{1, 2}, {0, 1}};
        CHECK(a && a->chain == want && a->loop, "and read back, looping");
        CHECK(f.setChain({{5, 1}}, false) == 1 && f.setChain({}, false) == 1, "a section past D, or no part: refused");
        CHECK(f.arrangementDo(2, 1) == 0 && f.arrangement()->songMode, "song mode on");
        f.transport(true);
        run(8);
        a = f.arrangement();
        CHECK(a && a->songPlays && a->entry == 0 && f.arrangementDo(0, 0) == 2, "PLAY plays the song; a section is not played meanwhile");
        f.transport(false);
        run(4);
        CHECK(f.arrangementDo(2, 0) == 0 && f.arrangementDo(3, 1) == 0 && f.arrangement()->songRec == 1, "SONG REC armed");
        CHECK(f.arrangementDo(2, 1) == 3, "song mode waits while SONG REC is on");
        CHECK(f.arrangementDo(3, 0) == 0 && f.arrangement()->songRec == 0, "and off");
        CHECK(f.arrangementDo(4, 0b0101) == 0 && f.arrangement()->solo == 0b0101u, "solo: parts 1 and 3");
    }

    // ---- a copy given back is as good as new ----
    {
        {
            Sloop dirty{kSloop};
            dirty.setEngine(0, 3); dirty.setEngine(1, 8); dirty.setEngine(2, 4);
            dirty.setParam(0, 0, 10); dirty.setGlobal(0, 200);
            noteOn(dirty, 1, 60, 127); noteOn(dirty, 2, 64, 127); noteOn(dirty, 10, 36, 127);
            std::vector<float> l(4096), r(4096);
            for (int k = 0; k < 20; ++k) dirty.render(l.data(), r.data(), 4096);
        }
        std::vector<std::unique_ptr<FeluccaEngine>> all;
        bool same = true;
        for (int i = 0; FeluccaEngine::copiesInUse(kSloop) < FeluccaEngine::copies(kSloop); ++i) {
            all.push_back(std::make_unique<FeluccaEngine>(kSloop));
            const auto h = play(*all.back(), 1, 128, 300);
            if (h != fresh) std::printf("  copy taken %d plays differently\n", i);
            same = same && h == fresh;
        }
        CHECK(same, "every copy, given back after use, plays exactly as a never-used copy");
    }

    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
