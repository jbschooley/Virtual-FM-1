// BankModel -- the plugin's copy of the FM-1's 128 presets plus the sound that
// is playing, and what the synth was last seen holding, so the UI can show
// which slots differ from the hardware (the MODX Connect idea: the plugin and
// the synth each hold a library; you push or pull a slot or the whole thing).

#pragma once

#include <array>
#include <functional>
#include <optional>

#include <juce_data_structures/juce_data_structures.h>

#include "Fm1Codec.h"

class BankModel {
public:
    struct Slot {
        fm1::Sound sound;                    // what the plugin holds
        std::optional<fm1::Sound> onDevice;  // what the synth held when last read/written
        bool synced() const {
            return onDevice && onDevice->voice == sound.voice && onDevice->record == sound.record;
        }
    };

    BankModel();

    static constexpr int kSlots = fm1::kSlots;

    Slot& slot(int i) { return slots_[size_t(i)]; }
    const Slot& slot(int i) const { return slots_[size_t(i)]; }

    // The slot the engine plays and the sync buttons act on ("current").
    int currentSlot() const { return current_; }
    void setCurrentSlot(int i);

    // Edits to the current sound (from the editor or a DX7 SysEx) go here.
    fm1::Sound& current() { return slots_[size_t(current_)].sound; }
    const fm1::Sound& current() const { return slots_[size_t(current_)].sound; }

    void setSound(int slot, const fm1::Sound& s, bool fromDevice);
    void markOnDevice(int slot, const fm1::Sound& s);   // after a verified write or a read
    void clearDeviceState();                            // a different synth was connected

    int unsyncedCount() const;
    juce::String slotLabel(int i) const;                // "A01 SUPERSAW  (VA)"
    static juce::String bankName(int slot);             // "A01".."D32"

    juce::ValueTree toState() const;
    void fromState(const juce::ValueTree& v);

    std::function<void()> onChange;          // UI refresh (message thread)
    std::function<void()> onLibraryChange;   // a slot's sound or sync state changed (to save the shared library)

private:
    void changed() { if (onChange) onChange(); }
    void libraryChanged() { if (onLibraryChange) onLibraryChange(); changed(); }
    std::array<Slot, kSlots> slots_;
    int current_ = 0;
};
