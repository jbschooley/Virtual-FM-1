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

// SLOOP's (core.h P_*, G_*; ranges and defaults read from it): Felucca 1.0's names up to the slicer
// (48), then CHORD (49), 2.4's FILT, STRUM, VLEAD (50-52), 2.6's ten (53-62, below) and the engine's
// eight (from 53 until 2.5, from 63 in 2.6). Not
// P_ED_FX (8): SLOOP's level trim, which its presets set. Its drum track (4) has its kit (P_E0),
// pattern, slicer and filter only, as the device shows it; its level and reverb are globals (DRLVL,
// DRREV). 2.4's three are listed after the rest (their ids added, none moved).
// SLOOP 2.6 added ten before the engine's eight (53..62, below), which now start at 63: the
// engine's host parameters keep their ids and follow them there.
constexpr int kSloopCommon = 50, kSloopEngineFirst = 63;
const struct { const char* name; int range[3]; } kSloop24[3] = {
    {"filt", {-64, 63, 0}}, {"strum", {-60, 60, 0}}, {"vlead", {0, 1, 0}}};
const int kSloopTrackRange[50][3] = {
    {0, 127, 104}, {0, 127, 10}, {0, 127, 70}, {0, 127, 90}, {0, 127, 60}, {-64, 63, 0},
    {-64, 63, 0}, {-64, 63, 0}, {-64, 63, 0}, {0, 127, 60}, {0, 4, 0}, {0, 127, 0},
    {0, 127, 0}, {-64, 63, 0}, {-64, 63, 0}, {-64, 63, 0}, {0, 127, 0}, {0, 14, 0},   // (17 AMODE: 2.7's fifteen)
    {0, 5, 2}, {1, 4, 1}, {1, 127, 64}, {0, 100, 0}, {0, 127, 127}, {0, 1, 0},
    {0, 1, 0}, {0, 11, 0}, {0, 15, 0}, {0, 2, 0}, {-24, 24, 0}, {1, 64, 16},
    {0, 8, 2}, {0, 100, 0}, {1, 127, 64}, {0, 127, 0}, {0, 127, 0}, {0, 127, 0},
    {0, 127, 0}, {0, 3, 0}, {0, 127, 0}, {-64, 63, 0}, {0, 1, 0}, {0, 1, 0},
    {0, 2, 0}, {0, 1, 0}, {0, 127, 40}, {0, 2, 0}, {1, 16, 1}, {0, 5, 1},
    {0, 127, 127}, {0, 5, 0}};
const int kSloopDrumIds[] = {29, 30, 31, 32, 45, 46, 47, 48};   // the pattern and the slicer
const struct { const char* name; int index; int range[3]; } kSloopGlobal[] = {
    {"bpm", 0, {40, 240, 90}}, {"swing", 1, {0, 100, 0}}, {"tune", 3, {-50, 50, 0}}, {"dtime", 4, {0, 8, 1}},   // (2.6: 1/4D)
    {"dfdbk", 5, {0, 120, 60}}, {"dcolor", 6, {0, 127, 70}}, {"dmix", 7, {0, 127, 90}}, {"rsize", 8, {0, 127, 90}},
    {"rdamp", 9, {0, 127, 60}}, {"crate", 10, {0, 127, 40}}, {"cdepth", 11, {0, 127, 60}},
    {"drlvl", 25, {0, 127, 100}}, {"drrev", 26, {0, 127, 16}}, {"dust", 27, {0, 127, 0}},
    {"duck", 28, {0, 127, 0}}, {"filt", 29, {-64, 63, 0}}};

