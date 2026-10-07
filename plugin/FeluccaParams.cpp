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

// SLOOP 2.4.1's (core.h P_*, G_*; ranges and defaults read from it): Felucca 1.0's names up to the
// slicer (48), then CHORD (49), 2.4's FILT, STRUM, VLEAD (50-52) and the engine's eight from 53. Not
// P_ED_FX (8): SLOOP's level trim, which its presets set. Its drum track (4) has its kit (P_E0),
// pattern, slicer and filter only, as the device shows it; its level and reverb are globals (DRLVL,
// DRREV). 2.4's three are listed after the rest (their ids added, none moved).
constexpr int kSloopCommon = 50, kSloopEngineFirst = 53;
const struct { const char* name; int range[3]; } kSloop24[3] = {
    {"filt", {-64, 63, 0}}, {"strum", {-60, 60, 0}}, {"vlead", {0, 1, 0}}};
const int kSloopTrackRange[50][3] = {
    {0, 127, 104}, {0, 127, 10}, {0, 127, 70}, {0, 127, 90}, {0, 127, 60}, {-64, 63, 0},
    {-64, 63, 0}, {-64, 63, 0}, {-64, 63, 0}, {0, 127, 60}, {0, 4, 0}, {0, 127, 0},
    {0, 127, 0}, {-64, 63, 0}, {-64, 63, 0}, {-64, 63, 0}, {0, 127, 0}, {0, 5, 0},
    {0, 5, 2}, {1, 4, 1}, {1, 127, 64}, {0, 100, 0}, {0, 127, 127}, {0, 1, 0},
    {0, 1, 0}, {0, 11, 0}, {0, 15, 0}, {0, 2, 0}, {-24, 24, 0}, {1, 64, 16},
    {0, 8, 2}, {0, 100, 0}, {1, 127, 64}, {0, 127, 0}, {0, 127, 0}, {0, 127, 0},
    {0, 127, 0}, {0, 3, 0}, {0, 127, 0}, {-64, 63, 0}, {0, 1, 0}, {0, 1, 0},
    {0, 2, 0}, {0, 1, 0}, {0, 127, 40}, {0, 2, 0}, {1, 16, 1}, {0, 5, 1},
    {0, 127, 127}, {0, 5, 0}};
const int kSloopDrumIds[] = {29, 30, 31, 32, 45, 46, 47, 48};   // the pattern and the slicer
const struct { const char* name; int index; int range[3]; } kSloopGlobal[] = {
    {"bpm", 0, {40, 240, 90}}, {"swing", 1, {0, 100, 0}}, {"tune", 3, {-50, 50, 0}}, {"dtime", 4, {0, 7, 1}},
    {"dfdbk", 5, {0, 120, 60}}, {"dcolor", 6, {0, 127, 70}}, {"dmix", 7, {0, 127, 90}}, {"rsize", 8, {0, 127, 90}},
    {"rdamp", 9, {0, 127, 60}}, {"crate", 10, {0, 127, 40}}, {"cdepth", 11, {0, 127, 60}},
    {"drlvl", 25, {0, 127, 100}}, {"drrev", 26, {0, 127, 16}}, {"dust", 27, {0, 127, 0}},
    {"duck", 28, {0, 127, 0}}, {"filt", 29, {-64, 63, 0}}};

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
    // SLOOP's
    auto name = [](int i) { return i == 49 ? juce::String("chord") : juce::String(kTrack[i]); };
    for (int t = 0; t < 3; ++t) {
        for (int i = 0; i < kSloopCommon; ++i)
            if (i != 8) out.push_back({"slp_t" + juce::String(t + 1) + "_" + name(i), t, i, defaultOf(kSloopTrackRange[i]), true});
        for (int e = 0; e < 8; ++e) out.push_back({"slp_t" + juce::String(t + 1) + "_e" + juce::String(e), t, kSloopEngineFirst + e, 0.0f, true});
    }
    out.push_back({"slp_dr_kit", 3, kSloopEngineFirst, 0.0f, true});
    for (int i : kSloopDrumIds) out.push_back({"slp_dr_" + name(i), 3, i, defaultOf(kSloopTrackRange[i]), true});
    for (const auto& g : kSloopGlobal) out.push_back({"slp_" + juce::String(g.name), -1, g.index, defaultOf(g.range), true});
    for (int t = 0; t < 3; ++t)   // (2.4)
        for (int k = 0; k < 3; ++k)
            out.push_back({"slp_t" + juce::String(t + 1) + "_" + kSloop24[k].name, t, kSloopCommon + k, defaultOf(kSloop24[k].range), true});
    out.push_back({"slp_dr_filt", 3, kSloopCommon, defaultOf(kSloop24[0].range), true});
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

int entryFor(int track, int index, bool sloop) {
    const auto& all = entries();
    for (size_t i = 0; i < all.size(); ++i) if (all[i].sloop == sloop && all[i].track == track && all[i].index == index) return int(i);
    return -1;
}

void addTo(juce::AudioProcessorValueTreeState::ParameterLayout& layout, std::shared_ptr<TextSource> text) {
    const auto& all = entries();
    std::unique_ptr<juce::AudioProcessorParameterGroup> groups[10];
    const char* titles[10] = {"Felucca part 1", "Felucca part 2", "Felucca part 3", "Felucca part 4", "Felucca global",
                              "SLOOP part 1", "SLOOP part 2", "SLOOP part 3", "SLOOP drums", "SLOOP global"};
    const char* ids[10] = {"felucca0", "felucca1", "felucca2", "felucca3", "felucca4", "sloop0", "sloop1", "sloop2", "sloop3", "sloop4"};
    for (int g = 0; g < 10; ++g) groups[g] = std::make_unique<juce::AudioProcessorParameterGroup>(ids[g], titles[g], " | ");
    for (size_t i = 0; i < all.size(); ++i) {
        const auto& e = all[i];
        // the host's name: which firmware, where it is and the firmware's name for it
        const juce::String where = e.track < 0 ? juce::String() : e.sloop && e.track == 3 ? juce::String("DRUMS ") : "P" + juce::String(e.track + 1) + " ";
        const juce::String what = e.id.substring(e.track < 0 ? 4 : 7).toUpperCase();   // after "fel_" / "fel_tN_" ("slp_", "slp_tN_", "slp_dr_")
        const juce::String name = juce::String(e.sloop ? "SLOOP " : "Felucca ") + where + what;
        const int entry = int(i);
        auto attrs = juce::AudioParameterFloatAttributes().withStringFromValueFunction([text, entry, sloop = e.sloop](float v, int) {
            if (text && text->text) return text->text(entry, v);
            return juce::String(sloop ? "(SLOOP)" : "(Felucca)");
        });
        const int g = (e.sloop ? 5 : 0) + (e.track < 0 ? 4 : e.track);
        groups[g]->addChild(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{e.id, e.sloop ? 3 : 2}, name, juce::NormalisableRange<float>(0.0f, 1.0f), e.def, attrs));
    }
    for (auto& g : groups) layout.add(std::move(g));
}

}  // namespace felparams
