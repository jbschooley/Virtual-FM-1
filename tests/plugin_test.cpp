// plugin_test -- the whole plugin (FM1Processor), without a host.
//
//   plugin_test render <golden.json> <out.txt>
//       Renders every preset in golden.json through processBlock at 44.1, 48
//       and 96 kHz, with Hardware character off and on, at block sizes 64, 256
//       and 1024, with notes, pitch bend and the mod wheel; plus the sequencer
//       and the arpeggiator playing. One line per case: a hash of the exact
//       float output and its RMS. Comparing two runs of one build against
//       another on the same machine shows whether a change altered the sound
//       at all (the restructure must not). ctest runs it to catch crashes; the
//       comparison is done by hand, since the hashes differ between machines.
//
//   plugin_test checks
//       The plugin's behaviour: Hardware character's 44.1 kHz engine and its
//       latency, projects (firmware, release, Felucca's music and what it could
//       not read), the library, and Felucca (its copies, host parameters, host
//       transport and SysEx, device file, sync between two of its copies), and
//       the messages said with no editor open.
//
//   plugin_test snapshot <golden.json> <dir>
//       Pictures of the editor's tabs and of Felucca's views, for looking at.
//
//   plugin_test library <golden.json>
//       The library folder: moving the old library file in, exact round trips,
//       instances seeing each other's writes, nothing lost when two write one slot.
//
//   plugin_test state-write <golden.json> <dir>
//       Builds a known plugin state (presets, an unsaved edit, a pattern,
//       settings), saves it as the host would to <dir>/state.bin and writes
//       what it should load back as to <dir>/expected.txt.
//
//   plugin_test state-check <dir>...
//       Loads each saved state into a fresh plugin and checks it against its
//       expected.txt: states saved by older versions must keep loading. (The
//       expected values come from the build that wrote the state, so this
//       guards later versions' loading, not that build's own correctness.)
//
// The plugin keeps its library in a temporary folder (FM1_DATA_DIR) and never
// connects to a synth (FM1_NO_DEVICE).

#include <array>
#include <atomic>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "Firmwares.h"
#include "Fm1Json.h"
#include "Panels.h"
#include "Fm1SeqPanel.h"
#if FM1_FELUCCA
 #include "FeluccaSync.h"
#endif
#if FM1_FELUCCA
 #include "FeluccaDevice.h"
 #include "FeluccaPanel.h"
 #include "FeluccaSeqPanel.h"
 #include "FeluccaMidi.h"
#endif

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, msg) do { if (cond) ++g_pass; else { ++g_fail; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, juce::String(msg).toRawUTF8()); } } while (0)

static std::vector<fm1::Sound> goldenSounds(const juce::File& f) {
    std::vector<fm1::Sound> out;
    auto v = juce::JSON::parse(f);
    auto* presets = v["presets"].getArray();
    if (presets == nullptr) return out;
    auto hex = [](const juce::String& s) {
        juce::MemoryBlock mb; mb.loadFromHexString(s);
        return std::vector<uint8_t>(static_cast<const uint8_t*>(mb.getData()), static_cast<const uint8_t*>(mb.getData()) + mb.getSize());
    };
    for (auto& p : *presets) {
        fm1::Sound s;
        s.slot = int(p["slot"].toString().getIntValue());
        auto voice = hex(p["voice"].toString()), record = hex(p["record"].toString());
        if (voice.size() != s.voice.size() || record.size() != s.record.size()) continue;
        std::copy(voice.begin(), voice.end(), s.voice.begin());
        std::copy(record.begin(), record.end(), s.record.begin());
        s.hasRecord = true;
        s.from = "FM-1";
        out.push_back(s);
    }
    return out;
}

static void setEnv(const char* key, const char* value) {
#if JUCE_WINDOWS
    _putenv_s(key, value);
#else
    setenv(key, value, 1);
#endif
}

// A fresh, empty data folder for each plugin instance.
static juce::File freshDataDir() {
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("fm1-plugin-test", "");
    dir.createDirectory();
    setEnv("FM1_DATA_DIR", dir.getFullPathName().toRawUTF8());
    return dir;
}

static uint64_t fnv(uint64_t h, float x) {
    uint32_t bits;
    std::memcpy(&bits, &x, sizeof bits);
    for (int i = 0; i < 4; ++i) { h ^= uint8_t(bits >> (8 * i)); h *= 1099511628211ull; }
    return h;
}

enum class Play { Notes, Sequencer, Arp };

// One case: a fresh plugin playing `s` for 1.5 s; the hash of its exact output and its RMS.
static juce::String renderCase(const fm1::Sound& s, double rate, bool hw, int block, Play play) {
    auto dir = freshDataDir();
    juce::String line;
    {
        FM1Processor p;
        auto set = p.settings();
        set.hardwareCharacter = hw;
        p.setSettings(set);
        p.setCurrentSound(s);
        if (play == Play::Sequencer) {
            const juce::SpinLock::ScopedLockType l(p.sequencer.lock);
            auto& pt = p.sequencer.patterns[0];
            pt.length = 8; pt.tempo = 150; pt.rate = 7;
            for (int i = 0; i < 8; ++i) pt.steps[size_t(i)].notes = {{48 + 5 * i, 70 + 7 * i, 0}};
            pt.steps[2].ratchet = 3; pt.steps[5].notes[0].len = 1; pt.steps[6].notes.clear();   // (held into step 7)
        }
        if (play == Play::Arp) { p.arp.syncToHost = false; p.arp.tempo = 140; p.arp.octaves = 2; p.arp.enabled = true; }
        p.setPlayConfigDetails(0, 2, rate, block);
        p.prepareToPlay(rate, block);
        if (play == Play::Sequencer) { p.sequencer.syncToHost = false; p.sequencer.enabled = true; p.sequencer.play(); }
        juce::AudioBuffer<float> buf(2, block);
        uint64_t h = 1469598103934665603ull;
        double sum = 0; long n = 0;
        const int total = int(rate * 1.5), offAt = int(rate * 0.8), bendAt = int(rate * 0.3), wheelAt = int(rate * 0.5);
        for (int pos = 0; pos < total; pos += block) {
            juce::MidiBuffer midi;
            auto inBlock = [&](int at) { return pos <= at && at < pos + block; };
            if (pos == 0 && play != Play::Sequencer) for (int note : {48, 64, 71}) midi.addEvent(juce::MidiMessage::noteOn(1, note, juce::uint8(100)), 0);
            if (inBlock(bendAt)) midi.addEvent(juce::MidiMessage::pitchWheel(1, 12000), bendAt - pos);
            if (inBlock(wheelAt)) midi.addEvent(juce::MidiMessage::controllerEvent(1, 1, 90), wheelAt - pos);
            if (inBlock(offAt) && play != Play::Sequencer)
                for (int note : {48, 64, 71}) midi.addEvent(juce::MidiMessage::noteOff(1, note), offAt - pos);
            buf.clear();
            p.processBlock(buf, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i) {
                    float x = buf.getSample(ch, i);
                    h = fnv(h, x);
                    sum += double(x) * x; ++n;
                }
        }
        const char* what = play == Play::Notes ? "notes" : (play == Play::Sequencer ? "seq" : "arp");
        line = juce::String(int(rate)) + (hw ? " hw " : " -- ") + juce::String(block).paddedLeft(' ', 4) + " " + what + " "
               + juce::String(s.slot).paddedLeft('0', 3) + " " + juce::String(fm1::voiceName(s.voice)) + " "
               + juce::String::toHexString((juce::int64) h) + " rms " + juce::String(std::sqrt(sum / double(n)), 6);
    }
    dir.deleteRecursively();
    return line;
}

static int render(const juce::File& golden, const juce::File& out) {
    auto sounds = goldenSounds(golden);
    CHECK(!sounds.empty(), "golden.json has presets");
    juce::StringArray lines;
    const int blocks[3] = {64, 256, 1024};
    for (double rate : {44100.0, 48000.0, 96000.0})
        for (bool hw : {false, true})
            for (size_t i = 0; i < sounds.size(); ++i)
                lines.add(renderCase(sounds[i], rate, hw, blocks[i % 3], Play::Notes));
    for (double rate : {44100.0, 48000.0})
        for (bool hw : {false, true}) {
            lines.add(renderCase(sounds[1], rate, hw, 256, Play::Sequencer));
            lines.add(renderCase(sounds[2], rate, hw, 512, Play::Arp));
        }
    out.replaceWithText(lines.joinIntoString("\n") + "\n");
    std::printf("%d cases written to %s\n", lines.size(), out.getFullPathName().toRawUTF8());
    return g_fail ? 1 : 0;
}

