// SeqMidi -- sequencer patterns to and from standard MIDI files.
//
// Export renders each pattern as the sequencer plays it (Sequencer::stepEvents:
// ratchet, gate, accent, transpose, ties and slide; every step plays, whatever
// its chance), one pass, at 96 ticks per quarter note, one track per pattern,
// starting together at the beginning. The tempo is the first pattern's.
//
// Import puts a file's notes, from all its tracks, on a pattern's step grid at
// its note value (straight, in beats from the file's ticks): each note goes to
// the nearest step, and a note that sounds at least a quarter of a following
// step is tied across the steps it reaches, as the sequencer ties notes.

#pragma once

#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

#include "Fm1Seq.h"

namespace seqmidi {

struct Entry { int index; fm1::seq::Pattern pattern; };   // index 0..15

juce::MidiFile toMidi(const std::vector<Entry>& patterns);

struct ImportResult {
    fm1::seq::Pattern pattern;
    int notes = 0;          // placed
    int pastEnd = 0;        // left out: they start after step 64
    int crowded = 0;        // left out: a step already had nine notes
    bool tempoFromFile = false;
};
// `base` gives the note value (the grid) and the pattern settings to keep; its
// steps are replaced. Length becomes the steps used, rounded up to a beat (4).
ImportResult fromMidi(const juce::MidiFile& file, const fm1::seq::Pattern& base);

}  // namespace seqmidi
