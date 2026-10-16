// fm1open_test -- M-VAVE's FM1 Open V12 in the plugin (engines/fm1open): it powers on, its 16 demo
// projects play as tests/fm1open-frozen.txt keeps them (a hash of each one's first two seconds; a
// change in the firmware or in how it is built shows here), instances are independent (one compiled
// copy, a state each), its projects export, import and survive the host's project.
//
//     fm1open_test            (FM1OPEN_UPDATE_FROZEN=1 writes the frozen file anew)
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "Fm1OpenEngine.h"

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) ++g_pass; else { ++g_fail; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, std::string(msg).c_str()); } } while (0)

static uint64_t fnv(uint64_t h, float x) {
    uint32_t bits;
    std::memcpy(&bits, &x, 4);
    for (int i = 0; i < 4; ++i) h = (h ^ ((bits >> (8 * i)) & 0xFF)) * 0x100000001b3ull;
    return h;
}

struct Take { uint64_t hash = 0xcbf29ce484222325ull; double mean = 0.0; };
// `seconds` of a demo project from power-on: loaded, PLAY pressed, rendered in blocks of `block`
static Take play(Fm1OpenEngine& e, int demo, double seconds, int block = 256) {
    Take t;
    auto p = e.factoryProject(demo);
    if (!p || !e.importProject(*p)) return t;
    e.transport(true);
    std::vector<float> l(static_cast<size_t>(block)), r(static_cast<size_t>(block));
    const int total = int(seconds * Fm1OpenEngine::kRate);
    double sum = 0.0;
    for (int done = 0; done < total; done += block) {
        const int n = std::min(block, total - done);
        e.render(l.data(), r.data(), n);
        for (int i = 0; i < n; ++i) { t.hash = fnv(fnv(t.hash, l[size_t(i)]), r[size_t(i)]); sum += std::fabs(l[size_t(i)]) + std::fabs(r[size_t(i)]); }
    }
    t.mean = sum / (2.0 * total);
    return t;
}

int main() {
    {   // powers on; its demos, by name
        Fm1OpenEngine e;
        CHECK(e.valid() && Fm1OpenEngine::instances() == 1, "FM1 Open powers on (its state laid out as fel_state built it)");
        const auto names = e.factoryNames();
        CHECK(names.size() == 16 && names[0] == "NEON KEYS" && names[15] == "AFTERGLOW", "its 16 demo projects (NEON KEYS .. AFTERGLOW)");
        std::vector<uint16_t> px;
        e.draw(px);
        bool lit = false;
        for (auto v : px) lit = lit || v != 0;
        CHECK(px.size() == 240 * 240 && lit, "its screen shows something at power-on");
        CHECK(e.buttonNames().size() == 14 && e.buttonNames()[12] == "PLAY" && e.knobNames().size() == 7, "its panel: 14 buttons (PLAY the 13th), 7 encoders");
    }
    CHECK(Fm1OpenEngine::instances() == 0, "an instance gone is counted out");

    {   // the demos, as the frozen file keeps them
        const std::string path = FM1OPEN_FROZEN;
        std::map<std::string, std::string> was;
        { std::ifstream in(path); std::string line; while (std::getline(in, line)) { auto tab = line.find('\t'); if (tab != std::string::npos) was[line.substr(0, tab)] = line.substr(tab + 1); } }
        std::ostringstream now;
        int differ = 0, silent = 0;
        Fm1OpenEngine e;
        const auto names = e.factoryNames();
        for (int d = 0; d < int(names.size()); ++d) {
            e.reset();
            const auto t = play(e, d, 2.0);
            char key[64], val[64];
            std::snprintf(key, sizeof key, "demo %02d %s", d, names[size_t(d)].c_str());
            std::snprintf(val, sizeof val, "%016llx", (unsigned long long)t.hash);
            now << key << '\t' << val << '\n';
            if (t.mean < 1e-4) ++silent;
            if (was[key] != val) { ++differ; std::printf("  %s: %s, was %s\n", key, val, was[key].c_str()); }
        }
        CHECK(silent == 0, "every demo project plays (none silent)");
        if (std::getenv("FM1OPEN_UPDATE_FROZEN")) { std::ofstream(path) << now.str(); std::printf("  wrote %s\n", path.c_str()); }
        else CHECK(differ == 0, "the demos sound as " + path + " keeps them (FM1OPEN_UPDATE_FROZEN=1 writes it anew)");
        e.reset();
        const auto a = play(e, 10, 1.0, 256);
        e.reset();
        const auto b = play(e, 10, 1.0, 37);
        CHECK(a.hash == b.hash, "rendering in 37-frame or 256-frame blocks gives the same samples");
    }

    {   // instances: one compiled copy, a state each
        Fm1OpenEngine alone;
        const auto x = play(alone, 1, 1.0).hash;
        Fm1OpenEngine other;
        const auto y = play(other, 6, 1.0).hash;
        CHECK(x != y, "two demos are two sounds");
        std::vector<std::unique_ptr<Fm1OpenEngine>> many;
        for (int i = 0; i < 8; ++i) many.push_back(std::make_unique<Fm1OpenEngine>());
        bool allValid = true;
        for (auto& m : many) allValid = allValid && m->valid();
        CHECK(allValid && Fm1OpenEngine::instances() == 10, "ten instances at once");
        // two rendered in turn, block by block: each as alone
        Fm1OpenEngine p, q;
        auto pp = p.factoryProject(1), qp = q.factoryProject(6);
        p.importProject(*pp); q.importProject(*qp);
        p.transport(true); q.transport(true);
        std::vector<float> l(256), r(256);
        uint64_t hp = 0xcbf29ce484222325ull, hq = hp;
        for (int done = 0; done < 44100; done += 256) {
            const int n = std::min(256, 44100 - done);
            p.render(l.data(), r.data(), n);
            for (int i = 0; i < n; ++i) hp = fnv(fnv(hp, l[size_t(i)]), r[size_t(i)]);
            q.render(l.data(), r.data(), n);
            for (int i = 0; i < n; ++i) hq = fnv(fnv(hq, l[size_t(i)]), r[size_t(i)]);
        }
        CHECK(hp == x && hq == y, "two instances rendered in turn sound as each alone");
        // on two threads at once
        Fm1OpenEngine s, t;
        uint64_t hs = 0, ht = 0;
        std::thread ts([&] { hs = play(s, 1, 1.0).hash; }), tt([&] { ht = play(t, 6, 1.0).hash; });
        ts.join(); tt.join();
        CHECK(hs == x && ht == y, "and on two threads at once");
    }

    {   // projects: export and import, and the flash as the host's project keeps it
        Fm1OpenEngine e;
        auto p = e.factoryProject(3);
        CHECK(p && p->size() == size_t(Fm1OpenEngine::kProjectBytes) && e.importProject(*p), "a demo project's bytes import");
        auto out = e.exportProject();
        CHECK(out && out->size() == size_t(Fm1OpenEngine::kProjectBytes) && e.projectName() == "GLASS TIDE", "and export again: " + e.projectName());
        Fm1OpenEngine f;
        CHECK(out && f.importProject(*out) && f.projectName() == "GLASS TIDE", "an exported project imports into another instance");
        // its storage: saved with the host's project and given back
        const auto flash = e.projects();
        Fm1OpenEngine g;
        CHECK(flash.size() == 69632 && g.setProjects(flash) && g.projects() == flash, "its projects and settings round trip through the host's project");
        CHECK(e.heapUsed() > 0 && e.heapUsed() < 1536u * 1024u, "its heap stays in its arena: " + std::to_string(e.heapUsed()) + " bytes");
    }

    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
