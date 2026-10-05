// Minimal replacement for Dexed's tuning.h: standard 12-TET only.
// Formula from Google's msfa freqlut/dx7note (Apache-2.0).
#pragma once
#include <memory>
#include <cstdint>

class TuningState {
public:
    virtual ~TuningState() {}
    virtual int32_t midinote_to_logfreq(int midinote) = 0;
    virtual bool is_standard_tuning() { return true; }
    virtual int scale_length() { return 12; }
};

std::shared_ptr<TuningState> createStandardTuning();
