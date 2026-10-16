#include "Fm1OpenEngine.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>

#define FEL_PREFIX fo_
#include "fm1open_core.h"

namespace {
std::atomic<int> gInstances{0};
// the panel as fm1.h numbers it (buttons 0..13) and as the workbench lays it out (sdk/Simulator.js)
const char* const kButtons[14] = {"OCT-", "OCT+", "FX", "SCL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"};
// the encoders by role, as FeluccaEngine names them, and their numbers in fm1_controls
const char* const kKnobs[7] = {"SELECT", "ALGORITHM", "PRESETS", "KNOB 1", "KNOB 2", "KNOB 3", "KNOB 4"};
const int kEncoder[7] = {0, 1, 6, 2, 3, 4, 5};
constexpr int kPlay = 12;
constexpr int kLeds = 168;   // sim_state: the 41 LEDs (14 buttons, 27 keys) from here
bool anyPlaying(const uint8_t* s) {
    for (int t = 0; t < 8; ++t) if (s[8 + t * 20 + 6]) return true;
    return false;
}
}  // namespace

int Fm1OpenEngine::instances() { return gInstances.load(); }

void Fm1OpenEngine::Free::operator()(void* p) const {
   #ifdef _WIN32
    _aligned_free(p);
   #else
    std::free(p);
   #endif
}

Fm1OpenEngine::Fm1OpenEngine() {
    const size_t bytes = size_t(fo_state_size());   // (a multiple of 16)
   #ifdef _WIN32
    void* p = _aligned_malloc(bytes, 16);
   #else
    void* p = nullptr;
    if (posix_memalign(&p, 16, bytes) != 0) p = nullptr;
   #endif
    if (!p) return;
    if (!fo_state_init(p)) { std::free(p); return; }   // fel_state's layout check failed: never run it
    state_.reset(p);
    ++gInstances;
    auto g = bind();
    fo_fo_boot(1);
}

Fm1OpenEngine::~Fm1OpenEngine() {
    if (state_) --gInstances;
}

std::unique_lock<std::mutex> Fm1OpenEngine::bind() const {
    std::unique_lock<std::mutex> l(lock_);
    if (state_) fo_bind(state_.get());
    return l;
}

void Fm1OpenEngine::reset() {
    if (!state_) return;
    auto g = bind();
    fo_state_init(state_.get());
    fo_fo_boot(1);
    blockPos_ = FO_BLOCK_FRAMES;
}

void Fm1OpenEngine::midi(const uint8_t* b, int n) {
    if (!state_ || n < 1) return;
    if (b[0] != 0xF0 && (b[0] < 0x80 || n > 3)) return;   // a channel or system message, or SysEx
    auto g = bind();
    fo_fo_midi(b, unsigned(n));
}

void Fm1OpenEngine::render(float* left, float* right, int frames) {
    if (!state_) {
        std::fill(left, left + frames, 0.0f);
        std::fill(right, right + frames, 0.0f);
        return;
    }
    auto g = bind();
    for (int i = 0; i < frames; ++i) {
        if (blockPos_ >= FO_BLOCK_FRAMES) {
            const float* o = fo_fo_render();
            std::copy(o, o + 2 * FO_BLOCK_FRAMES, block_.begin());
            blockPos_ = 0;
        }
        left[i] = block_[size_t(2 * blockPos_)];
        right[i] = block_[size_t(2 * blockPos_ + 1)];
        ++blockPos_;
    }
}

std::vector<std::string> Fm1OpenEngine::buttonNames() const { return {std::begin(kButtons), std::end(kButtons)}; }
std::vector<std::string> Fm1OpenEngine::knobNames() const { return {std::begin(kKnobs), std::end(kKnobs)}; }

void Fm1OpenEngine::button(int label, bool down) {
    if (!state_ || label < 0 || label >= 14) return;
    auto g = bind();
    fo_fo_control(0, unsigned(label), down ? 1 : 0);
}

void Fm1OpenEngine::key(int index, bool down) {
    if (!state_ || index < 0 || index >= 27) return;
    auto g = bind();
    fo_fo_control(1, unsigned(index), down ? 1 : 0);
}

