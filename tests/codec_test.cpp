// Checks sync/Fm1Codec against tests/golden.json, which was generated from
// baud girl's own FM-1+VA JavaScript (tests/gen_golden.mjs).
// Minimal JSON reading: the file is flat enough to scan with a tiny parser.

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "Fm1Codec.h"
#include "Fm1Seq.h"

using namespace fm1;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, msg) do { if (cond) ++g_pass; else { ++g_fail; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } } while (0)

static Bytes fromHex(const std::string& h) {
    Bytes b;
    for (size_t i = 0; i + 1 < h.size(); i += 2) b.push_back(uint8_t(std::strtoul(h.substr(i, 2).c_str(), nullptr, 16)));
    return b;
}
static std::string toHex(const Bytes& b) {
    std::string s;
    char t[3];
    for (uint8_t x : b) { std::snprintf(t, 3, "%02x", x); s += t; }
    return s;
}
template <size_t K> static std::string toHex(const std::array<uint8_t, K>& a) { return toHex(Bytes(a.begin(), a.end())); }

// --- a very small JSON reader (objects, arrays, strings, numbers, bools, null)
struct J {
    enum T { Null, Bool, Num, Str, Arr, Obj } t = Null;
    bool b = false; double n = 0; std::string s;
    std::vector<J> a; std::map<std::string, J> o;
    const J& operator[](const std::string& k) const { static J none; auto it = o.find(k); return it == o.end() ? none : it->second; }
    const J& operator[](size_t i) const { return a[i]; }
};
struct P {
    const std::string& s; size_t i = 0;
    void ws() { while (i < s.size() && std::isspace((unsigned char)s[i])) ++i; }
    J val() {
        ws(); J j;
        if (s[i] == '{') { j.t = J::Obj; ++i; ws(); if (s[i] == '}') { ++i; return j; }
            for (;;) { ws(); std::string k = str(); ws(); ++i; j.o[k] = val(); ws(); if (s[i] == ',') { ++i; continue; } ++i; return j; } }
        if (s[i] == '[') { j.t = J::Arr; ++i; ws(); if (s[i] == ']') { ++i; return j; }
            for (;;) { j.a.push_back(val()); ws(); if (s[i] == ',') { ++i; continue; } ++i; return j; } }
        if (s[i] == '"') { j.t = J::Str; j.s = str(); return j; }
        if (s.compare(i, 4, "true") == 0) { j.t = J::Bool; j.b = true; i += 4; return j; }
        if (s.compare(i, 5, "false") == 0) { j.t = J::Bool; i += 5; return j; }
        if (s.compare(i, 4, "null") == 0) { i += 4; return j; }
        j.t = J::Num; size_t e = i; while (e < s.size() && (std::isdigit((unsigned char)s[e]) || s[e] == '-' || s[e] == '.' || s[e] == 'e' || s[e] == '+')) ++e;
        j.n = std::atof(s.substr(i, e - i).c_str()); i = e; return j;
    }
    std::string str() { std::string r; ++i; while (s[i] != '"') { if (s[i] == '\\') { ++i; } r += s[i++]; } ++i; return r; }
};

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: codec_test golden.json\n"); return 2; }
    std::ifstream f(argv[1]);
    std::stringstream ss; ss << f.rdbuf();
    std::string text = ss.str();
    P p{text};
    J g = p.val();

    // presets: unpack/pack/encodeExact/decodeExact/toDx7Voice
    for (const J& pr : g["presets"].a) {
        Bytes voice = fromHex(pr["voice"].s);
        Voice v{}; std::copy(voice.begin(), voice.end(), v.begin());
        Edit e = unpackVoice(v);
        CHECK(toHex(e) == pr["edit"].s, "unpackVoice");
        CHECK(toHex(packVoice(e)) == pr["repack"].s, "packVoice");
        CHECK(voiceName(v) == pr["name"].s, "voiceName");
        Bytes rec = fromHex(pr["record"].s);
        Record r{}; std::copy(rec.begin(), rec.end(), r.begin());
        CHECK((engineOf(r) == Engine::VA ? "VA" : "FM") == pr["engine"].s, "engineOf");
        Bytes exact = encodeExact(int(pr["slot"].n), v, r);
        CHECK(toHex(exact) == pr["exact"].s, "encodeExact");
        auto d = decodeExact(exact);
        CHECK(d && d->slot == int(pr["slot"].n) && toHex(d->voice) == pr["voice"].s && toHex(d->record) == pr["record"].s, "decodeExact");
        CHECK(toHex(toDx7Voice(v)) == pr["dx7voice"].s, "toDx7Voice");
        Bytes bad = exact; bad[100] ^= 1;
        bool threw = false; try { decodeExact(bad); } catch (const CodecError&) { threw = true; }
        CHECK(threw, "decodeExact rejects a bad checksum");
    }
    // whole-file roundtrip through readSyx/toSyx
    {
        std::vector<Voice> voices;
        Bytes file;
        for (const J& pr : g["presets"].a) { Bytes m = fromHex(pr["exact"].s); file.insert(file.end(), m.begin(), m.end()); }
        SyxContents c = readSyx(file);
        CHECK(c.skipped.empty() && c.sounds.size() == g["presets"].a.size(), "readSyx reads every v4 message");
        CHECK(toHex(toSyx(c.sounds)) == toHex(file), "toSyx reproduces the file");
        for (const Sound& s : c.sounds) voices.push_back(s.voice);
        CHECK(toHex(toDx7Bank(voices)) == g["dx7"]["bank16"].s, "toDx7Bank pads with INIT VOICE");
        CHECK(toHex(kInitEdit) == g["dx7"]["initEdit"].s, "INIT VOICE bytes");
        // the DX7 bank reads back as 32 voices, the 16 real ones first
        SyxContents b = readSyx(toDx7Bank(voices));
        CHECK(b.skipped.empty() && b.sounds.size() == 32 && b.sounds[0].from == "DX7 bank" && !b.sounds[0].hasRecord, "readSyx DX7 bank");
        CHECK(toHex(b.sounds[0].voice) == toHex(voices[0]), "DX7 bank voice 0 survives normalisation");
        SyxContents sv = readSyx(toDx7Voice(voices[1]));
        CHECK(sv.skipped.empty() && sv.sounds.size() == 1 && toHex(sv.sounds[0].voice) == toHex(voices[1]), "readSyx DX7 single voice");
    }
    CHECK(toHex(defaultRecord()) == g["defaultRecord"].s, "defaultRecord");
    for (const auto& [slot, j] : g["soundRead"].o) CHECK(toHex(encodeSoundRead(std::atoi(slot.c_str()))) == j.s, "encodeSoundRead");
    for (const auto& [key, j] : g["memRead"].o) {
        auto colon = key.find(':');
        uint32_t addr = uint32_t(std::strtoull(key.substr(0, colon).c_str(), nullptr, 10));
        int n = std::atoi(key.substr(colon + 1).c_str());
        CHECK(toHex(encodeMemRead(addr, n)) == j.s, "encodeMemRead");
    }
    for (const J& rj : g["replies"].a) {
        Bytes frame = fromHex(rj["frame"].s);
        auto r = decodeReply(frame);
        CHECK(r.has_value(), "decodeReply recognises the frame");
        if (!r) continue;
        std::string kind = r->kind == Reply::Kind::Sound ? "sound" : r->kind == Reply::Kind::Mem ? "mem" : "other";
        CHECK(kind == rj["kind"].s && r->status == uint8_t(rj["status"].n) && r->arg == uint32_t(rj["arg"].n) && toHex(r->data) == rj["data"].s, "decodeReply fields");
        if (kind == "sound" && r->status == 0) {
            Sound s = soundFromReply(*r);
            CHECK(s.slot == int(rj["arg"].n) && s.hasRecord, "soundFromReply");
        } else if (kind == "sound") {
            bool threw = false; try { soundFromReply(*r); } catch (const CodecError&) { threw = true; }
            CHECK(threw, "soundFromReply throws on refusal");
        }
        Bytes damaged = frame; damaged[5] ^= 0x01;
        bool threw = false; try { decodeReply(damaged); } catch (const CodecError&) { threw = true; }
        CHECK(threw || !decodeReply(damaged), "decodeReply rejects a damaged frame");
    }
    CHECK(toHex(kIdentityQuery) == g["identityQuery"].s, "identity query bytes");
    for (const J& ij : g["identity"].a) {
        Bytes frame = fromHex(ij["frame"].s);
        auto id = parseIdentity(frame.data(), frame.size());
        const J& want = ij["parsed"];
        if (want.t == J::Null) { CHECK(!id, "parseIdentity rejects"); continue; }
        CHECK(id && id->model == want["model"].s && id->version == int(want["version"].n), "parseIdentity");
    }
    CHECK(!decodeReply(kIdentityQuery), "identity query is not a 7D reply");
    CHECK(!parseIdentity(encodeSoundRead(0).data(), encodeSoundRead(0).size()), "sound read is not an identity");

    // ---- sequencer patterns ------------------------------------------------------
    {
        const J& sq = g["seq"];
        auto toPattern = [](const J& pj) {
            fm1::seq::Pattern p;
            p.length = int(pj["length"].n); p.rate = int(pj["rate"].n); p.tempo = int(pj["tempo"].n);
            p.gate = int(pj["gate"].n); p.swing = int(pj["swing"].n); p.sound = int(pj["sound"].n);
            for (size_t i = 0; i < pj["steps"].a.size() && i < 64; ++i) {
                const J& st = pj["steps"][i];
                p.steps[i].rate = int(st["rate"].n);
                for (const J& n : st["notes"].a) p.steps[i].notes.push_back({int(n["note"].n), int(n["vel"].n)});
            }
            return p;
        };
        fm1::seq::Pattern p = toPattern(sq["pattern"]);   // already normalised by her code
        auto writes = fm1::seq::encodeWrite(p, 3, true);
        CHECK(writes.size() == sq["writes"].a.size(), "encodeWrite message count");
        for (size_t i = 0; i < writes.size() && i < sq["writes"].a.size(); ++i)
            CHECK(toHex(writes[i]) == sq["writes"][i].s, "encodeWrite message bytes");
        CHECK(toHex(fm1::seq::encodeWritePart(p, 3, 0, false)) == sq["writePart0NoSave"].s, "encodeWritePart without save");
        // normalise: feed the raw (out-of-range) values and expect the same bytes
        fm1::seq::Pattern raw = p;
        raw.steps[1].notes = {{200, 300}};
        raw.steps[3].notes.clear();
        for (int i = 0; i < 11; ++i) raw.steps[3].notes.push_back({40 + i, 1 + i});
        raw.steps[5].notes = {{43, 0}, {43, 50}};
        CHECK(toHex(fm1::seq::encodeWritePart(raw, 3, 0, false)) == sq["writePart0NoSave"].s, "normalise clamps, trims and dedups like hers");
        auto reqs = fm1::seq::readRequests(5);
        CHECK(reqs.size() == sq["readRequests"].a.size(), "readRequests count");
        for (size_t i = 0; i < reqs.size() && i < sq["readRequests"].a.size(); ++i)
            CHECK(reqs[i].addr == uint32_t(sq["readRequests"][i]["addr"].n) && reqs[i].n == int(sq["readRequests"][i]["n"].n), "readRequests addr/len");
        auto times = fm1::seq::stepTimes(p);
        CHECK(times.total == int(sq["stepTimes"]["total"].n) && times.steps.size() == sq["stepTimes"]["steps"].a.size(), "stepTimes total");
        for (size_t i = 0; i < times.steps.size() && i < sq["stepTimes"]["steps"].a.size(); ++i) {
            const J& st = sq["stepTimes"]["steps"][i];
            CHECK(times.steps[i].start == int(st["start"].n) && times.steps[i].dur == int(st["dur"].n) && times.steps[i].gate == int(st["gate"].n), "stepTimes step");
        }
        fm1::seq::Pattern d = fm1::seq::decodePattern(fromHex(sq["decode"]["steps"].s), fromHex(sq["decode"]["gset"].s), int(sq["decode"]["pat"].n));
        fm1::seq::Pattern want = toPattern(sq["decode"]["pattern"]);
        bool same = d.length == want.length && d.rate == want.rate && d.tempo == want.tempo && d.gate == want.gate && d.swing == want.swing && d.sound == want.sound;
        for (int i = 0; i < 64 && same; ++i) {
            same = d.steps[size_t(i)].rate == want.steps[size_t(i)].rate && d.steps[size_t(i)].notes.size() == want.steps[size_t(i)].notes.size();
            for (size_t j = 0; j < d.steps[size_t(i)].notes.size() && same; ++j)
                same = d.steps[size_t(i)].notes[j].note == want.steps[size_t(i)].notes[j].note && d.steps[size_t(i)].notes[j].vel == want.steps[size_t(i)].notes[j].vel;
        }
        CHECK(same, "decodePattern matches hers");
        for (int v = 0; v < 10; ++v) CHECK(fm1::seq::kValueTicks[v] == int(sq["valueTicks"][size_t(v)].n), "VALUE_TICKS");
        CHECK(fm1::seq::kStepsRam == uint32_t(sq["consts"]["steps"].n) && fm1::seq::kExtRam == uint32_t(sq["consts"]["ext"].n) && fm1::seq::kGsetRam == uint32_t(sq["consts"]["gset"].n) && fm1::seq::kGsetLen == int(sq["consts"]["gsetLen"].n), "RAM constants");
    }

    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
