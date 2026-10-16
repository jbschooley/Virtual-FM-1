// Fm1OpenSync -- syncing with an FM-1 running M-VAVE's FM1 Open (engines/fm1open) through its project
// transfer protocol (upstream app/transfer.c), as M-VAVE's workbench does it (sdk/WebUpdate.js, Library):
// the project playing, a track's sound or FM patch, the 16 user project slots, and the sample bank.
//
// A request: F0 7D 'F' 'M' 02 cmd token-lo token-hi <its bytes, packed LSB first into 7-bit> sum F7, the
// sum making 7D .. sum 0 modulo 128. The reply: the same with cmd | 64, its bytes a status (0 done, 1 empty,
// 64 + n an error -n) and what was asked. One request at a time; the same request again (the same token)
// gets the reply it had (a lost reply: asked again). Its replies fit 128 bytes, so a read moves 64 bytes and
// a sample write 128.
//
// FM1 Open has no message for one value changed, and tells nothing of changes made on the device: there is
// no Live (a sound written whole would cut what plays), only Pull and Send.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <juce_core/juce_core.h>

class Fm1Link;

namespace fm1open {

using Bytes = std::vector<uint8_t>;

constexpr int kProjectBytes = 4064;       // GROOVE_PROJECT_BYTES (V12; 4048 before its ARP settings)
constexpr int kLegacyProjectBytes = 4048;
constexpr int kTimbreBytes = 223;         // a track's sound (kind 2: struct groove_preset)
constexpr int kPatchBytes = 156;          // a track's FM patch (kind 1)
constexpr int kSlots = 16;                // user project slots
constexpr int kSampleBytes = 466944;      // the sample bank's room (GROOVE_SAMPLE_BYTES)
constexpr int kTracks = 8;

Bytes frame(int command, int token, const Bytes& payload);   // payload up to 132 bytes
struct Reply { int command = 0, token = 0; Bytes data; };
std::optional<Reply> parse(const Bytes& sysex);              // a frame of this protocol, or none
uint32_t crc32(const uint8_t* p, size_t n);                  // zlib's
// a project as M-VAVE's workbench saves it (.fm1: "FM1P", its version, size and CRC, then it)
Bytes projectFile(const Bytes& project);
// a project from before V12 (4048 bytes) as V12 keeps it (4064: its ARP settings at their defaults), as the
// workbench upgrades it; a V12 one as it is; empty: neither
Bytes upgradeProject(const Bytes& project);
// a sample bank FM1 Open takes (sdk/Samples.js read: its header, CRC, 16 entries of SEA5 blocks). FM1 Open
// erases its bank before a write, so a bank is checked before any is sent. Empty: fine, else why not
juce::String sampleBankProblem(const Bytes& bank);

class Endpoint {
public:
    virtual ~Endpoint() = default;
    // send a request; its reply (same command | 64, same token), or none in time. A request may be sent
    // again as it is: FM1 Open answers it with the reply it cached
    virtual std::optional<Bytes> ask(const Bytes& request) = 0;
};

// An FM-1 over the session's MIDI link: each request sent up to three times, a second each (the workbench's)
class LinkEndpoint : public Endpoint {
public:
    explicit LinkEndpoint(Fm1Link& link) : link_(link) {}
    std::optional<Bytes> ask(const Bytes& request) override;
private:
    Fm1Link& link_;
};

struct Info {
    int version = 0;           // 4: V12's projects (with ARP settings); 3 before
    int slots = 0;             // 16
    int slot = 0;              // the user slot the project playing came from
    int projectBytes = 0;      // 4064 (4048 before)
    int clock = 0;
    int sources[kTracks] = {};   // each track's sound source
};

using Progress = std::function<bool(int done, int total)>;   // false: stop

// What the workbench does with these requests, each a step it checks; false and error when one fails
class Client {
public:
    explicit Client(Endpoint& e);
    std::optional<Info> info(juce::String& error);
    std::optional<Bytes> download(int kind, int target, juce::String& error, const Progress& progress = {});   // 0 project, 1 FM patch, 2 sound
    bool upload(int kind, int target, const Bytes& bytes, juce::String& error, const Progress& progress = {});
    std::optional<Bytes> project(juce::String& error, const Progress& progress = {}) { return download(0, 0, error, progress); }
    bool setProject(const Bytes& project, juce::String& error, const Progress& progress = {});   // checked by reading it back
    bool saveToSlot(int slot, juce::String& error);      // the project playing, kept in slot 0..15
    bool loadSlot(int slot, juce::String& error);        // slot 0..15 becomes the project playing
    std::optional<std::string> slotName(int slot, juce::String& error);   // "" an empty slot
    bool rename(const std::string& name, juce::String& error);            // the project playing's name (12 printable)
    std::optional<Bytes> samples(juce::String& error, const Progress& progress = {});
    bool setSamples(const Bytes& bank, juce::String& error, const Progress& progress = {});   // checked by reading it back
private:
    std::optional<Bytes> query(int command, const Bytes& payload, juce::String& error, bool emptyOK = false, bool* empty = nullptr);
    Endpoint& e_;
    int token_;
};

}  // namespace fm1open
