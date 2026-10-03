// Test-only stub of ODDSound MTS-ESP: never a master, so Tuning stays 12-TET.
#pragma once
struct MTSClient;
inline MTSClient *MTS_RegisterClient() { return nullptr; }
inline void MTS_DeregisterClient(MTSClient *) {}
inline bool MTS_HasMaster(MTSClient *) { return false; }
inline double MTS_RetuningInSemitones(MTSClient *, char, char) { return 0.0; }
inline const char *MTS_GetScaleName(MTSClient *) { return ""; }
