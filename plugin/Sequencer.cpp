#include "Sequencer.h"

#include <algorithm>

Sequencer::Sequencer() {
    chain.fill(-1);
    for (int i = 0; i < kPatterns; ++i) patterns[size_t(i)] = fm1::seq::Pattern{};
    sounding_.fill(0);
}

void Sequencer::prepare(double sampleRate) { sr_ = sampleRate; recHeld_.reserve(64); }

void Sequencer::play() { startRequest_ = true; }
void Sequencer::stop() { stopRequest_ = true; }

void Sequencer::startPattern(int pat, double atTick, bool first) {
    pat_ = juce::jlimit(0, kPatterns - 1, pat);
    {
        const juce::SpinLock::ScopedTryLockType l(lock);
        if (l.isLocked()) cur_ = fm1::seq::normalise(patterns[size_t(pat_)]);
        else if (first) cur_ = fm1::seq::Pattern{};   // could not read it: play silence rather than stall
    }
    times_ = fm1::seq::stepTimes(cur_);
    stepIx_ = 0;
    nextStepTick_ = atTick;
    playingPattern_ = pat_;
    if (!syncToHost) tempo_ = cur_.tempo;

}

void Sequencer::fireStep(double atTick) {
    const fm1::seq::Step& s = cur_.steps[size_t(stepIx_)];
    const fm1::seq::StepTime& t = times_.steps[size_t(stepIx_)];
    const int n = cur_.length;
    auto tiedFrom = [&](int i, int note) {   // does step i hold `note` into step i+1, which plays it too?
        if (i + 1 >= n) return false;
        const auto& a = cur_.steps[size_t(i)];
        const auto& b = cur_.steps[size_t(i + 1)];
        bool tied = a.slide;
        for (const auto& x : a.notes) if (x.note == note && x.tie) tied = true;
        bool has = false;
        for (const auto& x : b.notes) if (x.note == note) has = true;
        return tied && has;
    };
    bool play = s.chance >= 100 || int(rng_() % 100) < s.chance;
    if (play && !s.notes.empty()) {
        int hits = s.ratchet;
        double hitDur = double(t.dur) / hits;
        int gatePct = s.gate > 0 ? s.gate : cur_.gate;
        double gate = std::max(1.0, hitDur * gatePct / 100.0);
        for (int h = 0; h < hits; ++h) {
            double on = atTick + h * hitDur;
            for (const auto& nt : s.notes) {
                // a continuation of a tie from the previous step is already sounding
                if (h == 0 && stepIx_ > 0 && tiedFrom(stepIx_ - 1, nt.note)) continue;
                int note = juce::jlimit(0, 127, nt.note + cur_.transpose + s.transpose);
                int vel = s.accent ? 127 : nt.vel;
                double len = gate;
                if (h == hits - 1) {
                    // held through every following step it is tied into
                    double extra = 0.0;
                    int i = stepIx_;
                    bool any = false;
                    while (tiedFrom(i, nt.note)) { any = true; extra += times_.steps[size_t(i)].dur; ++i; }
                    if (any) {
                        const auto& last = cur_.steps[size_t(i)];
                        int lastGate = last.gate > 0 ? last.gate : cur_.gate;
                        len = (double(t.dur) - h * hitDur) + (extra - t.dur) + std::max(1.0, times_.steps[size_t(i)].dur * lastGate / 100.0);
                    } else if (s.slide && stepIx_ + 1 < n) {
                        len = double(t.dur) + 2.0;   // Tie & Slide into a different note: legato overlap
                    }
                }
                pending_.push_back({on, note, true, vel});
                pending_.push_back({on + len, note, false, 0});
            }
        }
    }
    playingStep_ = stepIx_;
    lastStep_ = stepIx_;
    lastStepTick_ = atTick;
    nextStepTick_ = atTick + t.dur;
    if (++stepIx_ >= cur_.length) {
        recTouched_.fill(false);   // a new pass: the first note on a step replaces it again
        int next = chain[size_t(pat_)];
        int sel = selected.load();
        if (next < 0 || next >= kPatterns) next = sel != pat_ ? sel : pat_;   // Repeat, unless another pattern was chosen
        startPattern(next, nextStepTick_, false);
    }
}

int Sequencer::nearestStep(double tick) const {
    if (lastStep_ < 0 || times_.steps.empty()) return 0;
    double dur = times_.steps[size_t(std::min(lastStep_, int(times_.steps.size()) - 1))].dur;
    // a note in the second half of a step goes to the next one
    int step = (tick - lastStepTick_) < dur / 2.0 ? lastStep_ : lastStep_ + 1;
    return step % std::max(1, cur_.length);
}

void Sequencer::recordNoteOn(int note, int vel) {
    if (!playing_ || !recording) return;
    int step = nearestStep(absTick_);
    const juce::SpinLock::ScopedTryLockType l(lock);
    if (!l.isLocked()) return;
    auto& p = patterns[size_t(pat_)];
    auto& st = p.steps[size_t(step)];
    bool replace = !overdub.load() && !recTouched_[size_t(step)];
    if (replace) st.notes.clear();
    recTouched_[size_t(step)] = true;
    st.notes.erase(std::remove_if(st.notes.begin(), st.notes.end(), [note](const fm1::seq::Note& n) { return n.note == note; }), st.notes.end());
    if (int(st.notes.size()) < fm1::seq::kMaxNotes) st.notes.push_back({note, vel, false});
    recHeld_.push_back({note, vel, step});
}

