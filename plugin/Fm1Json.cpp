#include "Fm1Json.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>

#include "Fm1Record.h"

namespace fm1json {

const char* const kFormat = "virtual-fm1";
const char* const kSchemaUrl = "https://raw.githubusercontent.com/jbschooley/Virtual-FM-1/main/docs/virtual-fm1.schema.json";

namespace {

using juce::String;
using juce::StringArray;
using juce::var;

// ---- building and reading values ---------------------------------------------

var newObject() { return var(new juce::DynamicObject()); }
void put(var& o, const char* key, const var& v) { o.getDynamicObject()->setProperty(key, v); }
bool has(const var& o, const char* key) { return o.isObject() && o.getDynamicObject()->hasProperty(key); }
var get(const var& o, const char* key) { return o.getDynamicObject()->getProperty(key); }

bool isWhole(const var& v) {
    if (v.isInt() || v.isInt64()) return true;
    return v.isDouble() && std::floor(double(v)) == double(v) && std::abs(double(v)) < 1e9;
}

// One byte-sized setting. JSON shows `byte + shift` for numbers, the name for
// choices and true/false for switches.
struct Spec {
    const char* key;
    int offset;                     // VCED offset (operator specs: within the 21-byte block)
    int min, max, shift;
    const char* const* choices = nullptr;
    int choiceCount = 0;
    bool isBool = false;
};

String expectation(const Spec& s) {
    if (s.isBool) return "true or false";
    if (s.choices) {
        StringArray names;
        for (int i = 0; i < s.choiceCount; ++i) names.add("\"" + String(s.choices[i]) + "\"");
        return "one of " + names.joinIntoString(", ");
    }
    return "a whole number from " + String(s.min + s.shift) + " to " + String(s.max + s.shift);
}

// A stored byte outside its setting's range (some factory presets have them)
// gives no readable value: the field is left out and raw keeps the byte.
var exportValue(const Spec& s, int byte) {
    if (byte < s.min || byte > s.max) return {};
    if (s.isBool) return var(byte == 1);
    if (s.choices) return var(s.choices[byte]);
    return var(byte + s.shift);
}
void putValue(var& o, const Spec& s, int byte) {
    var v = exportValue(s, byte);
    if (!v.isVoid()) put(o, s.key, v);
}

// Reads one value into `byte`. A value equal to what `byte` already holds is
// always accepted, so a file written from odd bytes reads back unchanged.
void importValue(const var& v, const Spec& s, const String& at, int& byte, StringArray& errors) {
    int raw = 0;
    if (s.isBool && v.isBool()) {
        raw = bool(v) ? 1 : 0;
    } else if (s.choices && v.isString()) {
        raw = -1;
        for (int i = 0; i < s.choiceCount; ++i)
            if (v.toString().equalsIgnoreCase(s.choices[i])) raw = i;
        if (raw < 0) { errors.add(at + ": \"" + v.toString() + "\" is not " + expectation(s)); return; }
    } else if (isWhole(v)) {
        if (s.isBool || s.choices) {   // a number only where it repeats an odd stored byte
            if (int(v) != byte) errors.add(at + ": expected " + expectation(s));
            return;
        }
        raw = int(v) - s.shift;
    } else {
        errors.add(at + ": expected " + expectation(s));
        return;
    }
    if (raw == byte) return;
    if (raw < s.min || raw > s.max) {
        errors.add(at + ": " + String(raw + s.shift) + " is outside " + String(s.min + s.shift) + " to " + String(s.max + s.shift));
        return;
    }
    byte = raw;
}

void importKey(const var& o, const Spec& s, const String& path, int& byte, StringArray& errors) {
    if (has(o, s.key)) importValue(get(o, s.key), s, path + "." + s.key, byte, errors);
}

void importEditByte(const var& o, const Spec& s, int base, const String& path, fm1::Edit& e, StringArray& errors) {
    int b = e[size_t(base + s.offset)];
    importKey(o, s, path, b, errors);
    e[size_t(base + s.offset)] = uint8_t(b);
}

// Four numbers at offsets base+first .. base+first+3 (envelope rates or levels).
void putFour(var& o, const char* key, const fm1::Edit& e, int at) {
    var a;
    for (int i = 0; i < 4; ++i) {
        if (e[size_t(at + i)] > 99) return;   // out of range: left out, raw keeps it
        a.append(int(e[size_t(at + i)]));
    }
    put(o, key, a);
}
void importFour(const var& o, const char* key, int at, const String& path, fm1::Edit& e, StringArray& errors) {
    if (!has(o, key)) return;
    var a = get(o, key);
    String p = path + "." + key;
    if (!a.isArray() || a.size() != 4) { errors.add(p + ": expected a list of 4 whole numbers from 0 to 99"); return; }
    Spec s{key, 0, 0, 99, 0};
    for (int i = 0; i < 4; ++i) {
        int b = e[size_t(at + i)];
        importValue(a[i], s, p + "[" + String(i) + "]", b, errors);
        e[size_t(at + i)] = uint8_t(b);
    }
}

String toHex(const uint8_t* d, size_t n) { return String::toHexString(d, int(n), 0); }
bool fromHex(const var& v, uint8_t* out, size_t n) {
    if (!v.isString()) return false;
    String s = v.toString();
    if (size_t(s.length()) != 2 * n || !s.containsOnly("0123456789abcdefABCDEF")) return false;
    for (size_t i = 0; i < n; ++i) out[i] = uint8_t(s.substring(int(2 * i), int(2 * i + 2)).getHexValue32());
    return true;
}

// ---- the voice -----------------------------------------------------------------

const char* const kCurveNames[] = {"-lin", "-exp", "+exp", "+lin"};
const char* const kModeNames[] = {"ratio", "fixed"};
const char* const kWaveNames[] = {"triangle", "saw down", "saw up", "square", "sine", "sample and hold"};

const Spec kGlobal[] = {
    {"algorithm", 134, 0, 31, 1},
    {"feedback", 135, 0, 7, 0},
    {"oscKeySync", 136, 0, 1, 0, nullptr, 0, true},
    {"transpose", 144, 0, 48, -24},
};
const Spec kLfo[] = {
    {"wave", 142, 0, 5, 0, kWaveNames, 6},
    {"speed", 137, 0, 99, 0},
    {"delay", 138, 0, 99, 0},
    {"pitchModDepth", 139, 0, 99, 0},
    {"ampModDepth", 140, 0, 99, 0},
    {"keySync", 141, 0, 1, 0, nullptr, 0, true},
    {"pitchModSensitivity", 143, 0, 7, 0},
};
const Spec kOp[] = {   // offsets within an operator's 21 bytes
    {"mode", 17, 0, 1, 0, kModeNames, 2},
    {"coarse", 18, 0, 31, 0},
    {"fine", 19, 0, 99, 0},
    {"detune", 20, 0, 14, -7},
    {"level", 16, 0, 99, 0},
    {"velocitySensitivity", 15, 0, 7, 0},
    {"ampModSensitivity", 14, 0, 3, 0},
};
const Spec kScaling[] = {
    {"breakpoint", 8, 0, 99, 0},
    {"leftDepth", 9, 0, 99, 0},
    {"rightDepth", 10, 0, 99, 0},
    {"leftCurve", 11, 0, 3, 0, kCurveNames, 4},
    {"rightCurve", 12, 0, 3, 0, kCurveNames, 4},
    {"rate", 13, 0, 7, 0},
};
constexpr int kNameAt = 145, kNameLen = 10;

int opBase(int opNumber) { return (6 - opNumber) * 21; }   // the VCED lists OP6 first

// ---- the settings record ---------------------------------------------------------

String lower(const char* s) { return String(s).toLowerCase(); }

const int kKeyTrackPercent[4] = {0, 33, 67, 100};
const char* const kFilterKeys[7] = {"cutoff", "resonance", "envelope", "decay", "shape", "velocity", "lfo"};
const int kFilterBytes[7] = {23, 24, 25, 51, 49, 47, 50};
int filterValue(const fm1::VaFilter& f, int i) {
    const int v[7] = {f.cutoff, f.resonance, f.envelope, f.decay, f.shape, f.velocity, f.lfo};
    return v[i];
}

// byte 26: bit 4 on, bits 0-1 type, bits 2-3 key tracking; starts from the defaults when unset
void setFilterBits(fm1::Record& r, int field, int v) {
    uint8_t cur = (r[26] & 0x80) ? r[26] : uint8_t(0x80);
    if (field == 0) cur = uint8_t((cur & ~0x10) | (v ? 0x10 : 0));
    if (field == 1) cur = uint8_t((cur & ~0x03) | (v & 3));
    if (field == 2) cur = uint8_t((cur & ~0x0C) | ((v & 3) << 2));
    r[26] = cur;
}

const char* const kEnvKeys[4] = {"attack", "decay", "sustain", "release"};

void importEffects(const var& list, const String& path, fm1::Record& r, StringArray& errors) {
    if (!list.isArray() || list.size() != fm1::kEffects) {
        errors.add(path + ": expected a list of all six effects in chain order");
        return;
    }
    fm1::FxChain fc = fm1::fxFromRecord(r);
    std::array<int, fm1::kEffects> order{};
    std::set<int> seen;
    for (int k = 0; k < fm1::kEffects; ++k) {
        var item = list[k];
        String at = path + "[" + String(k) + "]";
        int id = -1;
        if (item.isObject() && get(item, "effect").isString())
            for (int e = 0; e < fm1::kEffects; ++e)
                if (get(item, "effect").toString().equalsIgnoreCase(fm1::kEffectNames[e])) id = e;
        if (id < 0) { errors.add(at + ".effect: expected one of \"filter\", \"reverb\", \"delay\", \"distortion\", \"chorus\", \"phaser\""); return; }
        if (!seen.insert(id).second) { errors.add(at + ".effect: \"" + lower(fm1::kEffectNames[id]) + "\" is listed twice"); return; }
        order[size_t(k)] = id;
    }
    auto chainPos = [&r](int effect) {
        for (int k = 0; k < fm1::kEffects; ++k) if (r[size_t(27 + 3 * k)] == effect) return k;
        return -1;
    };
    bool orderWritten = false;
    auto writeOrder = [&] {   // the effect ids with each effect's on and type, in the new order
        for (int k = 0; k < fm1::kEffects; ++k) {
            int id = order[size_t(k)];
            r[size_t(27 + 3 * k)] = uint8_t(id);
            r[size_t(28 + 3 * k)] = fc.fx[size_t(id)].on ? 1 : 0;
            r[size_t(29 + 3 * k)] = uint8_t(fc.fx[size_t(id)].type);
        }
        orderWritten = true;
    };
    if (order != fc.order) writeOrder();
    for (int k = 0; k < fm1::kEffects; ++k) {
        int e = order[size_t(k)];
        var item = list[k];
        String at = path + "[" + String(k) + "]";
        int on = fc.fx[size_t(e)].on ? 1 : 0;
        importKey(item, Spec{"on", 0, 0, 1, 0, nullptr, 0, true}, at, on, errors);
        int type = fc.fx[size_t(e)].type;
        if (fm1::kEffectTypeCount[e] > 0) {
            std::vector<std::string> lowered;
            std::vector<const char*> ptrs;
            for (int t = 0; t < fm1::kEffectTypeCount[e]; ++t) lowered.push_back(lower(fm1::kEffectTypeNames[e][t]).toStdString());
            for (const auto& name : lowered) ptrs.push_back(name.c_str());
            importKey(item, Spec{"type", 0, 0, fm1::kEffectTypeCount[e] - 1, 0, ptrs.data(), int(ptrs.size())}, at, type, errors);
        } else if (has(item, "type")) {
            errors.add(at + ".type: " + lower(fm1::kEffectNames[e]) + " has no type");
        }
        if (on != (fc.fx[size_t(e)].on ? 1 : 0) || type != fc.fx[size_t(e)].type) {
            fc.fx[size_t(e)].on = on != 0;
            fc.fx[size_t(e)].type = type;
            if (chainPos(e) < 0 && !orderWritten) writeOrder();   // a damaged chain: write it whole
            int pos = chainPos(e);
            r[size_t(28 + 3 * pos)] = uint8_t(on);
            r[size_t(29 + 3 * pos)] = uint8_t(type);
        }
        for (int i = 0; i < 3; ++i) {
            if (!fm1::kEffectParamNames[e][i][0]) continue;
            String key = lower(fm1::kEffectParamNames[e][i]);
            int v = fc.fx[size_t(e)].p[size_t(i)];
            importKey(item, Spec{key.toRawUTF8(), 0, 0, fm1::kEffectParamMax[e][i], 0}, at, v, errors);
            if (v != fc.fx[size_t(e)].p[size_t(i)]) r[size_t(3 * e + i)] = uint8_t(v);
        }
    }
}

void importFilter(const var& o, const String& path, fm1::Record& r, StringArray& errors) {
    if (!o.isObject()) { errors.add(path + ": expected an object"); return; }
    fm1::VaFilter f = fm1::filterFromRecord(r);
    int on = f.on ? 1 : 0;
    importKey(o, Spec{"on", 0, 0, 1, 0, nullptr, 0, true}, path, on, errors);
    if (on != (f.on ? 1 : 0)) setFilterBits(r, 0, on);
    int type = f.type;
    importKey(o, Spec{"type", 0, 0, 3, 0, fm1::kFilterTypeNames, 4}, path, type, errors);
    if (type != f.type) setFilterBits(r, 1, type);
    if (has(o, "keyTracking")) {
        var v = get(o, "keyTracking");
        int idx = -1;
        for (int i = 0; i < 4; ++i) if (isWhole(v) && int(v) == kKeyTrackPercent[i]) idx = i;
        if (idx < 0) errors.add(path + ".keyTracking: expected 0, 33, 67 or 100");
        else if (idx != f.keyTrack) setFilterBits(r, 2, idx);
    }
    for (int i = 0; i < 7; ++i) {
        int v = filterValue(f, i);
        importKey(o, Spec{kFilterKeys[i], 0, 0, 100, 0}, path, v, errors);
        if (v != filterValue(f, i)) r[size_t(kFilterBytes[i])] = uint8_t(0x80 | v);
    }
}

void importEnvelope(const var& o, const String& path, fm1::Record& r, StringArray& errors) {
    if (!o.isObject()) { errors.add(path + ": expected an object"); return; }
    fm1::Envelope env = fm1::envFromRecord(r);
    int on = env.on ? 1 : 0;
    importKey(o, Spec{"on", 0, 0, 1, 0, nullptr, 0, true}, path, on, errors);
    if (on != (env.on ? 1 : 0)) r[58] = uint8_t(on);
    const int cur[4] = {env.a, env.d, env.s, env.r};
    for (int i = 0; i < 4; ++i) {
        int v = cur[i];
        importKey(o, Spec{kEnvKeys[i], 0, 0, 100, 0}, path, v, errors);
        if (v != cur[i]) r[size_t(54 + i)] = uint8_t(v);
    }
}

// ---- pattern helpers ---------------------------------------------------------------

int noteValueIndex(const var& v) {
    if (!v.isString()) return -1;
    for (int i = 0; i < 10; ++i) if (v.toString() == fm1::seq::kNoteValueNames[i]) return i;
    return -1;
}

void readInt(const var& o, const char* key, const String& path, int lo, int hi, int& out, StringArray& errors) {
    if (!has(o, key)) return;
    var v = get(o, key);
    if (!isWhole(v) || int(v) < lo || int(v) > hi)
        errors.add(path + "." + key + ": expected a whole number from " + String(lo) + " to " + String(hi));
    else
        out = int(v);
}

void readBool(const var& o, const char* key, const String& path, bool& out, StringArray& errors) {
    if (!has(o, key)) return;
    var v = get(o, key);
    if (!v.isBool()) errors.add(path + "." + key + ": expected true or false");
    else out = bool(v);
}

}  // namespace

// ---- presets ---------------------------------------------------------------------

namespace {

// The FM voice's settings: algorithm, LFO, pitch envelope, operators.
void putFmFields(var& o, const fm1::Edit& e) {
    for (const auto& sp : kGlobal) putValue(o, sp, e[size_t(sp.offset)]);

    var lfo = newObject();
    for (const auto& sp : kLfo) putValue(lfo, sp, e[size_t(sp.offset)]);
    put(o, "lfo", lfo);

    var peg = newObject();
    putFour(peg, "rates", e, 126);
    putFour(peg, "levels", e, 130);
    put(o, "pitchEnvelope", peg);

    var ops;
    for (int n = 1; n <= 6; ++n) {
        int base = opBase(n);
        var op = newObject();
        for (const auto& sp : kOp) putValue(op, sp, e[size_t(base + sp.offset)]);
        var eg = newObject();
        putFour(eg, "rates", e, base + 0);
        putFour(eg, "levels", e, base + 4);
        put(op, "envelope", eg);
        var ks = newObject();
        for (const auto& sp : kScaling) putValue(ks, sp, e[size_t(base + sp.offset)]);
        put(op, "keyScaling", ks);
        ops.append(op);
    }
    put(o, "operators", ops);
}

}  // namespace

var presetToJson(const fm1::Sound& s) {
    const fm1::Edit e = fm1::unpackVoice(s.voice);
    const fm1::Record r = s.hasRecord ? s.record : fm1::defaultRecord();
    var o = newObject();
    if (s.slot >= 0) put(o, "slot", s.slot + 1);
    put(o, "name", String(fm1::voiceName(s.voice)).trimEnd());
    const auto engine = fm1::engineOf(r);
    put(o, "engine", fm1::engineLabel(engine));
    if (engine == fm1::Engine::FM) putFmFields(o, e);   // a VA or 8-Bit preset has no FM settings to show; raw keeps its bytes
    // an 8-Bit preset (FM-1_096) keeps its waveforms, arpeggios and Key where the filter's
    // bytes are, and what its envelope's bytes do is not known: only its effects are shown,
    // raw keeps the rest
    const bool chip = engine == fm1::Engine::EightBit;

    const fm1::FxChain fc = fm1::fxFromRecord(r);
    var fx;
    for (int k = 0; k < fm1::kEffects; ++k) {
        int id = fc.order[size_t(k)];
        const auto& st = fc.fx[size_t(id)];
        var item = newObject();
        put(item, "effect", lower(fm1::kEffectNames[id]));
        put(item, "on", st.on);
        if (fm1::kEffectTypeCount[id] > 0) put(item, "type", lower(fm1::kEffectTypeNames[id][st.type]));
        for (int i = 0; i < 3; ++i)
            if (fm1::kEffectParamNames[id][i][0]) put(item, lower(fm1::kEffectParamNames[id][i]).toRawUTF8(), st.p[size_t(i)]);
        fx.append(item);
    }
    put(o, "effects", fx);

    if (chip) {
        var raw = newObject();
        put(raw, "voice", toHex(s.voice.data(), s.voice.size()));
        put(raw, "settings", toHex(s.record.data(), s.record.size()));
        put(o, "raw", raw);
        return o;
    }
    const fm1::VaFilter f = fm1::filterFromRecord(r);
    var flt = newObject();
    put(flt, "on", f.on);
    put(flt, "type", fm1::kFilterTypeNames[f.type & 3]);
    put(flt, "keyTracking", kKeyTrackPercent[f.keyTrack & 3]);
    for (int i = 0; i < 7; ++i) put(flt, kFilterKeys[i], filterValue(f, i));
    put(o, "noteFilter", flt);

    const fm1::Envelope env = fm1::envFromRecord(r);
    var en = newObject();
    put(en, "on", env.on);
    const int ev[4] = {env.a, env.d, env.s, env.r};
    for (int i = 0; i < 4; ++i) put(en, kEnvKeys[i], ev[i]);
    put(o, "envelope", en);

    var raw = newObject();
    put(raw, "voice", toHex(s.voice.data(), s.voice.size()));
    if (s.hasRecord) put(raw, "settings", toHex(s.record.data(), s.record.size()));
    put(o, "raw", raw);
    return o;
}

std::optional<fm1::Sound> presetFromJson(const var& v, const String& path, StringArray& errors) {
    int before = errors.size();
    if (!v.isObject()) { errors.add(path + ": expected an object"); return std::nullopt; }
    fm1::Sound s;
    s.from = "JSON";
    s.hasRecord = true;
    s.voice = fm1::packVoice(fm1::kInitEdit);
    s.record = fm1::defaultRecord();
    if (has(v, "raw")) {
        var raw = get(v, "raw");
        if (!raw.isObject()) {
            errors.add(path + ".raw: expected an object");
        } else {
            if (has(raw, "voice") && !fromHex(get(raw, "voice"), s.voice.data(), s.voice.size()))
                errors.add(path + ".raw.voice: expected " + String(2 * s.voice.size()) + " hex digits");
            if (has(raw, "settings") && !fromHex(get(raw, "settings"), s.record.data(), s.record.size()))
                errors.add(path + ".raw.settings: expected " + String(2 * s.record.size()) + " hex digits");
        }
    }
    if (has(v, "slot")) {
        var sl = get(v, "slot");
        if (!isWhole(sl) || int(sl) < 1 || int(sl) > fm1::kSlots) errors.add(path + ".slot: expected a whole number from 1 to 128");
        else s.slot = int(sl) - 1;
    }

    fm1::Edit e = fm1::unpackVoice(s.voice);
    const fm1::Edit original = e;
    if (fm1::engineOf(s.record) == fm1::Engine::EightBit)   // its kit, bass and lead are in raw only
        for (const char* key : {"algorithm", "feedback", "oscKeySync", "transpose", "lfo", "pitchEnvelope", "operators", "noteFilter", "envelope"})
            if (has(v, key)) errors.add(path + "." + key + ": an 8-Bit preset takes its sound from raw only");
    if (has(v, "name")) {
        var nm = get(v, "name");
        String name = nm.toString();
        bool ok = nm.isString() && name.length() <= kNameLen;
        for (int i = 0; ok && i < name.length(); ++i) ok = name[i] >= 32 && name[i] <= 126;
        if (!ok) {
            errors.add(path + ".name: expected up to 10 plain ASCII characters");
        } else if (name.trimEnd() != String(fm1::voiceName(s.voice)).trimEnd()) {
            String padded = name.paddedRight(' ', kNameLen);
            for (int i = 0; i < kNameLen; ++i) e[size_t(kNameAt + i)] = uint8_t(padded[i]);
        }
    }
    for (const auto& sp : kGlobal) importEditByte(v, sp, 0, path, e, errors);
    if (has(v, "lfo")) {
        var lfo = get(v, "lfo");
        if (!lfo.isObject()) errors.add(path + ".lfo: expected an object");
        else for (const auto& sp : kLfo) importEditByte(lfo, sp, 0, path + ".lfo", e, errors);
    }
    if (has(v, "pitchEnvelope")) {
        var peg = get(v, "pitchEnvelope");
        if (!peg.isObject()) errors.add(path + ".pitchEnvelope: expected an object");
        else { importFour(peg, "rates", 126, path + ".pitchEnvelope", e, errors); importFour(peg, "levels", 130, path + ".pitchEnvelope", e, errors); }
    }
    if (has(v, "operators")) {
        var ops = get(v, "operators");
        if (!ops.isArray() || ops.size() != 6) {
            errors.add(path + ".operators: expected a list of 6 operators, OP1 first");
        } else {
            for (int n = 1; n <= 6; ++n) {
                var op = ops[n - 1];
                String at = path + ".operators[" + String(n - 1) + "]";
                if (!op.isObject()) { errors.add(at + ": expected an object"); continue; }
                int base = opBase(n);
                for (const auto& sp : kOp) importEditByte(op, sp, base, at, e, errors);
                if (has(op, "envelope")) {
                    var eg = get(op, "envelope");
                    if (!eg.isObject()) errors.add(at + ".envelope: expected an object");
                    else { importFour(eg, "rates", base + 0, at + ".envelope", e, errors); importFour(eg, "levels", base + 4, at + ".envelope", e, errors); }
                }
                if (has(op, "keyScaling")) {
                    var ks = get(op, "keyScaling");
                    if (!ks.isObject()) errors.add(at + ".keyScaling: expected an object");
                    else for (const auto& sp : kScaling) importEditByte(ks, sp, base, at + ".keyScaling", e, errors);
                }
            }
        }
    }
    if (e != original) s.voice = fm1::packVoice(e);

    if (has(v, "effects")) importEffects(get(v, "effects"), path + ".effects", s.record, errors);
    if (has(v, "noteFilter")) importFilter(get(v, "noteFilter"), path + ".noteFilter", s.record, errors);
    if (has(v, "envelope")) importEnvelope(get(v, "envelope"), path + ".envelope", s.record, errors);

    if (errors.size() != before) return std::nullopt;
    return s;
}

// ---- patterns --------------------------------------------------------------------

var patternToJson(const PatternEntry& entry) {
    const fm1::seq::Pattern p = fm1::seq::normalise(entry.pattern);
    var o = newObject();
    put(o, "pattern", entry.index + 1);
    put(o, "length", p.length);
    put(o, "noteValue", fm1::seq::kNoteValueNames[p.rate]);
    put(o, "tempo", p.tempo);
    put(o, "gate", p.gate);
    put(o, "swing", p.swing);
    put(o, "transpose", p.transpose);
    put(o, "chain", p.chain < 0 ? var("repeat") : var(p.chain + 1));
    var steps = var(juce::Array<var>());
    for (int i = 0; i < fm1::seq::kSteps; ++i) {
        const auto& st = p.steps[size_t(i)];
        // FM-1_096's parameter locks of this step: (what, value) as the synth's table holds them
        var locks = var(juce::Array<var>());
        if (p.locks.size() == size_t(fm1::seq::kLockBytes))
            for (int j = 0; j < fm1::seq::kLocksPerStep; ++j) {
                const size_t at = size_t(8 * i + 2 * j);
                if (p.locks[at] == 0xFF) continue;
                var lo = newObject();
                put(lo, "what", int(p.locks[at]));
                put(lo, "value", int(p.locks[at + 1]));
                locks.append(lo);
            }
        bool plain = st.notes.empty() && st.rate == p.rate && st.ratchet == 1 && st.gate == 0 && st.chance == 100
                     && st.transpose == 0 && !st.accent && !st.slide && locks.size() == 0;
        if (plain) continue;
        var so = newObject();
        put(so, "step", i + 1);
        var notes = var(juce::Array<var>());
        for (const auto& n : st.notes) {
            var no = newObject();
            put(no, "note", n.note);
            put(no, "velocity", n.vel);
            if (n.tie) put(no, "tie", true);
            notes.append(no);
        }
        put(so, "notes", notes);
        if (st.rate != p.rate) put(so, "noteValue", fm1::seq::kNoteValueNames[st.rate]);
        if (st.ratchet != 1) put(so, "ratchet", st.ratchet);
        if (st.gate != 0) put(so, "gate", st.gate);
        if (st.chance != 100) put(so, "chance", st.chance);
        if (st.transpose != 0) put(so, "transpose", st.transpose);
        if (st.accent) put(so, "accent", true);
        if (st.slide) put(so, "tieSlide", true);
        if (locks.size() > 0) put(so, "locks", locks);
        steps.append(so);
    }
    put(o, "steps", steps);
    return o;
}

std::optional<PatternEntry> patternFromJson(const var& v, const String& path, StringArray& errors) {
    int before = errors.size();
    if (!v.isObject()) { errors.add(path + ": expected an object"); return std::nullopt; }
    PatternEntry out;
    out.index = -1;
    if (!has(v, "pattern")) errors.add(path + ".pattern: missing (1 to 16)");
    else { int n = 0; readInt(v, "pattern", path, 1, fm1::seq::kPatterns, n, errors); out.index = n - 1; }
    fm1::seq::Pattern& p = out.pattern;
    readInt(v, "length", path, 1, fm1::seq::kSteps, p.length, errors);
    if (has(v, "noteValue")) {
        int r = noteValueIndex(get(v, "noteValue"));
        if (r < 0) errors.add(path + ".noteValue: expected one of \"1/1\", \"1/2\", \"1/4\", \"1/4T\", \"1/8\", \"1/8T\", \"1/16\", \"1/16T\", \"1/32\", \"1/32T\"");
        else p.rate = r;
    }
    readInt(v, "tempo", path, 30, 300, p.tempo, errors);
    readInt(v, "gate", path, 5, 100, p.gate, errors);
    readInt(v, "swing", path, 50, 75, p.swing, errors);
    readInt(v, "transpose", path, -24, 24, p.transpose, errors);
    if (has(v, "chain")) {
        var c = get(v, "chain");
        if (c.isString() && c.toString().equalsIgnoreCase("repeat")) p.chain = -1;
        else if (isWhole(c) && int(c) >= 1 && int(c) <= fm1::seq::kPatterns) p.chain = int(c) - 1;
        else errors.add(path + ".chain: expected \"repeat\" or a pattern number from 1 to 16");
    }
    for (auto& st : p.steps) st.rate = p.rate;
    if (has(v, "steps")) {
        var steps = get(v, "steps");
        if (!steps.isArray()) {
            errors.add(path + ".steps: expected a list");
        } else {
            std::set<int> seen;
            for (int k = 0; k < steps.size(); ++k) {
                var so = steps[k];
                String at = path + ".steps[" + String(k) + "]";
                if (!so.isObject()) { errors.add(at + ": expected an object"); continue; }
                int n = 0;
                if (!has(so, "step")) { errors.add(at + ".step: missing (1 to 64)"); continue; }
                readInt(so, "step", at, 1, fm1::seq::kSteps, n, errors);
                if (n == 0) continue;
                if (!seen.insert(n).second) { errors.add(at + ".step: step " + String(n) + " is listed twice"); continue; }
                fm1::seq::Step st;
                st.rate = p.rate;
                if (has(so, "notes")) {
                    var notes = get(so, "notes");
                    if (!notes.isArray() || notes.size() > fm1::seq::kMaxNotes) {
                        errors.add(at + ".notes: expected a list of up to 9 notes");
                    } else {
                        std::set<int> pitches;
                        for (int j = 0; j < notes.size(); ++j) {
                            var no = notes[j];
                            String nat = at + ".notes[" + String(j) + "]";
                            if (!no.isObject() || !has(no, "note")) { errors.add(nat + ": expected an object with a note"); continue; }
                            fm1::seq::Note note;
                            readInt(no, "note", nat, 0, 127, note.note, errors);
                            readInt(no, "velocity", nat, 1, 127, note.vel, errors);
                            readBool(no, "tie", nat, note.tie, errors);
                            if (!pitches.insert(note.note).second) errors.add(nat + ".note: note " + String(note.note) + " is listed twice");
                            st.notes.push_back(note);
                        }
                    }
                }
                if (has(so, "noteValue")) {
                    int r = noteValueIndex(get(so, "noteValue"));
                    if (r < 0) errors.add(at + ".noteValue: expected a note value such as \"1/16\"");
                    else st.rate = r;
                }
                readInt(so, "ratchet", at, 1, 4, st.ratchet, errors);
                if (has(so, "gate")) {
                    var g = get(so, "gate");
                    if (!isWhole(g) || !(int(g) == 0 || (int(g) >= 5 && int(g) <= 100)))
                        errors.add(at + ".gate: expected 0 (the pattern's gate) or a whole number from 5 to 100");
                    else st.gate = int(g);
                }
                readInt(so, "chance", at, 5, 100, st.chance, errors);
                readInt(so, "transpose", at, -24, 24, st.transpose, errors);
                readBool(so, "accent", at, st.accent, errors);
                readBool(so, "tieSlide", at, st.slide, errors);
                if (has(so, "locks")) {
                    var locks = get(so, "locks");
                    if (!locks.isArray() || locks.size() > fm1::seq::kLocksPerStep) {
                        errors.add(at + ".locks: expected a list of up to 4 locks");
                    } else {
                        if (p.locks.size() != size_t(fm1::seq::kLockBytes)) p.locks.assign(size_t(fm1::seq::kLockBytes), 0xFF);
                        for (int j = 0; j < locks.size(); ++j) {
                            var lo = locks[j];
                            String lat = at + ".locks[" + String(j) + "]";
                            if (!lo.isObject() || !has(lo, "what") || !has(lo, "value")) { errors.add(lat + ": expected an object with what and value"); continue; }
                            int what = 0, value = 0;
                            readInt(lo, "what", lat, 0, 58, what, errors);
                            readInt(lo, "value", lat, 0, 127, value, errors);
                            // the codes her pattern editor names: 8 knobs of FM, VA and 8-Bit, then 4 of each effect
                            if (what >= 24 && what < 32) errors.add(lat + ".what: " + String(what) + " is not a setting the FM-1 locks");
                            for (int k = 0; k < j; ++k)
                                if (p.locks[size_t(8 * (n - 1) + 2 * k)] == what) errors.add(lat + ".what: " + String(what) + " is locked twice on this step");
                            p.locks[size_t(8 * (n - 1) + 2 * j)] = uint8_t(what);
                            p.locks[size_t(8 * (n - 1) + 2 * j + 1)] = uint8_t(value);
                        }
                    }
                }
                p.steps[size_t(n - 1)] = st;
            }
        }
    }
    if (errors.size() != before) return std::nullopt;
    return out;
}

// ---- documents -------------------------------------------------------------------

namespace {
// Like JSON::toString, but a list of plain values (envelope rates, say) stays on one line.
void format(const var& v, int indent, String& out) {
    String pad = String::repeatedString(" ", indent), inner = String::repeatedString(" ", indent + 2);
    if (auto* obj = v.getDynamicObject()) {
        const auto& props = obj->getProperties();
        if (props.isEmpty()) { out << "{}"; return; }
        out << "{\n";
        for (int i = 0; i < props.size(); ++i) {
            out << inner << juce::JSON::toString(var(props.getName(i).toString())) << ": ";
            format(props.getValueAt(i), indent + 2, out);
            out << (i + 1 < props.size() ? ",\n" : "\n");
        }
        out << pad << "}";
    } else if (auto* arr = v.getArray()) {
        if (arr->isEmpty()) { out << "[]"; return; }
        bool plain = std::none_of(arr->begin(), arr->end(), [](const var& x) { return x.isObject() || x.isArray(); });
        if (plain) {
            out << "[";
            for (int i = 0; i < arr->size(); ++i) out << (i ? ", " : "") << juce::JSON::toString((*arr)[i]);
            out << "]";
            return;
        }
        out << "[\n";
        for (int i = 0; i < arr->size(); ++i) {
            out << inner;
            format((*arr)[i], indent + 2, out);
            out << (i + 1 < arr->size() ? ",\n" : "\n");
        }
        out << pad << "]";
    } else {
        out << juce::JSON::toString(v);
    }
}
}  // namespace

String write(const Document& d) {
    var root = newObject();
    put(root, "$schema", kSchemaUrl);
    put(root, "format", kFormat);
    put(root, "version", kVersion);
    if (!d.presets.empty()) {
        var list = var(juce::Array<var>());
        for (const auto& s : d.presets) list.append(presetToJson(s));
        put(root, "presets", list);
    }
    if (!d.patterns.empty()) {
        var list = var(juce::Array<var>());
        for (const auto& p : d.patterns) list.append(patternToJson(p));
        put(root, "patterns", list);
    }
    String out;
    format(root, 0, out);
    return out + "\n";
}

Document read(const String& text, StringArray& errors) {
    Document d;
    var root;
    auto result = juce::JSON::parse(text, root);
    if (result.failed()) { errors.add("not valid JSON: " + result.getErrorMessage()); return d; }
    if (!root.isObject()) { errors.add("expected a JSON object at the top"); return d; }
    if (!has(root, "format") || get(root, "format").toString() != kFormat) {
        errors.add("format: expected \"virtual-fm1\"; this is not a Virtual FM-1 file");
        return d;
    }
    var ver = get(root, "version");
    if (!isWhole(ver) || int(ver) < 1) { errors.add("version: expected a whole number"); return d; }
    if (int(ver) > kVersion) { errors.add("version: " + ver.toString() + " was written by a newer Virtual FM-1; update the plugin to read it"); return d; }
    if (has(root, "presets")) {
        var list = get(root, "presets");
        if (!list.isArray()) {
            errors.add("presets: expected a list");
        } else {
            std::set<int> slots;
            for (int i = 0; i < list.size(); ++i) {
                String at = "presets[" + String(i) + "]";
                if (auto s = presetFromJson(list[i], at, errors)) {
                    if (s->slot >= 0 && !slots.insert(s->slot).second) errors.add(at + ".slot: slot " + String(s->slot + 1) + " is used twice");
                    d.presets.push_back(*s);
                }
            }
        }
    }
    if (has(root, "patterns")) {
        var list = get(root, "patterns");
        if (!list.isArray()) {
            errors.add("patterns: expected a list");
        } else {
            std::set<int> seen;
            for (int i = 0; i < list.size(); ++i) {
                String at = "patterns[" + String(i) + "]";
                if (auto p = patternFromJson(list[i], at, errors)) {
                    if (!seen.insert(p->index).second) errors.add(at + ".pattern: pattern " + String(p->index + 1) + " is listed twice");
                    d.patterns.push_back(*p);
                }
            }
        }
    }
    return d;
}

}  // namespace fm1json
