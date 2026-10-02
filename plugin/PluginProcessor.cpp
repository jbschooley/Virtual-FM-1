#include "PluginProcessor.h"

#include <set>

#include "Fm1Json.h"
#include "SeqMidi.h"
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
    session.onGlobals = [this](const Fm1Session::Globals& g) {
        globals_ = g;
        channels.fx = g.fxChannel;
        channels.midi = g.midiChannel == 0 ? 1 : g.midiChannel;   // "All" hears every channel
        session.setMidiChannel(channels.midi);
        if (copyPending_) { copyPending_ = false; copyGlobalsToSettings(); }
        if (onGlobals) onGlobals();
    };
    session.onCurrentRead = [this](const fm1::Sound& live, const fm1::Sound& stored) {
        // the slot takes what the synth has stored; the editor takes what it is playing,
        // so its unsaved changes show up as edits here too
        bank.setSound(stored.slot, stored, true);
        bank.setCurrentSlot(stored.slot);
        loadCurrentIntoParams();
        params.applyEdit(live);
        editName_ = juce::String(fm1::voiceName(live.voice)).trimEnd();
        applyEditName();
        if (live_) { lastSent_ = live; haveLastSent_ = true; }
        updateHostDisplay(ChangeDetails().withProgramChanged(true));
    };
    session.onProgress = [this](const Fm1Session::Progress& p) { if (onStatus) onStatus(p.text); };
    session.onPatternRead = [this](int pat, const fm1::seq::Pattern& p) {
        const juce::SpinLock::ScopedLockType l(sequencer.lock);
        // keep the plugin-only per-step extras the synth does not carry
        auto& dst = sequencer.patterns[size_t(pat)];
        fm1::seq::Pattern merged = p;
        merged.transpose = dst.transpose;
        merged.sound = -1;                                   // the synth's old preset byte is stale since FM-1_060
        sequencer.chain[size_t(pat)] = p.chain;              // FM-1_093 stores Chain per pattern
        for (int i = 0; i < fm1::seq::kSteps; ++i) {
            auto& ms = merged.steps[size_t(i)];
            const auto& ds = dst.steps[size_t(i)];
            ms.ratchet = ds.ratchet; ms.gate = ds.gate; ms.chance = ds.chance; ms.transpose = ds.transpose; ms.accent = ds.accent; ms.slide = ds.slide;
            for (auto& n : ms.notes) for (const auto& dn : ds.notes) if (dn.note == n.note) n.tie = dn.tie;
        }
        dst = merged;
        ++patternsVersion;
    };
    // the 128-preset library is shared by every instance: start from it if it exists
    bank.onLibraryChange = [this] { libraryDirty_ = true; };
    loadLibrary();
    loadCurrentIntoParams();
    setSettings(defaultSettings());
    writeDiagnostics();
    background_.startTimer(1000);
}

juce::File FM1Processor::libraryFile() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Application Support").getChildFile("Virtual FM-1").getChildFile("library.fm1lib");
}

bool FM1Processor::loadLibrary() {
    auto f = libraryFile();
    if (!f.existsAsFile()) {   // carried over from the project's earlier name
        auto old = f.getParentDirectory().getSiblingFile("FM-1 Companion").getChildFile("library.fm1lib");
        if (old.existsAsFile()) { f.getParentDirectory().createDirectory(); old.copyFileTo(f); }
    }
    if (!f.existsAsFile()) return false;
    juce::MemoryBlock mb;
    if (!f.loadFileAsData(mb)) return false;
    auto v = juce::ValueTree::readFromData(mb.getData(), mb.getSize());
    if (!v.isValid() || !v.hasType("FM1Bank")) return false;
    int cur = bank.currentSlot();
    auto onLib = bank.onLibraryChange;
    bank.onLibraryChange = nullptr;
    bank.fromState(v);
    bank.setCurrentSlot(cur);            // each instance keeps its own current preset
    bank.onLibraryChange = onLib;
    libraryLoadedTime_ = f.getLastModificationTime();
    return true;
}

void FM1Processor::saveLibrary() {
    auto f = libraryFile();
    f.getParentDirectory().createDirectory();
    juce::MemoryOutputStream os;
    bank.toState().writeToStream(os);
    juce::TemporaryFile tmp(f);
    if (tmp.getFile().replaceWithData(os.getData(), os.getDataSize()) && tmp.overwriteTargetFileWithTemporary())
        libraryLoadedTime_ = f.getLastModificationTime();
    libraryDirty_ = false;
}

