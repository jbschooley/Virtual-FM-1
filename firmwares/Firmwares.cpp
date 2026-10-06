#include "Firmwares.h"

#include <cctype>

namespace fm1 {

const std::vector<FirmwareChoice>& firmwareChoices() {
    static const std::vector<FirmwareChoice> choices = {
        {"fm1_stock", "M-VAVE (stock)", false, true},
        {"baudgirl_fm1va", "FM-1+VA (baud girl)", true, true},
        {"felucca", "Felucca", false, false},
    };
    return choices;
}

const FirmwareChoice& firmwareChoice(const std::string& id) {
    for (const auto& c : firmwareChoices()) if (id == c.id) return c;
    return firmwareChoice(kDefaultFirmwareId);
}

std::string firmwareIdFor(const Identity& id) {
    if (id.isStock()) return "fm1_stock";
    // Sloop (isod89/sloop-fm1, from a Felucca before 1.0) answers as a Felucca development build
    // does, FM-1_900: its editor's INFO says SLOOP
    if (id.version >= 900 && id.editor.find("SLOOP") != std::string::npos) return "sloop";
    if (id.version >= 900) return "felucca";   // a Felucca release X.Y reports FM-1_9XY (build.py --release; 0.4 beta: FM-1_904), others FM-1_900
    return "baudgirl_fm1va";
}

std::string FirmwareChoice::label() const { return std::string(name) + " " + currentVersion(id).label; }

const std::vector<KnownVersion>& knownVersions(const std::string& firmwareId) {
    // M-VAVE's: V15 is its last (its FM-1.fwsc, the SHA-256 Felucca's tools/fm1_install.py
    // knows as V15), and what baud girl's and Felucca's installers start from
    static const std::vector<KnownVersion> stock = {
        {15, "V15", Support::Current, ""},
    };
    // baud girl's releases (baudgirl.com/work/FM-1+VA/install), their identity FM-1_0NN
    static const std::vector<KnownVersion> fmva = {
        {83, "0.83", Support::Older, "no per-note filter on FM presets, no Chain per pattern, GLOBE settings not read; not tried"},
        {84, "0.84", Support::Older, "no per-note filter on FM presets, no Chain per pattern, GLOBE settings not read; not tried"},
        {85, "0.85", Support::Older, "no per-note filter on FM presets, no Chain per pattern, GLOBE settings not read; not tried"},
        {86, "0.86", Support::Older, "no per-note filter on FM presets, no Chain per pattern, GLOBE settings not read; not tried"},
        {89, "0.89", Support::Older, "no per-note filter on FM presets, no Chain per pattern, GLOBE settings not read; not tried"},
        {92, "0.92", Support::Older, "no Chain per pattern, GLOBE settings not read; not tried"},
        {93, "0.93", Support::Current, ""},   // her current release (baudgirl.com, as of 2026-10-05)
        {94, "0.94", Support::Tested, "baud girl's beta: 0.93 with M-VAVE's updater fixed"},
    };
    // Felucca's releases X.Y answer FM-1_9XY (its build.py --release); other builds FM-1_900
    static const std::vector<KnownVersion> felucca = {
        {904, "0.4 beta", Support::Deprecated, "the plugin supports Felucca from 1.0: update it with Felucca's installer"},
        {910, "1.0.2", Support::Current, ""},   // (1.0 and 1.0.1 answer as FM-1_910 too: told by INFO, below)
    };
    if (firmwareId == "fm1_stock") return stock;
    if (firmwareId == "felucca") return felucca;
    return fmva;
}

const KnownVersion& currentVersion(const std::string& firmwareId) {
    for (const auto& v : knownVersions(firmwareId)) if (v.support == Support::Current) return v;
    return knownVersions(firmwareId).back();
}

VersionCheck checkVersion(const Identity& id) {
    VersionCheck c;
    c.firmwareId = firmwareIdFor(id);
    if (c.firmwareId == "sloop") {   // known by name only
        c.support = Support::Deprecated;
        const std::string prefix = "FELUCCA ";   // (Sloop's INFO: "FELUCCA " FELUCCA_VERSION, "SLOOP 2.2")
        const std::string named = id.editor.rfind(prefix, 0) == 0 ? id.editor.substr(prefix.size()) : id.editor;
        c.text = id.name() + ": " + named + ", which the plugin does not support yet";
        return c;
    }
    const auto& list = knownVersions(c.firmwareId);
    const std::string who = id.name();
    for (const auto& v : list)
        if (v.identity == id.version) c.known = &v;
    if (c.known && c.firmwareId == "felucca" && id.editor.rfind("FELUCCA v", 0) == 0) {
        // Felucca X.Y.Z answers as X.Y (FM-1_9XY): its INFO's version tells a newer bug-fix release
        const std::string release = id.editor.substr(9);
        auto parts = [](const std::string& v) {
            std::vector<int> n;
            for (size_t i = 0; i < v.size();) {
                if (!std::isdigit(static_cast<unsigned char>(v[i]))) break;
                int x = 0;
                while (i < v.size() && std::isdigit(static_cast<unsigned char>(v[i]))) x = x * 10 + (v[i++] - '0');
                n.push_back(x);
                if (i < v.size() && v[i] == '.') ++i; else break;
            }
            return n;
        };
        if (parts(release) > parts(c.known->label)) {
            c.newer = true;
            c.support = c.known->support;
            c.text = who + ": Felucca " + release + ", newer than the plugin's " + c.known->label + "; synced as "
                   + c.known->label + " (not tried yet)";
            return c;
        }
        if (parts(release) < parts(c.known->label)) {   // an earlier bug-fix release of the same X.Y
            c.support = release == "1.0" ? Support::Tested : Support::Older;
            c.text = who + ": Felucca " + release + ", older than the plugin's " + c.known->label + "; synced as "
                   + c.known->label + (release == "1.0" ? "" : " (not tried)")
                   + ". Update it with Felucca's installer for the same sounds on both";
            return c;
        }
    }
    if (c.known) {
        c.support = c.known->support;
        c.text = who;
        if (*c.known->note) c.text += ": " + std::string(c.known->note);
        return c;
    }
    const auto& newest = list.back();
    if (c.firmwareId == "felucca" && id.version > 900 && id.version < 910) {   // the betas before 1.0 (0.8, 0.9 ...)
        c.support = Support::Deprecated;
        c.text = who + ": a Felucca from before 1.0; the plugin supports Felucca from 1.0: update it with Felucca's installer";
        return c;
    }
    if (c.firmwareId == "felucca" && id.version == 900) {   // a build of Felucca that is not a release
        c.support = Support::Older;
        c.text = who + ": a development build of Felucca, synced as " + newest.label + " (not tried)";
        return c;
    }
    if (id.version > newest.identity) {
        c.newer = true;
        c.support = newest.support;
        c.text = who + ": newer than the plugin knows; synced as " + newest.label + " (not tried yet)";
        // what the newest's support has that a newer build cannot be assumed to have
        if (c.firmwareId == "baudgirl_fm1va") c.text += ", GLOBE settings not read";   // (RAM addresses known per build)
        return c;
    }
    c.support = Support::Older;
    c.text = who + (id.version < list.front().identity ? ": older than the releases the plugin knows"
                                                        : ": not a release the plugin knows")
           + "; some sync may not work (update it with its author's installer)";
    return c;
}

namespace {
// a firmware the plugin knows by name and does nothing with (not a choice: no instance plays it)
class UnsupportedFirmware : public Firmware {
public:
    explicit UnsupportedFirmware(juce::String n) : name_(std::move(n)) {}
    juce::String name() const override { return name_; }
    juce::String summary() const override { return name_ + ": not supported yet"; }
    bool has(Feature) const override { return false; }
    juce::String cannot(Feature) const override { return "This FM-1 runs " + name_ + ", which the plugin does not support yet."; }
    std::vector<Bytes> editMessages(const Sound&, edit::Channels) const override { return {}; }
    std::vector<Bytes> editChanges(const Sound&, const Sound&, edit::Channels) const override { return {}; }
private:
    juce::String name_;
};
}  // namespace

std::unique_ptr<Firmware> makeUnsupportedFirmware(const juce::String& name) { return std::make_unique<UnsupportedFirmware>(name); }

bool isFirmwareChoice(const std::string& id) {
    for (const auto& c : firmwareChoices()) if (id == c.id) return true;
    return false;
}

std::unique_ptr<Firmware> firmwareFor(const Identity& id) {
    const auto which = firmwareIdFor(id);
    std::unique_ptr<Firmware> f;
    if (which == "fm1_stock") f = makeStockFirmware();
    else if (which == "felucca") f = makeFeluccaFirmware();
    else if (which == "sloop") f = makeUnsupportedFirmware("Sloop");
    else f = makeFmVaFirmware();
    f->version = id.version;
    return f;
}

}  // namespace fm1
