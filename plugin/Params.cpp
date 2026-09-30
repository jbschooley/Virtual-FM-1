#include "Params.h"

namespace {

struct VcedDef { int offset; const char* name; int max; const char* const* choices; int choiceCount; int def; };

const char* const kCurves[] = {"-LIN", "-EXP", "+EXP", "+LIN"};
const char* const kOscModes[] = {"Ratio", "Fixed"};
const char* const kOnOff[] = {"Off", "On"};
const char* const kLfoWaves[] = {"Triangle", "Saw Down", "Saw Up", "Square", "Sine", "S&Hold"};

// per operator, in VCED order (offset within the 21-byte block)
const struct { const char* name; int max; const char* const* choices; int choiceCount; int def; } kOpDefs[21] = {
    {"EG Rate 1", 99, nullptr, 0, 99}, {"EG Rate 2", 99, nullptr, 0, 99}, {"EG Rate 3", 99, nullptr, 0, 99}, {"EG Rate 4", 99, nullptr, 0, 99},
    {"EG Level 1", 99, nullptr, 0, 99}, {"EG Level 2", 99, nullptr, 0, 99}, {"EG Level 3", 99, nullptr, 0, 99}, {"EG Level 4", 99, nullptr, 0, 0},
    {"Break Point", 99, nullptr, 0, 0}, {"L Depth", 99, nullptr, 0, 0}, {"R Depth", 99, nullptr, 0, 0},
    {"L Curve", 3, kCurves, 4, 0}, {"R Curve", 3, kCurves, 4, 0}, {"Rate Scaling", 7, nullptr, 0, 0},
    {"Amp Mod Sens", 3, nullptr, 0, 0}, {"Key Vel Sens", 7, nullptr, 0, 0}, {"Output Level", 99, nullptr, 0, 0},
    {"Osc Mode", 1, kOscModes, 2, 0}, {"Coarse", 31, nullptr, 0, 1}, {"Fine", 99, nullptr, 0, 0}, {"Detune", 14, nullptr, 0, 7},
};

const VcedDef kGlobalDefs[] = {
    {126, "Pitch EG Rate 1", 99, nullptr, 0, 99}, {127, "Pitch EG Rate 2", 99, nullptr, 0, 99}, {128, "Pitch EG Rate 3", 99, nullptr, 0, 99}, {129, "Pitch EG Rate 4", 99, nullptr, 0, 99},
    {130, "Pitch EG Level 1", 99, nullptr, 0, 50}, {131, "Pitch EG Level 2", 99, nullptr, 0, 50}, {132, "Pitch EG Level 3", 99, nullptr, 0, 50}, {133, "Pitch EG Level 4", 99, nullptr, 0, 50},
    {134, "Algorithm", 31, nullptr, 0, 0}, {135, "Feedback", 7, nullptr, 0, 0}, {136, "Osc Sync", 1, kOnOff, 2, 1},
    {137, "LFO Speed", 99, nullptr, 0, 35}, {138, "LFO Delay", 99, nullptr, 0, 0}, {139, "LFO Pitch Mod Depth", 99, nullptr, 0, 0}, {140, "LFO Amp Mod Depth", 99, nullptr, 0, 0},
    {141, "LFO Sync", 1, kOnOff, 2, 1}, {142, "LFO Wave", 5, kLfoWaves, 6, 0}, {143, "Pitch Mod Sens", 7, nullptr, 0, 3}, {144, "Transpose", 48, nullptr, 0, 24},
};

juce::String opName(int vcedOp) { return "OP" + juce::String(6 - vcedOp) + " "; }   // VCED lists OP6 first

std::unique_ptr<juce::RangedAudioParameter> makeParam(const juce::String& id, const juce::String& name, int max,
                                                      const char* const* choices, int choiceCount, int def, int displayOffset = 0) {
    if (choices) {
        juce::StringArray sa;
        for (int i = 0; i < choiceCount; ++i) sa.add(choices[i]);
        return std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{id, 1}, name, sa, def);
    }
    auto attrs = juce::AudioParameterIntAttributes()
        .withStringFromValueFunction([displayOffset](int v, int) { return juce::String(v + displayOffset); })
        .withValueFromStringFunction([displayOffset](const juce::String& s) { return s.getIntValue() - displayOffset; });
    return std::make_unique<juce::AudioParameterInt>(juce::ParameterID{id, 1}, name, 0, max, def, attrs);
}

}  // namespace

