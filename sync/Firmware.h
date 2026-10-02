// Firmware -- what a synth's firmware lets the plugin do, and how.
//
// Each firmware the plugin knows has a profile: its features (can it send
// presets back, take patterns, report its current sound ...) and the protocol
// for each. Fm1Session does the threading, progress, pacing and checking, and
// asks the profile for everything that depends on the firmware. A new firmware,
// or one that speaks a different protocol, is a new profile here; nothing else
// changes. See docs/FIRMWARE-PROFILES.md.
//
// Profiles today:
//   FM-1+VA (baud girl)  read-back protocol: presets, patterns, memory reads
//   M-VAVE (stock)       takes DX7 voices and parameter changes, sends nothing back
//   Felucca              identity only; no preset or pattern transfer

#pragma once

#include <functional>
#include <memory>
#include <optional>

#include <juce_core/juce_core.h>

#include "Fm1Codec.h"
#include "Fm1Edit.h"
#include "Fm1Link.h"
#include "Fm1Seq.h"

namespace fm1 {

// The synth-wide settings the plugin uses (the FM-1's GLOBE screen).
struct Globals {
    int midiChannel = 0;           // 0 = All, else 1..16
    int fxChannel = 2;             // 1..16
    int bendUp = 12, bendDown = 12;   // semitones
    int keyVelocity = 90;          // Keyboard > Velocity
    int glideTime = 0;             // 0..100
    bool glideFingered = false;    // Glide mode: Full Time or Fingered
    bool driveMinus6 = false;      // Drive: 0 or -6
    bool cc7Volume = true;         // MIDI > Ext Ctrl CC7 Vol
    bool overdubRec = false;       // Overdub Rec
};

// What a profile's operations talk through: the MIDI link, and the session's
// cancel flag and progress report for the long ones.
struct Port {
    explicit Port(Fm1Link& l) : link(l) {}
    Fm1Link& link;
    std::function<bool()> cancelled = [] { return false; };
    std::function<void(int done, int total, const juce::String& text)> progress = [](int, int, const juce::String&) {};
};

class Firmware {
public:
    enum class Feature { ReadPresets, WritePresets, ReadPatterns, WritePatterns, ReadCurrent, CheckEdit, ReadGlobals };

    virtual ~Firmware() = default;

    int version = 0;                                 // the identity's version number
    virtual juce::String name() const = 0;           // "FM-1+VA", "M-VAVE", "Felucca"
    virtual juce::String summary() const = 0;        // a few words on what it can do, for the identity line
    virtual bool has(Feature f) const = 0;
    // Why a feature is missing, as a sentence for the status line.
    virtual juce::String cannot(Feature f) const;
    virtual int writePaceMs() const { return 0; }    // between preset writes

    // Called only when has() says the feature is there. On failure each
    // returns nothing (or false) and puts the reason in `error`.
    virtual std::optional<Sound> readPreset(Port&, int slot, juce::String& error);
    virtual bool writePreset(Port&, const Sound&, juce::String& error);   // the session reads it back to check
    virtual std::optional<seq::Pattern> readPattern(Port&, int pattern, juce::String& error);
    virtual bool writePattern(Port&, const seq::Pattern&, int pattern, bool save, juce::String& error);
    struct Current { Sound live, stored; };          // the sound playing now, and its preset as saved
    virtual std::optional<Current> readCurrent(Port&, juce::String& error);
    struct Live { Edit voice{}; Record record{}; };  // the edit buffer, to check a sent edit
    virtual std::optional<Live> readLive(Port&, juce::String& error);
    virtual std::optional<Globals> readGlobals(Port&, juce::String& error);

    // The messages that make the synth's edit buffer play `s`, unsaved.
    virtual std::vector<Bytes> editMessages(const Sound& s, edit::Channels ch) const { return edit::fullSound(s, ch); }
};

// The profile for an identified synth (never null).
std::unique_ptr<Firmware> firmwareFor(const Identity& id);

}  // namespace fm1
