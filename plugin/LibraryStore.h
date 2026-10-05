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
    // A slot file that cannot be read (damaged, from a newer version, not downloaded yet)
    // leaves the slot as it was and is listed in unreadable(); it is never written over.
    bool load(BankModel& bank);
    // Write the slots that changed since the last load or save. Returns how many were written.
    // If another instance wrote meanwhile, its slots are read back in after (reloaded() says so).
    int save(BankModel& bank);
    // True when another instance wrote since this one last loaded or saved.
    bool changedElsewhere() const;
    const juce::Array<int>& unreadable() const { return unreadable_; }   // slots, from the last load
    bool reloaded() const { return reloaded_; }                        // the last save also read others' writes
    // The old single-file library changed after it was copied in (an older version still writing it).
    bool oldLibraryNewer(const juce::File& oldLibrary) const;

    // The first run: the bank from the old single-file library (left untouched).
    bool migrateFrom(const juce::File& oldLibrary, BankModel& scratch);

    // The format tag of a sound: which firmwares can play it.
    static juce::String formatOf(const fm1::Sound& s);

    static juce::String slotText(const BankModel::Slot& s, int rev);              // a slot file's contents
    static std::optional<BankModel::Slot> slotFromText(const juce::String& text, int slot, int* rev = nullptr);

private:
    struct Known { fm1::Voice voice{}; fm1::Record record{}; std::optional<std::pair<fm1::Voice, fm1::Record>> dev; int rev = 0; bool valid = false; };
    std::array<Known, BankModel::kSlots> known_{};   // what this instance last read or wrote, per slot
    std::array<bool, BankModel::kSlots> locked_{};  // the file could not be read: never write over it
    juce::Array<int> unreadable_;
    bool reloaded_ = false;
    juce::String generation_;                        // the .generation token this instance last saw
    static juce::String readGeneration();
    void bumpGeneration();
    static void writeAtomically(const juce::File& f, const juce::String& text);
    static Known knownOf(const BankModel::Slot& s, int rev);
};
