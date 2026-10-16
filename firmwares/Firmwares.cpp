#include "Firmwares.h"

#include <cctype>

namespace fm1 {

const std::vector<FirmwareChoice>& firmwareChoices() {
    static const std::vector<FirmwareChoice> choices = {
        {"fm1_stock", "M-VAVE (stock)", false, true},
        {"baudgirl_fm1va", "FM-1+VA (baud girl)", true, true},
        {"felucca", "Felucca", false, false},
        {"sloop", "SLOOP", false, false},
        {"melodee", "Melodee", false, false},
        {"melodee_013", "Melodee", false, false},   // 0.13, kept for the projects made with it (engines/melodee013)
        {"felucca_103", "Felucca", false, false},   // 1.0.3, kept the same way (engines/felucca103)
        {"melodee_110", "Melodee", false, false},   // 1.1.0, kept for the projects made with it (engines/melodee110)
        {"fm1open", "FM1 Open", false, false},      // M-VAVE's own (engines/fm1open): plays; no sync with a device yet
        {"choralroot", "ChoralRoot", false, false},   // Quixotic7's (engines/choralroot): plays; its backup syncs
    };
    return choices;
}

const FirmwareChoice& firmwareChoice(const std::string& id) {
    for (const auto& c : firmwareChoices()) if (id == c.id) return c;
    return firmwareChoice(kDefaultFirmwareId);
}

// Firmwares made for the FM-1 (most from Felucca) that the plugin knows by name and does not play or sync
// (their repositories' releases, 2026-10-16). Many answer an identity of Felucca's scheme, or one another's,
// so their INFO (Felucca's editor protocol, where they speak it) decides first; the ones with no editor
// protocol go by their identity's own scheme. Empty: none of them.
std::string otherFirmwareName(const Identity& id) {
    const std::string& info = id.editor;
    static const std::pair<const char*, const char*> byInfo[] = {
        {"CoralFlower ", "CoralFlower"}, {"PurpleMonkey ", "PurpleMonkey"}, {"X0X ", "X0X"},
        {"JANGADA ", "Jangada"}, {"FELUCCA ORBIT", "ORBIT"}, {"FELUCCA DUELL", "DUELL"}, {"FELUCCA OPTIMIST", "Optimist"}};
    for (auto [prefix, name] : byInfo)
        if (info.rfind(prefix, 0) == 0) return name;
    // another "FELUCCA <NAME> ..." than Felucca's own "FELUCCA vX.Y" (SLOOP and Melodee are told apart before)
    if (info.rfind("FELUCCA ", 0) == 0 && info.rfind("FELUCCA v", 0) != 0 && info.find("SLOOP") == std::string::npos)
        return "a firmware made from Felucca (\"" + info + "\")";
    if (!info.empty()) return {};
    // no editor protocol: identities of seven digits, the first naming the family (X.Y.Z as XXYYZZ)
    if (id.version >= 1000000 && id.version < 10000000) {
        switch (id.version / 1000000) {
            case 5: return "MONK-1";
            case 6: return "CTL-1";
            case 7: return "AMB-1 or FuMi-1";
            case 8: return "FoMni or FiMba-1";
            case 9: return "TRIBE (or an X0X build)";
            default: return "a firmware the plugin does not know";
        }
    }
    if (id.version >= 990 && id.version <= 999) return "Dinghy";
    if (id.version == 900) return "Hortator (or another Felucca build with no editor protocol)";   // (Felucca's own answer INFO)
    if (id.version == 906 || id.version == 907) return "FM-106 (or a Felucca 0.6 or 0.7 beta)";
    if (id.version >= 200 && id.version < 900) return "a firmware the plugin does not know";
    return {};
}

std::string firmwareIdFor(const Identity& id) {
    // M-VAVE's FM1 Open (m-vave/fm1_open): its releases answer FM-1_8NNNNNN (V12: FM-1_8000012,
    // app/version.h), its other builds FM1_OFFICIAL_OPEN or FM1_COMMUNITY_<n> (sdk/WebUpdate.js openIdentity)
    // (its releases Vnn: FM-1_80000nn. FoMni and FiMba-1 use the same eight-family scheme with their own
    // X.Y.Z, FM-1_8XXYYZZ from 0.1: otherFirmwareName names them)
    if (id.model == "FM-1" && id.product.empty() && id.version >= 8000000 && id.version < 8000100) return "fm1open";
    if (id.product == "FM1_OFFICIAL_OPEN" || id.product.rfind("FM1_COMMUNITY_", 0) == 0) return "fm1open";
    if (id.isStock()) return "fm1_stock";
    // ChoralRoot (Quixotic7/ChoralRootFM1, from Felucca) answers FM-1_920 in every release, as a Felucca 2.0
    // would: its INFO names it ("ChoralRoot 0.23"), before Felucca's rules and the firmwares known by name
    if (id.editor.rfind("ChoralRoot ", 0) == 0) return "choralroot";
    if (!otherFirmwareName(id).empty()) return "other";
    // SLOOP (isod89/sloop-fm1, from a Felucca before 1.0) answers as a Felucca development build
    // does, FM-1_900: its editor's INFO says SLOOP ("FELUCCA SLOOP 2.3")
    // Melodee (keremimo/melodee, from Felucca 1.0) answers FM-1_900 (development), FM-1_9010 .. FM-1_9012
    // (0.10 .. 0.12) and FM-1_910 from 1.0, as Felucca 1.0 does, and speaks Felucca's editor protocol
    // with other objects and parameters: told apart by its INFO ("MELODEE v0.12"), never synced as Felucca
    // Its Synth only build (an install option beside it: one track, no sequencer or projects, its own
    // backup objects) answers the same, with no steps in its INFO: known by name, never synced
    if (id.version >= 900 && id.editor.find("MELODEE") != std::string::npos) return id.noSteps ? "melodee_synth" : "melodee";
    if (id.version >= 900 && id.editor.find("SLOOP") != std::string::npos) return "sloop";
    if (id.version >= 900) return "felucca";   // a Felucca release X.Y reports FM-1_9XY (build.py --release; 0.4 beta: FM-1_904), others FM-1_900
    return "baudgirl_fm1va";
}

const std::vector<KeptRelease>& keptReleases() {
    static const std::vector<KeptRelease> kept = {
        // Melodee 1.0 retired 0.13's LOFI, PHASE, VOICE and NOISE and its project objects (engines/melodee013)
        {"melodee_013", "melodee", 1, "0.13", 9013, "Melodee013", {"v0."}},
        // Felucca 1.0.5 retired the HAND, CYM and H+CYM kits, 1.1 its formats (engines/felucca103)
        {"felucca_103", "felucca", 1, "1.0.3", 910, "Felucca103", {"v1.0"}},
        // Melodee 1.2 removed 1.1's effects and retired ANALOG and DIGITAL; 1.3's effects are new ones
        // (engines/melodee110). Projects saved by 1.0.x or 1.1.x
        {"melodee_110", "melodee", 2, "1.1.0", 91010, "Melodee110", {"v1.0", "v1.1"}},
    };
    return kept;
}

const KeptRelease* keptRelease(const std::string& choiceId) {
    for (const auto& k : keptReleases()) if (choiceId == k.choiceId) return &k;
    return nullptr;
}

const KeptRelease* keptRelease(const std::string& family, int release) {
    for (const auto& k : keptReleases()) if (family == k.family && release == k.release) return &k;
    return nullptr;
}

const KeptRelease* keptReleaseFor(const std::string& family, const std::string& savedVersion) {
    for (const auto& k : keptReleases())
        if (family == k.family)
            for (const char* p : k.savedBy) if (savedVersion.rfind(p, 0) == 0) return &k;
    return nullptr;
}

std::string familyOf(const std::string& choiceId) {
    const auto* k = keptRelease(choiceId);
    return k ? k->family : choiceId;
}

// a Melodee release from its INFO ("MELODEE v0.13.1" -> "0.13.1")
static std::string melodeeRelease(const Identity& id) {
    const auto at = id.editor.find("MELODEE v");
    return at == std::string::npos ? std::string() : id.editor.substr(at + 9);
}
// a Felucca release from its INFO ("FELUCCA v1.0.3" -> "1.0.3"; SLOOP's "FELUCCA SLOOP 2.6" is none)
static std::string feluccaRelease(const Identity& id) {
    return id.editor.rfind("FELUCCA v", 0) == 0 ? id.editor.substr(9) : std::string();
}

bool runs(const Identity& synth, const std::string& choiceId) {
    const std::string family = familyOf(choiceId);
    if (firmwareIdFor(synth) != family) return false;
    if (!isPreviousRelease(choiceId)) return true;
    // (the release kept: also its fix-only releases, the same formats: 0.13.1, 1.0.3.1)
    const std::string kept = currentVersion(choiceId).label, release = family == "felucca" ? feluccaRelease(synth) : melodeeRelease(synth);
    return release == kept || release.rfind(kept + ".", 0) == 0;
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
        {93, "0.93", Support::Tested, "no 8-Bit presets or parameter locks (FM-1_096)"},
        {94, "0.94", Support::Tested, "baud girl's beta: 0.93 with M-VAVE's updater fixed; no 8-Bit presets or parameter locks (FM-1_096)"},
        {96, "0.96", Support::Tested, ""},   // (her release of 2026-10-06; tried on an FM-1 the same day)
        // her current release (baudgirl.com, 2026-10-09): MIDI clock out, P2P Bluetooth clock, the Flow grid
        // as a view; its web app's protocol is 096's (nothing in it needs more than 96). Tried on an FM-1
        // 2026-10-12 (presets and patterns both ways)
        {97, "0.97", Support::Tested, ""},
        {98, "0.98", Support::Older, "baud girl's beta: synced as 0.97, not tried on an FM-1"},
        {99, "0.99", Support::Older, "baud girl's beta: synced as 0.97, not tried on an FM-1"},
        // her current release (2026-10-12: lower latency, lighter reverb and distortion, knob labels): 0.97's
        // formats. Tried on an FM-1 2026-10-16: all 128 presets written and read back, pattern and preset round trips
        {101, "101", Support::Current, ""},
    };
    // Felucca's releases X.Y answer FM-1_9XY (its build.py --release); other builds FM-1_900
    // (X.Y.Z answers FM-1_9XY: told apart by INFO, checkVersion below). 1.5's projects open on no release
    // before 1.5: those are pulled from only; an FM-1 on 1.0.3 syncs fully with an instance set to Felucca
    // 1.0.3 (kept)
    static const std::vector<KnownVersion> felucca = {
        {904, "0.4 beta", Support::Deprecated, "the plugin supports Felucca from 1.0: update it with Felucca's installer"},
        {910, "1.0.5.2", Support::Older, "pulled from only: update it to 1.6.1 with Felucca's installer to send to it or go Live"},
        {911, "1.1.5.1", Support::Older, "pulled from only: update it to 1.6.1 with Felucca's installer to send to it or go Live"},
        {914, "1.4.1", Support::Older, "pulled from only: update it to 1.6.1 with Felucca's installer to send to it or go Live"},
        // 1.6 stores a step's condition in its chance byte (102..112): 1.5 and 1.5.1 refuse a project holding one
        // (the slot reads empty there), so they are pulled from only
        {915, "1.5.1", Support::Older, "pulled from only: update it to 1.6.1 with Felucca's installer to send to it or go Live"},   // (tried 2026-10-11)
        {916, "1.6.1", Support::Current, ""},   // (not yet tried on an FM-1)
    };
    if (firmwareId == "fm1_stock") return stock;
    // SLOOP: every release answers FM-1_900; its INFO names the release ("FELUCCA SLOOP 2.3")
    static const std::vector<KnownVersion> sloop = {
        // Before 2.6 none reads 2.6's projects (format 6: INSERT, LFO 2, TEXTURE...): pulled from only
        // (2.3 does not read 2.4's either, and its engine parameters sit 3 lower)
        {900, "2.3", Support::Older, "pulled from only: update it to 2.7 with SLOOP's installer to send to it or go Live"},
        {900, "2.4", Support::Older, "pulled from only: update it to 2.7 with SLOOP's installer to send to it or go Live"},
        {900, "2.4.1", Support::Older, "pulled from only: update it to 2.7 with SLOOP's installer to send to it or go Live"},
        {900, "2.5", Support::Older, "pulled from only: update it to 2.7 with SLOOP's installer to send to it or go Live"},
        // 2.6 reads 2.7's projects (the same format 6; 2.7's additions in spare bytes and step flags it ignores,
        // project.c): Send works, its load clamps what 2.6 does not have (an arp MODE past ORD plays as ORD).
        // It has four globals fewer (GLO > MIDI 3), so Live refuses (Mirror::start)
        {900, "2.6", Support::Older, "Send works (2.7's new arp modes, held notes and MIDI 3 settings are dropped there); "
                                     "update it to 2.7 with SLOOP's installer to go Live"},
        {900, "2.7", Support::Current, ""},   // (not yet tried on an FM-1)
    };
    // Melodee's releases X.Y answer FM-1_9 X YY (its build.py --release: 0.13 FM-1_9013); its INFO
    // names the release ("MELODEE v0.13")
    // (X.Y.Z from 0.13.1: FM-1_9 X YY Z, 1.1.0 FM-1_91010). 1.4 saves FUN22 projects and keeps user presets in
    // one shared bank (UPB2), which no release before 1.4 reads (1.3.x reads up to FUN20, 1.1.0 and 1.2.1 up to
    // FUN16): those are pulled from only (1.4 takes their projects and banks, moving the banks into its own). An
    // FM-1 on 0.13 or 1.1.0 syncs fully with an instance set to that release (kept)
    static const std::vector<KnownVersion> melodee = {
        {9012, "0.12", Support::Older, "pulled from only: update it to 1.4.1 with Melodee's installer to send to it or go Live"},
        {9013, "0.13", Support::Older, "pulled from only (an instance set to Melodee 0.13 syncs with it): update it to 1.4.1 with Melodee's installer to send to it or go Live"},
        {90131, "0.13.1", Support::Older, "pulled from only (an instance set to Melodee 0.13 syncs with it): update it to 1.4.1 with Melodee's installer to send to it or go Live"},
        {91000, "1.0.0", Support::Older, "pulled from only: update it to 1.4.1 with Melodee's installer to send to it or go Live"},
        {91001, "1.0.1", Support::Older, "pulled from only: update it to 1.4.1 with Melodee's installer to send to it or go Live"},
        {91002, "1.0.2", Support::Older, "pulled from only: update it to 1.4.1 with Melodee's installer to send to it or go Live"},
        // (tried on an FM-1 2026-10-11 as the current release: Pull, Send, sound copies, pattern banks, Live)
        {91010, "1.1.0", Support::Older, "pulled from only (an instance set to Melodee 1.1.0 syncs with it): update it to 1.4.1 with Melodee's installer to send to it or go Live"},
        // 1.2.1 removed the effects and retired ANALOG and DIGITAL
        {91021, "1.2.1", Support::Older, "pulled from only: update it to 1.4.1 with Melodee's installer to send to it or go Live"},
        // later 1.3 releases changed the FM-1's clock (1.3.1 also its loader and recovery tools) and a
        // diagnostic field of the editor's: not its parameters, stored objects or projects (compared upstream)
        {91030, "1.3.0", Support::Older, "pulled from only: update it to 1.4.1 with Melodee's installer to send to it or go Live"},
        {91031, "1.3.1", Support::Older, "pulled from only: update it to 1.4.1 with Melodee's installer to send to it or go Live"},
        {91032, "1.3.2", Support::Older, "pulled from only: update it to 1.4.1 with Melodee's installer to send to it or go Live"},
        {91033, "1.3.3", Support::Older, "pulled from only: update it to 1.4.1 with Melodee's installer to send to it or go Live"},
        // (1.3.4 tried 2026-10-13 as the current release: Pull, Send, Live, its effects both ways)
        {91034, "1.3.4", Support::Older, "pulled from only: update it to 1.4.1 with Melodee's installer to send to it or go Live"},
        {91035, "1.3.5", Support::Older, "pulled from only: update it to 1.4.1 with Melodee's installer to send to it or go Live"},
        {91036, "1.3.6", Support::Older, "pulled from only: update it to 1.4.1 with Melodee's installer to send to it or go Live"},
        // 1.4.1 changed only its installer's Return to V15 and tests (compared upstream: firmware/ the same)
        {91040, "1.4.0", Support::Older, "synced as 1.4.1 (not tried): 1.4.1 changed only its installer"},
        {91041, "1.4.1", Support::Current, ""},   // (not yet tried on an FM-1)
    };
    if (firmwareId == "felucca") return felucca;
    if (firmwareId == "sloop") return sloop;
    if (firmwareId == "melodee") return melodee;
    // M-VAVE's FM1 Open: its release V12 answers FM-1_8000012 (app/version.h). Played by the plugin
    // (engines/fm1open); its identity seen from an FM-1 running V12 (2026-10-16); synced through its project
    // transfer protocol (Pull and Send; not yet tried with an FM-1)
    static const std::vector<KnownVersion> fm1open = {
        {8000012, "V12", Support::Current, "Pull and Send (no Live: FM1 Open has no message for one value changed)"},
    };
    if (firmwareId == "fm1open") return fm1open;
    // ChoralRoot: every release answers FM-1_920; its INFO names the release ("ChoralRoot 0.23"). 0.24-beta1
    // changed none of its backup objects or their formats (cr_backup.c, storage.c the same; its settings record
    // the same, with a fourth Key Touch, Vel + Poly AT, that 0.23 reads as Fixed)
    static const std::vector<KnownVersion> choralroot = {
        {920, "0.23", Support::Current, ""},   // (not yet tried on an FM-1)
        {920, "0.24-beta1", Support::Older, "synced as 0.23 (the same backup), not tried on an FM-1; its Key Touch Vel + Poly AT reads as Fixed in the plugin"},
    };
    if (firmwareId == "choralroot") return choralroot;
    // a release kept beside it: the one it plays (a synth on it is synced with)
    if (const auto* k = keptRelease(firmwareId)) {
        static const std::vector<std::vector<KnownVersion>> lists = [] {   // (one each, in keptReleases' order)
            std::vector<std::vector<KnownVersion>> v;
            for (const auto& r : keptReleases()) v.push_back({{r.identity, r.label, Support::Current, ""}});
            return v;
        }();
        return lists[size_t(k - keptReleases().data())];
    }
    return fmva;
}

