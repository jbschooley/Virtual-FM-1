// FeluccaParams -- Felucca's and SLOOP's parameters as host parameters, for automation.
//
// They exist in every build (with or without Felucca's engines) so that a project has
// the same parameter list everywhere. Their ids come from a fixed table named after
// Felucca's own parameters (core.h P_* and G_*; the table follows Felucca 1.0), not from Felucca's numbering,
// which shifts when Felucca adds a parameter: "fel_t2_lrate" is part 2's LFO rate in every
// version. A later Felucca maps its numbering onto the same names here; a parameter it
// adds gets a new id (with a higher version hint), never an old one.
//
// To the host every value is 0..1, spread over the parameter's range in Felucca at the
// time (an engine's eight parameters change range with the engine); the text the host
// shows is Felucca's own (SAW, 1/16, 120 ...).
#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

namespace felparams {

struct Entry {
    juce::String id;      // the host parameter's id
    int track = -1;       // part 0..3, -1 for a global
    int index = 0;        // Felucca 1.0's parameter number (P_* or G_*), or SLOOP 2.3's
    float def = 0.0f;     // its default, 0..1 (0 for an engine's own eight)
    bool sloop = false;   // SLOOP's ("slp_..."), not Felucca's
    int hint = 0;         // its version hint (0: Felucca's 2, SLOOP's 3); AU orders by it, so an added one has a higher one
    bool melodee = false; // Melodee's ("mel_...")
    bool song = false;    // the same value on every part (Melodee's SCL, QNT, DEG: held on part 1's entry)
};

// Felucca's, then SLOOP's, then Melodee's (each added after the others: a project's earlier
// parameters keep their places)
const std::vector<Entry>& entries();
int indexOf(const juce::String& id);   // -1 if not one of these
// the entry for the parameter (track -1: global) of Felucca, SLOOP or Melodee, or -1
int entryFor(int track, int index, bool sloop = false, bool melodee = false);

// What the host shows for an entry at a 0..1 value: set by the instance once it exists.
struct TextSource { std::function<juce::String(int entry, float value01)> text; };

void addTo(juce::AudioProcessorValueTreeState::ParameterLayout& layout, std::shared_ptr<TextSource> text);

}  // namespace felparams
