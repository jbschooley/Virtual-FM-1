#include "Firmware.h"

#include <algorithm>
#include <cstdlib>

#include "Fm1Record.h"

namespace fm1 {

// ---- defaults: a feature a profile does not override is one it does not have ----

juce::String Firmware::cannot(Feature f) const {
    switch (f) {
        case Feature::ReadPresets:   return "This firmware cannot send presets back.";
        case Feature::WritePresets:  return "This firmware cannot take presets from the plugin.";
        case Feature::ReadPatterns:  return "This firmware cannot send patterns back.";
        case Feature::WritePatterns: return "This firmware does not take patterns over MIDI.";
        case Feature::ReadCurrent:   return "This firmware cannot send its current sound back.";
        case Feature::CheckEdit:     return "this firmware cannot be read back to check it.";
        case Feature::ReadGlobals:   return "The plugin cannot read this firmware's settings.";
    }
    return {};
}

std::optional<Sound> Firmware::readPreset(Port&, int, juce::String& error) { error = cannot(Feature::ReadPresets); return std::nullopt; }
bool Firmware::writePreset(Port&, const Sound&, juce::String& error) { error = cannot(Feature::WritePresets); return false; }
std::optional<seq::Pattern> Firmware::readPattern(Port&, int, juce::String& error) { error = cannot(Feature::ReadPatterns); return std::nullopt; }
bool Firmware::writePattern(Port&, const seq::Pattern&, int, bool, juce::String& error) { error = cannot(Feature::WritePatterns); return false; }
std::optional<Firmware::Current> Firmware::readCurrent(Port&, juce::String& error) { error = cannot(Feature::ReadCurrent); return std::nullopt; }
std::optional<Firmware::Live> Firmware::readLive(Port&, juce::String& error) { error = cannot(Feature::CheckEdit); return std::nullopt; }
std::optional<Globals> Firmware::readGlobals(Port&, juce::String& error) { error = cannot(Feature::ReadGlobals); return std::nullopt; }

namespace {

// ---- M-VAVE's own firmware ----------------------------------------------------------

class StockFirmware : public Firmware {
public:
    juce::String name() const override { return "M-VAVE"; }
    juce::String summary() const override { return "M-VAVE firmware: can receive DX7 dumps, cannot be read back"; }
    bool has(Feature) const override { return false; }
    juce::String cannot(Feature f) const override {
        switch (f) {
            case Feature::ReadPresets:   return "This FM-1 runs M-VAVE's firmware, which cannot send presets back. Install FM-1+VA to pull.";
            // a DX7 single-voice dump stores at once on the selected preset, with no read-back,
            // so only the current preset could be targeted, by selecting it first
            case Feature::WritePresets:  return "This FM-1 runs M-VAVE's firmware: only the selected preset can be written, as a DX7 voice, unverified.";
            case Feature::ReadPatterns:  return "This FM-1 runs M-VAVE's firmware, which cannot send patterns back.";
            case Feature::WritePatterns: return "This FM-1 runs M-VAVE's firmware, which does not take patterns over MIDI.";
            case Feature::ReadCurrent:   return "This FM-1 runs M-VAVE's firmware, which cannot send its sound back.";
            case Feature::ReadGlobals:   return "The plugin can read the FM-1's GLOBE settings on FM-1_093 only.";
            case Feature::CheckEdit:     return Firmware::cannot(f);
        }
        return Firmware::cannot(f);
    }
};

// ---- Felucca (Leo Kuroshita) ----------------------------------------------------------
// Answers M-VAVE's identity request as FM-1_904 (0.4 beta). Its other SysEx is for
// firmware updates only: no preset or pattern transfer, and it ignores FM-1+VA's
// commands and DX7 dumps. It saves four projects (a sound and its sequence) in its
// own format, and its sound parameters are not FM-1+VA's, so the plugin sends it nothing.

class FeluccaFirmware : public Firmware {
public:
    juce::String name() const override { return "Felucca"; }
    juce::String summary() const override { return "Felucca firmware: no preset or pattern transfer over MIDI"; }
    bool has(Feature) const override { return false; }
    juce::String cannot(Feature f) const override {
        switch (f) {
            case Feature::ReadPresets:   return "This FM-1 runs Felucca, which cannot send its sounds over MIDI.";
            case Feature::WritePresets:  return "This FM-1 runs Felucca, which does not take presets over MIDI.";
            case Feature::ReadPatterns:  return "This FM-1 runs Felucca, which cannot send its sequences over MIDI.";
            case Feature::WritePatterns: return "This FM-1 runs Felucca, which does not take patterns over MIDI.";
            case Feature::ReadCurrent:   return "This FM-1 runs Felucca, which cannot send its sound over MIDI.";
            case Feature::ReadGlobals:   return "The plugin cannot read Felucca's settings.";
            case Feature::CheckEdit:     return "Felucca does not take FM-1+VA presets.";
        }
        return Firmware::cannot(f);
    }
    std::vector<Bytes> editMessages(const Sound&, edit::Channels) const override { return {}; }
    std::vector<Bytes> editChanges(const Sound&, const Sound&, edit::Channels) const override { return {}; }
};

// ---- baud girl's FM-1+VA --------------------------------------------------------------
// Sound read 0x10 and exact write 0x04, memory read 0x11 and pattern write 0x20
// (sync/Fm1Codec.h, sync/Fm1Seq.h); the edit buffer and GLOBE settings are read
// from RAM at addresses found per build.

class FmVaFirmware : public Firmware {
public:
    juce::String name() const override { return "FM-1+VA"; }
    juce::String summary() const override { return "FM-1+VA: full two-way sync"; }
    bool has(Feature f) const override {
        if (f == Feature::ReadGlobals) return knownAddrs().has_value();
        return true;
    }
    juce::String cannot(Feature f) const override {
        if (f == Feature::ReadGlobals) return "The plugin can read the FM-1's GLOBE settings on FM-1_093 only.";
        return Firmware::cannot(f);
    }
    // each write rebuilds the effects and writes flash; closer spacing was heard as crackling (her bank.js)
    int writePaceMs() const override { return 3000; }