static int checks() {
    // the releases the plugin knows, and what it makes of others
    {
        using fm1::Support;
        auto at = [](int v) { return fm1::checkVersion(fm1::Identity{"FM-1", v}); };
        for (const char* fw : {"fm1_stock", "baudgirl_fm1va", "felucca"}) {
            int current = 0;
            for (const auto& v : fm1::knownVersions(fw)) current += v.support == Support::Current;
            CHECK(current == 1, "each firmware has one current release");
        }
        CHECK(fm1::firmwareChoice("baudgirl_fm1va").label() == "FM-1+VA (baud girl) 0.96" && fm1::firmwareChoice("felucca").label() == "Felucca 1.0.3"
              && fm1::firmwareChoice("fm1_stock").label() == "M-VAVE (stock) V15", "the firmware list names each with its current release");
        CHECK(at(96).known && at(96).support == Support::Current && at(96).text.find("not tried") == std::string::npos, "FM-1_096: current, tried on an FM-1");
        CHECK(at(93).support == Support::Tested && at(93).text.find("8-Bit") != std::string::npos, "FM-1_093: tested, says what 096 adds");
        CHECK(at(94).support == Support::Tested && at(94).text.find("beta") != std::string::npos && at(15).support == Support::Current
              && at(910).support == Support::Current, "094 (a tested beta, said so), V15, Felucca 1.0.3 known");
        CHECK(at(92).support == Support::Older && at(92).text.find("GLOBE") != std::string::npos, "an older release says what it lacks");
        auto n = at(97);
        CHECK(n.newer && !n.known && n.support == Support::Current && n.text.find("newer") != std::string::npos && n.text.find("0.96") != std::string::npos,
              "an unknown newer release: synced as the newest known, said to be untested, never refused");
        CHECK(at(911).newer && at(911).firmwareId == "felucca" && at(911).text.find("1.0") != std::string::npos, "Felucca 1.1 too");
        CHECK(!at(70).known && !at(70).newer && at(70).text.find("older") != std::string::npos, "an unknown older release says so");
        CHECK(at(88).text.find("not a release the plugin knows") != std::string::npos && at(95).text.find("not a release the plugin knows") != std::string::npos,
              "one between known releases is not called older");
        CHECK(n.text.find("GLOBE settings not read") != std::string::npos, "a newer FM-1+VA says what newest-known support it lacks");
        CHECK(at(14).firmwareId == "fm1_stock" && !at(14).known && !at(14).newer, "stock V14: older than V15");
        CHECK(at(904).support == Support::Deprecated && at(904).text.find("1.0") != std::string::npos, "Felucca 0.4 beta: retired, says which release to update to");
        CHECK(at(900).text.find("development build") != std::string::npos, "a Felucca build that is not a release");
        {   // Sloop answers as a Felucca development build does (FM-1_900); its editor's INFO names it
            const fm1::Identity sloop{"FM-1", 900, "FELUCCA SLOOP 2.2"}, dev{"FM-1", 900, "FELUCCA v1.1-dev"};
            const auto c = fm1::checkVersion(sloop);
            CHECK(fm1::firmwareIdFor(sloop) == "sloop" && fm1::isFirmwareChoice("sloop") && fm1::firmwareFor(sloop)->name() == "SLOOP"
                  && fm1::isFeluccaFamily("sloop") && fm1::firmwareChoice("sloop").label() == "SLOOP 2.4.1",
                  "an FM-1 running SLOOP is SLOOP, a choice (2.4.1) played like Felucca");
            {   // Melodee: a Felucca fork answering as one (FM-1_9012, FM-1_910 from 1.0): recognised, not synced
                const fm1::Identity m12{"FM-1", 9012, "MELODEE v0.12"}, m10{"FM-1", 910, "MELODEE v1.0"};
                CHECK(fm1::firmwareIdFor(m12) == "melodee" && fm1::firmwareIdFor(m10) == "melodee" && !fm1::isFirmwareChoice("melodee")
                      && !fm1::isFeluccaFamily("melodee"), "Melodee is told apart from Felucca by its INFO, and is no choice");
                const auto mc = fm1::checkVersion(m12);
                CHECK(mc.support == fm1::Support::Deprecated && mc.text.find("MELODEE v0.12, which the plugin does not support yet") != std::string::npos,
                      "and says it is not supported: " + mc.text);
                auto prof = fm1::firmwareFor(m12);
                CHECK(prof->name() == "Melodee" && !prof->has(fm1::Firmware::Feature::ReadPresets) && !prof->has(fm1::Firmware::Feature::WritePatterns),
                      "its profile does nothing");
                const auto f = juce::File::createTempFile(".json");
                f.replaceWithText(R"({"format": "felucca-backup", "version": 1, "firmware": "MELODEE v0.12", "objects": []})");
                felucca::Objects o;
                juce::String err;
                CHECK(!felucca::readBackup(f, o, err, felucca::feluccaDialect()) && err.contains("Melodee"), "its backup files are refused: " + err);
                f.deleteFile();
            }
            const auto cur = fm1::checkVersion(fm1::Identity{"FM-1", 900, "FELUCCA SLOOP 2.4.1"});
            CHECK(cur.support == fm1::Support::Current && cur.known && cur.text == "FM-1_900: SLOOP 2.4.1", "SLOOP 2.4.1: current, nothing to say: " + cur.text);
            const fm1::Identity s23{"FM-1", 900, "FELUCCA SLOOP 2.3"};
            const auto old = fm1::checkVersion(s23);
            CHECK(old.known && old.support == fm1::Support::Older && old.text.find("pulled from only") != std::string::npos
                  && fm1::writesRefused(s23).find("update it to 2.4.1") != std::string::npos && fm1::writesRefused(fm1::Identity{"FM-1", 900, "FELUCCA SLOOP 2.4.1"}).empty(),
                  "SLOOP 2.3: known, pulled from only (nothing sent, no Live): " + old.text);
            const auto v30 = fm1::checkVersion(fm1::Identity{"FM-1", 900, "SLOOP 3.0"});
            CHECK(v30.newer && v30.text.find("SLOOP 3.0, newer than the plugin's 2.4.1") != std::string::npos, "a newer one, also without the FELUCCA prefix: " + v30.text);
            CHECK(c.support == fm1::Support::Older && !c.newer && c.text.find("SLOOP 2.2, older than the plugin's 2.4.1") != std::string::npos,
                  "an older one says so: " + c.text);
            CHECK(fm1::firmwareIdFor(dev) == "felucca" && fm1::firmwareIdFor(fm1::Identity{"FM-1", 910, "FELUCCA v1.0"}) == "felucca",
                  "a Felucca build with its INFO is still Felucca");
            const auto v104 = fm1::checkVersion(fm1::Identity{"FM-1", 910, "FELUCCA v1.0.4"});
            CHECK(v104.newer && v104.text.find("Felucca 1.0.4, newer than the plugin's 1.0.3") != std::string::npos,
                  "a later 1.0.x (FM-1_910, as 1.0) is told apart by its INFO: " + v104.text);
            const auto v10 = fm1::checkVersion(fm1::Identity{"FM-1", 910, "FELUCCA v1.0"});
            CHECK(!v10.newer && v10.support == Support::Tested && v10.text.find("Felucca 1.0, older than the plugin's 1.0.3") != std::string::npos,
                  "and 1.0 is an earlier one: " + v10.text);
            const auto v102 = fm1::checkVersion(fm1::Identity{"FM-1", 910, "FELUCCA v1.0.2"});
            CHECK(!v102.newer && v102.support == Support::Older && v102.text.find("older than the plugin's 1.0.3") != std::string::npos
                  && v102.text.find("FM6 patch bank") != std::string::npos, "1.0.2: older, its bank its own: " + v102.text);
            const auto v103 = fm1::checkVersion(fm1::Identity{"FM-1", 910, "FELUCCA v1.0.3"});
            CHECK(!v103.newer && v103.support == Support::Current && v103.text == "FM-1_910", "1.0.3 is the one built in");
        }
        CHECK(at(908).support == Support::Deprecated && at(909).support == Support::Deprecated && at(909).text.find("from 1.0") != std::string::npos,
              "any Felucca before 1.0 (0.8, 0.9 beta): supported from 1.0, update it");
    }

    auto dir = freshDataDir();
    {
        FM1Processor p;
        auto set = p.settings();
        p.setPlayConfigDetails(0, 2, 48000.0, 256);
        p.prepareToPlay(48000.0, 256);
        CHECK(p.getLatencySamples() == 0, "no latency at 48 kHz without Hardware character");
        set.hardwareCharacter = true;
        p.setSettings(set);
        CHECK(p.getLatencySamples() > 0 && p.getLatencySamples() < 40, "Hardware character at 48 kHz reports the rate converter's latency");
        juce::AudioBuffer<float> buf(2, 256);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, juce::uint8(100)), 10);
        float peak = 0;
        for (int k = 0; k < 40; ++k) { buf.clear(); p.processBlock(buf, midi); midi.clear(); peak = std::max(peak, buf.getMagnitude(0, 256)); }
        CHECK(peak > 0.01f, "a note sounds with Hardware character on at 48 kHz");
        set.hardwareCharacter = false;
        p.setSettings(set);
        CHECK(p.getLatencySamples() == 0, "switching Hardware character off removes the latency");
        p.setPlayConfigDetails(0, 2, 44100.0, 256);
        p.prepareToPlay(44100.0, 256);
        set.hardwareCharacter = true;
        p.setSettings(set);
        CHECK(p.getLatencySamples() == 0, "no rate converter (and no latency) in a 44.1 kHz host");
        // a block larger than announced is still rendered whole
        p.setPlayConfigDetails(0, 2, 48000.0, 128);
        p.prepareToPlay(48000.0, 128);
        juce::AudioBuffer<float> big(2, 2000);
        juce::MidiBuffer m2;
        m2.addEvent(juce::MidiMessage::noteOn(1, 64, juce::uint8(100)), 0);
        p.processBlock(big, m2);
        CHECK(big.getMagnitude(0, 1000, 1000) > 0.0f, "a block larger than announced is rendered to its end");
    }
    // an 8-Bit preset (FM-1_096): kept byte for byte, silent, and only its effects editable
    {
        FM1Processor p;
        fm1::Sound chip = p.bank.slot(9).sound;
        chip.slot = 9;
        chip.hasRecord = true;
        chip.record[18] = fm1::kMark8Bit;
        for (int i : {19, 20, 21, 22, 23, 24, 25, 26, 45, 46, 47, 48, 49, 50, 51, 54, 55, 56, 57, 58}) chip.record[size_t(i)] = uint8_t(0xF0 + (i & 7));
        for (size_t i = 0; i < 118; ++i) chip.voice[i] = uint8_t((i * 37) & 0x7F);   // not a DX7 voice
        p.bank.setSound(9, chip, false);
        p.selectSlot(9);
        CHECK(fm1::engineOf(p.bank.current().record) == fm1::Engine::EightBit && p.bank.slotLabel(9).contains("[8-Bit]"), "an 0xC3 record is an 8-Bit preset");
        auto move = [&](const juce::String& id) {
            if (auto* prm = p.apvts.getParameter(id)) prm->setValueNotifyingHost(prm->getValue() > 0.5f ? 0.1f : 0.9f);
        };
        auto valueOf = [&](const juce::String& id) { auto* prm = p.apvts.getParameter(id); return prm ? prm->getValue() : -1.0f; };
        const float fltBefore = valueOf(Params::filterId(1)), envBefore = valueOf(Params::envId(0));
        move(Params::vcedId(134)); move(Params::filterId(1)); move(Params::envId(0)); move(Params::fxParamId(1, 0));
        CHECK(p.isEdited(), "the moved controls count as an edit");
        const fm1::Sound after = p.commitCurrent();
        bool kept = after.voice == chip.voice;
        for (int i = 0; i < fm1::kRecordBytes; ++i) if (i != 3) kept &= after.record[size_t(i)] == chip.record[size_t(i)];
        CHECK(kept, "storing an 8-Bit preset changes none of its kit, bass and lead (FM, filter and envelope controls ignored)");
        CHECK(after.record[3] != chip.record[3], "its effects still edit");
        CHECK(!p.isEdited() && valueOf(Params::filterId(1)) == fltBefore && valueOf(Params::envId(0)) == envBefore,
              "after Store the ignored controls show what it holds again");
        p.setPlayConfigDetails(0, 2, 44100.0, 256);
        p.prepareToPlay(44100.0, 256);
        juce::AudioBuffer<float> buf(2, 256);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, juce::uint8(100)), 0);
        float peak = 0;
        for (int k = 0; k < 20; ++k) { buf.clear(); p.processBlock(buf, midi); midi.clear(); peak = std::max(peak, buf.getMagnitude(0, 256)); }
        CHECK(peak == 0.0f, "an 8-Bit preset is silent (its engine's source is not published)");
        p.selectSlot(0);
        midi.addEvent(juce::MidiMessage::noteOn(1, 62, juce::uint8(100)), 0);
        peak = 0;
        for (int k = 0; k < 20; ++k) { buf.clear(); p.processBlock(buf, midi); midi.clear(); peak = std::max(peak, buf.getMagnitude(0, 256)); }
        CHECK(peak > 0.01f, "an FM preset after it plays again");
        // JSON: an 8-Bit preset has only its effects as fields, and reads back to the same bytes
        juce::StringArray errors;
        auto j = fm1json::presetToJson(after);
        auto back = fm1json::presetFromJson(juce::JSON::parse(juce::JSON::toString(j)), "p", errors);
        CHECK(j["engine"].toString() == "8-Bit" && !j.hasProperty("operators") && !j.hasProperty("envelope") && !j.hasProperty("noteFilter")
              && back && back->voice == after.voice && back->record == after.record, "an 8-Bit preset through JSON: the same bytes");
        j.getDynamicObject()->setProperty("algorithm", 3);
        errors.clear();
        CHECK(!fm1json::presetFromJson(j, "p", errors) && errors.joinIntoString(" ").contains("8-Bit"), "an FM field on an 8-Bit preset is refused");
        // JSON: a pattern's locks
        fm1json::PatternEntry e{4, {}};
        e.pattern.locks.assign(512, 0xFF);
        e.pattern.locks[8 * 5] = 33; e.pattern.locks[8 * 5 + 1] = 12;   // (a step's locks are listed in order, as her editor reads them)
        e.pattern.locks[8 * 5 + 2] = 57; e.pattern.locks[8 * 5 + 3] = 4;
        errors.clear();
        auto pb = fm1json::patternFromJson(juce::JSON::parse(juce::JSON::toString(fm1json::patternToJson(e))), "q", errors);
        CHECK(pb && pb->pattern.locks == e.pattern.locks, "a pattern's locks through JSON");
    }
   #if FM1_FELUCCA
    // an instance set to SLOOP plays SLOOP's engines (its own copies), apart from Felucca's
    {
        using Fl = FeluccaEngine::Flavor;
        const int slpBefore = FeluccaEngine::copiesInUse(Fl::Sloop), felBefore = FeluccaEngine::copiesInUse();
        juce::MemoryBlock project;
        {
            FM1Processor p;
            p.setPlayConfigDetails(0, 2, 44100.0, 256);
            p.prepareToPlay(44100.0, 256);
            p.setFirmware("sloop");
            CHECK(p.emulates() && p.felucca() != nullptr && p.felucca()->flavor() == Fl::Sloop && p.felucca()->version() == "SLOOP 2.4.1",
                  "set to SLOOP, the instance plays SLOOP 2.4.1");
            CHECK(FeluccaEngine::copiesInUse(Fl::Sloop) == slpBefore + 1 && FeluccaEngine::copiesInUse() == felBefore,
                  "in a SLOOP copy, none of Felucca's");
            juce::AudioBuffer<float> buf(2, 256);
            auto peakOf = [&](int ch, int note) {
                float peak = 0;
                for (int k = 0; k < 60; ++k) {
                    juce::MidiBuffer m;
                    if (k == 0) m.addEvent(juce::MidiMessage::noteOn(ch, note, juce::uint8(110)), 3);
                    if (k == 30) m.addEvent(juce::MidiMessage::noteOff(ch, note), 3);
                    buf.clear();
                    p.processBlock(buf, m);
                    peak = std::max(peak, buf.getMagnitude(0, 256));
                }
                return peak;
            };
            CHECK(peakOf(1, 60) > 0.01f && peakOf(10, 36) > 0.01f, "channel 1 plays part 1, channel 10 the drums");
            p.felucca()->setEngine(0, 6);
            p.felucca()->setParam(0, 0, 33);
            p.getStateInformation(project);
            p.setFirmware("felucca");
            CHECK(p.felucca() != nullptr && p.felucca()->flavor() == Fl::Felucca && FeluccaEngine::copiesInUse(Fl::Sloop) == slpBefore,
                  "switched to Felucca: a Felucca engine, the SLOOP copy given back");
            p.felucca()->setParam(0, 0, 44);
            p.setFirmware("sloop");
            CHECK(p.felucca()->flavor() == Fl::Sloop && p.felucca()->engineOf(0) == 6 && p.felucca()->param(0, 0) == 33,
                  "and back to SLOOP: its sound as it was");
            p.setFirmware("felucca");
            CHECK(p.felucca()->param(0, 0) == 44, "and Felucca's as it was");
            juce::MemoryBlock both;
            p.getStateInformation(both);
            auto tree = juce::ValueTree::readFromData(both.getData(), both.getSize());
            CHECK(tree.getChildWithName("Felucca").isValid() && tree.getChildWithName("Sloop").isValid()
                  && tree.getChildWithName("Sloop").getProperty("version").toString() == "SLOOP 2.4.1",
                  "a project keeps both, apart");
        }
        {   // host automation of SLOOP's own parameters ("slp_..."), and Felucca's left alone meanwhile
            FM1Processor au;
            au.setPlayConfigDetails(0, 2, 44100.0, 128);
            au.prepareToPlay(44100.0, 128);
            au.setFirmware("sloop");
            auto f = au.felucca();
            auto* level = au.apvts.getParameter("slp_t1_level");
            auto* wave = au.apvts.getParameter("slp_t1_e0");
            auto* kit = au.apvts.getParameter("slp_dr_kit");
            auto* dust = au.apvts.getParameter("slp_dust");
            auto* felLevel = au.apvts.getParameter("fel_t1_level");
            CHECK(f && level && wave && kit && dust && felLevel, "SLOOP's parameters are host parameters");
            if (f && level && wave && kit && dust && felLevel) {
                int perTrack[4] = {}, globals = 0;
                for (const auto& en : felparams::entries()) if (en.sloop) { if (en.track >= 0) ++perTrack[en.track]; else ++globals; }
                CHECK(perTrack[0] == 60 && perTrack[1] == 60 && perTrack[2] == 60 && perTrack[3] == 10 && globals == 16
                      && !au.apvts.getParameter("slp_t1_ed_fx") && !au.apvts.getParameter("slp_dr_level") && au.apvts.getParameter("slp_t3_chord")
                      && au.apvts.getParameter("slp_t2_vlead") && au.apvts.getParameter("slp_dr_filt"),
                      "three parts alike (2.4: FILT, STRUM, VLEAD too), the drum track's kit, pattern, slicer and FILT, sixteen globals; not its preset trim");
                const auto ld = f->paramDesc(0, 0);
                CHECK(std::abs(level->getValue() - float(f->param(0, 0) - ld.min) / float(ld.max - ld.min)) < 1e-5f,
                      "the host sees SLOOP's values once the instance is set to SLOOP");
                const float felBefore = felLevel->getValue();
                juce::AudioBuffer<float> b(2, 128);
                juce::MidiBuffer m;
                au.processBlock(b, m);
                level->setValueNotifyingHost(0.25f);
                felLevel->setValueNotifyingHost(felBefore > 0.5f ? 0.1f : 0.9f);   // Felucca's: not SLOOP's level
                au.processBlock(b, m);
                CHECK(f->param(0, 0) == ld.min + int(std::lround(0.25f * float(ld.max - ld.min))), "automation reaches SLOOP");
                const int held = f->param(0, 0);
                felLevel->setValueNotifyingHost(felLevel->getValue() > 0.5f ? 0.2f : 0.8f);   // Felucca's alone
                au.processBlock(b, m);
                CHECK(f->param(0, 0) == held, "Felucca's host parameter does not reach SLOOP");
                const auto kd = f->paramDesc(3, f->firstEngineParam());
                kit->setValueNotifyingHost(float(2) / float(kd.max - kd.min));
                au.processBlock(b, m);
                CHECK(f->param(3, f->firstEngineParam()) == kd.min + 2 && kit->getText(kit->getValue(), 32) == juce::String(kd.names.size() > 2 ? kd.names[2] : std::string("?")),
                      "the drum kit, with SLOOP's own names");
                f->setGlobal(27, 40);
                au.feluccaChanged();
                CHECK(std::abs(dust->getValue() - 40.0f / 127.0f) < 1e-5f, "an edit in SLOOP reaches the host");
                CHECK(std::abs(au.apvts.getParameter("slp_bpm")->getDefaultValue() - (90.0f - 40.0f) / 200.0f) < 1e-6f, "slp_bpm's default is SLOOP's 90");
            }
        }
        {
            FM1Processor q;
            q.setStateInformation(project.getData(), int(project.getSize()));
            CHECK(q.firmwareId() == "sloop" && q.felucca() != nullptr && q.felucca()->flavor() == Fl::Sloop
                  && q.felucca()->engineOf(0) == 6 && q.felucca()->param(0, 0) == 33, "a SLOOP project opens set to SLOOP, with its sound");
        }
        // two SLOOP devices through its editor protocol (BK_LIST / GET / PUT 34-36), as with an FM-1 running SLOOP
        {
            auto a = std::make_shared<FeluccaEngine>(Fl::Sloop), b = std::make_shared<FeluccaEngine>(Fl::Sloop);
            felucca::VirtualEndpoint ea(a), eb(b);
            CHECK(&ea.dialect() == &felucca::sloopDialect(), "its endpoint speaks SLOOP's dialect");
            a->setEngine(1, 6);
            a->setParam(1, 0, 61);
            std::vector<uint8_t> music, slot;
            a->object(0, music);
            CHECK(a->putObject(3, music) == 0, "a project into slot B");
            juce::String err;
            // (2.4) a patch in its FM6 bank: a factory one, stored in bank slot 5 through the editor protocol
            const auto factory = ea.ask(felucca::frame(felucca::kFm6Get, {2, 0}), 400);
            const auto fa = factory ? felucca::argsOf(*factory) : std::vector<uint8_t>{};
            CHECK(fa.size() == 3 + 128 && fa[2] == 0, "FM6_GET: a factory patch");
            if (fa.size() == 3 + 128) {
                std::vector<uint8_t> put = {1, 5};
                put.insert(put.end(), fa.begin() + 3, fa.end());
                const auto r = ea.ask(felucca::frame(felucca::kFm6Put, put), 400);
                CHECK(r && felucca::argsOf(*r).size() >= 3 && felucca::argsOf(*r)[2] == 0, "FM6_PUT into bank slot 5");
            }
            auto all = felucca::backup(ea, {}, err);
            CHECK(all && all->size() == 9 && (*all)[3] == music && all->count(8) && (*all)[8].size() == 3472,
                  "a full backup through SLOOP's protocol: objects 0-8, its FM6 bank too (" + err + ")");
            CHECK(all && felucca::restore(eb, *all, {}, err) && b->engineOf(1) == 6 && b->param(1, 0) == 61
                  && b->object(3, slot) && slot == music, "restored into the other: the working project and slot B (" + err + ")");
            {
                const auto got = eb.ask(felucca::frame(felucca::kFm6Get, {1, 5}), 400);
                const auto ga = got ? felucca::argsOf(*got) : std::vector<uint8_t>{};
                CHECK(ga.size() == 3 + 128 && ga[2] == 0 && std::equal(ga.begin() + 3, ga.end(), fa.begin() + 3),
                      "and its FM6 bank: slot 5 holds the patch");
            }
            {   // a part's sound, with no FM6 to carry
                auto x = std::make_shared<FeluccaEngine>(Fl::Sloop), y = std::make_shared<FeluccaEngine>(Fl::Sloop);
                felucca::VirtualEndpoint ex(x), ey(y);
                x->setEngine(2, 4);
                x->setParam(2, 9, 77);
                juce::String cerr;
                CHECK(felucca::copySound(ex, ey, 2, cerr) && y->engineOf(2) == 4 && y->param(2, 9) == 77, "a part's sound copied (" + cerr + ")");
            }
            std::vector<float> l(256), r(256);
            auto run = [&](int blocks) { for (int k = 0; k < blocks; ++k) { a->render(l.data(), r.data(), 256); b->render(l.data(), r.data(), 256); } };
            felucca::Mirror mirror(ea, eb);
            CHECK(mirror.start(err), "live: both watched (" + err + ")");
            auto settle = [&](int rounds) { for (int i = 0; i < rounds; ++i) { run(4); if (!mirror.tick(err)) break; } };
            a->setParam(0, 9, 99);
            a->setGlobal(1, 37);
            a->setGlobal(27, a->global(27) + 5);   // DUST, SLOOP's own
            settle(10);
            CHECK(b->param(0, 9) == 99 && b->global(1) == 37 && b->global(27) == a->global(27), "a value and globals (DUST too) reach the other side");
            a->setEngine(0, 7);
            settle(20);
            CHECK(b->engineOf(0) == 7, "an engine change on one side loads it on the other");
            const int presetA = a->presetOf(0), presetB = b->presetOf(0);
            settle(20);
            CHECK(a->engineOf(0) == 7 && a->presetOf(0) == presetA && b->presetOf(0) == presetB, "no load bounces back");
            CHECK(err.isEmpty(), "no side stopped answering: " + err);
            mirror.stop();
        }
        {   // the drum track's lanes, whole. A lane edit on the device pushes STEP_CHANGED for the drum
            // track; an editor's write does not (it knows it), so the push is put in here, as the
            // device would send it after its own edit
            struct Injecting : felucca::Endpoint {
                felucca::Endpoint& e;
                std::vector<fm1::Bytes> extra;
                explicit Injecting(felucca::Endpoint& x) : e(x) {}
                const felucca::Dialect& dialect() const override { return e.dialect(); }
                std::optional<fm1::Bytes> ask(const fm1::Bytes& q, int t) override { return e.ask(q, t); }
                std::vector<fm1::Bytes> pushes() override { auto p = e.pushes(); p.insert(p.end(), extra.begin(), extra.end()); extra.clear(); return p; }
            };
            auto a = std::make_shared<FeluccaEngine>(Fl::Sloop), b = std::make_shared<FeluccaEngine>(Fl::Sloop);
            felucca::VirtualEndpoint ea(a), eb(b);
            Injecting ia(ea);
            auto drumStep = [](FeluccaEngine& f, std::vector<uint8_t> args) {
                auto r = f.ask(felucca::frame(33, args));
                return r ? felucca::argsOf(*r) : std::vector<uint8_t>{};
            };
            juce::String err;
            felucca::Mirror mirror(ia, eb);
            CHECK(mirror.start(err), "live (drums): both watched");
            // step 3: lane 6 soft with a ratchet, lane 12 hard (2 bits a lane for level and ratchet)
            const uint32_t on = (1u << 5) | (1u << 11), lv = (1u << 10) | (2u << 22), rt = 2u << 10;
            std::vector<uint8_t> w = {2, uint8_t(on & 127), uint8_t((on >> 7) & 127), uint8_t((on >> 14) & 3)};
            for (int i = 0; i < 5; ++i) w.push_back(uint8_t((lv >> (7 * i)) & 127));
            for (int i = 0; i < 5; ++i) w.push_back(uint8_t((rt >> (7 * i)) & 127));
            const auto before = drumStep(*b, {2});
            drumStep(*a, w);
            const auto wrote = drumStep(*a, {2});
            ia.extra.push_back(felucca::frame(felucca::kStepChanged, {2, 3}));   // step 3 of the drum track
            CHECK(mirror.tick(err), "the push carried (" + err + ")");
            CHECK(wrote.size() == 14 && wrote != before && drumStep(*b, {2}) == wrote,
                  "Live carries a drum step whole: lanes past the fourth, their levels and ratchets");
            mirror.stop();
        }
        // the sequencers through the editor protocol (FeluccaSeq): both firmwares
        {
            struct Injecting : felucca::Endpoint {
                felucca::Endpoint& e;
                std::vector<fm1::Bytes> extra;
                explicit Injecting(felucca::Endpoint& x) : e(x) {}
                const felucca::Dialect& dialect() const override { return e.dialect(); }
                std::optional<fm1::Bytes> ask(const fm1::Bytes& q, int t) override { return e.ask(q, t); }
                std::vector<fm1::Bytes> pushes() override { auto p = e.pushes(); p.insert(p.end(), extra.begin(), extra.end()); extra.clear(); return p; }
            };
            std::vector<float> l(256), r(256);
            for (auto flavor : {Fl::Felucca, Fl::Sloop}) {
                const bool slp = flavor == Fl::Sloop;
                const juce::String who = slp ? "SLOOP: " : "Felucca: ";
                auto a = std::make_shared<FeluccaEngine>(flavor), b = std::make_shared<FeluccaEngine>(flavor);
                felucca::VirtualEndpoint ea(a), eb(b);
                felucca::Step st;
                st.n = 2; st.note = {60, 64, 0, 0}; st.time = felucca::kNote; st.flags = felucca::kAccent | felucca::kSlide; st.vel = 100;
                if (slp) { st.lvl = 0x9B; st.rat = 0x86; } else { st.hit = 0x85; st.acc = 0x81; st.chance = 40; }
                CHECK(ea.ask(felucca::stepWrite(ea.dialect(), 1, 5, st), 400).has_value(), who + "a step written");
                auto back = felucca::readStep(ea, 1, 5);
                CHECK(back && *back == st, who + "and read back the same: notes, tie, accent and slide, " + (slp ? "levels and ratchets" : "lanes, accents and chance"));
                if (!slp) {
                    felucca::Step plain;
                    plain.n = 1; plain.note = {50, 0, 0, 0}; plain.time = felucca::kNote;
                    ea.ask(felucca::stepWrite(ea.dialect(), 1, 6, plain), 400);
                    auto pb = felucca::readStep(ea, 1, 6);
                    CHECK(pb && pb->chance == 100, who + "a step written with the defaults always plays (chance 100)");
                }
                CHECK(ea.ask(felucca::paramWrite(1, felucca::kLen, 24), 400) && felucca::readParam(ea, 1, felucca::kLen) == 24, who + "LEN set and read");
                if (slp) {
                    felucca::DrumStep ds;
                    ds.set(0, true, 3, 0); ds.set(9, true, 1, 2); ds.set(15, true, 2, 3);
                    CHECK(ea.ask(felucca::drumWrite(ea.dialect(), 7, ds), 400) && felucca::readDrumStep(ea, 7) == ds
                          && ds.has(9) && ds.level(9) == 1 && ds.ratchet(15) == 3, who + "a drum step, every lane's level and ratchet");
                }
                juce::String err;
                CHECK(felucca::copyPatterns(ea, eb, 4, err), who + "every pattern copied (" + err + ")");
                auto pa = felucca::readPattern(ea, 1, err), pb = felucca::readPattern(eb, 1, err);
                CHECK(pa && pb && *pa == *pb && pb->len == 24 && pb->steps[5] == st, who + "the other side has them");
                if (slp) { auto da = felucca::readPattern(ea, 3, err), db = felucca::readPattern(eb, 3, err); CHECK(da && db && da->drums == db->drums && db->drums[7].has(15), who + "drum track too"); }
                // the playhead
                CHECK(a->stepOf(0) == -1, who + "stopped: no step");
                a->transport(true);
                for (int k = 0; k < 40; ++k) a->render(l.data(), r.data(), 256);
                const int at = a->stepOf(0);
                CHECK(a->playing() && at >= 0 && at < 16, who + "playing: the step it plays (" + juce::String(at) + ")");
                a->transport(false);
                for (int k = 0; k < 4; ++k) a->render(l.data(), r.data(), 256);
                // Live: the tab's own edit forwarded to the synth's side
                {
                    Injecting ia(ea);
                    felucca::Mirror m(ia, eb);
                    CHECK(m.start(err), who + "live started");
                    felucca::Step other = st;
                    other.note = {48, 0, 0, 0}; other.n = 1;
                    CHECK(m.forward(felucca::stepWrite(ea.dialect(), 2, 9, other), err) && felucca::readStep(ea, 2, 9) == other, who + "an edit forwarded to the synth");
                    if (slp) {
                        // a step edit on the device: SLOOP pushes RELOAD naming nothing new; the patterns come again
                        felucca::Step dev = st; dev.note = {72, 0, 0, 0}; dev.n = 1;
                        ea.ask(felucca::stepWrite(ea.dialect(), 0, 3, dev), 400);
                        auto td = ea.ask(felucca::frame(felucca::kTrackDump, {0}), 400);
                        const auto tda = td ? felucca::argsOf(*td) : std::vector<uint8_t>{};
                        if (tda.size() >= 3) ia.extra.push_back(felucca::frame(felucca::kReload, {tda[1], tda[2], 0}));
                        CHECK(m.tick(err) && felucca::readStep(eb, 0, 3) == dev, who + "a reload that names nothing new re-reads the patterns (" + err + ")");
                        felucca::Step dev2 = dev; dev2.note = {74, 0, 0, 0};
                        ea.ask(felucca::stepWrite(ea.dialect(), 0, 4, dev2), 400);
                        ea.ask(felucca::frame(felucca::kTrack, {1}), 400);
                        auto t1 = ea.ask(felucca::frame(felucca::kTrackDump, {1}), 400);
                        const auto t1a = t1 ? felucca::argsOf(*t1) : std::vector<uint8_t>{};
                        if (t1a.size() >= 3) ia.extra.push_back(felucca::frame(felucca::kReload, {t1a[1], t1a[2], 1}));
                        CHECK(m.tick(err) && felucca::readStep(eb, 0, 4) != dev2, who + "a track selected is not a pattern re-read");
                        {   // a preset loaded in the plugin: the synth's echo of it is its newest RELOAD, so the
                            // device's next step edit (a RELOAD naming the same) still re-reads the patterns
                            b->select(1);
                            b->applyPreset(1, 2);
                            for (int k = 0; k < 20; ++k) { a->render(l.data(), r.data(), 256); b->render(l.data(), r.data(), 256); m.tick(err); }
                            felucca::Step dev3 = dev; dev3.note = {79, 0, 0, 0};
                            ea.ask(felucca::stepWrite(ea.dialect(), 1, 2, dev3), 400);
                            auto t2 = ea.ask(felucca::frame(felucca::kTrackDump, {1}), 400);
                            const auto t2a = t2 ? felucca::argsOf(*t2) : std::vector<uint8_t>{};
                            CHECK(t2a.size() >= 3 && t2a[2] == 2, who + "the preset reached the synth");
                            if (t2a.size() >= 3) ia.extra.push_back(felucca::frame(felucca::kReload, {t2a[1], t2a[2], 1}));
                            CHECK(m.tick(err) && felucca::readStep(eb, 1, 2) == dev3, who + "after a preset loaded in the plugin, a device step edit is still caught");
                        }
                    } else {
                        // a chain playing: its step pushes are not the pattern's
                        // project A: the pattern with another first step, which the chain plays
                        const auto own = felucca::readStep(ea, 0, 0);
                        felucca::Step chainStep = st; chainStep.note = {30, 0, 0, 0}; chainStep.n = 1;
                        ea.ask(felucca::stepWrite(ea.dialect(), 0, 0, chainStep), 400);
                        std::vector<uint8_t> music;
                        a->object(0, music);
                        a->putObject(2, music);
                        if (own) ea.ask(felucca::stepWrite(ea.dialect(), 0, 0, *own), 400);
                        ea.ask(felucca::chainWrite({{0, 1}}), 400);
                        auto started = ea.ask(felucca::chainPlay(true), 400);
                        for (int k = 0; k < 8; ++k) a->render(l.data(), r.data(), 256);
                        auto ch = felucca::readChain(ea);
                        CHECK(ch && ch->running && ch->rows.size() == 1, who + "a song chain plays");
                        juce::Thread::sleep(1100);   // (the mirror polls about once a second)
                        CHECK(m.tick(err), who + "polled");
                        const auto before = felucca::readStep(eb, 0, 0);
                        CHECK(felucca::readStep(ea, 0, 0) == chainStep && before != chainStep, who + "the chain's step plays where the pattern's was");
                        ia.extra.push_back(felucca::frame(felucca::kStepChanged, {0, 0}));
                        CHECK(m.tick(err) && felucca::readStep(eb, 0, 0) == before, who + "a step push while a chain plays is not carried");
                        ea.ask(felucca::chainPlay(false), 400);
                        for (int k = 0; k < 8; ++k) a->render(l.data(), r.data(), 256);
                        {   // stopped: at once carried again (not a second later)
                            felucca::Step after = st; after.note = {33, 0, 0, 0}; after.n = 1;
                            ea.ask(felucca::stepWrite(ea.dialect(), 0, 1, after), 400);
                            ia.extra.push_back(felucca::frame(felucca::kStepChanged, {1, 0}));
                            CHECK(m.tick(err) && felucca::readStep(eb, 0, 1) == after, who + "once the chain stops, step pushes are carried again");
                        }
                        // motion on the selected track: its values are not carried
                        ea.ask(felucca::motionSet(0, 2, 9, 30), 400);
                        ea.ask(felucca::motionOn(0, true), 400);
                        auto mo = felucca::readMotion(ea, 0);
                        CHECK(mo && mo->on && mo->events.size() == 1 && mo->events[0].param == 9 && mo->events[0].value == 30, who + "motion set and read");
                        juce::Thread::sleep(1100);
                        m.tick(err);
                        const int bRate = b->param(0, 9), bLen = b->param(0, felucca::kLen);
                        std::vector<uint8_t> q1 = {0, 9}, q2 = {0, uint8_t(felucca::kLen)};
                        auto v14 = [](std::vector<uint8_t>& q, int v) { q.push_back(uint8_t((v + 8192) & 127)); q.push_back(uint8_t(((v + 8192) >> 7) & 127)); };
                        v14(q1, (bRate + 11) % 100); v14(q2, bLen == 20 ? 21 : 20);
                        ia.extra.push_back(felucca::frame(felucca::kChanged, q1));
                        ia.extra.push_back(felucca::frame(felucca::kChanged, q2));
                        CHECK(m.tick(err) && b->param(0, 9) == bRate && b->param(0, felucca::kLen) != bLen,
                              who + "with motion on, a motion parameter is not carried; LEN is");
                        const int bAtk = b->param(0, 1);
                        std::vector<uint8_t> q3 = {0, 1};
                        v14(q3, bAtk == 20 ? 21 : 20);
                        ia.extra.push_back(felucca::frame(felucca::kChanged, q3));
                        CHECK(m.tick(err) && b->param(0, 1) != bAtk, who + "a motion parameter without events (ATK) still is");
                    }
                    m.stop();
                }
            }
        }
        // the FM-1+VA Sequencer tab: lanes, notes held over steps, locks
        {
            FM1Processor tp;
            Fm1SeqPage page(tp);
            page.setSize(1100, 600);
            page.selectStep(4);
            page.toggleNote(4, 64);
            page.setHold(4, 64, 3);
            {
                const juce::SpinLock::ScopedLockType l(tp.sequencer.lock);
                const auto& s4 = tp.sequencer.patterns[0].steps[4];
                CHECK(s4.notes.size() == 1 && s4.notes[0].note == 64 && s4.notes[0].len == 3, "FM-1+VA tab: a note added and held three steps past its own");
            }
            page.toggleNote(4, 64);
            CHECK(tp.sequencer.patterns[0].steps[4].notes.empty(), "FM-1+VA tab: and taken off again");
            page.addLock(4, 40);
            page.addLock(4, 1);
            page.setLock(4, 1, 200);   // (beyond its range: Feedback is 0 to 7)
            {
                const auto& lk = tp.sequencer.patterns[0].locks;
                CHECK(lk.size() == 512 && lk[8 * 4] == 40 && lk[8 * 4 + 1] == 50 && lk[8 * 4 + 2] == 1 && lk[8 * 4 + 3] == 7,
                      "FM-1+VA tab: locks start mid-range and keep to their range");
            }
            page.removeLock(4, 40);
            CHECK(tp.sequencer.patterns[0].locks[8 * 4] == 1 && tp.sequencer.patterns[0].locks[8 * 4 + 2] == 0xFF, "FM-1+VA tab: a lock removed, the rest move up");
            for (int w : {33, 34, 37, 40, 57}) CHECK(Fm1SeqPage::lockName(w).isNotEmpty(), "lock " + juce::String(w) + " is named: " + Fm1SeqPage::lockName(w));
            CHECK(Fm1SeqPage::lockName(32).isEmpty() && Fm1SeqPage::lockName(35).isEmpty() && Fm1SeqPage::lockName(24).isEmpty(),
                  "an effect's Type, its On/Off and the unused codes are not lockable");
            CHECK(Fm1SeqPage::lockName(1) == "Feedback" && Fm1SeqPage::lockRange(1).second == 7 && Fm1SeqPage::lockName(40) == "Delay Feedback"
                  && Fm1SeqPage::lockRange(33).second == 107, "lock names and ranges as the FM-1 has them");
            CHECK(page.laneCount() == 2 && !page.chipLanes(), "FM-1+VA tab: an FM pattern's lanes are Notes and Locks");
        }
        // the Sequencer tab: its edits are the device's (both firmwares)
        for (auto id : {"felucca", "sloop"}) {
            const juce::String who = juce::String(id) + " tab: ";
            FM1Processor tp;
            tp.setFirmware(id);
            auto f = tp.felucca();
            CHECK(f != nullptr, who + "an engine");
            if (!f) continue;
            FeluccaSeqPage page(tp);
            page.setSize(1100, 600);
            felucca::VirtualEndpoint e(f);
            page.selectTrack(1);
            CHECK(f->selected() == 1, who + "a track chosen is the device's selected part");
            page.toggleNote(4, 62);
            page.toggleNote(4, 65);
            auto s4 = felucca::readStep(e, 1, 4);
            CHECK(s4 && s4->time == felucca::kNote && s4->n == 2 && s4->note[0] == 62 && s4->note[1] == 65, who + "two notes put on step 5");
            page.toggleNote(4, 62);
            s4 = felucca::readStep(e, 1, 4);
            CHECK(s4 && s4->n == 1 && s4->note[0] == 65, who + "and one taken off");
            page.selectStep(4);
            page.setStepTime(felucca::kTie);
            page.setStepFlag(felucca::kSlide, true);
            page.setStepVelocity(90);
            s4 = felucca::readStep(e, 1, 4);
            CHECK(s4 && s4->time == felucca::kTie && (s4->flags & felucca::kSlide) && s4->vel == 90, who + "tie, slide and velocity");
            page.setPatternParam(felucca::kLen, 32);
            page.setPatternParam(felucca::kSwing, 25);
            CHECK(f->param(1, felucca::kLen) == 32 && f->param(1, felucca::kSwing) == 25, who + "LEN and swing");
            {   // the tab's MIDI file: exported, then imported onto another track
                page.selectStep(4);
                page.setStepTime(felucca::kNote);   // (a note again: it was tied above)
                const auto file = page.exportMidi();
                int ons = 0;
                for (int t = 0; t < file.getNumTracks(); ++t)
                    for (auto* ev : *file.getTrack(t)) ons += ev->message.isNoteOn();
                CHECK(file.getNumTracks() == f->tracks() && ons >= 1, who + "exported: a MIDI track a track, with its notes");
                page.selectTrack(2);
                juce::MidiFile part2;   // (part 2's track alone: a whole export's own Part 3 track would be taken)
                part2.setTicksPerQuarterNote(file.getTimeFormat());
                part2.addTrack(*file.getTrack(1));
                const auto said = page.importMidi(part2);
                auto s4 = felucca::readStep(e, 2, 4);
                CHECK(s4 && s4->time == felucca::kNote && s4->note[0] == 65, who + "imported onto part 3: " + said);
                page.selectTrack(1);
            }
            if (juce::String(id) == "felucca") {
                page.selectStep(6);
                page.setStepChance(30);
                auto s6 = felucca::readStep(e, 1, 6);
                CHECK(s6 && s6->chance == 30, who + "chance");
                {   // lane names: its KIT's on a DRUM track only (on another engine E1 is that engine's)
                    f->setEngine(2, 0);                                       // ANALOG
                    f->setParam(2, f->firstEngineParam(), 1);
                    const auto plain = f->laneNames(2);
                    int drum = -1;
                    for (int en = 0; en < f->engines(); ++en) if (f->engineName(en) == "DRUM") drum = en;
                    f->setEngine(2, drum);
                    f->setParam(2, f->firstEngineParam(), 1);                 // the HAND kit
                    const auto hand = f->laneNames(2);
                    CHECK(plain.size() == 8 && plain[5] == "TOM" && hand.size() == 8 && hand[5] == "CONGA", who + "lane names: the DRUM KIT's, or its own KIT's on a DRUM track");
                }
                page.toggleLane(8, 2, false);
                page.toggleLane(8, 2, true);
                auto s8 = felucca::readStep(e, 1, 8);
                CHECK(s8 && (s8->hit & 4) && (s8->acc & 4), who + "a drum lane hit, then accented");
            } else {
                page.toggleNote(10, 60);
                page.selectStep(10);
                page.setNoteLevel(0, 2);
                page.setNoteRatchet(0, 3);
                auto s10 = felucca::readStep(e, 1, 10);
                CHECK(s10 && (s10->lvl & 3) == 2 && (s10->rat & 3) == 3, who + "a note's level and ratchet");
                page.selectTrack(3);
                CHECK(page.drumsView(), who + "the drum track shows lanes");
                page.toggleLane(5, 12, false);
                auto d5 = felucca::readDrumStep(e, 5);
                CHECK(d5 && d5->has(12), who + "a drum hit on lane 13 (RIDE)");
            }
        }
        // MIDI files (FeluccaMidi): out as the firmware plays the steps, and back
        {
            auto events = [](const juce::MidiFile& f) {
                std::vector<std::tuple<int, int, int, int, int>> out;   // channel, note, vel, on, off
                for (int t = 0; t < f.getNumTracks(); ++t) {
                    juce::MidiMessageSequence seq(*f.getTrack(t));
                    seq.updateMatchedPairs();
                    for (int i = 0; i < seq.getNumEvents(); ++i) {
                        auto* e = seq.getEventPointer(i);
                        if (!e->message.isNoteOn()) continue;
                        out.push_back({e->message.getChannel(), e->message.getNoteNumber(), e->message.getVelocity(),
                                       int(e->message.getTimeStamp()), e->noteOffObject ? int(e->noteOffObject->message.getTimeStamp()) : -1});
                    }
                }
                return out;
            };
            auto has = [](const auto& ev, int ch, int note, int vel, int on, int off) {
                for (auto& [c, n, v, a, b] : ev) if (c == ch && n == note && v == vel && a == on && (off < 0 || b == off)) return true;
                return false;
            };
            auto dp0 = [] { felucca::TrackPattern d; d.len = 16; d.gate = 64; d.drums.assign(64, {}); return d; };
            // Felucca: C4 tied over step 2, an accented chord on step 5, a kick and a closed hat on step 7
            felucca::TrackPattern fp;
            fp.len = 16; fp.gate = 64;
            fp.steps.assign(64, {});
            for (auto& st : fp.steps) st.time = felucca::kRest, st.n = 0;
            fp.steps[0] = {}; fp.steps[0].n = 1; fp.steps[0].note = {60, 0, 0, 0}; fp.steps[0].time = felucca::kNote; fp.steps[0].vel = 100;
            fp.steps[1].time = felucca::kTie;
            fp.steps[4] = {}; fp.steps[4].n = 2; fp.steps[4].note = {64, 67, 0, 0}; fp.steps[4].time = felucca::kNote; fp.steps[4].flags = felucca::kAccent;
            fp.steps[6].hit = 1 | 8; fp.steps[6].vel = 90; fp.steps[6].time = felucca::kNote;
            fp.steps[9].hit = 1; fp.steps[9].time = felucca::kRest;   // (a REST step's hits: never played)
            felmidi::Song fs;
            fs.bpm = 100;
            fs.tracks.push_back({0, fp, false, true, 0, felmidi::divQuarters("1/16")});
            const auto ef = events(felmidi::toMidi(fs));
            // 480 a quarter: a 1/16 step is 120 ticks; GATE 64 of 128: 60 ticks
            CHECK(has(ef, 1, 60, 100, 0, 240), "Felucca: a note tied into the next step sounds to that step's gate and half a step (seq.c)");
            CHECK(!has(ef, 10, 36, 96, 1080, -1), "a REST step's lane hits do not play");
            CHECK(has(ef, 1, 64, 127, 480, 540) && has(ef, 1, 67, 127, 480, 540), "an accented chord plays at 127");
            CHECK(has(ef, 10, 36, 90, 720, 780) && has(ef, 10, 42, 90, 720, 780), "drum lanes as GM notes on channel 10");
            fs.tracks[0].swing = 50;   // odd steps 50/250 of a step late: 24 ticks
            CHECK(has(events(felmidi::toMidi(fs)), 1, 64, 127, 480, -1), "an even step is not moved by swing");
            fp.steps[5] = fp.steps[4];
            fs.tracks[0].pattern = fp;
            CHECK(has(events(felmidi::toMidi(fs)), 1, 64, 127, 624, -1), "an odd one is (120 x 5 + 24)");
            {   // a swung last step at GATE 128: its note-off is after the pattern's end, and so is the track's end
                auto late = fs;
                late.tracks[0].pattern.gate = 128;
                late.tracks[0].pattern.steps[15] = fp.steps[0];
                const auto mf = felmidi::toMidi(late);
                double lastOff = 0, eot = 0;
                for (int i = 0; i < mf.getTrack(0)->getNumEvents(); ++i) {
                    const auto& m = mf.getTrack(0)->getEventPointer(i)->message;
                    if (m.isNoteOff()) lastOff = std::max(lastOff, m.getTimeStamp());
                    if (m.isEndOfTrackMetaEvent()) eot = m.getTimeStamp();
                }
                CHECK(lastOff > 1920 && eot >= lastOff, "the end of track comes after the last note-off");
            }
            // back in: the same steps
            fs.tracks[0].swing = 0;
            fs.tracks[0].pattern.steps[5] = {};
            auto back = felmidi::fromMidi(felmidi::toMidi(fs), fp, false, false, 0.25, 0);
            CHECK(back.pattern.steps[0].time == felucca::kNote && back.pattern.steps[0].note[0] == 60 && back.pattern.steps[1].time == felucca::kTie
                  && back.pattern.steps[4].n == 2 && back.pattern.steps[4].note[1] == 67 && back.pattern.len == 8,
                  "and back from the file: the tie, the chord, LEN to the beat after the last step");
            CHECK(back.pattern.steps[6].hit == 9 && back.pattern.steps[6].time == felucca::kNote, "importing notes keeps the step's lane hits");
            auto lanesBack = felmidi::fromMidi(felmidi::toMidi(fs), fp, false, true, 0.25, 0);
            CHECK((lanesBack.pattern.steps[6].hit & 9) == 9 && lanesBack.pattern.steps[6].time == felucca::kNote, "drum notes back onto Felucca's lanes, on a NOTE step");
            CHECK(lanesBack.pattern.steps[0].n == 1 && lanesBack.pattern.steps[0].note[0] == 60 && lanesBack.pattern.steps[1].time == felucca::kTie,
                  "and the track's notes kept");
            {   // which notes: the part's own track; drum notes on the lanes the firmware plays them on
                felmidi::Song two = fs;
                two.tracks[0].pattern.steps[9] = {};
                auto other = fp;
                other.steps.assign(64, {});
                other.steps[0].n = 1; other.steps[0].note = {72, 0, 0, 0}; other.steps[0].time = felucca::kNote;
                two.tracks.push_back({1, other, false, true, 0, 0.25});
                const auto file = felmidi::toMidi(two);
                auto p1 = felmidi::fromMidi(file, fp, false, false, 0.25, 0), p2 = felmidi::fromMidi(file, fp, false, false, 0.25, 1);
                CHECK(p1.pattern.steps[0].n == 1 && p1.pattern.steps[0].note[0] == 60 && p2.pattern.steps[0].n == 1 && p2.pattern.steps[0].note[0] == 72,
                      "a file of several parts: each part from its own track");
                auto p3 = felmidi::fromMidi(file, fp, false, false, 0.25, 2);
                CHECK(p3.pattern.steps[0].n == 2 && p3.from.contains("all 2"), "a part the file has no track for: every track, and it says so");
                juce::MidiMessageSequence gm;
                for (int k : {41, 44, 49, 50}) {
                    gm.addEvent(juce::MidiMessage::noteOn(10, k, juce::uint8(100)), 0);
                    gm.addEvent(juce::MidiMessage::noteOff(10, k), 60);
                }
                juce::MidiFile gmf;
                gmf.setTicksPerQuarterNote(480);
                gmf.addTrack(gm);
                auto fl = felmidi::fromMidi(gmf, fp, false, true, 0.25, 0);
                auto sl = felmidi::fromMidi(gmf, dp0(), true, true, 0.25, 3);
                CHECK(fl.pattern.steps[0].hit == ((1 << 5) | (1 << 3) | (1 << 7)), "GM toms, a pedal hat and a crash on Felucca's TOM, HATCL and BELL lanes (drum_lane)");
                CHECK(sl.pattern.drums[0].has(9) && sl.pattern.drums[0].has(6) && sl.pattern.drums[0].has(11) && sl.pattern.drums[0].has(10),
                      "and on SLOOP's LOW TOM, PEDAL, CRASH, HI TOM (lane_of_note)");
                juce::MidiFile empty;
                empty.setTicksPerQuarterNote(480);
                empty.addTrack(juce::MidiMessageSequence{});
                auto none = felmidi::fromMidi(empty, fp, false, false, 0.25, 0);
                CHECK(none.notes == 0 && none.pattern == fp, "a file with no notes leaves the pattern as it was");
                juce::MidiFile smpte;
                smpte.setSmpteTimeFormat(25, 40);
                smpte.addTrack(gm);
                auto sm = felmidi::fromMidi(smpte, fp, false, false, 0.25, 0);
                CHECK(sm.smpte && sm.pattern == fp, "an SMPTE-timed one too");
                juce::MidiMessageSequence trip;
                for (int k = 0; k < 6; ++k) { trip.addEvent(juce::MidiMessage::noteOn(1, 60, juce::uint8(100)), k * 160); trip.addEvent(juce::MidiMessage::noteOff(1, 60), k * 160 + 80); }
                juce::MidiFile tf;
                tf.setTicksPerQuarterNote(480);
                tf.addTrack(trip);
                CHECK(felmidi::fromMidi(tf, fp, false, false, 1.0 / 3.0, 0).pattern.len == 6, "8T: two beats of 3 steps are LEN 6");
            }
            // SLOOP: a ghost note with a ratchet of 2; the drum track's levels
            felucca::TrackPattern sp;
            sp.len = 16; sp.gate = 64;
            sp.steps.assign(64, {});
            sp.steps[2].n = 1; sp.steps[2].note = {48, 0, 0, 0}; sp.steps[2].time = felucca::kNote; sp.steps[2].vel = 100;
            sp.steps[2].lvl = 1; sp.steps[2].rat = 1;
            felucca::TrackPattern dp;
            dp.len = 16; dp.gate = 64;
            dp.drums.assign(64, {});
            dp.drums[0].set(0, true, 3, 0);   // kick, hard
            dp.drums[2].set(4, true, 2, 0);   // hat, soft
            felmidi::Song ss;
            ss.sloop = true;
            ss.tracks.push_back({0, sp, false, false, 0, 0.25});
            ss.tracks.push_back({3, dp, true, false, 0, 0.25});
            const auto es = events(felmidi::toMidi(ss));
            CHECK(has(es, 1, 48, 42, 240, 270) && has(es, 1, 48, 42, 300, 330), "SLOOP: a ghost note (42) ratcheted: two hits, each its share of the gate");
            {
                auto chord = sp;
                chord.steps[2].n = 2; chord.steps[2].note = {48, 52, 0, 0};
                chord.steps[3].time = felucca::kTie;
                felmidi::Song cs;
                cs.sloop = true;
                cs.tracks.push_back({0, chord, false, false, 0, 0.25});
                CHECK(has(events(felmidi::toMidi(cs)), 1, 52, 100, 240, 270), "a chord's unratcheted note: the gate shared by the largest ratchet, not held into a TIE");
            }
            CHECK(has(es, 10, 36, 127, 0, 60) && has(es, 10, 42, 72, 240, 300), "its drum track: levels as velocities (hard 127, soft 72)");
            auto drumsBack = felmidi::fromMidi(felmidi::toMidi(ss), dp, true, true, 0.25, 3);
            CHECK(drumsBack.pattern.drums[0].has(0) && drumsBack.pattern.drums[0].level(0) == 3 && drumsBack.pattern.drums[2].level(4) == 2,
                  "and back onto its lanes, levels from the velocities");
        }
        // the device file: SLOOP's own backup format, in the library's SLOOP folder
        {
            const auto where = felucca::DeviceStore::defaultFile(felucca::sloopDialect());   // (named only: never written here)
            CHECK(where.getFileName() == "SLOOP device.json" && where.getParentDirectory().getFileName() == "SLOOP", "<library>/SLOOP/SLOOP device.json");
            const auto file = juce::File::createTempFile(".json");
            auto e = std::make_shared<FeluccaEngine>(Fl::Sloop);
            felucca::DeviceStore store(file, felucca::sloopDialect());
            store.load(*e);
            std::vector<uint8_t> music;
            e->object(0, music);
            e->putObject(4, music);   // slot C
            CHECK(store.tick(*e).isEmpty() && file.existsAsFile(), "a change saves the device");
            felucca::Objects o;
            juce::String err;
            const auto json = juce::JSON::parse(file);
            CHECK(json.getProperty("format", {}).toString() == "sloop-backup" && felucca::readBackup(file, o, err, felucca::sloopDialect())
                  && o[4] == music && o.count(8), "as SLOOP's editor writes a backup (sloop-backup), slot C and the FM6 bank (2.4) in it");
            CHECK(!felucca::readBackup(file, o, err, felucca::feluccaDialect()), "not taken for a Felucca backup");
            file.deleteFile();
        }
    }
   #endif
   #if FM1_FELUCCA
    // an instance set to Felucca plays Felucca's engines, and keeps them in its project
    {
        const int inUse = FeluccaEngine::copiesInUse();
        juce::MemoryBlock project;
        int felParamValue = 0, felGlobalValue = 0;
        std::array<uint8_t, 155> felPatch{};
        {
            FM1Processor p;
            p.setPlayConfigDetails(0, 2, 48000.0, 256);
            p.prepareToPlay(48000.0, 256);
            p.setFirmware("felucca");
            CHECK(p.emulates() && p.felucca() != nullptr, "set to Felucca, the instance plays it");
            CHECK(FeluccaEngine::copiesInUse() == inUse + 1, "with a copy of its own");
            CHECK(p.getLatencySamples() > 0, "at 48 kHz, with the converter's latency");
            juce::AudioBuffer<float> buf(2, 256);
            float peak = 0;
            for (int k = 0; k < 100; ++k) {
                juce::MidiBuffer m;
                if (k == 0) { m.addEvent(juce::MidiMessage::noteOn(1, 48, juce::uint8(100)), 5); m.addEvent(juce::MidiMessage::noteOn(2, 60, juce::uint8(100)), 9); }
                buf.clear();
                p.processBlock(buf, m);
                peak = std::max(peak, buf.getMagnitude(0, 256));
            }
            CHECK(peak > 0.01f, "notes on channels 1 and 2 play Felucca's parts");
            p.felucca()->setEngine(1, 6);                 // part 2: TRIO
            const int eid = p.felucca()->firstEngineParam() + 2;
            auto ed = p.felucca()->paramDesc(1, eid);
            felParamValue = ed.min + (ed.max - ed.min) / 3;   // something other than its default
            if (felParamValue == p.felucca()->param(1, eid)) ++felParamValue;
            p.felucca()->setParam(1, eid, felParamValue);
            auto gd = p.felucca()->globalDesc(1);         // SWING
            felGlobalValue = gd.max;
            p.felucca()->setGlobal(1, felGlobalValue);
            p.felucca()->setEngine(2, p.felucca()->fm6Engine());   // part 3: FM6, with an edited patch
            felPatch = p.felucca()->fm6Patch(2);
            for (int i = 145; i < 155; ++i) felPatch[size_t(i)] = uint8_t('a' + (i - 145));   // its name
            felPatch[134] = uint8_t((felPatch[134] + 7) % 32);                               // its algorithm
            p.felucca()->setFm6Patch(2, felPatch);
            p.getStateInformation(project);
            p.setFirmware("baudgirl_fm1va");
            CHECK(FeluccaEngine::copiesInUse() == inUse, "switching away gives the copy back");
            CHECK(p.getLatencySamples() == 0, "and the latency goes with it");
        }
        // host automation of Felucca's parameters
        {
            FM1Processor au;
            au.setPlayConfigDetails(0, 2, 44100.0, 128);
            au.prepareToPlay(44100.0, 128);
            au.setFirmware("felucca");
            auto f = au.felucca();
            CHECK(f != nullptr, "an instance for automation");
            if (f) {
                auto* level = au.apvts.getParameter("fel_t1_level");
                auto* wave = au.apvts.getParameter("fel_t1_e0");   // ANALOG's WAVE on part 1
                auto* bpm = au.apvts.getParameter("fel_bpm");
                CHECK(level && wave && bpm, "Felucca's parameters are host parameters");
                if (level && wave && bpm) {
                    auto ld = f->paramDesc(0, 0);
                    CHECK(std::abs(level->getValue() - float(f->param(0, 0) - ld.min) / float(ld.max - ld.min)) < 1e-5f,
                          "the host sees Felucca's values once the instance is set to Felucca");
                    juce::AudioBuffer<float> b(2, 128);
                    juce::MidiBuffer m;
                    au.processBlock(b, m);   // the first block takes the host's values as they are
                    level->setValueNotifyingHost(0.25f);
                    au.processBlock(b, m);
                    CHECK(f->param(0, 0) == ld.min + int(std::lround(0.25f * float(ld.max - ld.min))), "automation reaches Felucca");
                    f->setParam(0, 0, ld.min + 7);
                    au.feluccaChanged(0);
                    CHECK(std::abs(level->getValue() - 7.0f / float(ld.max - ld.min)) < 1e-5f, "an edit in Felucca reaches the host");
                    auto wd = f->paramDesc(0, f->firstEngineParam());
                    wave->setValueNotifyingHost(float(1) / float(wd.max - wd.min));
                    au.processBlock(b, m);
                    CHECK(wave->getText(wave->getValue(), 32) == juce::String(wd.names.size() > 1 ? wd.names[1] : std::string("?")),
                          "the host shows Felucca's own value names");
                    // defaults are Felucca's own; four parts alike; nothing for what the device never reads
                    CHECK(std::abs(bpm->getDefaultValue() - (120.0f - 40.0f) / 200.0f) < 1e-6f, "fel_bpm's default is Felucca's 120");
                    int perPart[4] = {};
                    for (const auto& en : felparams::entries()) if (en.track >= 0 && !en.sloop) ++perPart[en.track];
                    CHECK(perPart[0] == 70 && perPart[1] == 70 && perPart[2] == 70 && perPart[3] == 70
                          && au.apvts.getParameter("fel_t4_atk") && au.apvts.getParameter("fel_t3_m2dst") && au.apvts.getParameter("fel_rtype"),
                          "four parts, each with the same parameters (1.0's modulation and chords too)");
                    CHECK(!au.apvts.getParameter("fel_t1_fm1_atk") && !au.apvts.getParameter("fel_t1_ed_fx"),
                          "none for parameters nothing on the device reads");
                    // a project's Felucca sound wins over host values saved stale
                    f->setParam(0, 0, ld.min + 30);
                    au.feluccaChanged(0);
                    level->setValueNotifyingHost(0.9f);   // a host value never applied
                    juce::MemoryBlock pj;
                    au.getStateInformation(pj);
                    FM1Processor lo;
                    lo.setPlayConfigDetails(0, 2, 44100.0, 128);
                    lo.prepareToPlay(44100.0, 128);
                    lo.setStateInformation(pj.getData(), int(pj.getSize()));
                    juce::AudioBuffer<float> b2(2, 128);
                    for (int k = 0; k < 3; ++k) lo.processBlock(b2, m);
                    auto lf = lo.felucca();
                    CHECK(lf && lf->param(0, 0) == ld.min + 30, "a loaded project plays its Felucca sound, not stale host values");
                    if (lf) {
                        auto* lv = lo.apvts.getParameter("fel_t1_level");
                        CHECK(std::abs(lv->getValue() - 30.0f / float(ld.max - ld.min)) < 1e-5f, "and the host's value follows it");
                    }
                    std::printf("  host text: %s = %s, %s = %s\n", wave->getName(32).toRawUTF8(), wave->getText(wave->getValue(), 32).toRawUTF8(),
                                bpm->getName(32).toRawUTF8(), bpm->getText(bpm->getValue(), 32).toRawUTF8());
                }
            }
        }

        // the host's transport and tempo drive Felucca's while "Tempo follows the host" is on;
        // the host's SysEx reaches Felucca's editor protocol
        {
            struct Head : juce::AudioPlayHead {
                bool playing = false;
                juce::Optional<PositionInfo> getPosition() const override {
                    PositionInfo p;
                    p.setBpm(97.0);
                    p.setIsPlaying(playing);
                    return p;
                }
            } head;
            FM1Processor tp;
            tp.setPlayConfigDetails(0, 2, 44100.0, 256);
            tp.prepareToPlay(44100.0, 256);
            tp.setFirmware("felucca");
            tp.setPlayHead(&head);
            auto f = tp.felucca();
            CHECK(f != nullptr && tp.settings().hostTempo, "an instance following the host");
            if (f) {
                juce::AudioBuffer<float> b(2, 256);
                juce::MidiBuffer m;
                auto blocks = [&](int k) { for (int i = 0; i < k; ++i) { m.clear(); tp.processBlock(b, m); } };
                blocks(4);
                CHECK(!f->playing() && f->global(0) == 97, "stopped with the host, at its tempo");
                head.playing = true;
                blocks(4);
                const bool started = f->playing();
                head.playing = false;
                blocks(4);
                CHECK(started && !f->playing(), "the host's PLAY and STOP start and stop Felucca's sequencer");
                const uint8_t ping[] = {0xF0, 0x7D, 0x46, 0x4C, 25, 0xF7};
                m.clear();
                m.addEvent(juce::MidiMessage::createSysExMessage(ping + 1, 4), 0);
                tp.processBlock(b, m);
                blocks(8);
                auto replies = f->takeSysex();
                CHECK(!replies.empty() && replies.back().size() == 7 && replies.back()[4] == 25, "the host's SysEx reaches Felucca's editor protocol");
            }
            tp.setPlayHead(nullptr);
        }

        // the device's stored objects in the library, as Felucca's web editor writes a backup;
        // every instance plays the same device
        {
            auto file = juce::File::createTempFile(".json");
            FeluccaEngine a, b;
            felucca::DeviceStore sa(file), sb(file);
            sa.load(a);
            sb.load(b);
            std::vector<uint8_t> music, slot;
            a.setEngine(1, 6);
            a.object(0, music);
            CHECK(a.putObject(2, music) == 0 && a.object(2, slot) && slot == music, "a project saved into slot 1 of A");
            CHECK(sa.tick(a).isEmpty() && file.existsAsFile(), "A's change is saved to the library");
            felucca::Objects o;
            juce::String err;
            CHECK(felucca::readBackup(file, o, err) && o[2] == music && !o[0].empty() && !o[1].empty(),
                  "as a complete Felucca backup (the music, settings and the slot)");
            const auto t0 = file.getLastModificationTime();
            juce::Thread::sleep(20);
            sa.tick(a);
            CHECK(file.getLastModificationTime() == t0, "nothing changed: nothing written");
            sb.tick(b);
            CHECK(b.object(2, slot) && slot == music, "B takes what A saved");
            // both change a different slot before either saves: neither change is lost
            b.putObject(3, music);
            a.putObject(4, music);
            juce::Thread::sleep(20);
            sa.tick(a);
            juce::Thread::sleep(20);
            sb.tick(b);
            juce::Thread::sleep(20);
            sa.tick(a);
            std::vector<uint8_t> a3, b4;
            CHECK(a.object(3, a3) && a3 == music && b.object(4, b4) && b4 == music
                  && felucca::readBackup(file, o, err) && o[3] == music && o[4] == music,
                  "two instances' changes to different slots both survive");
            {   // a file it cannot read is reported and never written over
                auto bad = juce::File::createTempFile(".json");
                bad.replaceWithText("{\"format\": \"felucca-backup\", \"version\": 2, \"objects\": []}");
                FeluccaEngine d;
                felucca::DeviceStore sd(bad);
                const auto msg = sd.load(d);
                std::vector<uint8_t> m0;
                d.object(0, m0);
                d.putObject(5, m0);   // a change on this device
                sd.tick(d);
                CHECK(msg.isNotEmpty() && bad.loadFileAsString().contains("\"version\": 2"), "an unreadable device file is reported and left alone");
                bad.deleteFile();
            }
            FeluccaEngine c;
            felucca::DeviceStore sc(file);
            sc.load(c);
            std::vector<uint8_t> c2;
            CHECK(c.object(2, c2) && c2 == music, "a new instance loads the device");
            // (a copy for checking with Felucca's web editor, tests/felucca_backup_check.mjs)
            if (auto keep = juce::SystemStats::getEnvironmentVariable("FM1_FELUCCA_BACKUP_OUT", {}); keep.isNotEmpty())
                file.copyFileTo(juce::File(keep));
            file.deleteFile();
        }

        // the release a project was made for is saved; a different one is said on load
        {
            FM1Processor made;
            made.setFirmware("felucca");
            CHECK(made.felucca() && "v" + std::string(fm1::currentVersion("felucca").label) == made.felucca()->version(),
                  "the Felucca built in is the release the plugin says it plays");
            juce::MemoryBlock mb;
            made.getStateInformation(mb);
            auto tree = juce::ValueTree::readFromData(mb.getData(), mb.getSize());
            CHECK(tree.getProperty("firmwareVersion").toString() == "1.0.3" && tree.getChildWithName("Felucca").getProperty("version").toString() == "v1.0.3",
                  "a project says the release it was made for");
            tree.setProperty("firmwareVersion", "0.9", nullptr);
            juce::MemoryOutputStream os;
            tree.writeToStream(os);
            FM1Processor later;
            juce::String said;
            later.onStatus = [&said](const juce::String& t) { said = t; };
            later.setStateInformation(os.getData(), int(os.getDataSize()));
            CHECK(said.contains("made for Felucca 0.9") && said.contains("plays 1.0"), "loading one made for another release says so");
            later.onStatus = nullptr;
            FM1Processor hostLoaded;   // a host loads the project before any editor is open
            hostLoaded.setStateInformation(os.getData(), int(os.getDataSize()));
            CHECK(hostLoaded.takePendingStatus().contains("made for Felucca 0.9"), "said with no editor open: kept for the editor");
        }

        // syncing two Felucca devices through the editor protocol, as with an FM-1 running Felucca
        // (the plugin's own Felucca as the "synth": the same code talks to a real one)
        {
            auto a = std::make_shared<FeluccaEngine>(), b = std::make_shared<FeluccaEngine>();
            felucca::VirtualEndpoint ea(a), eb(b);
            std::vector<float> l(256), r(256);
            auto run = [&](int blocks) { for (int k = 0; k < blocks; ++k) { a->render(l.data(), r.data(), 256); b->render(l.data(), r.data(), 256); } };
            a->setEngine(1, 6);
            a->setParam(1, 0, 61);
            std::vector<uint8_t> music, slot;
            a->object(0, music);
            a->putObject(3, music);   // project slot 2
            juce::String err;
            int steps = 0;
            auto all = felucca::backup(ea, [&](int, int, const juce::String&) { ++steps; return true; }, err);
            CHECK(all && (*all)[3] == music && (*all)[0].size() == 3584 && steps > 10, "a full backup through the protocol, in pieces");
            CHECK(all && felucca::restore(eb, *all, {}, err) && b->engineOf(1) == 6 && b->param(1, 0) == 61
                  && b->object(3, slot) && slot == music, "restored into the other: the music and the project slot");
            if (!err.isEmpty()) std::printf("  sync: %s\n", err.toRawUTF8());
            {   // Send and Pull with the plugin's own Felucca while something reads its music (the
                // device file's save does once a second, a host saving the project does): through
                // the editor protocol a backup was refused partway (the staging memory reused);
                // read and written directly it is not
                auto e = std::make_shared<FeluccaEngine>();
                e->putObject(3, music);
                std::atomic<bool> stop{false};
                std::thread reader([&] { std::vector<uint8_t> b; while (!stop) e->object(0, b); });
                felucca::VirtualEndpoint ve(e);
                int refused = 0, direct = 0;
                for (int i = 0; i < 20; ++i) {
                    juce::String perr;
                    if (!felucca::backup(ve, {}, perr)) ++refused;
                    auto o = felucca::objectsOf(*e);
                    if (o.size() == 10 && o[3] == music && o[0].size() == 3584) ++direct;   // (0..9: 1.0.3)
                }
                auto o = felucca::objectsOf(*e);
                auto f2 = std::make_shared<FeluccaEngine>();
                juce::String perr;
                const bool put = felucca::putObjects(*f2, o, perr);
                stop = true;
                reader.join();
                std::vector<uint8_t> got;
                std::printf("  with the music read meanwhile: %d of 20 protocol backups refused, %d of 20 direct reads whole\n", refused, direct);
                CHECK(direct == 20 && put && f2->object(3, got) && got == music,
                      "the plugin's Felucca read and written directly, while its music is read meanwhile (" + perr + ")");
            }
            {   // a lossy link (as a real FM-1 drops MIDI when its queue is full): a piece arrives
                // cut short once, and another's answer is lost once; the object starts over. And
                // the music's last piece loses one USB-MIDI packet (3 bytes) once: Felucca takes it
                // short, and the commit fails validation; the object starts over then too
                struct Lossy : felucca::Endpoint {
                    const felucca::Dialect& dialect() const override { return felucca::feluccaDialect(); }
                    felucca::Endpoint& e;
                    int pieces = 0;
                    bool lastCut = false;
                    explicit Lossy(felucca::Endpoint& x) : e(x) {}
                    std::optional<fm1::Bytes> ask(const fm1::Bytes& q, int t) override {
                        auto a = felucca::argsOf(q);
                        if (felucca::commandOf(q) == felucca::kBackupPut && a.size() > 2 && a[0] == 1) {
                            ++pieces;
                            if (pieces == 3) { auto cut = q; cut.erase(cut.end() - 4, cut.end() - 1); return e.ask(cut, t); }
                            if (pieces == 7) { e.ask(q, t); return std::nullopt; }
                            const int off = a.size() > 6 ? int(a[2] | a[3] << 7 | a[4] << 14) : -1;
                            if (a[1] == 0 && off == 3584 - 128 && !lastCut) {
                                lastCut = true;
                                auto cut = q; cut.erase(cut.end() - 4, cut.end() - 1); return e.ask(cut, t);
                            }
                        }
                        return e.ask(q, t);
                    }
                    std::vector<fm1::Bytes> pushes() override { return e.pushes(); }
                };
                auto c = std::make_shared<FeluccaEngine>();
                felucca::VirtualEndpoint ec(c);
                Lossy lossy(ec);
                std::vector<uint8_t> got;
                juce::String lossErr;
                CHECK(all && felucca::restore(lossy, *all, {}, lossErr) && c->object(3, got) && got == music && c->engineOf(1) == 6,
                      "a piece cut short and an answer lost: restored all the same (" + lossErr + ")");
                CHECK(lossy.lastCut, "and the music's last piece was cut short (" + juce::String((*all)[0].size()) + " bytes)");
            }

            {   // INFO's live-sync capabilities (53 01 caps, Felucca 1.0.2 on): the one built in has both
                auto e = std::make_shared<FeluccaEngine>();
                auto info = e->ask(felucca::frame(felucca::kInfo));
                CHECK(info && felucca::liveCaps(felucca::argsOf(*info)) == 3, "the Felucca built in (1.0.3) has both live-sync capabilities");
                // as 1.0.2 sends it (EDITOR_PROTOCOL.md): names with an S (0x53) in them, then the blocks
                std::vector<uint8_t> a;
                for (char c : std::string("FELUCCA v1.0.2")) a.push_back(uint8_t(c));
                a.push_back(0);
                a.insert(a.end(), {2, 91, 27, 64, 83});
                for (const char* n : {"SAMPLE", "SLICE"}) { for (const char* c = n; *c; ++c) a.push_back(uint8_t(*c)); a.push_back(0); }
                a.insert(a.end(), {4, 16, 0x55, 1, 0, 0x4D, 1, 64, 1, 0x42, 1, 3, 0x46, 1, 8, 0x1B, 0x53, 1, 3});
                CHECK(felucca::liveCaps(a) == 3, "1.0.2's INFO: WATCH keeps pending pushes, no RELOAD echo");
                auto unknown = a;
                unknown.insert(unknown.end() - 3, {0x7E, 1, 9});   // a block it does not know, before 53
                CHECK(felucca::liveCaps(unknown) == 0, "a block it does not know ends the reading");
            }
            {   // one part's sound copied (the Sound page's from/to FM-1 buttons): only that part
                auto x = std::make_shared<FeluccaEngine>(), y = std::make_shared<FeluccaEngine>();
                felucca::VirtualEndpoint ex(x), ey(y);
                x->setEngine(2, 6);
                x->setParam(2, 9, 77);
                x->setParam(3, 9, 33);
                const int before = y->param(3, 9);
                juce::String cerr;
                felucca::Loaded loaded;
                const bool ok = felucca::copySound(ex, ey, 2, cerr, &loaded);
                CHECK(ok && loaded.did && y->engineOf(2) == 6 && y->param(2, 9) == 77 && y->param(3, 9) == before,
                      "a part's sound copied to the other side, its engine loaded there; another part left alone (" + cerr + ")");
            }
            felucca::Mirror mirror(ea, eb);
            CHECK(mirror.start(err), "live: both watched");
            auto settle = [&](int rounds) { for (int i = 0; i < rounds; ++i) { run(4); if (!mirror.tick(err)) break; } };
            a->setParam(0, 9, 99);       // part 1 (selected) LFO rate, as a knob on the synth
            a->setGlobal(1, 37);          // swing
            a->setParam(2, 39, -20);      // part 3's pan: another track's mix
            settle(10);
            CHECK(b->param(0, 9) == 99 && b->global(1) == 37 && b->param(2, 39) == -20, "a value, a global and another part's mix reach the other side");
            b->setParam(0, 9, 12);        // and the other way
            settle(10);
            CHECK(a->param(0, 9) == 12, "and back");
            a->setEngine(0, 7);           // a load on the synth: WHEEL on part 1
            a->setParam(0, 1, 77);
            settle(20);
            CHECK(b->engineOf(0) == 7 && b->param(0, 1) == 77, "an engine change on one side loads it on the other");
            const int presetA = a->presetOf(0), presetB = b->presetOf(0);
            settle(20);   // nothing left to carry: the load's own echo is dropped, no ping-pong
            CHECK(a->engineOf(0) == 7 && a->presetOf(0) == presetA && b->presetOf(0) == presetB && a->param(0, 1) == 77,
                  "no load bounces back");
            // 4 s of audio (Felucca's clock) with no request: watching lapsed; the mirror starts it again
            for (int k = 0; k < 700; ++k) { a->render(l.data(), r.data(), 256); b->render(l.data(), r.data(), 256); }
            juce::Thread::sleep(2100);
            settle(2);
            a->setParam(0, 9, 55);
            settle(10);
            CHECK(b->param(0, 9) == 55, "after a lapse in watching, changes are carried again");
            CHECK(err.isEmpty(), "no side stopped answering");
            if (!err.isEmpty()) std::printf("  mirror: %s\n", err.toRawUTF8());
            mirror.stop();
        }

        // with every copy taken by other instances, a Felucca project still opens and plays its sound
        {
            std::vector<std::unique_ptr<FeluccaEngine>> taken;
            while (FeluccaEngine::copiesInUse() < FeluccaEngine::copies()) taken.push_back(std::make_unique<FeluccaEngine>());
            FM1Processor more;
            more.setStateInformation(project.getData(), int(project.getSize()));
            CHECK(more.felucca() != nullptr && more.emulates() && more.felucca()->engineOf(1) == 6
                  && more.felucca()->param(1, more.felucca()->firstEngineParam() + 2) == felParamValue,
                  "more Felucca instances than copies: it still plays its own sound (no limit)");
        }
        FM1Processor q;
        q.setStateInformation(project.getData(), int(project.getSize()));
        CHECK(q.firmwareId() == "felucca" && q.felucca() != nullptr, "a Felucca project opens set to Felucca");
        // switching away and back keeps the sound
        q.setFirmware("baudgirl_fm1va");
        q.setFirmware("felucca");
        CHECK(q.felucca() != nullptr && q.felucca()->engineOf(1) == 6, "switching away and back keeps Felucca's sound");
        // the project keeps Felucca's own format (a FUN8 project); a damaged one is refused whole
        {
            auto tree = juce::ValueTree::readFromData(project.getData(), project.getSize()).getChildWithName("Felucca").createCopy();
            juce::MemoryOutputStream music;
            CHECK(juce::Base64::convertFromBase64(music, tree.getProperty("music").toString()) && music.getDataSize() == 3584,
                  "the project holds Felucca's music as Felucca saves a project");
            if (music.getDataSize() == 3584) {
                auto* bytes = static_cast<uint8_t*>(const_cast<void*>(music.getData()));
                bytes[200] ^= 0x5A;   // its hash no longer matches
                tree.setProperty("music", juce::Base64::toBase64(bytes, music.getDataSize()), nullptr);
                FM1Processor damaged;
                juce::ValueTree st("FM1Companion");
                st.setProperty("firmware", "felucca", nullptr);
                st.addChild(tree, -1, nullptr);
                juce::MemoryOutputStream os;
                st.writeToStream(os);
                damaged.setStateInformation(os.getData(), int(os.getDataSize()));
                CHECK(damaged.felucca() != nullptr && damaged.felucca()->engineOf(1) != 6,
                      "a damaged Felucca project is refused: the power-on sound stays");
                const juce::String unread = tree.getProperty("music").toString();
                juce::MemoryBlock again;
                damaged.getStateInformation(again);
                auto saved = juce::ValueTree::readFromData(again.getData(), again.getSize()).getChildWithName("Felucca");
                CHECK(saved.getProperty("music").toString() == unread, "music Felucca could not read is saved again as it came");
                if (damaged.felucca()) damaged.felucca()->setEngine(2, 7);   // something changed meanwhile
                damaged.getStateInformation(again);
                saved = juce::ValueTree::readFromData(again.getData(), again.getSize()).getChildWithName("Felucca");
                CHECK(saved.getProperty("music").toString() != unread && saved.getChildWithName("Unread").getProperty("music").toString() == unread,
                      "after a change, the new music is saved and the unread one beside it");
                {   // and carried on: loaded again and saved again, and through switching firmware away and back
                    juce::ValueTree st2("FM1Companion");
                    st2.setProperty("firmware", "felucca", nullptr);
                    st2.addChild(saved.createCopy(), -1, nullptr);
                    juce::MemoryOutputStream os2;
                    st2.writeToStream(os2);
                    FM1Processor reopened;
                    reopened.setStateInformation(os2.getData(), int(os2.getDataSize()));
                    reopened.setFirmware("baudgirl_fm1va");
                    reopened.setFirmware("felucca");
                    juce::MemoryBlock third;
                    reopened.getStateInformation(third);
                    auto s3 = juce::ValueTree::readFromData(third.getData(), third.getSize()).getChildWithName("Felucca");
                    CHECK(s3.getChildWithName("Unread").getProperty("music").toString() == unread,
                          "the unread music is carried on through a reload, a save and a firmware switch");
                }
            }
        }
        if (q.felucca() != nullptr) {
            CHECK(q.felucca()->engineOf(1) == 6, "with each part's engine");
            CHECK(q.felucca()->param(1, q.felucca()->firstEngineParam() + 2) == felParamValue, "its parameters");
            CHECK(q.felucca()->global(1) == felGlobalValue, "and the globals");
            juce::AudioBuffer<float> qb(2, 256);
            juce::MidiBuffer qm;
            q.prepareToPlay(44100.0, 256);
            q.processBlock(qb, qm);   // (where Felucca's main loop would load SLOT's factory patch)
            CHECK(q.felucca()->fm6Patch(2) == felPatch, "and an FM6 part's own patch");
        }
    }
   #endif
    dir.deleteRecursively();
    return 0;
}

