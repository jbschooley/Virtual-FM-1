// Fm1Edit -- MIDI that changes the synth's edit buffer without storing it.
//
// Verified on FM-1_093 by reading the edit buffer and live settings record back
// over the memory-read command (tests/fm1_probe.cpp):
//   - DX7 voice parameter change, F0 43 10 0g pp dd F7, sets VCED byte 128g+pp
//     of the edit buffer; stored presets are untouched
//   - effect CCs 0-23 on the FX channel set the live record's effect bytes
//     (on, type, parameters) as the FM-1+VA manual lists them
//   - envelope CCs 73/75/70/72 on the MIDI channel set A/D/S/R, 0-127 -> 0-100
// Not reachable this way: distortion type, effect order, envelope off, and the
// Virtual Analog settings beyond the manual's CCs.

#pragma once

#include <vector>

#include "Fm1Codec.h"
#include "Fm1Record.h"

namespace fm1::edit {

Bytes paramChange(int param, int value);                 // param 0..155
Bytes controlChange(int channel1to16, int cc, int value);

struct Channels { int fx = 2; int midi = 1; };

// Every message that makes the synth's edit buffer play `s`.
std::vector<Bytes> fullSound(const Sound& s, Channels ch);
// Only the messages for what differs between `from` and `to`.
std::vector<Bytes> delta(const Sound& from, const Sound& to, Channels ch);

// Which live-record bytes the messages above can set (for verifying a read-back).
bool recordByteSettable(int index, const Record& target);

int envToCc(int v);   // 0..100 -> the CC value that the synth maps back to v

}  // namespace fm1::edit