const KnownVersion& currentVersion(const std::string& firmwareId) {
    for (const auto& v : knownVersions(firmwareId)) if (v.support == Support::Current) return v;
    return knownVersions(firmwareId).back();
}

// a dotted release ("2.3", "2.10") newer than another
static bool sloopNewer(const std::string& a, const std::string& b) {
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
    return parts(a) > parts(b);
}

std::string writesRefused(const Identity& id, const std::string& choiceId) {
    if (isPreviousRelease(choiceId)) return {};   // (a kept release's choice: only a synth on that release is synced)
    if (firmwareIdFor(id) == "melodee") {   // none before 1.4 reads 1.4's projects (FUN22; 1.3.x read up to FUN20) or its shared preset bank
        const std::string release = melodeeRelease(id), current = currentVersion("melodee").label;
        if (release.empty() || !sloopNewer("1.4", release)) return {};
        const auto* kept = [&]() -> const KeptRelease* {
            for (const auto& k : keptReleases())
                if (std::string(k.family) == "melodee" && (release == k.label || release.rfind(std::string(k.label) + ".", 0) == 0)) return &k;
            return nullptr;
        }();
        return "This FM-1 runs Melodee " + release + ", which does not read Melodee " + current + "'s projects: update it to " + current
               + " with Melodee's installer to send to it or go Live" + (kept ? ", or set this instance to Melodee " + std::string(kept->label) : "") + ". Pull still works.";
    }
    if (firmwareIdFor(id) == "choralroot") {   // its backup objects as 0.23 has them from 0.22: an older one's differ
        std::string release = id.editor.size() > 11 ? id.editor.substr(11) : std::string();
        if (const auto at = release.find(' '); at != std::string::npos) release.resize(at);
        if (release.empty() || !sloopNewer("0.22", release)) return {};
        return "This FM-1 runs ChoralRoot " + release + ", whose backup holds other objects than " + std::string(currentVersion("choralroot").label)
               + "'s: update it with ChoralRoot's installer to send to it. Pull still works.";
    }
    if (firmwareIdFor(id) == "felucca") {   // 1.6's projects open on no release before 1.6 (1.5 refuses a step's condition)
        const std::string release = feluccaRelease(id);
        const bool before16 = release.empty() ? id.version >= 910 && id.version < 916 : sloopNewer("1.6", release);   // (no INFO: its X.Y)
        if (!before16) return {};
        const bool kept = release == "1.0.3" || release.rfind("1.0.3.", 0) == 0;
        return "This FM-1 runs Felucca " + (release.empty() ? std::string("before 1.6") : release) + ", which does not read Felucca " + std::string(currentVersion("felucca").label)
               + "'s projects: update it with Felucca's installer to send to it or go Live" + (kept ? ", or set this instance to Felucca 1.0.3" : "")
               + ". Pull still works.";
    }
    if (firmwareIdFor(id) != "sloop") return {};
    const auto at = id.editor.find("SLOOP ");
    const std::string release = at == std::string::npos ? std::string() : id.editor.substr(at + 6);
    if (release.empty() || !sloopNewer("2.6", release)) return {};
    return "This FM-1 runs SLOOP " + release + ", which does not read SLOOP 2.6's projects: update it to "
           + std::string(currentVersion("sloop").label) + " with SLOOP's installer to send to it or go Live. Pull still works.";
}

