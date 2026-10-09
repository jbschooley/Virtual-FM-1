// fm1_probe -- exercises the plugin's sync code (Fm1Link, Fm1Session) against a
// real FM-1 from the command line.
//
//   fm1_probe ports                     list MIDI ports and the one findFm1() picks
//   fm1_probe identify                  firmware identity
//   fm1_probe pull <first> <last>       read presets (0-based slots), print name and engine
//   fm1_probe roundtrip <slot>          read a preset, write the same bytes back, verify
//   fm1_probe patterns <first> <last>   read sequencer patterns (0-based)
//   fm1_probe dump <file.syx>           read all 128 presets into an FM-1+VA .syx backup
//   fm1_probe felucca-backup <file.json> an FM-1 running Felucca: every object, as its web editor saves a backup
//   fm1_probe felucca-live <seconds>     an FM-1 running Felucca mirrored live with a Felucca of the plugin's own
//                                        (no flash written: values, steps, selection)

#include <cstdio>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>

#include "Fm1Codec.h"
#include "Fm1Link.h"
#include "Fm1Seq.h"
#include "Fm1Record.h"
#include "Fm1Session.h"
#include "FmSynth.h"
#include "FeluccaSeq.h"
#include "FeluccaSync.h"
#include "Firmwares.h"
#if FM1_FELUCCA
 #include "FeluccaEngine.h"
