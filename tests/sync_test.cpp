// End-to-end test of the sync layer over real CoreMIDI, against a fake FM-1.
//
// The fake creates virtual MIDI ports named "FM-1 Midi" and behaves like the
// FM-1+VA firmware: answers the identity query, sound reads (0x10) and exact
// writes (0x04), packing replies as the firmware does. Fm1Link finds the ports
// by name, and Fm1Session runs identify, pull, push and verify against it.
//
// Run: sync_test golden.json   (needs no hardware; macOS only, virtual ports)

#include <cstdio>
#include <fstream>
#include <sstream>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>

#include "Fm1Codec.h"
#include "Fm1Link.h"
#include "Fm1Session.h"
#include "Fm1Seq.h"

#include <map>

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, msg) do { if (cond) ++g_pass; else { ++g_fail; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } } while (0)

// ---- the fake synth ------------------------------------------------------------

class FakeFm1 : private juce::MidiInputCallback {
public:
    std::array<fm1::Sound, 128> store;
    int identityVersion = 89;
    int dropNextReplies = 0;      // simulate lost frames to exercise the retry
    int reads = 0, writes = 0, identities = 0, memReads = 0, patternWrites = 0;
    std::atomic<int> received{0};   // every message, SysEx or not
    std::map<uint32_t, uint8_t> ram;   // sparse RAM for the memory-read command
    fm1::Bytes gset = fm1::Bytes(fm1::seq::kGsetLen, 0);
    void putPattern(int pat, const fm1::seq::Pattern& p) {
        // lay the pattern out as the firmware does: 32 B steps, 0xFF = no note
        for (int i = 0; i < 64; ++i) {
            uint32_t base = i < 16 ? fm1::seq::kStepsRam + uint32_t(pat) * 512 + uint32_t(i) * 32
                                   : fm1::seq::kExtRam + uint32_t(pat) * 1536 + uint32_t(i - 16) * 32;
            for (uint32_t k = 0; k < 32; ++k) ram[base + k] = 0;
            for (int j = 0; j < 10; ++j) ram[base + uint32_t(j)] = 0xFF;
            const auto& st = p.steps[size_t(i)];
            for (size_t j = 0; j < st.notes.size(); ++j) { ram[base + uint32_t(j)] = uint8_t(st.notes[j].note); ram[base + 20 + uint32_t(j)] = uint8_t(st.notes[j].vel); }
            ram[base + 10] = uint8_t(st.rate);
        }
        gset[size_t(98 + pat)] = uint8_t(p.length); gset[size_t(50 + pat)] = uint8_t(p.rate);
        gset[size_t(66 + 2 * pat)] = uint8_t(p.tempo & 0xFF); gset[size_t(67 + 2 * pat)] = uint8_t(p.tempo >> 8);
        gset[size_t(18 + pat)] = uint8_t(p.gate); gset[size_t(34 + pat)] = uint8_t(p.swing); gset[size_t(118 + pat)] = uint8_t(p.sound);
        for (int k = 0; k < fm1::seq::kGsetLen; ++k) ram[fm1::seq::kGsetRam + uint32_t(k)] = gset[size_t(k)];
    }