static void diag(const juce::String& line) {
    juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("fm1-companion.log")
        .appendText(juce::Time::getCurrentTime().toISO8601(true) + "  " + line + "\n");
}

void FM1Processor::backgroundTick() {
    if (settingsNotify_.exchange(false) && onSettingsChanged) onSettingsChanged();
    // the shared library: save our changes, or pick up another instance's
    if (libraryDirty_) saveLibrary();
    else if (libraryFile().getLastModificationTime() > libraryLoadedTime_) {
        bool edited = isEdited();
        if (loadLibrary() && !edited) loadCurrentIntoParams();
    }
    // the synth: connect when it appears, let go when it disappears
    if (session.busy()) return;
    if (link.isOpen()) {
        bool present = false;
        for (auto& d : juce::MidiOutput::getAvailableDevices()) present = present || d.identifier == link.ports().outputId;
        if (!present) {
            link.close();
            if (live_) setLive(false);
            if (onStatus) onStatus("The FM-1 was disconnected.");
        }
    } else if (autoConnect_) {
        if (auto p = Fm1Link::findFm1()) {
            bool ok = connect(p->inputId, p->outputId, true);
            diag(juce::String("auto-connect to ") + p->inputName + (ok ? ": ok" : ": could not open the ports"));
        }
    }
}

// A small log of what this instance can see, for diagnosing hosts that run the plugin
// out of process or sandboxed (Logic's AUHostingService): which MIDI ports exist and
// whether the FM-1's can be opened. Written to the process's temporary folder.
void FM1Processor::writeDiagnostics() {
    juce::String text;
    text << juce::Time::getCurrentTime().toISO8601(true) << "  " << juce::PluginHostType().getHostDescription()
         << "  wrapper " << juce::AudioProcessor::getWrapperTypeDescription(wrapperType)
         << "  process " << juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName() << "\n";
    for (auto& d : juce::MidiInput::getAvailableDevices()) text << "  in : " << d.name << "\n";
    for (auto& d : juce::MidiOutput::getAvailableDevices()) text << "  out: " << d.name << "\n";
    if (auto p = Fm1Link::findFm1()) text << "  findFm1: " << p->inputName << " / " << p->outputName << "\n";
    else text << "  findFm1: none\n";
    text << "  library: " << libraryFile().getFullPathName() << (libraryFile().existsAsFile() ? "" : " (none yet)") << "\n";
    juce::File f = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("fm1-companion.log");
    f.replaceWithText(text);
}

FM1Processor::~FM1Processor() {
    background_.stopTimer();
    if (libraryDirty_) saveLibrary();
    stopTimer();
    session.cancel();
}

void FM1Processor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    keyboardMidi.reset(sampleRate);
    synth_.prepare(sampleRate);
    fx_.prepare(sampleRate, samplesPerBlock);
    sequencer.prepare(sampleRate);
    arp.prepare(sampleRate);
    mono_.setSize(1, std::max(1, samplesPerBlock));
    generated_.ensureSize(4096);
    synthEvents_.ensureSize(4096);
    filtered_.ensureSize(4096);
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
    fm1::VaFilter f = params.filter();
    FmSynth::Filter ef;
    ef.on = f.on; ef.type = f.type; ef.keyTrack = f.keyTrack; ef.cutoff = f.cutoff; ef.resonance = f.resonance;
    ef.envelope = f.envelope; ef.decay = f.decay; ef.shape = f.shape; ef.velocity = f.velocity; ef.lfo = f.lfo;
    synth_.setFilter(ef);
}

void FM1Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const int numSamples = buffer.getNumSamples();
    if (mono_.getNumSamples() < numSamples) mono_.setSize(1, numSamples, false, false, true);

    if (params.changed.exchange(false)) applyParamsToEngine();
    for (int i = 0; i < 6; ++i) synth_.setOperatorEnabled(i, opEnabled[size_t(i)].load());
    synth_.setPitchBendRange(bendUp_.load(), bendDown_.load());

    // the host's MIDI on the chosen channel only (SysEx and the on-screen keyboard always pass)
    if (int ch = inputChannel_.load(); ch > 0) {
        filtered_.clear();
        for (const auto meta : midi) {
            const auto m = meta.getMessage();
            if (m.getChannel() == 0 || m.getChannel() == ch) filtered_.addEvent(m, meta.samplePosition);
        }
        midi.swapWith(filtered_);
    }
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
            sequencer.recordNoteOn(m.getNoteNumber(), m.getVelocity());
            if (arpOn) arp.noteOn(m.getNoteNumber(), m.getVelocity()); else synthEvents_.addEvent(m, at);
        } else if (m.isNoteOff()) {
            sequencer.recordNoteOff(m.getNoteNumber());
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
    sequencer.patternSoundRequest = -1;   // no preset per pattern on the FM-1: the selected preset plays

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
    editName_ = juce::String(fm1::voiceName(s.voice)).trimEnd();
    {
        const juce::SpinLock::ScopedLockType l(nameLock_);
        fm1::Edit e = fm1::unpackVoice(s.voice);
        std::copy(e.begin(), e.end(), vced_.begin());
    }
    params.load(s);
}

