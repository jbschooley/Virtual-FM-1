#include "FeluccaParams.h"

namespace felparams {

namespace {

// Felucca 0.9-beta's per-track parameters, P_LEVEL .. P_SLDEPTH (core.h), by name.
// P_ED_FX is unused on the device ("kept for the formats"), so it has no host parameter.
const char* const kTrack[] = {
    "level", "atk", "dec", "sus", "rel", "ed_flt", "ed_pit", "ed_shp", nullptr /* ed_fx */,
    "lrate", "lwave", "lphase", "lfade", "ld_pit", "ld_flt", "ld_shp", "ld_amp",
    "amode", "arate", "aoct", "agate", "aswing", "aprob", "ahold", "aorder",
    "root", "scale", "quant", "trans", "slen", "sdiv", "sswing", "sgate",
    "dist", "chor", "dly", "rev", "voice", "glide", "pan", "mute",
    "glmode", "prio", "alloc", "detune", "slcr", "slpat", "slrate", "sldepth"};
constexpr int kEngineFirst = 49;   // P_E0: the engine's eight follow
// Felucca 0.9-beta's globals worth automating (G_*); the rest are its pages' actions and
// MIDI routing.
const struct { const char* name; int index; } kGlobal[] = {
    {"bpm", 0}, {"swing", 1}, {"tune", 3}, {"dtime", 4}, {"dfdbk", 5}, {"dcolor", 6}, {"dmix", 7},
    {"rsize", 8}, {"rdamp", 9}, {"crate", 10}, {"cdepth", 11}, {"drlvl", 25}, {"drrev", 26}};

std::vector<Entry> build() {
    std::vector<Entry> out;
    for (int t = 0; t < 4; ++t) {
        for (int i = 0; i < int(std::size(kTrack)); ++i)
            if (kTrack[i] != nullptr) out.push_back({"fel_t" + juce::String(t + 1) + "_" + kTrack[i], t, i});
        if (t < 3)   // the drum track has no engine
            for (int e = 0; e < 8; ++e) out.push_back({"fel_t" + juce::String(t + 1) + "_e" + juce::String(e), t, kEngineFirst + e});
    }
    for (const auto& g : kGlobal) out.push_back({"fel_" + juce::String(g.name), -1, g.index});
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

void addTo(juce::AudioProcessorValueTreeState::ParameterLayout& layout, std::shared_ptr<TextSource> text) {
    const auto& all = entries();
    std::unique_ptr<juce::AudioProcessorParameterGroup> groups[5];
    const char* titles[5] = {"Felucca part 1", "Felucca part 2", "Felucca part 3", "Felucca drums", "Felucca global"};
    for (int g = 0; g < 5; ++g) groups[g] = std::make_unique<juce::AudioProcessorParameterGroup>("felucca" + juce::String(g), titles[g], " | ");
    for (size_t i = 0; i < all.size(); ++i) {
        const auto& e = all[i];
        // the host's name: where it is and Felucca's name for it in 0.9-beta's terms
        const juce::String where = e.track < 0 ? juce::String() : (e.track < 3 ? "P" + juce::String(e.track + 1) + " " : juce::String("Drums "));
        const juce::String what = e.id.substring(e.track < 0 ? 4 : 7).toUpperCase();   // after "fel_" or "fel_tN_"
        const juce::String name = "Felucca " + where + what;
        const int entry = int(i);
        auto attrs = juce::AudioParameterFloatAttributes().withStringFromValueFunction([text, entry](float v, int) {
            if (text && text->text) return text->text(entry, v);
            return juce::String(v, 3);
        });
        groups[e.track < 0 ? 4 : e.track]->addChild(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{e.id, 2}, name, juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f, attrs));
    }
    for (auto& g : groups) layout.add(std::move(g));
}

}  // namespace felparams
