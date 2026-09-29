#include "Fm1Session.h"

Fm1Session::Fm1Session(Fm1Link& link) : juce::Thread("FM-1 sync"), link_(link) {}
Fm1Session::~Fm1Session() { cancel_ = true; stopThread(5000); }

bool Fm1Session::start(Op op) {
    if (isThreadRunning() || !link_.isOpen()) return false;
    op_ = op;
    cancel_ = false;
    startThread();
    return true;
}

void Fm1Session::identify() { start(Op::Identify); }
void Fm1Session::pull(std::vector<int> slots) { pullSlots_ = std::move(slots); start(Op::Pull); }
void Fm1Session::push(std::vector<fm1::Sound> sounds, bool showLastOnDevice) { pushSounds_ = std::move(sounds); showLast_ = showLastOnDevice; start(Op::Push); }

void Fm1Session::pullPatterns(std::vector<int> pats) { pullPats_ = std::move(pats); start(Op::PullPatterns); }
void Fm1Session::pushPatterns(std::vector<std::pair<int, fm1::seq::Pattern>> pats, bool save) { pushPats_ = std::move(pats); savePats_ = save; start(Op::PushPatterns); }

std::optional<fm1::Bytes> Fm1Session::readMem(uint32_t addr, int n, juce::String& error) {
    error.clear();
    auto reply = link_.ask<fm1::Reply>(fm1::encodeMemRead(addr, n),
        [addr](const fm1::Bytes& f) -> std::optional<fm1::Reply> {
            auto r = fm1::decodeReply(f);
            if (!r || r->kind != fm1::Reply::Kind::Mem || r->arg != addr) return std::nullopt;
            return r;
        }, 1500, 3);
    if (!reply) { error = "no answer"; return std::nullopt; }
    if (reply->status != 0) { error = reply->status < 4 ? fm1::kStatusText[reply->status] : "refused"; return std::nullopt; }
    if (int(reply->data.size()) != n) { error = "wrong size"; return std::nullopt; }
    return reply->data;
}

bool Fm1Session::writePatternPart(const fm1::Bytes& msg, juce::String& error) {
    error.clear();
    auto reply = link_.ask<fm1::Reply>(msg,
        [](const fm1::Bytes& f) -> std::optional<fm1::Reply> {
            auto r = fm1::decodeReply(f);
            if (!r || r->kind != fm1::Reply::Kind::Pattern) return std::nullopt;
            return r;
        }, 1500, 3);
    if (!reply) { error = "no answer"; return false; }
    if (reply->status != 0) { error = reply->status < 4 ? fm1::kStatusText[reply->status] : "refused"; return false; }
    return true;
}

void Fm1Session::select(int slot, int midiChannel) {
    if (!link_.isOpen()) return;
    // Program Change 0 = preset 001, per the FM-1+VA manual.
    link_.sendRaw({uint8_t(0xC0 | ((midiChannel - 1) & 0x0F)), uint8_t(slot & 0x7F)});
}

void Fm1Session::report(int done, int total, const juce::String& text, bool finished, bool failed) {
    Progress p{op_, done, total, text, finished, failed};
    juce::MessageManager::callAsync([this, p] { if (onProgress) onProgress(p); });
}

std::optional<fm1::Identity> Fm1Session::doIdentify() {
    auto id = link_.ask<fm1::Identity>(fm1::kIdentityQuery,
        [](const fm1::Bytes& f) { return fm1::parseIdentity(f.data(), f.size()); }, 1000, 3);
    if (id) {
        identity_ = id;
        juce::MessageManager::callAsync([this, i = *id] { if (onIdentity) onIdentity(i); });
    }
    return id;
}

std::optional<fm1::Sound> Fm1Session::readSound(int slot, juce::String& error) {
    error.clear();
    auto reply = link_.ask<fm1::Reply>(fm1::encodeSoundRead(slot),
        [slot](const fm1::Bytes& f) -> std::optional<fm1::Reply> {
            auto r = fm1::decodeReply(f);
            if (!r || r->kind != fm1::Reply::Kind::Sound || int(r->arg) != slot) return std::nullopt;
            return r;
        }, 1500, 3);
    if (!reply) { error = "no answer"; return std::nullopt; }
    try {
        return fm1::soundFromReply(*reply);
    } catch (const fm1::CodecError& e) {
        error = e.what();
        return std::nullopt;
    }
}

