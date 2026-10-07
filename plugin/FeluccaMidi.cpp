#include "FeluccaMidi.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace felmidi {

namespace {
constexpr int kTpq = 480;
// eng_drum.c DRUM_LANE_NOTE; SLOOP's drums.c LANE_NOTE
const std::vector<int> kFeluccaLanes = {36, 38, 39, 42, 46, 45, 37, 56};
const std::vector<int> kSloopLanes = {36, 35, 38, 39, 42, 46, 44, 37, 40, 43, 48, 49, 51, 70, 63, 56};
// a GM note -> its lane, as each firmware routes MIDI in: Felucca eng_drum.c drum_lane (DRUM_GM and
// DV_TYPE_LANE; outside 35..81 folded by octave onto 36..47), SLOOP drums.c lane_of_note (LANE_OF_GM)
const int kFeluccaGm[47] = {0, 0, 6, 1, 2, 1, 5, 3, 5, 3, 5, 4, 5, 5, 7, 5, 7, 7, 7, 3, 7, 7, 7, 6, 7, 5, 5, 5, 5, 5, 5, 5,
                            7, 7, 3, 3, 6, 6, 3, 3, 6, 6, 6, 5, 5, 7, 7};
const int kSloopGm[47] = {1, 0, 7, 2, 3, 8, 9, 4, 9, 6, 9, 5, 10, 10, 11, 10, 12, 11, 12, 13, 11, 15, 11, 13, 12, 14, 14, 14, 14, 14,
                          14, 14, 15, 15, 13, 13, 15, 15, 13, 13, 7, 7, 7, 14, 14, 15, 15};
int laneOf(int note, bool sloop) {
    if (sloop) return note < 35 ? 0 : note > 81 ? 13 : kSloopGm[note - 35];
    const int n = note >= 35 && note <= 81 ? note : 36 + (note + 120 - 36) % 12;
    return kFeluccaGm[n - 35];
}
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
        auto swung = [&](int i) { return double(i) * base + ((i & 1) ? late : 0.0); };
        auto length = [&](int i) { return (i & 1) ? base - late : base + late; };     // the swung step
        auto start = [&](int i) {   // SLOOP 2.4: and its nudge, in 64ths of the step it falls in (seq.c micro_units):
            const int m = p.extras && size_t(i) < p.micro.size() ? p.micro[size_t(i)] : 0;   // late its own, early the one before
            return std::max(0.0, swung(i) + length(m < 0 ? i - 1 : i) * m / 64.0);
        };
        double end = swung(len - 1) + length(len - 1);
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
            end = std::max(end, std::max(on + 1.0, off));
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
            const int have = std::min(len, int(p.steps.size()));
            const double per = base;   // (Felucca's gate and a TIE's half step: from the straight step)
            for (int i = 0; i < have; ++i) {
                const auto& s = p.steps[size_t(i)];
                if (!song.sloop && tr.lanesToo && s.hit && s.time == felucca::kNote)   // Felucca's lanes on any track (a NOTE step's: seq.c)
                    for (int l = 0; l < 8; ++l)
                        if ((s.hit >> l) & 1)
                            note(10, lanes[size_t(l)], ((s.acc >> l) & 1) ? 127 : (s.vel ? s.vel : 96), start(i), start(i) + base * p.gate / 128.0);
                if (s.time != felucca::kNote || s.n == 0) continue;
                // seq.c: the gate is the step x GATE/128 (Felucca the straight step, SLOOP the swung one); a
                // slide or a TIE next holds the notes on; at a TIE they end at its gate plus half a step,
                // or hold on into the next step if that TIE slides. SLOOP: a ratcheted step never holds,
                // and its gate is shared out by the step's largest ratchet for all its notes.
                auto gateOf = [&](int k) { return (song.sloop ? length(k) : per) * p.gate / 128.0; };
                auto halfOf = [&](int k) { return (song.sloop ? length(k) : per) / 2.0; };
                int hitsMax = 1;
                if (song.sloop) for (int k = 0; k < s.n; ++k) hitsMax = std::max(hitsMax, 1 + ((s.rat >> (2 * k)) & 3));
                int last = i;
                if (hitsMax == 1)
                    while (last + 1 < have && p.steps[size_t(last + 1)].time == felucca::kTie) ++last;
                double off;
                if (hitsMax > 1) off = start(i) + gateOf(i) / hitsMax;
                else if (last > i) off = (p.steps[size_t(last)].flags & felucca::kSlide) ? start(last) + length(last) : start(last) + gateOf(last) + halfOf(last);
                else off = (s.flags & felucca::kSlide) ? start(i) + length(i) : start(i) + gateOf(i);
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
        seq.addEvent(juce::MidiMessage::endOfTrack(), std::round(end));   // (after the last note-off)
        seq.sort();
        seq.updateMatchedPairs();
        file.addTrack(seq);
    }
    return file;
}

