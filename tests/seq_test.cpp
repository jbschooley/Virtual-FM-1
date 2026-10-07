// Tests for the plugin's Sequencer and Arpeggiator: event timing against
// Fm1Seq::stepTimes, ratchet, chance, accent, slide, chaining, host sync,
// and the arp modes. No audio device; process() is driven directly.

#include <cstdio>
#include <map>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Arpeggiator.h"
#include "SeqMidi.h"
#include "Sequencer.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, msg) do { if (cond) ++g_pass; else { ++g_fail; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } } while (0)

struct Ev { double sample; int note; bool on; int vel; };

static std::vector<Ev> run(Sequencer& s, double sr, int blocks, int blockSize, const juce::AudioPlayHead::PositionInfo* pos = nullptr) {
    std::vector<Ev> out;
    juce::MidiBuffer buf;
    for (int b = 0; b < blocks; ++b) {
        buf.clear();
        s.process(pos, blockSize, buf);
        for (const auto m : buf) {
            auto msg = m.getMessage();
            if (msg.isNoteOn()) out.push_back({double(b * blockSize + m.samplePosition), msg.getNoteNumber(), true, msg.getVelocity()});
            else if (msg.isNoteOff()) out.push_back({double(b * blockSize + m.samplePosition), msg.getNoteNumber(), false, 0});
        }
    }
    return out;
}

