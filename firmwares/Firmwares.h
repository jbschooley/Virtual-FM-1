// The firmware profiles the plugin knows (core/Firmware.h), one folder each:
//   fm1_stock/       M-VAVE's own firmware
//   baudgirl_fm1va/  baud girl's FM-1+VA
//   felucca/         Felucca (Hügelton), and SLOOP (isod89's fork of it), which speaks the same
//                    editor protocol with its own backup commands
// firmwareFor() picks one from the synth's identity.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Firmware.h"

namespace fm1 {

std::unique_ptr<Firmware> makeStockFirmware();
std::unique_ptr<Firmware> makeFmVaFirmware();
std::unique_ptr<Firmware> makeFeluccaFirmware(const juce::String& name = "Felucca");   // Felucca, or "SLOOP"
std::unique_ptr<Firmware> makeUnsupportedFirmware(const juce::String& name, const juce::String& what = {});   // known by name only
std::string otherFirmwareName(const Identity& id);   // a firmware known by name only ("" none); firmwareIdFor "other"

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
bool isFirmwareChoice(const std::string& id);                 // a choice an instance can be set to
// Felucca, SLOOP or Melodee: played by FeluccaEngine (engines/felucca, engines/sloop, engines/melodee),
// synced through FeluccaSync
// A choice for a firmware's previous release, kept beside the current one for the projects made with
// it ("melodee_013": Melodee 0.13), and the firmware it is a release of ("melodee")
std::string familyOf(const std::string& choiceId);
// The releases kept beside a firmware's current one, each its own choice and its own compiled core.
// release: which of the firmware's kept releases (FeluccaEngine::Release: 1 Previous, 2 Previous2;
// the current release is 0), fixed once given: projects and host parameters go by it. savedBy: the
// starts of the "version" a project's music was saved with that this release, not the current one,
// reads ("v0." for Melodee 0.13). stateName: the project's child for its music ("Melodee013").
struct KeptRelease {
    const char* choiceId;     // "melodee_013"
    const char* family;       // "melodee"
    int release;              // 1, 2
    const char* label;        // "0.13": the release it plays and syncs with
    int identity;             // its FM-1_9XY
    const char* stateName;    // "Melodee013"
    std::vector<const char*> savedBy;   // "v0."
};
const std::vector<KeptRelease>& keptReleases();
const KeptRelease* keptRelease(const std::string& choiceId);                 // null: not a kept release
const KeptRelease* keptRelease(const std::string& family, int release);
// the kept release a project's music saved with this version is for (null: the current one)
const KeptRelease* keptReleaseFor(const std::string& family, const std::string& savedVersion);
inline bool isPreviousRelease(const std::string& choiceId) { return familyOf(choiceId) != choiceId; }
inline bool isFeluccaFamily(const std::string& id) { const auto f = familyOf(id); return f == "felucca" || f == "sloop" || f == "melodee"; }
// Whether a connected synth runs what an instance set to this choice plays: the same firmware and, for
// a previous release's choice, that release (the current choice takes any release of it, said by
// checkVersion)
bool runs(const Identity& synth, const std::string& choiceId);
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
// (choiceId: the instance's choice, if it is a kept previous release's: an FM-1 on that release is
// what it plays, nothing to say)
VersionCheck checkVersion(const Identity& id, const std::string& choiceId = {});
// Why the plugin only reads from this synth (sends nothing, no Live), or empty: SLOOP before 2.4
std::string writesRefused(const Identity& id, const std::string& choiceId = {});

}  // namespace fm1
