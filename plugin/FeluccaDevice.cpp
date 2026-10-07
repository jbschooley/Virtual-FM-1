#if FM1_FELUCCA

#include "FeluccaDevice.h"

#include "FeluccaEngine.h"
#include "LibraryStore.h"

namespace felucca {

const Dialect& dialectOf(const FeluccaEngine& f) {
    return f.flavor() == FeluccaEngine::Flavor::Sloop ? sloopDialect() : feluccaDialect();
}

VirtualEndpoint::VirtualEndpoint(std::shared_ptr<FeluccaEngine> f) : f_(std::move(f)), dialect_(&dialectOf(*f_)) {}

std::optional<Bytes> VirtualEndpoint::ask(const Bytes& request, int) {
    return f_->ask(request);   // (its pushes come with pushes(): the library's requests leave them too)
}

Objects objectsOf(FeluccaEngine& f) {
    Objects out;
    for (int id = 0; id <= dialectOf(f).lastObject; ++id) {
        std::vector<uint8_t> b;
        if (f.object(id, b)) out[id] = std::move(b);
    }
    return out;
}

bool putObjects(FeluccaEngine& f, const Objects& objects, juce::String& error) {
    // SLOOP refuses a project while it plays (Felucca stops itself): its transport stops first
    // (sloop_core.c's object_put does a stop asked for before it)
    if (f.flavor() == FeluccaEngine::Flavor::Sloop && f.playing()) f.transport(false);
    for (int id : dialectOf(f).restoreOrder) {   // as restore() sends them
        auto it = objects.find(id);
        if (it == objects.end()) continue;
        if (const int rc = f.putObject(id, it->second); rc != 0) {
            error = "object " + juce::String(id) + " refused (rc " + juce::String(rc) + ")";
            return false;
        }
    }
    return true;
}

std::vector<Bytes> VirtualEndpoint::pushes() {
    for (auto& m : f_->takeSysex())   // what its main loop pushed meanwhile
        if (isPush(commandOf(m))) pushes_.push_back(m);
    std::vector<Bytes> out;
    out.swap(pushes_);
    return out;
}

// What the file holds, to see another instance's save: its contents, not its time (a time
// can be the same for two saves: whole seconds on Linux). About 40 KB once a second.
juce::int64 DeviceStore::fileHash() const {
    return file_.existsAsFile() ? file_.loadFileAsString().hashCode64() : 0;
}

juce::File DeviceStore::defaultFile(const Dialect& d) {
    return LibraryStore::root().getChildFile(d.name).getChildFile(juce::String(d.name) + " device.json");
}

// The stored objects into the engine, as the web editor restores them: the project slots,
// user presets (and their FM6 patches), and the settings (not the music: the instance's own).
juce::String DeviceStore::loadInto(FeluccaEngine& f) {
    Objects o;
    juce::String error;
    if (!readBackup(file_, o, error, dialect_)) return file_.getFileName() + ": " + error + "; the device keeps what it has.";
    for (int id : dialect_.restoreOrder) {
        auto it = o.find(id);
        if (id == 0 || it == o.end()) continue;
        if (f.putObject(id, it->second) != 0) return file_.getFileName() + ": " + dialect_.name + " refused object " + juce::String(id) + ".";
    }
    known_.clear();
    std::vector<uint8_t> b;
    for (int id = 1; id <= dialect_.lastObject; ++id) if (f.object(id, b)) known_[id] = b;
    return {};
}

juce::String DeviceStore::load(FeluccaEngine& f) {
    seen_ = fileHash();
    blocked_ = false;
    lastError_ = {};
    if (file_.existsAsFile()) {
        lastError_ = loadInto(f);
        // a file it cannot read or Felucca refuses (damaged, from a newer editor) is never
        // written over: saving waits until the file changes and reads
        blocked_ = lastError_.isNotEmpty();
        if (!blocked_) return {};
        return lastError_ + " Nothing is saved to it until it can be read.";
    }
    known_.clear();   // nothing stored yet: what the device has now counts as known
    std::vector<uint8_t> b;
    for (int id = 1; id <= dialect_.lastObject; ++id) if (f.object(id, b)) known_[id] = b;
    return {};
}

juce::String DeviceStore::tick(FeluccaEngine& f) {
    auto report = [this](const juce::String& e) { if (e == lastError_) return juce::String(); lastError_ = e; return e; };
    const auto hash = fileHash();
    const bool changedThere = file_.existsAsFile() && hash != seen_;
    // SLOOP 2.4 refuses its user presets and FM6 bank (6, 7, 8) while it plays: another instance's
    // save waits until it stops (and so does saving its own changes, which would write over it)
    if (changedThere && f.flavor() == FeluccaEngine::Flavor::Sloop && f.playing()) return {};
    if (blocked_) {   // only a changed file that reads lets it save again
        if (!changedThere) return {};
        seen_ = hash;
        auto e = loadInto(f);
        blocked_ = e.isNotEmpty();
        return blocked_ ? report(e) : juce::String();
    }
    Objects now;
    std::vector<uint8_t> b;
    for (int id = 1; id <= dialect_.lastObject; ++id) if (f.object(id, b)) now[id] = b;
    // another instance saved: take each object this one has not changed itself
    Objects base = known_;
    if (changedThere) {
        seen_ = hash;
        Objects theirs;
        juce::String error;
        if (!readBackup(file_, theirs, error, dialect_)) {
            blocked_ = true;
            return report(file_.getFileName() + ": " + error + "; the device keeps what it has, and nothing is saved to it until it can be read.");
        }
        for (int id : dialect_.restoreOrder) {
            auto it = theirs.find(id);   // (an archive from before FM6 has no 8: the bank stays)
            if (id == 0 || it == theirs.end() || now[id] != known_[id] || it->second == now[id]) continue;
            if (f.putObject(id, it->second) != 0) return report(file_.getFileName() + ": " + dialect_.name + " refused object " + juce::String(id) + ".");
            if (f.object(id, b)) now[id] = b;
        }
        for (int id = 1; id <= dialect_.lastObject; ++id) {
            if (now[id] == known_[id]) base[id] = now[id];
            else if (auto it = theirs.find(id); it != theirs.end()) base[id] = it->second;
        }
    }
    known_ = base;
    if (now == known_) return {};
    // this instance changed something: save the device (the music too, as a backup must have it)
    if (!f.object(0, b)) return {};
    Objects all = now;
    all[0] = b;
    file_.getParentDirectory().createDirectory();
    juce::TemporaryFile tmp(file_);
    if (!tmp.getFile().replaceWithText(backupJson(all, juce::String(f.version()) + " (Virtual FM-1)", dialect_), false, false, "\n")
        || !tmp.overwriteTargetFileWithTemporary())
        return report("Could not save the " + juce::String(dialect_.name) + " device to " + file_.getFullPathName() + ".");
    known_ = now;
    seen_ = fileHash();
    lastError_ = {};
    return {};
}

}  // namespace felucca

#endif