VersionCheck checkVersion(const Identity& id, const std::string& choiceId) {
    VersionCheck c;
    c.firmwareId = firmwareIdFor(id);
    if (c.firmwareId == "fm1open" && !id.product.empty()) {   // a build of FM1 Open that is not a release
        c.support = Support::Older;
        c.text = id.name() + ": a build of FM1 Open (not a release); the plugin plays V12 and does not sync with FM1 Open yet";
        return c;
    }
    if (c.firmwareId == "melodee_synth") {   // (not a choice: no instance plays it, none syncs with it)
        c.support = Support::Older;
        c.text = id.name() + ": Melodee Synth only, which the plugin does not support yet";
        return c;
    }
    if (c.firmwareId == "other") {   // (likewise)
        c.support = Support::Older;
        c.text = id.name() + ": " + otherFirmwareName(id) + ", which the plugin does not support yet";
        return c;
    }
    if (isPreviousRelease(choiceId) && runs(id, choiceId)) {   // an instance set to a kept release, an FM-1 on it
        c.known = &currentVersion(choiceId);
        c.support = Support::Current;
        c.text = id.name() + ": " + firmwareChoice(choiceId).name + " " + c.known->label;
        return c;
    }
    if (c.firmwareId == "melodee") {   // its release from its INFO ("MELODEE v0.13"): Melodee changes often
        const auto& current = currentVersion("melodee");
        const auto at = id.editor.find("MELODEE v");
        const std::string release = at == std::string::npos ? std::string() : id.editor.substr(at + 9);
        c.known = &current;
        c.support = current.support;
        if (release.empty() || release == current.label) {
            c.text = id.name() + ": Melodee " + std::string(current.label);
            return c;
        }
        for (const auto& v : knownVersions("melodee"))   // a release the plugin knows, other than the current one
            if (release == v.label && &v != &current) {
                c.known = &v;
                c.support = v.support;
                c.text = id.name() + ": Melodee " + release + ", " + v.note;
                return c;
            }
        // another release: projects and parameters may differ (Live compares the parameter counts
        // and refuses another numbering; Pull and Send go by Melodee's own objects, which it converts)
        c.known = nullptr;
        c.newer = sloopNewer(release, current.label);
        c.text = id.name() + ": Melodee " + release + (c.newer ? ", newer than the plugin's " : ", older than the plugin's ") + current.label
               + "; synced as " + current.label + " (not tried). Update both to the same release for the same sounds";
        if (!c.newer) c.support = Support::Older;
        return c;
    }
    if (c.firmwareId == "choralroot") {   // its release from its INFO ("ChoralRoot 0.23"; its single-core package's "ChoralRoot 0.23 1-core")
        const auto& current = currentVersion("choralroot");
        std::string release = id.editor.substr(11);
        if (const auto at = release.find(' '); at != std::string::npos) release.resize(at);   // (cr_ui.h CR_VERSION)
        c.known = &current;
        c.support = current.support;
        if (release == current.label) {
            c.text = id.name() + ": ChoralRoot " + std::string(current.label);
            return c;
        }
        for (const auto& v : knownVersions("choralroot"))
            if (release == v.label && &v != &current) {
                c.known = &v;
                c.support = v.support;
                c.text = id.name() + ": ChoralRoot " + release + ", " + v.note;
                return c;
            }
        c.known = nullptr;
        c.newer = sloopNewer(release, current.label);
        c.text = id.name() + ": ChoralRoot " + release + (c.newer ? ", newer than the plugin's " : ", older than the plugin's ") + current.label
               + "; synced as " + current.label + " (not tried). Its backup may hold other objects than " + current.label + "'s";
        if (!c.newer) c.support = Support::Older;
        return c;
    }
    if (c.firmwareId == "sloop") {   // its release from its INFO ("FELUCCA SLOOP 2.3")
        const auto& current = currentVersion("sloop");
        c.known = &current;
        c.support = current.support;
        const auto at = id.editor.find("SLOOP ");
        const std::string release = at == std::string::npos ? std::string() : id.editor.substr(at + 6);
        for (const auto& v : knownVersions("sloop"))   // a release the plugin knows, other than the current one
            if (!release.empty() && release == v.label && &v != &current) {
                c.known = &v;
                c.support = v.support;
                c.text = id.name() + ": SLOOP " + release + ", " + v.note;
                return c;
            }
        if (release.empty() || release == current.label) {
            c.text = id.name() + ": SLOOP " + std::string(current.label) + (*current.note ? ", " + std::string(current.note) : std::string());
            return c;
        }
        c.known = nullptr;
        c.newer = sloopNewer(release, current.label);
        c.text = id.name() + ": SLOOP " + release + (c.newer ? ", newer than the plugin's " : ", older than the plugin's ") + current.label
               + "; synced as " + current.label + " (not tried). Its projects may not read the same on both";
        if (!c.newer) c.support = Support::Older;
        return c;
    }
    const auto& list = knownVersions(c.firmwareId);
    const std::string who = id.name();
    for (const auto& v : list)
        if (v.identity == id.version) c.known = &v;
    if (c.firmwareId == "felucca" && !feluccaRelease(id).empty() && id.version >= 910) {
        // the release from its INFO ("FELUCCA v1.0.3"): several answer the same FM-1_9XY
        const std::string release = feluccaRelease(id);
        const auto& current = currentVersion("felucca");
        c.known = c.known ? c.known : &current;
        if (release == current.label) { c.known = &current; c.support = current.support; c.text = who; return c; }
        if (sloopNewer(release, current.label)) {
            c.newer = true;
            c.support = current.support;
            c.text = who + ": Felucca " + release + ", newer than the plugin's " + current.label + "; synced as " + current.label + " (not tried yet)";
            return c;
        }
        const bool sameFormat = !sloopNewer("1.6", release);   // (1.6 and 1.6.1 read each other's projects)
        const bool kept = release == "1.0.3" || release.rfind("1.0.3.", 0) == 0;
        c.support = Support::Older;
        c.text = who + ": Felucca " + release + ", older than the plugin's " + current.label
               + (sameFormat ? "; synced as " + std::string(current.label) + " (not tried)"
                             : std::string("; pulled from only") + (kept ? " (an instance set to Felucca 1.0.3 syncs with it)" : "")
                               + ": update it to " + current.label + " with Felucca's installer to send to it or go Live");
        return c;
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
    // (what: what the plugin does with it, when more than nothing: FM1 Open plays in the plugin)
    UnsupportedFirmware(juce::String n, juce::String what) : name_(std::move(n)), what_(std::move(what)) {}
    juce::String name() const override { return name_; }
    juce::String summary() const override { return name_ + (what_.isEmpty() ? juce::String(": not supported yet") : ": " + what_); }
    bool has(Feature) const override { return false; }
    juce::String cannot(Feature) const override {
        return "This FM-1 runs " + name_ + (what_.isEmpty() ? juce::String(", which the plugin does not support yet.") : " (" + what_ + ").");
    }
    std::vector<Bytes> editMessages(const Sound&, edit::Channels) const override { return {}; }
    std::vector<Bytes> editChanges(const Sound&, const Sound&, edit::Channels) const override { return {}; }
private:
    juce::String name_, what_;
};

// M-VAVE's FM1 Open: played by the plugin, and synced through its own project transfer protocol
// (firmwares/fm1open/Fm1OpenSync.h; the instance's FM-1 tab), not FM-1+VA's preset messages
class Fm1OpenFirmware : public Firmware {
public:
    juce::String name() const override { return "FM1 Open"; }
    juce::String summary() const override {
        return "FM1 Open: Pull and Send (its project, a track's sound, its slots and sample bank); no Live (FM1 Open has no message for one value changed)";
    }
    bool has(Feature) const override { return false; }   // (FM-1+VA's: FM1 Open's sync is its own)
    juce::String cannot(Feature) const override {
        return "This FM-1 runs FM1 Open: Pull and Send its project, a track's sound, its slots and its sample bank on the FM-1 tab. "
               "Live is not possible: FM1 Open has no message for one value changed, and a sound sent whole would cut what plays.";
    }
    std::vector<Bytes> editMessages(const Sound&, edit::Channels) const override { return {}; }
    std::vector<Bytes> editChanges(const Sound&, const Sound&, edit::Channels) const override { return {}; }
};
}  // namespace

std::unique_ptr<Firmware> makeUnsupportedFirmware(const juce::String& name, const juce::String& what) {
    return std::make_unique<UnsupportedFirmware>(name, what);
}

bool isFirmwareChoice(const std::string& id) {
    for (const auto& c : firmwareChoices()) if (id == c.id) return true;
    return false;
}

std::unique_ptr<Firmware> firmwareFor(const Identity& id) {
    const auto which = firmwareIdFor(id);
    std::unique_ptr<Firmware> f;
    if (which == "fm1_stock") f = makeStockFirmware();
    else if (which == "felucca") f = makeFeluccaFirmware();
    else if (which == "sloop") f = makeFeluccaFirmware("SLOOP");
    else if (which == "melodee") f = makeFeluccaFirmware("Melodee");
    else if (which == "choralroot") f = makeFeluccaFirmware("ChoralRoot");
    else if (which == "melodee_synth") f = makeUnsupportedFirmware("Melodee Synth only");
    else if (which == "other") f = makeUnsupportedFirmware(otherFirmwareName(id));
    else if (which == "fm1open") f = std::make_unique<Fm1OpenFirmware>();
    else f = makeFmVaFirmware();
    f->version = id.version;
    return f;
}

}  // namespace fm1
