// melodee_test -- Melodee's engines as the plugin drives them (engines/melodee/melodee_core.c through
// FeluccaEngine, Flavor::Melodee): its pool of copies, sound, independence of instances,
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

constexpr auto kMel = FeluccaEngine::Flavor::Melodee;
using Mel = FeluccaEngine;

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

// Melodee's parameters and factory presets, as tests/melodee-frozen.txt keeps them: a later Melodee
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
    FeluccaEngine f{kMel};
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
    const std::string path = std::string(MELODEE_FROZEN);
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
    CHECK(changed == 0 && gone == 0, "Melodee's parameters and presets are as tests/melodee-frozen.txt keeps them "
                                     "(a change or removal: keep the old release beside this one, docs/ADDING-A-FIRMWARE.md)");
    if (added && std::getenv("MELODEE_APPEND_FROZEN")) {
        std::ofstream out(path, std::ios::app);
        for (const auto& line : fresh) out << line << "\n";
        std::printf("  appended %d to %s\n", added, path.c_str());
        added = 0;
    }
    if (added) std::printf("  %d new (MELODEE_APPEND_FROZEN=1 melodee_test appends them)\n", added);
    CHECK(added == 0, "nothing new missing from tests/melodee-frozen.txt");
}

int main() {
    checkFrozen();
    uint64_t fresh = 0;
    { Mel f{kMel}; fresh = play(f, 1, 128, 300); }

    // ---- the pool: its own copies, apart from Felucca's and SLOOP's; beyond them instances share one ----
    CHECK(FeluccaEngine::copies(kMel) >= 2, "there are Melodee copies to play");
    {
        const int copies = FeluccaEngine::copies(kMel), n = copies + 3;
        const int engines[3] = {19, 15, 3};                           // PROPHET, CZ-1, LOFI
        uint64_t alone[3];
        for (int e = 0; e < 3; ++e) { Mel x{kMel}; x.setEngine(0, engines[e]); alone[e] = play(x, 1, 256, 120); }
        std::vector<std::unique_ptr<FeluccaEngine>> all;
        for (int i = 0; i < n; ++i) { all.push_back(std::make_unique<FeluccaEngine>(kMel)); all.back()->setEngine(0, engines[i % 3]); }
        CHECK(FeluccaEngine::instances(kMel) == n && FeluccaEngine::copiesInUse(kMel) == copies && FeluccaEngine::instances() == 0
              && FeluccaEngine::instances(FeluccaEngine::Flavor::Sloop) == 0,
              "more instances than copies: every one plays, in Melodee's copies (none of Felucca's or SLOOP's)");
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
        for (int i = 0; i <= copies; ++i) all.push_back(std::make_unique<FeluccaEngine>(kMel));
        all[0]->setEngine(0, engines[1]);
        all[size_t(copies)]->setEngine(0, engines[2]);
        uint64_t t0 = 0, t1 = 0;
        std::thread a([&] { t0 = play(*all[0], 1, 256, 120); });
        std::thread b([&] { t1 = play(*all[size_t(copies)], 1, 256, 120); });
        a.join(); b.join();
        CHECK(t0 == alone[1] && t1 == alone[2] && all[size_t(copies)]->swaps() > 1, "and on two threads at once");
    }
    CHECK(FeluccaEngine::copiesInUse(kMel) == 0 && FeluccaEngine::instances(kMel) == 0, "every copy is given back");
    {   // Felucca, SLOOP and Melodee side by side: each its own
        FeluccaEngine fel, slp{FeluccaEngine::Flavor::Sloop}, mel{kMel};
        CHECK(fel.version() == "v1.0.3" && slp.version() == "SLOOP 2.4.1" && mel.version() == "v0.13" && fel.engines() == 14 && mel.engines() == 20,
              "a Felucca, a SLOOP and a Melodee instance at once, each its own firmware");
    }

    // ---- what it is ----
    Mel a{kMel};
    CHECK(a.tracks() == 4 && a.parts() == 4, "four parts, each with any engine");
    CHECK(a.engines() == 20 && a.enginesShown().size() == 8 && a.enginesShown()[0] == 19,
          "20 engine numbers (0.13: PROPHET 19 first in the list; PHYS, TRIO, WHEEL retired), eight to pick");
    CHECK(a.engineName(0) == "ANALOG" && a.engineName(12) == "FM6" && a.engineName(15) == "CZ-1" && a.engineName(19) == "PROPHET" && a.fm6Engine() == 12,
          "engines by name");
    std::printf("  engines:");
    for (int e : a.enginesShown()) std::printf(" %s(%zu)", a.engineName(e).c_str(), a.presetNames(e).size());
    std::printf("\n");
    {
        const auto cz = a.presetNames(15);
        CHECK(cz.size() == 65 && std::count_if(cz.begin(), cz.end(), [](const std::string& n) { return !n.empty(); }) >= 64,
              "CZ-1: Casio's 64 factory tones (and its init tone)");
        const auto p5 = a.presetNames(19);
        CHECK(p5.size() == 201 && std::count_if(p5.begin(), p5.end(), [](const std::string& n) { return !n.empty(); }) >= 200,
              "PROPHET: Sequential's 200 factory programs (and its init program)");
    }
    CHECK(a.engineName(a.engineOf(0)) == "ANALOG" && a.engineName(a.engineOf(1)) == "FM6" && a.engineName(a.engineOf(2)) == "LOFI"
          && a.engineName(a.engineOf(3)) == "DRUM", "power-on: ANALOG, FM6, LOFI, DRUM");

    // ---- sound ----
    double rms = 0;
    play(a, 1, 256, 400, &rms);
    std::printf("  part 1 chord: rms %.4f\n", rms);
    CHECK(rms > 0.001, "a chord on part 1 sounds");
    {
        Mel c{kMel};
        c.setEngine(0, 15);
        c.applyPreset(0, 1);
        double czRms = 0;
        play(c, 1, 256, 200, &czRms);
        std::printf("  CZ-1 %s: rms %.4f\n", c.presetNames(15)[1].c_str(), czRms);
        CHECK(czRms > 0.001, "a CZ-1 factory tone sounds");
        Mel p{kMel};
        p.setEngine(0, 19);
        p.applyPreset(0, 1);
        double p5Rms = 0;
        play(p, 1, 256, 200, &p5Rms);
        std::printf("  PROPHET %s: rms %.4f\n", p.presetNames(19)[1].c_str(), p5Rms);
        CHECK(p5Rms > 0.001, "a PROPHET factory program sounds");
    }
    a.reset();
    double silent = 0;
    { std::vector<float> l(4096), r(4096); a.render(l.data(), r.data(), 4096); for (float v : l) silent = std::max(silent, double(std::fabs(v))); }
    CHECK(silent == 0.0, "silent with no notes");
    {   // two instances in turn sound as each alone; any block size gives the same samples
        Mel x{kMel}, y{kMel};
        y.setEngine(0, 3);
        Mel xs{kMel}, ys{kMel};
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
        Mel p{kMel}, q{kMel};
        CHECK(play(p, 1, 37, 1000) != 0 && play(q, 1, 256, 1000 * 37 / 256) != 0, "odd block sizes render");
    }

    // ---- parameters ----
    {
        Mel f{kMel};
        CHECK(f.paramCount() == 92 && f.firstEngineParam() == 84 && f.globalCount() == 27, "92 parameters per track, the engine's 8 from 84; 27 globals");
        const auto lvl = f.paramDesc(0, 0);
        f.setParam(0, 0, lvl.max + 50);
        CHECK(f.param(0, 0) == lvl.max, "values are kept in range");
        auto labels = [&] { std::string s; for (int k = 0; k < 8; ++k) s += f.paramDesc(0, f.firstEngineParam() + k).label + "|"; return s; };
        const std::string before = labels();
        f.setEngine(0, 15);
        CHECK(f.engineOf(0) == 15 && labels() != before, "the engine switches, and its parameters' names follow");
        f.applyPreset(0, 2);
        CHECK(f.presetOf(0) == 2, "a preset applies");
        int enums = 0;
        for (int id = 0; id < f.paramCount(); ++id) enums += !f.paramDesc(0, id).names.empty();
        CHECK(enums > 5, "list parameters carry their value names");
    }

    // ---- the virtual device ----
    {
        Mel f{kMel};
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
        CHECK(info.size() > at + 5 && info[4] == 1 && version == "MELODEE v0.13" && info[at] == 20 && info[at + 1] == 92 && info[at + 2] == 27 && info[at + 4] == 84,
              "INFO answers, as Melodee 0.13 (20 engines, 92 parameters, 27 globals, P_E0 84)");
        auto set = request({3, 0, 0, uint8_t((50 + 8192) & 127), uint8_t((50 + 8192) >> 7)});
        CHECK(!set.empty() && f.param(0, 0) == 50, "SET through the editor protocol changes the sound");
        {
            const std::vector<uint8_t> identity = {0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7};
            f.sysex(identity.data(), int(identity.size()));
            settle(8);
            f.takeSysex();
            CHECK(request({25}).size() == 7, "after SysEx not its own, the editor still answers PING");
        }
        f.setEngine(2, 15);
        f.applyPreset(2, 3);
        f.setParam(2, 0, 77);
        std::vector<uint8_t> runtime;
        CHECK(f.object(0, runtime) && runtime.size() > 20000, "the working project (all 32 pattern banks) as an object");
        Mel g{kMel};
        CHECK(g.putObject(0, runtime) == 0 && g.engineOf(2) == 15 && g.param(2, 0) == 77, "and restores into another instance");
        std::vector<uint8_t> slot;
        CHECK(g.object(2, slot) && slot.empty(), "an empty project slot has no bytes");
        CHECK(g.putObject(2, runtime) == 0 && g.object(2, slot) && slot == runtime,
              "a project slot keeps a whole project, past its five sectors into their extension, and gives it back the same");
        Mel h{kMel};
        CHECK(h.putObject(5, slot) == 0 && h.object(5, slot) && slot == runtime, "so does slot D, in another instance");
        std::vector<uint8_t> settingsObj;
        CHECK(g.object(1, settingsObj) && !settingsObj.empty(), "the settings object");
        std::vector<uint8_t> cz, cz2;
        CHECK(g.object(9, cz) && cz.size() == 2332 && h.putObject(10, cz) == 0 && h.object(10, cz2) && cz2 == cz, "a CZ bank, kept and put back");
        std::vector<uint8_t> none;
        std::vector<uint8_t> p5, p52;
        CHECK(g.object(23, p5) && p5.size() == 3600 && h.putObject(24, p5) == 0 && h.object(24, p52) && p52 == p5, "a PROPHET user bank (0.13), kept and put back");
        CHECK(g.object(8, none) && none.empty() && !g.object(28, none), "object 8 retired (empty); none past 27");

        {   // A4 is the device's setting (tuning_a4), not the song's: the API, the editor and the sound agree
            f.setGlobal(21, 415);
            auto get = request({2, 1, 21});
            const int a4 = get.size() >= 9 ? (int(get[7]) | int(get[8]) << 7) - 8192 : -1;
            CHECK(f.global(21) == 415 && a4 == 415, "A4 set through the API is the editor's A4 too");
            request({3, 1, 21, uint8_t((432 + 8192) & 127), uint8_t((432 + 8192) >> 7)});
            CHECK(f.global(21) == 432, "and the editor's A4 is the API's");
            Mel x{kMel}, y{kMel};
            y.setGlobal(21, 400);
            CHECK(play(x, 1, 256, 60) != play(y, 1, 256, 60), "A4 changes the pitch");
            f.setGlobal(21, 440);
        }
        {   // the kept globals (CLK TUNE MIDI ROUT) of a settings object put back are applied, as at power-on,
            // and the settings stay as put (the instance's own are not saved back over them)
            Mel src{kMel}, dst{kMel};
            src.setGlobal(3, -5);
            settle(4);
            for (int k = 0; k < 120; ++k) src.render(l.data(), r.data(), 256);   // (its kept values saved: 1.5 s)
            std::vector<uint8_t> set1, after;
            CHECK(src.object(1, set1) && dst.putObject(1, set1) == 0 && dst.global(3) == -5, "TUNE comes with the settings object");
            for (int k = 0; k < 500; ++k) dst.render(l.data(), r.data(), 256);
            CHECK(dst.object(1, after) && after == set1, "and the settings stay as they were put");
        }
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
        const auto leds = f.leds();
        CHECK(leds.size() == 14u + 27u + 1u && std::count_if(leds.begin(), leds.end(), [](uint8_t v) { return v != 0; }) > 0, "the LEDs: some lit or glowing");
    }

    // ---- its flash: every object written into both of its copies (each write alternates them) ----
    {
        Mel m{kMel};
        std::vector<uint8_t> music;
        m.object(0, music);
        int fails = 0;
        for (int round = 0; round < 3; ++round) {
            for (int id = 0; id <= 27; ++id) {
                if (id == 8) continue;   // (retired)
                std::vector<uint8_t> b;
                if (id >= 2 && id <= 5) b = music;
                else if (!m.object(id, b)) continue;
                if (m.putObject(id, b) != 0) { ++fails; std::printf("  round %d: object %d refused\n", round, id); }
            }
            m.setGlobal(21, 430 + round);   // (the settings saved again)
        }
        CHECK(fails == 0, "every object stored three times over (both copies of each, PROPHET banks too): the RAM flash holds them all");
    }

    // ---- a copy given back is as good as new ----
    {
        {
            Mel dirty{kMel};
            dirty.setEngine(0, 3); dirty.setEngine(1, 15); dirty.setEngine(2, 12);
            dirty.setParam(0, 0, 10); dirty.setGlobal(0, 200);
            noteOn(dirty, 1, 60, 127); noteOn(dirty, 2, 64, 127); noteOn(dirty, 3, 36, 127);
            std::vector<uint8_t> runtime;
            dirty.object(0, runtime);
            dirty.putObject(3, runtime);
            std::vector<float> l(4096), r(4096);
            for (int k = 0; k < 20; ++k) dirty.render(l.data(), r.data(), 4096);
        }
        std::vector<std::unique_ptr<FeluccaEngine>> all;
        bool same = true;
        for (int i = 0; FeluccaEngine::copiesInUse(kMel) < FeluccaEngine::copies(kMel); ++i) {
            all.push_back(std::make_unique<FeluccaEngine>(kMel));
            const auto h = play(*all.back(), 1, 128, 300);
            if (h != fresh) std::printf("  copy taken %d plays differently\n", i);
            same = same && h == fresh;
            std::vector<uint8_t> empty;
            if (i == 0) CHECK(all.back()->object(3, empty) && empty.empty(), "its stored projects gone too");
        }
        CHECK(same, "every copy, given back after use, plays exactly as a never-used copy");
    }

    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
