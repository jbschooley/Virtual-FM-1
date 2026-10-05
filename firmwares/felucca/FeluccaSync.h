// FeluccaSync -- syncing with a synth running Felucca, through its editor protocol
// (F0 7D 46 4C, Felucca's web/EDITOR_PROTOCOL.md). Both ends speak it: a real FM-1 over
// MIDI (LinkEndpoint) and the plugin's own Felucca (plugin/FeluccaDevice VirtualEndpoint),
// so the same code copies either way and can be tested with two virtual ones.
//
//   backup / restore   every stored object (the music, settings, project slots, user
//                      presets, FM6 bank) as Felucca's full backup moves them; user
//                      sample slots are never read or written
//   Mirror             live: each side's changes as the other side's editor would make them
//
// Only editor-protocol frames are ever sent; never the update or boot loader messages.
#pragma once

#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <vector>

#include <juce_core/juce_core.h>

#include "Fm1Codec.h"

class Fm1Link;

namespace felucca {

using fm1::Bytes;
using Objects = std::map<int, std::vector<uint8_t>>;   // backup id -> bytes (empty: an empty object)

enum Cmd : int {
    kInfo = 1, kSet = 3, kPreset = 8, kWatch = 22, kChanged = 23, kReload = 24, kPing = 25, kStepChanged = 26,
    kTrack = 27, kTrackDump = 29, kTrackStep = 30, kTrackParam = 31, kTrackChanged = 32,
    kBackupList = 65, kBackupGet = 66, kBackupPut = 67, kFm6Get = 68, kFm6Put = 69
};

Bytes frame(int cmd, const std::vector<uint8_t>& args = {});
int commandOf(const Bytes& frame);             // -1: not an editor-protocol frame
std::vector<uint8_t> argsOf(const Bytes& frame);
bool isPush(int cmd);                          // CHANGED, RELOAD, STEP_CHANGED, TRACK_CHANGED

class Endpoint {
public:
    virtual ~Endpoint() = default;
    // Send a request; its reply (the first frame with its command that is not a push), or none in time.
    virtual std::optional<Bytes> ask(const Bytes& request, int timeoutMs) = 0;
    // The pushes that arrived since the last call.
    virtual std::vector<Bytes> pushes() = 0;
};

// A real synth over the session's MIDI link. Takes the link's SysEx listener while it lives.
class LinkEndpoint : public Endpoint {
public:
    explicit LinkEndpoint(Fm1Link& link);
    ~LinkEndpoint() override;
    std::optional<Bytes> ask(const Bytes& request, int timeoutMs) override;
    std::vector<Bytes> pushes() override;

private:
    Fm1Link& link_;
    std::mutex lock_;
    std::deque<Bytes> pushes_;
};

// done, total, what: false to stop
using Progress = std::function<bool(int done, int total, const juce::String& text)>;

std::optional<Objects> backup(Endpoint& from, const Progress& progress, juce::String& error);
// Into a synth, in the order Felucca's web editor restores (the music last). Every id given
// is written; an empty one empties that slot.
bool restore(Endpoint& to, const Objects& objects, const Progress& progress, juce::String& error);

class Mirror {
public:
    Mirror(Endpoint& a, Endpoint& b) : a_{a}, b_{b} {}
    bool start(juce::String& error);           // both watched, their selections read
    // About every 100 ms: keeps both watching and carries each side's changes to the other.
    // False when a side stopped answering (the error says which).
    bool tick(juce::String& error);
    void stop();                               // both unwatched

private:
    struct Side {
        Endpoint& ep;
        int sel = 0;
        juce::uint32 pinged = 0;
        std::vector<std::pair<Bytes, juce::uint32>> echoes;   // RELOADs our own loads will push, and when
    };
    bool carry(Side& from, Side& to, const Bytes& push, juce::String& error);
    bool copyTrackSound(Side& from, Side& to, int track, juce::String& error);
    Side a_, b_;
};

}  // namespace felucca
