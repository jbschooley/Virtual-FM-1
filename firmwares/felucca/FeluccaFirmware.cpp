#include "Firmwares.h"

namespace fm1 {

namespace {

// ---- Felucca (Leo Kuroshita) ----------------------------------------------------------
// Answers M-VAVE's identity request as FM-1_9XY for release X.Y (build.py --release), FM-1_900 otherwise. Its other SysEx is for
// firmware updates only: no preset or pattern transfer, and it ignores FM-1+VA's
// commands and DX7 dumps. It saves four projects (a sound and its sequence) in its
// own format, and its sound parameters are not FM-1+VA's, so the plugin sends it nothing.

class FeluccaFirmware : public Firmware {
public:
    juce::String name() const override { return "Felucca"; }
    juce::String summary() const override { return "Felucca firmware: no preset or pattern transfer over MIDI"; }
    bool has(Feature) const override { return false; }
    juce::String cannot(Feature f) const override {
        switch (f) {
            case Feature::ReadPresets:   return "This FM-1 runs Felucca, which cannot send its sounds over MIDI.";
            case Feature::WritePresets:  return "This FM-1 runs Felucca, which does not take presets over MIDI.";
            case Feature::ReadPatterns:  return "This FM-1 runs Felucca, which cannot send its sequences over MIDI.";
            case Feature::WritePatterns: return "This FM-1 runs Felucca, which does not take patterns over MIDI.";
            case Feature::ReadCurrent:   return "This FM-1 runs Felucca, which cannot send its sound over MIDI.";
            case Feature::ReadGlobals:   return "The plugin cannot read Felucca's settings.";
            case Feature::CheckEdit:     return "Felucca does not take FM-1+VA presets.";
        }
        return Firmware::cannot(f);
    }
    std::vector<Bytes> editMessages(const Sound&, edit::Channels) const override { return {}; }
    std::vector<Bytes> editChanges(const Sound&, const Sound&, edit::Channels) const override { return {}; }
};

}  // namespace

std::unique_ptr<Firmware> makeFeluccaFirmware() { return std::make_unique<FeluccaFirmware>(); }

}  // namespace fm1