    bool start() {
        in_ = juce::MidiInput::createNewDevice("FM-1 Midi", this);   // the plugin's OUTPUT goes here
        out_ = juce::MidiOutput::createNewDevice("FM-1 Midi");        // the plugin's INPUT comes from here
        if (!in_ || !out_) return false;
        in_->start();
        return true;
    }

private:
    void reply(uint8_t kind, uint8_t status, uint32_t arg, const fm1::Bytes& data) {
        fm1::Bytes buf = {fm1::kSubId, kind, status, uint8_t(arg), uint8_t(arg >> 8), uint8_t(arg >> 16), uint8_t(arg >> 24),
                          uint8_t(data.size()), uint8_t(data.size() >> 8)};
        buf.insert(buf.end(), data.begin(), data.end());
        unsigned sum = 0; for (uint8_t b : buf) sum += b;
        buf.push_back(uint8_t(~sum));
        fm1::Bytes packed = fm1::pack7(buf.data(), buf.size());
        fm1::Bytes frame = {0xF0};
        frame.insert(frame.end(), packed.begin(), packed.end());
        frame.push_back(0xF7);
        if (dropNextReplies > 0) { --dropNextReplies; return; }
        out_->sendMessageNow(juce::MidiMessage::createSysExMessage(frame.data() + 1, int(frame.size()) - 2));
    }

    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m) override {
        ++received;
        if (!m.isSysEx()) return;
        fm1::Bytes f(m.getRawData(), m.getRawData() + m.getRawDataSize());
        if (f == fm1::kIdentityQuery) {
            ++identities;
            std::string text = "FM-1_0" + std::to_string(identityVersion);
            fm1::Bytes block = {0x00, 0x59, 0x11, 27, 0, 0};
            fm1::Bytes body(27, 0);
            for (size_t i = 0; i < text.size(); ++i) body[i] = uint8_t(text[i]);
            block.insert(block.end(), body.begin(), body.end());
            unsigned sum = 0; for (uint8_t b : body) sum += b;
            block.push_back(uint8_t(~sum));
            fm1::Bytes packed = fm1::pack7(block.data(), block.size());
            if (dropNextReplies > 0) { --dropNextReplies; return; }
            out_->sendMessageNow(juce::MidiMessage::createSysExMessage(packed.data(), int(packed.size())));
            return;
        }
        if (f.size() >= 7 && f[1] == 0x43 && f[3] == fm1::kSubId) {
            if (f[4] == 0x04) {                 // v4 exact write: sum covers the payload only
                ++writes;
                try { if (auto s = fm1::decodeExact(f)) store[size_t(s->slot)] = *s; } catch (const fm1::CodecError&) {}
                return;
            }
            fm1::Bytes body(f.begin() + 4, f.end() - 2);
            if (fm1::ysum(body) != f[f.size() - 2]) return;   // the firmware ignores a bad sum
            if (body[0] == 0x11) {           // memory read
                ++memReads;
                uint32_t addr = 0;
                for (int i = 0; i < 5; ++i) addr |= uint32_t(body[size_t(1 + i)] & 0x7F) << (7 * i);
                int n = body[6] | (body[7] << 7);
                fm1::Bytes data;
                for (int i = 0; i < n; ++i) { auto it = ram.find(addr + uint32_t(i)); data.push_back(it == ram.end() ? 0 : it->second); }
                reply(0x51, 0, addr, data);
                return;
            }
            if (body[0] == 0x20) {           // pattern write: eight steps and the settings
                ++patternWrites;
                int pat = body[1], part = body[2];
                fm1::seq::Pattern p;
                // read the current stored pattern, apply the message, store it back
                fm1::Bytes steps;
                for (int i = 0; i < 64; ++i) {
                    uint32_t base = i < 16 ? fm1::seq::kStepsRam + uint32_t(pat) * 512 + uint32_t(i) * 32 : fm1::seq::kExtRam + uint32_t(pat) * 1536 + uint32_t(i - 16) * 32;
                    for (uint32_t k = 0; k < 32; ++k) { auto it = ram.find(base + k); steps.push_back(it == ram.end() ? 0xFF : it->second); }
                }
                try { p = fm1::seq::decodePattern(steps, gset, pat); } catch (...) {}
                p.length = body[4]; p.rate = body[5]; p.tempo = body[6] | (body[7] << 7); p.gate = body[8]; p.swing = body[9]; p.sound = body[10];
                for (int i = 0; i < 8; ++i) {
                    const uint8_t* st = body.data() + 11 + i * 20;
                    auto& dst = p.steps[size_t(part * 8 + i)];
                    dst.rate = st[0]; dst.notes.clear();
                    for (int j = 0; j < st[1] && j < 9; ++j) dst.notes.push_back({st[2 + j], st[11 + j]});
                }
                putPattern(pat, p);
                reply(0x52, 0, uint32_t(pat), {});
                return;
            }
            if (body[0] == 0x10) {
                ++reads;
                int slot = body[1];
                const auto& s = store[size_t(slot)];
                fm1::Bytes data(s.voice.begin(), s.voice.end());
                data.insert(data.end(), s.record.begin(), s.record.end());
                reply(0x50, 0, uint32_t(slot), data);
            }
        }
    }

    std::unique_ptr<juce::MidiInput> in_;
    std::unique_ptr<juce::MidiOutput> out_;
};