void FM1Processor::applyEditName() {
    fm1::Voice named = fm1::withName(bank.current().voice, editName_.toStdString());
    const juce::SpinLock::ScopedLockType l(nameLock_);
    for (int i = 0; i < 10; ++i) vced_[size_t(145 + i)] = named[size_t(118 + i)];
}

bool FM1Processor::isEdited() const {
    if (loadedSlot_ != bank.currentSlot()) return false;
    return params.isEdited() || editName_ != juce::String(fm1::voiceName(bank.current().voice)).trimEnd();
}

fm1::Sound FM1Processor::editedSound() const {
    fm1::Sound s = bank.current();
    Params& p = const_cast<Params&>(params);
    // commit into a copy without moving the editor's baseline
    auto snapshot = p.snapshot();
    p.commit(s);
    p.restoreSnapshot(snapshot);
    s.voice = fm1::withName(s.voice, editName_.toStdString());
    return s;
}

fm1::Sound& FM1Processor::commitCurrent() {
    fm1::Sound& s = bank.current();
    if (loadedSlot_ == bank.currentSlot()) {
        fm1::Sound c = s;
        params.commit(c);
        c.voice = fm1::withName(c.voice, editName_.toStdString());
        bank.setSound(bank.currentSlot(), c, false);
    }
    return bank.current();
}

void FM1Processor::store() { commitCurrent(); }
void FM1Processor::revert() { loadCurrentIntoParams(); bank.onChange ? bank.onChange() : void(); }

void FM1Processor::selectSlot(int slot) {
    slot = juce::jlimit(0, BankModel::kSlots - 1, slot);
    if (slot == bank.currentSlot() && loadedSlot_ == slot) return;
    bank.setCurrentSlot(slot);
    loadCurrentIntoParams();
    updateHostDisplay(ChangeDetails().withProgramChanged(true));
    if (live_ && link.isOpen()) sendToFm1EditBuffer();
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
    editName_ = name.substring(0, 10).trimEnd();
    applyEditName();
    if (bank.onChange) bank.onChange();
}

void FM1Processor::setCurrentSound(const fm1::Sound& s) {
    bank.setSound(bank.currentSlot(), s, false);
    loadCurrentIntoParams();
}

// ---- state ---------------------------------------------------------------------

static juce::String stepToString(const fm1::seq::Step& s) {
    juce::String t = "r" + juce::String(s.rate) + " k" + juce::String(s.ratchet) + " g" + juce::String(s.gate) + " c" + juce::String(s.chance)
                   + " t" + juce::String(s.transpose) + " a" + juce::String(s.accent ? 1 : 0) + " s" + juce::String(s.slide ? 1 : 0) + " n";
    for (size_t i = 0; i < s.notes.size(); ++i) t += (i ? "," : "") + juce::String(s.notes[i].note) + ":" + juce::String(s.notes[i].vel) + (s.notes[i].tie ? "~" : "");
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
                    if (nv.containsChar(':')) s.notes.push_back({nv.upToFirstOccurrenceOf(":", false, false).getIntValue(),
                                                                  nv.fromFirstOccurrenceOf(":", false, false).getIntValue(), nv.endsWithChar('~')});
                break;
            default: break;
        }
    }
    return s;
}

void FM1Processor::getStateInformation(juce::MemoryBlock& dest) {
    juce::ValueTree v("FM1Companion");   // the state's tag predates the rename; kept so saved sessions load
    v.setProperty("version", 3, nullptr);
    v.setProperty("editName", editName_, nullptr);
    v.setProperty("fxChannel", channels.fx, nullptr);
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
    v.addChild(settings_.toTree(), -1, nullptr);
    juce::MemoryOutputStream os(dest, false);
    v.writeToStream(os);
}

