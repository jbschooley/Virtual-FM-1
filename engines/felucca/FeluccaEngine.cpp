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
    if (status < 0x80 || status >= 0xF0) return;   // channel messages only (SysEx: sysex(); no MIDI clock: the host's tempo instead)
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

std::vector<int> FeluccaEngine::enginesShown() const {
    std::vector<int> out;
    if (!core_) return out;
    for (uint32_t n = 0; n < core_->engines_shown(); ++n) out.push_back(int(core_->engine_shown(n)));
    return out;
}

int FeluccaEngine::fm6Engine() const { return core_ ? int(core_->fm6_engine()) : -1; }

std::array<uint8_t, 155> FeluccaEngine::fm6Patch(int track) const {
    std::array<uint8_t, 155> v{};
    if (!core_) return v;
    std::lock_guard<std::mutex> g(lock_);   // render's fm6_poll may be loading a patch into it
    core_->fm6_patch_get(uint32_t(track), v.data());
    return v;
}

void FeluccaEngine::setFm6Patch(int track, const std::array<uint8_t, 155>& v) {
    if (!core_) return;
    std::lock_guard<std::mutex> g(lock_);   // many bytes: not while the audio side reads them
    core_->fm6_patch_set(uint32_t(track), v.data());
}

FeluccaEngine::Desc FeluccaEngine::paramDesc(int track, int id) const {
    fel_desc_t d{};
    if (!core_) return {};
    return core_->param_desc(uint32_t(track), uint32_t(id), &d) ? toDesc(d) : Desc{};
}

bool FeluccaEngine::paramRange(int track, int id, int& min, int& max) const {
    fel_desc_t d{};
    if (!core_ || !core_->param_desc(uint32_t(track), uint32_t(id), &d)) return false;
    min = d.min; max = d.max;
    return true;
}

bool FeluccaEngine::globalRange(int id, int& min, int& max) const {
    fel_desc_t d{};
    if (!core_ || !core_->global_desc(uint32_t(id), &d)) return false;
    min = d.min; max = d.max;
    return true;
}

FeluccaEngine::Desc FeluccaEngine::globalDesc(int id) const {
    fel_desc_t d{};
    if (!core_) return {};
    return core_->global_desc(uint32_t(id), &d) ? toDesc(d) : Desc{};
}

int FeluccaEngine::param(int track, int id) const {
    if (!core_) return 0;
    return int(core_->param_get(uint32_t(track), uint32_t(id)));
}

void FeluccaEngine::setParam(int track, int id, int value) {
    if (!core_) return;
    core_->param_set(uint32_t(track), uint32_t(id), value);
}

int FeluccaEngine::global(int id) const {
    if (!core_) return 0;
    return int(core_->global_get(uint32_t(id)));
}

void FeluccaEngine::setGlobal(int id, int value) {
    if (!core_) return;
    core_->global_set(uint32_t(id), value);
}

int FeluccaEngine::engineOf(int track) const {
    if (!core_) return 0;
    return int(core_->engine_of(uint32_t(track)));
}