    std::optional<Sound> readPreset(Port& port, int slot, juce::String& error) override {
        error.clear();
        auto reply = port.link.ask<Reply>(encodeSoundRead(slot),
            [slot](const Bytes& f) -> std::optional<Reply> {
                auto r = decodeReply(f);
                if (!r || r->kind != Reply::Kind::Sound || int(r->arg) != slot) return std::nullopt;
                return r;
            }, 1500, 3);
        if (!reply) { error = "no answer"; return std::nullopt; }
        try {
            return soundFromReply(*reply);
        } catch (const CodecError& e) {
            error = e.what();
            return std::nullopt;
        }
    }

    bool writePreset(Port& port, const Sound& s, juce::String&) override {
        port.link.send(encodeExact(s.slot, s.voice, s.record));
        return true;
    }

    std::optional<seq::Pattern> readPattern(Port& port, int pat, juce::String& error) override {
        auto reqs = seq::readRequests(pat);
        Bytes steps, gset;
        for (size_t i = 0; i < reqs.size(); ++i) {
            auto data = readMem(port, reqs[i].addr, reqs[i].n, error);
            if (!data) {
                // steps 17-64 only exist from FM-1_082; an older firmware refuses them
                if (i >= 3 && version < 82) break;
                return std::nullopt;
            }
            if (i == 2) gset = *data; else steps.insert(steps.end(), data->begin(), data->end());
        }
        try { return seq::decodePattern(steps, gset, pat); }
        catch (const CodecError& e) { error = e.what(); return std::nullopt; }
    }

    bool writePattern(Port& port, const seq::Pattern& p, int pat, bool save, juce::String& error) override {
        for (const auto& m : seq::encodeWrite(p, pat, save)) {
            error.clear();
            auto reply = port.link.ask<Reply>(m,
                [](const Bytes& f) -> std::optional<Reply> {
                    auto r = decodeReply(f);
                    if (!r || r->kind != Reply::Kind::Pattern) return std::nullopt;
                    return r;
                }, 1500, 3);
            if (!reply) { error = "no answer"; return false; }
            if (reply->status != 0) { error = reply->status < 4 ? kStatusText[reply->status] : "refused"; return false; }
            juce::Thread::sleep(50);
        }
        return true;
    }