void Sequencer::recordNoteOff(int note) {
    if (!playing_) { recHeld_.clear(); return; }
    auto it = std::find_if(recHeld_.begin(), recHeld_.end(), [note](const Held& h) { return h.note == note; });
    if (it == recHeld_.end()) return;
    Held h = *it;
    recHeld_.erase(it);
    int endStep = nearestStep(absTick_);
    const juce::SpinLock::ScopedTryLockType l(lock);
    if (!l.isLocked()) return;
    auto& p = patterns[size_t(pat_)];
    // a note ends on the step nearest to its release; one held past the pattern's end ends on its last step
    int last = endStep >= h.step ? endStep : p.length - 1;
    for (int i = h.step; i < last; ++i) {
        auto& a = p.steps[size_t(i)];
        for (auto& n : a.notes) if (n.note == note) n.tie = true;
        auto& b = p.steps[size_t(i + 1)];
        bool has = false;
        for (const auto& n : b.notes) if (n.note == note) has = true;
        if (!has && int(b.notes.size()) < fm1::seq::kMaxNotes) b.notes.push_back({note, h.vel, false});
    }
}

void Sequencer::flush(juce::MidiBuffer& out, int upToSample, double tickAtBlockStart, double tps, int blockStart) {
    // emit every pending event whose tick has been reached, note-offs first at equal ticks
    std::stable_sort(pending_.begin(), pending_.end(), [](const Pending& a, const Pending& b) {
        return a.tick < b.tick || (a.tick == b.tick && !a.on && b.on);
    });
    size_t k = 0;
    for (; k < pending_.size(); ++k) {
        const Pending& p = pending_[k];
        int sample = blockStart + int((p.tick - tickAtBlockStart) / tps);
        if (sample > upToSample) break;
        sample = juce::jlimit(blockStart, upToSample, sample);
        if (p.on) { out.addEvent(juce::MidiMessage::noteOn(1, p.note, juce::uint8(p.vel)), sample); ++sounding_[size_t(p.note)]; }
        else { out.addEvent(juce::MidiMessage::noteOff(1, p.note), sample); if (sounding_[size_t(p.note)] > 0) --sounding_[size_t(p.note)]; }
    }
    pending_.erase(pending_.begin(), pending_.begin() + long(k));
}

void Sequencer::panic(juce::MidiBuffer& out) {
    for (int n = 0; n < 128; ++n)
        if (sounding_[size_t(n)] > 0) { out.addEvent(juce::MidiMessage::noteOff(1, n), 0); sounding_[size_t(n)] = 0; }
    pending_.clear();
}

void Sequencer::process(const juce::AudioPlayHead::PositionInfo* pos, int numSamples, juce::MidiBuffer& out) {
    bool sync = syncToHost.load();
    bool hostPlaying = pos && pos->getIsPlaying();
    if (sync && pos) {
        if (auto bpm = pos->getBpm()) if (*bpm > 0) tempo_ = *bpm;
        if (enabled && hostPlaying && !hostWasPlaying_) startRequest_ = true;
        if (!hostPlaying && hostWasPlaying_) stopRequest_ = true;
        hostWasPlaying_ = hostPlaying;
    }
    if (!enabled && playing_) stopRequest_ = true;

    if (stopRequest_.exchange(false)) {
        playing_ = false;
        recording = false;
        recHeld_.clear();
        playingStep_ = -1;
        panic(out);
    }
    if (startRequest_.exchange(false) && enabled) {
        panic(out);
        absTick_ = 0.0;
        if (sync && pos) if (auto ppq = pos->getPpqPosition()) absTick_ = std::max(0.0, *ppq) * fm1::seq::kStepTicksPerQuarter;
        startPattern(selected.load(), absTick_, true);
        // when joining a running host transport, skip the steps already behind us
        while (nextStepTick_ + (times_.steps.empty() ? 1e9 : times_.steps[size_t(stepIx_)].dur) <= absTick_ && !times_.steps.empty()) {
            playingStep_ = stepIx_;
            nextStepTick_ += times_.steps[size_t(stepIx_)].dur;
            if (++stepIx_ >= cur_.length) startPattern(pat_, nextStepTick_, false);
        }
        playing_ = true;
    }
    if (!playing_) return;

    double tps = tempo_ * fm1::seq::kStepTicksPerQuarter / 60.0 / sr_;   // ticks per sample
    double blockStartTick = absTick_;
    double blockEndTick = absTick_ + tps * numSamples;
    int guard = 0;
    while (nextStepTick_ < blockEndTick && guard++ < 4096) fireStep(nextStepTick_);
    // note-offs that fall in this block, plus note-ons that do
    std::vector<Pending> later;
    for (const auto& p : pending_) if (p.tick >= blockEndTick) later.push_back(p);
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(), [&](const Pending& p) { return p.tick >= blockEndTick; }), pending_.end());
    flush(out, numSamples - 1, blockStartTick, tps, 0);
    pending_.insert(pending_.end(), later.begin(), later.end());
    absTick_ = blockEndTick;
}
