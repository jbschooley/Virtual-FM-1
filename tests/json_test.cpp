// Fm1Json: presets and patterns written as JSON must read back to the same
// bytes, hand edits must change only what was edited, and bad values must be
// reported with their place in the file.
//
//   json_test <golden.json> [<file.syx>]   (an FM-1+VA backup adds its presets)

#include <cstdio>
#include <random>

#include <juce_core/juce_core.h>

#include "Fm1Codec.h"
#include "Fm1Json.h"
#include "Fm1Record.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, msg) do { if (cond) ++g_pass; else { ++g_fail; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, juce::String(msg).toRawUTF8()); } } while (0)

using juce::var;

static bool sameBytes(const fm1::Sound& a, const fm1::Sound& b) { return a.voice == b.voice && a.record == b.record; }

static fm1::Sound roundTrip(const fm1::Sound& s, juce::StringArray& errors) {
    // through text, as a file would go
    var parsed = juce::JSON::parse(juce::JSON::toString(fm1json::presetToJson(s)));
    auto back = fm1json::presetFromJson(parsed, "p", errors);
    return back ? *back : fm1::Sound{};
}

static void setPath(var& o, const juce::StringArray& keys, const var& value) {
    var cur = o;
    for (int i = 0; i < keys.size() - 1; ++i) {
        var next = keys[i].containsOnly("0123456789") ? cur[keys[i].getIntValue()] : cur.getProperty(keys[i], var());
        cur = next;
    }
    if (keys[keys.size() - 1].containsOnly("0123456789")) cur.getArray()->set(keys[keys.size() - 1].getIntValue(), value);
    else cur.getDynamicObject()->setProperty(keys[keys.size() - 1], value);
}

