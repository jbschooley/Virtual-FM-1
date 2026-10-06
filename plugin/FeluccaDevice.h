// FeluccaDevice -- the virtual Felucca (or SLOOP) device's stored objects (its four project
// slots, user presets, FM6 patch bank and settings) kept in the library folder, as the file its
// web editor writes for a full backup (felucca-backup or sloop-backup version 1, JSON), so the
// same file restores a real FM-1 running it and the other way round:
//
//   <library>/Felucca/Felucca device.json
//   <library>/SLOOP/SLOOP device.json
//
// Every instance set to Felucca (or to SLOOP) plays the same device: what one saves (a project slot,
// a user preset) the others load within a second. The music now playing (object 0) is
// each instance's own and lives in the DAW project; the file holds the last one written,
// as a backup must have it.
#pragma once

#include <map>
#include <vector>

#include <juce_core/juce_core.h>

#include "FeluccaSync.h"

class FeluccaEngine;

namespace felucca {

const Dialect& dialectOf(const FeluccaEngine& f);   // Felucca's or SLOOP's, as the engine plays

// The plugin's own Felucca as one end of a sync (FeluccaSync.h): requests answered at once.
class VirtualEndpoint : public Endpoint {
public:
    explicit VirtualEndpoint(std::shared_ptr<FeluccaEngine> f);
    const Dialect& dialect() const override { return *dialect_; }
    std::optional<Bytes> ask(const Bytes& request, int timeoutMs) override;
    std::vector<Bytes> pushes() override;

private:
    std::shared_ptr<FeluccaEngine> f_;
    const Dialect* dialect_;
    std::vector<Bytes> pushes_;
};

// The plugin's own Felucca's stored objects (0..8, SLOOP's 0..7, as a full backup carries them), read and
// written directly, each under the engine's lock. Not through its editor protocol: there a
// backup is many requests, and anything that reads the music meanwhile (the device file's save,
// a host saving the project) reuses Felucca's staging memory, so the backup is refused partway.
Objects objectsOf(FeluccaEngine& f);
bool putObjects(FeluccaEngine& f, const Objects& objects, juce::String& error);   // in a restore's order

class DeviceStore {
public:
    static juce::File defaultFile(const Dialect& d = feluccaDialect());   // <library>/<name>/<name> device.json
    explicit DeviceStore(const Dialect& d = feluccaDialect()) : DeviceStore(defaultFile(d), d) {}
    explicit DeviceStore(juce::File f, const Dialect& d = feluccaDialect()) : file_(std::move(f)), dialect_(d) {}
    const Dialect& dialect() const { return dialect_; }

    // An engine just taken: give it the device's stored objects (not the music). A message for
    // the user if the file could not be used, else empty.
    juce::String load(FeluccaEngine& f);
    // About once a second, on the message thread: save what this instance changed, or take
    // what another one saved. A message for the user if something went wrong, else empty.
    juce::String tick(FeluccaEngine& f);
    const juce::File& file() const { return file_; }

private:
    juce::String loadInto(FeluccaEngine& f);
    juce::File file_;
    const Dialect& dialect_;
    Objects known_;                            // 1..8 (SLOOP 1..7) as last loaded or saved
    juce::int64 seen_ = 0;                     // the file's contents then (a hash)
    juce::int64 fileHash() const;
    juce::String lastError_;
    bool blocked_ = false;                     // the file could not be read: never written over
};

}  // namespace felucca
