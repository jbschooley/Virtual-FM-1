#include "Firmware.h"

namespace fm1 {

// ---- defaults: a feature a profile does not override is one it does not have ----

juce::String Firmware::cannot(Feature f) const {
    switch (f) {
        case Feature::ReadPresets:   return "This firmware cannot send presets back.";
        case Feature::WritePresets:  return "This firmware cannot take presets from the plugin.";
        case Feature::ReadPatterns:  return "This firmware cannot send patterns back.";
        case Feature::WritePatterns: return "This firmware does not take patterns over MIDI.";
        case Feature::ReadCurrent:   return "This firmware cannot send its current sound back.";
        case Feature::CheckEdit:     return "this firmware cannot be read back to check it.";
        case Feature::ReadGlobals:   return "The plugin cannot read this firmware's settings.";
    }
    return {};
}

std::optional<Sound> Firmware::readPreset(Port&, int, juce::String& error) { error = cannot(Feature::ReadPresets); return std::nullopt; }
bool Firmware::writePreset(Port&, const Sound&, juce::String& error) { error = cannot(Feature::WritePresets); return false; }
std::optional<seq::Pattern> Firmware::readPattern(Port&, int, juce::String& error) { error = cannot(Feature::ReadPatterns); return std::nullopt; }
bool Firmware::writePattern(Port&, const seq::Pattern&, int, bool, juce::String& error) { error = cannot(Feature::WritePatterns); return false; }
std::optional<Firmware::Current> Firmware::readCurrent(Port&, juce::String& error) { error = cannot(Feature::ReadCurrent); return std::nullopt; }
std::optional<Firmware::Live> Firmware::readLive(Port&, juce::String& error) { error = cannot(Feature::CheckEdit); return std::nullopt; }
std::optional<Globals> Firmware::readGlobals(Port&, juce::String& error) { error = cannot(Feature::ReadGlobals); return std::nullopt; }

}  // namespace fm1
