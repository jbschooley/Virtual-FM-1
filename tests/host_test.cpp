// Loads the built plugin the way a DAW does (AU and VST3), opens and closes its
// editor, and unloads it, so editor and processor teardown run as in a host.
//   host_test <path to .component or .vst3>
#include <cstdio>
#include <cstring>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) return 2;
    juce::AudioPluginFormatManager fm;
    juce::addDefaultFormatsToManager(fm);
    juce::OwnedArray<juce::PluginDescription> found;
    for (auto* f : fm.getFormats())
        if (f->fileMightContainThisPluginType(argv[1])) f->findAllTypesForFile(found, argv[1]);
    if (found.isEmpty()) { std::printf("no plugin found in %s\n", argv[1]); return 1; }
    int rounds = argc > 2 ? std::atoi(argv[2]) : 3;
    for (int round = 0; round < rounds; ++round) {
        juce::String err;
        auto inst = fm.createPluginInstance(*found[0], 44100.0, 512, err);
        if (!inst) { std::printf("could not load: %s\n", err.toRawUTF8()); return 1; }
        inst->prepareToPlay(44100.0, 512);
        juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
        inst->processBlock(buf, midi);
        if (argc > 3) {   // send a DX7 single voice named argv[3] into the current slot
            juce::uint8 e[155] = {};
            for (int op = 0; op < 6; ++op) { juce::uint8* o = e + op * 21; for (int i = 0; i < 4; ++i) { o[i] = 99; o[4 + i] = i < 3 ? 99 : 0; } o[18] = 1; o[20] = 7; o[16] = op == 5 ? 99 : 0; }
            juce::uint8 g[19] = {99, 99, 99, 99, 50, 50, 50, 50, 0, 0, 1, 35, 0, 0, 0, 1, 0, 3, 24};
            std::memcpy(e + 126, g, 19);
            juce::String nm = juce::String(argv[3]).paddedRight(' ', 10).substring(0, 10);
            for (int i = 0; i < 10; ++i) e[145 + i] = juce::uint8(nm[i]);
            int sum = 0; for (auto b : e) sum += b;
            juce::MemoryBlock sx; juce::uint8 hdr[5] = {0x43, 0x00, 0x00, 0x01, 0x1B};
            sx.append(hdr, 5); sx.append(e, 155); juce::uint8 cs = juce::uint8((-sum) & 0x7F); sx.append(&cs, 1);
            juce::MidiBuffer m2; m2.addEvent(juce::MidiMessage::createSysExMessage(sx.getData(), int(sx.getSize())), 0);
            inst->processBlock(buf, m2);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(2500);
            std::printf("  sent a DX7 voice named %s\n", argv[3]);
        }
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed(inst->createEditorAndMakeActive());
            juce::DocumentWindow w("host_test", juce::Colours::black, 0);
            w.setContentNonOwned(ed.get(), true);
            w.setVisible(true);
            for (int tab = 0; tab < 5; ++tab) {
                if (auto* tabs = ed->findChildWithID("tabs")) juce::ignoreUnused(tabs);
                juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
            }
            w.clearContentComponent();
            inst->editorBeingDeleted(ed.get());
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil(2500);   // background connect
        std::printf("  programs %d: %s | %s | %s | %s\n", inst->getNumPrograms(), inst->getProgramName(0).toRawUTF8(), inst->getProgramName(1).toRawUTF8(),
                    inst->getProgramName(45).toRawUTF8(), inst->getProgramName(127).toRawUTF8());
        juce::MemoryBlock state; inst->getStateInformation(state);

        inst->setStateInformation(state.getData(), int(state.getSize()));
        inst->releaseResources();
        inst.reset();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(200);
        std::printf("round %d: loaded %s, editor opened and closed, state %d bytes, unloaded\n", round + 1, found[0]->pluginFormatName.toRawUTF8(), int(state.getSize()));
    }
    return 0;
}