void FM1Processor::setStateInformation(const void* data, int size) {
    auto v = juce::ValueTree::readFromData(data, size_t(size));
    if (!v.isValid() || !v.hasType("FM1Companion")) return;
    // The library is shared; the project's copy is used only where there is none yet
    // (first run after updating, or a project opened on another computer).
    auto saved = v.getChildWithName("FM1Bank");
    auto hasContent = [](const juce::ValueTree& b) {   // anything beyond a blank instance's INIT VOICEs
        BankModel probe; probe.fromState(b);
        fm1::Voice init = fm1::packVoice(fm1::kInitEdit);
        for (int i = 0; i < BankModel::kSlots; ++i)
            if (probe.slot(i).onDevice || probe.slot(i).sound.voice != init) return true;
        return false;
    };
    if (!libraryFile().existsAsFile() && saved.isValid() && hasContent(saved)) { bank.fromState(saved); saveLibrary(); }
    else if (saved.isValid()) bank.setCurrentSlot(int(saved.getProperty("current", 0)));
    loadedSlot_ = -1;
    loadCurrentIntoParams();
    // the saved parameters (edits since the slot was loaded) win over the slot's bytes
    auto ps = v.getChildWithName(apvts.state.getType());
    if (ps.isValid()) { apvts.replaceState(ps); params.changed = true; }
    if (v.hasProperty("editName")) { editName_ = v.getProperty("editName").toString(); applyEditName(); }
    channels.fx = juce::jlimit(1, 16, int(v.getProperty("fxChannel", 2)));
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
            p.gate = pt.getProperty("gate", 50); p.swing = pt.getProperty("swing", 50); p.sound = -1;
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
    // a project saved before settings existed keeps the defaults this instance started with
    if (auto st = v.getChildWithName("Settings"); st.isValid()) setSettings(PluginSettings::fromTree(st));
    juce::String in = v.getProperty("midiIn").toString(), out = v.getProperty("midiOut").toString();
    if (in.isNotEmpty() && out.isNotEmpty()) connect(in, out);
}

// ---- sync ----------------------------------------------------------------------

bool FM1Processor::connect(const juce::String& inputId, const juce::String& outputId, bool quiet) {
    bool ok = link.open(inputId, outputId);
    if (ok) { autoConnect_ = true; session.identify(); }
    if (onStatus && (ok || !quiet)) onStatus(ok ? "Connected to " + link.ports().inputName : "Could not open those MIDI ports.");
    return ok;
}

bool FM1Processor::autoConnect() {
    auto p = Fm1Link::findFm1();
    if (!p) { if (onStatus) onStatus("No FM-1 found over USB. Connect it, switch it on, and try again."); return false; }
    return connect(p->inputId, p->outputId);
}

void FM1Processor::disconnect() {
    autoConnect_ = false;
    session.cancel();
    link.close();
    if (onStatus) onStatus("Disconnected.");
}

void FM1Processor::pullCurrent() { if (link.isOpen()) session.pullCurrent(); }
void FM1Processor::pushCurrent() { if (link.isOpen()) session.push({commitCurrent()}, true); }

void FM1Processor::sendToFm1EditBuffer() {
    if (!link.isOpen()) return;
    fm1::Sound s = editedSound();
    lastSent_ = s;
    haveLastSent_ = true;
    session.sendEdit(s, channels, true);
}

void FM1Processor::setLive(bool on) {
    live_ = on;
    if (on) { sendToFm1EditBuffer(); startTimerHz(30); }
    else { stopTimer(); haveLastSent_ = false; }
}

void FM1Processor::timerCallback() {
    if (!live_ || !link.isOpen() || !haveLastSent_ || session.busy()) return;
    fm1::Sound now = editedSound();
    if (now.voice == lastSent_.voice && now.record == lastSent_.record) return;
    session.sendNow(fm1::edit::delta(lastSent_, now, channels));
    lastSent_ = now;
}

void FM1Processor::pullAll() {
    if (!link.isOpen()) return;
    std::vector<int> all(BankModel::kSlots);
    for (int i = 0; i < BankModel::kSlots; ++i) all[size_t(i)] = i;
    session.pull(all);
}

void FM1Processor::pushChanged() {
    if (!link.isOpen()) return;
    std::vector<fm1::Sound> out;
    for (int i = 0; i < BankModel::kSlots; ++i)
        if (!bank.slot(i).synced()) out.push_back(bank.slot(i).sound);
    if (out.empty()) { if (onStatus) onStatus("Every preset already matches the FM-1."); return; }
    session.push(out);
}

