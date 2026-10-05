#if FM1_FELUCCA

#include "FeluccaDevice.h"

#include "FeluccaEngine.h"
#include "LibraryStore.h"

namespace felucca {

const std::vector<int>& backupIds() {
    static const std::vector<int> ids = {0, 1, 2, 3, 4, 5, 6, 7, 8, 32, 33, 34};
    return ids;
}

static uint32_t crc32(const std::vector<uint8_t>& b) {   // zlib's, as fm1backup.js bkCrc
    uint32_t c = 0xFFFFFFFFu;
    for (uint8_t v : b) {
        c ^= v;
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ ((c & 1u) ? 0xEDB88320u : 0u);
    }
    return ~c;
}

bool readBackup(const juce::File& f, Objects& out, juce::String& error) {
    out.clear();
    auto v = juce::JSON::parse(f.loadFileAsString());
    auto* objs = v.getProperty("objects", {}).getArray();
    const auto& ids = backupIds();
    // (an archive of firmware before FM6 has no object 8: 11 objects)
    if (v.getProperty("format", {}).toString() != "felucca-backup" || int(v.getProperty("version", 0)) != 1 || !objs
        || (objs->size() != int(ids.size()) && objs->size() != int(ids.size()) - 1)) {
        error = "not a complete Felucca backup";
        return false;
    }
    for (const auto& o : *objs) {
        const int id = o.getProperty("id", -1), size = o.getProperty("size", -1);
        const auto crc = uint32_t(juce::int64(o.getProperty("crc", -1)));
        juce::MemoryOutputStream data;
        if (std::find(ids.begin(), ids.end(), id) == ids.end() || size < 0 || size > (id >= 32 ? 81920 : 3840)
            || !juce::Base64::convertFromBase64(data, o.getProperty("data", {}).toString()) || int(data.getDataSize()) != size) {
            error = "object " + juce::String(id) + " is damaged";
            return false;
        }
        const auto* b = static_cast<const uint8_t*>(data.getData());
        std::vector<uint8_t> bytes(b, b + data.getDataSize());
        if (crc32(bytes) != crc) {
            error = "object " + juce::String(id) + " fails its checksum";
            return false;
        }
        out[id] = std::move(bytes);
    }
    if (out[0].empty() || out[1].empty()) {
        error = "the backup has no music or settings";
        return false;
    }
    return true;
}

juce::String backupJson(const Objects& objects, const juce::String& firmware) {
    juce::Array<juce::var> list;
    for (int id : backupIds()) {
        auto it = objects.find(id);
        const std::vector<uint8_t> none;
        const auto& b = it != objects.end() ? it->second : none;
        auto* o = new juce::DynamicObject();
        o->setProperty("id", id);
        o->setProperty("size", int(b.size()));
        o->setProperty("crc", b.empty() ? juce::int64(0) : juce::int64(crc32(b)));
        o->setProperty("data", juce::Base64::toBase64(b.data(), b.size()));
        list.add(juce::var(o));
    }
    auto* root = new juce::DynamicObject();
    root->setProperty("format", "felucca-backup");
    root->setProperty("version", 1);
    root->setProperty("firmware", firmware);
    root->setProperty("created", juce::Time::getCurrentTime().toISO8601(true));
    root->setProperty("objects", list);
    return juce::JSON::toString(juce::var(root), false);
}

std::optional<Bytes> VirtualEndpoint::ask(const Bytes& request, int) {
    const int cmd = commandOf(request);
    std::optional<Bytes> reply;
    for (auto& m : f_->request(request)) {
        if (isPush(commandOf(m))) pushes_.push_back(m);
        else if (commandOf(m) == cmd && !reply) reply = m;
    }
    return reply;
}

std::vector<Bytes> VirtualEndpoint::pushes() {
    for (auto& m : f_->takeSysex())   // what its main loop pushed meanwhile
        if (isPush(commandOf(m))) pushes_.push_back(m);
    std::vector<Bytes> out;
    out.swap(pushes_);
    return out;
}

juce::File DeviceStore::defaultFile() {
    return LibraryStore::root().getChildFile("Felucca").getChildFile("Felucca device.json");
}

// The stored objects into the engine, as the web editor restores them: the project slots,
// user presets and FM6 bank, then the settings (not the music: the instance's own).
juce::String DeviceStore::loadInto(FeluccaEngine& f) {
    Objects o;
    juce::String error;
    if (!readBackup(file_, o, error)) return file_.getFileName() + ": " + error + "; the device keeps what it has.";
    for (int id : {2, 3, 4, 5, 6, 7, 8, 1}) {
        auto it = o.find(id);
        if (it == o.end()) continue;
        if (f.putObject(id, it->second) != 0) return file_.getFileName() + ": Felucca refused object " + juce::String(id) + ".";
    }
    known_.clear();
    std::vector<uint8_t> b;
    for (int id = 1; id <= 8; ++id) if (f.object(id, b)) known_[id] = b;
    return {};
}

void DeviceStore::load(FeluccaEngine& f) {
    seen_ = file_.getLastModificationTime();
    if (file_.existsAsFile()) lastError_ = loadInto(f);
    else {   // nothing stored yet: what the device has now counts as known
        known_.clear();
        std::vector<uint8_t> b;
        for (int id = 1; id <= 8; ++id) if (f.object(id, b)) known_[id] = b;
    }
}

juce::String DeviceStore::tick(FeluccaEngine& f) {
    auto report = [this](const juce::String& e) { if (e == lastError_) return juce::String(); lastError_ = e; return e; };
    Objects now;
    std::vector<uint8_t> b;
    for (int id = 1; id <= 8; ++id) if (f.object(id, b)) now[id] = b;
    // another instance saved: take each object this one has not changed itself
    Objects base = known_;
    if (file_.existsAsFile() && file_.getLastModificationTime() != seen_) {
        seen_ = file_.getLastModificationTime();
        Objects theirs;
        juce::String error;
        if (!readBackup(file_, theirs, error)) return report(file_.getFileName() + ": " + error + "; the device keeps what it has.");
        for (int id : {2, 3, 4, 5, 6, 7, 8, 1}) {
            if (now[id] != known_[id] || theirs[id] == now[id]) continue;
            if (f.putObject(id, theirs[id]) != 0) return report(file_.getFileName() + ": Felucca refused object " + juce::String(id) + ".");
            if (f.object(id, b)) now[id] = b;
        }
        for (int id = 1; id <= 8; ++id) if (now[id] == known_[id]) base[id] = now[id];
        for (int id = 1; id <= 8; ++id) if (theirs.count(id) && now[id] != known_[id]) base[id] = theirs[id];
    }
    known_ = base;
    if (now == known_) return {};
    // this instance changed something: save the device (the music too, as a backup must have it)
    if (!f.object(0, b)) return {};
    Objects all = now;
    all[0] = b;
    file_.getParentDirectory().createDirectory();
    juce::TemporaryFile tmp(file_);
    if (!tmp.getFile().replaceWithText(backupJson(all, "FELUCCA v1.0 (Virtual FM-1)"), false, false, "\n")
        || !tmp.overwriteTargetFileWithTemporary())
        return report("Could not save the Felucca device to " + file_.getFullPathName() + ".");
    known_ = now;
    seen_ = file_.getLastModificationTime();
    lastError_ = {};
    return {};
}

}  // namespace felucca

#endif
