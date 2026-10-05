// LibraryStore -- the FM-1 bank as a folder of files, shared by every instance
// (the Standalone and the plugin in every host).
//
//   <root>/                      Documents/Virtual FM-1 (FM1_DATA_DIR in tests)
//     Banks/FM-1/
//       bank.json                what the folder holds: 128 FM-1 slots, the firmwares that use them
//       001.json ... 128.json    one slot each: a virtual-fm1 JSON document holding the preset
//                                (readable settings and its raw bytes, docs/JSON-FORMAT.md), plus
//                                "library": its format, a revision, and what the synth held
//     .generation                bumped after every write: instances watch this one file
//     .trash/                    what a write replaced, and anything removed; never deleted
//     migrated-from.txt          where the bank came from on the first run
//
// Files are written to a temporary file and moved into place, so a crash never
// leaves half a slot. Nothing is overwritten unseen: when a slot changed on
// disk since this instance read it (another instance wrote it), the version on
// disk goes to .trash before this instance's replaces it.
#pragma once

#include <array>
#include <optional>

#include <juce_core/juce_core.h>

#include "BankModel.h"

class LibraryStore {
public:
    static juce::File root();                  // FM1_DATA_DIR, else Documents/Virtual FM-1
    juce::File bankDir() const { return root().getChildFile("Banks").getChildFile("FM-1"); }
    juce::File slotFile(int slot) const { return bankDir().getChildFile(juce::String(slot + 1).paddedLeft('0', 3) + ".json"); }

    bool exists() const { return bankDir().getChildFile("bank.json").existsAsFile(); }

    // Read every slot into `bank` (the current slot is left alone). False when there is no bank yet.
    bool load(BankModel& bank);
    // Write the slots that changed since the last load or save. Returns how many were written.
    int save(const BankModel& bank);
    // True when another instance wrote since this one last loaded or saved.
    bool changedElsewhere() const;

    // The first run: the bank from the old single-file library (left untouched).
    bool migrateFrom(const juce::File& oldLibrary, BankModel& scratch);

    // The format tag of a sound: which firmwares can play it.
    static juce::String formatOf(const fm1::Sound& s);

    static juce::String slotText(const BankModel::Slot& s, int rev);              // a slot file's contents
    static std::optional<BankModel::Slot> slotFromText(const juce::String& text, int slot, int* rev = nullptr);

private:
    struct Known { fm1::Voice voice{}; fm1::Record record{}; std::optional<std::pair<fm1::Voice, fm1::Record>> dev; int rev = 0; bool valid = false; };
    std::array<Known, BankModel::kSlots> known_{};   // what this instance last read or wrote, per slot
    juce::int64 generation_ = -1;                    // the .generation this instance last saw
    static juce::int64 readGeneration();
    void bumpGeneration();
    static void writeAtomically(const juce::File& f, const juce::String& text);
    static Known knownOf(const BankModel::Slot& s, int rev);
};
