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
    controllers_.values_[kControllerPitchRangeUp] = 12;   // the FM-1's default bend range
    controllers_.values_[kControllerPitchRangeDn] = 12;
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
        if (v.live) v.note->update(patch_.data(), v.playedNote, v.velocity, 1);
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
    // The FM-1 scales MIDI velocity by 100/127 before the msfa engine sees it:
    // measured on FM-1_093 by recording its USB audio against renders of the same
    // presets (PIANO 1, BRASS 5; tests/compare_audio.py). 127 plays as 100.
    velocity = std::clamp((velocity * 100 + 63) / 127, 1, 127);
    // The patch's transpose (VCED 144, 24 = none) shifts every key, as on a DX7.
    // msfa leaves this to its host (Dexed applies it in its processor).
    int played = std::clamp(midiNote + int(patch_[144]) - 24, 0, 127);
    v.midiNote = midiNote;
    v.playedNote = played;
    v.velocity = velocity;
    v.keydown = true;
    v.sustained = sustain_;
    v.live = true;
    v.keydownSeq = ++seq_;
    v.note->init(patch_.data(), played, velocity, 1, &controllers_);
    if (patch_[136]) v.note->oscSync();
    envKeyDown(v);
    v.playedVelocity = velocity;
    filterKeyDown(v);
}

void FmSynth::noteOff(int midiNote) {
    for (auto& v : voices_) {
        if (v.midiNote == midiNote && v.keydown) {
            v.keydown = false;
            if (sustain_) v.sustained = true;
            else { v.note->keyup(); envKeyUp(v); filterKeyUp(v); }
        }
    }
}

void FmSynth::allNotesOff() {
    for (auto& v : voices_) {
        if (v.keydown || v.sustained) {
            v.keydown = v.sustained = false;
            v.note->keyup();
            envKeyUp(v);
            filterKeyUp(v);
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
                filterKeyUp(v);
            }
        }
    }
}

int FmSynth::activeVoices() const {
    int n = 0;
    for (const auto& v : voices_) if (v.live) ++n;
    return n;
}

void FmSynth::setFilter(const Filter& f) { filter_ = f; }

double FmSynth::cutoffHz(int setting) {
    // 0..100 spread exponentially over 20 Hz .. 20 kHz (an approximation of the
    // firmware's curve; the synth shows this setting in Hz)
    return 20.0 * std::pow(1000.0, std::clamp(setting, 0, 100) / 100.0);
}

void FmSynth::filterKeyDown(VoiceSlot& v) {
    v.fenvStage = 1;
    v.fenv = 0.0f;
    v.s1[0] = v.s1[1] = v.s2[0] = v.s2[1] = 0.0f;
}

void FmSynth::filterKeyUp(VoiceSlot& v) { if (v.fenvStage != 0) v.fenvStage = 3; }

// One sample through the filter. The envelope follows the manual: Shape 0 opens and
// closes at once, 50 rises over 1.5 s and closes at once, 100 opens at once and closes
// over 1.5 s; Decay lets it fall while the key is held, 2 s at 0 to 0.2 s at 100.
float FmSynth::filterSample(VoiceSlot& v, float x, float baseOct, float lfoOct) {
    const float sr = float(sampleRate_);
    const float sh = float(filter_.shape) / 100.0f;
    const float riseS = 1.5f * (sh <= 0.5f ? sh * 2.0f : (1.0f - sh) * 2.0f);
    const float closeS = 1.5f * std::max(0.0f, sh * 2.0f - 1.0f);
    const float decayS = 2.0f - 1.8f * float(filter_.decay) / 100.0f;
    switch (v.fenvStage) {
        case 1: v.fenv += riseS > 0.001f ? 1.0f / (riseS * sr) : 1.0f; if (v.fenv >= 1.0f) { v.fenv = 1.0f; v.fenvStage = 2; } break;
        case 2: if (filter_.decay > 0) v.fenv *= std::exp(-1.0f / (decayS * sr * 0.25f)); break;
        case 3: if (closeS > 0.001f) v.fenv *= std::exp(-1.0f / (closeS * sr * 0.25f)); else v.fenv = 0.0f; break;
        default: break;
    }
    float oct = baseOct + lfoOct + v.fenv * 8.0f * float(filter_.envelope) / 100.0f;
    float fc = std::clamp(float(cutoffHz(filter_.cutoff)) * std::exp2(oct), 20.0f, sr * 0.45f);
    float g = std::tan(3.14159265f * fc / sr);
    // damping at resonance 0 fitted to FM-1_093 recordings of LP24 at cutoff 30/50/70
    float k = 1.55f - 1.45f * float(filter_.resonance) / 100.0f;
    float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
    int stages = filter_.type == 1 ? 2 : 1;
    float in = x, out = 0.0f;
    for (int st = 0; st < stages; ++st) {
        float v3 = in - v.s2[st];
        float v1 = a1 * v.s1[st] + a2 * v3;
        float v2 = v.s2[st] + a2 * v.s1[st] + a3 * v3;
        v.s1[st] = 2.0f * v1 - v.s1[st];
        v.s2[st] = 2.0f * v2 - v.s2[st];
        switch (filter_.type) {
            case 2: out = v1 * k; break;                 // band pass, unity at the peak
            case 3: out = in - k * v1 - v2; break;       // high pass
            default: out = v2; break;                    // low pass (12, or 24 as two stages)
        }
        in = out;
    }
    return out;
}

void FmSynth::setOperatorEnabled(int vcedIndex, bool on) {
    if (vcedIndex >= 0 && vcedIndex < 6) controllers_.opSwitch[vcedIndex] = on ? '1' : '0';
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
        if (!envOn_ && !filter_.on) {
            v.note->compute(mix.get(), lfoValue, lfoDelay, &controllers_);
            if (!v.note->isPlaying()) v.live = false;
            continue;
        }
        for (int j = 0; j < N; ++j) one.get()[j] = 0;
        v.note->compute(one.get(), lfoValue, lfoDelay, &controllers_);
        if (filter_.on) {
            static const float kTrack[4] = {0.0f, 0.33f, 0.67f, 1.0f};
            float baseOct = kTrack[filter_.keyTrack & 3] * float(v.playedNote - 60) / 12.0f
                          + 4.0f * float(filter_.velocity) / 100.0f * (float(v.playedVelocity) / 127.0f - 1.0f);
            float lfoOct = 4.0f * float(filter_.lfo) / 100.0f * (float(lfoValue) / float(1 << 24) * 2.0f - 1.0f);
            for (int j = 0; j < N; ++j)
                one.get()[j] = int32_t(filterSample(v, float(one.get()[j]), baseOct, lfoOct));
        }
        if (!envOn_) {
            for (int j = 0; j < N; ++j) mix.get()[j] += one.get()[j];
            if (!v.note->isPlaying()) v.live = false;
            continue;
        }
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
