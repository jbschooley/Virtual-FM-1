#include "FmSynth.h"

#include <algorithm>
#include <cstdarg>
#include <cmath>
#include <cstring>

#include "msfa/env.h"
#include "msfa/exp2.h"
#include "msfa/freqlut.h"
#include "msfa/pitchenv.h"
#include "msfa/porta.h"
#include "msfa/sin.h"
#include "msfa/synth.h"

// msfa declares this trace hook; nothing in this project traces.
void dexed_trace(const char*, const char*, ...) {}

namespace {
bool g_tablesReady = false;
void initTables() {
    if (g_tablesReady) return;
    Exp2::init();
    Tanh::init();
    Sin::init();
    g_tablesReady = true;
}
}  // namespace

FmSynth::FmSynth() : tuning_(createStandardTuning()) {
    initTables();
    std::memset(controllers_.values_, 0, sizeof controllers_.values_);
    controllers_.values_[kControllerPitch] = 0x2000;
    controllers_.values_[kControllerPitchRangeUp] = 2;
    controllers_.values_[kControllerPitchRangeDn] = 2;
    controllers_.values_[kControllerPitchStep] = 0;
    controllers_.masterTune = 0;
    controllers_.modwheel_cc = 0;
    controllers_.foot_cc = 0;
    controllers_.breath_cc = 0;
    controllers_.aftertouch_cc = 0;
    controllers_.portamento_enable_cc = false;
    controllers_.portamento_cc = 0;
    controllers_.portamento_gliss_cc = false;
    controllers_.mpeEnabled = false;
    controllers_.wheel.range = 99;
    controllers_.wheel.pitch = true;
    controllers_.wheel.amp = true;
    controllers_.refresh();
    controllers_.core = &core_;
    for (auto& v : voices_) v.note = std::make_unique<Dx7Note>(tuning_, nullptr);
    prepare(44100.0);
}

FmSynth::~FmSynth() = default;

void FmSynth::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    updateEnvCoefs();
    Freqlut::init(sampleRate);
    Lfo::init(sampleRate);
    PitchEnv::init(sampleRate);
    Env::init_sr(sampleRate);
    Porta::init_sr(sampleRate);
    reset();
}

void FmSynth::reset() {
    for (auto& v : voices_) {
        v.midiNote = -1;
        v.keydown = v.sustained = v.live = false;
    }
    lfo_.reset(patch_.data() + 137);
    blockPos_ = int(block_.size());
    block_.fill(0.0f);
}

void FmSynth::setPatch(const uint8_t* vced) {
    std::memcpy(patch_.data(), vced, kPatchBytes);
    patch_[155] = 0;
    for (auto& v : voices_)
        if (v.live) v.note->update(patch_.data(), v.midiNote, v.velocity, 1);
    lfo_.reset(patch_.data() + 137);
}

void FmSynth::noteOn(int midiNote, int velocity) {
    if (velocity == 0) { noteOff(midiNote); return; }
    // Prefer a silent voice, then a released one, then the same pitch; break ties
    // with the oldest key-down (msfa's allocation rule).
    int best = 0, bestScore = -1;
    for (int i = 0; i < kVoices; ++i) {
        const auto& v = voices_[i];
        int score = 0;
        if (!v.note->isPlaying()) score += 4;
        if (!v.keydown) score += 2;
        if (v.midiNote == midiNote) score += 1;
        if (score > bestScore || (score == bestScore && v.keydownSeq < voices_[best].keydownSeq)) {
            best = i;
            bestScore = score;
        }
    }
    bool anyDown = false;
    for (const auto& v : voices_) anyDown = anyDown || v.keydown;
    if (!anyDown) lfo_.keydown();   // LFO key sync

    auto& v = voices_[best];
    v.midiNote = midiNote;
    v.velocity = velocity;
    v.keydown = true;
    v.sustained = sustain_;
    v.live = true;
    v.keydownSeq = ++seq_;
    v.note->init(patch_.data(), midiNote, velocity, 1, &controllers_);
    if (patch_[136]) v.note->oscSync();
    envKeyDown(v);
}

void FmSynth::noteOff(int midiNote) {
    for (auto& v : voices_) {
        if (v.midiNote == midiNote && v.keydown) {
            v.keydown = false;
            if (sustain_) v.sustained = true;
            else { v.note->keyup(); envKeyUp(v); }
        }
    }
}

void FmSynth::allNotesOff() {
    for (auto& v : voices_) {
        if (v.keydown || v.sustained) {
            v.keydown = v.sustained = false;
            v.note->keyup();
            envKeyUp(v);
        }
    }
}

void FmSynth::allSoundOff() {
    reset();
}

