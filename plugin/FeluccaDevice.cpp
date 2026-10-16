#if FM1_FELUCCA

#include "FeluccaDevice.h"

#include "FeluccaEngine.h"
#include "Firmwares.h"
#include "FeluccaSeq.h"
#include "LibraryStore.h"

namespace felucca {

const Dialect& dialectOf(const FeluccaEngine& f) {
    const char* family = f.flavor() == FeluccaEngine::Flavor::Sloop ? "sloop" : f.flavor() == FeluccaEngine::Flavor::Melodee ? "melodee"
                       : f.flavor() == FeluccaEngine::Flavor::ChoralRoot ? "choralroot" : "felucca";
    return dialectFor(family, int(f.release()));
}

VirtualEndpoint::VirtualEndpoint(std::shared_ptr<FeluccaEngine> f) : f_(std::move(f)), dialect_(&dialectOf(*f_)) {}

std::optional<Bytes> VirtualEndpoint::ask(const Bytes& request, int) {
    return f_->ask(request);   // (its pushes come with pushes(): the library's requests leave them too)
}

Objects objectsOf(FeluccaEngine& f) {
    Objects out;
    const auto& d = dialectOf(f);
    std::vector<uint8_t> b;
    if (d.music && f.object(0, b)) out[0] = std::move(b);
    for (int id : storedIds(d))
        if (f.object(id, b)) out[id] = std::move(b);
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
            f.objectsDone();
            return false;
        }
    }
    f.objectsDone();
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
    return LibraryStore::root().getChildFile(d.name).getChildFile(juce::String(d.device ? d.device : d.name) + " device.json");
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
        // (ChoralRoot: an object never saved is empty, and it refuses an empty settings record: one it has
        // not got either is left as it is)
        std::vector<uint8_t> mine;
        if (isChoralRoot(dialect_) && it->second.empty() && f.object(id, mine) && mine.empty()) continue;
        if (f.putObject(id, it->second) != 0) { f.objectsDone(); return file_.getFileName() + ": " + dialect_.name + " refused object " + juce::String(id) + "."; }
    }
    f.objectsDone();
    known_.clear();
    std::vector<uint8_t> b;
    for (int id : storedIds(dialect_)) if (f.object(id, b)) known_[id] = b;
    return {};
}

// A kept release's device file (Melodee 0.13's, Felucca 1.0.3's) starts as the one that release wrote
// before the current release was built in, if the library has it ("Melodee device.json" made by 0.13):
// copied before either reads, so the current release cannot have written over it yet. Which release wrote
// the file: its "firmware" ("MELODEE v0.13 (Virtual FM-1)", "v1.0.3 (Virtual FM-1)") without the name,
// matched against the versions each kept release takes (fm1::KeptRelease::savedBy)
// Done whichever of the firmware's stores loads first (the current release's included: it would write
// over the file).
static void seedKept(const Dialect& d, const juce::File& mine) {
    if (isSloop(d) || isChoralRoot(d) || mine != DeviceStore::defaultFile(d)) return;   // (a file given by path: the caller's)
    const std::string family = isMelodee(d) ? "melodee" : "felucca";
    const auto currentFile = DeviceStore::defaultFile(dialectFor(family, 0));
    if (!currentFile.existsAsFile()) return;
    juce::String made = juce::JSON::parse(currentFile).getProperty("firmware", {}).toString();
    if (made.startsWith("MELODEE ")) made = made.substring(8);
    const auto* k = fm1::keptReleaseFor(family, made.toStdString());
    if (!k) return;
    const auto keptFile = DeviceStore::defaultFile(dialectFor(family, k->release));
    if (keptFile.existsAsFile() || keptFile == currentFile) return;
    keptFile.getParentDirectory().createDirectory();
    currentFile.copyFileTo(keptFile);
}

juce::String DeviceStore::load(FeluccaEngine& f) {
    seedKept(dialect_, file_);
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
    for (int id : storedIds(dialect_)) if (f.object(id, b)) known_[id] = b;
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
    for (int id : storedIds(dialect_)) if (f.object(id, b)) now[id] = b;
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
            if (f.putObject(id, it->second) != 0) { f.objectsDone(); return report(file_.getFileName() + ": " + dialect_.name + " refused object " + juce::String(id) + "."); }
            if (f.object(id, b)) now[id] = b;
        }
        f.objectsDone();
        for (int id : storedIds(dialect_)) {
            if (now[id] == known_[id]) base[id] = now[id];
            else if (auto it = theirs.find(id); it != theirs.end()) base[id] = it->second;
        }
    }
    known_ = base;
    if (now == known_) return {};
    // this instance changed something: save the device (the music too, as a backup must have it;
    // ChoralRoot has none)
    Objects all = now;
    if (dialect_.music) {
        if (!f.object(0, b)) return {};
        all[0] = b;
    }
    file_.getParentDirectory().createDirectory();
    juce::TemporaryFile tmp(file_);
    // (Melodee's files name it as its INFO does, "MELODEE v0.13": its editor and readBackup tell them by that;
    // ChoralRoot's as its INFO, "ChoralRoot 0.23")
    const juce::String version(f.version());
    const juce::String made = isChoralRoot(dialect_) ? "ChoralRoot " + version.trimCharactersAtStart("v") + " (Virtual FM-1)"
                            : (isMelodee(dialect_) ? "MELODEE " : "") + version + " (Virtual FM-1)";
    if (!tmp.getFile().replaceWithText(backupJson(all, made, dialect_), false, false, "\n")
        || !tmp.overwriteTargetFileWithTemporary())
        return report("Could not save the " + juce::String(dialect_.name) + " device to " + file_.getFullPathName() + ".");
    known_ = now;
    seen_ = fileHash();
    lastError_ = {};
    return {};
}

}  // namespace felucca

#endif
