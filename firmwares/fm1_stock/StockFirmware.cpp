#include "Firmwares.h"

namespace fm1 {

namespace {

// ---- M-VAVE's own firmware ----------------------------------------------------------

class StockFirmware : public Firmware {
public:
    juce::String name() const override { return "M-VAVE"; }
    juce::String summary() const override { return "M-VAVE firmware: can receive DX7 dumps, cannot be read back"; }
    bool has(Feature) const override { return false; }
    juce::String cannot(Feature f) const override {
        switch (f) {
            case Feature::ReadPresets:   return "This FM-1 runs M-VAVE's firmware, which cannot send presets back. Install FM-1+VA to pull.";
            // a DX7 single-voice dump stores at once on the selected preset, with no read-back,
            // so only the current preset could be targeted, by selecting it first
            case Feature::WritePresets:  return "This FM-1 runs M-VAVE's firmware: only the selected preset can be written, as a DX7 voice, unverified.";
            case Feature::ReadPatterns:  return "This FM-1 runs M-VAVE's firmware, which cannot send patterns back.";
            case Feature::WritePatterns: return "This FM-1 runs M-VAVE's firmware, which does not take patterns over MIDI.";
            case Feature::ReadCurrent:   return "This FM-1 runs M-VAVE's firmware, which cannot send its sound back.";
            case Feature::ReadGlobals:   return "The plugin can read the FM-1's GLOBE settings on FM-1_093 and FM-1_094 only.";
            case Feature::CheckEdit:     return Firmware::cannot(f);
        }
        return Firmware::cannot(f);
    }
};

}  // namespace

std::unique_ptr<Firmware> makeStockFirmware() { return std::make_unique<StockFirmware>(); }

}  // namespace fm1
