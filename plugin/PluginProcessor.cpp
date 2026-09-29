#include "PluginProcessor.h"

#include "PluginEditor.h"

FM1Processor::FM1Processor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "params", Params::layout()),
      params(apvts),
      session(link) {
    session.onSoundRead = [this](const fm1::Sound& s) {
        bank.setSound(s.slot, s, true);
        if (s.slot == bank.currentSlot()) loadCurrentIntoParams();
    };
    session.onSoundWritten = [this](const fm1::Sound& s) { bank.markOnDevice(s.slot, s); };
    session.onProgress = [this](const Fm1Session::Progress& p) { if (onStatus) onStatus(p.text); };
    session.onPatternRead = [this](int pat, const fm1::seq::Pattern& p) {
        const juce::SpinLock::ScopedLockType l(sequencer.lock);
        // keep the plugin-only per-step extras the synth does not carry
        auto& dst = sequencer.patterns[size_t(pat)];
        fm1::seq::Pattern merged = p;
        merged.transpose = dst.transpose;
        for (int i = 0; i < fm1::seq::kSteps; ++i) {
            auto& ms = merged.steps[size_t(i)];
            const auto& ds = dst.steps[size_t(i)];
            ms.ratchet = ds.ratchet; ms.gate = ds.gate; ms.chance = ds.chance; ms.transpose = ds.transpose; ms.accent = ds.accent; ms.slide = ds.slide;
        }
        dst = merged;
    };
    loadCurrentIntoParams();
}

FM1Processor::~FM1Processor() { session.cancel(); }

void FM1Processor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    keyboardMidi.reset(sampleRate);
    synth_.prepare(sampleRate);
    fx_.prepare(sampleRate, samplesPerBlock);
    sequencer.prepare(sampleRate);
    arp.prepare(sampleRate);
    mono_.setSize(1, std::max(1, samplesPerBlock));
    generated_.ensureSize(4096);
    synthEvents_.ensureSize(4096);
    params.changed = true;
}

bool FM1Processor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void FM1Processor::pushNoteOn(int note, int vel) {
    int start1, size1, start2, size2;
    noteFifo_.prepareToWrite(1, start1, size1, start2, size2);
    if (size1 > 0) { noteFifoData_[size_t(start1)] = {note, vel}; noteFifo_.finishedWrite(1); }
}

bool FM1Processor::popNoteOn(NoteEvent& e) {
    int start1, size1, start2, size2;
    noteFifo_.prepareToRead(1, start1, size1, start2, size2);
    if (size1 <= 0) return false;
    e = noteFifoData_[size_t(start1)];
    noteFifo_.finishedRead(1);
    return true;
}

void FM1Processor::applyParamsToEngine() {
    {
        const juce::SpinLock::ScopedTryLockType l(nameLock_);
        if (!l.isLocked()) { params.changed = true; return; }
        params.fillVced(vced_.data());
    }
    synth_.setPatch(vced_.data());
    fx_.setChain(params.fxChain(fm1::fxFromRecord(bank.current().record)));
    fm1::Envelope e = params.envelope();
    synth_.setEnvelope(e.on, e.a, e.d, e.s, e.r);
}

