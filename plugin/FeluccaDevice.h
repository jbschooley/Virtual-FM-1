// FeluccaDevice -- the virtual Felucca device's stored objects (its four project slots, 32
// user presets, FM6 patch bank and settings) kept in the library folder, as the file
// Felucca's web editor writes for a full backup ("felucca-backup" version 1, JSON), so the
// same file restores a real FM-1 running Felucca and the other way round:
//
//   <library>/Felucca/Felucca device.json
//
// Every instance set to Felucca plays the same device: what one saves (a project slot,
// a user preset) the others load within a second. The music now playing (object 0) is
// each instance's own and lives in the DAW project; the file holds the last one written,
// as a backup must have it.
#pragma once

#include <map>
#include <vector>

#include <juce_core/juce_core.h>

class FeluccaEngine;

namespace felucca {

using Objects = std::map<int, std::vector<uint8_t>>;   // backup id -> bytes (empty: an empty object)

// The ids a full backup holds, in order: 0..8, then the user sample slots 32..34.
const std::vector<int>& backupIds();
// Read a backup file: every object's size and CRC checked, as the web editor checks them.
bool readBackup(const juce::File& f, Objects& out, juce::String& error);
juce::String backupJson(const Objects& objects, const juce::String& firmware);

class DeviceStore {
public:
    static juce::File defaultFile();           // <library>/Felucca/Felucca device.json
    explicit DeviceStore(juce::File f = defaultFile()) : file_(std::move(f)) {}

    // An engine just taken: give it the device's stored objects (not the music).
    void load(FeluccaEngine& f);
    // About once a second, on the message thread: save what this instance changed, or take
    // what another one saved. A message for the user if something went wrong, else empty.
    juce::String tick(FeluccaEngine& f);
    const juce::File& file() const { return file_; }

private:
    juce::String loadInto(FeluccaEngine& f);
    juce::File file_;
    Objects known_;                            // 1..8 as last loaded or saved
    juce::Time seen_;                          // the file's time then
    juce::String lastError_;
};

}  // namespace felucca
