#include "Sequencer.h"

#include <algorithm>

Sequencer::Sequencer() {
    chain.fill(-1);
    for (int i = 0; i < kPatterns; ++i) patterns[size_t(i)] = fm1::seq::Pattern{};
    sounding_.fill(0);
}

void Sequencer::prepare(double sampleRate) { sr_ = sampleRate; }

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
    patternSoundRequest = cur_.sound;
}

void Sequencer::fireStep(double atTick) {
    const fm1::seq::Step& s = cur_.steps[size_t(stepIx_)];
    const fm1::seq::StepTime& t = times_.steps[size_t(stepIx_)];
    bool play = s.chance >= 100 || int(rng_() % 100) < s.chance;
    if (play && !s.notes.empty()) {
        int hits = s.ratchet;
        double hitDur = double(t.dur) / hits;
        int gatePct = s.gate > 0 ? s.gate : cur_.gate;
        double gate = std::max(1.0, hitDur * gatePct / 100.0);
        if (s.slide) gate = double(t.dur) + 2.0;   // tie into the next step (or into step 1 when looping)
        for (int h = 0; h < hits; ++h) {
            double on = atTick + h * hitDur;
            for (const auto& n : s.notes) {
                int note = juce::jlimit(0, 127, n.note + cur_.transpose + s.transpose);
                int vel = s.accent ? 127 : n.vel;
                pending_.push_back({on, note, true, vel});
                pending_.push_back({on + gate, note, false, 0});
            }
        }
    }
    playingStep_ = stepIx_;
    nextStepTick_ = atTick + t.dur;
    if (++stepIx_ >= cur_.length) {
        int next = chain[size_t(pat_)];
        int sel = selected.load();
        if (next < 0 || next >= kPatterns) next = sel != pat_ ? sel : pat_;   // the knob picks the next pattern
        startPattern(next, nextStepTick_, false);
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
