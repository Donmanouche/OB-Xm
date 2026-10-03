// Test-only: drives OB-Xf's own Voice class (src/engine/Voice.h) the way
// SynthEngine/Motherboard do, for A/B comparison with the VCV engines.
#pragma once

struct RefPatch
{
    // oscillators (same units as obxfvcv::OscSettings)
    float osc1Pitch{0}, osc2Pitch{0}, detune{0}, pw{0}, osc2PWOffset{0};
    float envToPitch{0}, envToPW{0}, crossmod{0}, brightness{1};
    float osc1Vol{1}, osc2Vol{0}, ringModVol{0}, noiseVol{0};
    int noiseColor{0}, transpose{0};
    float tune{0}, unisonDetune{0.25f};
    bool saw1{true}, pulse1{false}, saw2{true}, pulse2{false}, sync{false}, osc2Keytrack{true};
    bool envPitchBothOscs{true}, envPitchInvert{false}, envPWBothOscs{true}, envPWInvert{false};
    // filter (same units as obxfvcv::FilterSettings)
    float cutoff{1}, resonance{0}, mode{0}, envAmount{0}, keytrack{0};
    bool fourPole{false}, xpander{false}, push2Pole{false}, bpBlend2Pole{false}, invertEnv{false};
    float attack{0}, decay{0}, sustain{1}, release{0}, attackCurve{0}, velocity{0};
};

class RefSynth
{
  public:
    // unison voices are seeded with std::srand(seed + u) before Voice::setSampleRate
    RefSynth(float sampleRate, int unison, bool hq, unsigned seed, const RefPatch &patch);
    ~RefSynth();
    void noteOn(int note, float velocity);
    void noteOff();
    float process(); // sum of the unison voices (Motherboard L+R with centre pan)

  private:
    struct Impl;
    Impl *impl;
};