void FM1Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const int numSamples = buffer.getNumSamples();
    if (mono_.getNumSamples() < numSamples) mono_.setSize(1, numSamples, false, false, true);

    if (params.changed.exchange(false)) applyParamsToEngine();

    keyboardMidi.removeNextBlockOfMessages(midi, numSamples);

    juce::AudioPlayHead::PositionInfo posInfo;
    const juce::AudioPlayHead::PositionInfo* pos = nullptr;
    if (auto* ph = getPlayHead()) if (auto p = ph->getPosition()) { posInfo = *p; pos = &posInfo; }

    // 1. incoming MIDI: notes go to the arp when it is on, otherwise straight to the synth
    synthEvents_.clear();
    generated_.clear();
    bool arpOn = arp.enabled.load();
    for (const auto meta : midi) {
        const auto m = meta.getMessage();
        int at = juce::jlimit(0, numSamples - 1, meta.samplePosition);
        if (m.isNoteOn()) {
            pushNoteOn(m.getNoteNumber(), m.getVelocity());
            if (arpOn) arp.noteOn(m.getNoteNumber(), m.getVelocity()); else synthEvents_.addEvent(m, at);
        } else if (m.isNoteOff()) {
            if (arpOn) arp.noteOff(m.getNoteNumber()); else synthEvents_.addEvent(m, at);
        } else if (m.isProgramChange()) {
            pendingProgram_ = m.getProgramChangeNumber();
        } else if (m.isSysEx()) {
            juce::MemoryBlock mb(m.getRawData(), size_t(m.getRawDataSize()));
            juce::MessageManager::callAsync([this, mb] { handleDx7Sysex(static_cast<const uint8_t*>(mb.getData()), int(mb.getSize())); });
        } else {
            synthEvents_.addEvent(m, at);
        }
    }
    // 2. the sequencer and arpeggiator add their notes
    sequencer.process(pos, numSamples, generated_);
    arp.process(pos, numSamples, generated_);
    synthEvents_.addEvents(generated_, 0, numSamples, 0);
    int soundReq = sequencer.patternSoundRequest.exchange(-1);
    if (soundReq >= 0 && soundReq != bank.currentSlot()) pendingProgram_ = soundReq;

    // 3. render
    float* out = mono_.getWritePointer(0);
    int p0 = 0;
    for (const auto meta : synthEvents_) {
        const auto m = meta.getMessage();
        int at = juce::jlimit(0, numSamples, meta.samplePosition);
        if (at > p0) { synth_.render(out + p0, at - p0); p0 = at; }
        if (m.isNoteOn()) synth_.noteOn(m.getNoteNumber(), m.getVelocity());
        else if (m.isNoteOff()) synth_.noteOff(m.getNoteNumber());
        else if (m.isPitchWheel()) synth_.setPitchBend(m.getPitchWheelValue());
        else if (m.isSustainPedalOn()) synth_.setSustain(true);
        else if (m.isSustainPedalOff()) synth_.setSustain(false);
        else if (m.isAllNotesOff()) synth_.allNotesOff();
        else if (m.isAllSoundOff()) synth_.allSoundOff();
        else if (m.isController()) {
            switch (m.getControllerNumber()) {
                case 1: synth_.setModWheel(m.getControllerValue()); break;
                case 2: synth_.setBreath(m.getControllerValue()); break;
                case 4: synth_.setFoot(m.getControllerValue()); break;
                default: break;
            }
        } else if (m.isChannelPressure()) synth_.setAftertouch(m.getChannelPressureValue());
    }
    if (p0 < numSamples) synth_.render(out + p0, numSamples - p0);
    fx_.process(out, numSamples);
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) buffer.copyFrom(ch, 0, out, numSamples);

    // 4. MIDI out: what the sequencer and arpeggiator played
    midi.clear();
    midi.addEvents(generated_, 0, numSamples, 0);

    int prog = pendingProgram_.exchange(-1);
    if (prog >= 0) juce::MessageManager::callAsync([this, prog] { selectSlot(prog); });
}

juce::AudioProcessorEditor* FM1Processor::createEditor() { return new FM1Editor(*this); }

// ---- programs and the current sound ---------------------------------------------

void FM1Processor::loadCurrentIntoParams() {
    loadedSlot_ = bank.currentSlot();
    const fm1::Sound& s = bank.current();
    {
        const juce::SpinLock::ScopedLockType l(nameLock_);
        fm1::Edit e = fm1::unpackVoice(s.voice);
        std::copy(e.begin(), e.end(), vced_.begin());
    }
    params.load(s);
}

fm1::Sound& FM1Processor::commitCurrent() {
    fm1::Sound& s = bank.current();
    if (loadedSlot_ == bank.currentSlot()) params.commit(s);
    return s;
}

void FM1Processor::selectSlot(int slot) {
    slot = juce::jlimit(0, BankModel::kSlots - 1, slot);
    if (slot == bank.currentSlot() && loadedSlot_ == slot) return;
    commitCurrent();
    bank.setCurrentSlot(slot);
    loadCurrentIntoParams();
    updateHostDisplay(ChangeDetails().withProgramChanged(true));
}

void FM1Processor::setCurrentProgram(int index) {
    if (juce::MessageManager::getInstance()->isThisTheMessageThread()) selectSlot(index);
    else pendingProgram_ = index;
}

const juce::String FM1Processor::getProgramName(int index) {
    if (index < 0 || index >= BankModel::kSlots) return {};
    return BankModel::bankName(index) + " " + juce::String(fm1::voiceName(bank.slot(index).sound.voice)).trimEnd();
}

void FM1Processor::setCurrentName(const juce::String& name) {
    fm1::Sound& s = commitCurrent();
    s.voice = fm1::withName(s.voice, name.toStdString());
    {
        const juce::SpinLock::ScopedLockType l(nameLock_);
        for (int i = 0; i < 10; ++i) vced_[size_t(145 + i)] = s.voice[size_t(118 + i)];
    }
    bank.setSound(bank.currentSlot(), s, false);
}

