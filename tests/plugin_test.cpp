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
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "Firmwares.h"
#if FM1_FELUCCA
 #include "FeluccaDevice.h"
 #include "FeluccaPanel.h"
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
            for (int i = 0; i < 8; ++i) pt.steps[size_t(i)].notes = {{48 + 5 * i, 70 + 7 * i, false}};
            pt.steps[2].ratchet = 3; pt.steps[5].slide = true; pt.steps[6].notes.clear();
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
        CHECK(fm1::firmwareChoice("baudgirl_fm1va").label() == "FM-1+VA (baud girl) 0.94" && fm1::firmwareChoice("felucca").label() == "Felucca 1.0"
              && fm1::firmwareChoice("fm1_stock").label() == "M-VAVE (stock) V15", "the firmware list names each with its current release");
        CHECK(at(94).known && at(94).support == Support::Current && at(94).text == "FM-1_094", "FM-1_094: current, nothing to say");
        CHECK(at(93).support == Support::Tested && at(15).support == Support::Current && at(910).support == Support::Current, "093, V15, Felucca 1.0 known");
        CHECK(at(92).support == Support::Older && at(92).text.find("GLOBE") != std::string::npos, "an older release says what it lacks");
        auto n = at(95);
        CHECK(n.newer && !n.known && n.support == Support::Current && n.text.find("newer") != std::string::npos && n.text.find("0.94") != std::string::npos,
              "an unknown newer release: synced as the newest known, said to be untested, never refused");
        CHECK(at(911).newer && at(911).firmwareId == "felucca" && at(911).text.find("1.0") != std::string::npos, "Felucca 1.1 too");
        CHECK(!at(70).known && !at(70).newer && at(70).text.find("older") != std::string::npos, "an unknown older release says so");
        CHECK(at(88).text.find("not a release the plugin knows") != std::string::npos, "one between known releases is not called older");
        CHECK(n.text.find("GLOBE settings not read") != std::string::npos, "a newer FM-1+VA says what newest-known support it lacks");
        CHECK(at(14).firmwareId == "fm1_stock" && !at(14).known && !at(14).newer, "stock V14: older than V15");
        CHECK(at(904).support == Support::Deprecated && at(904).text.find("1.0") != std::string::npos, "Felucca 0.4 beta: retired, says which release to update to");
        CHECK(at(900).text.find("development build") != std::string::npos, "a Felucca build that is not a release");
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
                    for (const auto& en : felparams::entries()) if (en.track >= 0) ++perPart[en.track];
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
            CHECK(tree.getProperty("firmwareVersion").toString() == "1.0" && tree.getChildWithName("Felucca").getProperty("version").toString() == "v1.0",
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
            q.processBlock(qb, qm);   // (where Felucca's main loop would load PTCH's slot)
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
                     << (step.accent ? "a" : "") << (step.slide ? "s" : "");
                for (const auto& n : step.notes) line << "," << n.note << "." << n.vel << (n.tie ? "t" : "");
            }
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
            pt.steps[0].notes = {{60, 100, false}, {64, 90, true}};
            pt.steps[3].notes = {{67, 127, false}};
            pt.steps[3].ratchet = 3; pt.steps[3].chance = 40; pt.steps[3].accent = true;
            pt.steps[5].slide = true; pt.steps[5].gate = 80; pt.steps[5].transpose = 5;
            pt.steps[5].notes = {{72, 64, false}};
            p.sequencer.chain[2] = 4;
            p.sequencer.patterns[9].steps[40].notes = {{50, 33, false}};   // beyond the pattern's length
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
        for (int i = 1; i < tabs->getNumTabs(); ++i) { tabs->setCurrentTabIndex(i); save("tab-" + tabs->getTabNames()[i]); }
        p.setFirmware("felucca");
        save("firmware-felucca");
       #if FM1_FELUCCA
        {   // Felucca's own front panel, its screen drawn
            juce::Button* device = nullptr;
            std::function<void(juce::Component*)> findButton = [&](juce::Component* c) {
                for (auto* ch : c->getChildren()) {
                    if (auto* b = dynamic_cast<juce::Button*>(ch); b != nullptr && b->getButtonText() == "DEVICE") device = b;
                    findButton(ch);
                }
            };
            findButton(ed.get());
            CHECK(device != nullptr, "the Felucca editor has its DEVICE view");
            auto press = [](juce::Button* b) { b->setToggleState(!b->getToggleState(), juce::dontSendNotification); if (b->onClick) b->onClick(); };
            if (device != nullptr) {
                press(device);
                std::function<void(juce::Component*)> draw = [&](juce::Component* c) {
                    for (auto* ch : c->getChildren()) {
                        if (auto* v = dynamic_cast<FeluccaDeviceView*>(ch)) v->refreshScreen();
                        draw(ch);
                    }
                };
                draw(ed.get());
                save("felucca-device");
                press(device);
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
    else if (cmd == "snapshot" && argc == 4) rc = snapshots(juce::File(argv[3]), juce::File(argv[2]));
    else if (cmd == "state-write" && argc == 4) rc = stateWrite(juce::File(argv[2]), juce::File(argv[3]));
    else if (cmd == "state-check" && argc >= 3) { for (int i = 2; i < argc; ++i) stateCheck(juce::File(argv[i])); rc = 0; }
    else { std::printf("usage: plugin_test render <golden.json> <out.txt> | checks | state-write <golden.json> <dir> | state-check <dir>...\n"); return 2; }
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return rc != 0 ? rc : (g_fail ? 1 : 0);
}
