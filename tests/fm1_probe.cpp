// fm1_probe -- exercises the plugin's sync code (Fm1Link, Fm1Session) against a
// real FM-1 from the command line.
//
//   fm1_probe ports                     list MIDI ports and the one findFm1() picks
//   fm1_probe identify                  firmware identity
//   fm1_probe pull <first> <last>       read presets (0-based slots), print name and engine
//   fm1_probe roundtrip <slot>          read a preset, write the same bytes back, verify
//   fm1_probe patterns <first> <last>   read sequencer patterns (0-based)
//   fm1_probe dump <file.syx>           read all 128 presets into an FM-1+VA .syx backup

#include <cstdio>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>

#include "Fm1Codec.h"
#include "Fm1Link.h"
#include "Fm1Seq.h"
#include "Fm1Session.h"

static void pump(int ms) {
    auto end = juce::Time::getMillisecondCounter() + juce::uint32(ms);
    while (juce::Time::getMillisecondCounter() < end) juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
}
static void waitIdle(Fm1Session& s) {
    pump(100);
    while (s.busy()) pump(50);
    pump(200);
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    juce::String cmd = argc > 1 ? argv[1] : "identify";

    if (cmd == "ports") {
        for (auto& d : Fm1Link::inputs()) std::printf("in : %s\n", d.name.toRawUTF8());
        for (auto& d : Fm1Link::outputs()) std::printf("out: %s\n", d.name.toRawUTF8());
    }
    auto ports = Fm1Link::findFm1();
    if (!ports) { std::printf("findFm1: no FM-1 found\n"); return 1; }
    std::printf("findFm1: in \"%s\", out \"%s\"\n", ports->inputName.toRawUTF8(), ports->outputName.toRawUTF8());
    if (cmd == "ports") return 0;

    Fm1Link link;
    if (!link.open(ports->inputId, ports->outputId)) { std::printf("could not open the ports\n"); return 1; }
    Fm1Session session(link);
    std::vector<fm1::Sound> read, written;
    std::vector<std::pair<int, fm1::seq::Pattern>> pats;
    Fm1Session::Progress last;
    session.onIdentity = [](const fm1::Identity& id) { std::printf("identity: %s (%s)\n", id.name().c_str(), id.isStock() ? "M-VAVE stock" : "FM-1+VA"); };
    session.onSoundRead = [&](const fm1::Sound& s) { read.push_back(s); };
    session.onSoundWritten = [&](const fm1::Sound& s) { written.push_back(s); };
    session.onPatternRead = [&](int p, const fm1::seq::Pattern& pp) { pats.emplace_back(p, pp); };
    session.onProgress = [&](const Fm1Session::Progress& p) { last = p; if (p.finished) std::printf("%s: %s\n", p.failed ? "FAILED" : "ok", p.text.toRawUTF8()); };

    session.identify();
    waitIdle(session);
    if (!session.lastIdentity()) return 1;
    if (cmd == "identify") return 0;

    auto range = [&](int dflt0, int dflt1) {
        int a = argc > 2 ? std::atoi(argv[2]) : dflt0, b = argc > 3 ? std::atoi(argv[3]) : a;
        if (argc <= 2) b = dflt1;
        std::vector<int> v; for (int i = a; i <= b; ++i) v.push_back(i); return v;
    };

    if (cmd == "pull" || cmd == "dump") {
        auto slots = cmd == "dump" ? range(0, 127) : range(0, 3);
        if (cmd == "dump") { slots.clear(); for (int i = 0; i < 128; ++i) slots.push_back(i); }
        auto t0 = juce::Time::getMillisecondCounter();
        session.pull(slots);
        waitIdle(session);
        std::printf("%zu presets in %u ms\n", read.size(), juce::Time::getMillisecondCounter() - t0);
        for (const auto& s : read) {
            auto fx = std::string();
            std::printf("  %3d  %-10s  %s  rec[18]=%02X\n", s.slot + 1, fm1::voiceName(s.voice).c_str(),
                        fm1::engineOf(s.record) == fm1::Engine::VA ? "VA" : "FM", s.record[18]);
        }
        if (cmd == "dump" && argc > 2 && read.size() == 128) {
            auto bytes = fm1::toSyx(read);
            juce::File(juce::File::getCurrentWorkingDirectory().getChildFile(argv[2])).replaceWithData(bytes.data(), bytes.size());
            std::printf("wrote %s (%zu bytes)\n", argv[2], bytes.size());
        }
        return last.failed ? 1 : 0;
    }
    if (cmd == "roundtrip") {
        int slot = argc > 2 ? std::atoi(argv[2]) : 0;
        session.pull({slot});
        waitIdle(session);
        if (read.size() != 1) return 1;
        std::printf("read %d: %s\n", slot + 1, fm1::voiceName(read[0].voice).c_str());
        session.push({read[0]});
        waitIdle(session);
        std::printf("write-back verified: %s\n", written.size() == 1 && written[0].voice == read[0].voice && written[0].record == read[0].record ? "yes" : "NO");
        return written.size() == 1 ? 0 : 1;
    }
    if (cmd == "patterns") {
        session.pullPatterns(range(0, 1));
        waitIdle(session);
        for (const auto& [p, pp] : pats) {
            int notes = 0; for (const auto& st : pp.steps) notes += int(st.notes.size());
            std::printf("  pattern %2d: length %d, tempo %d, gate %d, swing %d, preset %d, %d notes\n", p + 1, pp.length, pp.tempo, pp.gate, pp.swing, pp.sound + 1, notes);
        }
        return last.failed ? 1 : 0;
    }
    std::printf("unknown command\n");
    return 2;
}