void FM1Processor::setCurrentSound(const fm1::Sound& s) {
    bank.setSound(bank.currentSlot(), s, false);
    loadCurrentIntoParams();
}

// ---- state ---------------------------------------------------------------------

static juce::String stepToString(const fm1::seq::Step& s) {
    juce::String t = "r" + juce::String(s.rate) + " k" + juce::String(s.ratchet) + " g" + juce::String(s.gate) + " c" + juce::String(s.chance)
                   + " t" + juce::String(s.transpose) + " a" + juce::String(s.accent ? 1 : 0) + " s" + juce::String(s.slide ? 1 : 0) + " n";
    for (size_t i = 0; i < s.notes.size(); ++i) t += (i ? "," : "") + juce::String(s.notes[i].note) + ":" + juce::String(s.notes[i].vel);
    return t;
}
static fm1::seq::Step stepFromString(const juce::String& t) {
    fm1::seq::Step s;
    for (const auto& tok : juce::StringArray::fromTokens(t, " ", "")) {
        if (tok.isEmpty()) continue;
        juce::juce_wchar c = tok[0];
        juce::String v = tok.substring(1);
        switch (c) {
            case 'r': s.rate = v.getIntValue(); break;
            case 'k': s.ratchet = v.getIntValue(); break;
            case 'g': s.gate = v.getIntValue(); break;
            case 'c': s.chance = v.getIntValue(); break;
            case 't': s.transpose = v.getIntValue(); break;
            case 'a': s.accent = v.getIntValue() != 0; break;
            case 's': s.slide = v.getIntValue() != 0; break;
            case 'n':
                for (const auto& nv : juce::StringArray::fromTokens(v, ",", ""))
                    if (nv.containsChar(':')) s.notes.push_back({nv.upToFirstOccurrenceOf(":", false, false).getIntValue(), nv.fromFirstOccurrenceOf(":", false, false).getIntValue()});
                break;
            default: break;
        }
    }
    return s;
}

void FM1Processor::getStateInformation(juce::MemoryBlock& dest) {
    commitCurrent();
    juce::ValueTree v("FM1Companion");
    v.setProperty("version", 2, nullptr);
    v.setProperty("midiIn", link.ports().inputId, nullptr);
    v.setProperty("midiOut", link.ports().outputId, nullptr);
    v.addChild(bank.toState(), -1, nullptr);
    v.addChild(apvts.copyState(), -1, nullptr);
    juce::ValueTree sq("Sequencer");
    sq.setProperty("enabled", sequencer.enabled.load(), nullptr);
    sq.setProperty("sync", sequencer.syncToHost.load(), nullptr);
    sq.setProperty("selected", sequencer.selected.load(), nullptr);
    sq.setProperty("overdub", sequencer.overdub.load(), nullptr);
    {
        const juce::SpinLock::ScopedLockType l(sequencer.lock);
        for (int i = 0; i < Sequencer::kPatterns; ++i) {
            const auto& p = sequencer.patterns[size_t(i)];
            juce::ValueTree pt("Pattern");
            pt.setProperty("i", i, nullptr);
            pt.setProperty("length", p.length, nullptr); pt.setProperty("rate", p.rate, nullptr); pt.setProperty("tempo", p.tempo, nullptr);
            pt.setProperty("gate", p.gate, nullptr); pt.setProperty("swing", p.swing, nullptr); pt.setProperty("sound", p.sound, nullptr);
            pt.setProperty("transpose", p.transpose, nullptr); pt.setProperty("chain", sequencer.chain[size_t(i)], nullptr);
            juce::StringArray steps;
            for (const auto& s : p.steps) steps.add(stepToString(s));
            pt.setProperty("steps", steps.joinIntoString("|"), nullptr);
            sq.addChild(pt, -1, nullptr);
        }
    }
    v.addChild(sq, -1, nullptr);
    juce::ValueTree ar("Arp");
    ar.setProperty("enabled", arp.enabled.load(), nullptr); ar.setProperty("mode", arp.mode.load(), nullptr); ar.setProperty("octaves", arp.octaves.load(), nullptr);
    ar.setProperty("rate", arp.rate.load(), nullptr); ar.setProperty("tempo", arp.tempo.load(), nullptr); ar.setProperty("gate", arp.gate.load(), nullptr);
    ar.setProperty("swing", arp.swing.load(), nullptr); ar.setProperty("latch", arp.latch.load(), nullptr); ar.setProperty("sync", arp.syncToHost.load(), nullptr);
    v.addChild(ar, -1, nullptr);
    juce::MemoryOutputStream os(dest, false);
    v.writeToStream(os);
}

