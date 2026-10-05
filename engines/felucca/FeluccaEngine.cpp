#include "FeluccaEngine.h"

#include <algorithm>

// The compiled copies (felucca_copies.inc, written by CMake: FELUCCA_COPIES of them):
// first their declarations, then a table of their functions.
extern "C" {
#define FELUCCA_COPIES_DECLARE
#include "felucca_copies.inc"
#undef FELUCCA_COPIES_DECLARE
}

namespace {

#define FEL_ENTRY(ret, name, params) &FEL(name),
const FeluccaCopy kCopies[] = {
#define FELUCCA_COPIES_TABLE
#include "felucca_copies.inc"
#undef FELUCCA_COPIES_TABLE
};
#undef FEL_ENTRY

constexpr int kCount = int(sizeof kCopies / sizeof kCopies[0]);

std::mutex& poolLock() { static std::mutex m; return m; }
std::array<bool, kCount>& inUse() { static std::array<bool, kCount> used{}; return used; }

}  // namespace

int FeluccaEngine::copies() { return kCount; }

int FeluccaEngine::copiesInUse() {
    std::lock_guard<std::mutex> g(poolLock());
    return int(std::count(inUse().begin(), inUse().end(), true));
}

FeluccaEngine::FeluccaEngine() {
    std::lock_guard<std::mutex> g(poolLock());
    for (int i = 0; i < kCount; ++i)
        if (!inUse()[size_t(i)]) {
            inUse()[size_t(i)] = true;
            index_ = i;
            core_ = &kCopies[i];
            break;
        }
    if (core_ != nullptr) {
        ctl_ = int(core_->ctl());
        core_->restore();   // as the program started: nothing left from the copy's last instance
        core_->init();
    }
}

FeluccaEngine::~FeluccaEngine() {
    if (index_ < 0) return;
    std::lock_guard<std::mutex> g(poolLock());
    inUse()[size_t(index_)] = false;
}

void FeluccaEngine::reset() {
    if (!core_) return;
    std::lock_guard<std::mutex> g(lock_);
    core_->restore();
    core_->init();
    blockPos_ = ctl_;
}

void FeluccaEngine::midi(const uint8_t* b, int n) {
    if (!core_ || n < 1) return;
    const uint8_t status = b[0];
    if (status < 0x80 || status >= 0xF0) return;   // channel messages only; Felucca takes no clock or SysEx
    // a USB-MIDI event packet: cable 0, code index = the status nibble
    const uint32_t pkt = uint32_t(status >> 4) | uint32_t(status) << 8 | uint32_t(n > 1 ? b[1] : 0) << 16 | uint32_t(n > 2 ? b[2] : 0) << 24;
    std::lock_guard<std::mutex> g(lock_);
    core_->midi(pkt);
}

void FeluccaEngine::render(float* left, float* right, int frames) {
    if (!core_) {
        std::fill(left, left + frames, 0.0f);
        std::fill(right, right + frames, 0.0f);
        return;
    }
    std::lock_guard<std::mutex> g(lock_);
    constexpr float scale = 1.0f / 32768.0f;   // Felucca's output is 16-bit full scale
    for (int i = 0; i < frames; ++i) {
        if (blockPos_ >= ctl_) {
            core_->render(block_.data(), uint32_t(ctl_));
            blockPos_ = 0;
        }
        left[i] = float(block_[size_t(2 * blockPos_)]) * scale;
        right[i] = float(block_[size_t(2 * blockPos_ + 1)]) * scale;
        ++blockPos_;
    }
}

static FeluccaEngine::Desc toDesc(const fel_desc_t& d) {
    FeluccaEngine::Desc out;
    out.label = d.label ? d.label : "";
    out.unit = d.unit ? d.unit : "";
    out.fmt = d.fmt;
    out.min = d.min;
    out.max = d.max;
    out.def = d.def;
    for (uint32_t i = 0; d.names && i < d.nnames; ++i) out.names.push_back(d.names[i] ? d.names[i] : "");
    return out;
}

int FeluccaEngine::tracks() const { return core_ ? int(core_->ntracks()) : 0; }
int FeluccaEngine::parts() const { return core_ ? int(core_->nparts()) : 0; }
int FeluccaEngine::engines() const { return core_ ? int(core_->nengines()) : 0; }
int FeluccaEngine::paramCount() const { return core_ ? int(core_->pcount()) : 0; }
int FeluccaEngine::firstEngineParam() const { return core_ ? int(core_->pe0()) : 0; }
int FeluccaEngine::globalCount() const { return core_ ? int(core_->gcount()) : 0; }
std::string FeluccaEngine::engineName(int e) const { return core_ ? core_->engine_name(uint32_t(e)) : ""; }
std::string FeluccaEngine::enginePage(int e, int page) const { return core_ ? core_->engine_page(uint32_t(e), uint32_t(page)) : ""; }

std::vector<std::string> FeluccaEngine::presetNames(int e) const {
    std::vector<std::string> out;
    if (!core_) return out;
    for (uint32_t i = 0; i < core_->npresets(uint32_t(e)); ++i) out.push_back(core_->preset_name(uint32_t(e), i));
    return out;
}

FeluccaEngine::Desc FeluccaEngine::paramDesc(int track, int id) const {
    fel_desc_t d{};
    if (!core_) return {};
    std::lock_guard<std::mutex> g(lock_);
    return core_->param_desc(uint32_t(track), uint32_t(id), &d) ? toDesc(d) : Desc{};
}

FeluccaEngine::Desc FeluccaEngine::globalDesc(int id) const {
    fel_desc_t d{};
    if (!core_) return {};
    return core_->global_desc(uint32_t(id), &d) ? toDesc(d) : Desc{};
}

int FeluccaEngine::param(int track, int id) const {
    if (!core_) return 0;
    std::lock_guard<std::mutex> g(lock_);
    return int(core_->param_get(uint32_t(track), uint32_t(id)));
}

void FeluccaEngine::setParam(int track, int id, int value) {
    if (!core_) return;
    std::lock_guard<std::mutex> g(lock_);
    core_->param_set(uint32_t(track), uint32_t(id), value);
}

int FeluccaEngine::global(int id) const {
    if (!core_) return 0;
    std::lock_guard<std::mutex> g(lock_);
    return int(core_->global_get(uint32_t(id)));
}

void FeluccaEngine::setGlobal(int id, int value) {
    if (!core_) return;
    std::lock_guard<std::mutex> g(lock_);
    core_->global_set(uint32_t(id), value);
}

int FeluccaEngine::engineOf(int track) const {
    if (!core_) return 0;
    std::lock_guard<std::mutex> g(lock_);
    return int(core_->engine_of(uint32_t(track)));
}

int FeluccaEngine::presetOf(int track) const {
    if (!core_) return 0;
    std::lock_guard<std::mutex> g(lock_);
    return int(core_->preset_of(uint32_t(track)));
}

void FeluccaEngine::setEngine(int track, int engine) {
    if (!core_) return;
    std::lock_guard<std::mutex> g(lock_);
    core_->set_engine(uint32_t(track), uint32_t(engine));
}

void FeluccaEngine::applyPreset(int track, int preset) {
    if (!core_) return;
    std::lock_guard<std::mutex> g(lock_);
    core_->apply_preset(uint32_t(track), uint32_t(preset));
}