// Melodee 0.13's (core.h P_*, G_*; ranges and defaults read from it): Felucca 1.0's numbering, with
// P_ED_FX (8) now the sequencer's playback quantize (QNT), the delay send (35) unused (its shared delay
// is gone), 70 scales (26), QNT's fifth mode (27), and DEG (83, the MPC pad's scale degree) before the
// engine's eight (84). SCL, QNT and DEG are the song's (one scale for every part: the device copies
// them to all, ui.c scale_share): one host parameter each ("mel_scale", ...), on part 1. Its globals:
// Felucca's without the delay; not TUNE, A4, BOOT or the DRUM channel, which are the device's
// settings (saved when they change: TUNE by glo_poll, the others at once).
constexpr int kMelodeeEngineFirst = 84, kMelodeeEngineFirst013 = 84;   // (the ids below: 0.13's numbering; indexOn maps them for 1.0)
const struct { int index; const char* name; int range[3]; } kMelodeeChanged[] = {{8, "pqnt", {0, 10, 0}}};
const struct { int index; const char* name; int range[3]; } kMelodeeSong[] = {
    {26, "scale", {0, 69, 0}}, {27, "quant", {0, 4, 0}}, {83, "mpcdeg", {1, 12, 1}}};
const struct { const char* name; int index; int range[3]; } kMelodeeGlobal[] = {
    {"bpm", 0, {40, 240, 120}}, {"swing", 1, {0, 100, 0}}, {"rsize", 8, {0, 127, 90}},
    {"rdamp", 9, {0, 127, 60}}, {"crate", 10, {0, 127, 40}}, {"cdepth", 11, {0, 127, 60}}, {"rtype", 24, {0, 1, 0}}};