void FM1Processor::setStateInformation(const void* data, int size) {
    auto v = juce::ValueTree::readFromData(data, size_t(size));
    if (!v.isValid() || !v.hasType("FM1Companion")) return;
    bank.fromState(v.getChildWithName("FM1Bank"));
    loadedSlot_ = -1;
    loadCurrentIntoParams();
    // the saved parameters (edits since the slot was loaded) win over the slot's bytes
    auto ps = v.getChildWithName(apvts.state.getType());
    if (ps.isValid()) { apvts.replaceState(ps); params.changed = true; }
    auto sq = v.getChildWithName("Sequencer");
    if (sq.isValid()) {
        sequencer.enabled = bool(sq.getProperty("enabled", false));
        sequencer.syncToHost = bool(sq.getProperty("sync", true));
        sequencer.selected = int(sq.getProperty("selected", 0));
        sequencer.overdub = bool(sq.getProperty("overdub", false));
        const juce::SpinLock::ScopedLockType l(sequencer.lock);
        for (const auto& pt : sq) {
            int i = pt.getProperty("i", -1);
            if (i < 0 || i >= Sequencer::kPatterns) continue;
            auto& p = sequencer.patterns[size_t(i)];
            p.length = pt.getProperty("length", 16); p.rate = pt.getProperty("rate", 6); p.tempo = pt.getProperty("tempo", 120);
            p.gate = pt.getProperty("gate", 50); p.swing = pt.getProperty("swing", 50); p.sound = pt.getProperty("sound", 0);
            p.transpose = pt.getProperty("transpose", 0); sequencer.chain[size_t(i)] = pt.getProperty("chain", -1);
            auto steps = juce::StringArray::fromTokens(pt.getProperty("steps").toString(), "|", "");
            for (int k = 0; k < fm1::seq::kSteps && k < steps.size(); ++k) p.steps[size_t(k)] = stepFromString(steps[k]);
        }
    }
    auto ar = v.getChildWithName("Arp");
    if (ar.isValid()) {
        arp.enabled = bool(ar.getProperty("enabled", false)); arp.mode = int(ar.getProperty("mode", 0)); arp.octaves = int(ar.getProperty("octaves", 1));
        arp.rate = int(ar.getProperty("rate", 6)); arp.tempo = int(ar.getProperty("tempo", 120)); arp.gate = int(ar.getProperty("gate", 50));
        arp.swing = int(ar.getProperty("swing", 50)); arp.latch = bool(ar.getProperty("latch", false)); arp.syncToHost = bool(ar.getProperty("sync", true));
    }
    juce::String in = v.getProperty("midiIn").toString(), out = v.getProperty("midiOut").toString();
    if (in.isNotEmpty() && out.isNotEmpty()) connect(in, out);
}

// ---- sync ----------------------------------------------------------------------

bool FM1Processor::connect(const juce::String& inputId, const juce::String& outputId) {
    bool ok = link.open(inputId, outputId);
    if (ok) { bank.clearDeviceState(); session.identify(); }
    if (onStatus) onStatus(ok ? "Connected to " + link.ports().inputName : "Could not open those MIDI ports.");
    return ok;
}

bool FM1Processor::autoConnect() {
    auto p = Fm1Link::findFm1();
    if (!p) { if (onStatus) onStatus("No FM-1 found over USB. Connect it, switch it on, and try again."); return false; }
    return connect(p->inputId, p->outputId);
}

void FM1Processor::disconnect() {
    session.cancel();
    link.close();
    if (onStatus) onStatus("Disconnected.");
}

void FM1Processor::pullCurrent() { if (link.isOpen()) session.pull({bank.currentSlot()}); }
void FM1Processor::pushCurrent() { if (link.isOpen()) session.push({commitCurrent()}, true); }

void FM1Processor::pullAll() {
    if (!link.isOpen()) return;
    std::vector<int> all(BankModel::kSlots);
    for (int i = 0; i < BankModel::kSlots; ++i) all[size_t(i)] = i;
    session.pull(all);
}

