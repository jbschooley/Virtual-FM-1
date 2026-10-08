// FeluccaSync -- syncing with a synth running Felucca, or SLOOP (a fork of it), through its
// editor protocol (F0 7D 46 4C, Felucca's web/EDITOR_PROTOCOL.md; SLOOP's is the same up to
// command 32, then its own: a Dialect). Both ends speak it: a real FM-1 over
// MIDI (LinkEndpoint) and the plugin's own Felucca (plugin/FeluccaDevice VirtualEndpoint),
// so the same code copies either way and can be tested with two virtual ones.
//
//   backup / restore   every stored object (the music, settings, project slots, user
//                      presets and their FM6 patches) as the full backup moves them; user
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
    kInfo = 1, kSet = 3, kPreset = 8, kProject = 9,
    kUpList = 16, kUpGet = 17, kUpPut = 18, kUpStore = 19, kUpLoad = 20, kUpErase = 21,   // user presets
    kWatch = 22, kChanged = 23, kReload = 24, kPing = 25, kStepChanged = 26,
    kTrack = 27, kTrackDump = 29, kTrackStep = 30, kTrackParam = 31, kTrackChanged = 32,
    kSong = 33, kMotion = 64,   // (Felucca's)
    kBackupList = 65, kBackupGet = 66, kBackupPut = 67, kFm6Get = 68, kFm6Put = 69
};

// What differs between Felucca and SLOOP: the backup commands and their objects, the backup file,
// FM6 (Felucca only: its patches, and the bank of 1.0..1.0.2) and which globals a mirror carries.
struct Dialect {
    const char* name;                          // "Felucca", "SLOOP"
    int backupList, backupGet, backupPut;      // the full backup's commands
    bool listVersion;                          // LIST's reply starts with a version byte (Felucca)
    std::vector<int> ids;                      // a full backup's objects, in its file's order (32..34: user samples)
    std::vector<int> restoreOrder;             // the objects (not the samples), in the order its editor restores them
    int lastObject;                            // the highest id that is not a user sample slot
    bool fm6;                                  // FM6_GET / FM6_PUT and object 8
    const char* fileFormat;                    // its editor's backup file: "felucca-backup", "sloop-backup"
    std::vector<int> mirroredGlobals;          // the globals a mirror carries (tempo, swing, tuning, effects)
    int drumTrack = -1, drumStep = -1;         // SLOOP: its drum track's steps go whole by DRUM_STEP (33), not TRACK_STEP
};
const Dialect& feluccaDialect();
const Dialect& sloopDialect();
const Dialect& melodeeDialect();
bool isMelodee(const Dialect& d);

Bytes frame(int cmd, const std::vector<uint8_t>& args = {});
int commandOf(const Bytes& frame);             // -1: not an editor-protocol frame
std::vector<uint8_t> argsOf(const Bytes& frame);
bool isPush(int cmd);                          // CHANGED, RELOAD, STEP_CHANGED, TRACK_CHANGED

class Endpoint {
public:
    virtual ~Endpoint() = default;
    virtual const Dialect& dialect() const = 0;
    // Send a request; its reply (the first frame with its command that is not a push), or none in time.
    virtual std::optional<Bytes> ask(const Bytes& request, int timeoutMs) = 0;
    // The pushes that arrived since the last call.
    virtual std::vector<Bytes> pushes() = 0;
    int extras = -1;   // SLOOP 2.4's locks, nudges and fill conditions: 1 has them, 0 not (2.3), -1 not asked yet
};

// A real synth over the session's MIDI link. Takes the link's SysEx listener while it lives.
class LinkEndpoint : public Endpoint {
public:
    explicit LinkEndpoint(Fm1Link& link, const Dialect& d = feluccaDialect());
    ~LinkEndpoint() override;
    const Dialect& dialect() const override { return dialect_; }
    std::optional<Bytes> ask(const Bytes& request, int timeoutMs) override;
    std::vector<Bytes> pushes() override;

private:
    Fm1Link& link_;
    const Dialect& dialect_;
    std::mutex lock_;
    std::deque<Bytes> pushes_;
};

