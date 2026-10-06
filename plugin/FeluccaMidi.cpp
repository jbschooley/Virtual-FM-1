#include "FeluccaMidi.h"

#include <algorithm>
#include <cmath>

namespace felmidi {

namespace {
constexpr int kTpq = 480;
// eng_drum.c DRUM_LANE_NOTE; SLOOP's drums.c LANE_NOTE
const std::vector<int> kFeluccaLanes = {36, 38, 39, 42, 46, 45, 37, 56};
const std::vector<int> kSloopLanes = {36, 35, 38, 39, 42, 46, 44, 37, 40, 43, 48, 49, 51, 70, 63, 56};
int levelVelocity(int level, int vel) { static const int v[4] = {0, 42, 72, 127}; return level & 3 ? v[level & 3] : vel; }
int velocityLevel(int vel) { return vel < 56 ? 1 : vel < 88 ? 2 : vel < 116 ? 0 : 3; }   // SLOOP's vel_lvl
}  // namespace

const std::vector<int>& laneNotes(bool sloop) { return sloop ? kSloopLanes : kFeluccaLanes; }

double divQuarters(const std::string& n) {
    if (n == "1/4") return 1.0;
    if (n == "1/8") return 0.5;
    if (n == "1/16") return 0.25;
    if (n == "1/32") return 0.125;
    if (n == "8T") return 1.0 / 3.0;
    if (n == "16T") return 1.0 / 6.0;
    if (n == "1/2") return 2.0;
    if (n == "1/1") return 4.0;
    if (n == "2BAR") return 8.0;
    if (n == "4BAR") return 16.0;
    return 0.25;
}

juce::MidiFile toMidi(const Song& song) {
    juce::MidiFile file;
    file.setTicksPerQuarterNote(kTpq);
    bool first = true;
    for (const auto& tr : song.tracks) {
        const auto& p = tr.pattern;
        const int len = std::clamp(p.len, 1, felucca::kSteps);
        const double base = tr.stepQuarters * kTpq;                                // a straight step, in ticks
        const double late = base * double(tr.swing) / (song.sloop ? 200.0 : 250.0); // an odd step's delay
        auto start = [&](int i) { return double(i) * base + ((i & 1) ? late : 0.0); };
        auto length = [&](int i) { return (i & 1) ? base - late : base + late; };     // the swung step
        juce::MidiMessageSequence seq;
        seq.addEvent(juce::MidiMessage::textMetaEvent(3, tr.drums ? juce::String("Drums") : "Part " + juce::String(tr.index + 1)), 0);
        if (first) {
            seq.addEvent(juce::MidiMessage::tempoMetaEvent(int(std::lround(60000000.0 / std::max(1, song.bpm)))), 0);
            seq.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4, 4), 0);
            first = false;
        }
        const int ch = tr.drums ? 10 : tr.index + 1;
        auto note = [&](int channel, int n, int vel, double on, double off) {
            seq.addEvent(juce::MidiMessage::noteOn(channel, n, juce::uint8(std::clamp(vel, 1, 127))), std::round(on));
            seq.addEvent(juce::MidiMessage::noteOff(channel, n), std::round(std::max(on + 1.0, off)));
        };
        const auto& lanes = laneNotes(song.sloop);
        if (tr.drums) {   // SLOOP's drum track: 16 lanes, a level and ratchet each
            for (int i = 0; i < len && size_t(i) < p.drums.size(); ++i) {
                const auto& d = p.drums[size_t(i)];
                for (int l = 0; l < 16; ++l) {
                    if (!d.has(l)) continue;
                    const int hits = 1 + d.ratchet(l);
                    const double share = length(i) / hits;
                    for (int h = 0; h < hits; ++h)
                        note(10, lanes[size_t(l)], levelVelocity(d.level(l), 100), start(i) + h * share, start(i) + h * share + share * p.gate / 128.0);
                }
            }
        } else {
            for (int i = 0; i < len && size_t(i) < p.steps.size(); ++i) {
                const auto& s = p.steps[size_t(i)];
                if (!song.sloop && tr.lanesToo && s.hit)   // Felucca's lanes on any track
                    for (int l = 0; l < 8; ++l)
                        if ((s.hit >> l) & 1)
                            note(10, lanes[size_t(l)], ((s.acc >> l) & 1) ? 127 : (s.vel ? s.vel : 96), start(i), start(i) + base * p.gate / 128.0);
                if (s.time != felucca::kNote || s.n == 0) continue;
                // held on through the TIE steps after it (and over a slide into the next step)
                int last = i;
                while (last + 1 < len && p.steps[size_t(last + 1)].time == felucca::kTie) ++last;
                const bool slide = s.flags & felucca::kSlide;
                const double gate = (song.sloop ? length(last) : base) * p.gate / 128.0;
                double off = last > i || slide ? start(last) + (slide ? length(last) : gate) : start(i) + gate;
                const int velBase = (s.flags & felucca::kAccent) ? 127 : (s.vel ? s.vel : 96);
                for (int k = 0; k < s.n; ++k) {
                    const int vel = song.sloop ? levelVelocity((s.lvl >> (2 * k)) & 3, velBase) : velBase;
                    const int hits = song.sloop ? 1 + ((s.rat >> (2 * k)) & 3) : 1;
                    if (hits > 1) {   // SLOOP's ratchet: each hit its share of the step and of the gate
                        const double share = length(i) / hits;
                        for (int h = 0; h < hits; ++h) note(ch, s.note[size_t(k)], vel, start(i) + h * share, start(i) + h * share + share * p.gate / 128.0);
                    } else {
                        note(ch, s.note[size_t(k)], vel, start(i), off);
                    }
                }
            }
        }
        seq.addEvent(juce::MidiMessage::endOfTrack(), std::round(start(len - 1) + length(len - 1)));
        seq.sort();
        seq.updateMatchedPairs();
        file.addTrack(seq);
    }
    return file;
}