    std::optional<Current> readCurrent(Port& port, juce::String& error) override {
        juce::String err;
        int slot = -1;
        auto addr = editBufferAddr(port, err, &slot);
        if (!addr) { error = "Could not find the synth's current sound: " + err + "."; return std::nullopt; }
        uint32_t edit = *addr;
        Bytes vced;
        if (!readBlock(port, edit, kEditBytes, vced, err)) { error = "Could not read the edit buffer: " + err; return std::nullopt; }
        // sanity: a DX7 edit buffer has every parameter at 99 or below and a printable name
        bool plausible = vced[134] <= 31 && vced[135] <= 7;
        for (int i = 0; i < 145 && plausible; ++i) plausible = vced[size_t(i)] <= 99;
        for (int i = 145; i < 155 && plausible; ++i) plausible = vced[size_t(i)] >= 0x20 && vced[size_t(i)] < 0x7F;
        if (!plausible) { error = "The synth's edit buffer did not look like a sound; this firmware may keep it elsewhere."; return std::nullopt; }
        Edit e{};
        std::copy(vced.begin(), vced.end(), e.begin());
        if (slot < 0) {
            // discovered earlier: the slot is the stored preset with the same name, if exactly one
            for (int i = 0; i < kSlots; ++i) {
                juce::String e2;
                auto s = readPreset(port, i, e2);
                if (s && std::equal(s->voice.begin() + 118, s->voice.end(), vced.begin() + 145)) { slot = i; break; }
            }
            if (slot < 0) { error = "Read the sound, but could not tell which preset it is."; return std::nullopt; }
        }
        if (slot < 0 || slot >= kSlots) { error = "The current preset number read back as " + juce::String(slot) + "."; return std::nullopt; }
        auto stored = readPreset(port, slot, err);
        if (!stored) { error = "Could not read preset " + juce::String(slot + 1) + ": " + err; return std::nullopt; }
        Sound cur = *stored;
        cur.voice = packVoice(e);
        // the live settings record follows the edit buffer and its operator-switch byte
        Bytes rec;
        if (readBlock(port, edit + 156, kRecordBytes, rec, err)) {
            bool looksRight = true;   // the chain positions hold effect ids 0..5 in some order
            int seen = 0;
            for (int k = 0; k < kEffects; ++k) { int id = rec[size_t(27 + 3 * k)]; if (id >= kEffects || (seen & (1 << id))) looksRight = false; else seen |= 1 << id; }
            if (looksRight) std::copy(rec.begin(), rec.end(), cur.record.begin());
        }
        return Current{cur, *stored};
    }

    std::optional<Live> readLive(Port& port, juce::String& error) override {
        auto addr = editBufferAddr(port, error, nullptr);
        if (!addr) return std::nullopt;
        Bytes vced, rec;
        if (!readBlock(port, *addr, kEditBytes, vced, error) || !readBlock(port, *addr + 156, kRecordBytes, rec, error)) return std::nullopt;
        Live l;
        std::copy(vced.begin(), vced.end(), l.voice.begin());
        std::copy(rec.begin(), rec.end(), l.record.begin());
        return l;
    }

    // GLOBE settings on FM-1_093, from the MIDI channel byte on (offsets found by
    // changing each setting between memory dumps):
    //   +0 MIDI channel (0 = All)  +1 FX channel - 1  +2 bend up  +3 bend down
    //   +4 Keyboard > Velocity     +0x82 glide time   +0x83 glide mode (1 = Fingered)
    //   +0x6E flags: bit 1 Drive -6, bit 2 Ext Ctrl CC7 Vol off, bit 3 Overdub Rec on
    //         (bits 4 and 6 were seen set too; their meaning is not known)
    std::optional<Globals> readGlobals(Port& port, juce::String& error) override {
        auto a = knownAddrs();
        if (!a) { error = cannot(Feature::ReadGlobals); return std::nullopt; }
        Bytes b;
        if (!readBlock(port, a->globals, 0x84, b, error) || b.size() < 0x84) return std::nullopt;
        Globals g;
        g.midiChannel = b[0];
        g.fxChannel = b[1] + 1;
        g.bendUp = b[2];
        g.bendDown = b[3];
        g.keyVelocity = b[4];
        g.glideTime = b[0x82];
        g.glideFingered = b[0x83] == 1;
        g.driveMinus6 = (b[0x6E] & 0x02) != 0;
        g.cc7Volume = (b[0x6E] & 0x04) == 0;
        g.overdubRec = (b[0x6E] & 0x08) != 0;
        // a different layout (an unknown build) shows up as values out of range: ignore it
        if (g.midiChannel > 16 || g.fxChannel > 16 || g.bendUp > 48 || g.bendDown > 48 || g.keyVelocity < 1 || g.keyVelocity > 127
            || g.glideTime > 100 || b[0x83] > 1) {
            error = "Could not read the FM-1's GLOBE settings.";
            return std::nullopt;
        }
        return g;
    }

private:
    // Where the live edit buffer (155-byte VCED), the current preset number and the
    // GLOBE settings live in RAM, for the builds where they are known. Located on
    // FM-1_093 by dumping RAM around program changes and GLOBE changes
    // (tests/fm1_probe.cpp "mem" and "select"); other builds move them, so the edit
    // buffer is found by search there and the GLOBE settings are not read.
    struct Addrs { uint32_t editBuffer, slotByte, globals; };
    std::optional<Addrs> knownAddrs() const {
        if (std::getenv("FM1_SEARCH_EDIT_BUFFER") != nullptr) return std::nullopt;   // test the search path
        if (version == 93) return Addrs{0x01C10070, 0x01C0FEFA, 0x01C0FEFD};
        return std::nullopt;
    }
    std::optional<uint32_t> discovered_;   // the edit buffer, when found by search