// What a loaded state should show, one "key value" per line.
static juce::StringArray describe(FM1Processor& p) {
    juce::StringArray d;
    d.add("current " + juce::String(p.bank.currentSlot()));
    d.add("editName " + p.editName());
    d.add("edited " + juce::String(p.isEdited() ? 1 : 0));
    for (int i = 0; i < BankModel::kSlots; ++i) {
        const auto& s = p.bank.slot(i).sound;
        juce::MemoryBlock v(s.voice.data(), s.voice.size()), r(s.record.data(), s.record.size());
        d.add("slot " + juce::String(i) + " " + v.toBase64Encoding() + " " + r.toBase64Encoding());
        if (const auto& dev = p.bank.slot(i).onDevice) {
            juce::MemoryBlock dv(dev->voice.data(), dev->voice.size()), dr(dev->record.data(), dev->record.size());
            d.add("onDevice " + juce::String(i) + " " + dv.toBase64Encoding() + " " + dr.toBase64Encoding());
        }
    }
    for (auto* prm : p.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(prm))
            d.add("param " + r->getParameterID() + " " + juce::String(std::lrint(r->convertFrom0to1(r->getValue()))));
    {
        const juce::SpinLock::ScopedLockType l(p.sequencer.lock);
        for (int i = 0; i < Sequencer::kPatterns; ++i) {
            const auto& pt = p.sequencer.patterns[size_t(i)];
            juce::String line = "pattern " + juce::String(i) + " len " + juce::String(pt.length) + " rate " + juce::String(pt.rate)
                + " tempo " + juce::String(pt.tempo) + " gate " + juce::String(pt.gate) + " swing " + juce::String(pt.swing)
                + " transpose " + juce::String(pt.transpose) + " chain " + juce::String(p.sequencer.chain[size_t(i)]) + " :";
            for (int st = 0; st < fm1::seq::kSteps; ++st) {
                const auto& step = pt.steps[size_t(st)];
                line << " " << step.rate << "/" << step.ratchet << "/" << step.gate << "/" << step.chance << "/" << step.transpose
                     << (step.accent ? "a" : "");
                for (const auto& n : step.notes) line << "," << n.note << "." << n.vel << (n.len > 0 ? "h" + std::to_string(n.len) : "");
            }
            if (fm1::seq::hasLocks(pt)) line << " locks " << juce::String::toHexString(pt.locks.data(), int(pt.locks.size()), 0);
            d.add(line);
        }
    }
    d.add("sequencer enabled " + juce::String(p.sequencer.enabled.load() ? 1 : 0) + " sync " + juce::String(p.sequencer.syncToHost.load() ? 1 : 0)
          + " selected " + juce::String(p.sequencer.selected.load()) + " overdub " + juce::String(p.sequencer.overdub.load() ? 1 : 0));
    d.add("arp enabled " + juce::String(p.arp.enabled.load() ? 1 : 0) + " mode " + juce::String(p.arp.mode.load()) + " octaves " + juce::String(p.arp.octaves.load())
          + " rate " + juce::String(p.arp.rate.load()) + " tempo " + juce::String(p.arp.tempo.load()) + " gate " + juce::String(p.arp.gate.load())
          + " swing " + juce::String(p.arp.swing.load()) + " latch " + juce::String(p.arp.latch.load() ? 1 : 0) + " sync " + juce::String(p.arp.syncToHost.load() ? 1 : 0));
    const auto& s = p.settings();
    d.add("settings bend " + juce::String(s.bendUp) + "/" + juce::String(s.bendDown) + " channel " + juce::String(s.midiChannel)
          + " fixedVelocity " + juce::String(s.fixedVelocity ? 1 : 0) + " velocity " + juce::String(s.velocity)
          + " hardware " + juce::String(s.hardwareCharacter ? 1 : 0) + " volume " + juce::String(s.fm1VolumeDb));
    d.add("fxChannel " + juce::String(p.channels.fx));
    return d;
}