ImportResult fromMidi(const juce::MidiFile& source, const felucca::TrackPattern& base, bool sloop, bool drums, double stepQuarters,
                      int part) {
    ImportResult r;
    r.pattern = base;
    const short format = source.getTimeFormat();
    if (format <= 0 || stepQuarters <= 0) { r.smpte = true; return r; }   // SMPTE time: not a musical grid
    const double tpq = format;
    struct In { double start, end; int note, vel, track, channel; };
    std::vector<In> all;
    int named = -1;   // the file's track of this part ("Part n" / "Drums", as export names them)
    const juce::String want = drums && sloop ? juce::String("Drums") : "Part " + juce::String(part + 1);
    for (int t = 0; t < source.getNumTracks(); ++t) {
        juce::MidiMessageSequence seq(*source.getTrack(t));
        seq.updateMatchedPairs();
        for (int i = 0; i < seq.getNumEvents(); ++i) {
            const auto* e = seq.getEventPointer(i);
            if (named < 0 && e->message.isTextMetaEvent() && e->message.getMetaEventType() == 3 && e->message.getTextFromTextMetaEvent().trim() == want)
                named = t;
            if (!e->message.isNoteOn()) continue;
            const double on = e->message.getTimeStamp() / tpq;
            const double off = e->noteOffObject ? e->noteOffObject->message.getTimeStamp() / tpq : on + stepQuarters;
            all.push_back({on, off, e->message.getNoteNumber(), e->message.getVelocity(), t, e->message.getChannel()});
        }
    }
    // which of them: the part's own track if the file has one with notes for it, else all; drum
    // lanes from channel 10 and notes from the others, where the file has both
    auto choose = [&](int only) {
        std::vector<In> notes;
        for (const auto& n : all) if (only < 0 || n.track == only) notes.push_back(n);
        if (!(sloop && drums && only >= 0)) {
            std::vector<In> pick;
            for (const auto& n : notes) if ((n.channel == 10) == drums) pick.push_back(n);
            if (!pick.empty() || only >= 0) notes = pick;
        }
        return notes;
    };
    auto notes = choose(named);
    if (notes.empty() && named >= 0) { named = -1; notes = choose(-1); }
    if (named >= 0) r.from = "the file's " + want + " track";
    else {
        std::set<std::pair<int, int>> seen;
        for (const auto& n : notes) seen.insert({n.track, n.channel});
        if (seen.size() > 1) r.from = "all " + juce::String(int(seen.size())) + " tracks and channels of the file";
    }
    if (notes.empty()) return r;   // (nothing to place: the pattern as it was)

    auto& p = r.pattern;
    if (drums && sloop) p.drums.assign(felucca::kSteps, {});
    else if (drums) {   // Felucca's lanes: the hits replaced, the notes kept (one step holds both)
        p.steps.resize(felucca::kSteps);
        for (auto& s : p.steps) s.hit = s.acc = 0;
    } else {            // the notes replaced; Felucca's lane hits kept (on NOTE steps, where they play)
        std::vector<felucca::Step> steps(felucca::kSteps);
        for (size_t i = 0; i < steps.size() && i < p.steps.size(); ++i)
            if (!sloop && p.steps[i].hit) { steps[i].time = felucca::kNote; steps[i].hit = p.steps[i].hit; steps[i].acc = p.steps[i].acc; }
        p.steps = steps;
    }
    std::sort(notes.begin(), notes.end(), [](const In& a, const In& b) { return a.start != b.start ? a.start < b.start : a.note < b.note; });
    int lastStep = -1;
    for (const auto& n : notes) {
        const int step = int(std::lround(n.start / stepQuarters));
        if (step < 0 || step >= felucca::kSteps) { ++r.pastEnd; continue; }
        if (drums) {   // the lane the firmware plays the note on
            const int lane = laneOf(n.note, sloop);
            if (sloop) p.drums[size_t(step)].set(lane, true, velocityLevel(n.vel), 0);
            else {
                auto& s = p.steps[size_t(step)];
                if (s.time != felucca::kNote) { s = felucca::Step{}; s.time = felucca::kNote; }   // as its grid does (ui.c grid_hit)
                s.hit |= 1 << lane;
                if (n.vel > 110) s.acc |= 1 << lane;   // (as its recording accents)
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
        // held over the steps it sounds at least a quarter of: TIE steps, where nothing starts (nor a lane hit)
        int endStep = std::min(felucca::kSteps - 1, std::max(step, int(std::ceil(n.end / stepQuarters - 0.25)) - 1));
        for (int i = step + 1; i <= endStep; ++i) {
            auto& t = p.steps[size_t(i)];
            if (t.time == felucca::kNote && (t.n > 0 || t.hit)) { endStep = i - 1; break; }
            t = felucca::Step{};
            t.time = felucca::kTie;
        }
        lastStep = std::max(lastStep, endStep);
        ++r.notes;
    }
    if (lastStep >= 0) {   // the steps used, rounded up to a beat
        const int beat = std::max(1, int(std::lround(1.0 / stepQuarters)));
        p.len = std::min(felucca::kSteps, ((lastStep + beat) / beat) * beat);
    }
    return r;
}

}  // namespace felmidi