void Fm1OpenEngine::knob(int role, int steps) {
    if (!state_ || role < 0 || role >= 7 || steps == 0) return;
    auto g = bind();
    fo_fo_control(2, unsigned(kEncoder[role]), std::clamp(steps, -127, 127));
}

void Fm1OpenEngine::draw(std::vector<uint16_t>& rgb565) {
    rgb565.resize(size_t(FO_SCREEN * FO_SCREEN));
    if (!state_) { std::fill(rgb565.begin(), rgb565.end(), uint16_t(0)); return; }
    auto g = bind();
    const uint16_t* p = fo_fo_pixels();
    std::copy(p, p + FO_SCREEN * FO_SCREEN, rgb565.begin());
}

std::vector<uint8_t> Fm1OpenEngine::leds() const {
    std::vector<uint8_t> out(14 + 27 + 1, 0);
    if (!state_) return out;
    auto g = bind();
    const uint8_t* s = fo_fo_state();
    for (int i = 0; i < 41; ++i) out[size_t(i)] = s[kLeds + i] ? 3 : 0;
    return out;   // (its PLAY LED is one colour: [41], the green, stays dark)
}

bool Fm1OpenEngine::playing() const {
    if (!state_) return false;
    auto g = bind();
    return anyPlaying(fo_fo_state());
}

void Fm1OpenEngine::transport(bool play) {
    if (!state_) return;
    auto g = bind();
    if (anyPlaying(fo_fo_state()) == play) return;
    fo_fo_control(0, kPlay, 1);
    fo_fo_control(0, kPlay, 0);
}

std::string Fm1OpenEngine::projectName() const {
    if (!state_) return {};
    auto g = bind();
    std::string s = fo_fo_project_name();
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

std::vector<std::string> Fm1OpenEngine::factoryNames() const {
    std::vector<std::string> out;
    if (!state_) return out;
    auto g = bind();
    for (unsigned i = 0; i < 64; ++i) {
        const char* n = fo_fo_factory_name(i);
        if (!n) break;
        out.emplace_back(n);
    }
    return out;
}

std::optional<std::vector<uint8_t>> Fm1OpenEngine::factoryProject(int index) const {
    if (!state_ || index < 0) return std::nullopt;
    std::vector<uint8_t> b(static_cast<size_t>(kProjectBytes));
    auto g = bind();
    if (fo_fo_factory_project(unsigned(index), b.data()) != 0) return std::nullopt;
    return b;
}

std::optional<std::vector<uint8_t>> Fm1OpenEngine::exportProject() const {
    if (!state_) return std::nullopt;
    std::vector<uint8_t> b(static_cast<size_t>(kProjectBytes));
    auto g = bind();
    if (fo_fo_export(b.data()) != 0) return std::nullopt;
    return b;
}

bool Fm1OpenEngine::importProject(const std::vector<uint8_t>& bytes) {
    if (!state_ || bytes.empty()) return false;
    auto g = bind();
    return fo_fo_import(bytes.data(), unsigned(bytes.size())) == 0;
}

std::vector<uint8_t> Fm1OpenEngine::projects() const {
    if (!state_) return {};
    auto g = bind();
    const uint8_t* f = fo_fo_flash();
    return {f, f + FO_PROJECTS_BYTES};
}

bool Fm1OpenEngine::setProjects(const std::vector<uint8_t>& bytes) {
    if (!state_ || bytes.size() != size_t(FO_PROJECTS_BYTES)) return false;
    auto g = bind();
    fo_state_init(state_.get());
    fo_fo_boot(1);   // (empty storage and the demo sample bank, as the device after an install)
    std::memcpy(fo_fo_flash(), bytes.data(), bytes.size());
    fo_fo_boot(0);   // powered on again from them
    blockPos_ = FO_BLOCK_FRAMES;
    return true;
}

uint32_t Fm1OpenEngine::heapUsed() const {
    if (!state_) return 0;
    auto g = bind();
    return fo_fo_heap_used();
}