juce::String Params::vcedId(int offset) { return "v" + juce::String(offset); }
juce::String Params::fxParamId(int fx, int i) { return "fx" + juce::String(fx) + "p" + juce::String(i); }
juce::String Params::fxOnId(int fx) { return "fx" + juce::String(fx) + "on"; }
juce::String Params::fxTypeId(int fx) { return "fx" + juce::String(fx) + "type"; }
juce::String Params::envId(int i) { return "env" + juce::String(i); }

juce::AudioProcessorValueTreeState::ParameterLayout Params::layout() {
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    for (int op = 0; op < 6; ++op) {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup>("op" + juce::String(6 - op), opName(op).trim(), " | ");
        for (int i = 0; i < 21; ++i) {
            const auto& d = kOpDefs[i];
            int off = op * 21 + i;
            int displayOffset = (i == 20) ? -7 : 0;   // detune shown -7..7
            group->addChild(makeParam(vcedId(off), opName(op) + d.name, d.max, d.choices, d.choiceCount, d.def, displayOffset));
        }
        l.add(std::move(group));
    }
    auto global = std::make_unique<juce::AudioProcessorParameterGroup>("global", "Global", " | ");
    for (const auto& d : kGlobalDefs) {
        int displayOffset = d.offset == 134 ? 1 : d.offset == 144 ? -24 : 0;   // algorithm 1..32, transpose -24..24
        global->addChild(makeParam(vcedId(d.offset), d.name, d.max, d.choices, d.choiceCount, d.def, displayOffset));
    }
    l.add(std::move(global));

    for (int e = 0; e < fm1::kEffects; ++e) {
        auto g = std::make_unique<juce::AudioProcessorParameterGroup>("fx" + juce::String(e), fm1::kEffectNames[e], " | ");
        g->addChild(makeParam(fxOnId(e), juce::String(fm1::kEffectNames[e]) + " On", 1, kOnOff, 2, 0));
        if (fm1::kEffectTypeCount[e])
            g->addChild(makeParam(fxTypeId(e), juce::String(fm1::kEffectNames[e]) + " Type", fm1::kEffectTypeCount[e] - 1,
                                  fm1::kEffectTypeNames[e], fm1::kEffectTypeCount[e], 0));
        for (int i = 0; i < 3; ++i)
            if (fm1::kEffectParamNames[e][i][0])
                g->addChild(makeParam(fxParamId(e, i), juce::String(fm1::kEffectNames[e]) + " " + fm1::kEffectParamNames[e][i],
                                      fm1::kEffectParamMax[e][i], nullptr, 0, 0));
        l.add(std::move(g));
    }
    auto env = std::make_unique<juce::AudioProcessorParameterGroup>("env", "Envelope", " | ");
    env->addChild(makeParam(kEnvOn, "Envelope On", 1, kOnOff, 2, 0));
    const char* envNames[4] = {"Envelope Attack", "Envelope Decay", "Envelope Sustain", "Envelope Release"};
    const int envDefs[4] = {0, 0, 100, 0};
    for (int i = 0; i < 4; ++i) env->addChild(makeParam(envId(i), envNames[i], 100, nullptr, 0, envDefs[i]));
    l.add(std::move(env));
    return l;
}

Params::Params(juce::AudioProcessorValueTreeState& a) : apvts(a) {
    for (int off = 0; off < 145; ++off) bindings_.push_back({vcedId(off), Kind::Vced, off, 0, nullptr});
    for (int e = 0; e < fm1::kEffects; ++e) {
        bindings_.push_back({fxOnId(e), Kind::FxOn, e, 0, nullptr});
        if (fm1::kEffectTypeCount[e]) bindings_.push_back({fxTypeId(e), Kind::FxType, e, 0, nullptr});
        for (int i = 0; i < 3; ++i)
            if (fm1::kEffectParamNames[e][i][0]) bindings_.push_back({fxParamId(e, i), Kind::FxParam, e, i, nullptr});
    }
    bindings_.push_back({kEnvOn, Kind::EnvOn, 0, 0, nullptr});
    for (int i = 0; i < 4; ++i) bindings_.push_back({envId(i), Kind::Env, i, 0, nullptr});
    for (auto& b : bindings_) {
        b.param = apvts.getParameter(b.id);
        jassert(b.param != nullptr);
        apvts.addParameterListener(b.id, this);
    }
}

Params::~Params() {
    for (auto& b : bindings_) apvts.removeParameterListener(b.id, this);
}

