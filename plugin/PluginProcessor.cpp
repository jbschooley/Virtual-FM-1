#include "PluginProcessor.h"

#include <set>

#include "Fm1Json.h"
#include "SeqMidi.h"
#include "PluginEditor.h"

#if FM1_FELUCCA
// Whether a host parameter is one of the firmware the engine plays: Felucca's or SLOOP's (Melodee
// has none yet: Felucca's numbers are not its own)
static bool playsOn(const felparams::Entry& e, const FeluccaEngine& f) {
    return f.flavor() != FeluccaEngine::Flavor::Melodee && e.sloop == (f.flavor() == FeluccaEngine::Flavor::Sloop);
}
#endif

FM1Processor::FM1Processor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "params", Params::layout(felText_)),
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
    // only a synth running this instance's firmware is synced with
    session.acceptIdentity = [this](const fm1::Identity& id) {
        return fm1::firmwareIdFor(id) == fm1::firmwareChoices()[size_t(firmwareIndex_.load())].id;
    };
    session.onRejected = [this](const fm1::Identity& id) {
        link.close();
        if (live_) setLive(false);
        autoConnect_ = false;            // not again until asked (Find FM-1, Connect, or a switch)
        pendingMismatch_ = id;
        if (onFirmwareMismatch) onFirmwareMismatch(id);
        else if (onStatus) {
            if (!fm1::isFirmwareChoice(fm1::firmwareIdFor(id)))
                onStatus("The FM-1 runs " + fm1::firmwareFor(id)->name() + ", which the plugin does not support yet. Not connected.");
            else
                onStatus("The FM-1 runs " + fm1::firmwareFor(id)->name() + "; this instance is set to "
                         + fm1::firmwareChoices()[size_t(firmwareIndex_.load())].name + ". Not connected.");
        }
    };
    session.onPatternRead = [this](int pat, const fm1::seq::Pattern& p) {
        const juce::SpinLock::ScopedLockType l(sequencer.lock);
        // keep the plugin's own step settings where the synth's release has none: before FM-1_092
        // accents, ratchets and note lengths; before FM-1_096 each step's gate, chance and transpose
        auto& dst = sequencer.patterns[size_t(pat)];
        fm1::seq::Pattern merged = p;
        merged.transpose = dst.transpose;
        merged.sound = -1;                                   // the synth's old preset byte is stale since FM-1_060
        sequencer.chain[size_t(pat)] = p.chain;              // FM-1_093 stores Chain per pattern
        for (int i = 0; i < fm1::seq::kSteps; ++i) {
            auto& ms = merged.steps[size_t(i)];
            const auto& ds = dst.steps[size_t(i)];
            if (p.readFrom < 96) { ms.gate = ds.gate; ms.chance = ds.chance; ms.transpose = ds.transpose; }
            if (p.readFrom < 92) {
                ms.ratchet = ds.ratchet; ms.accent = ds.accent;
                for (auto& n : ms.notes) for (const auto& dn : ds.notes) if (dn.note == n.note) n.len = dn.len;
            }
        }
        dst = merged;
        ++patternsVersion;
    };
   #if FM1_FELUCCA
    // Felucca's host parameters: their text is Felucca's own, from this instance's engine
    for (const auto& e : felparams::entries()) felParams_.push_back(apvts.getParameter(e.id));
    felApplied_.resize(felParams_.size());
    for (size_t i = 0; i < felParams_.size(); ++i) felApplied_[i] = felParams_[i]->getValue();
    felText_->text = [this](int entry, float v) -> juce::String {
        auto f = felucca();
        const auto& e = felparams::entries()[size_t(entry)];
        if (!f || !playsOn(e, *f)) return juce::String(v, 3);   // (the other firmware's)
        auto d = e.track < 0 ? f->globalDesc(e.index) : f->paramDesc(e.track, e.index);
        if (d.max <= d.min) return {};
        const int value = d.min + int(std::lround(v * float(d.max - d.min)));
        if (!d.names.empty() && value - d.min < int(d.names.size())) return d.names[size_t(value - d.min)];
        return juce::String(value) + (d.unit.empty() ? "" : " " + juce::String(d.unit));
    };
   #endif
    // the 128-preset library is shared by every instance: start from it if it exists
    bank.onLibraryChange = [this] { libraryDirty_ = true; };
    loadLibrary();
    loadCurrentIntoParams();
    setSettings(defaultSettings());
    writeDiagnostics();
    background_.startTimer(1000);
}

juce::File FM1Processor::libraryFile() {
    // tests and harnesses point this elsewhere so they never touch the user's library
    auto dir = juce::SystemStats::getEnvironmentVariable("FM1_DATA_DIR", {});
    if (dir.isNotEmpty()) return juce::File(dir).getChildFile("library.fm1lib");
   #if JUCE_LINUX
    // ~/.config/Virtual FM-1 (userApplicationDataDirectory is ~/.config there)
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Virtual FM-1").getChildFile("library.fm1lib");
   #else
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Application Support").getChildFile("Virtual FM-1").getChildFile("library.fm1lib");
   #endif
}

// The shared bank lives in a folder (plugin/LibraryStore.h). The first time,
// it is copied from the single library file earlier versions kept, which is
// left where it was so an older version still finds it.
bool FM1Processor::loadLibrary() {
    if (!store_.exists()) {
        auto old = libraryFile();
        if (!old.existsAsFile())   // the project's earlier name
            old = old.getParentDirectory().getSiblingFile("FM-1 Companion").getChildFile("library.fm1lib");
        BankModel scratch;
        if (!store_.migrateFrom(old, scratch)) return false;
    }
    int cur = bank.currentSlot();
    auto onLib = bank.onLibraryChange;
    bank.onLibraryChange = nullptr;
    bool ok = store_.load(bank);
    bank.setCurrentSlot(cur);            // each instance keeps its own current preset
    bank.onLibraryChange = onLib;
    reportLibrary();
    return ok;
}

// What the library folder could not give us, said once per change.
void FM1Processor::reportLibrary() {
    juce::StringArray msgs;
    if (!store_.unreadable().isEmpty()) {
        juce::StringArray names;
        for (int s : store_.unreadable()) names.add(BankModel::bankName(s));
        msgs.add(juce::String(names.size()) + " preset file(s) could not be read (" + names.joinIntoString(", ")
                 + "): damaged, from a newer version, or not downloaded yet. They are left as they are.");
    }
    if (store_.oldLibraryNewer(libraryFile()))
        msgs.add("library.fm1lib changed after your presets moved to " + store_.bankDir().getFullPathName()
                 + ": an older version of the plugin may still be using it.");
    auto text = msgs.joinIntoString(" ");
    if (text.isNotEmpty() && text != lastLibraryReport_) {
        lastLibraryReport_ = text;
        status(text);
    }
}