void FM1Processor::pushChanged() {
    if (!link.isOpen()) return;
    commitCurrent();
    std::vector<fm1::Sound> out;
    for (int i = 0; i < BankModel::kSlots; ++i)
        if (!bank.slot(i).synced()) out.push_back(bank.slot(i).sound);
    if (out.empty()) { if (onStatus) onStatus("Every preset already matches the FM-1."); return; }
    session.push(out);
}

void FM1Processor::pushAll() {
    if (!link.isOpen()) return;
    commitCurrent();
    std::vector<fm1::Sound> out;
    for (int i = 0; i < BankModel::kSlots; ++i) out.push_back(bank.slot(i).sound);
    session.push(out);
}

void FM1Processor::selectOnDevice() { session.select(bank.currentSlot()); }

void FM1Processor::pullPatterns() {
    if (!link.isOpen()) return;
    std::vector<int> all(Sequencer::kPatterns);
    for (int i = 0; i < Sequencer::kPatterns; ++i) all[size_t(i)] = i;
    session.pullPatterns(all);
}

void FM1Processor::pushPatterns(bool save) {
    if (!link.isOpen()) return;
    std::vector<std::pair<int, fm1::seq::Pattern>> all;
    {
        const juce::SpinLock::ScopedLockType l(sequencer.lock);
        for (int i = 0; i < Sequencer::kPatterns; ++i) all.emplace_back(i, sequencer.patterns[size_t(i)]);
    }
    session.pushPatterns(all, save);
}

// ---- SysEx from the host and .syx files -------------------------------------------

void FM1Processor::handleDx7Sysex(const uint8_t* data, int size) {
    fm1::Bytes m(data, data + size);
    fm1::SyxContents c = fm1::readSyx(m);
    if (c.sounds.empty()) return;
    if (c.sounds.size() == 1) {
        fm1::Sound s = c.sounds[0];
        if (!s.hasRecord) { s.record = bank.current().record; s.hasRecord = true; }
        setCurrentSound(s);
        if (onStatus) onStatus("Received voice " + juce::String(fm1::voiceName(s.voice)).trimEnd() + " into " + BankModel::bankName(bank.currentSlot()));
    } else {
        int base = (bank.currentSlot() / fm1::kBankSlots) * fm1::kBankSlots;
        for (size_t i = 0; i < c.sounds.size() && int(i) < fm1::kBankSlots; ++i) {
            fm1::Sound s = c.sounds[i];
            int slot = s.slot >= 0 ? s.slot : base + int(i);
            if (!s.hasRecord) { s.record = bank.slot(slot).sound.record; s.hasRecord = true; }
            bank.setSound(slot, s, false);
        }
        loadCurrentIntoParams();
        if (onStatus) onStatus("Received a bank of " + juce::String(int(c.sounds.size())) + " voices into bank " + BankModel::bankName(base).substring(0, 1));
    }
}

juce::String FM1Processor::importSyx(const juce::File& f) {
    juce::MemoryBlock mb;
    if (!f.loadFileAsData(mb)) return "Could not read " + f.getFileName();
    fm1::Bytes bytes(static_cast<const uint8_t*>(mb.getData()), static_cast<const uint8_t*>(mb.getData()) + mb.getSize());
    fm1::SyxContents c = fm1::readSyx(bytes);
    if (c.sounds.empty()) return "No sounds in " + f.getFileName() + (c.skipped.empty() ? "" : " (" + juce::String(c.skipped[0]) + ")");
    commitCurrent();
    int base = (bank.currentSlot() / fm1::kBankSlots) * fm1::kBankSlots;
    int n = 0;
    for (size_t i = 0; i < c.sounds.size(); ++i) {
        fm1::Sound s = c.sounds[i];
        int slot = s.slot >= 0 ? s.slot : base + int(i);
        if (slot >= BankModel::kSlots) break;
        if (!s.hasRecord) { s.record = bank.slot(slot).sound.record; s.hasRecord = true; }
        bank.setSound(slot, s, false);
        ++n;
    }
    loadCurrentIntoParams();
    return "Loaded " + juce::String(n) + " sounds from " + f.getFileName() + (c.skipped.empty() ? "" : ", skipped " + juce::String(int(c.skipped.size())));
}

bool FM1Processor::exportSyx(const juce::File& f) {
    commitCurrent();
    std::vector<fm1::Sound> all;
    for (int i = 0; i < BankModel::kSlots; ++i) all.push_back(bank.slot(i).sound);
    fm1::Bytes bytes = fm1::toSyx(all);
    return f.replaceWithData(bytes.data(), bytes.size());
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FM1Processor(); }
