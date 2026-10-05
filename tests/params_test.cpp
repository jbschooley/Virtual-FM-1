// Params load/commit: loading a preset into the host parameters and committing
// it back must reproduce its bytes exactly (the firmware's "unset" markers
// included), and a changed setting must land in the right byte.
//
//   params_test <file.syx>   (an FM-1+VA backup; tests/golden.json's pack is used when omitted)

#include <cstdio>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Fm1Codec.h"
#include "Params.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, msg) do { if (cond) ++g_pass; else { ++g_fail; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } } while (0)

struct Host : juce::AudioProcessor {
    juce::AudioProcessorValueTreeState apvts{*this, nullptr, "params", Params::layout()};
    Params params{apvts};
    const juce::String getName() const override { return "t"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
};

static void setParam(Params& p, const juce::String& id, int v) {
    auto* prm = p.apvts.getParameter(id);
    prm->setValueNotifyingHost(prm->convertTo0to1(float(v)));
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    std::vector<fm1::Sound> sounds;
    if (argc > 1) {
        juce::MemoryBlock mb;
        juce::File(argv[1]).loadFileAsData(mb);
        fm1::Bytes b(static_cast<const uint8_t*>(mb.getData()), static_cast<const uint8_t*>(mb.getData()) + mb.getSize());
        sounds = fm1::readSyx(b).sounds;
    }
    // always include a record shaped like the synth's stored ones and the VA pack's style
    fm1::Sound unset;
    unset.slot = 0; unset.voice = fm1::packVoice(fm1::kInitEdit); unset.hasRecord = true;
    const uint8_t rec[59] = {0x50, 3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3, 0,0,0, 1,0,0, 2,0,0, 3,0,0, 4,0,0, 5,0,0, 6,0,0, 7,0,0, 8,0,0, 0,0,0,0,0};
    std::copy(rec, rec + 59, unset.record.begin());
    sounds.push_back(unset);
    fm1::Sound va = unset;
    const uint8_t varec[59] = {0,0,0,0x15,0x26,0x16,0,0,0,0,0,0,0,0,0,0,0,0,0x5a,2,0x4e,0x3e,0,0xc8,0x99,0x80,0x81, 0,0,0, 1,1,1, 2,0,0, 3,0,0, 4,0,0, 5,0,0, 6,0,0, 7,0,0, 8,0,0, 0,0,0,0,0};
    std::copy(varec, varec + 59, va.record.begin());
    sounds.push_back(va);

    Host host;
    int identical = 0;
    for (const auto& s : sounds) {
        host.params.load(s);
        fm1::Sound out = s;
        host.params.commit(out);
        if (out.voice == s.voice && out.record == s.record) ++identical;
        else {
            std::printf("slot %d %s changed:", s.slot + 1, fm1::voiceName(s.voice).c_str());
            for (int i = 0; i < 59; ++i) if (out.record[size_t(i)] != s.record[size_t(i)]) std::printf(" rec[%d] %02x->%02x", i, s.record[size_t(i)], out.record[size_t(i)]);
            for (int i = 0; i < 128; ++i) if (out.voice[size_t(i)] != s.voice[size_t(i)]) std::printf(" voice[%d] %02x->%02x", i, s.voice[size_t(i)], out.voice[size_t(i)]);
            std::printf("\n");
        }
    }
    std::printf("%d of %zu presets survive load + commit unchanged\n", identical, sounds.size());
    CHECK(identical == int(sounds.size()), "load + commit is the identity for untouched presets");

    // an edit lands where it should and nothing else moves
    host.params.load(unset);
    fm1::Sound edited = unset;
    setParam(host.params, Params::vcedId(134), 17);       // algorithm 18
    setParam(host.params, Params::fxOnId(fm1::FxReverb), 1);
    setParam(host.params, Params::fxParamId(fm1::FxReverb, 1), 40);   // reverb mix
    setParam(host.params, Params::envId(0), 25);          // attack
    host.params.commit(edited);
    fm1::Edit e = fm1::unpackVoice(edited.voice);
    CHECK(e[134] == 17, "algorithm written to the voice");
    CHECK(edited.record[28 + 3 * 1] == 1, "reverb on written at its chain position");
    CHECK(edited.record[3 * 1 + 1] == 40, "reverb mix written to byte 4");
    CHECK(edited.record[54] == 25, "attack written to byte 54");
    int others = 0;
    for (int i = 0; i < 59; ++i) if (i != 31 && i != 4 && i != 54 && edited.record[size_t(i)] != unset.record[size_t(i)]) ++others;
    CHECK(others == 0, "no other record byte changed");
    fm1::Edit e0 = fm1::unpackVoice(unset.voice);
    int otherVoice = 0;
    for (int i = 0; i < 155; ++i) if (i != 134 && e[size_t(i)] != e0[size_t(i)]) ++otherVoice;
    CHECK(otherVoice == 0, "no other voice byte changed");

    // a value changed and changed back is written back too
    fm1::Sound again = edited;
    setParam(host.params, Params::vcedId(134), 0);
    host.params.commit(again);
    CHECK(fm1::unpackVoice(again.voice)[134] == 0, "changing back is committed");

    // Parameter freeze: hosts save automation and parameter values by id (VST3
    // hashes it, AU orders by version hint then id, LV2 names ports by it), so an
    // existing parameter's id, version hint and range (ends, step, skew) must never change.
    // tests/params-frozen.txt lists them; a new parameter is appended to it
    // (FM1_APPEND_FROZEN=1 params_test does that) with a higher version hint.
    {
        juce::StringArray now;
        for (auto* p : host.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p)) {
                auto range = r->getNormalisableRange();
                now.add(r->getParameterID() + "\t" + juce::String(r->getVersionHint()) + "\t"
                        + juce::String(range.start, 6) + "\t" + juce::String(range.end, 6) + "\t"
                        + juce::String(range.interval, 6) + "\t" + juce::String(range.skew, 6));
            }
        juce::File frozen(juce::String(FM1_SOURCE_DIR) + "/tests/params-frozen.txt");
        juce::StringArray was;
        was.addLines(frozen.loadFileAsString());
        was.removeEmptyStrings();
        int missing = 0, added = 0;
        for (auto& line : was) if (!now.contains(line)) { ++missing; std::printf("frozen parameter changed or gone: %s\n", line.toRawUTF8()); }
        juce::StringArray fresh;
        for (auto& line : now) if (!was.contains(line)) { ++added; fresh.add(line); }
        CHECK(missing == 0, "every frozen parameter keeps its id, version hint and range");
        if (added > 0 && juce::SystemStats::getEnvironmentVariable("FM1_APPEND_FROZEN", {}).isNotEmpty()) {
            frozen.appendText(fresh.joinIntoString("\n") + "\n");
            std::printf("appended %d new parameters to %s\n", added, frozen.getFullPathName().toRawUTF8());
        } else {
            CHECK(added == 0, "no parameter missing from tests/params-frozen.txt (append new ones with FM1_APPEND_FROZEN=1)");
            for (auto& line : fresh) std::printf("not frozen yet: %s\n", line.toRawUTF8());
        }
    }

    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
