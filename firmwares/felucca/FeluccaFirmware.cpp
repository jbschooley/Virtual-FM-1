#include "Firmwares.h"

namespace fm1 {

namespace {

// ---- Felucca (Leo Kuroshita), and SLOOP (isod89's fork of it) ----------------------------
// Answers M-VAVE's identity request as FM-1_9XY for release X.Y (build.py --release; 1.0:
// FM-1_910), FM-1_900 otherwise. It ignores FM-1+VA's commands and DX7 dumps; its own
// editor protocol (F0 7D 46 4C) syncs it, through FeluccaSync (the Felucca editor's Pull,
// Send and Live), not through this profile's FM-1+VA-shaped features.

class FeluccaFirmware : public Firmware {
public:
    explicit FeluccaFirmware(juce::String name) : name_(std::move(name)) {}
    juce::String name() const override { return name_; }
    juce::String summary() const override { return name_ + ": Pull, Send and Live in its editor"; }
    bool has(Feature) const override { return false; }
    juce::String cannot(Feature f) const override {
        switch (f) {
            case Feature::ReadPresets:   return "This FM-1 runs " + name_ + ", which cannot send its sounds over MIDI.";
            case Feature::WritePresets:  return "This FM-1 runs " + name_ + ", which does not take presets over MIDI.";
            case Feature::ReadPatterns:  return "This FM-1 runs " + name_ + ", which cannot send its sequences over MIDI.";
            case Feature::WritePatterns: return "This FM-1 runs " + name_ + ", which does not take patterns over MIDI.";
            case Feature::ReadCurrent:   return "This FM-1 runs " + name_ + ", which cannot send its sound over MIDI.";
            case Feature::ReadGlobals:   return "The plugin cannot read " + name_ + "'s settings.";
            case Feature::CheckEdit:     return name_ + " does not take FM-1+VA presets.";
        }
        return Firmware::cannot(f);
    }
    std::vector<Bytes> editMessages(const Sound&, edit::Channels) const override { return {}; }
    std::vector<Bytes> editChanges(const Sound&, const Sound&, edit::Channels) const override { return {}; }
private:
    juce::String name_;
};

}  // namespace

std::unique_ptr<Firmware> makeFeluccaFirmware(const juce::String& name) { return std::make_unique<FeluccaFirmware>(name); }

}  // namespace fm1