static int stateWrite(const juce::File& golden, const juce::File& dir) {
    auto sounds = goldenSounds(golden);
    CHECK(sounds.size() >= 4, "golden.json has presets");
    auto data = freshDataDir();
    {
        FM1Processor p;
        for (size_t i = 0; i < sounds.size(); ++i) { auto s = sounds[i]; s.slot = int(i); p.bank.setSound(int(i), s, false); }
        p.selectSlot(3);
        // an unsaved edit: the algorithm, and the name
        if (auto* alg = p.apvts.getParameter(Params::vcedId(134))) alg->setValueNotifyingHost(alg->convertTo0to1(7.0f));
        p.setCurrentName("EDITED");
        {
            const juce::SpinLock::ScopedLockType l(p.sequencer.lock);
            auto& pt = p.sequencer.patterns[2];
            pt.length = 12; pt.rate = 5; pt.tempo = 97; pt.gate = 63; pt.swing = 58; pt.transpose = -3;
            pt.steps[0].notes = {{60, 100, 0}, {64, 90, 2}};
            pt.steps[3].notes = {{67, 127, 0}};
            pt.steps[3].ratchet = 3; pt.steps[3].chance = 40; pt.steps[3].accent = true;
            pt.steps[5].gate = 80; pt.steps[5].transpose = 5;
            pt.steps[5].notes = {{72, 64, 1}};
            pt.repeats = 4;
            p.sequencer.chain[2] = 4;
            p.sequencer.patterns[9].steps[40].notes = {{50, 33, 0}};   // beyond the pattern's length
            pt.locks.assign(512, 0xFF);                                         // FM-1_096's parameter locks
            pt.locks[8 * 3] = 40; pt.locks[8 * 3 + 1] = 77; pt.locks[8 * 63 + 6] = 58; pt.locks[8 * 63 + 7] = 100;
        }
        p.sequencer.enabled = true; p.sequencer.syncToHost = false; p.sequencer.selected = 2; p.sequencer.overdub = true;
        p.arp.enabled = true; p.arp.mode = 2; p.arp.octaves = 3; p.arp.rate = 4; p.arp.tempo = 133; p.arp.gate = 71;
        p.arp.swing = 62; p.arp.latch = true; p.arp.syncToHost = false;
        { auto dev = sounds[5]; dev.slot = 5; dev.voice[3] ^= 1; p.bank.markOnDevice(5, dev); }   // the synth holds a slightly different slot 5
        auto set = p.settings();
        set.bendUp = 7; set.bendDown = 2; set.midiChannel = 5; set.fixedVelocity = true; set.velocity = 77;
        set.hardwareCharacter = true; set.fm1VolumeDb = -12;
        p.setSettings(set);
        p.channels.fx = 5;
        juce::MemoryBlock mb;
        p.getStateInformation(mb);
        dir.createDirectory();
        dir.getChildFile("state.bin").replaceWithData(mb.getData(), mb.getSize());
        auto expect = describe(p);
        // a project without the whole bank promises only the slot it uses, not the rest of
        // the library (which lives in the library folder) or what the synth held
        auto tree = juce::ValueTree::readFromData(mb.getData(), mb.getSize());
        if (tree.getChildWithName("FM1Bank").getNumChildren() == 0) {   // only which slot is current
            const juce::String keep = "slot " + juce::String(p.bank.currentSlot()) + " ";
            juce::StringArray kept;
            for (auto& line : expect)
                if ((!line.startsWith("slot ") || line.startsWith(keep)) && !line.startsWith("onDevice ")) kept.add(line);
            expect = kept;
        }
        dir.getChildFile("expected.txt").replaceWithText(expect.joinIntoString("\n") + "\n");
        std::printf("wrote %s (%d bytes)\n", dir.getFullPathName().toRawUTF8(), int(mb.getSize()));
    }
    data.deleteRecursively();
    return g_fail ? 1 : 0;
}