int main() {
    const double sr = 48000.0;
    // ---- a simple 4-step pattern at 120 BPM, 1/16 steps: a step is 0.125 s = 6000 samples
    {
        Sequencer s;
        s.prepare(sr);
        {
            const juce::SpinLock::ScopedLockType l(s.lock);
            auto& p = s.patterns[0];
            p.length = 4; p.rate = 6; p.tempo = 120; p.gate = 50; p.swing = 50;
            for (auto& st : p.steps) st.rate = 6;
            p.steps[0].notes = {{60, 100}};
            p.steps[1].notes = {{62, 90}, {65, 80}};
            p.steps[2].notes = {};
            p.steps[3].notes = {{67, 70}};
        }
        s.enabled = true;
        s.syncToHost = false;
        s.play();
        auto ev = run(s, sr, 100, 480);   // 1 s
        std::map<int, std::vector<double>> ons;
        for (const auto& e : ev) if (e.on) ons[e.note].push_back(e.sample);
        CHECK(ons[60].size() == 2 && ons[67].size() == 2 && ons[62].size() == 2, "each step fires twice in one second (pattern loops)");
        CHECK(!ons[60].empty() && std::abs(ons[60][0] - 0) < 2, "step 1 at the start");
        CHECK(!ons[62].empty() && std::abs(ons[62][0] - 6000) < 2, "step 2 at 0.125 s");
        CHECK(!ons[67].empty() && std::abs(ons[67][0] - 18000) < 2, "step 4 at 0.375 s");
        CHECK(ons[60].size() > 1 && std::abs(ons[60][1] - 24000) < 2, "loops after 4 steps");
        double off60 = -1;
        for (const auto& e : ev) if (!e.on && e.note == 60) { off60 = e.sample; break; }
        CHECK(std::abs(off60 - 3000) < 2, "gate 50% = note off after half a step");
        s.stop();
        auto tail = run(s, sr, 2, 480);
        bool anyOn = false; for (const auto& e : tail) anyOn = anyOn || e.on;
        CHECK(!anyOn, "nothing plays after stop");
    }
    // ---- swing, ratchet, accent, chance, slide, transpose
    {
        Sequencer s;
        s.prepare(sr);
        {
            const juce::SpinLock::ScopedLockType l(s.lock);
            auto& p = s.patterns[0];
            p.length = 4; p.rate = 6; p.tempo = 120; p.gate = 50; p.swing = 66; p.transpose = 12;
            for (auto& st : p.steps) st.rate = 6;
            p.steps[0].notes = {{60, 100}}; p.steps[0].ratchet = 2;
            p.steps[1].notes = {{62, 50}}; p.steps[1].accent = true;
            p.steps[2].notes = {{64, 100}}; p.steps[2].chance = 5;
            p.steps[3].notes = {{65, 100, 1}}; p.steps[3].transpose = -12;   // (held into the next step: none after the last)
        }
        s.enabled = true; s.syncToHost = false; s.play();
        auto ev = run(s, sr, 100, 480);
        // swing 66: first step lasts 66% of 6000 = 3960 samples (ticks 48 * 66 / 100 = 31 ticks = 3875 samples), second 17 ticks
        int t1 = 48 * 66 / 100;  // 31 ticks
        double stepA = t1 * (sr * 60.0 / 120.0 / 96.0), stepB = (48 - t1) * (sr * 60.0 / 120.0 / 96.0);
        std::vector<double> on72; int accentVel = 0; int count64 = 0; double on65 = -1, off65 = -1, on62 = -1;
        for (const auto& e : ev) {
            if (e.on && e.note == 72) on72.push_back(e.sample);
            if (e.on && e.note == 74) { accentVel = e.vel; on62 = on62 < 0 ? e.sample : on62; }
            if (e.on && e.note == 76) ++count64;
            if (e.note == 65 && e.on && on65 < 0) on65 = e.sample;
            if (e.note == 65 && !e.on && off65 < 0 && on65 >= 0) off65 = e.sample;
        }
        CHECK(on72.size() >= 2 && std::abs(on72[1] - on72[0] - stepA / 2) < 2, "ratchet 2 splits the step in halves");
        CHECK(std::abs(on62 - stepA) < 2, "swing delays the second step");
        CHECK(accentVel == 127, "accent plays at full velocity");
        CHECK(count64 <= 2, "chance 5% mostly skips (expected 0-2 of 4 loops)");
        CHECK(on65 >= 0 && off65 - on65 < stepB, "Tie & Slide does nothing on the last step (greyed out on the FM-1)");
        CHECK(on72.size() > 0 && on62 >= 0, "pattern transpose +12 and step transpose -12 applied");
    }
    // ---- chaining and host sync
    {
        Sequencer s;
        s.prepare(sr);
        {
            const juce::SpinLock::ScopedLockType l(s.lock);
            for (int i = 0; i < 2; ++i) { auto& p = s.patterns[size_t(i)]; p.length = 2; p.rate = 6; p.tempo = 120; for (auto& st : p.steps) st.rate = 6; }
            s.patterns[0].steps[0].notes = {{40, 100}};
            s.patterns[1].steps[0].notes = {{50, 100}};
            s.chain[0] = 1; s.chain[1] = 0;
        }
        s.enabled = true; s.syncToHost = true;
        juce::AudioPlayHead::PositionInfo pos;
        pos.setBpm(240.0); pos.setIsPlaying(true); pos.setPpqPosition(0.0);
        auto ev = run(s, sr, 100, 480, &pos);   // at 240 BPM a 1/16 step is 3000 samples; a 2-step pattern 6000
        std::vector<int> order;
        for (const auto& e : ev) if (e.on) order.push_back(e.note);
        CHECK(order.size() >= 4 && order[0] == 40 && order[1] == 50 && order[2] == 40 && order[3] == 50, "chain alternates patterns");
        CHECK(std::abs(s.currentTempo() - 240.0) < 0.01, "tempo follows the host");
        double first50 = -1; for (const auto& e : ev) if (e.on && e.note == 50) { first50 = e.sample; break; }
        CHECK(std::abs(first50 - 6000) < 2, "host tempo sets the step length");
        pos.setIsPlaying(false);
        auto after = run(s, sr, 10, 480, &pos);
        bool anyOn = false; for (const auto& e : after) anyOn = anyOn || e.on;
        CHECK(!anyOn, "stops when the host stops");
    }
    // ---- ties: a note tied across three steps sounds once, held; real-time recording
    {
        Sequencer s;
        s.prepare(sr);
        {
            const juce::SpinLock::ScopedLockType l(s.lock);
            auto& p = s.patterns[0];
            p.length = 8; p.rate = 6; p.tempo = 120; p.gate = 50; p.swing = 50;
            for (auto& st : p.steps) st.rate = 6;
            p.steps[0].notes = {{60, 100, 2}};                 // held two steps past its own
            p.steps[4].notes = {{64, 90, 1}, {67, 90, 1}};     // Tie & Slide: both held into step 6
            p.steps[5].notes = {{69, 90, 0}};
        }
        s.enabled = true; s.syncToHost = false; s.play();
        auto ev = run(s, sr, 100, 480);    // 1 s = one pass of 8 x 0.125 s
        int on60 = 0, on64 = 0; double start60 = -1, end60 = -1, end67 = -1, start69 = -1;
        for (const auto& e : ev) {
            if (e.note == 60 && e.on && e.sample < 48000) { ++on60; if (start60 < 0) start60 = e.sample; }
            if (e.note == 60 && !e.on && end60 < 0) end60 = e.sample;
            if (e.note == 64 && e.on && e.sample < 48000) ++on64;
            if (e.note == 67 && !e.on && end67 < 0) end67 = e.sample;
            if (e.note == 69 && e.on && start69 < 0) start69 = e.sample;
        }
        CHECK(on60 == 1, "a note held over three steps is played once");
        CHECK(std::abs((end60 - start60) - (2 * 6000 + 3000)) < 4, "and held for two steps plus the third step's gate");
        CHECK(on64 == 1, "Tie & Slide: a held note is not retriggered");
        CHECK(end67 > start69, "Tie & Slide: a note that changes overlaps into the next step (legato)");

        // real-time recording: a note played half way through step 2 lands on step 3; held 2 steps it ties
        Sequencer r;
        r.prepare(sr);
        { const juce::SpinLock::ScopedLockType l(r.lock); auto& p = r.patterns[0]; p.length = 8; p.rate = 6; p.tempo = 120; for (auto& st : p.steps) st.rate = 6; }
        r.enabled = true; r.syncToHost = false; r.play();
        juce::MidiBuffer buf;
        auto block = [&](int n) { for (int i = 0; i < n; ++i) { buf.clear(); r.process(nullptr, 480, buf); } };
        r.recording = true;
        block(1);                      // start: step 1 fired
        block(12);                     // 0.13 s -> within step 2 (6000..12000 samples)
        block(6);                      // 9120 samples: 3120 into step 2, past its half
        r.recordNoteOn(72, 99);
        block(25);                     // hold ~0.25 s = 2 steps
        r.recordNoteOff(72);
        {
            const juce::SpinLock::ScopedLockType l(r.lock);
            const auto& p = r.patterns[0];
            bool on3 = false, on4 = false, on5 = false;
            int len3 = -1;
            for (const auto& n : p.steps[2].notes) if (n.note == 72) { on3 = n.vel == 99; len3 = n.len; }
            for (const auto& n : p.steps[3].notes) if (n.note == 72) on4 = true;
            for (const auto& n : p.steps[4].notes) if (n.note == 72) on5 = true;
            CHECK(on3, "recorded note lands on the nearest step (3) with its velocity");
            CHECK(len3 == 2 && !on4 && !on5, "a held note is one note, its length the steps it spans past its own");
            CHECK(p.steps[1].notes.empty(), "nothing recorded on step 2");
        }
    }

    // ---- arpeggiator
    {
        Arpeggiator a;
        a.prepare(sr);
        a.enabled = true; a.syncToHost = false; a.tempo = 120; a.rate = 6; a.gate = 50; a.octaves = 2; a.mode = Arpeggiator::Up;
        a.noteOn(60, 100); a.noteOn(64, 100); a.noteOn(67, 100);
        std::vector<int> order;
        juce::MidiBuffer buf;
        for (int b = 0; b < 100; ++b) { buf.clear(); a.process(nullptr, 480, buf); for (const auto m : buf) if (m.getMessage().isNoteOn()) order.push_back(m.getMessage().getNoteNumber()); }
        CHECK(order.size() == 8, "8 arp notes in one second at 1/16");
        std::vector<int> want = {60, 64, 67, 72, 76, 79, 60, 64};
        CHECK(order == want, "Up over two octaves");
        a.mode = Arpeggiator::Down; a.noteOff(60); a.noteOn(60, 100);   // force rebuild
        order.clear();
        for (int b = 0; b < 60; ++b) { buf.clear(); a.process(nullptr, 480, buf); for (const auto m : buf) if (m.getMessage().isNoteOn()) order.push_back(m.getMessage().getNoteNumber()); }
        CHECK(order.size() >= 3 && order[0] == 79 && order[1] == 76 && order[2] == 72, "Down starts from the top");
        a.noteOff(60); a.noteOff(64); a.noteOff(67);
        buf.clear(); a.process(nullptr, 480, buf);
        int offs = 0, ons = 0; for (const auto m : buf) { if (m.getMessage().isNoteOff()) ++offs; if (m.getMessage().isNoteOn()) ++ons; }
        buf.clear(); a.process(nullptr, 4800, buf);
        for (const auto m : buf) if (m.getMessage().isNoteOn()) ++ons;
        CHECK(ons == 0, "silent once every key is released");
        // latch keeps playing after release
        a.latch = true; a.mode = Arpeggiator::Up; a.octaves = 1;
        a.noteOn(48, 90); a.noteOff(48);
        order.clear();
        for (int b = 0; b < 30; ++b) { buf.clear(); a.process(nullptr, 480, buf); for (const auto m : buf) if (m.getMessage().isNoteOn()) order.push_back(m.getMessage().getNoteNumber()); }
        CHECK(!order.empty() && order[0] == 48, "latch keeps the arp running after keys are released");
    }
    // ---- MIDI files ----
    {
        using namespace fm1::seq;
        auto roundTrip = [](const juce::MidiFile& mf) {   // through bytes, as a file would go
            juce::MemoryOutputStream os; mf.writeTo(os, 1);
            juce::MemoryInputStream in(os.getData(), os.getDataSize(), false);
            juce::MidiFile back; back.readFrom(in); return back;
        };
        // notes, velocities and ties come back on the same steps, at gate 50 and 30
        for (int gate : {50, 30}) {
            Pattern p; p.length = 16; p.rate = 6; p.gate = gate; p.tempo = 97;
            for (auto& st : p.steps) st.rate = p.rate;
            p.steps[0].notes = {{60, 90, false}, {64, 114, false}};
            p.steps[3].notes = {{67, 70, 2}};
            p.steps[9].notes = {{48, 127, false}};
            p.steps[15].notes = {{72, 1, false}};
            auto mf = roundTrip(seqmidi::toMidi({{2, p}}));
            CHECK(mf.getTimeFormat() == kStepTicksPerQuarter && mf.getNumTracks() == 1, "MIDI export: 96 ticks per quarter, one track");
            Pattern base; base.rate = 6;
            auto r = seqmidi::fromMidi(mf, base);
            bool same = r.pattern.length == 16 && r.pattern.tempo == 97;
            for (int i = 0; i < 16; ++i) {
                const auto& a = p.steps[size_t(i)].notes; const auto& b = r.pattern.steps[size_t(i)].notes;
                if (a.size() != b.size()) { same = false; std::printf("  gate %d step %d: %zu notes, back %zu\n", gate, i + 1, a.size(), b.size()); continue; }
                for (size_t k = 0; k < a.size(); ++k)
                    if (a[k].note != b[k].note || a[k].vel != b[k].vel || a[k].len != b[k].len) {
                        same = false; std::printf("  gate %d step %d: %d/%d/%d back %d/%d/%d\n", gate, i + 1, a[k].note, a[k].vel, a[k].len, b[k].note, b[k].vel, b[k].len);
                    }
            }
            CHECK(same && r.notes == 5 && r.tempoFromFile, gate == 50 ? "MIDI round trip at gate 50: notes, velocities, ties, length, tempo" : "MIDI round trip at gate 30");
        }
        // what plays: ratchet, accent, transpose; a step's chance does not drop it
        {
            Pattern p; p.length = 4; p.rate = 6; p.transpose = 2;
            for (auto& st : p.steps) st.rate = p.rate;
            p.steps[0].notes = {{60, 80, false}}; p.steps[0].ratchet = 3;
            p.steps[1].notes = {{62, 80, false}}; p.steps[1].accent = true; p.steps[1].transpose = 12;
            p.steps[2].notes = {{64, 80, false}}; p.steps[2].chance = 5;
            auto mf = roundTrip(seqmidi::toMidi({{0, p}}));
            std::vector<std::pair<int, int>> ons;
            const auto* tr = mf.getTrack(0);
            for (int i = 0; i < tr->getNumEvents(); ++i) {
                const auto& m = tr->getEventPointer(i)->message;
                if (m.isNoteOn()) ons.push_back({m.getNoteNumber(), m.getVelocity()});
            }
            int at62 = 0, at76 = 0, at66 = 0; bool accent = false;
            for (auto [n, v] : ons) { if (n == 62) ++at62; if (n == 76) { ++at76; accent = v == 127; } if (n == 66) ++at66; }
            CHECK(ons.size() == 5 && at62 == 3, "MIDI export: ratchet 3 plays three notes (transposed by the pattern)");
            CHECK(at76 == 1 && accent, "MIDI export: accent at 127, step transpose added");
            CHECK(at66 == 1, "MIDI export: a step with low chance is still written");
        }
        // all patterns: one track each, named
        {
            Pattern a, b; a.steps[0].notes = {{60, 100, false}}; b.steps[0].notes = {{61, 100, false}};
            auto mf = roundTrip(seqmidi::toMidi({{0, a}, {5, b}}));
            bool named = false;
            const auto* tr = mf.getTrack(1);
            for (int i = 0; i < tr->getNumEvents(); ++i)
                if (tr->getEventPointer(i)->message.isTrackNameEvent() && tr->getEventPointer(i)->message.getTextFromTextMetaEvent() == "Pattern 6") named = true;
            CHECK(mf.getNumTracks() == 2 && named, "MIDI export: one named track per pattern");
        }
        // import: grid from the pattern's note value, notes past step 64 left out
        {
            juce::MidiMessageSequence seq;
            for (int i = 0; i < 70; ++i) {   // eighth notes at 480 ticks per quarter
                seq.addEvent(juce::MidiMessage::noteOn(1, 60 + i % 12, (juce::uint8) 100), i * 240.0);
                seq.addEvent(juce::MidiMessage::noteOff(1, 60 + i % 12), i * 240.0 + 120.0);
            }
            juce::MidiFile mf; mf.setTicksPerQuarterNote(480); mf.addTrack(seq);
            Pattern base; base.rate = 4;   // 1/8
            auto r = seqmidi::fromMidi(roundTrip(mf), base);
            CHECK(r.notes == 64 && r.pastEnd == 6 && r.pattern.length == 64, "MIDI import: an eighth-note line on a 1/8 grid, 64 steps, the rest reported");
            CHECK(r.pattern.steps[1].notes.size() == 1 && r.pattern.steps[1].notes[0].note == 61 && r.pattern.steps[1].notes[0].len == 0, "MIDI import: notes land on their steps");
        }
    }

    // ---- FM-1_096's step bytes: what baud girl's own code made and reads (tests/fm1-096-steps.txt)
    {
        using namespace fm1::seq;
        juce::StringArray lines;
        juce::File(FM1_096_STEPS).readLines(lines);
        juce::String hex;
        juce::StringArray want;
        for (const auto& l : lines) {
            if (l.startsWith("#") || l.trim().isEmpty()) continue;
            if (l.startsWith("S ")) want.add(l.trim()); else hex = l.trim();
        }
        juce::MemoryBlock mb;
        mb.loadFromHexString(hex);
        CHECK(mb.getSize() == 2048 && want.size() == 8, "the vector file reads");
        fm1::Bytes bytes(static_cast<const uint8_t*>(mb.getData()), static_cast<const uint8_t*>(mb.getData()) + mb.getSize());
        fm1::Bytes gset(size_t(kGsetLen), 0);
        gset[98] = 32; gset[50] = 6; gset[18] = 50; gset[34] = 50;
        auto lineOf = [](const Pattern& p) {
            juce::StringArray out;
            for (int k = 0; k < kSteps; ++k) {
                const Step& s = p.steps[size_t(k)];
                if (s.notes.empty() && !s.accent && s.ratchet == 1 && s.gate == 0 && s.chance == 100 && s.transpose == 0) continue;
                juce::String l = "S " + juce::String(k) + " acc=" + juce::String(int(s.accent)) + " rat=" + juce::String(s.ratchet) + " gate=" + juce::String(s.gate)
                               + " chance=" + juce::String(s.chance) + " tr=" + juce::String(s.transpose) + " notes=";
                for (const auto& n : s.notes) l << n.note << "/" << n.vel << "/" << n.len << " ";
                out.add(l.trim());
            }
            return out;
        };
        const auto p = decodePattern(bytes, gset, 0, true);
        const auto got = lineOf(p);
        CHECK(got == want, "her bytes read as she reads them: accents, ratchets, options, note lengths");
        if (got != want) for (const auto& l : got) std::printf("  got %s\n", l.toRawUTF8());
        CHECK(stepBytes(p) == bytes, "an unchanged pattern goes back as the very bytes read");
        Pattern edited = p;
        edited.steps[3].notes = {{50, 99, 0}};
        const auto rebuilt = stepBytes(edited);
        CHECK(rebuilt != bytes, "an edited one is laid out again");
        edited = normalise(edited);
        auto again = decodePattern(rebuilt, gset, 0, true);
        CHECK(lineOf(again) == lineOf(edited), "and reads back as edited");
        const auto msgs = encodeWholeSteps(p, 2, true);
        CHECK(msgs.size() == 24 && msgs[0].size() == 162 && msgs[0][4] == 0x22 && msgs[23][4] == 0x21 && msgs[23][7] == 1 && msgs[22][7] == 0,
              "whole steps: 16 messages of 162 bytes, then 8 lock messages, the last one saving");
        CHECK(replyArg(msgs[5]) == (2u | 5u << 8 | 0x4000u) && replyArg(msgs[17]) == (2u | 1u << 8 | 0x8000u), "each names the reply it expects");
        Pattern chained = p;
        chained.chain = 6; chained.repeats = 8;
        CHECK(settingsBytes(chained)[4] == uint8_t(128 | 5 << 4 | 6), "the Chain byte: 128 + repeats index << 4 + the next pattern");
        fm1::Bytes g2 = gset;
        g2[118] = uint8_t(128 | 5 << 4 | 6);
        const auto cp = decodePattern(bytes, g2, 0, true);
        CHECK(cp.chain == 6 && cp.repeats == 8, "and read back");

        // one pitch played again while it still sounds: both keep the later end, as the synth's entry
        // leaves them, and the bytes read go back unchanged
        Pattern o;
        o.length = 16;
        o.steps[0].notes = {{60, 100, 4}};
        o.steps[2].notes = {{60, 90, 2}};
        o.raw = stepBytes(o);
        CHECK(stepBytes(o) == o.raw, "a held note entered again shares its end: its bytes go back as read");
        const auto on = normalise(o);
        CHECK(on.steps[0].notes[0].len == 4 && on.steps[2].notes[0].len == 2, "and both keep that end");
        Pattern cut;
        cut.length = 16;
        cut.steps[0].notes = {{60, 100, 4}};
        cut.steps[2].notes = {{60, 90, 0}};
        const auto cn = normalise(cut);
        CHECK(cn.steps[0].notes[0].len == 2 && cn.steps[2].notes[0].len == 0, "one that ends earlier ends the held one there too (the first end found)");
        // a step keeps at most six note ends once it has its own options: the seventh moves on
        Pattern full;
        full.length = 16;
        for (int k = 0; k < 7; ++k) full.steps[4].notes.push_back({60 + k, 100, 0});
        full.steps[4].gate = 70;
        const auto fit = fitToSynth(full);
        int moved = 0;
        for (const auto& n : fit.steps[4].notes) moved += n.len != 0;
        CHECK(moved == 1 && fit.raw == stepBytes(fit), "fitToSynth: the seventh end moves on, and the pattern is what its bytes say");
    }

    // ---- a chain's Repeats: the pattern plays that many times before the next
    {
        Sequencer s;
        s.prepare(sr);
        {
            const juce::SpinLock::ScopedLockType l(s.lock);
            for (int i = 0; i < 2; ++i) { auto& p = s.patterns[size_t(i)]; p.length = 1; p.rate = 6; p.tempo = 120; for (auto& st : p.steps) st.rate = 6; }
            s.patterns[0].steps[0].notes = {{40, 100}};
            s.patterns[1].steps[0].notes = {{50, 100}};
            s.patterns[0].repeats = 3;
            s.chain[0] = 1; s.chain[1] = 0;
        }
        s.enabled = true; s.syncToHost = false; s.play();
        auto ev = run(s, sr, 100, 480);
        std::vector<int> order;
        for (const auto& e : ev) if (e.on) order.push_back(e.note);
        CHECK(order.size() >= 8 && order[0] == 40 && order[1] == 40 && order[2] == 40 && order[3] == 50 && order[4] == 40 && order[7] == 50,
              "Repeats 3: pattern 1 three times, then pattern 2, again");
    }

    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