constexpr int kMelodeeHint = 5;
// what 1.2 retired (its effects: the sends 33..36, the SLICER 45..48, the reverb and chorus 8..11, RTYPE 24;
// padding on 1.3): on 0.13 and 1.1.0 only
constexpr int kMelodeeBefore12 = (1 << 1) | (1 << 2);
bool melodeeRetired(int index, bool global) {
    return global ? (index >= 8 && index <= 11) || index == 24 : (index >= 33 && index <= 36) || (index >= 45 && index <= 48);
}

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
            out.push_back({"slp_t" + juce::String(t + 1) + "_" + kSloop24[k].name, t, kSloopCommon + k, defaultOf(kSloop24[k].range), true, 4});
    out.push_back({"slp_dr_filt", 3, kSloopCommon, defaultOf(kSloop24[0].range), true, 4});
    // Melodee's
    for (int t = 0; t < kParts; ++t) {
        const juce::String p = "mel_t" + juce::String(t + 1) + "_";
        for (int i = 0; i < kEngineFirst; ++i) {
            if (i == 35 || i == 26 || i == 27) continue;   // (the delay send: no delay; the song's scale: below)
            const auto* changed = std::find_if(std::begin(kMelodeeChanged), std::end(kMelodeeChanged), [i](const auto& c) { return c.index == i; });
            const int only = melodeeRetired(i, false) ? kMelodeeBefore12 : -1;
            if (changed != std::end(kMelodeeChanged)) { out.push_back({p + changed->name, t, i, defaultOf(changed->range), false, kMelodeeHint, true, false, only}); continue; }
            if (kTrack[i] != nullptr) out.push_back({p + kTrack[i], t, i, defaultOf(kTrackRange[i]), false, kMelodeeHint, true, false, only});
        }
        for (int e = 0; e < 8; ++e) out.push_back({p + "e" + juce::String(e), t, kMelodeeEngineFirst + e, 0.0f, false, kMelodeeHint, true});
    }
    for (const auto& c : kMelodeeSong) out.push_back({"mel_" + juce::String(c.name), 0, c.index, defaultOf(c.range), false, kMelodeeHint, true, true});
    for (const auto& g : kMelodeeGlobal)
        out.push_back({"mel_" + juce::String(g.name), -1, g.index, defaultOf(g.range), false, kMelodeeHint, true, false, melodeeRetired(g.index, true) ? kMelodeeBefore12 : -1});
    // SLOOP 2.5's: the drums' delay send (GLO > DRUMS > DLY, global 32), after everything before it
    const int drdly[3] = {0, 127, 0};
    out.push_back({"slp_drdly", -1, 32, defaultOf(drdly), true, 6});
    // SLOOP 2.6's (core.h, params.c): every track's INSERT (the drums' too), and the synth parts'
    // ANALOG filter TYPE, ENV SYNC and LFO 2; GLO > TEXTURE, the song's. Not GLO > MIDI or MIDI 2's
    // channels and BEND: settings of the FM-1, as SYNC and OUT
    static const struct { const char* name; int index; int range[3]; bool drums; } k26[] = {
        {"itype", 53, {0, 8, 0}, true}, {"ia", 54, {0, 127, 64}, true}, {"ib", 55, {0, 127, 96}, true},
        {"ic", 56, {0, 127, 96}, true}, {"imix", 57, {0, 127, 127}, true}, {"ftype", 58, {0, 2, 0}, false},
        {"esync", 59, {0, 1, 0}, false}, {"lsync", 60, {0, 10, 0}, false}, {"ltrig", 61, {0, 1, 0}, false},
        {"lpol", 62, {0, 1, 0}, false}};
    for (int t = 0; t < 3; ++t)
        for (const auto& k : k26) out.push_back({"slp_t" + juce::String(t + 1) + "_" + k.name, t, k.index, defaultOf(k.range), true, 7});
    for (const auto& k : k26) if (k.drums) out.push_back({"slp_dr_" + juce::String(k.name), 3, k.index, defaultOf(k.range), true, 7});
    static const struct { const char* name; int index; int range[3]; } k26g[] = {
        {"txsrc", 37, {0, 10, 0}}, {"txlvl", 38, {0, 127, 64}}, {"txtone", 39, {-64, 63, 0}}, {"txmove", 40, {0, 127, 40}},
        {"txrate", 41, {0, 4, 2}}, {"txwhen", 42, {0, 1, 0}}, {"txduck", 43, {0, 1, 0}}};
    for (const auto& g : k26g) out.push_back({"slp_" + juce::String(g.name), -1, g.index, defaultOf(g.range), true, 7});
    // Melodee 1.0's (1.1.0 core.h, params.c): the DRUM lanes' levels (84..91), LFO 2's SYNC TRIG POL
    // (92..94) and SPRD (95), on 1.1.0 and 1.3 (the same ids); the delay send (35) and the shared delay
    // (globals 4..7) with its TYPE and WEAR (25, 26) on 1.1.0 only (1.2 removed them)
    {
        constexpr int kHint = 8;
        constexpr int k11 = 1 << 2, k13 = 1 << 0;   // (releases: 1.1.0 kept, Previous2; 1.3 the current one)
        static const struct { const char* name; int index; int range[3]; int releases; } part[] = {
            {"dly", 35, {0, 127, 0}, k11}, {"kick", 84, {0, 127, 127}, k11 | k13}, {"snare", 85, {0, 127, 127}, k11 | k13},
            {"clap", 86, {0, 127, 127}, k11 | k13}, {"hatcl", 87, {0, 127, 127}, k11 | k13}, {"hatop", 88, {0, 127, 127}, k11 | k13},
            {"tom", 89, {0, 127, 127}, k11 | k13}, {"rim", 90, {0, 127, 127}, k11 | k13}, {"bell", 91, {0, 127, 127}, k11 | k13},
            {"lsync", 92, {0, 10, 0}, k11 | k13}, {"ltrig", 93, {0, 1, 0}, k11 | k13}, {"lpol", 94, {0, 1, 0}, k11 | k13},
            {"sprd", 95, {0, 127, 0}, k11 | k13}};
        for (int t = 0; t < kParts; ++t)
            for (const auto& k : part)
                out.push_back({"mel_t" + juce::String(t + 1) + "_" + k.name, t, k.index, defaultOf(k.range), false, kHint, true, false, k.releases});
        static const struct { const char* name; int index; int range[3]; } glob[] = {
            {"dtime", 4, {0, 13, 11}}, {"dfdbk", 5, {0, 120, 50}}, {"dcolor", 6, {0, 127, 51}}, {"dmix", 7, {0, 127, 85}},
            {"dtype", 25, {0, 3, 1}}, {"dwear", 26, {0, 127, 64}}};
        for (const auto& g : glob) out.push_back({"mel_" + juce::String(g.name), -1, g.index, defaultOf(g.range), false, kHint, true, false, k11});
    }
    // Felucca 1.1 to 1.5's (1.5.1 core.h, params.c), on the current release only: the DRUM lanes' levels
    // (83..90), LFO 2's SYNC TRIG POL (91..93), QNTZ (94), SPRD (95), the INSERT (96..100), ANALOG's
    // filter TYPE (101) and ENV SYNC (102)
    {
        constexpr int kHint = 9;
        static const struct { const char* name; int index; int range[3]; } part[] = {
            {"kick", 83, {0, 127, 127}}, {"snare", 84, {0, 127, 127}}, {"clap", 85, {0, 127, 127}}, {"hatcl", 86, {0, 127, 127}},
            {"hatop", 87, {0, 127, 127}}, {"tom", 88, {0, 127, 127}}, {"rim", 89, {0, 127, 127}}, {"bell", 90, {0, 127, 127}},
            {"lsync", 91, {0, 10, 0}}, {"ltrig", 92, {0, 1, 0}}, {"lpol", 93, {0, 1, 0}}, {"sqnt", 94, {0, 1, 1}}, {"sprd", 95, {0, 127, 0}},
            {"itype", 96, {0, 8, 0}}, {"ia", 97, {0, 127, 64}}, {"ib", 98, {0, 127, 96}}, {"ic", 99, {0, 127, 96}}, {"imix", 100, {0, 127, 127}},
            {"ftype", 101, {0, 2, 0}}, {"esync", 102, {0, 1, 0}}};
        for (int t = 0; t < kParts; ++t)
            for (const auto& k : part)
                out.push_back({"fel_t" + juce::String(t + 1) + "_" + k.name, t, k.index, defaultOf(k.range), false, kHint, false, false, 1 << 0});
    }
    // Melodee 1.3's (1.3.4 core.h, params.c), on 1.3 only: each part's three inserts (TYPE MIX RATE CHAR,
    // 96..107) and its three sends (108..110); the shared return's three slots (globals 27..38), its
    // RETURN (39) and MODE (40, a serial chain or independent sends)
    {
        constexpr int kHint = 10;
        static const struct { const char* name; int index; int range[3]; } part[] = {
            {"fx1type", 96, {0, 9, 0}}, {"fx1mix", 97, {0, 127, 0}}, {"fx1rate", 98, {0, 127, 60}}, {"fx1char", 99, {0, 127, 64}},
            {"fx2type", 100, {0, 9, 0}}, {"fx2mix", 101, {0, 127, 0}}, {"fx2rate", 102, {0, 127, 60}}, {"fx2char", 103, {0, 127, 64}},
            {"fx3type", 104, {0, 9, 0}}, {"fx3mix", 105, {0, 127, 0}}, {"fx3rate", 106, {0, 127, 60}}, {"fx3char", 107, {0, 127, 64}},
            {"send", 108, {0, 127, 0}}, {"send2", 109, {0, 127, 0}}, {"send3", 110, {0, 127, 0}}};
        for (int t = 0; t < kParts; ++t)
            for (const auto& k : part)
                out.push_back({"mel_t" + juce::String(t + 1) + "_" + k.name, t, k.index, defaultOf(k.range), false, kHint, true, false, 1 << 0});
        static const struct { const char* name; int index; int range[3]; } glob[] = {
            {"rfx1type", 27, {0, 9, 0}}, {"rfx1mix", 28, {0, 127, 127}}, {"rfx1rate", 29, {0, 127, 60}}, {"rfx1char", 30, {0, 127, 64}},
            {"rfx2type", 31, {0, 9, 0}}, {"rfx2mix", 32, {0, 127, 127}}, {"rfx2rate", 33, {0, 127, 60}}, {"rfx2char", 34, {0, 127, 64}},
            {"rfx3type", 35, {0, 9, 0}}, {"rfx3mix", 36, {0, 127, 127}}, {"rfx3rate", 37, {0, 127, 60}}, {"rfx3char", 38, {0, 127, 64}},
            {"freturn", 39, {0, 127, 100}}, {"fxmode", 40, {0, 1, 0}}};
        for (const auto& g : glob) out.push_back({"mel_" + juce::String(g.name), -1, g.index, defaultOf(g.range), false, kHint, true, false, 1 << 0});
    }
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

