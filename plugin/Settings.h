// PluginSettings -- how the plugin plays, separate from the FM-1's own GLOBE
// settings. An instance's settings take effect at once and are saved with the
// host project; "Save as default" writes them to a shared file that new
// instances start from (see FM1Processor::saveSettingsAsDefault).

#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

struct PluginSettings {
    int bendUp = 12, bendDown = 12;   // semitones, 0..24; 12 is the FM-1's default
    int midiChannel = 0;              // 0 = every channel, else 1..16: the host MIDI the plugin plays
    bool fixedVelocity = false;       // on-screen keyboard: one velocity, or by where a key is clicked
    int velocity = 100;               // 1..127, when fixed

    bool operator==(const PluginSettings&) const = default;

    PluginSettings clamped() const {
        PluginSettings s = *this;
        s.bendUp = juce::jlimit(0, 24, s.bendUp);
        s.bendDown = juce::jlimit(0, 24, s.bendDown);
        s.midiChannel = juce::jlimit(0, 16, s.midiChannel);
        s.velocity = juce::jlimit(1, 127, s.velocity);
        return s;
    }

    // In a project (ValueTree) and in the shared defaults file (JSON).
    juce::ValueTree toTree() const {
        juce::ValueTree t("Settings");
        t.setProperty("bendUp", bendUp, nullptr);
        t.setProperty("bendDown", bendDown, nullptr);
        t.setProperty("midiChannel", midiChannel, nullptr);
        t.setProperty("fixedVelocity", fixedVelocity, nullptr);
        t.setProperty("velocity", velocity, nullptr);
        return t;
    }
    static PluginSettings fromTree(const juce::ValueTree& t) {
        PluginSettings s;
        s.bendUp = t.getProperty("bendUp", s.bendUp);
        s.bendDown = t.getProperty("bendDown", s.bendDown);
        s.midiChannel = t.getProperty("midiChannel", s.midiChannel);
        s.fixedVelocity = t.getProperty("fixedVelocity", s.fixedVelocity);
        s.velocity = t.getProperty("velocity", s.velocity);
        return s.clamped();
    }
    juce::var toJson() const {
        auto* o = new juce::DynamicObject();
        o->setProperty("bendUp", bendUp);
        o->setProperty("bendDown", bendDown);
        o->setProperty("midiChannel", midiChannel);
        o->setProperty("fixedVelocity", fixedVelocity);
        o->setProperty("velocity", velocity);
        return juce::var(o);
    }
    static PluginSettings fromJson(const juce::var& v) {
        PluginSettings s;
        if (!v.isObject()) return s;
        s.bendUp = v.getProperty("bendUp", s.bendUp);
        s.bendDown = v.getProperty("bendDown", s.bendDown);
        s.midiChannel = v.getProperty("midiChannel", s.midiChannel);
        s.fixedVelocity = v.getProperty("fixedVelocity", s.fixedVelocity);
        s.velocity = v.getProperty("velocity", s.velocity);
        return s.clamped();
    }
};
