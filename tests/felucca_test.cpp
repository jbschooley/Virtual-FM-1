// felucca_test -- Felucca's engines as the plugin drives them (engines/felucca/FeluccaEngine):
// the pool of compiled copies, sound, independence of instances, parameters, engines, presets.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include "FeluccaEngine.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, msg) do { if (cond) ++g_pass; else { ++g_fail; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } } while (0)

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

int main() {
    // the reference: a copy no instance has used yet, playing a fixed phrase
    uint64_t fresh = 0;
    { FeluccaEngine f; fresh = play(f, 1, 128, 300); }

    // ---- the pool ----
    CHECK(FeluccaEngine::copies() >= 2, "there are copies to play");
    {
        std::vector<std::unique_ptr<FeluccaEngine>> all;
        for (int i = 0; i < FeluccaEngine::copies(); ++i) all.push_back(std::make_unique<FeluccaEngine>());
        bool allValid = true;
        for (auto& e : all) allValid = allValid && e->valid();
        CHECK(allValid, "every copy can be taken");
        FeluccaEngine extra;
        CHECK(!extra.valid(), "one more instance than copies gets none");
        CHECK(FeluccaEngine::copiesInUse() == FeluccaEngine::copies(), "all in use");
        all.pop_back();
        FeluccaEngine again;
        CHECK(again.valid(), "a copy given back can be taken again");
    }
    CHECK(FeluccaEngine::copiesInUse() == 0, "every copy is given back");

    // ---- what it is ----
    FeluccaEngine a;
    CHECK(a.tracks() == 4 && a.parts() == 4, "four parts");
    CHECK(a.engines() == 14, "fourteen engine numbers");
    const int fm6 = a.fm6Engine();
    CHECK(a.engineName(0) == "ANALOG" && a.engineName(fm6) == "FM6", "engines by name");
    const auto shown = a.enginesShown();
    CHECK(shown.size() == 13 && shown[0] == 0 && shown[1] == fm6 && std::find(shown.begin(), shown.end(), 1) == shown.end(),
          "thirteen to pick, in Felucca's order, never DIGITAL's retired number");
    std::printf("  engines:");
    for (int e : shown) std::printf(" %s(%zu)", a.engineName(e).c_str(), a.presetNames(e).size());
    std::printf("\n");
    CHECK(!a.presetNames(0).empty(), "ANALOG has presets");
    CHECK(a.engineOf(0) == 0 && a.engineOf(1) == fm6 && a.engineOf(2) == 3 && a.engineName(a.engineOf(3)) == "DRUM",
          "power-on: ANALOG, FM6, LOFI, DRUM");

    // ---- sound ----
    double rms = 0;
    play(a, 1, 256, 400, &rms);
    std::printf("  part 1 chord: rms %.4f\n", rms);
    CHECK(rms > 0.001, "a chord on part 1 sounds");
    a.reset();
    double silent = 0;
    { std::vector<float> l(4096), r(4096); a.render(l.data(), r.data(), 4096); for (float v : l) silent = std::max(silent, double(std::fabs(v))); }
    CHECK(silent == 0.0, "silent with no notes");

    // ---- instances do not share anything: interleaved = alone ----
    {
        FeluccaEngine x, y;
        y.setEngine(0, 4);   // a different engine on the other instance
        uint64_t xAlone = play(x, 1, 100, 300);
        uint64_t yAlone = play(y, 1, 100, 300);
        FeluccaEngine x2, y2;
        y2.setEngine(0, 4);
        std::vector<float> l(100), r(100);
        uint64_t hx = 1469598103934665603ull, hy = hx;
        for (int k = 0; k < 300; ++k) {
            if (k == 0) { noteOn(x2, 1, 48, 100); noteOn(x2, 1, 55, 90); noteOn(x2, 1, 64, 80); noteOn(y2, 1, 48, 100); noteOn(y2, 1, 55, 90); noteOn(y2, 1, 64, 80); }
            if (k == 150) { noteOff(x2, 1, 48); noteOff(x2, 1, 55); noteOff(x2, 1, 64); noteOff(y2, 1, 48); noteOff(y2, 1, 55); noteOff(y2, 1, 64); }
            x2.render(l.data(), r.data(), 100); hx = hash(hash(hx, l), r);
            y2.render(l.data(), r.data(), 100); hy = hash(hash(hy, l), r);
        }
        CHECK(hx == xAlone && hy == yAlone, "two instances rendered in turn sound as each alone");
        CHECK(xAlone != yAlone, "and they really are different sounds");
        // on two threads
        FeluccaEngine x3, y3;
        y3.setEngine(0, 4);
        uint64_t tx = 0, ty = 0;
        std::thread t1([&] { tx = play(x3, 1, 100, 300); });
        std::thread t2([&] { ty = play(y3, 1, 100, 300); });
        t1.join(); t2.join();
        CHECK(tx == xAlone && ty == yAlone, "and on two threads at once");
    }

    // ---- block size does not change the sound ----
    {
        FeluccaEngine p, q;
        uint64_t h32 = play(p, 2, 32, 1200);
        std::vector<float> l(37), r(37);
        // 37-frame blocks cover the same 38400 frames differently: compare sample by sample
        std::vector<float> a1, a2;
        FeluccaEngine p2, q2;
        noteOn(p2, 2, 48, 100); noteOn(q2, 2, 48, 100);
        std::vector<float> bl(256), br(256);
        for (int k = 0; k < 40; ++k) { p2.render(bl.data(), br.data(), 256); a1.insert(a1.end(), bl.begin(), bl.end()); }
        while (a2.size() < a1.size()) { q2.render(l.data(), r.data(), 37); a2.insert(a2.end(), l.begin(), l.end()); }
        a2.resize(a1.size());
        CHECK(a1 == a2, "rendering in 37-frame or 256-frame blocks gives the same samples");
        (void)h32;
    }

    // ---- parameters, engines, presets ----
    {
        FeluccaEngine f;
        const int pe0 = f.firstEngineParam();
        CHECK(f.paramCount() == 91 && pe0 == 83 && f.globalCount() == 27, "91 parameters per track, 8 of them the engine's; 27 globals");
        auto lvl = f.paramDesc(0, 0);
        CHECK(lvl.label == "LVL" || lvl.label == "LEVEL", "the first track parameter is the level");
        f.setParam(0, 0, lvl.max + 50);
        CHECK(f.param(0, 0) == lvl.max, "values are kept in range");
        auto e0 = f.paramDesc(0, pe0);
        f.setEngine(0, f.fm6Engine());
        auto e1 = f.paramDesc(0, pe0);
        CHECK(f.engineOf(0) == f.fm6Engine(), "the engine switches");
        CHECK(e0.label != e1.label, "and the engine parameters' names follow it");
        std::printf("  engine parameter 1: %s on ANALOG, %s on FM6\n", e0.label.c_str(), e1.label.c_str());
        f.setEngine(1, 1);
        CHECK(f.engineOf(1) == f.fm6Engine(), "DIGITAL (retired) arrives as FM6, as on the device");
        f.applyPreset(0, 2);
        CHECK(f.presetOf(0) == 2, "a preset applies");
        auto bpm = f.globalDesc(0);
        f.setGlobal(0, 133);
        CHECK(f.global(0) == std::min(133, int(bpm.max)), "globals read and write");
        int enums = 0;
        for (int id = 0; id < f.paramCount(); ++id) if (!f.paramDesc(0, id).names.empty()) ++enums;
        CHECK(enums > 5, "list parameters carry their value names");
    }

    // ---- SAMPLE's retired preset plays as the one it stands for ----
    {
        FeluccaEngine f;
        int sample = -1;
        for (int e : f.enginesShown()) if (f.engineName(e) == "SAMPLE") sample = e;
        CHECK(sample >= 0, "there is a SAMPLE engine");
        if (sample >= 0) {
            const auto names = f.presetNames(sample);
            CHECK(names.size() > 1 && names[1].empty() && !names[0].empty(), "its preset 1 is an alias, not offered");
            f.setEngine(0, sample);
            f.applyPreset(0, 1);
            CHECK(f.presetOf(0) == 0, "and loads as preset 0");
        }
    }

    // ---- FM6's patch: read, written, and kept over PTCH's slot ----
    {
        FeluccaEngine f;
        const int fm6 = f.fm6Engine();
        CHECK(f.engineOf(1) == fm6, "part 2 plays FM6");
        auto patch = f.fm6Patch(1);
        const auto factory = patch;
        for (int i = 145; i < 155; ++i) patch[size_t(i)] = uint8_t('A' + (i - 145));   // VCED's name
        patch[134] = uint8_t((patch[134] + 5) % 32);                                       // the algorithm
        f.setFm6Patch(1, patch);
        CHECK(f.fm6Patch(1) == patch, "a patch set reads back");
        std::vector<float> l(256), r(256);
        f.render(l.data(), r.data(), 256);   // Felucca's main loop would load PTCH's slot if it disagreed
        CHECK(f.fm6Patch(1) == patch && patch != factory, "and stays: PTCH's slot does not replace it");
        f.applyPreset(1, 1);
        f.render(l.data(), r.data(), 256);
        CHECK(f.fm6Patch(1) != patch, "a preset brings its own patch");
    }

    // ---- more MIDI in one block than Felucca's queue holds: no note left hanging ----
    {
        FeluccaEngine f;
        noteOn(f, 1, 60, 100);
        for (int i = 0; i < 1000; ++i) { uint8_t cc[3] = {0xB0, 1, uint8_t(i & 127)}; f.midi(cc, 3); }   // a mod wheel flood
        noteOff(f, 1, 60);   // past the full queue: lost
        std::vector<float> l(256), r(256);
        double tail = 0;
        for (int k = 0; k < 400; ++k) {   // about 2.3 s
            f.render(l.data(), r.data(), 256);
            if (k >= 300) for (float v : l) tail = std::max(tail, double(std::fabs(v)));
        }
        CHECK(tail < 1e-4, "an overflowing queue stops every note, as on the device (no stuck note)");
    }

    // ---- the virtual device: editor protocol, objects, panel, transport, screen ----
    {
        FeluccaEngine f;
        std::vector<float> l(256), r(256);
        auto settle = [&](int blocks) { for (int k = 0; k < blocks; ++k) f.render(l.data(), r.data(), 256); };
        auto request = [&](std::vector<uint8_t> body) {   // F0 7D 46 4C cmd args F7, then its reply
            std::vector<uint8_t> m = {0xF0, 0x7D, 0x46, 0x4C};
            m.insert(m.end(), body.begin(), body.end());
            m.push_back(0xF7);
            f.sysex(m.data(), int(m.size()));
            settle(8);   // past the next main loop pass (16 ms)
            auto got = f.takeSysex();
            return got.empty() ? std::vector<uint8_t>{} : got.back();
        };
        auto info = request({1});   // INFO: version string first
        const std::string version(info.size() > 5 ? reinterpret_cast<const char*>(info.data() + 5) : "");
        const size_t at = 5 + version.size() + 1;   // then NENGINES, P_COUNT, G_COUNT, NSTEP, P_E0
        CHECK(info.size() > at + 5 && info[4] == 1 && version == "FELUCCA v1.0" && info[at] == 14 && info[at + 1] == 91
              && info[at + 4] == 83, "INFO answers, as Felucca 1.0 (14 engines, 91 parameters, P_E0 83)");
        auto dump = request({4});   // DUMP: engine, preset, P_COUNT + G_COUNT values (2 bytes each)
        CHECK(dump.size() == size_t(4 + 1 + 2 + 2 * (91 + 27) + 1) && dump[5] == 0, "DUMP gives the whole sound, longer than usb.c's queue");
        // SET part 1's level (scope 0, id 0) to 50 through the protocol; the engine sees it
        auto set = request({3, 0, 0, uint8_t((50 + 8192) & 127), uint8_t((50 + 8192) >> 7)});
        CHECK(!set.empty() && f.param(0, 0) == 50, "SET through the editor protocol changes the sound");
        // the boot loader key is taken and does nothing
        const uint8_t uboot[] = {0xF0, 0x22, 0x24, 0x35, 0x7D, 0xF7};
        f.sysex(uboot, 6);
        settle(8);
        CHECK(request({25}).size() == 7, "the boot loader key is ignored: the editor still answers PING");

        // the music now, as an object, back into another instance
        f.setEngine(2, 6);
        f.setParam(2, 0, 77);
        std::vector<uint8_t> runtime;
        CHECK(f.object(0, runtime) && runtime.size() == 3584
              && (runtime[0] | runtime[1] << 8 | runtime[2] << 16 | uint32_t(runtime[3]) << 24) == 0x46554E38u,
              "the music now is a FUN8 project");
        FeluccaEngine g;
        CHECK(g.putObject(0, runtime) == 0 && g.engineOf(2) == 6 && g.param(2, 0) == 77, "and restores into another instance");
        runtime[100] ^= 0x55;   // its hash no longer matches
        CHECK(g.putObject(0, runtime) == 2, "a damaged project is refused");
        std::vector<uint8_t> empty;
        CHECK(g.object(2, empty) && empty.empty(), "an empty project slot has no bytes");
        std::vector<uint8_t> settingsObj;
        CHECK(g.object(1, settingsObj) && !settingsObj.empty(), "the settings object");

        // PLAY on the panel starts the transport, PLAY again stops it
        const auto names = f.buttonNames();
        const int play = int(std::find(names.begin(), names.end(), std::string("PLAY")) - names.begin());
        CHECK(play < int(names.size()) && !f.playing(), "a PLAY button; stopped at power-on");
        f.button(play, true); settle(8); f.button(play, false); settle(8);
        const bool started = f.playing();
        f.button(play, true); settle(8); f.button(play, false); settle(8);
        CHECK(started && !f.playing(), "PLAY starts and stops the sequencer");
        f.transport(true); settle(2);
        CHECK(f.playing(), "the plugin starts it too");
        f.transport(false); settle(2);

        // a flash command from the editor while playing: Felucca stops the transport first
        // (ed_flash_stop waits for the audio side, which must not hang the plugin)
        f.transport(true); settle(2);
        const bool wasPlaying = f.playing();
        auto stored = request({19, 0, 'T', 'S', 'T', 0});   // UP_STORE slot 1, named TST
        CHECK(wasPlaying && stored.size() == 8 && stored[5] == 0 && stored[6] == 0 && !f.playing(),
              "UP_STORE while playing stops the transport and stores (no hang)");

        std::vector<uint16_t> screen;
        f.draw(screen);
        int lit = 0;
        for (auto v : screen) if (v != screen[0]) ++lit;
        CHECK(screen.size() == 240u * 240u && lit > 1000, "the screen draws");
    }

    // ---- a copy given back is as good as new ----
    {
        {   // leave a copy in a mess: other engines, edited values, tempo, notes still held, tails
            FeluccaEngine dirty;
            dirty.setEngine(0, 3); dirty.setEngine(1, 8); dirty.setEngine(2, 4);
            dirty.setParam(0, 0, 10); dirty.setGlobal(0, 200);
            noteOn(dirty, 1, 60, 127); noteOn(dirty, 2, 64, 127); noteOn(dirty, 10, 36, 127);
            std::vector<float> l(4096), r(4096);
            for (int k = 0; k < 20; ++k) dirty.render(l.data(), r.data(), 4096);
        }
        // every copy, taken again, plays the phrase exactly as a never-used one did
        std::vector<std::unique_ptr<FeluccaEngine>> all;
        bool same = true;
        for (int i = 0; FeluccaEngine::copiesInUse() < FeluccaEngine::copies(); ++i) {   // every free copy
            all.push_back(std::make_unique<FeluccaEngine>());
            const auto h = play(*all.back(), 1, 128, 300);
            if (h != fresh) std::printf("  copy taken %d plays differently\n", i);
            same = same && h == fresh;
        }
        CHECK(same, "every copy, given back after use, plays exactly as a never-used copy");
    }

    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
