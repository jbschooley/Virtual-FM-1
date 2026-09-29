// Smoke test for FmSynth: the engine must make sound from a real preset,
// go silent after release, and follow a patch change. Uses the VA preset
// pack's voices (FM voice bytes) from golden.json.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "FmSynth.h"
#include "Fm1Codec.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, msg) do { if (cond) ++g_pass; else { ++g_fail; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } } while (0)

static std::string field(const std::string& text, const std::string& key, size_t from) {
    size_t k = text.find("\"" + key + "\": \"", from);
    if (k == std::string::npos) return "";
    k += key.size() + 5;
    return text.substr(k, text.find('"', k) - k);
}
static fm1::Bytes fromHex(const std::string& h) {
    fm1::Bytes b;
    for (size_t i = 0; i + 1 < h.size(); i += 2) b.push_back(uint8_t(std::strtoul(h.substr(i, 2).c_str(), nullptr, 16)));
    return b;
}
static float rms(const std::vector<float>& v) {
    double s = 0; for (float x : v) s += double(x) * x; return float(std::sqrt(s / double(v.size())));
}
static float peak(const std::vector<float>& v) {
    float p = 0; for (float x : v) p = std::max(p, std::fabs(x)); return p;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: engine_test golden.json\n"); return 2; }
    std::ifstream f(argv[1]); std::stringstream ss; ss << f.rdbuf(); std::string text = ss.str();
    std::vector<fm1::Edit> edits;
    for (size_t pos = 0;;) {
        std::string e = field(text, "edit", pos);
        if (e.empty()) break;
        fm1::Bytes b = fromHex(e); fm1::Edit ed{}; std::copy(b.begin(), b.end(), ed.begin());
        edits.push_back(ed);
        pos = text.find("\"edit\": \"", pos) + 10;
    }
    CHECK(edits.size() >= 2, "golden has patches");

    FmSynth synth;
    synth.prepare(44100.0);
    synth.setPatch(fm1::kInitEdit.data());  // INIT VOICE: a plain sine on OP1

    std::vector<float> buf(4410);
    synth.render(buf.data(), int(buf.size()));
    CHECK(peak(buf) == 0.0f, "silent before any note");

    synth.noteOn(60, 100);
    synth.render(buf.data(), int(buf.size()));
    float loud = rms(buf);
    std::printf("INIT VOICE note: rms %.4f peak %.4f voices %d\n", loud, peak(buf), synth.activeVoices());
    CHECK(loud > 0.01f && peak(buf) <= 1.0f, "INIT VOICE sounds, within range");
    CHECK(synth.activeVoices() == 1, "one voice live");

    // odd buffer sizes must stitch 64-sample blocks correctly: no discontinuity
    std::vector<float> a(37), b(91);
    synth.render(a.data(), 37); synth.render(b.data(), 91);
    float maxStep = 0;
    for (size_t i = 1; i < a.size(); ++i) maxStep = std::max(maxStep, std::fabs(a[i] - a[i - 1]));
    maxStep = std::max(maxStep, std::fabs(b[0] - a.back()));
    for (size_t i = 1; i < b.size(); ++i) maxStep = std::max(maxStep, std::fabs(b[i] - b[i - 1]));
    CHECK(maxStep < 0.2f, "no discontinuity across odd buffer sizes");

    synth.noteOff(60);
    for (int i = 0; i < 40; ++i) synth.render(buf.data(), int(buf.size()));  // 4 s
    CHECK(peak(buf) < 1e-3f, "silent after release");
    CHECK(synth.activeVoices() == 0, "voice freed after release");

    // 12-voice polyphony and stealing
    for (int n = 0; n < 14; ++n) synth.noteOn(48 + n, 90);
    synth.render(buf.data(), int(buf.size()));
    CHECK(synth.activeVoices() == 12, "twelve voices at most");
    synth.allNotesOff();
    for (int i = 0; i < 40; ++i) synth.render(buf.data(), int(buf.size()));
    CHECK(synth.activeVoices() == 0, "all notes off releases everything");

    // a real preset from the pack, with and without the pitch envelope, sounds
    synth.setPatch(edits[0].data());
    synth.noteOn(57, 110);
    synth.render(buf.data(), int(buf.size()));
    std::printf("preset 0 note: rms %.4f peak %.4f\n", rms(buf), peak(buf));
    CHECK(rms(buf) > 0.001f, "pack preset 0 sounds");
    synth.noteOff(57);

    // sustain pedal holds a released note
    synth.allSoundOff();
    synth.setPatch(fm1::kInitEdit.data());
    synth.setSustain(true);
    synth.noteOn(64, 100); synth.noteOff(64);
    synth.render(buf.data(), int(buf.size()));
    CHECK(rms(buf) > 0.01f, "sustain holds the note");
    synth.setSustain(false);
    for (int i = 0; i < 40; ++i) synth.render(buf.data(), int(buf.size()));
    CHECK(peak(buf) < 1e-3f, "pedal up releases");

    // pitch bend changes the pitch: compare zero-crossing counts
    auto zeroCrossings = [](const std::vector<float>& v) { int n = 0; for (size_t i = 1; i < v.size(); ++i) if ((v[i] >= 0) != (v[i - 1] >= 0)) ++n; return n; };
    synth.noteOn(69, 100);
    synth.render(buf.data(), int(buf.size()));
    int zc0 = zeroCrossings(buf);
    synth.setPitchBend(16383);
    synth.render(buf.data(), int(buf.size()));
    synth.render(buf.data(), int(buf.size()));
    int zc1 = zeroCrossings(buf);
    std::printf("A4 zero crossings/0.1s: %d, bent up: %d\n", zc0, zc1);
    CHECK(zc0 > 80 && zc0 < 96, "A4 sine at 440 Hz");
    CHECK(zc1 > zc0 + 5, "pitch bend raises the pitch");

    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
