// The firmware profiles the plugin knows (core/Firmware.h), one folder each:
//   fm1_stock/       M-VAVE's own firmware
//   baudgirl_fm1va/  baud girl's FM-1+VA
//   felucca/         Felucca (Hügelton)
// firmwareFor() picks one from the synth's identity.

#pragma once

#include <memory>

#include "Firmware.h"

namespace fm1 {

std::unique_ptr<Firmware> makeStockFirmware();
std::unique_ptr<Firmware> makeFmVaFirmware();
std::unique_ptr<Firmware> makeFeluccaFirmware();

}  // namespace fm1
