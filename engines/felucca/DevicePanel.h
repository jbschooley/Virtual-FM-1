// DevicePanel -- a firmware's front panel and screen, as the Device tab draws and plays it
// (plugin/FeluccaPanel.cpp FeluccaDeviceView): FeluccaEngine's (Felucca, SLOOP, Melodee) and
// Fm1OpenEngine's (M-VAVE's FM1 Open). Any thread: each takes its engine's lock.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

class DevicePanel {
public:
    virtual ~DevicePanel() = default;
    virtual std::vector<std::string> buttonNames() const = 0;   // the 14 buttons, by the panel's labels
    virtual std::vector<std::string> knobNames() const = 0;     // the encoders, by role (SELECT ...)
    virtual void button(int label, bool down) = 0;
    virtual void key(int index, bool down) = 0;                 // the 27 keys from the lowest
    virtual void knob(int role, int steps) = 0;
    virtual void draw(std::vector<uint16_t>& rgb565) = 0;       // 240 x 240, RGB565 (native byte order)
    // the 14 buttons by label, the 27 keys, PLAY's green LED; each 0 dark, 1 glow, 2 dim, 3 lit
    virtual std::vector<uint8_t> leds() const = 0;
};
