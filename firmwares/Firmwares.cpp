#include "Firmwares.h"

namespace fm1 {

std::unique_ptr<Firmware> firmwareFor(const Identity& id) {
    std::unique_ptr<Firmware> f;
    if (id.isStock()) f = makeStockFirmware();
    else if (id.version >= 900) f = makeFeluccaFirmware();   // Felucca X.Y reports FM-1_9XY (0.4 beta: FM-1_904)
    else f = makeFmVaFirmware();
    f->version = id.version;
    return f;
}

}  // namespace fm1
