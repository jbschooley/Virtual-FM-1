// Fm1Json -- presets and sequencer patterns as JSON, for backups and for
// editing by hand or by script. The format is described by
// docs/virtual-fm1.schema.json and docs/JSON-FORMAT.md.
//
// A file holds any mix of presets (one, a bank, all 128) and patterns.
// Each preset carries readable settings and its raw bytes ("raw"). Reading
// starts from the raw bytes (or an INIT VOICE when there are none) and writes
// only the readable settings whose value differs from what the raw bytes
// already say, the way the editor commits a sound. So a file read back
// unchanged gives the same bytes, and a hand edit changes only what was edited,
// keeping the bytes this project does not interpret (the Virtual Analog
// engine's settings, knob assignments, the firmware's "unset" markers).

#pragma once

#include <optional>
#include <vector>

#include <juce_core/juce_core.h>

#include "Fm1Codec.h"
#include "Fm1Seq.h"

namespace fm1json {

constexpr int kVersion = 1;
extern const char* const kFormat;      // "virtual-fm1"
extern const char* const kSchemaUrl;

struct PatternEntry {
    int index = 0;                  // 0..15
    fm1::seq::Pattern pattern;      // pattern.chain is the per-pattern Chain
};

struct Document {
    std::vector<fm1::Sound> presets;       // slot -1 where the file gave none
    std::vector<PatternEntry> patterns;
};

juce::var presetToJson(const fm1::Sound& s);
juce::var patternToJson(const PatternEntry& p);
juce::String write(const Document& d);

// Problems are added to `errors`, each starting with its place in the file
// ("presets[2].operators[0].level: ..."). Use the result only when `errors` is empty.
std::optional<fm1::Sound> presetFromJson(const juce::var& v, const juce::String& path, juce::StringArray& errors);
std::optional<PatternEntry> patternFromJson(const juce::var& v, const juce::String& path, juce::StringArray& errors);
Document read(const juce::String& text, juce::StringArray& errors);

}  // namespace fm1json