#endif
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
        std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream>(f);
        auto w = wav.createWriterFor(os, juce::AudioFormatWriterOptions{}.withSampleRate(44100.0).withNumChannels(1).withBitsPerSample(24));
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
    session.onIdentity = [](const fm1::Identity& id) { std::printf("identity: %s (%s)\n", id.name().c_str(), fm1::firmwareFor(id)->name().toRawUTF8()); };
    session.onSoundRead = [&](const fm1::Sound& s) { read.push_back(s); };
    session.onSoundWritten = [&](const fm1::Sound& s) { written.push_back(s); };
    session.onPatternRead = [&](int p, const fm1::seq::Pattern& pp) { pats.emplace_back(p, pp); };
    session.onProgress = [&](const Fm1Session::Progress& p) { last = p; if (p.finished) std::printf("%s: %s\n", p.failed ? "FAILED" : "ok", p.text.toRawUTF8()); };

    session.identify();
    waitIdle(session);
    if (!session.lastIdentity()) return 1;
    if (cmd == "identify") return 0;
   #if FM1_FELUCCA
    // an FM-1 running SLOOP or Melodee: its dialect, and one of the plugin's own on the other side
    const auto fw = fm1::firmwareIdFor(*session.lastIdentity());
    const bool slp = fw == "sloop", mel = fw == "melodee";
    const felucca::Dialect& dl = slp ? felucca::sloopDialect() : mel ? felucca::melodeeDialect() : felucca::feluccaDialect();
    const auto flavor = slp ? FeluccaEngine::Flavor::Sloop : mel ? FeluccaEngine::Flavor::Melodee : FeluccaEngine::Flavor::Felucca;
   #else
    const felucca::Dialect& dl = felucca::feluccaDialect();
   #endif

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
    if (cmd == "felucca-backup" && argc > 2) {   // reads only
        felucca::LinkEndpoint synth(link, dl);
        juce::String err;
        auto o = felucca::backup(synth, [](int done, int total, const juce::String&) { std::printf("\r  %d / %d bytes", done, total); std::fflush(stdout); return true; }, err);
        std::printf("\n");
        if (!o) { std::printf("failed: %s\n", err.toRawUTF8()); return 1; }
        for (auto& [id, b] : *o) std::printf("  object %d: %zu bytes\n", id, b.size());
        juce::File(juce::File::getCurrentWorkingDirectory().getChildFile(argv[2])).replaceWithText(felucca::backupJson(*o, juce::String("FM-1 running ") + dl.name, dl));
        std::printf("wrote %s\n", argv[2]);
        return 0;
    }
   #if FM1_FELUCCA
    if (cmd == "felucca-send" && argc > 2) {   // writes the synth's flash: as the plugin's Send does
        struct Mine : felucca::Endpoint {
            const felucca::Dialect& dialect() const override { return *d; }
            const felucca::Dialect* d = nullptr;
            std::shared_ptr<FeluccaEngine> f;
            std::optional<fm1::Bytes> ask(const fm1::Bytes& q, int) override { return f->ask(q); }
            std::vector<fm1::Bytes> pushes() override { f->takeSysex(); return {}; }
        } mine;
        mine.f = std::make_shared<FeluccaEngine>(flavor);
        mine.d = &dl;
        felucca::LinkEndpoint synth(link, dl);
        juce::String err;
        auto progress = [](int done, int total, const juce::String&) { std::printf("\r  %d / %d", done, total); std::fflush(stdout); return true; };
        // 1. the synth's backup, kept (argv[2])
        auto theirs = felucca::backup(synth, progress, err);
        std::printf("\n");
        if (!theirs) { std::printf("backup failed: %s\n", err.toRawUTF8()); return 1; }
        juce::File(juce::File::getCurrentWorkingDirectory().getChildFile(argv[2])).replaceWithText(felucca::backupJson(*theirs, juce::String("FM-1 running ") + dl.name, dl));
        std::printf("the FM-1's backup: %s\n", argv[2]);
        // 2. the plugin's Felucca: the synth's things (or a device file's: argv[3]), and one user
        // preset only it has
        felucca::Objects source = *theirs;
        if (argc > 3) {
            source.clear();
            if (!felucca::readBackup(juce::File::getCurrentWorkingDirectory().getChildFile(argv[3]), source, err, dl)) { std::printf("cannot read %s: %s\n", argv[3], err.toRawUTF8()); return 1; }
            std::printf("the plugin's Felucca from %s\n", argv[3]);
        }
        if (!felucca::restore(mine, source, {}, err)) { std::printf("into the plugin's Felucca failed: %s\n", err.toRawUTF8()); return 1; }
        const uint8_t slot = 31;   // U32
        auto st = mine.ask(felucca::frame(felucca::kUpStore, {slot, 'S', 'E', 'N', 'D', ' ', 'T', 'E', 'S', 'T', 0}), 0);
        std::printf("U32 \"SEND TEST\" stored in the plugin's Felucca: %s\n", st && felucca::argsOf(*st).size() >= 2 && felucca::argsOf(*st)[1] == 0 ? "ok" : "FAILED");
        auto ours = felucca::backup(mine, {}, err);
        if (!ours) { std::printf("the plugin's backup failed: %s\n", err.toRawUTF8()); return 1; }
        ours->erase(1);   // the synth's settings stay its own, as Send leaves them
        // 3. send
        if (!felucca::restore(synth, *ours, progress, err)) { std::printf("\nsend failed: %s\n", err.toRawUTF8()); return 1; }
        std::printf("\nsent\n");
        // 4. read the synth back and compare
        auto after = felucca::backup(synth, progress, err);
        std::printf("\n");
        if (!after) { std::printf("read-back failed: %s\n", err.toRawUTF8()); return 1; }
        int same = 0, differ = 0;
        for (auto& [id, b] : *ours) {
            const bool eq = after->count(id) && (*after)[id] == b;
            std::printf("  object %d: %zu bytes sent, %s\n", id, b.size(), eq ? "the same on the FM-1" : "DIFFERENT on the FM-1");
            (eq ? same : differ)++;
        }
        std::printf("  object 1 (settings): %s\n", after->count(1) && (*after)[1] == (*theirs)[1] ? "the FM-1's own, unchanged" : "CHANGED");
        auto l = synth.ask(felucca::frame(felucca::kUpList, {slot, 1}), 1000);
        juce::String name;
        if (l) { auto a = felucca::argsOf(*l); for (size_t k = 5; k < a.size() && a[k]; ++k) name += juce::String::charToString(juce::juce_wchar(a[k])); }
        std::printf("the FM-1's U32: \"%s\"\n", name.toRawUTF8());
        std::printf("%d the same, %d different\n", same, differ);
        return differ == 0 && name == "SEND TEST" ? 0 : 1;
    }
    if (cmd == "sloop-extras") {   // SLOOP 2.4: part 2's locks, nudges and fill conditions, pulled and sent; put back after (RAM only)
        struct Mine : felucca::Endpoint {
            const felucca::Dialect& dialect() const override { return *d; }
            const felucca::Dialect* d = nullptr;
            std::shared_ptr<FeluccaEngine> f;
            std::optional<fm1::Bytes> ask(const fm1::Bytes& q, int) override { return f->ask(q); }
            std::vector<fm1::Bytes> pushes() override { f->takeSysex(); return {}; }
        } mine;
        mine.f = std::make_shared<FeluccaEngine>(flavor);
        mine.d = &dl;
        felucca::LinkEndpoint synth(link, dl);
        juce::String err;
        const int t = 1;
        int fails = 0;
        auto check = [&](bool ok, const char* what) { std::printf("%s: %s\n", what, ok ? "ok" : "FAILED"); if (!ok) ++fails; };
        auto extrasOf = [&](felucca::Endpoint& e) { felucca::TrackPattern x; return felucca::readExtras(e, t, x) ? std::optional<felucca::TrackPattern>(x) : std::nullopt; };
        auto same = [](const felucca::TrackPattern& a, const felucca::TrackPattern& b) { return a.locks == b.locks && a.micro == b.micro && a.fill == b.fill; };
        auto orig = felucca::readPattern(synth, t, err);
        if (!orig || !orig->extras) { std::printf("the FM-1 gave no locks, nudges or conditions (%s)\n", err.toRawUTF8()); return 1; }
        std::printf("part 2 on the FM-1: %zu locks\n", orig->locks.size());
        // set on the FM-1 through its editor protocol, read back
        synth.ask(felucca::lockSet(t, 4, 1, 100), 400);   // step 5: ATK 100
        synth.ask(felucca::microSet(t, 4, -12), 400);
        synth.ask(felucca::fillSet(t, 5, felucca::kFillNot), 400);
        auto a = extrasOf(synth);
        check(a && std::find(a->locks.begin(), a->locks.end(), felucca::Lock{4, 1, 100}) != a->locks.end() && a->micro[4] == -12 && a->fill[5] == felucca::kFillNot,
              "set on the FM-1 and read back");
        // Pull: into the plugin's SLOOP
        check(felucca::copyPatterns(synth, mine, 4, err), "pull");
        auto m = extrasOf(mine);
        check(a && m && same(*a, *m), "the plugin has the FM-1's locks, nudges and conditions");
        // Send: changed in the plugin, sent, read back from the FM-1
        mine.ask(felucca::lockDelete(t, 4, 1), 400);
        mine.ask(felucca::lockSet(t, 8, 39, 20), 400);    // step 9: PAN 20
        mine.ask(felucca::microSet(t, 8, 7), 400);
        mine.ask(felucca::fillSet(t, 8, felucca::kFillOnly), 400);
        check(felucca::copyPatterns(mine, synth, 4, err), (juce::String("send ") + err).toRawUTF8());
        auto s2 = extrasOf(synth), m2 = extrasOf(mine);
        check(s2 && m2 && same(*s2, *m2) && s2->micro[8] == 7 && s2->fill[8] == felucca::kFillOnly
              && std::find(s2->locks.begin(), s2->locks.end(), felucca::Lock{4, 1, 100}) == s2->locks.end(), "the FM-1 has the plugin's (the deleted lock gone)");
        // put the FM-1's part 2 back as it was
        auto now = felucca::readPattern(synth, t, err);
        check(now && felucca::writePattern(synth, t, *orig, err, &*now), "put back");
        auto back = extrasOf(synth);
        check(back && same(*back, *orig), "the FM-1's part 2 as before");
        std::printf("%s\n", fails ? "FAILED" : "all ok");
        return fails ? 1 : 0;
    }
    if (cmd == "melodee-sync" && mel) {   // Pull, Send, sound copies, pattern banks and Live with an FM-1 on Melodee (writes its flash)
        struct Mine : felucca::Endpoint {
            const felucca::Dialect& dialect() const override { return felucca::melodeeDialect(); }
            std::shared_ptr<FeluccaEngine> f;
            std::vector<fm1::Bytes> got;
            std::optional<fm1::Bytes> ask(const fm1::Bytes& q, int) override {
                std::optional<fm1::Bytes> r;
                for (auto& m : f->request(q)) { if (felucca::isPush(felucca::commandOf(m))) got.push_back(m); else if (felucca::commandOf(m) == felucca::commandOf(q) && !r) r = m; }
                return r;
            }
            std::vector<fm1::Bytes> pushes() override {
                for (auto& m : f->takeSysex()) if (felucca::isPush(felucca::commandOf(m))) got.push_back(m);
                std::vector<fm1::Bytes> out; out.swap(got); return out;
            }
        } mine;
        mine.f = std::make_shared<FeluccaEngine>(flavor);
        felucca::LinkEndpoint synth(link, dl);
        juce::String err;
        int fails = 0;
        auto check = [&](bool ok, const juce::String& what) { std::printf("%s: %s\n", what.toRawUTF8(), ok ? "ok" : "FAILED"); std::fflush(stdout); if (!ok) ++fails; };
        auto progress = [](int done, int total, const juce::String&) { std::printf("\r  %d / %d", done, total); std::fflush(stdout); return true; };
        auto args = [](const std::optional<fm1::Bytes>& r) { return r ? felucca::argsOf(*r) : std::vector<uint8_t>{}; };
        auto tone = [&](felucca::Endpoint& e, int t) { auto a = args(e.ask(felucca::frame(felucca::kCzGet, {0, uint8_t(t)}), 1000)); return a.size() > 3 ? std::vector<uint8_t>(a.begin() + 3, a.end()) : std::vector<uint8_t>{}; };
        auto patch = [&](felucca::Endpoint& e, int t) { auto a = args(e.ask(felucca::frame(felucca::kProphetCmd, {0, uint8_t(t)}), 1000)); return a.size() > 3 && a[1] == 0 ? std::vector<uint8_t>(a.begin() + 3, a.end()) : std::vector<uint8_t>{}; };
        auto dumpOf = [&](felucca::Endpoint& e, int t) { return args(e.ask(felucca::frame(felucca::kTrackDump, {uint8_t(t)}), 1000)); };
        auto info = args(synth.ask(felucca::frame(felucca::kInfo), 1000));
        std::printf("INFO: %s\n", info.empty() ? "(none)" : reinterpret_cast<const char*>(info.data()));
        // 1. Pull: the FM-1's whole backup, kept, then into the plugin's Melodee (settings left its own)
        auto theirs = felucca::backup(synth, progress, err);
        std::printf("\n");
        check(theirs.has_value() && theirs->size() == 28, "pull: the FM-1's 28 objects (" + err + ")");
        if (!theirs) return 1;
        const auto keep = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Virtual FM-1/FM-1 backups")
                              .getChildFile("melodee-0.13-" + juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S") + "-before-sync-test.json");
        keep.replaceWithText(felucca::backupJson(*theirs, juce::String(info.empty() ? "MELODEE" : reinterpret_cast<const char*>(info.data())), dl));
        std::printf("its backup: %s\n", keep.getFullPathName().toRawUTF8());
        auto pulled = *theirs;
        pulled.erase(1);
        int rcs = 0;
        for (int id : dl.restoreOrder) if (pulled.count(id)) rcs += mine.f->putObject(id, pulled[id]) != 0;
        std::vector<uint8_t> m0;
        check(rcs == 0 && mine.f->object(0, m0) && !m0.empty(), "pull: into the plugin's Melodee");
        // 2. sound copies, both ways: a PROPHET program past 127, an edited CZ-1 tone, a renamed PROPHET patch
        mine.f->setEngine(1, 19);
        mine.f->applyPreset(1, 150);
        check(felucca::copySound(mine, synth, 1, err) && dumpOf(synth, 1).size() > 3 && dumpOf(synth, 1)[1] == 19 && patch(synth, 1) == patch(mine, 1),
              "send sound: PROPHET program 150 (" + err + ")");
        mine.f->setEngine(2, 15);
        mine.f->applyPreset(2, 7);
        auto t = tone(mine, 2);
        if (t.size() == 288) {
            t[2 * 128] = 'V' & 15; t[2 * 128 + 1] = 'V' >> 4;
            std::vector<uint8_t> q = {0, 2};
            q.insert(q.end(), t.begin(), t.end());
            mine.ask(felucca::frame(felucca::kCzPut, q), 1000);
        }
        check(felucca::copySound(mine, synth, 2, err) && tone(synth, 2) == tone(mine, 2) && !tone(synth, 2).empty(), "send sound: an edited CZ-1 tone (" + err + ")");
        synth.ask(felucca::frame(felucca::kProphetCmd, {3, 1, 'P', 'R', 'O', 'B', 'E'}), 1000);   // renamed on the FM-1
        check(felucca::copySound(synth, mine, 1, err) && patch(mine, 1) == patch(synth, 1), "pull sound: a PROPHET patch renamed on the FM-1 (" + err + ")");
        // 3. the patterns: part 1 on bank 4 with a step, a bank song; to the FM-1 and back
        felucca::Step st;
        st.n = 1; st.note = {64, 0, 0, 0}; st.time = felucca::kNote; st.vel = 100;
        felucca::selectPatternBank(mine, 0, 3);
        mine.ask(felucca::stepWrite(dl, 0, 6, st), 1000);
        felucca::BankRow r1, r2;
        r1.banks = {3, 0, 0, 0}; r1.repeat = 2;
        r2.banks = {0, 1, 0, 0}; r2.repeat = 1;
        mine.ask(felucca::bankChainWrite({r1, r2}), 1000);
        check(felucca::copySequencer(mine, synth, 4, err, progress), "send patterns (" + err + ")");
        std::printf("\n");
        const auto song = felucca::readBankChain(synth);
        const auto s6 = felucca::readStep(synth, 0, 6);
        check(felucca::readPatternBank(synth, 0) == 3 && s6 && s6->note[0] == 64 && song && song->rows == std::vector<felucca::BankRow>{r1, r2},
              "the FM-1: part 1 on pattern 4 with the step, and the bank song");
        // 4. Send: everything (its settings left), then read back and compared
        auto ours = std::optional<felucca::Objects>(felucca::Objects{});
        for (int id = 0; id <= dl.lastObject; ++id) { std::vector<uint8_t> b; if (mine.f->object(id, b)) (*ours)[id] = b; }
        ours->erase(1);
        if (auto it = ours->find(8); it != ours->end() && it->second.empty()) ours->erase(it);
        check(felucca::restore(synth, *ours, progress, err), "\nsend: everything (" + err + ")");
        auto after = felucca::backup(synth, progress, err);
        std::printf("\n");
        int same = 0, differ = 0;
        if (after) for (auto& [id, b] : *ours) { if (after->count(id) && (*after)[id] == b) ++same; else { ++differ; std::printf("  object %d differs\n", id); } }
        check(after && differ == 0, "send: read back, " + juce::String(same) + " objects the same");
        // 5. Live: the plugin's changes reach the FM-1, the FM-1's loads reach the plugin, nothing bounces
        felucca::Mirror mirror(synth, mine);
        check(mirror.start(err), "live: started (" + err + ")");
        std::vector<float> l(256), r(256);
        auto settle = [&](int ms) {
            const auto end = juce::Time::getMillisecondCounter() + juce::uint32(ms);
            while (juce::Time::getMillisecondCounter() < end) {
                for (int k = 0; k < 4; ++k) mine.f->render(l.data(), r.data(), 256);
                if (!mirror.tick(err)) return false;
                pump(20);
            }
            return true;
        };
        const int sel = mine.f->selected();
        mine.f->setParam(sel, 9, (mine.f->param(sel, 9) + 23) % 128);
        settle(1500);
        auto d = dumpOf(synth, sel);
        check(d.size() > 3 + 2 * 9 + 1 && ((int(d[3 + 18]) | int(d[4 + 18]) << 7) - 8192) == mine.f->param(sel, 9), "live: a value to the FM-1");
        mine.f->setEngine(sel, 19);
        mine.f->applyPreset(sel, 170);
        settle(2500);
        d = dumpOf(synth, sel);
        check(d.size() > 3 && d[1] == 19 && (int(d[2]) | ((d.size() - 3) % 2 ? int(d.back()) << 7 : 0)) == 170 && mine.f->presetOf(sel) == 170,
              "live: PROPHET program 170 loaded on the FM-1, not bounced back");
        synth.ask(felucca::frame(felucca::kPreset, {15, 9}), 4000);   // a load on the FM-1 (its RELOAD carried)
        settle(2500);
        check(mine.f->engineOf(sel) == 15 && mine.f->presetOf(sel) == 9, "live: a CZ-1 preset loaded on the FM-1 reaches the plugin");
        felucca::selectPatternBank(synth, sel, 5);
        settle(2500);
        check(felucca::readPatternBank(mine, sel) == 5, "live: a pattern bank switch on the FM-1 reaches the plugin");
        check(err.isEmpty(), "live: nothing stopped (" + err + ")");
        mirror.stop();
        std::printf("%s\n", fails ? "FAILED" : "all ok");
        return fails ? 1 : 0;
    }
    if (cmd == "felucca-live" && argc > 2) {   // the synth's values, steps and selection change; no flash written
        struct Mine : felucca::Endpoint {
            const felucca::Dialect& dialect() const override { return *d; }
            const felucca::Dialect* d = nullptr;
            std::shared_ptr<FeluccaEngine> f;
            std::vector<fm1::Bytes> got;
            std::optional<fm1::Bytes> ask(const fm1::Bytes& q, int) override {
                std::optional<fm1::Bytes> r;
                for (auto& m : f->request(q)) { if (felucca::isPush(felucca::commandOf(m))) got.push_back(m); else if (felucca::commandOf(m) == felucca::commandOf(q) && !r) r = m; }
                return r;
            }
            std::vector<fm1::Bytes> pushes() override {
                for (auto& m : f->takeSysex()) if (felucca::isPush(felucca::commandOf(m))) got.push_back(m);
                std::vector<fm1::Bytes> out; out.swap(got); return out;
            }
        } mine;
        mine.f = std::make_shared<FeluccaEngine>(flavor);
        mine.d = &dl;
        felucca::LinkEndpoint synth(link, dl);
        juce::String err;
        // start alike: the synth's music into the plugin's Felucca (read from the synth only)
        auto o = felucca::backup(synth, {}, err);
        if (!o) { std::printf("backup failed: %s\n", err.toRawUTF8()); return 1; }
        const int rc = mine.f->putObject(0, (*o)[0]);
        std::printf("the synth's music into the plugin's Felucca: rc %d; part 1 engine %s\n", rc, mine.f->engineName(mine.f->engineOf(0)).c_str());
        // every push the FM-1 sends, printed as it arrives (what the mirror then carries is printed below)
        struct Tap : felucca::Endpoint {
            felucca::Endpoint& e;
            explicit Tap(felucca::Endpoint& x) : e(x) {}
            const felucca::Dialect& dialect() const override { return e.dialect(); }
            std::optional<fm1::Bytes> ask(const fm1::Bytes& q, int t) override { return e.ask(q, t); }
            std::vector<fm1::Bytes> pushes() override {
                auto p = e.pushes();
                for (auto& m : p) {
                    std::printf("  FM-1 pushed cmd %d:", felucca::commandOf(m));
                    for (auto b : felucca::argsOf(m)) std::printf(" %d", b);
                    std::printf("\n");
                }
                return p;
            }
        } tap(synth);
        felucca::Mirror mirror(tap, mine);
        if (!mirror.start(err)) { std::printf("mirror did not start: %s\n", err.toRawUTF8()); return 1; }
        {   // the plugin's side to the synth: a value set here, read back from the synth (its RAM only)
            const int sel = mine.f->selected(), id = 9;   // P_LRATE of the selected part
            const int want = (mine.f->param(sel, id) + 17) % 128;
            mine.f->setParam(sel, id, want);
            std::vector<float> l0(256), r0(256);
            int got = -1;
            for (int i = 0; i < 40 && got != want; ++i) {
                for (int k = 0; k < 4; ++k) mine.f->render(l0.data(), r0.data(), 256);
                mirror.tick(err);
                if (auto rep = synth.ask(felucca::frame(felucca::kTrackParam, {uint8_t(sel), uint8_t(id)}), 400)) {
                    auto a = felucca::argsOf(*rep);
                    if (a.size() >= 4) got = (int(a[2]) | int(a[3]) << 7) - 8192;
                }
                pump(20);
            }
            std::printf("plugin -> FM-1: part %d LFO RATE set to %d here, the FM-1 has %d: %s\n", sel + 1, want, got, got == want ? "ok" : "NOT CARRIED");
        }
        std::printf("live for %s s: turn knobs on the FM-1; values seen here are printed\n", argv[2]);
        std::fflush(stdout);
        std::vector<float> l(256), r(256);
        std::vector<int> last(91, -99999);
        int lastBpm = mine.f->global(0);
        auto stepsOf = [&](int t) {   // the plugin's side's steps of a part, as TRACK_STEP gives them
            std::vector<fm1::Bytes> out;
            for (int i = 0; i < 64; ++i) {
                auto m = mine.f->request(felucca::frame(felucca::kTrackStep, {uint8_t(t), uint8_t(i)}));
                for (auto& x : m) if (felucca::commandOf(x) == felucca::kTrackStep) { out.push_back(felucca::argsOf(x)); break; }
            }
            return out;
        };
        std::vector<std::vector<fm1::Bytes>> lastSteps;
        for (int t = 0; t < 4; ++t) lastSteps.push_back(stepsOf(t));
        const auto end = juce::Time::getMillisecondCounter() + juce::uint32(std::atoi(argv[2]) * 1000);
        while (juce::Time::getMillisecondCounter() < end) {
            for (int k = 0; k < 9; ++k) mine.f->render(l.data(), r.data(), 256);   // ~50 ms of its audio
            if (!mirror.tick(err)) { std::printf("mirror stopped: %s\n", err.toRawUTF8()); return 1; }
            const int sel = mine.f->selected();
            for (int id = 0; id < mine.f->paramCount(); ++id) {
                const int v = mine.f->param(sel, id);
                if (last[size_t(id)] != -99999 && v != last[size_t(id)])
                    std::printf("  part %d %s = %d\n", sel + 1, mine.f->paramDesc(sel, id).label.c_str(), v);
                last[size_t(id)] = v;
            }
            if (mine.f->global(0) != lastBpm) { lastBpm = mine.f->global(0); std::printf("  BPM = %d\n", lastBpm); }
            static int every = 0;
            if (++every % 20 == 0)   // about once a second: which steps changed
                for (int t = 0; t < 4; ++t) {
                    auto now = stepsOf(t);
                    for (size_t i = 0; i < now.size() && i < lastSteps[size_t(t)].size(); ++i)
                        if (now[i] != lastSteps[size_t(t)][i]) {
                            const auto& a = now[i];
                            std::printf("  part %d step %zu: %d note(s), first %d\n", t + 1, i + 1, a.size() > 2 ? a[2] : 0, a.size() > 3 ? a[3] : 0);
                        }
                    lastSteps[size_t(t)] = now;
                }
            std::fflush(stdout);
            pump(40);
        }
        mirror.stop();
        if (dl.drumStep >= 0) {   // SLOOP: its drum track's steps, whole (every lane, level, ratchet), on both sides
            int same = 0, differ = 0, hits = 0;
            for (int i = 0; i < 64; ++i) {
                auto theirs = synth.ask(felucca::frame(dl.drumStep, {uint8_t(i)}), 400);
                auto ours = mine.ask(felucca::frame(dl.drumStep, {uint8_t(i)}), 400);
                const auto a = theirs ? felucca::argsOf(*theirs) : fm1::Bytes{}, b = ours ? felucca::argsOf(*ours) : fm1::Bytes{};
                if (a.size() >= 4 && (a[1] | a[2] | a[3])) ++hits;
                if (!a.empty() && a == b) ++same; else { ++differ; std::printf("  drum step %d differs\n", i + 1); }
            }
            std::printf("drum steps: %d the same on both sides (%d with hits), %d different\n", same, hits, differ);
        }
        std::printf("done\n");
        return 0;
    }
   #endif
    if (cmd == "patterns") {   // patterns <first> <last> [file.syx]: read; with a file, keep them as the messages that write them back
        session.pullPatterns(range(0, 1));
        waitIdle(session);
        fm1::Bytes keep;
        for (const auto& [p, pp] : pats) {
            int notes = 0; for (const auto& st : pp.steps) notes += int(st.notes.size());
            int locks = 0;   // (FM-1_096: 8 bytes a step, four (what, value) pairs, what 0xFF = none)
            for (size_t i = 0; i + 1 < pp.locks.size(); i += 2) locks += pp.locks[i] != 0xFF;
            std::printf("  pattern %2d: length %d, tempo %d, gate %d, swing %d, preset %d, %d notes, %d locks\n", p + 1, pp.length, pp.tempo, pp.gate, pp.swing, pp.sound + 1, notes, locks);
            // (FM-1_096: whole steps and every lock message, so the file puts back exactly what was there)
            const auto msgs = pp.readFrom >= 96 ? fm1::seq::encodeWholeSteps(pp, p, true) : fm1::seq::encodeWrite(pp, p, true, !pp.locks.empty());
            for (const auto& m : msgs) keep.insert(keep.end(), m.begin(), m.end());
        }
        if (argc > 4 && !last.failed) {
            juce::File(juce::File::getCurrentWorkingDirectory().getChildFile(argv[4])).replaceWithData(keep.data(), keep.size());
            std::printf("wrote %s (%zu bytes)\n", argv[4], keep.size());
        }
        return last.failed ? 1 : 0;
    }
    if (cmd == "pattern-roundtrip" && argc > 2) {   // pattern-roundtrip <n>: pull pattern n, send it back unchanged, pull it again
        const int pat = std::atoi(argv[2]) - 1;
        session.pullPatterns({pat});
        waitIdle(session);
        if (pats.size() != 1 || last.failed) { std::printf("could not read pattern %d\n", pat + 1); return 1; }
        const auto before = pats[0].second;
        std::printf("read pattern %d (from FM-1_0%d): %zu step bytes kept\n", pat + 1, before.readFrom, before.raw.size());
        session.pushPatterns({{pat, before}}, true);
        waitIdle(session);
        if (last.failed) { std::printf("the send failed: %s\n", last.text.toRawUTF8()); return 1; }
        pats.clear();
        session.pullPatterns({pat});
        waitIdle(session);
        if (pats.size() != 1) { std::printf("could not read it back\n"); return 1; }
        const auto& after = pats[0].second;
        const bool raw = after.raw == before.raw, locks = after.locks == before.locks;
        const bool set = fm1::seq::settingsBytes(after) == fm1::seq::settingsBytes(before);
        std::printf("steps' bytes %s, locks %s, settings %s\n", raw ? "the same" : "DIFFERENT", locks ? "the same" : "DIFFERENT", set ? "the same" : "DIFFERENT");
        return raw && locks && set ? 0 : 1;
    }
    if (cmd == "pattern-edit-roundtrip" && argc > 2) {   // pattern-edit-roundtrip <n>: pull, add a note on step 16, send, pull: as edited?
        const int pat = std::atoi(argv[2]) - 1;
        session.pullPatterns({pat});
        waitIdle(session);
        if (pats.size() != 1 || last.failed) { std::printf("could not read pattern %d\n", pat + 1); return 1; }
        auto edited = pats[0].second;
        edited.steps[15].notes.push_back({60, 77, 0});
        edited = fm1::seq::normalise(edited);
        session.pushPatterns({{pat, edited}}, true);
        waitIdle(session);
        if (last.failed) { std::printf("the send failed: %s\n", last.text.toRawUTF8()); return 1; }
        pats.clear();
        session.pullPatterns({pat});
        waitIdle(session);
        if (pats.size() != 1) { std::printf("could not read it back\n"); return 1; }
        auto back = pats[0].second;
        int differ = 0;
        for (int k = 0; k < fm1::seq::kSteps; ++k) {
            const auto& a = edited.steps[size_t(k)];
            const auto& b2 = back.steps[size_t(k)];
            bool same = a.rate == b2.rate && a.ratchet == b2.ratchet && a.gate == b2.gate && a.chance == b2.chance && a.transpose == b2.transpose
                        && a.accent == b2.accent && a.notes.size() == b2.notes.size();
            for (size_t j = 0; same && j < a.notes.size(); ++j)
                same = a.notes[j].note == b2.notes[j].note && a.notes[j].vel == b2.notes[j].vel && a.notes[j].len == b2.notes[j].len;
            if (!same) { ++differ; std::printf("  step %d differs\n", k + 1); }
        }
        const bool locks = back.locks == edited.locks;
        std::printf("%d steps differ from the edited pattern; locks %s; the bytes %s\n", differ, locks ? "the same" : "DIFFERENT",
                    back.raw == fm1::seq::stepBytes(edited) ? "as built" : "not as built (the synth laid some out its own way)");
        return differ == 0 && locks ? 0 : 1;
    }
    if (cmd == "pattern-put-raw" && argc > 6) {   // pattern-put-raw <n> <steps1-16.bin> <steps17-64.bin> <gset.bin> <locks.bin>: send those bytes back (FM-1_096)
        const int pat = std::atoi(argv[2]) - 1;
        auto load = [&](int i) { juce::MemoryBlock mb; juce::File(argv[i]).loadFileAsData(mb); return fm1::Bytes(static_cast<const uint8_t*>(mb.getData()), static_cast<const uint8_t*>(mb.getData()) + mb.getSize()); };
        auto steps = load(3);
        const auto ext = load(4), gset = load(5), locks = load(6);
        steps.insert(steps.end(), ext.begin(), ext.end());
        if (steps.size() != 2048 || gset.size() != size_t(fm1::seq::kGsetLen) || locks.size() != 512) { std::printf("wrong file sizes\n"); return 1; }
        auto p = fm1::seq::decodePattern(steps, gset, pat, true);
        p.locks = locks;
        p.readFrom = 96;
        session.pushPatterns({{pat, p}}, true);
        waitIdle(session);
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
