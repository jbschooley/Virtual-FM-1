// plugin_test -- the whole plugin (FM1Processor), without a host.
//
//   plugin_test render <golden.json> <out.txt>
//       Renders every preset in golden.json through processBlock at 44.1 and
//       48 kHz, with Hardware character off and on, and writes one line per
//       case: a hash of the 16-bit output and its RMS. Comparing two runs shows
//       whether a change altered the sound by even one sample (the restructure
//       must not); the hashes are for comparing builds on one machine.
//
//   plugin_test state-write <golden.json> <dir>
//       Builds a known plugin state (presets, an unsaved edit, a pattern,
//       settings), saves it as the host would to <dir>/state.bin and writes
//       what it should load back as to <dir>/expected.txt.
//
//   plugin_test state-check <dir>...
//       Loads each saved state into a fresh plugin and checks it against its
//       expected.txt: states saved by older versions must keep loading.
//
// The plugin keeps its library in a temporary folder (FM1_DATA_DIR) and never
// connects to a synth (FM1_NO_DEVICE).

#include <cstdio>
#include <cstdlib>

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

// A fresh, empty data folder for each plugin instance.
static juce::File freshDataDir() {
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("fm1-plugin-test", "");
    dir.createDirectory();
    setenv("FM1_DATA_DIR", dir.getFullPathName().toRawUTF8(), 1);
    return dir;
}

static uint64_t fnv(uint64_t h, int16_t v) {
    for (int i = 0; i < 2; ++i) { h ^= uint8_t(uint16_t(v) >> (8 * i)); h *= 1099511628211ull; }
    return h;
}

static int render(const juce::File& golden, const juce::File& out) {
    auto sounds = goldenSounds(golden);
    CHECK(!sounds.empty(), "golden.json has presets");
    juce::StringArray lines;
    for (double rate : {44100.0, 48000.0})
        for (bool hw : {false, true})
            for (const auto& s : sounds) {
                auto dir = freshDataDir();
                {
                    FM1Processor p;
                    auto set = p.settings();
                    set.hardwareCharacter = hw;
                    p.setSettings(set);
                    p.setCurrentSound(s);
                    const int block = 256;
                    p.setPlayConfigDetails(0, 2, rate, block);
                    p.prepareToPlay(rate, block);
                    juce::AudioBuffer<float> buf(2, block);
                    uint64_t h = 1469598103934665603ull;
                    double sum = 0; long n = 0;
                    const int total = int(rate * 1.5), offAt = int(rate * 0.8);
                    for (int pos = 0; pos < total; pos += block) {
                        juce::MidiBuffer midi;
                        if (pos == 0) for (int note : {48, 64, 71}) midi.addEvent(juce::MidiMessage::noteOn(1, note, juce::uint8(100)), 0);
                        if (pos <= offAt && offAt < pos + block)
                            for (int note : {48, 64, 71}) midi.addEvent(juce::MidiMessage::noteOff(1, note), offAt - pos);
                        buf.clear();
                        p.processBlock(buf, midi);
                        for (int ch = 0; ch < 2; ++ch)
                            for (int i = 0; i < block; ++i) {
                                float x = juce::jlimit(-1.0f, 1.0f, buf.getSample(ch, i));
                                h = fnv(h, int16_t(std::lrint(x * 32767.0f)));
                                sum += double(x) * x; ++n;
                            }
                    }
                    lines.add(juce::String(int(rate)) + (hw ? " hw " : " -- ") + juce::String(s.slot).paddedLeft('0', 3) + " "
                              + juce::String(fm1::voiceName(s.voice)) + " " + juce::String::toHexString((juce::int64) h)
                              + " rms " + juce::String(std::sqrt(sum / double(n)), 6));
                }
                dir.deleteRecursively();
            }
    out.replaceWithText(lines.joinIntoString("\n") + "\n");
    std::printf("%d cases written to %s\n", lines.size(), out.getFullPathName().toRawUTF8());
    return g_fail ? 1 : 0;
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
            for (int st = 0; st < pt.length; ++st) {
                const auto& step = pt.steps[size_t(st)];
                line << " " << step.rate << "/" << step.ratchet << "/" << step.gate << "/" << step.chance << "/" << step.transpose
                     << (step.accent ? "a" : "") << (step.slide ? "s" : "");
                for (const auto& n : step.notes) line << "," << n.note << "." << n.vel << (n.tie ? "t" : "");
            }
            d.add(line);
        }
    }
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
        }
        auto set = p.settings();
        set.bendUp = 7; set.bendDown = 2; set.midiChannel = 5; set.fixedVelocity = true; set.velocity = 77;
        set.hardwareCharacter = true; set.fm1VolumeDb = -12;
        p.setSettings(set);
        p.channels.fx = 5;
        juce::MemoryBlock mb;
        p.getStateInformation(mb);
        dir.createDirectory();
        dir.getChildFile("state.bin").replaceWithData(mb.getData(), mb.getSize());
        dir.getChildFile("expected.txt").replaceWithText(describe(p).joinIntoString("\n") + "\n");
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

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    setenv("FM1_NO_DEVICE", "1", 1);
    juce::String cmd = argc > 1 ? argv[1] : "";
    int rc = 2;
    if (cmd == "render" && argc == 4) rc = render(juce::File(argv[2]), juce::File(argv[3]));
    else if (cmd == "state-write" && argc == 4) rc = stateWrite(juce::File(argv[2]), juce::File(argv[3]));
    else if (cmd == "state-check" && argc >= 3) { for (int i = 2; i < argc; ++i) stateCheck(juce::File(argv[i])); rc = 0; }
    else { std::printf("usage: plugin_test render <golden.json> <out.txt> | state-write <golden.json> <dir> | state-check <dir>...\n"); return 2; }
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return rc != 0 ? rc : (g_fail ? 1 : 0);
}
