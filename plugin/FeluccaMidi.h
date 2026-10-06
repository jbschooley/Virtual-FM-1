// FeluccaMidi -- Felucca's and SLOOP's patterns to and from standard MIDI files.
//
// Export plays each track's pattern once (its LEN steps) as the firmware's sequencer times it
// (seq.c): its DIV, its swing (the track's plus the global one; Felucca: odd steps start
// swing/250 of a step late, SLOOP: swing/200), its GATE (the step x GATE/128: Felucca the straight
// step, SLOOP the swung one and each ratchet hit its share), a TIE or a slide holding the notes on
// into the next step. Velocity: an accent 127, else the step's (0: 96); SLOOP's level per note
// (ghost 42, soft 72, hard 127) and its ratchets (each hit its share of the step). Every step
// plays, whatever Felucca's chance. Drum lanes go out as their GM notes on channel 10 (Felucca's
// 8 lanes on any track, SLOOP's drum track's 16); synth tracks on channels 1-4. One MIDI track a
// firmware track, at 480 ticks a quarter, with the tempo.
//
// Import puts a file's notes on one track's step grid at its DIV (straight; each note on the
// nearest step): up to 4 notes a step (more are left out), a note held over the next steps
// writes TIE steps when nothing new starts there. It takes the file's track of that part ("Part n"
// or "Drums", as export names them) if it has notes for it, else every track; drum lanes from channel 10
// and notes from the other channels where the file has both. Onto drum lanes (SLOOP's drum track,
// or Felucca's lanes view): each note on the lane the firmware plays it on from MIDI in (Felucca
// eng_drum.c drum_lane, SLOOP drums.c lane_of_note); SLOOP's level from the velocity as its own
// recording does (below 56 ghost, 88 soft, 116 normal, else hard), Felucca's accent above 110.
// On Felucca a step holds both notes and lane hits: importing notes keeps the hits and the other
// way round (a lane hit makes its step a NOTE step, as its grid does). LEN becomes the steps
// used, rounded up to a beat. Not carried either way: Felucca's chance, slides, SLOOP's synth-step
// ratchets and levels (import keeps the velocity), the file's tempo and time signature.
#pragma once

#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

#include "FeluccaSeq.h"

namespace felmidi {

struct Track {
    int index = 0;                    // 0..3
    felucca::TrackPattern pattern;
    bool drums = false;               // its lanes (SLOOP's drum track), not notes
    bool lanesToo = true;             // Felucca: its steps' drum-lane hits as well
    int swing = 0;                    // the track's own plus the global (0..100)
    double stepQuarters = 0.25;       // its DIV, in quarter notes
};
struct Song {
    bool sloop = false;
    int bpm = 120;
    std::vector<Track> tracks;
};

// a DIV's name (as the firmware's parameter shows it: "1/16", "8T", "2BAR" ...) in quarter notes
double divQuarters(const std::string& name);

juce::MidiFile toMidi(const Song& song);

struct ImportResult {
    felucca::TrackPattern pattern;
    int notes = 0, pastEnd = 0, crowded = 0;
    bool smpte = false;               // SMPTE-timed: not placed (the pattern as it was)
    juce::String from;                // which of the file's notes, if not simply all of one track
};
// `base` gives the settings to keep; its steps (or drums) are replaced.
// No notes to place (r.notes 0): the pattern is `base` unchanged.
ImportResult fromMidi(const juce::MidiFile& file, const felucca::TrackPattern& base, bool sloop, bool drums,
                      double stepQuarters, int part);

// the GM note of a drum lane: Felucca's 8, SLOOP's 16
const std::vector<int>& laneNotes(bool sloop);

}  // namespace felmidi
