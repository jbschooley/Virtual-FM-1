// The firmware profiles the plugin knows (core/Firmware.h), one folder each:
//   fm1_stock/       M-VAVE's own firmware
//   baudgirl_fm1va/  baud girl's FM-1+VA
//   felucca/         Felucca (Hügelton)
// firmwareFor() picks one from the synth's identity.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Firmware.h"

namespace fm1 {

std::unique_ptr<Firmware> makeStockFirmware();
std::unique_ptr<Firmware> makeFmVaFirmware();
std::unique_ptr<Firmware> makeFeluccaFirmware();

// The firmwares an instance can be set to (the dropdown at the top of the
// editor). The choice decides what the editor shows; it is saved with the project.
struct FirmwareChoice {
    const char* id;           // saved in projects: never change one
    const char* name;         // shown
    bool vaEngine;            // presets may use baud girl's VA engine (an editor tab for it)
    bool supported;           // false: the plugin cannot emulate it yet (a page says so)
    std::string label() const;   // the name and the version the plugin plays and syncs with (versions below)
};
const std::vector<FirmwareChoice>& firmwareChoices();
const FirmwareChoice& firmwareChoice(const std::string& id);   // the default (FM-1+VA) for an unknown id
std::string firmwareIdFor(const Identity& id);                // which choice a connected synth runs
constexpr const char* kDefaultFirmwareId = "baudgirl_fm1va";

// ---- versions -----------------------------------------------------------------------------
// Each firmware's releases the plugin knows, oldest first, and how well it works with each.
// A release it does not know is never refused: a newer one gets the newest one's support,
// marked untested; an older one is said to be older. To add a release: a line here, and a
// profile change (firmwares/<id>/) only if the protocol changed.
enum class Support {
    Current,      // the release the plugin plays and syncs with, tried on an FM-1
    Tested,       // tried on an FM-1
    Older,        // should work, with less (the note says what); not tried on an FM-1
    Deprecated,   // syncing needs a newer release (the note says which)
};
struct KnownVersion {
    int identity;             // its identity reply's number: FM-1_094 is 94
    const char* label;        // as its author names it: "V15", "0.94", "1.0"
    Support support;
    const char* note;         // what is different with it, or "" (said with the identity)
};
const std::vector<KnownVersion>& knownVersions(const std::string& firmwareId);   // oldest first
const KnownVersion& currentVersion(const std::string& firmwareId);
// What the plugin makes of a connected synth's release, as a line for the user.
struct VersionCheck {
    std::string firmwareId;
    const KnownVersion* known = nullptr;   // null: a release not in the list
    bool newer = false;                    // newer than every release in the list
    Support support = Support::Older;      // (a newer one: the newest's, untested)
    std::string text;                      // "FM-1_095: newer than ... (not tried yet)"
};
VersionCheck checkVersion(const Identity& id);

}  // namespace fm1
