// Stub replacing the MTS-ESP client used by Dexed's msfa fork.
// This project does not support MTS-ESP; every call reports "no master".
#pragma once
struct MTSClient;
inline bool MTS_HasMaster(MTSClient*) { return false; }
inline double MTS_NoteToFrequency(MTSClient*, char, char) { return 0.0; }
