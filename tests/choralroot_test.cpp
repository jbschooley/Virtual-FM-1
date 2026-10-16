// choralroot_test -- ChoralRoot 0.23 in the plugin (engines/choralroot): it powers on, plays what
// tests/choralroot-frozen.txt keeps (a hash of each take: a change in the firmware or in how it is built
// shows here), renders the same in any block size, instances are independent (one compiled copy, a state
// each), and its stored objects go through its own backup protocol, restarting as the unit does.
//
//     choralroot_test            (CHORALROOT_UPDATE_FROZEN=1 writes the frozen file anew)
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "FeluccaEngine.h"

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) ++g_pass; else { ++g_fail; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, std::string(msg).c_str()); } } while (0)

using Fl = FeluccaEngine::Flavor;
static constexpr int kSave = 7;   // SAVE, by its label's index (buttonNames)

static uint64_t fnv(uint64_t h, float x) {
    uint32_t bits;
    std::memcpy(&bits, &x, 4);
    for (int i = 0; i < 4; ++i) h = (h ^ ((bits >> (8 * i)) & 0xFF)) * 0x100000001b3ull;
    return h;
}

struct Take { uint64_t hash = 0xcbf29ce484222325ull; double mean = 0.0; };
// `seconds` rendered in blocks of `block`, `at(frame)` called before each block (the take's own actions)
static Take render(FeluccaEngine& e, double seconds, int block, const std::function<void(int)>& at = {}) {
    Take t;
    std::vector<float> l(static_cast<size_t>(block)), r(static_cast<size_t>(block));
    const int total = int(seconds * FeluccaEngine::kRate);
    double sum = 0.0;
    for (int done = 0; done < total; done += block) {
        if (at) at(done);
        const int n = std::min(block, total - done);
        e.render(l.data(), r.data(), n);
        for (int i = 0; i < n; ++i) { t.hash = fnv(fnv(t.hash, l[size_t(i)]), r[size_t(i)]); sum += std::fabs(l[size_t(i)]) + std::fabs(r[size_t(i)]); }
    }
    t.mean = sum / (2.0 * total);
    return t;
}

// The takes the frozen file keeps: each from power-on, 2 s
struct Script { const char* name; std::function<void(FeluccaEngine&, int)> at; bool sounds; };
static std::vector<Script> scripts() {
    auto key = [](int k) { return [k](FeluccaEngine& e, int frame) { if (frame == 4352) e.key(k, true); if (frame == 66048) e.key(k, false); }; };
    auto note = [](std::vector<int> notes) {
        return [notes](FeluccaEngine& e, int frame) {
            if (frame == 4352) for (int n : notes) { const uint8_t m[3] = {0x90, uint8_t(n), 100}; e.midi(m, 3); }
            if (frame == 66048) for (int n : notes) { const uint8_t m[3] = {0x80, uint8_t(n), 0}; e.midi(m, 3); }
        };
    };
    return {
        {"power-on, nothing played", {}, false},
        {"key 16 held 1.4 s", key(16), true},
        {"key 20 held 1.4 s", key(20), true},
        {"key 12 held 1.4 s", key(12), true},
        {"key 26 held 1.4 s", key(26), true},
        {"MIDI note 48 on channel 1", note({48}), true},
        {"MIDI notes 60 64 67 on channel 1", note({60, 64, 67}), true},
        {"OCT+ then key 12", [k = key(12)](FeluccaEngine& e, int frame) { if (frame == 0) e.button(13, true); if (frame == 2048) e.button(13, false); k(e, frame); }, true},
    };
}

static void run(FeluccaEngine& e, double seconds) { render(e, seconds, 256); }