void FmSynth::setPitchBend(int value14) {
    controllers_.values_[kControllerPitch] = std::clamp(value14, 0, 16383);
}

void FmSynth::setPitchBendRange(int up, int down) {
    controllers_.values_[kControllerPitchRangeUp] = std::clamp(up, 0, 24);
    controllers_.values_[kControllerPitchRangeDn] = std::clamp(down, 0, 24);
}

void FmSynth::setModWheel(int v)   { controllers_.modwheel_cc = v; controllers_.refresh(); }
void FmSynth::setBreath(int v)     { controllers_.breath_cc = v; controllers_.refresh(); }
void FmSynth::setFoot(int v)       { controllers_.foot_cc = v; controllers_.refresh(); }
void FmSynth::setAftertouch(int v) { controllers_.aftertouch_cc = v; controllers_.refresh(); }
void FmSynth::setMasterTune(int c) { controllers_.masterTune = c; }

void FmSynth::setSustain(bool down) {
    sustain_ = down;
    if (!down) {
        for (auto& v : voices_) {
            if (v.sustained && !v.keydown) {
                v.sustained = false;
                v.note->keyup();
                envKeyUp(v);
            }
        }
    }
}

int FmSynth::activeVoices() const {
    int n = 0;
    for (const auto& v : voices_) if (v.live) ++n;
    return n;
}

void FmSynth::setEnvelope(bool on, int a, int d, int s, int r) {
    envOn_ = on; envA_ = a; envD_ = d; envS_ = s; envR_ = r;
    updateEnvCoefs();
}

void FmSynth::updateEnvCoefs() {
    auto seconds = [](int v, double max) { double x = v / 100.0; return 0.004 + x * x * max; };
    double a = seconds(envA_, 2.0), d = seconds(envD_, 4.0), r = seconds(envR_, 4.0);
    envAttackInc_ = float(1.0 / (a * sampleRate_));
    envDecayCoef_ = float(std::exp(-1.0 / (d * sampleRate_ * 0.25)));    // ~ reaches sustain within d
    envReleaseCoef_ = float(std::exp(-1.0 / (r * sampleRate_ * 0.25)));
    envSustain_ = float(envS_) / 100.0f;
}

void FmSynth::envKeyDown(VoiceSlot& v) { v.envStage = 1; v.envLevel = 0.0f; }
void FmSynth::envKeyUp(VoiceSlot& v) { if (v.envStage != 0) v.envStage = 4; }

void FmSynth::renderBlock() {
    AlignedBuf<int32_t, N> mix;
    AlignedBuf<int32_t, N> one;
    for (int j = 0; j < N; ++j) mix.get()[j] = 0;
    int32_t lfoValue = lfo_.getsample();
    int32_t lfoDelay = lfo_.getdelay();
    for (auto& v : voices_) {
        if (!v.live) continue;
        if (!envOn_) {
            v.note->compute(mix.get(), lfoValue, lfoDelay, &controllers_);
            if (!v.note->isPlaying()) v.live = false;
            continue;
        }
        for (int j = 0; j < N; ++j) one.get()[j] = 0;
        v.note->compute(one.get(), lfoValue, lfoDelay, &controllers_);
        for (int j = 0; j < N; ++j) {
            switch (v.envStage) {
                case 1: v.envLevel += envAttackInc_; if (v.envLevel >= 1.0f) { v.envLevel = 1.0f; v.envStage = 2; } break;
                case 2: v.envLevel = envSustain_ + (v.envLevel - envSustain_) * envDecayCoef_;
                        if (v.envLevel - envSustain_ < 0.001f) { v.envLevel = envSustain_; v.envStage = 3; } break;
                case 3: break;
                case 4: v.envLevel *= envReleaseCoef_; if (v.envLevel < 0.0005f) { v.envLevel = 0.0f; v.envStage = 0; } break;
                default: break;
            }
            mix.get()[j] += int32_t(float(one.get()[j]) * v.envLevel);
        }
        if (!v.note->isPlaying() || v.envStage == 0) v.live = false;
    }
    for (int j = 0; j < N; ++j) {
        int32_t val = mix.get()[j] >> 4;
        int clip = val < -(1 << 24) ? -(1 << 24) : (val >= (1 << 24) ? (1 << 24) - 1 : val);
        block_[size_t(j)] = float(clip) / float(1 << 24);
    }
    blockPos_ = 0;
}

void FmSynth::render(float* out, int numSamples) {
    int i = 0;
    while (i < numSamples) {
        if (blockPos_ >= N) renderBlock();
        int take = std::min(N - blockPos_, numSamples - i);
        std::memcpy(out + i, block_.data() + blockPos_, size_t(take) * sizeof(float));
        blockPos_ += take;
        i += take;
    }
}