static int stateCheck(const juce::File& dir) {
    juce::MemoryBlock mb;
    CHECK(dir.getChildFile("state.bin").loadFileAsData(mb), "read " + dir.getFullPathName());
    juce::StringArray want;
    want.addLines(dir.getChildFile("expected.txt").loadFileAsString());
    want.removeEmptyStrings();
    auto data = freshDataDir();
    {
        FM1Processor p;
        p.setStateInformation(mb.getData(), int(mb.getSize()));
        auto got = describe(p);
        int bad = 0;
        for (auto& line : want)
            if (!got.contains(line)) {
                if (++bad <= 10) std::printf("  %s: expected %s\n", dir.getFileName().toRawUTF8(), line.substring(0, 120).toRawUTF8());
            }
        CHECK(bad == 0, dir.getFileName() + " loads as saved (" + juce::String(bad) + " differences)");
    }
    data.deleteRecursively();
    return 0;
}

// Pictures of the editor, drawn offscreen: the library with each of its pages,
// the other tabs, and an instance set to a firmware the plugin cannot play yet.
static int snapshots(const juce::File& outDir, const juce::File& golden) {
    outDir.createDirectory();
    auto sounds = goldenSounds(golden);
    auto dir = freshDataDir();
    {
        FM1Processor p;
        for (size_t i = 0; i < sounds.size(); ++i) { auto s = sounds[i]; s.slot = int(i); p.bank.setSound(int(i), s, false); }
        p.selectSlot(1);
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        ed->setVisible(true);
        auto save = [&](const juce::String& name) {
            auto img = ed->createComponentSnapshot(ed->getLocalBounds());
            juce::FileOutputStream os(outDir.getChildFile(name + ".png"));
            os.setPosition(0); os.truncate();
            juce::PNGImageFormat().writeImageToStream(img, os);
        };
        auto* tabs = dynamic_cast<juce::TabbedComponent*>(ed->findChildWithID("tabs"));
        CHECK(tabs != nullptr, "the editor has its tabs");
        if (tabs == nullptr) return 1;
        juce::TabbedComponent* pages = nullptr;
        std::function<void(juce::Component*)> find = [&](juce::Component* c) {
            for (auto* ch : c->getChildren()) {
                if (auto* t = dynamic_cast<juce::TabbedComponent*>(ch); t != nullptr && t != tabs) pages = t;
                find(ch);
            }
        };
        find(ed.get());
        CHECK(pages != nullptr, "the library has its pages");
        for (int i = 0; pages != nullptr && i < pages->getNumTabs(); ++i) {
            tabs->setCurrentTabIndex(0);
            pages->setCurrentTabIndex(i);
            save("library-" + pages->getTabNames()[i].replaceCharacters(" &", "__"));
        }
        {   // a pattern to show: notes held over steps, an accent, a ratchet, a step's options, locks
            const juce::SpinLock::ScopedLockType l(p.sequencer.lock);
            auto& pt = p.sequencer.patterns[0];
            pt.length = 32;
            pt.steps[0].notes = {{60, 100, 3}, {64, 90, 0}};
            pt.steps[2].notes = {{67, 100, 0}}; pt.steps[2].accent = true;
            pt.steps[4].notes = {{62, 100, 0}}; pt.steps[4].ratchet = 3;
            pt.steps[6].notes = {{65, 100, 1}}; pt.steps[6].gate = 65; pt.steps[6].chance = 45;
            pt.steps[9].notes = {{71, 100, 6}};
            pt.steps[17].notes = {{72, 100, 0}};
            pt.locks.assign(512, 0xFF);
            pt.locks[0] = 1; pt.locks[1] = 5;                        // step 1: Feedback 5
            pt.locks[8 * 6] = 40; pt.locks[8 * 6 + 1] = 77;           // step 7: Delay Feedback 77
            pt.locks[8 * 6 + 2] = 33; pt.locks[8 * 6 + 3] = 90;       // and Filter Cutoff 90
            p.sequencer.selected = 0;
            ++p.patternsVersion;
        }
        Fm1SeqPage* sq = nullptr;
        {
            std::function<void(juce::Component*)> findSq = [&](juce::Component* c) {
                for (auto* ch : c->getChildren()) { if (auto* sp = dynamic_cast<Fm1SeqPage*>(ch)) sq = sp; findSq(ch); }
            };
            tabs->setCurrentTabIndex(1);   // (a tab's page is in the window only while it is shown)
            findSq(ed.get());
            if (sq != nullptr) sq->refresh();
        }
        for (int i = 1; i < tabs->getNumTabs(); ++i) { tabs->setCurrentTabIndex(i); save("tab-" + tabs->getTabNames()[i]); }
        {   // the Sequencer's lanes opened, and an 8-Bit pattern's
            tabs->setCurrentTabIndex(1);
            CHECK(sq != nullptr && sq->laneCount() == 2, "the Sequencer tab: Notes and Locks lanes");
            if (sq != nullptr) {
                sq->selectStep(6);
                sq->openLane(0);
                save("sequencer-notes-open");
                sq->openLane(1);
                save("sequencer-locks-open");
                sq->openLane(-1);
                {
                    const juce::SpinLock::ScopedLockType l(p.sequencer.lock);
                    auto& c8 = p.sequencer.patterns[1];
                    c8.length = 16;
                    for (int k = 0; k < 16; k += 2) c8.steps[size_t(k)].notes = {{17 + (k % 4 == 0 ? 0 : 2), 100, 0}};   // drums
                    for (int k = 0; k < 16; k += 4) c8.steps[size_t(k)].notes.push_back({48 + k, 100, 2});           // bass
                    c8.steps[3].notes.push_back({84, 100, 1});                                                         // lead
                    c8.steps[8].notes.push_back({31, 100, 0});                                                         // an SFX
                }
                p.sequencer.selected = 1;
                ++p.patternsVersion;
                sq->refresh();
                CHECK(sq->chipLanes() && sq->laneCount() == 5 && sq->laneName(0) == "Lead" && sq->laneName(3) == "Drums",
                      "notes on an 8-Bit preset's keys with drums: its five lanes");
                save("sequencer-8bit");
                sq->openLane(3);
                save("sequencer-8bit-drums");
                sq->openLane(-1);
                p.sequencer.selected = 0;
                sq->refresh();
            }
        }
        {   // a phone in portrait (402 x 780 points: an iPhone 17 Pro's screen less its bars)
            const auto desk = ed->getBounds();
            ed->setSize(402, 780);
            const int was = p.bank.currentSlot();
            p.selectSlot(16);   // an FM preset (the FM page)
            tabs->setCurrentTabIndex(0);   // (a tab's content is in the tree only while it shows)
            juce::Button* soundButton = nullptr;
            std::function<void(juce::Component*)> findSound = [&](juce::Component* c) {
                for (auto* ch : c->getChildren()) {
                    if (auto* b = dynamic_cast<juce::Button*>(ch); b && b->getButtonText() == "Sound") soundButton = b;
                    findSound(ch);
                }
            };
            findSound(ed.get());
            tabs->setCurrentTabIndex(0);
            save("phone-library-list");
            CHECK(soundButton != nullptr, "the narrow library has its Sound button");
            if (soundButton) { soundButton->setToggleState(true, juce::dontSendNotification); if (soundButton->onClick) soundButton->onClick(); }
            for (int i = 0; pages != nullptr && i < pages->getNumTabs(); ++i) {
                tabs->setCurrentTabIndex(0);
                pages->setCurrentTabIndex(i);
                save("phone-library-" + pages->getTabNames()[i].replaceCharacters(" &", "__"));
                if (pages->getTabNames()[i] == "FM") {   // and its Global page
                    std::function<void(juce::Component*)> press = [&](juce::Component* c) {
                        for (auto* ch : c->getChildren()) {
                            if (auto* b = dynamic_cast<juce::Button*>(ch); b && b->getButtonText() == "LFO" && b->isVisible()) {
                                b->setToggleState(true, juce::dontSendNotification);
                                if (b->onClick) b->onClick();
                            }
                            press(ch);
                        }
                    };
                    press(ed.get());
                    save("phone-library-FM-lfo");
                    for (const char* other : {"Algorithm", "Pitch EG"}) {
                        std::function<void(juce::Component*)> pressOther = [&](juce::Component* c) {
                            for (auto* ch : c->getChildren()) {
                                if (auto* b = dynamic_cast<juce::Button*>(ch); b && b->getButtonText() == other && b->isVisible()) {
                                    b->setToggleState(true, juce::dontSendNotification);
                                    if (b->onClick) b->onClick();
                                }
                                pressOther(ch);
                            }
                        };
                        pressOther(ed.get());
                        save("phone-library-FM-" + juce::String(other).replaceCharacters(" ", "_"));
                    }
                }
            }
            for (int i = 1; i < tabs->getNumTabs(); ++i) { tabs->setCurrentTabIndex(i); save("phone-tab-" + tabs->getTabNames()[i]); }
            Fm1SeqPage* seq = nullptr;
            std::function<void(juce::Component*)> findSeq = [&](juce::Component* c) {
                for (auto* ch : c->getChildren()) { if (auto* sp = dynamic_cast<Fm1SeqPage*>(ch)) seq = sp; findSeq(ch); }
            };
            tabs->setCurrentTabIndex(1);
            findSeq(ed.get());
            CHECK(seq != nullptr && seq->getHeight() > 780, "the sequencer on a phone is a page taller than the screen, to scroll");
            if (seq != nullptr) {   // the whole page, as it scrolls
                auto img = seq->createComponentSnapshot(seq->getLocalBounds());
                juce::FileOutputStream os(outDir.getChildFile("phone-sequencer-page.png"));
                os.setPosition(0); os.truncate();
                juce::PNGImageFormat().writeImageToStream(img, os);
            }
            ed->setSize(874, 365);   // the phone on its side
            tabs->setCurrentTabIndex(0);
            for (int i = 0; pages != nullptr && i < pages->getNumTabs(); ++i)
                if (pages->getTabNames()[i] == "FM") { pages->setCurrentTabIndex(i); save("phone-side-library-FM"); }
            ed->setSize(820, 1100);   // an iPad upright
            save("ipad-library-FM");
            tabs->setCurrentTabIndex(1);
            save("ipad-tab-Sequencer");
            ed->setBounds(desk);
            save("tab-Sequencer-after");
            if (seq != nullptr && seq->getParentComponent() != nullptr)
                CHECK(seq->getHeight() <= seq->getParentComponent()->getHeight(), "back at full size, the sequencer has nothing to scroll");
            tabs->setCurrentTabIndex(0);
            for (int i = 0; pages != nullptr && i < pages->getNumTabs(); ++i)   // the FM page at full size too
                if (pages->getTabNames()[i] == "FM") { pages->setCurrentTabIndex(i); save("library-FM"); }
            p.selectSlot(was);
        }
        p.setFirmware("felucca");
        save("firmware-felucca");
       #if FM1_FELUCCA
        {   // Felucca's Library tab (its user presets and projects, the Sound and Sync pages) and its Device tab
            juce::TabbedComponent* fel = nullptr;
            juce::TabbedComponent* felPages = nullptr;
            std::function<void(juce::Component*)> findTabs = [&](juce::Component* c) {
                for (auto* ch : c->getChildren()) {
                    if (auto* t = dynamic_cast<juce::TabbedComponent*>(ch)) {
                        if (t->getComponentID() == "felucca tabs") fel = t;
                        else if (t->getNumTabs() == 2 && t->getTabNames()[0] == "Sound") felPages = t;
                    }
                    findTabs(ch);
                }
            };
            findTabs(ed.get());
            CHECK(fel != nullptr && fel->getNumTabs() == 3 && fel->getTabNames()[1] == "Device" && fel->getTabNames()[2] == "Sequencer",
                  "the Felucca editor has its Library, Device and Sequencer tabs");
            CHECK(felPages != nullptr, "Felucca's library has its Sound and Sync pages");
            std::function<void(juce::Component*)> draw = [&](juce::Component* c) {
                for (auto* ch : c->getChildren()) {
                    if (auto* v = dynamic_cast<FeluccaDeviceView*>(ch)) v->refreshScreen();
                    draw(ch);
                }
            };
            if (fel != nullptr && felPages != nullptr) {
                felPages->setCurrentTabIndex(1);
                save("felucca-sync");
                felPages->setCurrentTabIndex(0);
                fel->setCurrentTabIndex(1);
                draw(ed.get());
                save("felucca-device");
                {   // the Sequencer tab: a few notes, a chord, a tie, an accent; then the drum lanes
                    FeluccaSeqPage* seqPage = nullptr;
                    std::function<void(juce::Component*)> findSeq = [&](juce::Component* c) {
                        for (auto* ch : c->getChildren()) { if (auto* s = dynamic_cast<FeluccaSeqPage*>(ch)) seqPage = s; findSeq(ch); }
                    };
                    fel->setCurrentTabIndex(2);
                    findSeq(ed.get());
                    CHECK(seqPage != nullptr, "the Sequencer tab");
                    if (seqPage) {
                        for (int i : {0, 4, 8, 12}) seqPage->toggleNote(i, 60 + (i % 8));
                        seqPage->toggleNote(2, 67); seqPage->toggleNote(2, 71); seqPage->toggleNote(2, 74);
                        seqPage->selectStep(13); seqPage->setStepTime(felucca::kTie);
                        seqPage->selectStep(8); seqPage->setStepFlag(felucca::kAccent, true);
                        seqPage->selectStep(4); seqPage->setStepChance(40);
                        seqPage->selectStep(2);
                        save("felucca-sequencer");
                        if (auto fe = p.felucca()) {   // a song chain and a motion event, then their views
                            std::vector<uint8_t> music;
                            fe->object(0, music);
                            fe->putObject(2, music);
                            fe->putObject(3, music);
                            p.feluccaEdit(felucca::chainWrite({{0, 2}, {1, 1}, {0, 4}}));
                            p.feluccaEdit(felucca::motionSet(0, 3, 9, 80));
                            p.feluccaEdit(felucca::motionSet(0, 7, 1, 20));
                            p.feluccaEdit(felucca::motionOn(0, true));
                        }
                        seqPage->showView(1);
                        save("felucca-sequencer-song");
                        seqPage->showView(2);
                        save("felucca-sequencer-motion");
                        seqPage->showView(0);
                        const int was = ed->getWidth(), hgt = ed->getHeight();
                        ed->setSize(402, 780);
                        save("phone-felucca-sequencer");
                        ed->setSize(was, hgt);
                    }
                    fel->setCurrentTabIndex(1);
                }
                {   // on a phone
                    const auto desk = ed->getBounds();
                    ed->setSize(402, 780);
                    draw(ed.get());
                    save("phone-felucca-device");
                    fel->setCurrentTabIndex(0);
                    save("phone-felucca-library");
                    std::function<void(juce::Component*)> pressSound = [&](juce::Component* c) {
                        for (auto* ch : c->getChildren()) {
                            if (auto* b = dynamic_cast<juce::Button*>(ch); b && b->getButtonText() == "Sound" && b->isVisible()) {
                                b->setToggleState(true, juce::dontSendNotification);
                                if (b->onClick) b->onClick();
                            }
                            pressSound(ch);
                        }
                    };
                    pressSound(ed.get());
                    save("phone-felucca");
                    ed->setBounds(desk);
                }
                fel->setCurrentTabIndex(0);
            }
        }
        {   // SLOOP: the same editor for its firmware (its Sound page, its drum track, its Device tab)
            p.setFirmware("sloop");
            save("firmware-sloop");
            juce::TabbedComponent* slp = nullptr;
            std::function<void(juce::Component*)> findTabs = [&](juce::Component* c) {
                for (auto* ch : c->getChildren()) {
                    if (auto* t = dynamic_cast<juce::TabbedComponent*>(ch); t && t->getComponentID() == "felucca tabs") slp = t;
                    findTabs(ch);
                }
            };
            findTabs(ed.get());
            CHECK(slp != nullptr, "the SLOOP editor has its tabs");
            std::function<void(juce::Component*)> press = [&](juce::Component* c) {
                for (auto* ch : c->getChildren()) {
                    if (auto* b = dynamic_cast<juce::Button*>(ch); b && b->getButtonText() == "DRUMS" && b->isVisible()) {
                        b->setToggleState(true, juce::dontSendNotification);
                        if (b->onClick) b->onClick();
                    }
                    press(ch);
                }
            };
            press(ed.get());
            save("sloop-drums");
            if (slp != nullptr) {
                slp->setCurrentTabIndex(1);
                std::function<void(juce::Component*)> draw = [&](juce::Component* c) {
                    for (auto* ch : c->getChildren()) {
                        if (auto* v = dynamic_cast<FeluccaDeviceView*>(ch)) v->refreshScreen();
                        draw(ch);
                    }
                };
                draw(ed.get());
                save("sloop-device");
                {   // its Sequencer tab: part 1's notes with levels and ratchets, then the drum track
                    FeluccaSeqPage* seqPage = nullptr;
                    std::function<void(juce::Component*)> findSeq = [&](juce::Component* c) {
                        for (auto* ch : c->getChildren()) { if (auto* s = dynamic_cast<FeluccaSeqPage*>(ch)) seqPage = s; findSeq(ch); }
                    };
                    slp->setCurrentTabIndex(2);
                    findSeq(ed.get());
                    CHECK(seqPage != nullptr, "SLOOP's Sequencer tab");
                    if (seqPage) {
                        seqPage->selectTrack(0);
                        for (int i : {0, 3, 6, 8, 10, 14}) seqPage->toggleNote(i, 60 + (i % 7));
                        seqPage->selectStep(3); seqPage->setNoteLevel(0, 1);
                        seqPage->selectStep(8); seqPage->setNoteLevel(0, 3); seqPage->setNoteRatchet(0, 2);
                        save("sloop-sequencer");
                        seqPage->selectTrack(3);
                        for (int i = 0; i < 16; i += 4) seqPage->toggleLane(i, 0, false);
                        for (int i = 2; i < 16; i += 4) seqPage->toggleLane(i, 2, false);
                        for (int i = 0; i < 16; i += 2) seqPage->toggleLane(i, 4, false);
                        save("sloop-sequencer-drums");
                        if (auto fe = p.felucca()) { fe->arrangementDo(1, 0); fe->arrangementDo(1, 2); fe->setChain({{0, 4}, {2, 2}, {0, 4}}, true); fe->arrangementDo(4, 2); }
                        seqPage->showView(1);
                        save("sloop-sequencer-song");
                        const int was = ed->getWidth(), hgt = ed->getHeight();
                        ed->setSize(402, 780);
                        save("phone-sloop-sequencer-song");
                        seqPage->showView(0);
                        save("phone-sloop-sequencer");
                        ed->setSize(was, hgt);
                    }
                }
                slp->setCurrentTabIndex(0);
            }
        }
       #endif
        p.setFirmware("fm1_stock");
        tabs->setCurrentTabIndex(0);
        save("firmware-stock");
    }
    dir.deleteRecursively();
    std::printf("pictures in %s\n", outDir.getFullPathName().toRawUTF8());
    return g_fail ? 1 : 0;
}