ImportResult fromMidi(const juce::MidiFile& source, const felucca::TrackPattern& base, bool sloop, bool drums, double stepQuarters) {
    ImportResult r;
    r.pattern = base;
    auto& p = r.pattern;
    if (drums && sloop) p.drums.assign(felucca::kSteps, {});
    else {
        p.steps.assign(felucca::kSteps, {});
        if (drums) for (auto& s : p.steps) s.time = felucca::kRest;   // (Felucca's lanes: on rests)
    }
    const short format = source.getTimeFormat();
    if (format <= 0 || stepQuarters <= 0) return r;   // SMPTE time: not a musical grid
    const double tpq = format;
    struct In { double start, end; int note, vel; };
    std::vector<In> notes;
    for (int t = 0; t < source.getNumTracks(); ++t) {
        juce::MidiMessageSequence seq(*source.getTrack(t));
        seq.updateMatchedPairs();
        for (int i = 0; i < seq.getNumEvents(); ++i) {
            const auto* e = seq.getEventPointer(i);
            if (!e->message.isNoteOn()) continue;
            const double on = e->message.getTimeStamp() / tpq;
            const double off = e->noteOffObject ? e->noteOffObject->message.getTimeStamp() / tpq : on + stepQuarters;
            notes.push_back({on, off, e->message.getNoteNumber(), e->message.getVelocity()});
        }
    }
    std::sort(notes.begin(), notes.end(), [](const In& a, const In& b) { return a.start != b.start ? a.start < b.start : a.note < b.note; });
    int lastStep = -1;
    const auto& lanes = laneNotes(sloop);
    for (const auto& n : notes) {
        const int step = int(std::lround(n.start / stepQuarters));
        if (step < 0 || step >= felucca::kSteps) { ++r.pastEnd; continue; }
        if (drums) {   // the lane whose GM note is nearest
            int lane = 0;
            for (int l = 1; l < int(lanes.size()); ++l)
                if (std::abs(lanes[size_t(l)] - n.note) < std::abs(lanes[size_t(lane)] - n.note)) lane = l;
            if (std::abs(lanes[size_t(lane)] - n.note) > 6) { ++r.unmapped; continue; }
            if (sloop) p.drums[size_t(step)].set(lane, true, velocityLevel(n.vel), 0);
            else {
                auto& s = p.steps[size_t(step)];
                s.hit |= 1 << lane;
                if (n.vel >= 116) s.acc |= 1 << lane;
            }
            lastStep = std::max(lastStep, step);
            ++r.notes;
            continue;
        }
        auto& s = p.steps[size_t(step)];
        if (s.time == felucca::kTie) { s = felucca::Step{}; }   // something starts here: no longer held
        bool dup = false;
        for (int k = 0; k < s.n; ++k) dup = dup || s.note[size_t(k)] == n.note;
        if (dup) continue;
        if (s.n >= 4) { ++r.crowded; continue; }
        if (s.n == 0) { s.vel = std::clamp(n.vel, 1, 127); s.time = felucca::kNote; }
        s.note[size_t(s.n++)] = uint8_t(n.note);
        // held over the steps it sounds at least a quarter of: TIE steps, where nothing starts
        const int endStep = std::min(felucca::kSteps - 1, std::max(step, int(std::ceil(n.end / stepQuarters - 0.25)) - 1));
        for (int i = step + 1; i <= endStep; ++i) {
            auto& t = p.steps[size_t(i)];
            if (t.time == felucca::kNote && t.n > 0) break;
            t = felucca::Step{};
            t.time = felucca::kTie;
        }
        lastStep = std::max(lastStep, endStep);
        ++r.notes;
    }
    for (auto& s : p.steps) if (s.time == felucca::kRest && s.n == 0 && !drums) s = felucca::Step{};
    if (lastStep >= 0) p.len = std::min(felucca::kSteps, ((lastStep + 1 + 3) / 4) * 4);
    return r;
}

}  // namespace felmidi
