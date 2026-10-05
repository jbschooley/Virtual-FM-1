#include "Firmwares.h"

namespace fm1 {

const std::vector<FirmwareChoice>& firmwareChoices() {
    static const std::vector<FirmwareChoice> choices = {
        {"fm1_stock", "M-VAVE (stock)", false, true},
        {"baudgirl_fm1va", "FM-1+VA (baud girl)", true, true},
        {"felucca", "Felucca", false, false},
    };
    return choices;
}

const FirmwareChoice& firmwareChoice(const std::string& id) {
    for (const auto& c : firmwareChoices()) if (id == c.id) return c;
    return firmwareChoice(kDefaultFirmwareId);
}

std::string firmwareIdFor(const Identity& id) {
    if (id.isStock()) return "fm1_stock";
    if (id.version >= 900) return "felucca";   // a Felucca release X.Y reports FM-1_9XY (build.py --release; 0.4 beta: FM-1_904), others FM-1_900
    return "baudgirl_fm1va";
}

std::unique_ptr<Firmware> firmwareFor(const Identity& id) {
    const auto which = firmwareIdFor(id);
    std::unique_ptr<Firmware> f;
    if (which == "fm1_stock") f = makeStockFirmware();
    else if (which == "felucca") f = makeFeluccaFirmware();
    else f = makeFmVaFirmware();
    f->version = id.version;
    return f;
}

}  // namespace fm1
