#include "Arpeggiator.h"

#include <algorithm>

#include "Fm1Seq.h"

const char* const Arpeggiator::kModeNames[kModes] = {"Up", "Down", "Inclusive", "Exclusive", "Random", "Order", "Repeat"};

void Arpeggiator::prepare(double sampleRate) { sr_ = sampleRate; sounding_.fill(0); }

void Arpeggiator::noteOn(int note, int vel) {
    if (!anyKeyDown_ && latch) held_.clear();   // a new chord after latch release
    bool down = false;
    for (const auto& h : held_) if (h.note == note) down = true;
    if (!down) held_.push_back({note, vel});
    anyKeyDown_ = true;
    dirty_ = true;
}

void Arpeggiator::noteOff(int note) {
    if (latch) {
        // keys up: keep playing; remember that nothing is held so the next key starts fresh
        bool any = false;
        for (auto& h : held_) if (h.note == note) h.vel = -h.vel;   // mark released
        for (auto& h : held_) any = any || h.vel > 0;
        anyKeyDown_ = any;
        return;
    }
    held_.erase(std::remove_if(held_.begin(), held_.end(), [note](const Held& h) { return h.note == note; }), held_.end());
    anyKeyDown_ = !held_.empty();
    dirty_ = true;
}

void Arpeggiator::allOff() { held_.clear(); anyKeyDown_ = false; dirty_ = true; }

void Arpeggiator::rebuild() {
    dirty_ = false;
    sequence_.clear();
    std::vector<Held> base;
    for (const auto& h : held_) base.push_back({h.note, std::abs(h.vel)});
    if (base.empty()) return;
    int oct = juce::jlimit(1, 4, octaves.load());
    std::vector<Held> asc = base;
    std::sort(asc.begin(), asc.end(), [](const Held& a, const Held& b) { return a.note < b.note; });
    auto spread = [oct](const std::vector<Held>& src) {
        std::vector<Held> out;
        for (int o = 0; o < oct; ++o)
            for (const auto& h : src) if (h.note + 12 * o < 128) out.push_back({h.note + 12 * o, h.vel});
        return out;
    };
    switch (mode.load()) {
        case Up: sequence_ = spread(asc); break;
        case Down: { sequence_ = spread(asc); std::reverse(sequence_.begin(), sequence_.end()); break; }
        case Inclusive: { auto up = spread(asc); sequence_ = up; std::reverse(up.begin(), up.end()); sequence_.insert(sequence_.end(), up.begin(), up.end()); break; }
        case Exclusive: { auto up = spread(asc); sequence_ = up; if (up.size() > 2) sequence_.insert(sequence_.end(), up.rbegin() + 1, up.rend() - 1); break; }
        case Random: sequence_ = spread(asc); break;
        case Order: sequence_ = spread(base); break;
        case Repeat: sequence_ = spread(base); break;   // every step plays the whole chord (below)
        default: sequence_ = spread(asc); break;
    }
    stepIx_ = 0;
}

void Arpeggiator::panic(juce::MidiBuffer& out) {
    for (int n = 0; n < 128; ++n)
        if (sounding_[size_t(n)] > 0) { out.addEvent(juce::MidiMessage::noteOff(1, n), 0); sounding_[size_t(n)] = 0; }
    pending_.clear();
}

void Arpeggiator::process(const juce::AudioPlayHead::PositionInfo* pos, int numSamples, juce::MidiBuffer& out) {
    if (!enabled) { if (running_) { running_ = false; panic(out); } held_.clear(); anyKeyDown_ = false; sequence_.clear(); return; }
    if (dirty_) rebuild();
    double bpm = double(tempo.load());
    if (syncToHost && pos) if (auto b = pos->getBpm()) if (*b > 0) bpm = *b;
    double tps = bpm * fm1::seq::kStepTicksPerQuarter / 60.0 / sr_;
    double blockStart = absTick_, blockEnd = absTick_ + tps * numSamples;

    if (sequence_.empty()) {
        if (running_) { running_ = false; panic(out); parity_ = 0; }
        absTick_ = blockEnd;
        return;
    }
    if (!running_) {
        running_ = true;
        nextTick_ = blockStart;
        stepIx_ = 0;
        parity_ = 0;
        // when synced to a playing host, land on the grid of the note value
        if (syncToHost && pos && pos->getIsPlaying()) if (auto ppq = pos->getPpqPosition()) {
            double ticks = fm1::seq::kValueTicks[juce::jlimit(0, 9, rate.load())];
            double now = std::max(0.0, *ppq) * fm1::seq::kStepTicksPerQuarter;
            double off = std::fmod(now, ticks);
            nextTick_ = blockStart + (off > 0.5 ? ticks - off : 0.0);
        }
    }
    int guard = 0;
    while (nextTick_ < blockEnd && guard++ < 1024) {
        int ticks = fm1::seq::kValueTicks[juce::jlimit(0, 9, rate.load())];
        int sw = juce::jlimit(50, 75, swing.load()) * ticks / 100;
        int dur = parity_ ? ticks - sw : sw;
        parity_ ^= 1;
        double g = std::max(1.0, dur * juce::jlimit(0, 100, gate.load()) / 100.0);
        if (mode == Repeat) {
            for (const auto& h : sequence_) { pending_.push_back({nextTick_, h.note, true, h.vel}); pending_.push_back({nextTick_ + g, h.note, false, 0}); }
        } else {
            int ix = mode == Random ? int(rng_() % sequence_.size()) : stepIx_;
            const Held& h = sequence_[size_t(ix)];
            pending_.push_back({nextTick_, h.note, true, h.vel});
            pending_.push_back({nextTick_ + g, h.note, false, 0});
            stepIx_ = (stepIx_ + 1) % int(sequence_.size());
        }
        nextTick_ += dur;
    }
    std::stable_sort(pending_.begin(), pending_.end(), [](const Pending& a, const Pending& b) { return a.tick < b.tick || (a.tick == b.tick && !a.on && b.on); });
    size_t k = 0;
    for (; k < pending_.size(); ++k) {
        const Pending& p = pending_[k];
        if (p.tick >= blockEnd) break;
        int sample = juce::jlimit(0, numSamples - 1, int((p.tick - blockStart) / tps));
        if (p.on) { out.addEvent(juce::MidiMessage::noteOn(1, p.note, juce::uint8(p.vel)), sample); ++sounding_[size_t(p.note)]; }
        else { out.addEvent(juce::MidiMessage::noteOff(1, p.note), sample); if (sounding_[size_t(p.note)] > 0) --sounding_[size_t(p.note)]; }
    }
    pending_.erase(pending_.begin(), pending_.begin() + long(k));
    absTick_ = blockEnd;
}