    std::optional<Bytes> readMem(Port& port, uint32_t addr, int n, juce::String& error) {
        error.clear();
        auto reply = port.link.ask<Reply>(encodeMemRead(addr, n),
            [addr](const Bytes& f) -> std::optional<Reply> {
                auto r = decodeReply(f);
                if (!r || r->kind != Reply::Kind::Mem || r->arg != addr) return std::nullopt;
                return r;
            }, 1500, 3);
        if (!reply) { error = "no answer"; return std::nullopt; }
        if (reply->status != 0) { error = reply->status < 4 ? kStatusText[reply->status] : "refused"; return std::nullopt; }
        if (int(reply->data.size()) != n) { error = "wrong size"; return std::nullopt; }
        return reply->data;
    }

    bool readBlock(Port& port, uint32_t addr, int n, Bytes& out, juce::String& error) {
        for (int off = 0; off < n; off += 256) {
            int len = std::min(256, n - off);
            auto d = readMem(port, addr + uint32_t(off), len, error);
            if (!d) return false;
            out.insert(out.end(), d->begin(), d->end());
        }
        return true;
    }

    std::optional<uint32_t> editBufferAddr(Port& port, juce::String& err, int* slotOut) {
        if (auto a = knownAddrs()) {
            if (slotOut) {
                Bytes b;
                if (!readBlock(port, a->slotByte, 1, b, err)) return std::nullopt;
                *slotOut = b[0];
            }
            return a->editBuffer;
        }
        if (discovered_) return discovered_;
        int slot = -1;
        auto found = discoverEditBuffer(port, slot, err);
        if (!found) return std::nullopt;
        discovered_ = found;
        if (slotOut) *slotOut = slot;
        return found;
    }

    // Find the edit buffer by content: read every stored preset, dump RAM, and look
    // for the one 155-byte VCED that equals a stored preset. Works while the synth
    // has no unsaved voice edits; the slot is the preset it matches.
    std::optional<uint32_t> discoverEditBuffer(Port& port, int& slotOut, juce::String& error) {
        std::vector<Edit> stored;
        for (int i = 0; i < kSlots; ++i) {
            juce::String err;
            auto s = readPreset(port, i, err);
            if (!s) { error = "could not read preset " + juce::String(i + 1); return std::nullopt; }
            stored.push_back(unpackVoice(s->voice));
            port.progress(i, 160, "Looking for the synth's edit buffer...");
        }
        const uint32_t base = 0x01C00000, size = 0x80000;
        Bytes ram;
        for (uint32_t off = 0; off < size; off += 0x4000) {
            if (port.cancelled()) { error = "stopped"; return std::nullopt; }
            if (!readBlock(port, base + off, 0x4000, ram, error)) return std::nullopt;
            port.progress(128 + int(off / 0x4000), 160, "Looking for the synth's edit buffer...");
        }
        std::optional<uint32_t> found;
        int matches = 0;
        for (int i = 0; i < kSlots; ++i) {
            auto it = std::search(ram.begin(), ram.end(), stored[size_t(i)].begin(), stored[size_t(i)].end());
            while (it != ram.end()) {
                ++matches;
                found = base + uint32_t(it - ram.begin());
                slotOut = i;
                it = std::search(it + 1, ram.end(), stored[size_t(i)].begin(), stored[size_t(i)].end());
            }
        }
        if (matches != 1) {
            error = matches == 0 ? "the synth has unsaved changes or this firmware keeps its edit buffer differently; save on the synth and try again"
                                 : "more than one preset matched; select a preset with a unique sound on the synth and try again";
            return std::nullopt;
        }
        return found;
    }
};

}  // namespace

std::unique_ptr<Firmware> firmwareFor(const Identity& id) {
    std::unique_ptr<Firmware> f;
    if (id.isStock()) f = std::make_unique<StockFirmware>();
    else if (id.version >= 900) f = std::make_unique<FeluccaFirmware>();   // 0.4 beta is FM-1_904; later versions assumed to stay in the 900s
    else f = std::make_unique<FmVaFirmware>();
    f->version = id.version;
    return f;
}

}  // namespace fm1