void FM1Processor::saveLibrary() {
    libraryDirty_ = false;
    store_.save(bank);
    if (store_.reloaded()) {   // another instance had written meanwhile: its slots are in now
        if (!isEdited()) loadCurrentIntoParams();
        reportLibrary();
    }
}

static void diag(const juce::String& line) {
    juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("fm1-companion.log")
        .appendText(juce::Time::getCurrentTime().toISO8601(true) + "  " + line + "\n");
}

void FM1Processor::backgroundTick() {
    if (settingsNotify_.exchange(false)) {
        if (onSettingsChanged) onSettingsChanged();
        if (onFirmwareChanged) onFirmwareChanged();
    }
    // the shared library: save our changes, or pick up another instance's
    if (libraryDirty_) saveLibrary();
    else if (store_.changedElsewhere()) {
        bool edited = isEdited();
        if (loadLibrary() && !edited) loadCurrentIntoParams();
    }
   #if FM1_FELUCCA
    if (auto f = felucca()) {   // the Felucca device: save what changed here, take what another instance saved
        juce::String msg;
        {
            std::lock_guard<std::mutex> g(felDeviceLock_);
            msg = deviceFor(*f).tick(*f);
        }
        if (msg.isNotEmpty()) status(msg);
    }
   #endif
    // the synth: let go when it disappears; the app also connects when it appears (a plugin
    // only when asked, with Connect or Find FM-1: several instances in a project would each
    // take the synth)
    if (session.busy()) return;
    if (link.isOpen()) {
        bool present = false;
        for (auto& d : juce::MidiOutput::getAvailableDevices()) present = present || d.identifier == link.ports().outputId;
        if (!present) {
            link.close();
            if (live_) setLive(false);
            if (onStatus) onStatus("The FM-1 was disconnected.");
        }
    } else if (autoConnect_ && wrapperType == wrapperType_Standalone) {
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
    text << "  library: " << store_.bankDir().getFullPathName() << (store_.exists() ? "" : " (none yet)") << "\n";
    juce::File f = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("fm1-companion.log");
    f.replaceWithText(text);
}

FM1Processor::~FM1Processor() {
    background_.stopTimer();
    if (libraryDirty_) saveLibrary();
    stopTimer();
    session.stop();   // its jobs use this instance's members: done before any of them goes
}

void FM1Processor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    hostRate_ = sampleRate;
    hostBlock_ = std::max(1, samplesPerBlock);
   #if FM1_FELUCCA
    if (felucca_) prepareFelucca();
   #endif
    keyboardMidi.reset(sampleRate);
    sequencer.prepare(sampleRate);
    arp.prepare(sampleRate);
    mono_.setSize(1, hostBlock_);
    generated_.ensureSize(4096);
    synthEvents_.ensureSize(4096);
    filtered_.ensureSize(4096);
    engineEvents_.ensureSize(4096);
    prepareEngine();
    prepared_ = true;
}

// The engine, effects and Hardware character stage run at the host's rate, or,
// with Hardware character on in a host not at 44.1 kHz, at the FM-1's own
// 44.1 kHz and are converted to the host's rate: FM aliases differently at
// different rates, and the FM-1's output is 44.1 kHz audio.
void FM1Processor::prepareEngine() {
    resampling_ = hardwareCharacter_.load() && std::abs(hostRate_ - kFm1Rate) > 0.5;
    const double rate = resampling_ ? kFm1Rate : hostRate_;
    int block = hostBlock_;
    if (resampling_) {
        toHost_.prepare(kFm1Rate, hostRate_, hostBlock_);
        block = toHost_.maxInputFor(hostBlock_);
    }
    engineBuf_.setSize(1, block);
    synth_.prepare(rate);
    fx_.prepare(rate, block);
    hwChar_.prepare(rate);
    setLatencySamples(resampling_ ? int(std::lround(toHost_.latencyOut())) : 0);
    params.changed = true;   // the patch again, on the freshly prepared engine
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
        baseRecord_ = base().record;
    }
    const bool silent = fm1::engineOf(baseRecord_) == fm1::Engine::EightBit;   // its patch is not a DX7 voice
    if (silent && !silent_) synth_.allSoundOff();
    silent_ = silent;
    synth_.setPatch(vced_.data());
    fx_.setChain(params.fxChain(fm1::fxFromRecord(baseRecord_)));
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
    // a firmware the plugin cannot play yet (Felucca, until its engines are in) is silent
    if (!emulates()) {
        keyboardMidi.removeNextBlockOfMessages(midi, numSamples);   // the on-screen keyboard's queue must not grow
        midi.clear();
        return;
    }

    if (params.changed.exchange(false)) applyParamsToEngine();
    for (int i = 0; i < 6; ++i) synth_.setOperatorEnabled(i, opEnabled[size_t(i)].load());
    synth_.setPitchBendRange(bendUp_.load(), bendDown_.load());

   #if FM1_FELUCCA
    const bool isFelucca = felucca_ != nullptr;
   #else
    const bool isFelucca = false;
   #endif
    // the host's MIDI on the chosen channel only (SysEx and the on-screen keyboard always pass)
    // (not for Felucca, which routes channels itself: 1-4 its parts)
    if (int ch = inputChannel_.load(); ch > 0 && !isFelucca) {
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
   #if FM1_FELUCCA
    if (isFelucca) { renderFelucca(buffer, midi, pos); return; }
   #endif

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
    if (!resampling_) {
        renderEngine(out, numSamples, synthEvents_);
    } else {
        // the engine at 44.1 kHz: its share of samples for this block, the events
        // placed at the same point of it, then converted to the host's rate
        const int need = toHost_.inputNeeded(numSamples);
        if (engineBuf_.getNumSamples() < need) engineBuf_.setSize(1, need, false, false, true);   // a block larger than announced
        engineEvents_.clear();
        for (const auto meta : synthEvents_) {
            const int at = numSamples > 0 ? int(juce::int64(meta.samplePosition) * need / numSamples) : 0;
            engineEvents_.addEvent(meta.getMessage(), juce::jlimit(0, std::max(0, need - 1), at));
        }
        renderEngine(engineBuf_.getWritePointer(0), need, engineEvents_);
        toHost_.process(engineBuf_.getReadPointer(0), need, out, numSamples);
    }
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) buffer.copyFrom(ch, 0, out, numSamples);

    // 4. MIDI out: what the sequencer and arpeggiator played
    midi.clear();
    midi.addEvents(generated_, 0, numSamples, 0);

    int prog = pendingProgram_.exchange(-1);
    if (prog >= 0) juce::MessageManager::callAsync([this, prog] { selectSlot(prog); });
}

