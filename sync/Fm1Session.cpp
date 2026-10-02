#include <algorithm>
#include "Fm1Session.h"
#include "Fm1Record.h"

using Feature = fm1::Firmware::Feature;

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
bool Fm1Session::readSettings() { return start(Op::ReadGlobals); }
void Fm1Session::pull(std::vector<int> slots) { pullSlots_ = std::move(slots); start(Op::Pull); }
void Fm1Session::push(std::vector<fm1::Sound> sounds, bool showLastOnDevice) { pushSounds_ = std::move(sounds); showLast_ = showLastOnDevice; start(Op::Push); }

void Fm1Session::pullPatterns(std::vector<int> pats) { pullPats_ = std::move(pats); start(Op::PullPatterns); }
void Fm1Session::pushPatterns(std::vector<std::pair<int, fm1::seq::Pattern>> pats, bool save) { pushPats_ = std::move(pats); savePats_ = save; start(Op::PushPatterns); }

void Fm1Session::pullCurrent() { start(Op::PullCurrent); }

void Fm1Session::sendEdit(const fm1::Sound& snd, fm1::edit::Channels ch, bool selectFirst) {
    editSound_ = snd; editCh_ = ch; editSelect_ = selectFirst;
    start(Op::SendEdit);
}

void Fm1Session::sendNow(const std::vector<fm1::Bytes>& msgs) {
    if (!link_.isOpen()) return;
    for (const auto& m : msgs) link_.sendRaw(m);
}

fm1::Port Fm1Session::port() {
    fm1::Port p(link_);
    p.cancelled = [this] { return cancel_.load() || threadShouldExit(); };
    p.progress = [this](int done, int total, const juce::String& text) { report(done, total, text); };
    return p;
}

void Fm1Session::readGlobals() {
    if (!firmware_ || !firmware_->has(Feature::ReadGlobals)) return;
    auto p = port();
    juce::String err;
    if (auto read = firmware_->readGlobals(p, err))
        juce::MessageManager::callAsync([this, g = *read] { if (onGlobals) onGlobals(g); });
}

void Fm1Session::select(int slot, int midiChannel) {
    if (!link_.isOpen()) return;
    // Program Change 0 = preset 001, per the FM-1+VA manual.
    if (midiChannel <= 0) midiChannel = midiChannel_.load();
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
        // a new profile only for a different firmware, so what it found (a searched edit buffer) is kept
        if (!firmware_ || !identity_ || identity_->isStock() != id->isStock() || identity_->version != id->version)
            firmware_ = fm1::firmwareFor(*id);
        identity_ = id;
        juce::MessageManager::callAsync([this, i = *id] { if (onIdentity) onIdentity(i); });
    }
    return id;
}

