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
//       Hardware character moves the engine to 44.1 kHz with a rate converter
//       in other hosts: the reported latency follows it, and switching it on
//       and off while running works.
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

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

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
        if (!tree.getChildWithName("FM1Bank").isValid()) {
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

        // a project keeps its sound when its library slot changes afterwards
        {
            juce::MemoryBlock project;
            int slot;
            std::vector<int> paramsSaved;
            {
                FM1Processor q;
                q.selectSlot(2);
                slot = q.bank.currentSlot();
                q.getStateInformation(project);
                for (auto* prm : q.getParameters()) paramsSaved.push_back(int(std::lrint(dynamic_cast<juce::RangedAudioParameter*>(prm)->convertFrom0to1(prm->getValue()))));
            }
            {   // the library's slot changes (another instance stores something else there)
                BankModel lib; LibraryStore ls; ls.load(lib);
                auto other = sounds[7]; other.slot = slot;
                lib.setSound(slot, other, false); ls.save(lib);
            }
            FM1Processor r;
            juce::String status;
            r.onStatus = [&](const juce::String& s) { status = s; };
            r.setStateInformation(project.getData(), int(project.getSize()));
            std::vector<int> paramsLoaded;
            for (auto* prm : r.getParameters()) paramsLoaded.push_back(int(std::lrint(dynamic_cast<juce::RangedAudioParameter*>(prm)->convertFrom0to1(prm->getValue()))));
            CHECK(paramsLoaded == paramsSaved, "the project's sound is restored though its library slot changed");
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
