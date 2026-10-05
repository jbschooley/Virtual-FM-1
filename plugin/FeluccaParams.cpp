#include "FeluccaParams.h"

namespace felparams {

namespace {

// Felucca 1.0's per-track parameters, P_LEVEL .. P_VOIC (core.h), by name. One that
// nothing on the device reads (nullptr) has no host parameter: P_ED_FX ("kept for the
// formats") and the operator envelopes.
const char* const kTrack[] = {
    "level", "atk", "dec", "sus", "rel", "ed_flt", "ed_pit", "ed_shp", nullptr /* ed_fx */,
    "lrate", "lwave", "lphase", "lfade", "ld_pit", "ld_flt", "ld_shp", "ld_amp",
    "amode", "arate", "aoct", "agate", "aswing", "aprob", "ahold", "aorder",
    "root", "scale", "quant", "trans", "slen", "sdiv", "sswing", "sgate",
    "dist", "chor", "dly", "rev", "voice", "glide", "pan", "mute",
    "glmode", "prio", "alloc", "detune", "slcr", "slpat", "slrate", "sldepth",
    "m1src", "m1dst", "m1amt", "m2src", "m2dst", "m2amt",          // modulation matrix (1.0)
    "m3src", "m3dst", "m3amt", "m4src", "m4dst", "m4amt",
    // P_FM1_ATK .. P_FM4_LEVEL: the DIGITAL engine's operator envelopes, which 1.0 does not
    // build (FELUCCA_FM4 0: nothing reads them and the device hides their pages)
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    "chrd", "voic"};                                                // chord keys (1.0)
constexpr int kEngineFirst = 83;   // P_E0: the engine's eight follow
// Felucca 1.0's globals worth automating (G_*); the rest are its pages' actions, MIDI
// routing and the clock source. (0.9-beta's drum level and reverb, 25 and 26, are gone.)
const struct { const char* name; int index; } kGlobal[] = {
    {"bpm", 0}, {"swing", 1}, {"tune", 3}, {"dtime", 4}, {"dfdbk", 5}, {"dcolor", 6}, {"dmix", 7},
    {"rsize", 8}, {"rdamp", 9}, {"crate", 10}, {"cdepth", 11}, {"rtype", 24}};
constexpr int kParts = 4;          // Felucca 1.0: four parts, each with any engine (DRUM is one)
// Felucca 1.0's ranges and defaults ({min, max, def}) for those parameters, so a host's
// "reset to default" means Felucca's default (read from the engine; an engine's own eight
// have none fixed, they change with the engine).
const int kTrackRange[83][3] = {
    {0, 127, 104}, {0, 127, 10}, {0, 127, 70}, {0, 127, 90}, {0, 127, 60}, {-64, 63, 0},
    {-64, 63, 0}, {-64, 63, 0}, {-64, 63, 0}, {0, 127, 60}, {0, 4, 0}, {0, 127, 0},
    {0, 127, 0}, {-64, 63, 0}, {-64, 63, 0}, {-64, 63, 0}, {0, 127, 0}, {0, 6, 0},
    {0, 9, 2}, {1, 4, 1}, {1, 127, 64}, {0, 100, 0}, {0, 127, 127}, {0, 1, 0},
    {0, 1, 0}, {0, 11, 0}, {0, 15, 0}, {0, 2, 0}, {-24, 24, 0}, {1, 64, 16},
    {0, 9, 2}, {0, 100, 0}, {1, 127, 64}, {0, 127, 0}, {0, 127, 0}, {0, 127, 0},
    {0, 127, 0}, {0, 3, 0}, {0, 127, 0}, {-64, 63, 0}, {0, 1, 0}, {0, 1, 0},
    {0, 2, 0}, {0, 1, 0}, {0, 127, 40}, {0, 2, 0}, {1, 16, 1}, {0, 5, 1},
    {0, 127, 127},
    {0, 8, 0}, {0, 19, 0}, {-64, 63, 0}, {0, 8, 0}, {0, 19, 0}, {-64, 63, 0},
    {0, 8, 0}, {0, 19, 0}, {-64, 63, 0}, {0, 8, 0}, {0, 19, 0}, {-64, 63, 0},
    {0, 127, 0}, {0, 127, 0}, {0, 127, 127}, {0, 127, 0}, {0, 127, 127},
    {0, 127, 0}, {0, 127, 0}, {0, 127, 127}, {0, 127, 0}, {0, 127, 127},
    {0, 127, 0}, {0, 127, 0}, {0, 127, 127}, {0, 127, 0}, {0, 127, 127},
    {0, 127, 0}, {0, 127, 0}, {0, 127, 127}, {0, 127, 0}, {0, 127, 127},
    {0, 9, 0}, {0, 4, 0}};
const int kGlobalRange[27][3] = {
    {40, 240, 120}, {0, 100, 0}, {0, 2, 0}, {-50, 50, 0}, {0, 9, 1}, {0, 120, 60},
    {0, 127, 70}, {0, 127, 90}, {0, 127, 90}, {0, 127, 60}, {0, 127, 40}, {0, 127, 60},
    {0, 1, 0}, {0, 0, 0}, {0, 1, 0}, {0, 0, 0}, {1, 4, 1}, {0, 0, 0},
    {0, 1, 0}, {0, 1, 0}, {0, 13, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0},
    {0, 1, 0}, {0, 0, 0}, {0, 0, 0}};

float defaultOf(const int r[3]) { return r[1] > r[0] ? float(r[2] - r[0]) / float(r[1] - r[0]) : 0.0f; }
std::vector<Entry> build() {
    std::vector<Entry> out;
    static_assert(std::size(kTrack) == kEngineFirst, "a name for every common parameter");
    for (int t = 0; t < kParts; ++t) {
        for (int i = 0; i < int(std::size(kTrack)); ++i)
            if (kTrack[i] != nullptr) out.push_back({"fel_t" + juce::String(t + 1) + "_" + kTrack[i], t, i, defaultOf(kTrackRange[i])});
        for (int e = 0; e < 8; ++e) out.push_back({"fel_t" + juce::String(t + 1) + "_e" + juce::String(e), t, kEngineFirst + e, 0.0f});
    }
    for (const auto& g : kGlobal) out.push_back({"fel_" + juce::String(g.name), -1, g.index, defaultOf(kGlobalRange[g.index])});
    return out;
}

}  // namespace

const std::vector<Entry>& entries() {
    static const std::vector<Entry> all = build();
    return all;
}

int indexOf(const juce::String& id) {
    const auto& all = entries();
    for (size_t i = 0; i < all.size(); ++i) if (all[i].id == id) return int(i);
    return -1;
}

int entryFor(int track, int index) {
    const auto& all = entries();
    for (size_t i = 0; i < all.size(); ++i) if (all[i].track == track && all[i].index == index) return int(i);
    return -1;
}

void addTo(juce::AudioProcessorValueTreeState::ParameterLayout& layout, std::shared_ptr<TextSource> text) {
    const auto& all = entries();
    std::unique_ptr<juce::AudioProcessorParameterGroup> groups[5];
    const char* titles[5] = {"Felucca part 1", "Felucca part 2", "Felucca part 3", "Felucca part 4", "Felucca global"};
    for (int g = 0; g < 5; ++g) groups[g] = std::make_unique<juce::AudioProcessorParameterGroup>("felucca" + juce::String(g), titles[g], " | ");
    for (size_t i = 0; i < all.size(); ++i) {
        const auto& e = all[i];
        // the host's name: where it is and Felucca's name for it
        const juce::String where = e.track < 0 ? juce::String() : "P" + juce::String(e.track + 1) + " ";
        const juce::String what = e.id.substring(e.track < 0 ? 4 : 7).toUpperCase();   // after "fel_" or "fel_tN_"
        const juce::String name = "Felucca " + where + what;
        const int entry = int(i);
        auto attrs = juce::AudioParameterFloatAttributes().withStringFromValueFunction([text, entry](float v, int) {
            if (text && text->text) return text->text(entry, v);
            return juce::String("(Felucca)");
        });
        groups[e.track < 0 ? 4 : e.track]->addChild(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{e.id, 2}, name, juce::NormalisableRange<float>(0.0f, 1.0f), e.def, attrs));
    }
    for (auto& g : groups) layout.add(std::move(g));
}

}  // namespace felparams
