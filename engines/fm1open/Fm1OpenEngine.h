// Fm1OpenEngine -- M-VAVE's FM1 Open V12 (engines/fm1open, fm1open_core.c) in the plugin: one
// instance of it, with its own copy of the firmware's state (fel_state.py: the code is shared by
// every instance, the state is not). It plays at the FM-1's 44.1 kHz in blocks of 128 frames, takes
// the host's MIDI and the Device tab's panel, and keeps its projects in its own flash (saved with the
// host's project, projects() / setProjects()). No host parameters and no sync with a device yet.
#pragma once

#include <array>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "DevicePanel.h"

class Fm1OpenEngine : public DevicePanel {
public:
    static constexpr double kRate = 44100.0;
    static constexpr int kProjectBytes = 4064;   // an .fm1 project
    static int instances();                      // alive now

    Fm1OpenEngine();                             // powered on with empty storage and its demo samples
    ~Fm1OpenEngine() override;
    Fm1OpenEngine(const Fm1OpenEngine&) = delete;
    Fm1OpenEngine& operator=(const Fm1OpenEngine&) = delete;
    bool valid() const { return state_ != nullptr; }   // false only if fel_state's layout check failed

    // ---- audio thread ----
    void midi(const uint8_t* bytes, int size);   // a channel message, or a whole SysEx message
    void render(float* left, float* right, int frames);

    // ---- any thread (each takes the lock) ----
    std::vector<std::string> buttonNames() const override;
    std::vector<std::string> knobNames() const override;
    void button(int label, bool down) override;
    void key(int index, bool down) override;
    void knob(int role, int steps) override;
    void draw(std::vector<uint16_t>& rgb565) override;
    std::vector<uint8_t> leds() const override;
    void transport(bool play);                   // presses PLAY when it is not as asked
    bool playing() const;
    std::string projectName() const;
    // the demo projects it comes with (FACTORY on HOME > PRESETS), by name; one's bytes
    std::vector<std::string> factoryNames() const;
    std::optional<std::vector<uint8_t>> factoryProject(int index) const;
    std::optional<std::vector<uint8_t>> exportProject() const;   // the project playing, as an .fm1 file
    bool importProject(const std::vector<uint8_t>& bytes);       // into the project playing
    // its stored projects and settings (the flash before the sample bank): saved with the host's
    // project; set powers it on again from them. The sample bank stays its demo bank (not saved).
    std::vector<uint8_t> projects() const;
    bool setProjects(const std::vector<uint8_t>& bytes);
    void reset();                                // powered on again with empty storage
    uint32_t heapUsed() const;                   // (for the tests)

private:
    std::unique_lock<std::mutex> bind() const;
    struct Free { void operator()(void* p) const; };
    std::unique_ptr<void, Free> state_;
    mutable std::mutex lock_;
    std::array<float, 2 * 128> block_{};
    int blockPos_ = 128;
};
