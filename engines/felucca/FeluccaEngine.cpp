#include "FeluccaEngine.h"

#include <algorithm>
#include <memory>

// The compiled copies of each firmware (felucca_copies.inc and sloop_copies.inc, written by
// CMake: FELUCCA_COPIES and SLOOP_COPIES of them): first their declarations, then tables of
// their functions.
extern "C" {
#define FELUCCA_COPIES_DECLARE
#include "felucca_copies.inc"
#include "sloop_copies.inc"
#undef FELUCCA_COPIES_DECLARE
}

namespace {

#define FEL_ENTRY(ret, name, params) &FEL(name),
const FeluccaCopy kFeluccaCopies[] = {
#define FELUCCA_COPIES_TABLE
#include "felucca_copies.inc"
#undef FELUCCA_COPIES_TABLE
};
const FeluccaCopy kSloopCopies[] = {
#define FELUCCA_COPIES_TABLE
#include "sloop_copies.inc"
#undef FELUCCA_COPIES_TABLE
};
#undef FEL_ENTRY

std::mutex& poolLock() { static std::mutex m; return m; }

// A compiled copy and the instances that play in it. One instance per copy until there are
// more instances than copies; then instances share one, and the one that plays next has its
// state put back first (an instance always plays in the copy it started in: its state holds
// pointers into that copy).
struct Slot {
    std::mutex m;                          // held while one of its instances runs in it
    const FeluccaEngine* resident = nullptr;   // whose state is in the copy now
    int instances = 0;                     // how many have it as theirs (poolLock)
};
// One firmware's copies and their slots (never freed: an engine let go during static
// destruction still finds its slot)
struct Pool {
    const FeluccaCopy* copies;
    int count;
    Slot* slots;
    Pool(const FeluccaCopy* c, int n) : copies{c}, count{n}, slots{new Slot[size_t(n)]} {}
};
Pool& pool(FeluccaEngine::Flavor f) {
    static Pool felucca{kFeluccaCopies, int(sizeof kFeluccaCopies / sizeof kFeluccaCopies[0])};
    static Pool sloop{kSloopCopies, int(sizeof kSloopCopies / sizeof kSloopCopies[0])};
    return f == FeluccaEngine::Flavor::Sloop ? sloop : felucca;
}

}  // namespace

int FeluccaEngine::copies(Flavor f) { return pool(f).count; }

int FeluccaEngine::copiesInUse(Flavor f) {
    std::lock_guard<std::mutex> g(poolLock());
    auto& p = pool(f);
    int n = 0;
    for (int i = 0; i < p.count; ++i) n += p.slots[size_t(i)].instances > 0;
    return n;
}

int FeluccaEngine::instances(Flavor f) {
    std::lock_guard<std::mutex> g(poolLock());
    auto& p = pool(f);
    int n = 0;
    for (int i = 0; i < p.count; ++i) n += p.slots[size_t(i)].instances;
    return n;
}

FeluccaEngine::FeluccaEngine(Flavor flavor) : flavor_{flavor} {
    {
        std::lock_guard<std::mutex> g(poolLock());   // the copy with the fewest instances
        auto& p = pool(flavor_);
        int best = 0;
        for (int i = 1; i < p.count; ++i)
            if (p.slots[size_t(i)].instances < p.slots[size_t(best)].instances) best = i;
        p.slots[size_t(best)].instances++;
        index_ = best;
        core_ = &p.copies[best];
    }
    ctl_ = int(core_->ctl());
    saved_.resize(core_->state_bytes());   // its state while another instance plays in the copy
    auto g = bind();                       // as the program started: restore() and init() there
}

FeluccaEngine::~FeluccaEngine() {
    if (index_ < 0) return;
    auto& s = pool(flavor_).slots[size_t(index_)];
    {
        std::lock_guard<std::mutex> g(s.m);
        if (s.resident == this) s.resident = nullptr;
    }
    std::lock_guard<std::mutex> g(poolLock());
    s.instances--;
}

// This instance's state in its copy, with the copy's lock held: another instance's state is
// saved out first, then this one's put back (or, the first time, the copy started afresh).
std::unique_lock<std::mutex> FeluccaEngine::bind() const {
    auto& s = pool(flavor_).slots[size_t(index_)];
    std::unique_lock<std::mutex> l(s.m);
    if (s.resident != this) {
        if (s.resident != nullptr) core_->state_get(s.resident->saved_.data());
        if (fresh_) {
            core_->restore();
            core_->init();
            fresh_ = false;
        } else {
            core_->state_put(saved_.data());
        }
        s.resident = this;
        ++swaps_;
    }
    return l;
}

void FeluccaEngine::reset() {
    if (!core_) return;
    auto g = bind();
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
    auto g = bind();
    core_->midi(pkt);
}

void FeluccaEngine::render(float* left, float* right, int frames) {
    if (!core_) {
        std::fill(left, left + frames, 0.0f);
        std::fill(right, right + frames, 0.0f);
        return;
    }
    auto g = bind();
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
    auto g = bind();   // render's fm6_poll may be loading a patch into it
    core_->fm6_patch_get(uint32_t(track), v.data());
    return v;
}

void FeluccaEngine::setFm6Patch(int track, const std::array<uint8_t, 155>& v) {
    if (!core_) return;
    auto g = bind();   // many bytes: not while the audio side reads them
    core_->fm6_patch_set(uint32_t(track), v.data());
}

FeluccaEngine::Desc FeluccaEngine::paramDesc(int track, int id) const {
    fel_desc_t d{};
    if (!core_) return {};
    auto g = bind();   // (an engine parameter's description is the track's engine's)
    return core_->param_desc(uint32_t(track), uint32_t(id), &d) ? toDesc(d) : Desc{};
}