void Fm1Session::run() {
    auto p = port();
    switch (op_) {
    case Op::Identify: {
        report(0, 1, "Asking the FM-1 which firmware it runs...");
        auto id = doIdentify();
        if (!id) { report(0, 1, "The FM-1 did not answer. Check the cable and close other programs using its MIDI port.", true, true); return; }
        juce::String text = juce::String(id->name());
        if (!firmware_->has(Feature::ReadPresets)) text += " (" + firmware_->summary() + ")";
        readGlobals();
        report(1, 1, text, true);
        return;
    }
    case Op::Pull: {
        int total = int(pullSlots_.size());
        report(0, total, "Reading...");
        if (!identity_ && !doIdentify()) { report(0, total, "The FM-1 did not answer.", true, true); return; }
        if (!firmware_->has(Feature::ReadPresets)) { report(0, total, firmware_->cannot(Feature::ReadPresets), true, true); return; }
        for (int k = 0; k < total; ++k) {
            if (cancel_ || threadShouldExit()) { report(k, total, "Stopped.", true, true); return; }
            int slot = pullSlots_[size_t(k)];
            report(k, total, "Reading preset " + juce::String(slot + 1) + "...");
            juce::String err;
            auto s = firmware_->readPreset(p, slot, err);
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
        if (!firmware_->has(Feature::WritePresets)) { report(0, total, firmware_->cannot(Feature::WritePresets), true, true); return; }
        const bool check = firmware_->has(Feature::ReadPresets);
        for (int k = 0; k < total; ++k) {
            if (cancel_ || threadShouldExit()) { report(k, total, "Stopped.", true, true); return; }
            const fm1::Sound& s = pushSounds_[size_t(k)];
            auto t0 = juce::Time::getMillisecondCounter();
            report(k, total, "Writing preset " + juce::String(s.slot + 1) + " " + juce::String(fm1::voiceName(s.voice)).trimEnd() + "...");
            juce::String err;
            if (!firmware_->writePreset(p, s, err)) { report(k, total, "Preset " + juce::String(s.slot + 1) + " could not be written: " + err + ".", true, true); return; }
            fm1::Sound written = s;
            if (check) {
                auto back = firmware_->readPreset(p, s.slot, err);
                if (!back || back->voice != s.voice || back->record != s.record) {
                    report(k, total, "Preset " + juce::String(s.slot + 1) + " did not read back the same as it was sent, so writing stopped. "
                           + juce::String(k) + " of " + juce::String(total) + " were written and checked.", true, true);
                    return;
                }
                written = *back;
            }
            juce::MessageManager::callAsync([this, snd = written] { if (onSoundWritten) onSoundWritten(snd); });
            if (k < total - 1) {
                int wait = firmware_->writePaceMs() - int(juce::Time::getMillisecondCounter() - t0);
                while (wait > 0 && !cancel_ && !threadShouldExit()) { int step = std::min(wait, 100); juce::Thread::sleep(step); wait -= step; }
            }
        }
        if (showLast_ && total > 0) select(pushSounds_.back().slot);
        report(total, total, total == 1 ? (check ? "Preset written and verified." : "Preset written.")
                                        : juce::String(total) + (check ? " presets written and verified." : " presets written."), true);
        return;
    }
    case Op::PullPatterns: {
        int total = int(pullPats_.size());
        report(0, total, "Reading patterns...");
        if (!identity_ && !doIdentify()) { report(0, total, "The FM-1 did not answer.", true, true); return; }
        if (!firmware_->has(Feature::ReadPatterns)) { report(0, total, firmware_->cannot(Feature::ReadPatterns), true, true); return; }
        for (int k = 0; k < total; ++k) {
            if (cancel_ || threadShouldExit()) { report(k, total, "Stopped.", true, true); return; }
            int pat = pullPats_[size_t(k)];
            report(k, total, "Reading pattern " + juce::String(pat + 1) + "...");
            juce::String err;
            auto pattern = firmware_->readPattern(p, pat, err);
            if (!pattern) { report(k, total, "Pattern " + juce::String(pat + 1) + " could not be read: " + err + ".", true, true); return; }
            juce::MessageManager::callAsync([this, pat, pt = *pattern] { if (onPatternRead) onPatternRead(pat, pt); });
        }
        report(total, total, juce::String(total) + (total == 1 ? " pattern read." : " patterns read."), true);
        return;
    }
    case Op::PushPatterns: {
        int total = int(pushPats_.size());
        report(0, total, "Writing patterns...");
        if (!identity_ && !doIdentify()) { report(0, total, "The FM-1 did not answer.", true, true); return; }
        if (!firmware_->has(Feature::WritePatterns)) { report(0, total, firmware_->cannot(Feature::WritePatterns), true, true); return; }
        for (int k = 0; k < total; ++k) {
            if (cancel_ || threadShouldExit()) { report(k, total, "Stopped.", true, true); return; }
            int pat = pushPats_[size_t(k)].first;
            report(k, total, "Writing pattern " + juce::String(pat + 1) + "...");
            juce::String err;
            if (!firmware_->writePattern(p, pushPats_[size_t(k)].second, pat, savePats_ && k == total - 1, err)) {
                report(k, total, "Pattern " + juce::String(pat + 1) + ": " + err + ".", true, true);
                return;
            }
            juce::MessageManager::callAsync([this, pat] { if (onPatternWritten) onPatternWritten(pat); });
        }
        report(total, total, juce::String(total) + (total == 1 ? " pattern written." : " patterns written."), true);
        return;
    }
    case Op::ReadGlobals: {
        if (!identity_ && !doIdentify()) { report(0, 1, "The FM-1 did not answer.", true, true); return; }
        if (!firmware_->has(Feature::ReadGlobals)) { report(0, 1, firmware_->cannot(Feature::ReadGlobals), true, true); return; }
        juce::String err;
        auto g = firmware_->readGlobals(p, err);
        if (!g) { report(0, 1, err.isNotEmpty() && err.endsWithChar('.') ? err : "Could not read the FM-1's GLOBE settings.", true, true); return; }
        juce::MessageManager::callAsync([this, gl = *g] { if (onGlobals) onGlobals(gl); });
        report(1, 1, "Read the FM-1's GLOBE settings.", true);
        return;
    }
    case Op::PullCurrent: {
        report(0, 1, "Reading the synth's current sound...");
        if (!identity_ && !doIdentify()) { report(0, 1, "The FM-1 did not answer.", true, true); return; }
        if (!firmware_->has(Feature::ReadCurrent)) { report(0, 1, firmware_->cannot(Feature::ReadCurrent), true, true); return; }
        juce::String err;
        auto cur = firmware_->readCurrent(p, err);
        if (!cur) { report(0, 1, err, true, true); return; }
        bool same = cur->live.voice == cur->stored.voice && cur->live.record == cur->stored.record;
        juce::MessageManager::callAsync([this, c = *cur] { if (onCurrentRead) onCurrentRead(c.live, c.stored); });
        report(1, 1, "Pulled preset " + juce::String(cur->live.slot + 1) + " " + juce::String(fm1::voiceName(cur->live.voice)).trimEnd()
               + (same ? "." : ", with the synth's unsaved changes."), true);
        return;
    }
    case Op::SendEdit: {
        report(0, 1, "Sending to the synth's edit buffer...");
        if (!identity_ && !doIdentify()) { report(0, 1, "The FM-1 did not answer.", true, true); return; }
        if (editSelect_ && editSound_.slot >= 0) { select(editSound_.slot); juce::Thread::sleep(250); }
        auto msgs = firmware_->editMessages(editSound_, editCh_);
        for (size_t i = 0; i < msgs.size(); ++i) {
            link_.sendRaw(msgs[i]);
            if (i % 8 == 7) juce::Thread::sleep(2);
        }
        juce::Thread::sleep(100);
        juce::String what = "Sent " + juce::String(fm1::voiceName(editSound_.voice)).trimEnd() + " to the synth's edit buffer (not saved)";
        if (fm1::engineOf(editSound_.record) == fm1::Engine::FM && fm1::filterFromRecord(editSound_.record).on)
            what += "; the filter has no MIDI control on FM presets, so it reaches the synth only with Store to FM-1";
        if (!firmware_->has(Feature::CheckEdit)) { report(1, 1, what + "; " + firmware_->cannot(Feature::CheckEdit), true); return; }
        // the synth applies some CCs (the envelope) a little later; re-read for up to ~1 s
        fm1::Edit want = fm1::unpackVoice(editSound_.voice);
        int voiceBad = 0, recBad = 0;
        juce::String detail;
        for (int attempt = 0; attempt < 6; ++attempt) {
            juce::String err;
            auto live = firmware_->readLive(p, err);
            if (!live) { report(1, 1, what + "; could not read it back (" + err + ").", true); return; }
            voiceBad = recBad = 0; detail.clear();
            for (int i = 0; i < fm1::kEditBytes; ++i) if (live->voice[size_t(i)] != want[size_t(i)]) { ++voiceBad; detail << " v" << i << ":" << int(live->voice[size_t(i)]) << "/" << int(want[size_t(i)]); }
            for (int i = 0; i < fm1::kRecordBytes; ++i)
                if (fm1::edit::recordByteSettable(i, editSound_.record) && live->record[size_t(i)] != editSound_.record[size_t(i)]) {
                    ++recBad; detail << " r" << i << ":" << int(live->record[size_t(i)]) << "/" << int(editSound_.record[size_t(i)]);
                }
            if (!voiceBad && !recBad) break;
            juce::Thread::sleep(200);
        }
        if (voiceBad || recBad) report(1, 1, what + ", but " + juce::String(voiceBad) + " voice and " + juce::String(recBad) + " effect/envelope values read back different (got/wanted:" + detail + ").", true, true);
        else report(1, 1, what + ", verified.", true);
        return;
    }
    default: return;
    }
}
