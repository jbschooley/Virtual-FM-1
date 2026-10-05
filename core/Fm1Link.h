// Fm1Link -- a MIDI connection to an FM-1: open its ports, send SysEx, and
// wait for the reply a request expects, asking again after silence.
//
// The same behaviour as baud girl's link.js `ask`: send, wait up to `timeout`
// for a frame the matcher accepts, resend, up to `tries` times. Blocking; use
// it from a worker thread (Fm1Session), never from the audio or message thread.

#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include <juce_audio_devices/juce_audio_devices.h>

#include "Fm1Codec.h"

class Fm1Link : private juce::MidiInputCallback {
public:
    Fm1Link();
    ~Fm1Link() override;

    struct Ports {
        juce::String inputId, inputName, outputId, outputName;
    };

    // Ports that look like an FM-1 (her otaweb.js name patterns), over USB if there is one, else Bluetooth.
    static std::optional<Ports> findFm1();
    static juce::Array<juce::MidiDeviceInfo> inputs()  { return juce::MidiInput::getAvailableDevices(); }
    static juce::Array<juce::MidiDeviceInfo> outputs() { return juce::MidiOutput::getAvailableDevices(); }

    bool open(const juce::String& inputId, const juce::String& outputId);
    void close();
    bool isOpen() const { return in_ != nullptr && out_ != nullptr; }
    Ports ports() const { return ports_; }

    void send(const fm1::Bytes& sysex);   // whole message including F0/F7
    void sendRaw(const fm1::Bytes& midi);  // any complete MIDI message

    // Send `msg`, return the first SysEx frame `match` accepts (its result),
    // resending after `timeoutMs` of silence, up to `tries` times.
    template <typename T>
    std::optional<T> ask(const fm1::Bytes& msg,
                         const std::function<std::optional<T>(const fm1::Bytes&)>& match,
                         int timeoutMs = 1500, int tries = 3);

    // Every SysEx frame received, for a listener, called on the MIDI input thread. Set and
    // cleared under a lock the call holds too: once cleared, it is not running and will not.
    void setSysexListener(std::function<void(const fm1::Bytes&)> fn) {
        std::lock_guard<std::mutex> l(listenerMutex_);
        onSysex_ = std::move(fn);
    }

private:
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage&) override;

    std::unique_ptr<juce::MidiInput> in_;
    std::unique_ptr<juce::MidiOutput> out_;
    Ports ports_;

    std::mutex sendMutex_;             // sends come from the session thread and the message thread
    std::mutex mutex_;
    std::mutex listenerMutex_;
    std::function<void(const fm1::Bytes&)> onSysex_;
    std::vector<fm1::Bytes> pending_;   // frames received since the last ask began
    juce::WaitableEvent frameArrived_;
};

template <typename T>
std::optional<T> Fm1Link::ask(const fm1::Bytes& msg,
                              const std::function<std::optional<T>(const fm1::Bytes&)>& match,
                              int timeoutMs, int tries) {
    if (!isOpen()) return std::nullopt;
    for (int attempt = 0; attempt < tries; ++attempt) {
        { std::lock_guard<std::mutex> l(mutex_); pending_.clear(); }
        frameArrived_.reset();
        send(msg);
        auto deadline = juce::Time::getMillisecondCounter() + juce::uint32(timeoutMs);
        for (;;) {
            std::vector<fm1::Bytes> frames;
            { std::lock_guard<std::mutex> l(mutex_); frames.swap(pending_); }
            for (const auto& f : frames) {
                try {
                    if (auto v = match(f)) return v;
                } catch (const fm1::CodecError&) {
                    // damaged frame that looked like ours: keep waiting, the resend covers it
                }
            }
            auto now = juce::Time::getMillisecondCounter();
            if (now >= deadline) break;
            frameArrived_.wait(int(deadline - now));
            frameArrived_.reset();
        }
    }
    return std::nullopt;
}