void Params::parameterChanged(const juce::String&, float) {
    if (!loading_) changed = true;
}

static int intValue(const juce::RangedAudioParameter* p) {
    return juce::roundToInt(p->convertFrom0to1(p->getValue()));
}
static void setInt(juce::RangedAudioParameter* p, int v) {
    p->setValueNotifyingHost(p->convertTo0to1(float(v)));
}

void Params::load(const fm1::Sound& s) {
    loading_ = true;
    fm1::Edit e = fm1::unpackVoice(s.voice);
    fm1::FxChain fx = fm1::fxFromRecord(s.record);
    fm1::Envelope env = fm1::envFromRecord(s.record);
    for (auto& b : bindings_) {
        int v = 0;
        switch (b.kind) {
            case Kind::Vced:    v = e[size_t(b.a)]; break;
            case Kind::FxParam: v = fx.fx[size_t(b.a)].p[size_t(b.b)]; break;
            case Kind::FxOn:    v = fx.fx[size_t(b.a)].on ? 1 : 0; break;
            case Kind::FxType:  v = fx.fx[size_t(b.a)].type; break;
            case Kind::Env:     v = b.a == 0 ? env.a : b.a == 1 ? env.d : b.a == 2 ? env.s : env.r; break;
            case Kind::EnvOn:   v = env.on ? 1 : 0; break;
        }
        int max = juce::roundToInt(b.param->getNormalisableRange().end);
        setInt(b.param, juce::jlimit(0, max, v));
    }
    committed_.clear();
    for (const auto& b : bindings_) committed_.push_back(intValue(b.param));
    loading_ = false;
    changed = true;   // the engine reloads from the parameters
}

void Params::commit(fm1::Sound& s) {
    if (committed_.size() != bindings_.size()) {   // never loaded: nothing to compare against
        committed_.clear();
        for (const auto& b : bindings_) committed_.push_back(intValue(b.param));
        return;
    }
    fm1::Edit e = fm1::unpackVoice(s.voice);
    bool voiceChanged = false;
    auto chainPos = [&s](int effect) {
        for (int k = 0; k < fm1::kEffects; ++k) if (s.record[size_t(27 + 3 * k)] == effect) return k;
        return -1;
    };
    for (size_t i = 0; i < bindings_.size(); ++i) {
        const auto& b = bindings_[i];
        int v = intValue(b.param);
        if (v == committed_[i]) continue;
        committed_[i] = v;
        auto u = uint8_t(v);
        switch (b.kind) {
            case Kind::Vced:    e[size_t(b.a)] = u; voiceChanged = true; break;
            case Kind::FxParam: s.record[size_t(3 * b.a + b.b)] = u; break;
            case Kind::FxOn:    if (int k = chainPos(b.a); k >= 0) s.record[size_t(28 + 3 * k)] = u; break;
            case Kind::FxType:  if (int k = chainPos(b.a); k >= 0) s.record[size_t(29 + 3 * k)] = u; break;
            case Kind::Env:     s.record[size_t(54 + b.a)] = u; break;
            case Kind::EnvOn:   s.record[58] = u; break;
        }
    }
    if (voiceChanged) s.voice = fm1::packVoice(e);
    s.hasRecord = true;
}

void Params::fillVced(uint8_t* vced) const {
    for (const auto& b : bindings_)
        if (b.kind == Kind::Vced) vced[b.a] = uint8_t(intValue(b.param));
}

fm1::FxChain Params::fxChain(const fm1::FxChain& orderFrom) const {
    fm1::FxChain c = orderFrom;
    for (const auto& b : bindings_) {
        switch (b.kind) {
            case Kind::FxParam: c.fx[size_t(b.a)].p[size_t(b.b)] = intValue(b.param); break;
            case Kind::FxOn:    c.fx[size_t(b.a)].on = intValue(b.param) != 0; break;
            case Kind::FxType:  c.fx[size_t(b.a)].type = intValue(b.param); break;
            default: break;
        }
    }
    return c;
}

fm1::Envelope Params::envelope() const {
    fm1::Envelope env;
    for (const auto& b : bindings_) {
        if (b.kind == Kind::Env) {
            int v = intValue(b.param);
            (b.a == 0 ? env.a : b.a == 1 ? env.d : b.a == 2 ? env.s : env.r) = v;
        } else if (b.kind == Kind::EnvOn) env.on = intValue(b.param) != 0;
    }
    return env;
}