// The engine, its effects and Hardware character, n samples at the engine's rate.
void FM1Processor::renderEngine(float* out, int numSamples, const juce::MidiBuffer& events) {
    int p0 = 0;
    for (const auto meta : events) {
        const auto m = meta.getMessage();
        int at = juce::jlimit(0, numSamples, meta.samplePosition);
        if (at > p0) { synth_.render(out + p0, at - p0); p0 = at; }
        if (m.isNoteOn()) { if (!silent_) synth_.noteOn(m.getNoteNumber(), m.getVelocity()); }
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
    // the FM-1's output (16 bits, its high end), when chosen in Settings
    bool hw = hardwareCharacter_.load();
    if (hw != hwCharWasOn_) { hwChar_.reset(); hwCharWasOn_ = hw; }
    if (hw) hwChar_.process(out, numSamples);
}

juce::AudioProcessorEditor* FM1Processor::createEditor() { return new FM1Editor(*this); }

// ---- programs and the current sound ---------------------------------------------

void FM1Processor::loadCurrentIntoParams() {
    loadedSlot_ = bank.currentSlot();
    {
        const juce::SpinLock::ScopedLockType l(nameLock_);
        projectBase_.reset();   // the library's slot is what is edited again
    }
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
    fm1::Voice named = fm1::withName(base().voice, editName_.toStdString());
    const juce::SpinLock::ScopedLockType l(nameLock_);
    for (int i = 0; i < 10; ++i) vced_[size_t(145 + i)] = named[size_t(118 + i)];
}

bool FM1Processor::isEdited() const {
    if (loadedSlot_ != bank.currentSlot()) return false;
    return projectBase_.has_value() || params.isEdited() || editName_ != juce::String(fm1::voiceName(bank.current().voice)).trimEnd();
}

fm1::Sound FM1Processor::editedSound() const {
    fm1::Sound s = base();
    Params& p = const_cast<Params&>(params);
    // commit into a copy without moving the editor's baseline
    auto snapshot = p.snapshot();
    p.commit(s);
    p.restoreSnapshot(snapshot);
    s.voice = fm1::withName(s.voice, editName_.toStdString());
    return s;
}

fm1::Sound& FM1Processor::commitCurrent() {
    if (loadedSlot_ == bank.currentSlot()) {
        fm1::Sound c = base();
        params.commit(c);
        c.voice = fm1::withName(c.voice, editName_.toStdString());
        {
            const juce::SpinLock::ScopedLockType l(nameLock_);
            projectBase_.reset();   // stored: the library's slot is the project's sound now
        }
        bank.setSound(bank.currentSlot(), c, false);
        // an 8-Bit preset keeps only its effects' edits (Params::commit): the other controls show
        // what it holds again
        if (fm1::engineOf(c.record) == fm1::Engine::EightBit) params.load(bank.current());
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

void FM1Processor::initCurrent() {
    fm1::Sound s = base();
    s.voice = fm1::packVoice(fm1::kInitEdit);
    s.record = fm1::defaultRecord();
    params.applyEdit(s);
    setCurrentName("INIT VOICE");
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
                   + " t" + juce::String(s.transpose) + " a" + juce::String(s.accent ? 1 : 0) + " n";
    // a note's length after "~" (steps past its own); an older state's bare "~" (and "s1", the step's
    // Tie & Slide) is the old tie, joined into lengths when the pattern is read (joinTies)
    for (size_t i = 0; i < s.notes.size(); ++i)
        t += (i ? "," : "") + juce::String(s.notes[i].note) + ":" + juce::String(s.notes[i].vel) + (s.notes[i].len > 0 ? "~" + juce::String(s.notes[i].len) : "");
    return t;
}
static fm1::seq::Step stepFromString(const juce::String& t) {
    fm1::seq::Step s;
    bool slide = false;
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
            case 's': slide = v.getIntValue() != 0; break;
            case 'n':
                for (const auto& nv : juce::StringArray::fromTokens(v, ",", ""))
                    if (nv.containsChar(':')) {
                        const auto hold = nv.fromFirstOccurrenceOf("~", false, false);
                        s.notes.push_back({nv.upToFirstOccurrenceOf(":", false, false).getIntValue(),
                                           nv.fromFirstOccurrenceOf(":", false, false).upToFirstOccurrenceOf("~", false, false).getIntValue(),
                                           !nv.containsChar('~') ? 0 : hold.isEmpty() ? -1 : hold.getIntValue()});
                    }
                break;
            default: break;
        }
    }
    if (slide) for (auto& n : s.notes) if (n.len == 0) n.len = -1;   // (the old Tie & Slide: every note tied)
    return s;
}

void FM1Processor::getStateInformation(juce::MemoryBlock& dest) {
    juce::ValueTree v("FM1Companion");   // the state's tag predates the rename; kept so saved sessions load
    v.setProperty("version", 4, nullptr);
    v.setProperty("editName", editName_, nullptr);
    v.setProperty("firmware", firmwareId(), nullptr);
    // the release it was made for: a later plugin playing a newer one can say so
    v.setProperty("firmwareVersion", juce::String(fm1::currentVersion(firmwareId().toStdString()).label), nullptr);
   #if FM1_FELUCCA
    for (auto flavor : {FeluccaEngine::Flavor::Felucca, FeluccaEngine::Flavor::Sloop, FeluccaEngine::Flavor::Melodee})
        if (auto fs = feluccaState(flavor); fs.isValid() && (fs.getNumProperties() > 0 || fs.getNumChildren() > 0)) v.addChild(fs, -1, nullptr);
   #endif
    v.setProperty("fxChannel", channels.fx, nullptr);
    v.setProperty("midiIn", link.ports().inputId, nullptr);
    v.setProperty("midiOut", link.ports().outputId, nullptr);
    // what the project uses: the current slot as stored (the parameters below hold
    // the editor's changes to it) and where it came from; the whole bank only on request
    {
        juce::ValueTree cur("Current");
        const auto& s = bank.current();
        cur.setProperty("bank", "FM-1", nullptr);
        cur.setProperty("slot", bank.currentSlot(), nullptr);
        cur.setProperty("voice", juce::MemoryBlock(s.voice.data(), s.voice.size()).toBase64Encoding(), nullptr);
        cur.setProperty("record", juce::MemoryBlock(s.record.data(), s.record.size()).toBase64Encoding(), nullptr);
        v.addChild(cur, -1, nullptr);
    }
    if (settings_.embedBank) v.addChild(bank.toState(), -1, nullptr);
    else {
        // no presets, only which one is current: versions before 4 look for it here
        juce::ValueTree b("FM1Bank");
        b.setProperty("current", bank.currentSlot(), nullptr);
        v.addChild(b, -1, nullptr);
    }
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
            if (fm1::seq::hasLocks(p)) pt.setProperty("locks", juce::String::toHexString(p.locks.data(), int(p.locks.size()), 0), nullptr);
            if (p.repeats != 1) pt.setProperty("repeats", p.repeats, nullptr);
            if (p.chainByte >= 0) pt.setProperty("chainByte", p.chainByte, nullptr);
            if (p.readFrom > 0) pt.setProperty("readFrom", p.readFrom, nullptr);
            // the steps' bytes as read from the synth: what goes back when they were not changed here
            if (p.raw.size() == size_t(fm1::seq::kSteps * fm1::seq::kStepBytes))
                pt.setProperty("fm1Steps", juce::String::toHexString(p.raw.data(), int(p.raw.size()), 0), nullptr);
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
   #if FM1_FELUCCA
    felResync_ = true;   // the project's Felucca sound, not its saved host values, is what plays
   #endif
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
    // (hosts may restore state off the message thread: the library is written from there, by backgroundTick)
    if (!store_.exists() && saved.isValid() && hasContent(saved)) { bank.fromState(saved); libraryDirty_ = true; }
    else if (saved.isValid()) bank.setCurrentSlot(int(saved.getProperty("current", 0)));
    // since version 4: the current slot as the project stored it
    auto cur = v.getChildWithName("Current");
    std::optional<fm1::Sound> stored;
    if (cur.isValid()) {
        fm1::Sound s;
        juce::MemoryBlock vb, rb;
        const int slot = juce::jlimit(0, BankModel::kSlots - 1, int(cur.getProperty("slot", 0)));
        if (vb.fromBase64Encoding(cur.getProperty("voice").toString()) && vb.getSize() == s.voice.size()
            && rb.fromBase64Encoding(cur.getProperty("record").toString()) && rb.getSize() == s.record.size()) {
            std::memcpy(s.voice.data(), vb.getData(), s.voice.size());
            std::memcpy(s.record.data(), rb.getData(), s.record.size());
            s.slot = slot; s.hasRecord = true; s.from = "project";
            stored = s;
        }
        bank.setCurrentSlot(slot);
        // a computer without a library yet starts it with what the project used
        if (stored && !store_.exists()) { bank.setSound(slot, *stored, false); libraryDirty_ = true; }
    }
    loadedSlot_ = -1;
    loadCurrentIntoParams();
    // The library's slot changed since the project was saved: the project's own copy is
    // what this instance edits and plays (effect order and settings the parameters do not
    // cover included), shown as unsaved changes until stored or reverted.
    if (stored && (bank.current().voice != stored->voice || bank.current().record != stored->record)) {
        {
            const juce::SpinLock::ScopedLockType l(nameLock_);
            projectBase_ = *stored;
        }
        params.load(*stored);
        editName_ = juce::String(fm1::voiceName(stored->voice)).trimEnd();
        status(BankModel::bankName(bank.currentSlot()) + " has changed in the library since this project was saved; "
               "the project's sound is loaded as unsaved changes.");
    }
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
            p.chain = sequencer.chain[size_t(i)];
            auto steps = juce::StringArray::fromTokens(pt.getProperty("steps").toString(), "|", "");
            for (int k = 0; k < fm1::seq::kSteps && k < steps.size(); ++k) p.steps[size_t(k)] = stepFromString(steps[k]);
            fm1::seq::joinTies(p);   // (an older state's ties)
            p.repeats = pt.getProperty("repeats", 1);
            p.chainByte = pt.getProperty("chainByte", -1);
            p.readFrom = pt.getProperty("readFrom", 0);
            p.raw.clear();
            juce::MemoryBlock rw;
            if (const auto hex = pt.getProperty("fm1Steps").toString(); hex.length() == 2 * fm1::seq::kSteps * fm1::seq::kStepBytes) {
                rw.loadFromHexString(hex);
                if (rw.getSize() == size_t(fm1::seq::kSteps * fm1::seq::kStepBytes))
                    p.raw.assign(static_cast<const uint8_t*>(rw.getData()), static_cast<const uint8_t*>(rw.getData()) + rw.getSize());
            }
            p.locks.clear();   // FM-1_096's parameter locks, as its table holds them
            juce::MemoryBlock lk;
            if (const auto hex = pt.getProperty("locks").toString(); hex.length() == 2 * fm1::seq::kLockBytes) {
                lk.loadFromHexString(hex);
                if (lk.getSize() == size_t(fm1::seq::kLockBytes)) p.locks.assign(static_cast<const uint8_t*>(lk.getData()), static_cast<const uint8_t*>(lk.getData()) + lk.getSize());
            }
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
    // projects from before the firmware choice were made for FM-1+VA
    setFirmware(v.getProperty("firmware", fm1::kDefaultFirmwareId).toString());
    {   // made for another release of it than the plugin plays now (a project from before versions: none said)
        const juce::String made = v.getProperty("firmwareVersion").toString();
        const auto& choice = fm1::firmwareChoice(firmwareId().toStdString());
        const juce::String now = fm1::currentVersion(choice.id).label;
        if (made.isNotEmpty() && made != now)
            status("This project was made for " + juce::String(choice.name) + " " + made + "; the plugin plays " + now
                   + (fm1::isFeluccaFamily(choice.id) ? ", which reads its projects as the device does." : "."));
    }
   #if FM1_FELUCCA
    setFeluccaState(v.getChildWithName("Felucca"), FeluccaEngine::Flavor::Felucca);
    setFeluccaState(v.getChildWithName("Sloop"), FeluccaEngine::Flavor::Sloop);
    setFeluccaState(v.getChildWithName("Melodee"), FeluccaEngine::Flavor::Melodee);
    feluccaChanged();
   #endif
    // the ports it was connected to: the app connects to them again; a plugin only when asked
    // (several instances in a project would each take the synth)
    juce::String in = v.getProperty("midiIn").toString(), out = v.getProperty("midiOut").toString();
    if (in.isNotEmpty() && out.isNotEmpty() && wrapperType == wrapperType_Standalone) connect(in, out);
}

// ---- sync ----------------------------------------------------------------------

bool FM1Processor::connect(const juce::String& inputId, const juce::String& outputId, bool quiet) {
    // tests that load the plugin must not talk to a real FM-1 that happens to be plugged in
    if (juce::SystemStats::getEnvironmentVariable("FM1_NO_DEVICE", {}).isNotEmpty()) return false;
    bool ok = link.open(inputId, outputId);
    if (ok) { autoConnect_ = true; session.identify(); }
    if (onStatus && (ok || !quiet)) onStatus(ok ? "Connected to " + link.ports().inputName : "Could not open those MIDI ports.");
    return ok;
}

bool FM1Processor::autoConnect() {
    auto p = Fm1Link::findFm1();
    if (!p) { if (onStatus) onStatus("No FM-1 found. Connect it by USB (or pair it over Bluetooth MIDI), switch it on, and try again."); return false; }
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
    session.sendChange(lastSent_, now, channels);
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
    // FM-1_096 takes every step setting (whole steps): the patterns here become what it will hold
    // (a note end that does not fit on its step moves, as on the synth), so the editor shows that
    const auto synth = session.lastIdentity();
    const bool whole = synth && synth->version >= 96 && !synth->isStock();
    std::vector<std::pair<int, fm1::seq::Pattern>> all;
    {
        const juce::SpinLock::ScopedLockType l(sequencer.lock);
        for (int i = 0; i < Sequencer::kPatterns; ++i) {
            auto& p = sequencer.patterns[size_t(i)];
            p.chain = sequencer.chain[size_t(i)];   // (the Chain is kept beside the pattern)
            if (whole) p = fm1::seq::fitToSynth(p);
            all.emplace_back(i, p);
        }
    }
    if (whole) ++patternsVersion;
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
    const bool hwChanged = s.clamped().hardwareCharacter != settings_.hardwareCharacter;
    settings_ = s.clamped();
    bendUp_ = settings_.bendUp;
    bendDown_ = settings_.bendDown;
    inputChannel_ = settings_.midiChannel;
    hardwareCharacter_ = settings_.hardwareCharacter;
    hwChar_.setVolumeDb(float(settings_.fm1VolumeDb));   // atomic inside
    if (hwChanged && prepared_) {
        // the engine moves between the host's rate and 44.1 kHz: prepare it again,
        // with the audio callback held off meanwhile
        suspendProcessing(true);
        prepareEngine();
        suspendProcessing(false);
    }
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

juce::String FM1Processor::firmwareId() const { return fm1::firmwareChoices()[size_t(firmwareIndex_.load())].id; }

bool FM1Processor::emulates() const {
   #if FM1_FELUCCA
    if (fm1::isFeluccaFamily(fm1::firmwareChoices()[size_t(firmwareIndex_.load())].id)) return felucca_ != nullptr;
   #endif
    return fm1::firmwareChoices()[size_t(firmwareIndex_.load())].supported;
}

#if FM1_FELUCCA
// Felucca runs at 44.1 kHz; in another host it is converted, with the converter's latency.
void FM1Processor::prepareFelucca() {
    felConvert_ = std::abs(hostRate_ - FeluccaEngine::kRate) > 0.5;
    int block = hostBlock_;
    if (felConvert_) {
        felL_.prepare(FeluccaEngine::kRate, hostRate_, hostBlock_);
        felR_.prepare(FeluccaEngine::kRate, hostRate_, hostBlock_);
        block = felL_.maxInputFor(hostBlock_);
    }
    felBuf_.setSize(2, block);
    setLatencySamples(felConvert_ ? int(std::lround(felL_.latencyOut())) : 0);
}

void FM1Processor::renderFelucca(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, const juce::AudioPlayHead::PositionInfo* pos) {
    auto& f = *felucca_;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() == 0) { midi.clear(); return; }
    applyHostToFelucca(f);
    if (settings_.hostTempo && pos) {   // the host's tempo, and its PLAY and STOP as Felucca's
        if (auto bpm = pos->getBpm(); bpm && *bpm > 0) f.setGlobal(0 /* G_BPM */, int(std::lround(*bpm)));
        const bool playing = pos->getIsPlaying();
        if (playing != felHostPlaying_) {
            felHostPlaying_ = playing;
            f.transport(playing);
        }
    }
    const int need = felConvert_ ? felL_.inputNeeded(n) : n;
    if (felBuf_.getNumSamples() < need) felBuf_.setSize(2, need, false, false, true);   // a block larger than announced
    float* l = felBuf_.getWritePointer(0);
    float* r = felBuf_.getWritePointer(1);
    int p0 = 0;
    for (const auto meta : midi) {
        const auto m = meta.getMessage();
        int at = felConvert_ && n > 0 ? int(juce::int64(meta.samplePosition) * need / n) : meta.samplePosition;
        at = juce::jlimit(0, std::max(0, need - 1), at);
        if (at > p0) { f.render(l + p0, r + p0, at - p0); p0 = at; }
        // the editor protocol, from the host. Not in the app: its MIDI input is a device's port, often
        // the FM-1's own, whose replies to the app's sync (a TRACK_PARAM reply reads as a setting,
        // a PRESET reply as a load) would change this Felucca
        if (m.isSysEx()) { if (wrapperType != wrapperType_Standalone) f.sysex(m.getRawData(), m.getRawDataSize()); }
        else if (m.getRawDataSize() <= 3) f.midi(m.getRawData(), m.getRawDataSize());
    }
    if (p0 < need) f.render(l + p0, r + p0, need - p0);
    const int chans = buffer.getNumChannels();
    if (felConvert_) {
        felL_.process(l, need, buffer.getWritePointer(0), n);
        if (chans > 1) felR_.process(r, need, buffer.getWritePointer(1), n);
        else {   // a mono bus: both sides, mixed
            if (mono_.getNumSamples() < n) mono_.setSize(1, n, false, false, true);
            felR_.process(r, need, mono_.getWritePointer(0), n);
            buffer.addFrom(0, 0, mono_, 0, 0, n);
            buffer.applyGain(0.5f);
        }
    } else {
        buffer.copyFrom(0, 0, l, n);
        if (chans > 1) buffer.copyFrom(1, 0, r, n);
        else { buffer.addFrom(0, 0, r, n); buffer.applyGain(0.5f); }
    }
    midi.clear();
}

// Felucca's part of a project: the music now playing as Felucca saves it, a FUN8 project
// (every part's sound, steps, FM6 patch, the song chain, the motion and the globals).
// Felucca reads its own older formats, so a later version converts it as the device does.
static const char* stateName(FeluccaEngine::Flavor f) {
    return f == FeluccaEngine::Flavor::Sloop ? "Sloop" : f == FeluccaEngine::Flavor::Melodee ? "Melodee" : "Felucca";
}
static int flavorIndex(FeluccaEngine::Flavor f) { return f == FeluccaEngine::Flavor::Sloop ? 1 : f == FeluccaEngine::Flavor::Melodee ? 2 : 0; }

juce::ValueTree FM1Processor::feluccaState(FeluccaEngine::Flavor flavor) const {
    auto engine = felucca();
    std::lock_guard<std::mutex> g(felStateLock_);
    if (!engine || engine->flavor() != flavor) {
        const auto& saved = feluccaSaved_[flavorIndex(flavor)];
        return saved.isValid() ? saved.createCopy() : juce::ValueTree(stateName(flavor));
    }
    juce::ValueTree t(stateName(flavor));
    std::vector<uint8_t> music;
    if (!engine->object(0, music) || music.empty()) return t;
    // music this Felucca could not read: the project's part as it came while nothing changed,
    // else beside the new music
    if (felOriginal_.isValid() && music == felBase_) return felOriginal_.createCopy();
    t.setProperty("version", juce::String(engine->version()), nullptr);   // the release it was saved with ("v1.0", "SLOOP 2.3")
    t.setProperty("music", juce::Base64::toBase64(music.data(), music.size()), nullptr);
    const auto& unread = felOriginal_.isValid() ? felOriginal_ : felUnread_;
    if (unread.isValid()) {
        juce::ValueTree kept("Unread");
        kept.copyPropertiesFrom(unread, nullptr);
        t.addChild(kept, -1, nullptr);
    }
    return t;
}

void FM1Processor::setFeluccaState(const juce::ValueTree& t, FeluccaEngine::Flavor flavor) {
    auto engine = felucca();
    {
        std::lock_guard<std::mutex> g(felStateLock_);
        auto& saved = feluccaSaved_[flavorIndex(flavor)];
        // (a project without one: nothing kept from before for it either)
        if (!engine || engine->flavor() != flavor) { saved = t.isValid() ? t.createCopy() : juce::ValueTree(); return; }   // kept for later, and saved
        saved = {};
    }
    if (t.isValid()) applyFeluccaState(*engine, t);
}

bool FM1Processor::applyFeluccaState(FeluccaEngine& f, const juce::ValueTree& t, bool announce) {
    const auto carried = t.getChildWithName("Unread");   // unread music from an earlier time: carried on
    {
        std::lock_guard<std::mutex> g(felStateLock_);
        felOriginal_ = {};
        felUnread_ = carried.isValid() ? carried.createCopy() : juce::ValueTree();
        felBase_.clear();
    }
    juce::MemoryOutputStream music;
    if (!juce::Base64::convertFromBase64(music, t.getProperty("music").toString()) || music.getDataSize() == 0) return true;
    const auto* b = static_cast<const uint8_t*>(music.getData());
    if (f.putObject(0, std::vector<uint8_t>(b, b + music.getDataSize())) == 0) return true;
    // a newer Felucca's, or damaged: kept untouched (saved again), and said
    {
        std::lock_guard<std::mutex> g(felStateLock_);
        felOriginal_ = t.createCopy();
        f.object(0, felBase_);
    }
    if (announce) {
        const juce::String from = t.getProperty("version").toString();
        const juce::String name = felucca::dialectOf(f).name;
        status("This project's " + name + " music" + (from.isNotEmpty() ? " (saved with " + from + ")" : juce::String())
               + " could not be read by the " + name + " built in (" + juce::String(f.version()) + "): newer, or damaged. " + name + " plays "
               "its power-on music; the project keeps its own in the file, as it was.");
    }
    return false;
}

// ---- syncing with an FM-1 running Felucca ----

// (a synth running the firmware this instance is set to, Felucca or SLOOP)
bool FM1Processor::feluccaSynth() const {
    auto id = session.lastIdentity();
    return link.isOpen() && id && fm1::isFeluccaFamily(fm1::firmwareIdFor(*id)) && juce::String(fm1::firmwareIdFor(*id)) == firmwareId()
           && feluccaSynthProblem().isEmpty();
}

juce::String FM1Processor::feluccaSynthProblem() const {
    auto id = session.lastIdentity();
    if (!link.isOpen() || !id || !fm1::isFeluccaFamily(fm1::firmwareIdFor(*id)) || juce::String(fm1::firmwareIdFor(*id)) != firmwareId()) return {};
    const auto check = fm1::checkVersion(*id);   // a release too old to sync: why, and what to do
    return check.support == fm1::Support::Deprecated ? juce::String(check.text) : juce::String();
}

// Connected to a synth the plugin only reads from (SLOOP before 2.4): why, else empty
juce::String FM1Processor::feluccaWritesRefused() const {
    auto id = session.lastIdentity();
    if (!feluccaSynth() || !id) return {};
    return fm1::writesRefused(*id);
}

// The synth's own objects, kept in the library before anything is written to it.
static juce::String saveSynthBackup(const felucca::Objects& o, const felucca::Dialect& d) {
    auto f = LibraryStore::root().getChildFile(d.name).getChildFile("Backups")
                 .getChildFile("FM-1 " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S") + ".json");
    f.getParentDirectory().createDirectory();
    return f.replaceWithText(felucca::backupJson(o, juce::String(d.name).toUpperCase() + " (FM-1)", d), false, false, "\n") ? f.getFullPathName() : juce::String();
}

// what a full Pull or Send moves, said in the firmware's terms
static juce::String everything(const felucca::Dialect& d) {
    return felucca::isSloop(d) ? "working project, projects A-D, user presets and FM6 bank"
         : felucca::isMelodee(d) ? "music, projects, user presets, CZ banks and FM6 tones"
                                 : "music, projects and user presets (with their FM6 patches)";
}

bool FM1Processor::feluccaPull() {
    auto f = felucca();
    if (!f || !feluccaSynth()) return false;
    const auto& d = felucca::dialectOf(*f);
    return session.job("Reading everything from the FM-1 running " + juce::String(d.name) + "...", [this, f, &d](fm1::Port& p) {
        felucca::LinkEndpoint synth(p.link, d);
        auto progress = [&p](int done, int total, const juce::String& text) { p.progress(done, total, text); return !p.cancelled(); };
        juce::String err;
        auto objects = felucca::backup(synth, progress, err);
        if (!objects) return Fm1Session::JobResult{false, "Could not read the FM-1: " + err + "."};
        const auto kept = saveSynthBackup(*objects, d);
        // the plugin's settings stay its own, as Send leaves the synth's (a newer Felucca's settings,
        // its LED mode from 1.0.1, would not be taken by the Felucca built in)
        objects->erase(1);
        if (!felucca::putObjects(*f, *objects, err)) return Fm1Session::JobResult{false, "Read the FM-1, but the plugin's " + juce::String(d.name) + " refused it: " + err + "."};
        felResync_ = true;
        juce::MessageManager::callAsync([this, alive = std::weak_ptr<bool>(alive_)] { if (alive.lock()) feluccaChanged(); });
        return Fm1Session::JobResult{true, "Pulled the FM-1's " + everything(d) + "." + (kept.isEmpty() ? juce::String() : " Its backup: " + kept)};
    });
}

bool FM1Processor::feluccaSend() {
    auto f = felucca();
    if (!f || !feluccaSynth() || feluccaWritesRefused().isNotEmpty()) return false;
    const auto& d = felucca::dialectOf(*f);
    return session.job("Backing up the FM-1, then sending everything to it...", [f, &d](fm1::Port& p) {
        felucca::LinkEndpoint synth(p.link, d);
        auto progress = [&p](int done, int total, const juce::String& text) { p.progress(done, total, text); return !p.cancelled(); };
        juce::String err;
        auto theirs = felucca::backup(synth, progress, err);   // first: what the synth has, kept
        if (!theirs) return Fm1Session::JobResult{false, "Nothing sent: could not back up the FM-1 first (" + err + ")."};
        const auto kept = saveSynthBackup(*theirs, d);
        if (kept.isEmpty()) return Fm1Session::JobResult{false, "Nothing sent: could not save the FM-1's backup in the library."};
        auto ours = std::optional<felucca::Objects>(felucca::objectsOf(*f));
        if (int(ours->size()) < d.lastObject + 1) return Fm1Session::JobResult{false, "Nothing sent: the plugin's " + juce::String(d.name) + " gave no backup."};
        ours->erase(1);   // the synth's settings stay its own (its panel calibration, palette, favourites)
        // Felucca: never an empty FM6 bank (8: 1.0.3's is always empty; sent to 1.0.2 it would
        // empty the synth's bank), and no 9 for a synth that does not list it (before 1.0.3)
        if (auto it = ours->find(8); it != ours->end() && it->second.empty()) ours->erase(it);
        if (!theirs->count(9)) ours->erase(9);
        if (felucca::isMelodee(d)) {
            // as Melodee's editor checks before its restore (fm1backup.js): an object the synth does not list
            // is left out if empty, else nothing is sent; nor a project larger than its music object (an
            // older release with less recording room). Nothing is written before these pass.
            for (auto it = ours->begin(); it != ours->end();) {
                if (theirs->count(it->first)) { ++it; continue; }
                if (!it->second.empty())
                    return Fm1Session::JobResult{false, "Nothing sent: this FM-1's Melodee has no object " + juce::String(it->first) + " (another release?). Its backup: " + kept};
                it = ours->erase(it);
            }
            const size_t room = (*theirs)[0].size();
            for (int id : {0, 2, 3, 4, 5})
                if (auto it = ours->find(id); it != ours->end() && it->second.size() > room)
                    return Fm1Session::JobResult{false, "Nothing sent: this FM-1's Melodee keeps smaller projects (" + juce::String(int(room)) + " bytes, "
                                                        "the plugin's " + juce::String(int(it->second.size())) + "): update it to the plugin's release. Its backup: " + kept};
        }
        if (!felucca::restore(synth, *ours, progress, err))
            return Fm1Session::JobResult{false, "Sending stopped: " + err + ". The FM-1's backup from before: " + kept};
        return Fm1Session::JobResult{true, "Sent the " + everything(d) + " to the FM-1. Its backup from before: " + kept};
    });
}

bool FM1Processor::feluccaPullSound(int track) {
    auto f = felucca();
    if (!f || !feluccaSynth() || track < 0 || track >= f->tracks()) return false;
    return session.job("Reading part " + juce::String(track + 1) + "'s sound from the FM-1...", [this, f, track](fm1::Port& p) {
        felucca::LinkEndpoint synth(p.link, felucca::dialectOf(*f));
        felucca::VirtualEndpoint mine(f);
        juce::String err;
        if (!felucca::copySound(synth, mine, track, err))
            return Fm1Session::JobResult{false, "Could not read part " + juce::String(track + 1) + "'s sound: " + err + "."};
        juce::MessageManager::callAsync([this, track, alive = std::weak_ptr<bool>(alive_)] { if (alive.lock()) feluccaChanged(track); });
        return Fm1Session::JobResult{true, "Part " + juce::String(track + 1) + "'s sound read from the FM-1."};
    });
}

bool FM1Processor::feluccaSendSound(int track) {
    auto f = felucca();
    if (!f || !feluccaSynth() || feluccaWritesRefused().isNotEmpty() || track < 0 || track >= f->tracks()) return false;
    return session.job("Sending part " + juce::String(track + 1) + "'s sound to the FM-1...", [f, track](fm1::Port& p) {
        felucca::LinkEndpoint synth(p.link, felucca::dialectOf(*f));
        felucca::VirtualEndpoint mine(f);
        juce::String err;
        if (!felucca::copySound(mine, synth, track, err))
            return Fm1Session::JobResult{false, "Could not send part " + juce::String(track + 1) + "'s sound: " + err + "."};
        return Fm1Session::JobResult{true, "Part " + juce::String(track + 1) + "'s sound sent to the FM-1 (not saved there)."};
    });
}

bool FM1Processor::feluccaLive(bool on) {
    if (!on) { if (felLive_) session.cancel(); return true; }
    auto f = felucca();
    if (!f || !feluccaSynth() || felLive_ || feluccaWritesRefused().isNotEmpty()) return false;
    felLive_ = true;
    const bool started = session.job("Live with the FM-1 running " + juce::String(felucca::dialectOf(*f).name) + "...", [this, f](fm1::Port& p) {
        felucca::LinkEndpoint synth(p.link, felucca::dialectOf(*f));
        felucca::VirtualEndpoint mine(f);
        felucca::Mirror mirror(synth, mine);
        juce::String err;
        Fm1Session::JobResult r{true, "Live sync with the FM-1 stopped."};
        {   // (edits queued after the last session's end are not this one's)
            std::lock_guard<std::mutex> g(felEditsLock_);
            felEdits_.clear();
        }
        if (!mirror.start(err)) r = {false, "Live sync did not start: " + err + "."};
        else {
            p.progress(0, 1, "Live with the FM-1: changes on either side reach the other.");
            while (!p.cancelled()) {
                std::vector<fm1::Bytes> edits;
                {
                    std::lock_guard<std::mutex> g(felEditsLock_);
                    edits.swap(felEdits_);
                }
                bool ok = true;
                for (auto& e : edits) if (!(ok = mirror.forward(e, err))) break;
                if (!ok || !mirror.tick(err)) { r = {false, "Live sync stopped: " + err + "."}; break; }
                juce::Thread::sleep(50);
            }
            mirror.stop();
        }
        felLive_ = false;
        {
            std::lock_guard<std::mutex> g(felEditsLock_);
            felEdits_.clear();
        }
        juce::MessageManager::callAsync([this, alive = std::weak_ptr<bool>(alive_)] { if (alive.lock() && onFeluccaLive) onFeluccaLive(); });
        return r;
    });
    if (!started) felLive_ = false;
    return started;
}

std::optional<fm1::Bytes> FM1Processor::feluccaEdit(const fm1::Bytes& request) {
    auto f = felucca();
    if (!f) return std::nullopt;
    auto reply = f->ask(request);
    if (felLive_) {
        std::lock_guard<std::mutex> g(felEditsLock_);
        if (felEdits_.size() < 4096) felEdits_.push_back(request);
    }
    return reply;
}

bool FM1Processor::feluccaPullPatterns() {
    auto f = felucca();
    if (!f || !feluccaSynth()) return false;
    return session.job("Reading the FM-1's patterns...", [this, f](fm1::Port& p) {
        felucca::LinkEndpoint synth(p.link, felucca::dialectOf(*f));
        felucca::VirtualEndpoint mine(f);
        auto progress = [&p](int done, int total, const juce::String& text) { p.progress(done, total, text); return !p.cancelled(); };
        juce::String err;
        if (!felucca::copySequencer(synth, mine, f->tracks(), err, progress)) return Fm1Session::JobResult{false, "Could not read the patterns: " + err + "."};
        felResync_ = true;
        return Fm1Session::JobResult{true, "Pulled the FM-1's patterns."};
    });
}

bool FM1Processor::feluccaSendPatterns() {
    auto f = felucca();
    if (!f || !feluccaSynth() || feluccaWritesRefused().isNotEmpty()) return false;
    return session.job("Sending the patterns to the FM-1...", [f](fm1::Port& p) {
        felucca::LinkEndpoint synth(p.link, felucca::dialectOf(*f));
        felucca::VirtualEndpoint mine(f);
        auto progress = [&p](int done, int total, const juce::String& text) { p.progress(done, total, text); return !p.cancelled(); };
        juce::String err;
        if (!felucca::copySequencer(mine, synth, f->tracks(), err, progress)) return Fm1Session::JobResult{false, "Sending the patterns stopped: " + err + "."};
        return Fm1Session::JobResult{true, "Sent the patterns to the FM-1 (not saved there)."};
    });
}

// Host automation into Felucca: each value the host changed since the last block, spread
// over the parameter's range in Felucca now.
void FM1Processor::applyHostToFelucca(FeluccaEngine& f) {
    const auto& all = felparams::entries();
    // just after a project loaded or an engine arrived, Felucca's own sound is the truth
    // (feluccaChanged gives it to the host): what the host holds now counts as given
    if (felResync_.exchange(false)) {
        for (size_t i = 0; i < all.size(); ++i) felApplied_[i] = felParams_[i]->getValue();
        return;
    }
    for (size_t i = 0; i < all.size(); ++i) {
        const float v = felParams_[i]->getValue();
        if (v == felApplied_[i]) continue;
        felApplied_[i] = v;
        const auto& e = all[i];
        if (!playsOn(e, f)) continue;   // (the other firmware's: not applied; when it plays, its own state is the truth)
        if (e.track < 0 && e.index == 0 && settings_.hostTempo) continue;   // BPM is the host's then
        int min = 0, max = 0;
        if (!(e.track < 0 ? f.globalRange(e.index, min, max) : f.paramRange(e.track, e.index, min, max)) || max <= min) continue;
        const int value = min + int(std::lround(v * float(max - min)));
        if (e.track < 0) f.setGlobal(e.index, value); else f.setParam(e.track, e.index, value);
    }
}

void FM1Processor::feluccaChanged(int track) {
    auto f = felucca();
    if (!f) return;
    const auto& all = felparams::entries();
    for (size_t i = 0; i < all.size(); ++i) {
        const auto& e = all[i];
        if (!playsOn(e, *f) || (track >= 0 && e.track != track)) continue;
        int min = 0, max = 0;
        if (!(e.track < 0 ? f->globalRange(e.index, min, max) : f->paramRange(e.track, e.index, min, max)) || max <= min) continue;
        const int value = e.track < 0 ? f->global(e.index) : f->param(e.track, e.index);
        const float v = float(value - min) / float(max - min);
        if (std::abs(felParams_[i]->getValue() - v) > 1.0e-6f) felParams_[i]->setValueNotifyingHost(v);
    }
}

#endif


void FM1Processor::status(const juce::String& text) {
    auto say = [this](const juce::String& t) {
        if (onStatus) { onStatus(t); return; }
        std::lock_guard<std::mutex> g(statusLock_);   // no editor yet: kept for when one opens
        pendingStatus_ = pendingStatus_.isEmpty() ? t : pendingStatus_ + " " + t;
    };
    if (juce::MessageManager::getInstance()->isThisTheMessageThread()) { say(text); return; }
    std::weak_ptr<bool> alive = alive_;   // the instance may be gone by the time it runs
    juce::MessageManager::callAsync([alive, say, text] { if (alive.lock()) say(text); });
}

juce::String FM1Processor::takePendingStatus() {
    std::lock_guard<std::mutex> g(statusLock_);
    juce::String t;
    t.swapWith(pendingStatus_);
    return t;
}

void FM1Processor::setFirmware(const juce::String& id) {
    const auto& choices = fm1::firmwareChoices();
    int index = 1;
    for (size_t i = 0; i < choices.size(); ++i) if (id == choices[i].id) index = int(i);
    if (index == firmwareIndex_.load()) return;
    firmwareIndex_ = index;
    pendingMismatch_.reset();
   #if FM1_FELUCCA
    {
        // Felucca (or SLOOP) plays from an engine taken now and given back when switching away (it
        // plays in one of the compiled copies, alone or sharing it: FeluccaEngine); its sound is
        // kept meanwhile (and saved), and comes back with the next engine of that firmware
        const juce::String id = choices[size_t(index)].id;
        const bool want = fm1::isFeluccaFamily(id.toStdString());
        const auto flavor = id == "sloop" ? FeluccaEngine::Flavor::Sloop : id == "melodee" ? FeluccaEngine::Flavor::Melodee : FeluccaEngine::Flavor::Felucca;
        const bool away = felucca_ && (!want || felucca_->flavor() != flavor);
        std::shared_ptr<FeluccaEngine> next;
        if (away) {
            auto kept = feluccaState(felucca_->flavor());
            std::lock_guard<std::mutex> g(felStateLock_);
            feluccaSaved_[flavorIndex(felucca_->flavor())] = kept;
            felOriginal_ = {};   // (the unread music kept with it is its own, not the next engine's)
            felUnread_ = {};
            felBase_.clear();
        }
        if (want && (!felucca_ || away)) {
            next = std::make_shared<FeluccaEngine>(flavor);
            if (!next->valid()) {
                next.reset();
                status(juce::String(choices[size_t(index)].name) + " could not start in this instance; it is silent.");   // (not expected: there is no limit)
            } else {
                {   // the device's projects, user presets (and their FM6 patches) and settings, from the library
                    juce::String msg;
                    {
                        std::lock_guard<std::mutex> g(felDeviceLock_);
                        msg = deviceFor(*next).load(*next);
                    }
                    if (msg.isNotEmpty()) status(msg);
                }
                juce::ValueTree saved;
                {
                    std::lock_guard<std::mutex> g(felStateLock_);
                    saved = feluccaSaved_[flavorIndex(flavor)];
                    feluccaSaved_[flavorIndex(flavor)] = {};
                }
                if (saved.isValid()) applyFeluccaState(*next, saved, false);   // before the audio thread sees it
            }
        }
        if (next || away) {
            suspendProcessing(true);
            std::atomic_store(&felucca_, next);
            if (felucca_) prepareFelucca(); else if (prepared_) prepareEngine();
            felResync_ = true;
            suspendProcessing(false);
            if (felucca_) feluccaChanged();
        }
    }
   #endif
    if (link.isOpen()) {
        auto synth = session.lastIdentity();
        if (synth && juce::String(fm1::firmwareIdFor(*synth)) != choices[size_t(index)].id) {
            // the connected synth runs another firmware: stop whatever runs and let it go
            session.cancel();
            link.close();
            if (live_) setLive(false);
            autoConnect_ = false;
            status("Disconnected: the FM-1 runs another firmware than " + juce::String(choices[size_t(index)].name) + ".");
        } else if (!synth) {
            session.identify();   // not identified yet: it is checked against the new choice
        }
    }
    if (juce::MessageManager::getInstance()->isThisTheMessageThread()) { if (onFirmwareChanged) onFirmwareChanged(); }
    else settingsNotify_ = true;
}

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
