#include "BankModel.h"

BankModel::BankModel() {
    fm1::Voice init = fm1::packVoice(fm1::kInitEdit);
    for (int i = 0; i < kSlots; ++i) {
        auto& s = slots_[size_t(i)].sound;
        s.slot = i;
        s.voice = init;
        s.record = fm1::defaultRecord();
        s.hasRecord = true;
        s.from = "init";
    }
}

void BankModel::setCurrentSlot(int i) {
    current_ = juce::jlimit(0, kSlots - 1, i);
    changed();
}

void BankModel::setSound(int slot, const fm1::Sound& s, bool fromDevice) {
    if (slot < 0 || slot >= kSlots) return;
    auto& sl = slots_[size_t(slot)];
    sl.sound = s;
    sl.sound.slot = slot;
    if (!s.hasRecord) {
        sl.sound.record = fm1::defaultRecord();
        sl.sound.hasRecord = true;
    }
    if (fromDevice) sl.onDevice = sl.sound;
    libraryChanged();
}

void BankModel::markOnDevice(int slot, const fm1::Sound& s) {
    if (slot < 0 || slot >= kSlots) return;
    slots_[size_t(slot)].onDevice = s;
    libraryChanged();
}

void BankModel::clearDeviceState() {
    for (auto& s : slots_) s.onDevice.reset();
    libraryChanged();
}

int BankModel::unsyncedCount() const {
    int n = 0;
    for (const auto& s : slots_) if (!s.synced()) ++n;
    return n;
}

juce::String BankModel::bankName(int slot) {
    return juce::String::charToString(juce::juce_wchar('A' + slot / fm1::kBankSlots))
         + juce::String(slot % fm1::kBankSlots + 1).paddedLeft('0', 2);
}

juce::String BankModel::slotLabel(int i) const {
    const auto& s = slots_[size_t(i)];
    juce::String label = bankName(i) + "  " + juce::String(fm1::voiceName(s.sound.voice)).trimEnd();
    label += juce::String("  [") + fm1::engineLabel(fm1::engineOf(s.sound.record)) + "]";
    if (!s.onDevice) label += "  ?";
    else if (!s.synced()) label += "  *";
    return label;
}

juce::ValueTree BankModel::toState() const {
    juce::ValueTree v("FM1Bank");
    v.setProperty("current", current_, nullptr);
    for (int i = 0; i < kSlots; ++i) {
        const auto& s = slots_[size_t(i)];
        juce::ValueTree sl("Slot");
        sl.setProperty("i", i, nullptr);
        sl.setProperty("voice", juce::MemoryBlock(s.sound.voice.data(), s.sound.voice.size()).toBase64Encoding(), nullptr);
        sl.setProperty("record", juce::MemoryBlock(s.sound.record.data(), s.sound.record.size()).toBase64Encoding(), nullptr);
        if (s.onDevice) {
            sl.setProperty("devVoice", juce::MemoryBlock(s.onDevice->voice.data(), s.onDevice->voice.size()).toBase64Encoding(), nullptr);
            sl.setProperty("devRecord", juce::MemoryBlock(s.onDevice->record.data(), s.onDevice->record.size()).toBase64Encoding(), nullptr);
        }
        v.addChild(sl, -1, nullptr);
    }
    return v;
}

void BankModel::fromState(const juce::ValueTree& v) {
    if (!v.hasType("FM1Bank")) return;
    auto decode = [](const juce::var& prop, uint8_t* dst, size_t n) {
        juce::MemoryBlock mb;
        if (!mb.fromBase64Encoding(prop.toString()) || mb.getSize() != n) return false;
        std::memcpy(dst, mb.getData(), n);
        return true;
    };
    for (const auto& sl : v) {
        int i = sl.getProperty("i", -1);
        if (i < 0 || i >= kSlots) continue;
        auto& s = slots_[size_t(i)];
        decode(sl.getProperty("voice"), s.sound.voice.data(), s.sound.voice.size());
        decode(sl.getProperty("record"), s.sound.record.data(), s.sound.record.size());
        s.sound.hasRecord = true;
        s.sound.slot = i;
        s.sound.from = "state";
        if (sl.hasProperty("devVoice")) {
            fm1::Sound d;
            d.slot = i; d.hasRecord = true; d.from = "FM-1";
            if (decode(sl.getProperty("devVoice"), d.voice.data(), d.voice.size()) &&
                decode(sl.getProperty("devRecord"), d.record.data(), d.record.size()))
                s.onDevice = d;
        }
    }
    current_ = juce::jlimit(0, kSlots - 1, int(v.getProperty("current", 0)));
    changed();
}
