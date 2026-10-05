#include "LibraryStore.h"

#include "Fm1Json.h"
#include "Fm1Record.h"

juce::File LibraryStore::root() {
    auto dir = juce::SystemStats::getEnvironmentVariable("FM1_DATA_DIR", {});
    if (dir.isNotEmpty()) return juce::File(dir);
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Virtual FM-1");
}

juce::String LibraryStore::formatOf(const fm1::Sound& s) {
    return fm1::engineOf(s.record) == fm1::Engine::VA ? "baudgirl.va" : "fm1.voice";
}

static juce::String toHex(const uint8_t* p, size_t n) { return juce::String::toHexString(p, int(n), 0); }

static bool fromHex(const juce::var& v, uint8_t* dst, size_t n) {
    juce::MemoryBlock mb;
    mb.loadFromHexString(v.toString());
    if (mb.getSize() != n) return false;
    std::memcpy(dst, mb.getData(), n);
    return true;
}

juce::String LibraryStore::slotText(const BankModel::Slot& s, int rev) {
    fm1json::Document doc;
    doc.presets.push_back(s.sound);
    auto v = juce::JSON::parse(fm1json::write(doc));
    auto* lib = new juce::DynamicObject();
    lib->setProperty("format", formatOf(s.sound));
    lib->setProperty("rev", rev);
    if (s.onDevice) {
        auto* dev = new juce::DynamicObject();
        dev->setProperty("voice", toHex(s.onDevice->voice.data(), s.onDevice->voice.size()));
        dev->setProperty("record", toHex(s.onDevice->record.data(), s.onDevice->record.size()));
        lib->setProperty("onDevice", juce::var(dev));
    }
    if (auto* o = v.getDynamicObject()) o->setProperty("library", juce::var(lib));
    return juce::JSON::toString(v) + "\n";
}

std::optional<BankModel::Slot> LibraryStore::slotFromText(const juce::String& text, int slot, int* rev) {
    juce::StringArray errors;
    auto doc = fm1json::read(text, errors);
    if (!errors.isEmpty() || doc.presets.size() != 1) return std::nullopt;
    BankModel::Slot s;
    s.sound = doc.presets[0];
    s.sound.slot = slot;
    auto lib = juce::JSON::parse(text)["library"];
    if (rev != nullptr) *rev = int(lib.getProperty("rev", 0));
    auto dev = lib["onDevice"];
    if (dev.isObject()) {
        fm1::Sound d = s.sound;
        if (fromHex(dev["voice"], d.voice.data(), d.voice.size()) && fromHex(dev["record"], d.record.data(), d.record.size()))
            s.onDevice = d;
    }
    return s;
}

LibraryStore::Known LibraryStore::knownOf(const BankModel::Slot& s, int rev) {
    Known k;
    k.voice = s.sound.voice;
    k.record = s.sound.record;
    if (s.onDevice) k.dev = std::make_pair(s.onDevice->voice, s.onDevice->record);
    k.rev = rev;
    k.valid = true;
    return k;
}

juce::int64 LibraryStore::readGeneration() {
    auto f = root().getChildFile(".generation");
    return f.existsAsFile() ? f.loadFileAsString().trim().getLargeIntValue() : 0;
}

void LibraryStore::bumpGeneration() {
    generation_ = std::max(readGeneration(), generation_) + 1;
    writeAtomically(root().getChildFile(".generation"), juce::String(generation_));
}

void LibraryStore::writeAtomically(const juce::File& f, const juce::String& text) {
    f.getParentDirectory().createDirectory();
    juce::TemporaryFile tmp(f);
    if (tmp.getFile().replaceWithText(text, false, false, "\n")) tmp.overwriteTargetFileWithTemporary();
}

bool LibraryStore::changedElsewhere() const { return readGeneration() != generation_; }

bool LibraryStore::load(BankModel& bank) {
    if (!exists()) return false;
    generation_ = readGeneration();
    for (int i = 0; i < BankModel::kSlots; ++i) {
        auto f = slotFile(i);
        if (!f.existsAsFile()) { known_[size_t(i)] = {}; continue; }
        int rev = 0;
        if (auto s = slotFromText(f.loadFileAsString(), i, &rev)) {
            auto& dst = bank.slot(i);
            dst.sound = s->sound;
            dst.onDevice = s->onDevice;
            known_[size_t(i)] = knownOf(dst, rev);
        }
    }
    return true;
}

int LibraryStore::save(const BankModel& bank) {
    auto dir = bankDir();
    dir.createDirectory();
    auto info = dir.getChildFile("bank.json");
    if (!info.existsAsFile())
        writeAtomically(info, "{\n  \"bank\": \"FM-1\",\n  \"slots\": 128,\n  \"firmwares\": [\"fm1_stock\", \"baudgirl_fm1va\"],\n"
                              "  \"formats\": [\"fm1.voice\", \"baudgirl.va\"]\n}\n");
    int written = 0;
    for (int i = 0; i < BankModel::kSlots; ++i) {
        const auto& s = bank.slot(i);
        auto& k = known_[size_t(i)];
        const bool same = k.valid && k.voice == s.sound.voice && k.record == s.sound.record
                          && (k.dev.has_value() == s.onDevice.has_value())
                          && (!k.dev || (k.dev->first == s.onDevice->voice && k.dev->second == s.onDevice->record));
        if (same) continue;
        auto f = slotFile(i);
        int diskRev = 0;
        if (f.existsAsFile()) {
            slotFromText(f.loadFileAsString(), i, &diskRev);
            // another instance wrote this slot since we read it: keep its version
            if (diskRev != k.rev) {
                auto trash = root().getChildFile(".trash");
                trash.createDirectory();
                f.copyFileTo(trash.getChildFile(f.getFileNameWithoutExtension() + " replaced "
                                                + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H%M%S") + ".json"));
            }
        }
        const int rev = std::max(diskRev, k.rev) + 1;
        writeAtomically(f, slotText(s, rev));
        k = knownOf(s, rev);
        ++written;
    }
    if (written > 0) bumpGeneration();
    return written;
}

bool LibraryStore::migrateFrom(const juce::File& oldLibrary, BankModel& scratch) {
    if (!oldLibrary.existsAsFile()) return false;
    juce::MemoryBlock mb;
    if (!oldLibrary.loadFileAsData(mb)) return false;
    auto v = juce::ValueTree::readFromData(mb.getData(), mb.getSize());
    if (!v.isValid() || !v.hasType("FM1Bank")) return false;
    scratch.fromState(v);
    save(scratch);
    writeAtomically(root().getChildFile("migrated-from.txt"),
                    "The FM-1 bank in Banks/FM-1 was copied from\n" + oldLibrary.getFullPathName() + "\non "
                    + juce::Time::getCurrentTime().toISO8601(true) + ". That file was left as it was.\n");
    return true;
}