bool FeluccaEngine::paramRange(int track, int id, int& min, int& max) const {
    fel_desc_t d{};
    if (!core_) return false;
    auto g = bind();
    if (!core_->param_desc(uint32_t(track), uint32_t(id), &d)) return false;
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
    auto g = bind();
    return int(core_->param_get(uint32_t(track), uint32_t(id)));
}

void FeluccaEngine::setParam(int track, int id, int value) {
    if (!core_) return;
    auto g = bind();
    core_->param_set(uint32_t(track), uint32_t(id), value);
}

int FeluccaEngine::global(int id) const {
    if (!core_) return 0;
    auto g = bind();
    return int(core_->global_get(uint32_t(id)));
}

void FeluccaEngine::setGlobal(int id, int value) {
    if (!core_) return;
    auto g = bind();
    core_->global_set(uint32_t(id), value);
}

int FeluccaEngine::engineOf(int track) const {
    if (!core_) return 0;
    auto g = bind();
    return int(core_->engine_of(uint32_t(track)));
}

int FeluccaEngine::presetOf(int track) const {
    if (!core_) return 0;
    auto g = bind();
    return int(core_->preset_of(uint32_t(track)));
}

void FeluccaEngine::setEngine(int track, int engine) {
    if (!core_) return;
    auto g = bind();
    core_->set_engine(uint32_t(track), uint32_t(engine));
}

void FeluccaEngine::applyPreset(int track, int preset) {
    if (!core_) return;
    auto g = bind();
    core_->apply_preset(uint32_t(track), uint32_t(preset));
}

// ---- the virtual device ----

void FeluccaEngine::sysex(const uint8_t* b, int n) {
    if (!core_ || n < 2 || b[0] != 0xF0 || b[n - 1] != 0xF7) return;
    auto g = bind();
    feed(b, n);
}

void FeluccaEngine::feed(const uint8_t* b, int n) {
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
    auto g = bind();
    drain();
    out.swap(sxDone_);
    return out;
}

void FeluccaEngine::drain() {
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
    // what nobody took (pushes after ask(), with no live sync reading them): the newest only
    if (sxDone_.size() > 256) sxDone_.erase(sxDone_.begin(), sxDone_.end() - 256);
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
    auto g = bind();
    core_->button(uint32_t(label), down ? 1 : 0);
}

void FeluccaEngine::key(int index, bool down) {
    if (!core_ || index < 0) return;
    auto g = bind();
    core_->key(uint32_t(index), down ? 1 : 0);
}

void FeluccaEngine::knob(int role, int steps) {
    if (!core_ || role < 0) return;
    auto g = bind();
    core_->knob(uint32_t(role), steps);
}

void FeluccaEngine::draw(std::vector<uint16_t>& px) {
    px.resize(size_t(kScreen * kScreen));
    if (!core_) { std::fill(px.begin(), px.end(), uint16_t(0)); return; }
    {
        auto g = bind();
        core_->draw(px.data());
    }
    for (auto& v : px) v = uint16_t((v >> 8) | (v << 8));   // the LCD's big endian to ours
}

void FeluccaEngine::transport(bool play) {
    if (!core_) return;
    auto g = bind();
    core_->transport(play ? 1 : 0);
}

bool FeluccaEngine::playing() const {
    if (!core_) return false;
    auto g = bind();
    return core_->playing() != 0;
}

bool FeluccaEngine::object(int id, std::vector<uint8_t>& out) {
    out.clear();
    if (!core_ || id < 0) return false;
    auto g = bind();
    out.resize(core_->object_max());
    const int32_t n = core_->object_get(uint32_t(id), out.data(), uint32_t(out.size()));
    if (n < 0) { out.clear(); return false; }
    out.resize(size_t(n));
    return true;
}

int FeluccaEngine::putObject(int id, const std::vector<uint8_t>& bytes) {
    if (!core_ || id < 0) return 1;
    auto g = bind();
    return int(core_->object_put(uint32_t(id), bytes.data(), uint32_t(bytes.size())));
}

std::vector<std::vector<uint8_t>> FeluccaEngine::request(const std::vector<uint8_t>& m) {
    std::vector<std::vector<uint8_t>> out;
    if (!core_ || m.size() < 2 || m.front() != 0xF0 || m.back() != 0xF7) return out;
    auto g = bind();   // (held throughout: another thread's request cannot take this one's reply)
    core_->service_ready();   // a frame from the host still waiting would make Felucca drop this one: serve it first
    feed(m.data(), int(m.size()));
    core_->service();
    drain();
    out.swap(sxDone_);
    return out;
}

std::optional<std::vector<uint8_t>> FeluccaEngine::ask(const std::vector<uint8_t>& m) {
    if (!core_ || m.size() < 6 || m.front() != 0xF0 || m.back() != 0xF7) return std::nullopt;
    auto g = bind();
    core_->service_ready();
    feed(m.data(), int(m.size()));
    core_->service();
    drain();
    // the reply: the first message of the same command (F0 7D 46 4C cmd ..); the rest stays
    // for takeSysex (pushes it made, and what its main loop sent meanwhile)
    for (auto it = sxDone_.begin(); it != sxDone_.end(); ++it)
        if (it->size() >= 6 && std::equal(m.begin(), m.begin() + 5, it->begin())) {
            auto reply = std::move(*it);
            sxDone_.erase(it);
            return reply;
        }
    return std::nullopt;
}

int FeluccaEngine::selected() const {
    if (!core_) return 0;
    auto g = bind();
    return int(core_->selected());
}

void FeluccaEngine::select(int track) {
    if (!core_ || track < 0) return;
    auto g = bind();
    core_->select(uint32_t(track));
}

std::string FeluccaEngine::version() const { return core_ ? core_->version() : std::string(); }