// The bank folder (plugin/LibraryStore.h): moving the old single-file library
// in, exact round trips, instances seeing each other's writes, and nothing lost
// when two instances write the same slot.
static int library(const juce::File& golden) {
    auto sounds = goldenSounds(golden);
    CHECK(sounds.size() >= 6, "golden.json has presets");
    auto dir = freshDataDir();
    {
        // an old library.fm1lib with presets, and what the synth held for one slot
        BankModel old;
        for (size_t i = 0; i < sounds.size(); ++i) { auto s = sounds[i]; s.slot = int(i); old.setSound(int(i), s, false); }
        { auto dev = sounds[2]; dev.slot = 2; dev.voice[7] ^= 3; old.markOnDevice(2, dev); }
        juce::MemoryOutputStream os;
        old.toState().writeToStream(os);
        auto oldFile = dir.getChildFile("library.fm1lib");
        oldFile.replaceWithData(os.getData(), os.getDataSize());
        juce::MemoryBlock oldBytes;
        oldFile.loadFileAsData(oldBytes);

        // the first instance moves it into the folder
        FM1Processor p;
        LibraryStore store;
        CHECK(store.exists(), "the bank folder is made from the old library");
        CHECK(dir.getChildFile("migrated-from.txt").existsAsFile(), "the move is noted");
        juce::MemoryBlock nowBytes;
        oldFile.loadFileAsData(nowBytes);
        CHECK(nowBytes == oldBytes, "the old library file is left as it was");
        int same = 0;
        for (int i = 0; i < BankModel::kSlots; ++i) {
            const auto& a = old.slot(i);
            const auto& b = p.bank.slot(i);
            bool dev = a.onDevice.has_value() == b.onDevice.has_value()
                       && (!a.onDevice || (a.onDevice->voice == b.onDevice->voice && a.onDevice->record == b.onDevice->record));
            if (a.sound.voice == b.sound.voice && a.sound.record == b.sound.record && dev) ++same;
        }
        CHECK(same == BankModel::kSlots, "every slot, and what the synth held, arrives exactly (" + juce::String(same) + " of 128)");
        auto f = store.slotFile(0).loadFileAsString();
        CHECK(f.contains("\"format\": \"virtual-fm1\"") || f.contains("\"format\":\"virtual-fm1\""), "a slot file is a virtual-fm1 JSON document");
        CHECK(f.contains("baudgirl.va") || f.contains("fm1.voice"), "a slot file carries its format tag");

        // exact round trips through a slot file, FM and VA alike
        int exact = 0;
        for (const auto& s0 : sounds) {
            BankModel::Slot s; s.sound = s0;
            auto back = LibraryStore::slotFromText(LibraryStore::slotText(s, 5), s0.slot);
            if (back && back->sound.voice == s0.voice && back->sound.record == s0.record) ++exact;
        }
        CHECK(exact == int(sounds.size()), "every golden preset round-trips through a slot file byte for byte");

        // a slot file this version cannot read (from a newer version) is never written over
        {
            auto f = store.slotFile(9);
            auto text = f.loadFileAsString().replace("\"version\": 1", "\"version\": 99");
            f.replaceWithText(text, false, false, "\n");
            BankModel b; LibraryStore s; s.load(b);
            CHECK(s.unreadable().contains(9), "a newer slot file is reported as unreadable");
            auto s3 = sounds[3]; s3.slot = 3; s3.voice[2] = uint8_t((s3.voice[2] + 7) % 100);
            b.setSound(3, s3, false);
            b.setSound(9, sounds[0], false);   // even a change to that very slot
            s.save(b);
            CHECK(f.loadFileAsString() == text, "and survives saves, untouched");
        }

        // a project keeps its sound when its library slot changes afterwards
        {
            juce::MemoryBlock project;
            int slot;
            std::vector<int> paramsSaved;
            fm1::Sound projectSound;
            {
                FM1Processor q;
                q.selectSlot(2);
                slot = q.bank.currentSlot();
                projectSound = q.editedSound();
                q.getStateInformation(project);
                for (auto* prm : q.getParameters()) paramsSaved.push_back(int(std::lrint(dynamic_cast<juce::RangedAudioParameter*>(prm)->convertFrom0to1(prm->getValue()))));
            }
            {   // the library's slot changes (another instance stores something else there,
                // with another effect order and a different record)
                BankModel lib; LibraryStore ls; ls.load(lib);
                auto other = sounds[7]; other.slot = slot;
                std::swap(other.record[27], other.record[30]);   // effect order: first two positions
                lib.setSound(slot, other, false); ls.save(lib);
            }
            FM1Processor r;
            juce::String status;
            r.onStatus = [&](const juce::String& s) { status = s; };
            r.setStateInformation(project.getData(), int(project.getSize()));
            std::vector<int> paramsLoaded;
            for (auto* prm : r.getParameters()) paramsLoaded.push_back(int(std::lrint(dynamic_cast<juce::RangedAudioParameter*>(prm)->convertFrom0to1(prm->getValue()))));
            CHECK(paramsLoaded == paramsSaved, "the project's sound is restored though its library slot changed");
            auto playing = r.editedSound();
            CHECK(playing.voice == projectSound.voice && playing.record == projectSound.record,
                  "including what the parameters do not hold: the effect order and the rest of the record");
            CHECK(r.isEdited(), "the difference from the library shows as unsaved changes");
            CHECK(status.contains("changed in the library"), "and the user is told");
            CHECK(r.bank.slot(slot).sound.voice == sounds[7].voice, "the library's slot is left as the library has it");
        }

        // two instances: one writes, the other sees it
        BankModel a, b;
        LibraryStore sa, sb;
        sa.load(a); sb.load(b);
        CHECK(!sb.changedElsewhere(), "nothing changed yet");
        auto s3 = sounds[3]; s3.slot = 3; s3.voice[0] = uint8_t((s3.voice[0] + 1) % 100);
        a.setSound(3, s3, false);
        CHECK(sa.save(a) == 1, "only the changed slot is written");
        CHECK(sb.changedElsewhere(), "the other instance notices the write");
        sb.load(b);
        CHECK(b.slot(3).sound.voice == s3.voice, "and reads the new slot");
        CHECK(!sb.changedElsewhere(), "and is up to date after reading");

        // both write the same slot: the second write wins, the first is kept in .trash
        BankModel c; LibraryStore sc; sc.load(c);
        auto s4a = sounds[4]; s4a.slot = 4; s4a.voice[1] = 11;
        auto s4b = sounds[4]; s4b.slot = 4; s4b.voice[1] = 22;
        a.setSound(4, s4a, false); sa.save(a);
        c.setSound(4, s4b, false); sc.save(c);
        auto onDisk = LibraryStore::slotFromText(sc.slotFile(4).loadFileAsString(), 4);
        CHECK(onDisk && onDisk->sound.voice[1] == 22, "the later write is on disk");
        auto trashed = dir.getChildFile(".trash").findChildFiles(juce::File::findFiles, false, "005 replaced*.json");
        CHECK(trashed.size() == 1, "the version it replaced is in .trash");
        if (trashed.size() == 1) {
            auto t = LibraryStore::slotFromText(trashed[0].loadFileAsString(), 4);
            CHECK(t && t->sound.voice[1] == 11, "with the other instance's change in it");
        }
    }
    dir.deleteRecursively();
    return 0;
}