int FeluccaEngine::presetOf(int track) const {
    if (!core_) return 0;
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

// ---- the virtual device ----

void FeluccaEngine::sysex(const uint8_t* b, int n) {
    if (!core_ || n < 2 || b[0] != 0xF0 || b[n - 1] != 0xF7) return;
    std::lock_guard<std::mutex> g(lock_);
    // USB-MIDI SysEx packets: CIN 4 for three bytes that go on, 5/6/7 for the last one/two/three
    for (int i = 0; i < n; i += 3) {
        const int left = n - i, k = left > 3 ? 3 : left;
        const uint32_t cin = left > 3 ? 4u : uint32_t(4 + k);
        uint32_t pkt = cin;
        for (int j = 0; j < k; ++j) pkt |= uint32_t(b[i + j]) << (8 * (j + 1));
        core_->midi(pkt);
    }
}

std::vector<std::vector<uint8_t>> FeluccaEngine::takeSysex() {
    std::vector<std::vector<uint8_t>> out;
    if (!core_) return out;
    std::lock_guard<std::mutex> g(lock_);
    uint32_t pkts[64];
    for (;;) {
        const uint32_t got = core_->midi_out(pkts, 64);
        for (uint32_t i = 0; i < got; ++i) {
            const uint32_t p = pkts[i], cin = p & 15u;
            const int nb = cin == 4u || cin == 7u ? 3 : cin == 6u ? 2 : cin == 5u ? 1 : 0;
            for (int j = 0; j < nb; ++j) {
                const uint8_t byte = uint8_t(p >> (8 * (j + 1)));
                if (byte == 0xF0) sxOut_.clear();
                sxOut_.push_back(byte);
                if (byte == 0xF7) {
                    if (sxOut_.size() >= 2 && sxOut_[0] == 0xF0) sxDone_.push_back(sxOut_);
                    sxOut_.clear();
                }
            }
        }
        if (got < 64) break;
    }
    out.swap(sxDone_);
    return out;
}

std::vector<std::string> FeluccaEngine::buttonNames() const {
    std::vector<std::string> out;
    if (core_) for (uint32_t i = 0; i < core_->nbuttons(); ++i) out.push_back(core_->button_name(i));
    return out;
}

std::vector<std::string> FeluccaEngine::knobNames() const {
    std::vector<std::string> out;
    if (core_) for (uint32_t i = 0; i < core_->nknobs(); ++i) out.push_back(core_->knob_name(i));
    return out;
}

void FeluccaEngine::button(int label, bool down) {
    if (!core_ || label < 0) return;
    std::lock_guard<std::mutex> g(lock_);
    core_->button(uint32_t(label), down ? 1 : 0);
}

void FeluccaEngine::key(int index, bool down) {
    if (!core_ || index < 0) return;
    std::lock_guard<std::mutex> g(lock_);
    core_->key(uint32_t(index), down ? 1 : 0);
}

void FeluccaEngine::knob(int role, int steps) {
    if (!core_ || role < 0) return;
    std::lock_guard<std::mutex> g(lock_);
    core_->knob(uint32_t(role), steps);
}

void FeluccaEngine::draw(std::vector<uint16_t>& px) {
    px.resize(size_t(kScreen * kScreen));
    if (!core_) { std::fill(px.begin(), px.end(), uint16_t(0)); return; }
    {
        std::lock_guard<std::mutex> g(lock_);
        core_->draw(px.data());
    }
    for (auto& v : px) v = uint16_t((v >> 8) | (v << 8));   // the LCD's big endian to ours
}

void FeluccaEngine::transport(bool play) {
    if (!core_) return;
    std::lock_guard<std::mutex> g(lock_);
    core_->transport(play ? 1 : 0);
}

bool FeluccaEngine::playing() const { return core_ && core_->playing() != 0; }

bool FeluccaEngine::object(int id, std::vector<uint8_t>& out) {
    out.clear();
    if (!core_ || id < 0) return false;
    std::lock_guard<std::mutex> g(lock_);
    out.resize(core_->object_max());
    const int32_t n = core_->object_get(uint32_t(id), out.data(), uint32_t(out.size()));
    if (n < 0) { out.clear(); return false; }
    out.resize(size_t(n));
    return true;
}

int FeluccaEngine::putObject(int id, const std::vector<uint8_t>& bytes) {
    if (!core_ || id < 0) return 1;
    std::lock_guard<std::mutex> g(lock_);
    return int(core_->object_put(uint32_t(id), bytes.data(), uint32_t(bytes.size())));
}

std::vector<std::vector<uint8_t>> FeluccaEngine::request(const std::vector<uint8_t>& m) {
    if (core_) {   // a frame from the host still waiting would make Felucca drop this one: serve it first
        std::lock_guard<std::mutex> g(lock_);
        core_->service_ready();
    }
    sysex(m.data(), int(m.size()));
    if (core_) {
        std::lock_guard<std::mutex> g(lock_);
        core_->service();
    }
    return takeSysex();
}
