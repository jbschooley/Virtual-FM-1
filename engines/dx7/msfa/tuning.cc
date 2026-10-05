#include "tuning.h"

namespace {
class StandardTuning : public TuningState {
public:
    int32_t midinote_to_logfreq(int midinote) override {
        const int base = 50857777;  // (1 << 24) * (log(440) / log(2) - 69/12)
        const int step = (1 << 24) / 12;
        return base + step * midinote;
    }
};
}

std::shared_ptr<TuningState> createStandardTuning() {
    return std::make_shared<StandardTuning>();
}