#if FM1_FELUCCA
// Felucca's library tab: Save, Rename and Load by its buttons, read back through Felucca's own
// editor protocol (a sound preset, and a DRUM one whose pattern is a grid).
static int feluccaLibrary() {
    auto dir = freshDataDir();
    {
        FM1Processor p;
        p.setFirmware("felucca");
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        ed->setVisible(true);
        auto f = p.felucca();
        CHECK(f != nullptr, "an instance set to Felucca plays it");
        if (!f) return 1;
        FeluccaLibraryList* list = nullptr;
        std::function<void(juce::Component*)> find = [&](juce::Component* c) {
            for (auto* ch : c->getChildren()) { if (auto* l = dynamic_cast<FeluccaLibraryList*>(ch)) list = l; find(ch); }
        };
        find(ed.get());
        CHECK(list != nullptr, "the Felucca editor has its library list");
        if (!list) return 1;
        juce::TextEditor* name = nullptr;
        juce::ListBox* box = nullptr;
        for (auto* ch : list->getChildren()) {
            if (auto* t = dynamic_cast<juce::TextEditor*>(ch)) name = t;
            if (auto* b = dynamic_cast<juce::ListBox*>(ch)) box = b;
        }
        auto press = [&](const juce::String& prefix) {
            for (auto* ch : list->getChildren())
                if (auto* b = dynamic_cast<juce::Button*>(ch); b && b->getButtonText().startsWith(prefix)) { if (b->onClick) b->onClick(); return true; }
            return false;
        };
        // a user preset's slot, engine and name, as Felucca lists it (UP_LIST)
        auto listed = [&](int slot, int& engine, juce::String& n) {
            auto r = f->ask(felucca::frame(felucca::kUpList, {uint8_t(slot), 1}));
            if (!r) return false;
            auto a = felucca::argsOf(*r);
            if (a.size() < 6 || a[3] == 0) return false;
            engine = a[4];
            n.clear();
            for (size_t k = 5; k < a.size() && a[k]; ++k) n += juce::String::charToString(juce::juce_wchar(a[k]));
            return true;
        };
        if (!name || !box) { CHECK(false, "the library list has its name box and list"); return 1; }
        const int e0 = f->engineOf(0);
        f->select(0);
        box->selectRow(0);
        name->setText("my bass");
        CHECK(press("Save PART"), "the library has Save");
        int engine = -1;
        juce::String n;
        CHECK(listed(0, engine, n) && n == "MY BASS" && engine == e0, "Save stored PART 1's sound as U01, named (upper case): " + n);
        name->setText("renamed");
        CHECK(press("Rename"), "the library has Rename");
        CHECK(listed(0, engine, n) && n == "RENAMED" && engine == e0, "Rename renamed U01 and kept its sound: " + n);
        int other = -1;
        for (int e : f->enginesShown()) if (e != e0) { other = e; break; }
        f->setEngine(0, other);
        CHECK(f->engineOf(0) == other, "PART 1 changed to another engine");
        box->selectRow(0);
        CHECK(press("Load to PART"), "the library has Load");
        CHECK(f->engineOf(0) == e0, "Load put U01's sound back into PART 1");

        f->select(3);   // PART 4: DRUM, whose pattern is a grid: lane 0 on step 1, lane 7 (a high bit) on step 2
        const int drum = f->engineOf(3);
        // TRACK_STEP: track, index, n, 4 notes, time, flags, vel, hit, accent, high bits
        f->ask(felucca::frame(felucca::kTrackStep, {3, 0, 0, 0, 0, 0, 0, 0, 0, 100, 1, 0, 0}));
        f->ask(felucca::frame(felucca::kTrackStep, {3, 1, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 1}));
        box->selectRow(1);
        name->setText("kit");
        press("Save PART");
        CHECK(listed(1, engine, n) && n == "KIT" && engine == drum, "PART 4's DRUM sound stored as U02: " + n);
        auto kindOf = [&](int slot) {   // UP_GET: slot, used, engine, name 0, values, pattern, kind [, 16 hi]
            auto r = f->ask(felucca::frame(felucca::kUpGet, {uint8_t(slot)}));
            if (!r) return -1;
            auto a = felucca::argsOf(*r);
            size_t k = 3;
            while (k < a.size() && a[k]) ++k;
            k += 1 + size_t(2 * f->paramCount() + 32);
            return k < a.size() ? int(a[k]) : -1;
        };
        CHECK(kindOf(1) == 1, "U02's pattern is a drum grid (kind " + juce::String(kindOf(1)) + ")");
        auto afterName = [&](int slot) {   // UP_GET's reply after the name: the values and the pattern
            auto r = f->ask(felucca::frame(felucca::kUpGet, {uint8_t(slot)}));
            if (!r) return std::vector<uint8_t>{};
            auto a = felucca::argsOf(*r);
            size_t k = 3;
            while (k < a.size() && a[k]) ++k;
            return std::vector<uint8_t>(a.begin() + std::ptrdiff_t(std::min(a.size(), k + 1)), a.end());
        };
        const auto before = afterName(1);
        name->setText("kit two");
        press("Rename");
        CHECK(listed(1, engine, n) && n == "KIT TWO" && engine == drum, "a DRUM user preset renames too: " + n);
        CHECK(!before.empty() && afterName(1) == before, "renaming kept its values and its grid, high bits too");
        f->select(0);
    }
    dir.deleteRecursively();
    return 0;
}
#endif