int indexOn(const Entry& e, int release) {
    if (e.only >= 0 && !(e.only & (1 << release))) return -1;
    // (the ids from before a release added parameters before the engine's eight, in their first release's
    // numbering: on a later release those eight are that many higher. Melodee 1.0: twelve from 84, and 1.3
    // fifteen more (its inserts and sends); Felucca 1.1 to 1.5: twenty from 83)
    if (e.only < 0 && e.track >= 0 && !e.sloop) {
        if (e.melodee && e.index >= kMelodeeEngineFirst013) return release == 2 ? e.index + 12 : release == 0 ? e.index + 12 + 15 : e.index;
        if (!e.melodee && release == 0 && e.index >= kEngineFirst) return e.index + 20;
    }
    return e.index;
}

int entryFor(int track, int index, bool sloop, bool melodee, int release) {
    const auto& all = entries();
    for (size_t i = 0; i < all.size(); ++i)
        if (all[i].sloop == sloop && all[i].melodee == melodee && indexOn(all[i], release) == index && (all[i].track == track || (all[i].song && track >= 0)))
            return int(i);   // (a song-wide one is every part's)
    return -1;
}

void addTo(juce::AudioProcessorValueTreeState::ParameterLayout& layout, std::shared_ptr<TextSource> text) {
    const auto& all = entries();
    std::unique_ptr<juce::AudioProcessorParameterGroup> groups[15];
    const char* titles[15] = {"Felucca part 1", "Felucca part 2", "Felucca part 3", "Felucca part 4", "Felucca global",
                              "SLOOP part 1", "SLOOP part 2", "SLOOP part 3", "SLOOP drums", "SLOOP global",
                              "Melodee part 1", "Melodee part 2", "Melodee part 3", "Melodee part 4", "Melodee global"};
    const char* ids[15] = {"felucca0", "felucca1", "felucca2", "felucca3", "felucca4", "sloop0", "sloop1", "sloop2", "sloop3", "sloop4",
                           "melodee0", "melodee1", "melodee2", "melodee3", "melodee4"};
    for (int g = 0; g < 15; ++g) groups[g] = std::make_unique<juce::AudioProcessorParameterGroup>(ids[g], titles[g], " | ");
    for (size_t i = 0; i < all.size(); ++i) {
        const auto& e = all[i];
        // the host's name: which firmware, where it is and the firmware's name for it
        // (ids: "fel_tN_x" / "slp_dr_x" a part's, "fel_x" a global's or, Melodee's scale, the song's)
        const juce::String rest = e.id.fromFirstOccurrenceOf("_", false, false);
        const bool part = rest.startsWith("dr_") || (rest.length() > 3 && rest[0] == 't' && juce::CharacterFunctions::isDigit(rest[1]) && rest[2] == '_');
        const juce::String where = !part ? juce::String() : e.sloop && e.track == 3 ? juce::String("DRUMS ") : "P" + juce::String(e.track + 1) + " ";
        const juce::String what = (part ? rest.fromFirstOccurrenceOf("_", false, false) : rest).toUpperCase();
        const juce::String firmware = e.melodee ? "Melodee" : e.sloop ? "SLOOP" : "Felucca";
        const juce::String name = firmware + " " + where + what;
        const int entry = int(i);
        auto attrs = juce::AudioParameterFloatAttributes().withStringFromValueFunction([text, entry, firmware](float v, int) {
            if (text && text->text) return text->text(entry, v);
            return "(" + firmware + ")";
        });
        const int g = (e.melodee ? 10 : e.sloop ? 5 : 0) + (e.track < 0 ? 4 : e.track);
        groups[g]->addChild(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{e.id, e.hint ? e.hint : e.sloop ? 3 : 2}, name, juce::NormalisableRange<float>(0.0f, 1.0f), e.def, attrs));
    }
    for (auto& g : groups) layout.add(std::move(g));
}

}  // namespace felparams