void Fm1Session::run() {
    switch (op_) {
    case Op::Identify: {
        report(0, 1, "Asking the FM-1 which firmware it runs...");
        auto id = doIdentify();
        if (!id) { report(0, 1, "The FM-1 did not answer. Check the cable and close other programs using its MIDI port.", true, true); return; }
        juce::String text = juce::String(id->name());
        if (id->isStock()) text += " (M-VAVE firmware: can receive DX7 dumps, cannot be read back)";
        report(1, 1, text, true);
        return;
    }
    case Op::Pull: {
        int total = int(pullSlots_.size());
        report(0, total, "Reading...");
        if (!identity_ && !doIdentify()) { report(0, total, "The FM-1 did not answer.", true, true); return; }
        if (identity_->isStock()) { report(0, total, "This FM-1 runs M-VAVE's firmware, which cannot send presets back. Install FM-1+VA to pull.", true, true); return; }
        for (int k = 0; k < total; ++k) {
            if (cancel_ || threadShouldExit()) { report(k, total, "Stopped.", true, true); return; }
            int slot = pullSlots_[size_t(k)];
            report(k, total, "Reading preset " + juce::String(slot + 1) + "...");
            juce::String err;
            auto s = readSound(slot, err);
            if (!s) { report(k, total, "Preset " + juce::String(slot + 1) + " could not be read: " + err + ".", true, true); return; }
            juce::MessageManager::callAsync([this, snd = *s] { if (onSoundRead) onSoundRead(snd); });
        }
        report(total, total, total == 1 ? "Preset read." : juce::String(total) + " presets read.", true);
        return;
    }
    case Op::Push: {
        int total = int(pushSounds_.size());
        report(0, total, "Writing...");
        if (!identity_ && !doIdentify()) { report(0, total, "The FM-1 did not answer.", true, true); return; }
        if (identity_->isStock()) {
            // Stock firmware: a DX7 single-voice dump stores at once on the selected preset,
            // with no read-back. Only the current preset can be targeted, by selecting it first.
            report(0, total, "This FM-1 runs M-VAVE's firmware: only the selected preset can be written, as a DX7 voice, unverified.", true, true);
            return;
        }
        for (int k = 0; k < total; ++k) {
            if (cancel_ || threadShouldExit()) { report(k, total, "Stopped.", true, true); return; }
            const fm1::Sound& s = pushSounds_[size_t(k)];
            auto t0 = juce::Time::getMillisecondCounter();
            report(k, total, "Writing preset " + juce::String(s.slot + 1) + " " + juce::String(fm1::voiceName(s.voice)).trimEnd() + "...");
            link_.send(fm1::encodeExact(s.slot, s.voice, s.record));
            juce::String err;
            auto back = readSound(s.slot, err);
            if (!back || back->voice != s.voice || back->record != s.record) {
                report(k, total, "Preset " + juce::String(s.slot + 1) + " did not read back the same as it was sent, so writing stopped. "
                       + juce::String(k) + " of " + juce::String(total) + " were written and checked.", true, true);
                return;
            }
            juce::MessageManager::callAsync([this, snd = *back] { if (onSoundWritten) onSoundWritten(snd); });
            if (k < total - 1) {
                int wait = kPaceMs - int(juce::Time::getMillisecondCounter() - t0);
                while (wait > 0 && !cancel_ && !threadShouldExit()) { int step = std::min(wait, 100); juce::Thread::sleep(step); wait -= step; }
            }
        }
        if (showLast_ && total > 0) select(pushSounds_.back().slot);
        report(total, total, total == 1 ? "Preset written and verified." : juce::String(total) + " presets written and verified.", true);
        return;
    }
    case Op::PullPatterns: {
        int total = int(pullPats_.size());
        report(0, total, "Reading patterns...");
        if (!identity_ && !doIdentify()) { report(0, total, "The FM-1 did not answer.", true, true); return; }
        if (identity_->isStock()) { report(0, total, "This FM-1 runs M-VAVE's firmware, which cannot send patterns back.", true, true); return; }
        for (int k = 0; k < total; ++k) {
            if (cancel_ || threadShouldExit()) { report(k, total, "Stopped.", true, true); return; }
            int pat = pullPats_[size_t(k)];
            report(k, total, "Reading pattern " + juce::String(pat + 1) + "...");
            auto reqs = fm1::seq::readRequests(pat);
            fm1::Bytes steps, gset;
            juce::String err;
            for (size_t i = 0; i < reqs.size(); ++i) {
                auto data = readMem(reqs[i].addr, reqs[i].n, err);
                if (!data) {
                    // steps 17-64 only exist from FM-1_082; an older firmware refuses them
                    if (i >= 3 && identity_->version < 82) break;
                    report(k, total, "Pattern " + juce::String(pat + 1) + " could not be read: " + err + ".", true, true); return;
                }
                if (i == 2) gset = *data; else steps.insert(steps.end(), data->begin(), data->end());
            }
            fm1::seq::Pattern p;
            try { p = fm1::seq::decodePattern(steps, gset, pat); }
            catch (const fm1::CodecError& e) { report(k, total, juce::String("Pattern ") + juce::String(pat + 1) + ": " + e.what(), true, true); return; }
            juce::MessageManager::callAsync([this, pat, p] { if (onPatternRead) onPatternRead(pat, p); });
        }
        report(total, total, juce::String(total) + (total == 1 ? " pattern read." : " patterns read."), true);
        return;
    }
    case Op::PushPatterns: {
        int total = int(pushPats_.size());
        report(0, total, "Writing patterns...");
        if (!identity_ && !doIdentify()) { report(0, total, "The FM-1 did not answer.", true, true); return; }
        if (identity_->isStock()) { report(0, total, "This FM-1 runs M-VAVE's firmware, which does not take patterns over MIDI.", true, true); return; }
        for (int k = 0; k < total; ++k) {
            if (cancel_ || threadShouldExit()) { report(k, total, "Stopped.", true, true); return; }
            int pat = pushPats_[size_t(k)].first;
            const auto& p = pushPats_[size_t(k)].second;
            report(k, total, "Writing pattern " + juce::String(pat + 1) + "...");
            auto msgs = fm1::seq::encodeWrite(p, pat, savePats_ && k == total - 1);
            for (const auto& m : msgs) {
                juce::String err;
                if (!writePatternPart(m, err)) { report(k, total, "Pattern " + juce::String(pat + 1) + ": " + err + ".", true, true); return; }
                juce::Thread::sleep(50);
            }
            juce::MessageManager::callAsync([this, pat] { if (onPatternWritten) onPatternWritten(pat); });
        }
        report(total, total, juce::String(total) + (total == 1 ? " pattern written." : " patterns written."), true);
        return;
    }
    default: return;
    }
}