int main() {
    {   // powers on
        FeluccaEngine e(Fl::ChoralRoot);
        CHECK(e.valid() && FeluccaEngine::instances(Fl::ChoralRoot) == 1, "ChoralRoot powers on (its state laid out as fel_state built it)");
        CHECK(e.version() == "v0.23" && FeluccaEngine::coreVersion(Fl::ChoralRoot) == "v0.23", "it is 0.23: " + e.version());
        CHECK(e.tracks() == 4 && e.paramCount() == 0 && e.globalCount() == 0, "four parts, no parameters offered (its protocol has none)");
        const auto b = e.buttonNames();
        CHECK(b.size() == 14 && b[size_t(kSave)] == "SAVE" && b[13] == "OCT+" && e.knobNames().size() == 7, "its panel: 14 buttons (SAVE the 8th), 7 encoders");
        run(e, 0.2);
        std::vector<uint16_t> px;
        e.draw(px);
        bool lit = false;
        for (auto v : px) lit = lit || v != px[0];
        CHECK(px.size() == 240 * 240 && lit, "its screen shows something");
        const auto info = e.ask({0xF0, 0x7D, 0x46, 0x4C, 0x01, 0xF7});
        const std::string version = info && info->size() > 5 ? std::string(info->begin() + 5, std::find(info->begin() + 5, info->end(), uint8_t(0))) : "";
        CHECK(version == "ChoralRoot 0.23 1-core", "its INFO answers through its USB port: \"" + version + "\" (the single-core package)");
    }
    CHECK(FeluccaEngine::instances(Fl::ChoralRoot) == 0, "an instance gone is counted out");

    {   // the takes, as the frozen file keeps them
        const std::string path = CHORALROOT_FROZEN;
        std::map<std::string, std::string> was;
        { std::ifstream in(path); std::string line; while (std::getline(in, line)) { auto tab = line.find('\t'); if (tab != std::string::npos) was[line.substr(0, tab)] = line.substr(tab + 1); } }
        std::ostringstream now;
        int differ = 0, wrong = 0;
        for (const auto& s : scripts()) {
            FeluccaEngine e(Fl::ChoralRoot);
            const auto t = render(e, 2.0, 256, [&](int frame) { if (s.at) s.at(e, frame); });
            char val[64];
            std::snprintf(val, sizeof val, "%016llx", (unsigned long long)t.hash);
            now << s.name << '\t' << val << '\n';
            if ((t.mean > 1e-4) != s.sounds) { ++wrong; std::printf("  %s: mean %g\n", s.name, t.mean); }
            if (was[s.name] != val) { ++differ; std::printf("  %s: %s, was %s\n", s.name, val, was[s.name].c_str()); }
        }
        CHECK(wrong == 0, "what is played sounds, and nothing played is silent");
        if (std::getenv("CHORALROOT_UPDATE_FROZEN")) { std::ofstream(path) << now.str(); std::printf("  wrote %s\n", path.c_str()); }
        else CHECK(differ == 0, "the takes sound as " + path + " keeps them (CHORALROOT_UPDATE_FROZEN=1 writes it anew)");
        // the same take twice, and in other block sizes
        const auto s = scripts()[6];
        FeluccaEngine a(Fl::ChoralRoot), b(Fl::ChoralRoot), c(Fl::ChoralRoot);
        const auto ta = render(a, 2.0, 256, [&](int f) { s.at(a, f); });
        const auto tb = render(b, 2.0, 256, [&](int f) { s.at(b, f); });
        // (the events at the same frames: a block boundary at each of them in both)
        const auto tc = render(c, 2.0, 32, [&](int f) { if (f % 256 == 0) s.at(c, f); });
        CHECK(ta.hash == tb.hash && ta.hash == tc.hash, "the same take twice, and in 32-frame blocks, gives the same samples");
        FeluccaEngine d(Fl::ChoralRoot);
        const auto td = render(d, 2.0, 37);
        FeluccaEngine q(Fl::ChoralRoot);
        CHECK(td.hash == render(q, 2.0, 256).hash, "rendering in 37-frame blocks gives the same as in 256-frame ones");
    }

    {   // instances: one compiled copy, a state each
        const auto list = scripts();
        FeluccaEngine x0(Fl::ChoralRoot), y0(Fl::ChoralRoot);
        const auto x = render(x0, 1.0, 256, [&](int f) { list[3].at(x0, f); }).hash;
        const auto y = render(y0, 1.0, 256, [&](int f) { list[6].at(y0, f); }).hash;
        CHECK(x != y, "two takes are two sounds");
        std::vector<std::unique_ptr<FeluccaEngine>> many;
        for (int i = 0; i < 8; ++i) many.push_back(std::make_unique<FeluccaEngine>(Fl::ChoralRoot));
        bool allValid = true;
        for (auto& m : many) allValid = allValid && m->valid();
        CHECK(allValid && FeluccaEngine::instances(Fl::ChoralRoot) == 10, "ten instances at once");
        // two rendered in turn, block by block: each as alone
        FeluccaEngine p(Fl::ChoralRoot), q(Fl::ChoralRoot);
        std::vector<float> l(256), r(256);
        uint64_t hp = 0xcbf29ce484222325ull, hq = hp;
        for (int done = 0; done < 44100; done += 256) {
            const int n = std::min(256, 44100 - done);
            list[3].at(p, done);
            p.render(l.data(), r.data(), n);
            for (int i = 0; i < n; ++i) hp = fnv(fnv(hp, l[size_t(i)]), r[size_t(i)]);
            list[6].at(q, done);
            q.render(l.data(), r.data(), n);
            for (int i = 0; i < n; ++i) hq = fnv(fnv(hq, l[size_t(i)]), r[size_t(i)]);
        }
        CHECK(hp == x && hq == y, "two instances rendered in turn sound as each alone");
        FeluccaEngine s(Fl::ChoralRoot), t(Fl::ChoralRoot);
        uint64_t hs = 0, ht = 0;
        std::thread ts([&] { hs = render(s, 1.0, 256, [&](int f) { list[3].at(s, f); }).hash; });
        std::thread tt([&] { ht = render(t, 1.0, 256, [&](int f) { list[6].at(t, f); }).hash; });
        ts.join();
        tt.join();
        CHECK(hs == x && ht == y, "and on two threads at once");
    }

    {   // its stored objects, through its backup protocol (cr_backup.c LIST, GET, PUT)
        const std::vector<int> ids = {1, 8, 14, 15, 16, 17, 18, 19, 20, 21, 23, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
                                      50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62};
        FeluccaEngine a(Fl::ChoralRoot);
        run(a, 0.3);
        int listed = 0, empty = 0;
        std::vector<uint8_t> o;
        for (int id = 0; id < 64; ++id) {
            const bool has = a.object(id, o);
            if (has != (std::find(ids.begin(), ids.end(), id) != ids.end())) std::printf("  object %d: %s\n", id, has ? "listed" : "not listed");
            else if (has) { ++listed; empty += o.empty(); }
        }
        CHECK(listed == 34 && empty == 34, "its 34 objects listed (no music, no samples), all empty on a fresh flash");
        a.button(kSave, true);   // SAVE held alone: the global save, as on the unit
        run(a, 2.0);
        a.button(kSave, false);
        run(a, 0.3);
        std::vector<uint8_t> settings;
        CHECK(a.object(1, settings) && settings.size() == 764 && std::memcmp(settings.data(), "5REP", 4) == 0,
              "SAVE held: the settings record saved (PER5, " + std::to_string(settings.size()) + " bytes)");
        FeluccaEngine b(Fl::ChoralRoot);
        CHECK(b.putObject(1, settings) == 0, "put into another");
        b.objectsDone();
        std::vector<uint8_t> got;
        CHECK(b.object(1, got) && got == settings, "and read back from it the same");
        CHECK(b.putObject(1, {}) != 0 && b.putObject(77, {1, 2, 3}) != 0, "an empty settings record and an object it does not have: refused");
        std::vector<uint8_t> bad = settings;
        bad[100] ^= 0x55;
        CHECK(b.putObject(8, bad) != 0, "an FM6 bank that is not one: refused");
        b.objectsDone();
        CHECK(b.object(1, got) && got == settings, "and nothing changed");
    }

    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
