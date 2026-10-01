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
#include "Fm1Record.h"
#include "Fm1Session.h"
#include "FmSynth.h"
#include <juce_audio_formats/juce_audio_formats.h>

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

    if (cmd == "render") {   // render <backup.syx> <slot 0-based> <note> <vel> <hold ms> <total ms> <out.wav>
        juce::MemoryBlock mb;
        juce::File(juce::File::getCurrentWorkingDirectory().getChildFile(argv[2])).loadFileAsData(mb);
        fm1::Bytes b(static_cast<const uint8_t*>(mb.getData()), static_cast<const uint8_t*>(mb.getData()) + mb.getSize());
        auto sounds = fm1::readSyx(b).sounds;
        int slot = std::atoi(argv[3]), note = std::atoi(argv[4]), vel = std::atoi(argv[5]), hold = std::atoi(argv[6]), total = std::atoi(argv[7]);
        const fm1::Sound* snd = nullptr;
        for (const auto& x : sounds) if (x.slot == slot) snd = &x;
        if (!snd) { std::printf("slot not in file\n"); return 1; }
        FmSynth synth; synth.prepare(44100.0);
        fm1::Edit e = fm1::unpackVoice(snd->voice);
        synth.setPatch(e.data());
        if (snd->hasRecord) {
            fm1::VaFilter f = fm1::filterFromRecord(snd->record);
            FmSynth::Filter ef;
            ef.on = f.on; ef.type = f.type; ef.keyTrack = f.keyTrack; ef.cutoff = f.cutoff; ef.resonance = f.resonance;
            ef.envelope = f.envelope; ef.decay = f.decay; ef.shape = f.shape; ef.velocity = f.velocity; ef.lfo = f.lfo;
            synth.setFilter(ef);
            fm1::Envelope env = fm1::envFromRecord(snd->record);
            synth.setEnvelope(env.on, env.a, env.d, env.s, env.r);
        }
        juce::AudioBuffer<float> out(1, total * 441 / 10);
        int holdS = hold * 441 / 10;
        synth.noteOn(note, vel);
        synth.render(out.getWritePointer(0), holdS);
        synth.noteOff(note);
        synth.render(out.getWritePointer(0) + holdS, out.getNumSamples() - holdS);
        juce::File f = juce::File::getCurrentWorkingDirectory().getChildFile(argv[8]);
        f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(new juce::FileOutputStream(f), 44100.0, 1, 24, {}, 0));
        w->writeFromAudioSampleBuffer(out, 0, out.getNumSamples());
        std::printf("rendered %s note %d to %s\n", fm1::voiceName(snd->voice).c_str(), note, argv[8]);
        return 0;
    }
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
    if (cmd == "mem") {   // mem <addr hex> <len hex> <file>: read RAM in 256-byte requests
        uint32_t addr = uint32_t(std::strtoul(argv[2], nullptr, 16)), len = uint32_t(std::strtoul(argv[3], nullptr, 16));
        juce::MemoryBlock out;
        auto t0 = juce::Time::getMillisecondCounter();
        for (uint32_t off = 0; off < len; off += 256) {
            int n = int(std::min<uint32_t>(256, len - off));
            uint32_t a = addr + off;
            auto r = link.ask<fm1::Reply>(fm1::encodeMemRead(a, n), [a](const fm1::Bytes& f) -> std::optional<fm1::Reply> {
                auto d = fm1::decodeReply(f);
                if (!d || d->kind != fm1::Reply::Kind::Mem || d->arg != a) return std::nullopt;
                return d;
            }, 1000, 3);
            if (!r || r->status != 0 || int(r->data.size()) != n) { std::printf("read failed at %08X (%s)\n", a, r ? "refused" : "no answer"); return 1; }
            out.append(r->data.data(), r->data.size());
        }
        juce::File(juce::File::getCurrentWorkingDirectory().getChildFile(argv[4])).replaceWithData(out.getData(), out.getSize());
        std::printf("read %u bytes in %u ms\n", len, juce::Time::getMillisecondCounter() - t0);
        return 0;
    }
    if (cmd == "current") {
        fm1::Sound got; bool have = false, same = false;
        session.onCurrentRead = [&](const fm1::Sound& live, const fm1::Sound& stored) { got = live; have = true; same = live.voice == stored.voice && live.record == stored.record; };
        session.pullCurrent();
        waitIdle(session);
        if (have) {
            std::printf("current: preset %d %s (%s)\n", got.slot + 1, fm1::voiceName(got.voice).c_str(), same ? "same as stored" : "unsaved changes");
            auto f = fm1::filterFromRecord(got.record);
            std::printf("  filter: %s type %s cutoff %d resonance %d envelope %d decay %d shape %d velocity %d keytrack %d lfo %d\n",
                        f.on ? "on" : "off", fm1::kFilterTypeNames[f.type & 3], f.cutoff, f.resonance, f.envelope, f.decay, f.shape, f.velocity, f.keyTrack, f.lfo);
        }
        return have ? 0 : 1;
    }
    if (cmd == "write") {   // write <file.syx>: write every FM-1+VA sound message in the file to its slot, verified
        juce::MemoryBlock mb;
        juce::File(juce::File::getCurrentWorkingDirectory().getChildFile(argv[2])).loadFileAsData(mb);
        fm1::Bytes b(static_cast<const uint8_t*>(mb.getData()), static_cast<const uint8_t*>(mb.getData()) + mb.getSize());
        auto c = fm1::readSyx(b);
        std::vector<fm1::Sound> own;
        for (auto& x : c.sounds) if (x.slot >= 0 && x.hasRecord) own.push_back(x);
        if (own.empty()) { std::printf("no FM-1 sound messages in the file\n"); return 1; }
        session.push(own);
        waitIdle(session);
        std::printf("%zu of %zu written and verified\n", written.size(), own.size());
        return written.size() == own.size() ? 0 : 1;
    }
    if (cmd == "param") {   // param <num> <val>: DX7 voice parameter change (edit buffer only)
        int num = std::atoi(argv[2]), val = std::atoi(argv[3]);
        link.send({0xF0, 0x43, 0x10, uint8_t((num >> 7) & 1), uint8_t(num & 0x7F), uint8_t(val & 0x7F), 0xF7});
        pump(100);
        return 0;
    }
    if (cmd == "cc") {   // cc <channel 1-16> <cc> <val>
        int ch = std::atoi(argv[2]), cc = std::atoi(argv[3]), val = std::atoi(argv[4]);
        link.sendRaw({uint8_t(0xB0 | ((ch - 1) & 15)), uint8_t(cc), uint8_t(val)});
        pump(100);
        return 0;
    }
    if (cmd == "press") {   // press <cc> [<cc> ...]: a short press and release of each panel control, in order
        for (int i = 2; i < argc; ++i) {
            int cc = std::atoi(argv[i]);
            link.sendRaw({0xB0, uint8_t(cc), 127});
            juce::Thread::sleep(30);
            link.sendRaw({0xB0, uint8_t(cc), 0});
            juce::Thread::sleep(150);
        }
        return 0;
    }
    if (cmd == "note") {   // note <note> <vel> <hold ms>: play one note on channel 1
        int n = std::atoi(argv[2]), v = std::atoi(argv[3]), ms = std::atoi(argv[4]);
        link.sendRaw({0x90, uint8_t(n), uint8_t(v)});
        juce::Thread::sleep(ms);
        link.sendRaw({0x80, uint8_t(n), 0});
        return 0;
    }
    if (cmd == "sendtest") {   // sendtest <slot 0-based>: send an edited version of the slot to the edit buffer, unsaved
        int slot = std::atoi(argv[2]);
        session.pull({slot}); waitIdle(session);
        if (read.size() != 1) return 1;
        fm1::Sound s = read[0];
        fm1::Edit e = fm1::unpackVoice(s.voice);
        e[134] = (e[134] + 7) % 32;                          // another algorithm
        e[5 * 21 + 16] = 70;                                 // OP1 output level
        s.voice = fm1::withName(fm1::packVoice(e), "SENT TEST");
        fm1::FxChain fx = fm1::fxFromRecord(s.record);
        fx.fx[fm1::FxReverb].on = true; fx.fx[fm1::FxReverb].type = 1; fx.fx[fm1::FxReverb].p = {60, 35, 0};
        fx.fx[fm1::FxChorus].on = true; fx.fx[fm1::FxChorus].p = {20, 40, 50};
        fm1::fxToRecord(fx, s.record);
        fm1::Envelope env; env.on = true; env.a = 10; env.d = 40; env.s = 80; env.r = 30;
        fm1::envToRecord(env, s.record);
        session.sendEdit(s, {}, true); waitIdle(session);
        std::printf("sent: %s\n", last.text.toRawUTF8());
        return last.failed ? 1 : 0;
    }
    if (cmd == "select") {   // select <slot 0-based>: program change, as Show on FM-1 does
        session.select(std::atoi(argv[2]));
        pump(300);
        return 0;
    }
    std::printf("unknown command\n");
    return 2;
}
