#include "SeqMidi.h"

#include <algorithm>
#include <cmath>

#include "Sequencer.h"

namespace seqmidi {

using namespace fm1::seq;

juce::MidiFile toMidi(const std::vector<Entry>& patterns) {
    juce::MidiFile file;
    file.setTicksPerQuarterNote(kStepTicksPerQuarter);
    bool first = true;
    for (const auto& e : patterns) {
        const Pattern p = normalise(e.pattern);
        const Times t = stepTimes(p);
        juce::MidiMessageSequence seq;
        seq.addEvent(juce::MidiMessage::textMetaEvent(3, "Pattern " + juce::String(e.index + 1)), 0);
        if (first) {
            seq.addEvent(juce::MidiMessage::tempoMetaEvent(int(std::lround(60000000.0 / p.tempo))), 0);
            seq.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4, 4), 0);
            first = false;
        }
        std::vector<Sequencer::Event> ev;
        for (int i = 0; i < p.length; ++i) Sequencer::stepEvents(p, t, i, t.steps[size_t(i)].start, ev);
        for (const auto& x : ev) {
            double tick = std::round(x.tick);
            auto m = x.on ? juce::MidiMessage::noteOn(1, x.note, juce::uint8(juce::jlimit(1, 127, x.vel)))
                          : juce::MidiMessage::noteOff(1, x.note);
            seq.addEvent(m, tick);
        }
        seq.addEvent(juce::MidiMessage::endOfTrack(), double(t.total));
        seq.sort();
        seq.updateMatchedPairs();
        file.addTrack(seq);
    }
    return file;
}

ImportResult fromMidi(const juce::MidiFile& source, const Pattern& base) {
    ImportResult r;
    r.pattern = normalise(base);
    Pattern& p = r.pattern;
    for (auto& s : p.steps) { s = Step{}; s.rate = p.rate; }

    // positions in beats, from the file's ticks (tempo changes do not move notes on the grid)
    const short format = source.getTimeFormat();
    if (format <= 0) return r;   // SMPTE time: not a musical grid
    const double tpq = format;
    juce::MidiMessageSequence tempos;
    source.findAllTempoEvents(tempos);
    if (tempos.getNumEvents() > 0) {
        double spq = tempos.getEventPointer(0)->message.getTempoSecondsPerQuarterNote();
        if (spq > 0) { p.tempo = juce::jlimit(30, 300, int(std::lround(60.0 / spq))); r.tempoFromFile = true; }
    }
    const double stepQuarters = double(kValueTicks[p.rate]) / kTicksPerQuarter;   // the grid, in beats

    struct In { double start, end; int note, vel; };
    std::vector<In> notes;
    for (int t = 0; t < source.getNumTracks(); ++t) {
        juce::MidiMessageSequence seq(*source.getTrack(t));
        seq.updateMatchedPairs();
        for (int i = 0; i < seq.getNumEvents(); ++i) {
            const auto* e = seq.getEventPointer(i);
            if (!e->message.isNoteOn()) continue;
            double on = e->message.getTimeStamp() / tpq;
            double off = e->noteOffObject ? e->noteOffObject->message.getTimeStamp() / tpq : on + stepQuarters;
            notes.push_back({on, off, e->message.getNoteNumber(), e->message.getVelocity()});
        }
    }
    std::sort(notes.begin(), notes.end(), [](const In& a, const In& b) {
        if (a.start < b.start) return true;
        if (b.start < a.start) return false;
        return a.note < b.note;
    });

    int lastStep = -1;
    for (const auto& n : notes) {
        int step = int(std::lround(n.start / stepQuarters));
        if (step >= kSteps) { ++r.pastEnd; continue; }
        // the steps it covers: every step it sounds at least a quarter of
        int endStep = std::max(step, int(std::ceil(n.end / stepQuarters - 0.25)) - 1);
        endStep = std::min(endStep, kSteps - 1);
        auto& first = p.steps[size_t(step)].notes;
        bool dup = std::any_of(first.begin(), first.end(), [&](const Note& x) { return x.note == n.note; });
        if (dup) continue;
        if (int(first.size()) >= kMaxNotes) { ++r.crowded; continue; }
        // one note held over those steps (its length), until the same note starts again
        int len = endStep - step;
        for (int i = step + 1; i <= endStep; ++i) {
            const auto& a = p.steps[size_t(i)].notes;
            if (std::any_of(a.begin(), a.end(), [&](const Note& x) { return x.note == n.note; })) { len = i - step - 1; break; }
        }
        first.push_back({n.note, juce::jlimit(1, 127, n.vel), len});
        lastStep = std::max(lastStep, endStep);
        ++r.notes;
    }
    if (lastStep >= 0) p.length = std::min(kSteps, ((lastStep + 1 + 3) / 4) * 4);
    return r;
}

}  // namespace seqmidi
