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
        // the settings record's sizes, as ChoralRoot 0.23 takes them (cr_backup.c crb_size_ok: 8 + panel_t .. persist_t,
        // whose parts are fixed in every build: cr_settings_t asserted 192 bytes, panel_t 32, the favourites 520): its own
        // PER5 is 764 bytes; an older record (PER1 .. PER4) shorter. A record another firmware left in that flash (an
        // FM-1 that ran others: a "PER3" of 1552 bytes, its panel "PAN5", listed by ChoralRoot as its object 1) is no
        // record of its own: refused, as the device itself refuses it, and nothing changes
        std::vector<uint8_t> per3(52, 0), foreign(1552, 0);
        std::memcpy(per3.data(), "3REP", 4);
        std::memcpy(per3.data() + 16, settings.data() + 16, 32);   // (the panel, PAN5, as PER5 keeps it at the same place)
        std::memcpy(foreign.data(), "3REP", 4);
        std::memcpy(foreign.data() + 16, settings.data() + 16, 32);
        FeluccaEngine c(Fl::ChoralRoot);
        CHECK(c.putObject(1, per3) == 0, "an older settings record (PER3, 52 bytes): taken");
        CHECK(b.putObject(1, foreign) != 0 && (b.objectsDone(), b.object(1, got)) && got == settings,
              "a 1552-byte record another firmware left (\"PER3\"): refused as the device refuses it, nothing changed");
    }

    {   // other SysEx goes to the firmware's own parser, as on the unit (usb.c sysex_byte): a DX7 single voice is the FM6
        // part's new voice. Three instances from power-on, the same actions: one sent the voice, one the same frame with
        // its checksum broken (FM6 ignores it), one nothing; then a held chord
        std::vector<uint8_t> voice = {0xF0, 0x43, 0x00, 0x00, 0x01, 0x1B};
        for (int op = 0; op < 6; ++op)   // every operator a sine at full level, 1:1, its envelope wide open
            for (int v : {99, 99, 99, 99, 99, 99, 99, 0, 39, 0, 0, 0, 0, 0, 0, 0, 99, 0, 1, 0, 7}) voice.push_back(uint8_t(v));
        for (int v : {99, 99, 99, 99, 50, 50, 50, 50, 31, 7, 1, 35, 0, 0, 0, 1, 0, 3, 24}) voice.push_back(uint8_t(v));   // algorithm 32
        for (char ch : std::string("PROBEVOICE")) voice.push_back(uint8_t(ch));
        uint32_t sum = 0;
        for (size_t i = 6; i < voice.size(); ++i) sum += voice[i];
        voice.push_back(uint8_t(-sum & 0x7F));
        voice.push_back(0xF7);
        auto broken = voice;
        broken[broken.size() - 2] ^= 1;
        auto take = [](const std::vector<uint8_t>* frame) {
            FeluccaEngine e(Fl::ChoralRoot);
            render(e, 0.5, 256);
            if (frame) e.sysex(frame->data(), int(frame->size()));
            return render(e, 1.0, 256, [&e](int frame) {
                if (frame == 0x2000) for (int n : {60, 64, 67}) { const uint8_t m[3] = {0x90, uint8_t(n), 100}; e.midi(m, 3); }
            });
        };
        const auto none = take(nullptr), dx7 = take(&voice), bad = take(&broken);
        CHECK(voice.size() == 163 && dx7.hash != none.hash && bad.hash == none.hash && dx7.mean > 0.0,
              "a DX7 single voice changes what the FM6 part plays; one with its checksum broken does not");
    }

    {   // the emulator's switches in the host's environment (EMU_MASTER, EMU_OUTPUT ...: its scripts') change nothing
        auto take = [] {
            FeluccaEngine e(Fl::ChoralRoot);
            return render(e, 1.0, 256, [&e](int frame) { if (frame == 0x2000) { const uint8_t m[3] = {0x90, 60, 100}; e.midi(m, 3); } });
        };
        const auto plain = take();
        setenv("EMU_MASTER", "0", 1);
        setenv("EMU_OUTPUT", "classic", 1);
        const auto set = take();
        unsetenv("EMU_MASTER");
        unsetenv("EMU_OUTPUT");
        CHECK(plain.mean > 0.0 && set.hash == plain.hash, "EMU_MASTER=0 and EMU_OUTPUT=classic set: it plays the same");
    }

    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