void FM1Processor::pushAll() {
    if (!link.isOpen()) return;
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

bool FM1Processor::exportSyx(const juce::File& f, const std::vector<int>& slots) {
    std::vector<fm1::Sound> all;
    std::vector<int> which = slots;
    if (which.empty()) for (int i = 0; i < BankModel::kSlots; ++i) which.push_back(i);
    for (int i : which) {
        if (i < 0 || i >= BankModel::kSlots) continue;
        fm1::Sound s = i == bank.currentSlot() ? editedSound() : bank.slot(i).sound;   // as in exportJson
        s.slot = i;
        all.push_back(s);
    }
    fm1::Bytes bytes = fm1::toSyx(all);
    return f.replaceWithData(bytes.data(), bytes.size());
}

// ---- settings ------------------------------------------------------------------

juce::File FM1Processor::defaultSettingsFile() { return libraryFile().getSiblingFile("settings.json"); }

PluginSettings FM1Processor::defaultSettings() {
    auto f = defaultSettingsFile();
    if (!f.existsAsFile()) return {};
    return PluginSettings::fromJson(juce::JSON::parse(f.loadFileAsString()));
}

void FM1Processor::setSettings(const PluginSettings& s) {
    settings_ = s.clamped();
    bendUp_ = settings_.bendUp;
    bendDown_ = settings_.bendDown;
    inputChannel_ = settings_.midiChannel;
    // hosts may restore state off the message thread; then the editor hears of it
    // from the background timer (backgroundTick), on the message thread
    if (juce::MessageManager::getInstance()->isThisTheMessageThread()) { if (onSettingsChanged) onSettingsChanged(); }
    else settingsNotify_ = true;
}

bool FM1Processor::saveSettingsAsDefault() {
    auto f = defaultSettingsFile();
    f.getParentDirectory().createDirectory();
    return f.replaceWithText(juce::JSON::toString(settings_.toJson()) + "\n");
}

void FM1Processor::revertSettingsToDefault() { setSettings(defaultSettings()); }

bool FM1Processor::readSynthSettings(bool thenCopy) {
    if (!link.isOpen() || !session.readSettings()) return false;
    copyPending_ = thenCopy;
    return true;
}

void FM1Processor::copyGlobalsToSettings() {
    if (!globals_) return;
    PluginSettings s = settings_;
    s.bendUp = globals_->bendUp;
    s.bendDown = globals_->bendDown;
    s.fixedVelocity = true;               // the FM-1's keys play at its Keyboard > Velocity
    s.velocity = globals_->keyVelocity;
    setSettings(s);
}

bool FM1Processor::exportJson(const juce::File& f, const std::vector<int>& slots, const std::vector<int>& patterns) {
    fm1json::Document d;
    for (int slot : slots) {
        if (slot < 0 || slot >= BankModel::kSlots) continue;
        fm1::Sound s = slot == bank.currentSlot() ? editedSound() : bank.slot(slot).sound;
        s.slot = slot;
        d.presets.push_back(s);
    }
    {
        const juce::SpinLock::ScopedLockType l(sequencer.lock);
        for (int i : patterns) {
            if (i < 0 || i >= Sequencer::kPatterns) continue;
            fm1json::PatternEntry e{i, sequencer.patterns[size_t(i)]};
            e.pattern.chain = sequencer.chain[size_t(i)];
            d.patterns.push_back(e);
        }
    }
    return f.replaceWithText(fm1json::write(d));
}

FM1Processor::JsonPreview FM1Processor::previewJson(const juce::File& f) const {
    JsonPreview p;
    if (!f.existsAsFile()) { p.errors.add("could not read " + f.getFileName()); return p; }
    fm1json::Document d = fm1json::read(f.loadFileAsString(), p.errors);
    p.presets = int(d.presets.size());
    p.patterns = int(d.patterns.size());
    for (const auto& s : d.presets) if (s.slot >= 0) p.slots.push_back(s.slot);
    return p;
}

FM1Processor::ImportResult FM1Processor::importJson(const juce::File& f, bool presets, bool patterns, Placement placement) {
    ImportResult r;
    if (!f.existsAsFile()) { r.summary = "Could not read " + f.getFileName(); return r; }
    fm1json::Document d = fm1json::read(f.loadFileAsString(), r.errors);
    if (!r.errors.isEmpty()) {
        r.summary = "Nothing imported: " + f.getFileName() + " has " + juce::String(r.errors.size()) + (r.errors.size() == 1 ? " problem" : " problems");
        return r;
    }
    juce::StringArray done, ignored;
    if (presets && !d.presets.empty()) {
        std::set<int> taken;   // slots the file's own slot numbers claim
        if (placement == Placement::OwnSlots)
            for (const auto& s : d.presets) if (s.slot >= 0) taken.insert(s.slot);
        int next = bank.currentSlot(), n = 0, dropped = 0;
        bool currentChanged = false;
        for (auto s : d.presets) {
            int slot;
            if (placement == Placement::OwnSlots && s.slot >= 0) {
                slot = s.slot;
            } else {
                while (next < BankModel::kSlots && taken.count(next)) ++next;
                slot = next++;
            }
            if (slot >= BankModel::kSlots) { ++dropped; continue; }
            s.slot = slot;
            bank.setSound(slot, s, false);
            currentChanged |= slot == bank.currentSlot();
            ++n;
        }
        if (currentChanged) loadCurrentIntoParams();
        done.add(juce::String(n) + (n == 1 ? " preset" : " presets"));
        if (dropped) ignored.add(juce::String(dropped) + " past slot 128");
    } else if (!d.presets.empty()) {
        ignored.add(juce::String(int(d.presets.size())) + " presets (import them from the Library tab)");
    }
    if (patterns && !d.patterns.empty()) {
        {
            const juce::SpinLock::ScopedLockType l(sequencer.lock);
            for (const auto& e : d.patterns) {
                auto p = e.pattern;
                sequencer.chain[size_t(e.index)] = p.chain;
                p.sound = -1;
                sequencer.patterns[size_t(e.index)] = p;
            }
        }
        ++patternsVersion;
        done.add(juce::String(int(d.patterns.size())) + (d.patterns.size() == 1 ? " pattern" : " patterns"));
    } else if (!d.patterns.empty()) {
        ignored.add(juce::String(int(d.patterns.size())) + " patterns (import them from the Sequencer tab)");
    }
    r.ok = true;
    r.summary = done.isEmpty() ? "Nothing to import in " + f.getFileName()
                               : "Loaded " + done.joinIntoString(" and ") + " from " + f.getFileName();
    if (!ignored.isEmpty()) r.summary << "; skipped " << ignored.joinIntoString(", ");
    return r;
}

bool FM1Processor::exportPatternsMidi(const juce::File& f, const std::vector<int>& patterns) {
    std::vector<seqmidi::Entry> list;
    {
        const juce::SpinLock::ScopedLockType l(sequencer.lock);
        for (int i : patterns)
            if (i >= 0 && i < Sequencer::kPatterns) list.push_back({i, sequencer.patterns[size_t(i)]});
    }
    if (list.empty()) return false;
    juce::MidiFile mf = seqmidi::toMidi(list);
    f.deleteFile();
    juce::FileOutputStream os(f);
    return os.openedOk() && mf.writeTo(os, 1);
}

juce::String FM1Processor::importPatternMidi(const juce::File& f) {
    juce::FileInputStream in(f);
    juce::MidiFile mf;
    if (!in.openedOk() || !mf.readFrom(in)) return "Could not read " + f.getFileName() + " as a MIDI file";
    if (mf.getTimeFormat() <= 0) return f.getFileName() + " uses SMPTE time, which has no beat grid";
    int pat = sequencer.selected.load();
    seqmidi::ImportResult r;
    {
        const juce::SpinLock::ScopedLockType l(sequencer.lock);
        r = seqmidi::fromMidi(mf, sequencer.patterns[size_t(pat)]);
        if (r.notes > 0) sequencer.patterns[size_t(pat)] = r.pattern;
    }
    if (r.notes == 0) return "No notes in " + f.getFileName() + "; pattern " + juce::String(pat + 1) + " is unchanged";
    ++patternsVersion;
    juce::String msg = "Pattern " + juce::String(pat + 1) + ": " + juce::String(r.notes) + " notes from " + f.getFileName()
                     + " on a " + fm1::seq::kNoteValueNames[r.pattern.rate] + " grid, " + juce::String(r.pattern.length) + " steps";
    if (r.tempoFromFile) msg << ", tempo " << r.pattern.tempo;
    if (r.pastEnd) msg << "; " << r.pastEnd << " past step 64 left out";
    if (r.crowded) msg << "; " << r.crowded << " left out where a step already had nine notes";
    return msg;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FM1Processor(); }