// A backup file, as the dialect's web editor reads and writes it (Felucca: "felucca-backup"
// version 1, objects with id, size, crc, data; SLOOP: "sloop-backup" version 1, with id, len,
// crc, data): every object's size and CRC checked.
bool readBackup(const juce::File& f, Objects& out, juce::String& error, const Dialect& d = feluccaDialect());
juce::String backupJson(const Objects& objects, const juce::String& firmware, const Dialect& d = feluccaDialect());

// done, total, what: false to stop
using Progress = std::function<bool(int done, int total, const juce::String& text)>;

std::optional<Objects> backup(Endpoint& from, const Progress& progress, juce::String& error);
// Into a synth, in the order its web editor restores (Felucca: the music last; SLOOP: the
// settings last). Every id given is written; an empty one empties that slot.
bool restore(Endpoint& to, const Objects& objects, const Progress& progress, juce::String& error);

// One track's sound from one side to the other, as Live carries a load: its engine and preset
// (that track selected on `to` for it), every value that differs, its FM6 patch. *loaded says
// whether it loaded a preset on `to`.
// INFO's live-sync capabilities (its tagged block 53 01 caps, Felucca 1.0.2 on; 0 before):
// bit 0 WATCH while watching keeps what is not pushed yet, bit 1 no RELOAD after the editor's own
// PRESET or SET of G_ENGSEL. The reply's arguments.
constexpr uint8_t kCapWatchKeeps = 1, kCapNoEcho = 2;
uint8_t liveCaps(const std::vector<uint8_t>& infoArgs);

// what copySound loaded on the other side: a preset, and (Melodee) a CZ-1 tone, which leaves the part
// on preset 0 (each has the other side push a RELOAD)
struct Loaded { bool did = false; uint8_t engine = 0, preset = 0; bool presetLoad = false, czTone = false; };
constexpr int kMelodeeCz = 15, kCzGet = 75, kCzPut = 76, kCzBytes = 144;   // Melodee's CZ-1 (core.h ENGI_CZ; editor_cz.c)
bool copySound(Endpoint& from, Endpoint& to, int track, juce::String& error, Loaded* loaded = nullptr);

class Mirror {
public:
    Mirror(Endpoint& a, Endpoint& b) : a_{a}, b_{b} {}
    bool start(juce::String& error);           // both watched, their selections read
    // About every 100 ms: keeps both watching and carries each side's changes to the other.
    // False when a side stopped answering (the error says which).
    bool tick(juce::String& error);
    void stop();                               // both unwatched
    // A request made on side b (the plugin's own device: the Sequencer tab's edits), done on side a
    // too: neither firmware pushes what its editor writes, so it is carried here. False when a did
    // not answer.
    bool forward(const Bytes& request, juce::String& error);

private:
    struct Side {
        Endpoint& ep;
        int sel = 0;
        juce::uint32 pinged = 0, polled = 0;
        bool chainRunning = false;             // Felucca: a song chain plays (its step pushes are the chain's)
        std::vector<int> selMotion;            // Felucca: the parameters the selected track's motion plays (on, with events)
        Bytes lastReload;                      // the last RELOAD it pushed (engine, preset, track)
        uint8_t caps = 0;                      // its INFO 53 01 (no RELOAD echo of our loads: kCapNoEcho)
        std::vector<std::pair<Bytes, juce::uint32>> echoes;   // RELOADs our own loads will push, and when
    };
    bool carry(Side& from, Side& to, const Bytes& push, juce::String& error);
    void poll(Side& s);                        // what is never pushed and a mirror must know
    bool chainPlays(Side& s);                  // Felucca: SONG asked now
    bool copyTrackSound(Side& from, Side& to, int track, juce::String& error);
    Side a_, b_;
};

}  // namespace felucca