// ---- helpers ---------------------------------------------------------------------

static void pump(int ms) {
    auto end = juce::Time::getMillisecondCounter() + juce::uint32(ms);
    while (juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
}
static void waitIdle(Fm1Session& s, int maxMs) {
    auto end = juce::Time::getMillisecondCounter() + juce::uint32(maxMs);
    while (s.busy() && juce::Time::getMillisecondCounter() < end) pump(20);
    pump(50);
}
static fm1::Bytes fromHex(const std::string& h) {
    fm1::Bytes b;
    for (size_t i = 0; i + 1 < h.size(); i += 2) b.push_back(uint8_t(std::strtoul(h.substr(i, 2).c_str(), nullptr, 16)));
    return b;
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juceInit;
    if (argc < 2) { std::printf("usage: sync_test golden.json\n"); return 2; }

    // the 16 VA pack presets from golden.json, into slots 112..127; INIT elsewhere
    std::ifstream f(argv[1]); std::stringstream ss; ss << f.rdbuf(); std::string text = ss.str();
    FakeFm1 fake;
    fm1::Voice init = fm1::packVoice(fm1::kInitEdit);
    for (int i = 0; i < 128; ++i) { fake.store[size_t(i)].slot = i; fake.store[size_t(i)].voice = init; fake.store[size_t(i)].record = fm1::defaultRecord(); fake.store[size_t(i)].hasRecord = true; }
    for (size_t pos = 0;;) {
        size_t k = text.find("\"exact\": \"", pos);
        if (k == std::string::npos) break;
        k += 10;
        auto s = fm1::decodeExact(fromHex(text.substr(k, text.find('"', k) - k)));
        fake.store[size_t(s->slot)] = *s;
        pos = k;
    }
    CHECK(fake.start(), "fake FM-1 created virtual ports");
    pump(300);

    Fm1Link link;
    auto ports = Fm1Link::findFm1();
    CHECK(ports.has_value(), "findFm1 finds the virtual FM-1 ports by name");
    if (!ports) return 1;
    CHECK(link.open(ports->inputId, ports->outputId), "link opens");

    Fm1Session session(link);
    std::vector<fm1::Sound> read, written;
    std::vector<Fm1Session::Progress> progress;
    std::optional<fm1::Identity> identity;
    session.onSoundRead = [&](const fm1::Sound& s) { read.push_back(s); };
    session.onSoundWritten = [&](const fm1::Sound& s) { written.push_back(s); };
    session.onProgress = [&](const Fm1Session::Progress& p) { progress.push_back(p); };
    session.onIdentity = [&](const fm1::Identity& id) { identity = id; };

    // identify
    session.identify();
    waitIdle(session, 5000);
    CHECK(identity && identity->model == "FM-1" && identity->version == 89, "identity FM-1_089");
    CHECK(!progress.empty() && progress.back().finished && !progress.back().failed, "identify reports success");

    // identify survives a dropped reply (retry)
    identity.reset(); fake.dropNextReplies = 1;
    session.identify();
    waitIdle(session, 8000);
    CHECK(identity && identity->version == 89, "identify retries after a lost reply");

    // pull one
    read.clear();
    session.pull({112});
    waitIdle(session, 5000);
    CHECK(read.size() == 1 && read[0].slot == 112 && fm1::voiceName(read[0].voice) == "SUPERSAW  ", "pull slot 112 = SUPERSAW");
    CHECK(read.size() == 1 && fm1::engineOf(read[0].record) == fm1::Engine::VA, "record came through (VA marker)");

    // pull all 128
    read.clear(); progress.clear();
    auto t0 = juce::Time::getMillisecondCounter();
    std::vector<int> all(128); for (int i = 0; i < 128; ++i) all[size_t(i)] = i;
    session.pull(all);
    waitIdle(session, 60000);
    auto dt = juce::Time::getMillisecondCounter() - t0;
    std::printf("pull all 128: %u ms\n", unsigned(dt));
    CHECK(read.size() == 128, "pull all reads 128");
    bool allMatch = read.size() == 128;
    for (size_t i = 0; i < read.size() && allMatch; ++i) allMatch = read[i].slot == int(i) && read[i].voice == fake.store[i].voice && read[i].record == fake.store[i].record;
    CHECK(allMatch, "every pulled preset equals the fake's store");
    CHECK(progress.back().finished && !progress.back().failed && progress.back().done == 128, "pull all reports 128/128");

    // push two changed presets, verified by read-back, 3 s apart
    fm1::Sound a = fake.store[0], b = fake.store[1];
    a.voice = fm1::withName(a.voice, "PUSHED ONE"); a.record[18] = fm1::kMarkVA; a.record[19] = 2;
    b.voice = fm1::withName(b.voice, "PUSHED TWO");
    written.clear(); progress.clear();
    t0 = juce::Time::getMillisecondCounter();
    session.push({a, b});
    waitIdle(session, 20000);
    dt = juce::Time::getMillisecondCounter() - t0;
    std::printf("push 2: %u ms\n", unsigned(dt));
    CHECK(written.size() == 2 && written[0].slot == 0 && written[1].slot == 1, "push wrote and verified two");
    CHECK(fm1::voiceName(fake.store[0].voice) == "PUSHED ONE" && fake.store[0].record[18] == fm1::kMarkVA, "fake store updated by the v4 write");
    CHECK(dt >= 2900 && dt < 8000, "writes paced ~3 s apart");
    CHECK(progress.back().finished && !progress.back().failed, "push reports success");

    // push against stock firmware is refused with a clear message
    fake.identityVersion = 15; identity.reset();
    session.identify(); waitIdle(session, 5000);
    CHECK(identity && identity->isStock(), "stock identity recognised");
    progress.clear(); read.clear();
    session.pull({0}); waitIdle(session, 5000);
    CHECK(read.empty() && progress.back().failed && progress.back().text.contains("cannot send presets back"), "pull on stock firmware refused with explanation");

    // Felucca answers the identity request as FM-1_904 and is not taken for FM-1+VA
    CHECK(fm1::firmwareFor(fm1::Identity{"FM-1", 904})->name() == "Felucca", "FM-1_904 is Felucca");
    CHECK(fm1::firmwareFor(fm1::Identity{"FM-1", 93})->name() == "FM-1+VA", "FM-1_093 is FM-1+VA");
    CHECK(fm1::firmwareFor(fm1::Identity{"FM-1", 19})->name() == "M-VAVE", "FM-1_019 is M-VAVE");
    fake.identityVersion = 904; identity.reset();
    session.identify(); waitIdle(session, 5000);
    CHECK(identity && identity->version == 904 && progress.back().text.contains("Felucca"), "Felucca identity reported");
    progress.clear(); read.clear();
    session.pull({0}); waitIdle(session, 5000);
    CHECK(read.empty() && progress.back().failed && progress.back().text.contains("Felucca"), "pull on Felucca refused with explanation");
    // live edits reach FM-1+VA but not Felucca, whose parameters are not FM-1+VA's
    fm1::Sound liveFrom = fake.store[0], liveTo = liveFrom;
    liveTo.voice[0] = uint8_t((liveTo.voice[0] + 1) % 100);
    CHECK(!fm1::edit::delta(liveFrom, liveTo, fm1::edit::Channels{}).empty(), "the live change has messages to send");
    int before = fake.received.load();
    session.sendChange(liveFrom, liveTo, fm1::edit::Channels{});
    juce::Thread::sleep(300);
    CHECK(fake.received.load() == before, "live edit sends Felucca nothing");
    fake.identityVersion = 89; identity.reset();
    session.identify(); waitIdle(session, 5000);
    before = fake.received.load();
    session.sendChange(liveFrom, liveTo, fm1::edit::Channels{});
    juce::Thread::sleep(300);
    CHECK(fake.received.load() > before, "live edit reaches FM-1+VA");

    // no device answering: the session fails within the retry budget
    fake.identityVersion = 89; identity.reset();
    fake.dropNextReplies = 100;
    progress.clear();
    t0 = juce::Time::getMillisecondCounter();
    session.identify(); waitIdle(session, 15000);
    dt = juce::Time::getMillisecondCounter() - t0;
    CHECK(progress.back().failed && progress.back().text.contains("did not answer"), "silent synth reported");
    CHECK(dt >= 2500 && dt < 6000, "gave up after 3 tries of 1 s");
    fake.dropNextReplies = 0;

    // ---- patterns: pull, push, pull back (re-identify first: the last identify was the silent one)
    {
        identity.reset();
        session.identify(); waitIdle(session, 5000);
        CHECK(identity && identity->version == 89, "re-identified as FM-1_089");
        fm1::seq::Pattern p;
        p.length = 20; p.rate = 6; p.tempo = 133; p.gate = 70; p.swing = 58; p.sound = 5;
        for (auto& st : p.steps) st.rate = 6;
        p.steps[0].notes = {{36, 100}, {48, 90}}; p.steps[3].notes = {{40, 64}}; p.steps[17].notes = {{60, 64}}; p.steps[19].notes = {{62, 64}};
        fake.putPattern(2, p);
        std::vector<std::pair<int, fm1::seq::Pattern>> got;
        session.onPatternRead = [&](int pat, const fm1::seq::Pattern& pp) { got.emplace_back(pat, pp); };
        progress.clear();
        session.pullPatterns({2});
        waitIdle(session, 20000);
        CHECK(got.size() == 1 && got[0].first == 2, "pullPatterns reads pattern 3");
        bool same = got.size() == 1 && got[0].second.length == 20 && got[0].second.tempo == 133 && got[0].second.swing == 58 && got[0].second.sound == 5
                    && got[0].second.steps[0].notes.size() == 2 && got[0].second.steps[0].notes[1].note == 48
                    && got[0].second.steps[17].notes.size() == 1 && got[0].second.steps[17].notes[0].note == 60
                    && got[0].second.steps[19].notes.size() == 1 && got[0].second.steps[2].notes.empty();
        CHECK(same, "pulled pattern matches what the fake holds, steps 17-64 included");
        CHECK(progress.back().finished && !progress.back().failed, "pullPatterns reports success");

        fm1::seq::Pattern q = p;
        q.tempo = 99; q.length = 33; q.steps[32].notes = {{70, 77}}; q.steps[0].notes.clear();
        int writtenPat = -1;
        session.onPatternWritten = [&](int pat) { writtenPat = pat; };
        progress.clear();
        session.pushPatterns({{2, q}}, true);
        waitIdle(session, 20000);
        CHECK(writtenPat == 2 && progress.back().finished && !progress.back().failed, "pushPatterns writes and is acknowledged");
        CHECK(fake.patternWrites == 5, "33 steps = 5 write messages (2 + 3 more eights)");
        got.clear();
        session.pullPatterns({2});
        waitIdle(session, 20000);
        CHECK(got.size() == 1 && got[0].second.tempo == 99 && got[0].second.length == 33 && got[0].second.steps[32].notes.size() == 1
              && got[0].second.steps[32].notes[0].note == 70 && got[0].second.steps[0].notes.empty(), "pushed pattern reads back");
    }

    link.close();
    std::printf("fake saw %d identities, %d reads, %d writes, %d mem reads, %d pattern writes\n", fake.identities, fake.reads, fake.writes, fake.memReads, fake.patternWrites);
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