// Moving a real library: a copy of <old.fm1lib> in a scratch folder (the file
// given is only read), then every slot compared with the original.
static int migrate(const juce::File& source) {
    auto dir = freshDataDir();
    {
        CHECK(source.copyFileTo(dir.getChildFile("library.fm1lib")), "copied " + source.getFullPathName());
        juce::MemoryBlock mb;
        source.loadFileAsData(mb);
        BankModel orig;
        orig.fromState(juce::ValueTree::readFromData(mb.getData(), mb.getSize()));
        FM1Processor p;
        int same = 0, va = 0, dev = 0;
        for (int i = 0; i < BankModel::kSlots; ++i) {
            const auto& a = orig.slot(i);
            const auto& b = p.bank.slot(i);
            bool d = a.onDevice.has_value() == b.onDevice.has_value()
                     && (!a.onDevice || (a.onDevice->voice == b.onDevice->voice && a.onDevice->record == b.onDevice->record));
            if (a.sound.voice == b.sound.voice && a.sound.record == b.sound.record && d) ++same;
            if (fm1::engineOf(a.sound.record) == fm1::Engine::VA) ++va;
            if (a.onDevice) ++dev;
        }
        std::printf("  %d of 128 slots identical after the move (%d VA presets, %d with the synth's copy)\n", same, va, dev);
        CHECK(same == BankModel::kSlots, "the whole library arrives exactly");
        std::printf("  folder: %s\n", LibraryStore().bankDir().getFullPathName().toRawUTF8());
    }
    dir.deleteRecursively();
    return 0;
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    setEnv("FM1_NO_DEVICE", "1");
    juce::String cmd = argc > 1 ? argv[1] : "";
    int rc = 2;
    if (cmd == "render" && argc == 4) rc = render(juce::File(argv[2]), juce::File(argv[3]));
    else if (cmd == "checks") rc = checks();
    else if (cmd == "library" && argc == 3) rc = library(juce::File(argv[2]));
    else if (cmd == "migrate" && argc == 3) rc = migrate(juce::File(argv[2]));
   #if FM1_FELUCCA
    else if (cmd == "felucca-library") rc = feluccaLibrary();
   #endif
    else if (cmd == "snapshot" && argc == 4) rc = snapshots(juce::File(argv[3]), juce::File(argv[2]));
    else if (cmd == "state-write" && argc == 4) rc = stateWrite(juce::File(argv[2]), juce::File(argv[3]));
    else if (cmd == "state-check" && argc >= 3) { for (int i = 2; i < argc; ++i) stateCheck(juce::File(argv[i])); rc = 0; }
    else { std::printf("usage: plugin_test render <golden.json> <out.txt> | checks | state-write <golden.json> <dir> | state-check <dir>...\n"); return 2; }
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return rc != 0 ? rc : (g_fail ? 1 : 0);
}