static fm1::Sound fromHexStrings(const juce::String& voice, const juce::String& record, int slot) {
    fm1::Sound s;
    juce::MemoryBlock v, r;
    v.loadFromHexString(voice);
    r.loadFromHexString(record);
    std::copy_n(static_cast<const uint8_t*>(v.getData()), s.voice.size(), s.voice.begin());
    std::copy_n(static_cast<const uint8_t*>(r.getData()), s.record.size(), s.record.begin());
    s.hasRecord = true;
    s.slot = slot;
    return s;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: json_test golden.json [backup.syx]\n"); return 2; }
    std::vector<fm1::Sound> sounds;

    // the Virtual Analog pack from tests/golden.json
    var golden = juce::JSON::parse(juce::File(argv[1]));
    for (const auto& p : *golden["presets"].getArray())
        sounds.push_back(fromHexStrings(p["voice"].toString(), p["record"].toString(), int(p["slot"])));
    const size_t vaCount = sounds.size();

    // FM sounds: INIT VOICE, then random settings within each byte's range, with odd records
    std::mt19937 rng(7);
    const int opMax[21] = {99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 3, 3, 7, 3, 7, 99, 1, 31, 99, 14};
    const int globalMax[19] = {99, 99, 99, 99, 99, 99, 99, 99, 31, 7, 1, 99, 99, 99, 99, 1, 5, 7, 48};
    for (int n = 0; n < 40; ++n) {
        fm1::Edit e = fm1::kInitEdit;
        if (n > 0) {
            for (int op = 0; op < 6; ++op)
                for (int i = 0; i < 21; ++i) e[size_t(op * 21 + i)] = uint8_t(rng() % unsigned(opMax[i] + 1));
            for (int i = 0; i < 19; ++i) e[size_t(126 + i)] = uint8_t(rng() % unsigned(globalMax[i] + 1));
        }
        fm1::Sound s;
        s.voice = fm1::packVoice(e);
        s.record = fm1::defaultRecord();
        s.hasRecord = true;
        s.slot = n;
        if (n % 3 == 1) for (auto& b : s.record) b = 0x03;                 // stored presets' "unset" bytes
        if (n % 3 == 2) for (auto& b : s.record) b = uint8_t(rng() % 256);  // anything at all
        sounds.push_back(s);
    }

    // an FM-1+VA backup, when given
    if (argc > 2) {
        juce::MemoryBlock mb;
        if (juce::File(argv[2]).loadFileAsData(mb)) {
            fm1::Bytes bytes(static_cast<const uint8_t*>(mb.getData()), static_cast<const uint8_t*>(mb.getData()) + mb.getSize());
            auto c = fm1::readSyx(bytes);
            for (auto& s : c.sounds) sounds.push_back(s);
            std::printf("backup: %zu presets from %s\n", c.sounds.size(), argv[2]);
        }
    }

    // 1. every sound round-trips byte for byte
    int exact = 0;
    for (const auto& s : sounds) {
        juce::StringArray errors;
        auto back = roundTrip(s, errors);
        CHECK(errors.isEmpty(), "round trip errors: " + errors.joinIntoString("; "));
        if (sameBytes(s, back) && back.slot == s.slot) ++exact;
        else std::printf("  not exact: slot %d %s\n", s.slot + 1, fm1::voiceName(s.voice).c_str());
    }
    CHECK(exact == int(sounds.size()), "every preset round-trips exactly");
    std::printf("round trip: %d of %zu exact\n", exact, sounds.size());

    // 1a. a Virtual Analog preset shows no FM settings; raw carries the sound
    {
        var j = fm1json::presetToJson(sounds[0]);
        CHECK(j["engine"].toString() == "VA", "golden preset is VA");
        auto* o = j.getDynamicObject();
        CHECK(!o->hasProperty("operators") && !o->hasProperty("algorithm") && !o->hasProperty("lfo") && !o->hasProperty("pitchEnvelope"),
              "no FM settings for a VA preset");
        CHECK(o->hasProperty("effects") && o->hasProperty("envelope") && o->hasProperty("noteFilter") && o->hasProperty("raw"),
              "shared settings and raw still there");
    }

    // 1b. a stored value outside its range (some factory presets): left out of the
    // readable fields, kept by raw
    {
        fm1::Edit e = fm1::kInitEdit;
        e[size_t(5 * 21 + 20)] = 15;   // OP1 detune one past +7
        e[size_t(5 * 21 + 2)] = 127;   // OP1 EG rate 3
        fm1::Sound s;
        s.voice = fm1::packVoice(e);
        s.record = fm1::defaultRecord();
        s.hasRecord = true;
        var j = fm1json::presetToJson(s);
        var op1 = j["operators"][0];
        CHECK(!op1.getDynamicObject()->hasProperty("detune"), "out-of-range detune left out");
        CHECK(!op1["envelope"].getDynamicObject()->hasProperty("rates"), "rates with an out-of-range value left out");
        CHECK(op1["envelope"].getDynamicObject()->hasProperty("levels"), "levels still there");
        juce::StringArray errors;
        auto back = roundTrip(s, errors);
        CHECK(errors.isEmpty() && sameBytes(s, back), "out-of-range bytes survive through raw");
    }

    // 2. without the raw bytes, the readable settings alone come back the same
    for (size_t i = vaCount; i < vaCount + 3; ++i) {
        var j = fm1json::presetToJson(sounds[i]);
        j.getDynamicObject()->removeProperty("raw");
        juce::StringArray errors;
        auto s = fm1json::presetFromJson(juce::JSON::parse(juce::JSON::toString(j)), "p", errors);
        CHECK(s && errors.isEmpty(), "readable-only import: " + errors.joinIntoString("; "));
        if (!s) continue;
        var again = fm1json::presetToJson(*s);
        again.getDynamicObject()->removeProperty("raw");
        CHECK(juce::JSON::toString(again) == juce::JSON::toString(j), "readable settings survive without raw bytes");
    }

    // 3. one edit changes exactly one byte
    {
        const fm1::Sound& s = sounds[vaCount + 1];
        var j = fm1json::presetToJson(s);
        int level = int(j["operators"][2]["level"]);
        setPath(j, {"operators", "2", "level"}, (level + 10) % 100);
        juce::StringArray errors;
        auto back = fm1json::presetFromJson(j, "p", errors);
        CHECK(back.has_value(), "edited preset reads");
        if (back) {
            fm1::Edit a = fm1::unpackVoice(s.voice), b = fm1::unpackVoice(back->voice);
            int diffs = 0, at = -1;
            for (size_t i = 0; i < a.size(); ++i) if (a[i] != b[i]) { ++diffs; at = int(i); }
            CHECK(diffs == 1 && at == 3 * 21 + 16, "only OP3's output level changed");   // OP3 is the fourth block (OP6 first)
            CHECK(back->record == s.record, "record untouched by a voice edit");
        }
        // the name, padded to 10 characters
        setPath(j, {"name"}, "NEW NAME");
        back = fm1json::presetFromJson(j, "p", errors);
        CHECK(back && fm1::voiceName(back->voice) == "NEW NAME  ", "name written and padded");
    }

    // 4. effects: reorder, change one value
    {
        fm1::Sound s = sounds[vaCount];   // INIT VOICE, default record
        var j = fm1json::presetToJson(s);
        auto* list = j["effects"].getArray();
        std::reverse(list->begin(), list->end());
        setPath(j, {"effects", "0", "mix"}, 42);         // phaser, now first
        setPath(j, {"effects", "0", "on"}, true);
        juce::StringArray errors;
        auto back = fm1json::presetFromJson(j, "p", errors);
        CHECK(back && errors.isEmpty(), "reordered effects read: " + errors.joinIntoString("; "));
        if (back) {
            fm1::FxChain fc = fm1::fxFromRecord(back->record), orig = fm1::fxFromRecord(s.record);
            CHECK(fc.order[0] == fm1::FxPhaser && fc.order[5] == fm1::FxFilter, "chain order reversed");
            CHECK(fc.fx[fm1::FxPhaser].p[2] == 42 && fc.fx[fm1::FxPhaser].on, "phaser mix and on written");
            CHECK(fc.fx[fm1::FxReverb].p == orig.fx[fm1::FxReverb].p, "other effects keep their values");
        }
    }

    // 5. note filter and envelope
    {
        fm1::Sound s = sounds[vaCount];
        var j = fm1json::presetToJson(s);
        setPath(j, {"noteFilter", "on"}, true);
        setPath(j, {"noteFilter", "type"}, "lp24");
        setPath(j, {"noteFilter", "keyTracking"}, 67);
        setPath(j, {"noteFilter", "cutoff"}, 40);
        setPath(j, {"envelope", "attack"}, 12);
        juce::StringArray errors;
        auto back = fm1json::presetFromJson(j, "p", errors);
        CHECK(back && errors.isEmpty(), "filter and envelope read: " + errors.joinIntoString("; "));
        if (back) {
            auto f = fm1::filterFromRecord(back->record);
            CHECK(f.on && f.type == 1 && f.keyTrack == 2 && f.cutoff == 40, "note filter written");
            CHECK(f.resonance == fm1::filterFromRecord(s.record).resonance, "unedited filter field stays unset");
            CHECK((back->record[24] & 0x80) == (s.record[24] & 0x80), "resonance byte untouched");
            CHECK(fm1::envFromRecord(back->record).a == 12, "envelope attack written");
        }
    }

    // 6. problems are reported with their place
    {
        var j = fm1json::presetToJson(sounds[vaCount]);
        setPath(j, {"operators", "2", "level"}, 120);
        setPath(j, {"lfo", "wave"}, "wobble");
        setPath(j, {"transpose", }, 3.5);
        juce::StringArray errors;
        auto back = fm1json::presetFromJson(j, "presets[0]", errors);
        CHECK(!back, "bad preset rejected");
        CHECK(errors.joinIntoString("\n").contains("presets[0].operators[2].level: 120 is outside 0 to 99"), "range error names its place");
        CHECK(errors.joinIntoString("\n").contains("presets[0].lfo.wave: \"wobble\" is not one of"), "choice error names its place");
        CHECK(errors.joinIntoString("\n").contains("presets[0].transpose: expected a whole number from -24 to 24"), "fractional value rejected");
        errors.clear();
        fm1json::read("{\"format\": \"something-else\", \"version\": 1}", errors);
        CHECK(errors.size() == 1 && errors[0].startsWith("format:"), "foreign files refused");
        errors.clear();
        fm1json::read("{\"format\": \"virtual-fm1\", \"version\": 99}", errors);
        CHECK(errors.size() == 1 && errors[0].contains("newer"), "newer versions refused");
    }

    // 7. patterns, with every plugin-side setting
    {
        fm1json::PatternEntry e;
        e.index = 4;
        auto& p = e.pattern;
        p.length = 24; p.rate = 7; p.tempo = 97; p.gate = 63; p.swing = 58; p.transpose = -5; p.chain = 9;
        for (auto& st : p.steps) st.rate = p.rate;
        p.steps[0].notes = {{60, 90, 0}, {64, 114, 3}, {67, 1, 0}};
        p.steps[0].ratchet = 3; p.steps[0].chance = 40; p.steps[0].accent = true;
        p.steps[5].rate = 2; p.steps[5].gate = 80; p.steps[5].transpose = 12;
        p.repeats = 6;
        p.steps[23].notes = {{127, 127, false}};
        fm1json::Document d;
        d.patterns.push_back(e);
        d.presets.push_back(sounds[0]);
        juce::String text = fm1json::write(d);
        juce::StringArray errors;
        auto back = fm1json::read(text, errors);
        CHECK(errors.isEmpty(), "document reads: " + errors.joinIntoString("; "));
        CHECK(back.patterns.size() == 1 && back.presets.size() == 1, "document holds both");
        if (back.patterns.size() == 1) {
            const auto& q = back.patterns[0];
            CHECK(q.index == 4, "pattern number");
            CHECK(fm1json::write({{}, {q}}) == fm1json::write({{}, {e}}), "pattern round-trips");
            CHECK(q.pattern.steps[0].notes.size() == 3 && q.pattern.steps[0].notes[1].vel == 114 && q.pattern.steps[0].notes[1].len == 3, "notes, velocity and length");
            CHECK(q.pattern.chain == 9 && q.pattern.repeats == 6 && q.pattern.transpose == -5, "chain, repeats and transpose");
            CHECK(q.pattern.steps[5].rate == 2 && q.pattern.steps[5].gate == 80, "step extras");
        }
        {   // an older file: "tie" repeated the note on the next step; "tieSlide" tied every note of a step
            const juce::String old = R"({"format": "virtual-fm1", "version": 1, "patterns": [{"pattern": 1, "length": 8, "noteValue": "1/16", "tempo": 120, "gate": 50, "swing": 50,
                "chain": "repeat", "steps": [{"step": 1, "notes": [{"note": 60, "velocity": 100, "tie": true}]},
                {"step": 2, "notes": [{"note": 60, "velocity": 100, "tie": true}]}, {"step": 3, "notes": [{"note": 60, "velocity": 100}]},
                {"step": 5, "notes": [{"note": 64, "velocity": 90}], "tieSlide": true}, {"step": 6, "notes": [{"note": 67, "velocity": 90}]}]}]})";
            juce::StringArray errs;
            auto od = fm1json::read(old, errs);
            CHECK(errs.isEmpty() && od.patterns.size() == 1, "an older pattern file reads: " + errs.joinIntoString("; "));
            if (od.patterns.size() == 1) {
                const auto& op = od.patterns[0].pattern;
                CHECK(op.steps[0].notes.size() == 1 && op.steps[0].notes[0].len == 2 && op.steps[1].notes.empty() && op.steps[2].notes.empty(),
                      "its tied note becomes one note held two steps past its own");
                CHECK(op.steps[4].notes.size() == 1 && op.steps[4].notes[0].len == 1, "Tie & Slide into another note: held into the next step");
            }
        }
        if (back.presets.size() == 1) CHECK(sameBytes(back.presets[0], sounds[0]), "preset inside a document round-trips");
        // a bad step
        juce::String bad = text.replace("\"ratchet\": 3", "\"ratchet\": 7");
        errors.clear();
        fm1json::read(bad, errors);
        CHECK(errors.joinIntoString("\n").contains("patterns[0].steps[0].ratchet"), "pattern error names its place");
    }

    std::printf("json_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
