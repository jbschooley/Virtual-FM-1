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
    const char* version;      // the release the plugin plays and syncs with ("" while not known)
    std::string label() const { return *version ? std::string(name) + " " + version : std::string(name); }
};
const std::vector<FirmwareChoice>& firmwareChoices();
const FirmwareChoice& firmwareChoice(const std::string& id);   // the default (FM-1+VA) for an unknown id
std::string firmwareIdFor(const Identity& id);                // which choice a connected synth runs
constexpr const char* kDefaultFirmwareId = "baudgirl_fm1va";

}  // namespace fm1
