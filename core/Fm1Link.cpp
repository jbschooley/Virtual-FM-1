#include "Fm1Link.h"

#include <cstring>

Fm1Link::Fm1Link() = default;
Fm1Link::~Fm1Link() { close(); }

std::optional<Fm1Link::Ports> Fm1Link::findFm1() {
    static const char* patterns[] = {"fm-1", "fm1", "ota-fm", "usb composite device", "usb-midi"};
    // USB first: FM-1+VA answers sync requests only over USB. A Bluetooth MIDI
    // connection (already paired, say, when the app restarts) still plays notes.
    auto pick = [](const juce::Array<juce::MidiDeviceInfo>& list, bool bluetooth) -> std::optional<juce::MidiDeviceInfo> {
        for (const char* pat : patterns)
            for (const auto& d : list) {
                auto low = d.name.toLowerCase();
                bool ble = low.contains("bluetooth") || low.containsWholeWord("ble");   // not "cable", "Ableton"
                if (low.contains(pat) && ble == bluetooth) return d;
            }
        return std::nullopt;
    };
    for (bool bluetooth : {false, true}) {
        auto i = pick(inputs(), bluetooth), o = pick(outputs(), bluetooth);
        if (i && o) return Ports{i->identifier, i->name, o->identifier, o->name};
    }
    return std::nullopt;
}

bool Fm1Link::open(const juce::String& inputId, const juce::String& outputId) {
    close();
    in_ = juce::MidiInput::openDevice(inputId, this);
    out_ = juce::MidiOutput::openDevice(outputId);
    if (!in_ || !out_) { close(); return false; }
    ports_ = {inputId, in_->getName(), outputId, out_->getName()};
    in_->start();
    return true;
}

void Fm1Link::close() {
    if (in_) in_->stop();
    in_.reset();
    std::lock_guard<std::mutex> l(sendMutex_);   // not while the session thread is sending
    out_.reset();
    ports_ = {};
}

void Fm1Link::send(const fm1::Bytes& sysex) {
    std::lock_guard<std::mutex> l(sendMutex_);
    if (!out_ || sysex.size() < 2) return;
    // MidiMessage::createSysExMessage wants the payload without F0/F7
    out_->sendMessageNow(juce::MidiMessage::createSysExMessage(sysex.data() + 1, int(sysex.size()) - 2));
}

void Fm1Link::sendRaw(const fm1::Bytes& midi) {
    std::lock_guard<std::mutex> l(sendMutex_);
    if (!out_ || midi.empty()) return;
    if (midi[0] == 0xF0) { out_->sendMessageNow(juce::MidiMessage::createSysExMessage(midi.data() + 1, int(midi.size()) - 2)); return; }
    out_->sendMessageNow(juce::MidiMessage(midi.data(), int(midi.size())));
}

void Fm1Link::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m) {
    if (!m.isSysEx()) return;
    fm1::Bytes frame(size_t(m.getRawDataSize()));
    std::memcpy(frame.data(), m.getRawData(), frame.size());
    { std::lock_guard<std::mutex> l(mutex_); pending_.push_back(frame); }
    frameArrived_.signal();
    std::lock_guard<std::mutex> l(listenerMutex_);
    if (onSysex_) onSysex_(frame);
}
