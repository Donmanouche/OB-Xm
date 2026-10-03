/*
 * OB-Xm — shared constants for the oscillator and filter engines.
 *
 * Copyright (C) 2026 OB-Xm contributors.
 * Released under the GNU General Public Licence v3 or later (GPL-3.0-or-later).
 * Based on OB-Xf, https://github.com/surge-synthesizer/OB-Xf
 */
#pragma once

#include "ObxfDsp.hpp"

namespace obxfvcv
{

/* OB-Xf's OscillatorBlock returns about +-1.5 for one full-scale saw
 * ((phase - 0.5) * 3, OscillatorBlock.h). The filter's non-linearities depend on
 * that level, so the oscillator scales it to +-5 V and the filter scales it back. */
constexpr float VOLT_SCALE = 10.f / 3.f;
constexpr float VOLT_SCALE_INV = 3.f / 10.f;

#ifdef METAMODULE
// MetaModule SDK v2.3: rack::engine::PORT_MAX_CHANNELS == 4.
constexpr int MAX_CHANNELS = 4;
constexpr int MAX_UNISON = 4;
// CPU budget: channels * unison voices never exceeds this on MetaModule.
constexpr int MAX_OSC_BLOCKS = 8;
#else
constexpr int MAX_CHANNELS = 16;
constexpr int MAX_UNISON = 8; // OB-Xf configuration.h: MAX_UNISON
constexpr int MAX_OSC_BLOCKS = MAX_CHANNELS * MAX_UNISON;
#endif

/* OB-Xf "Voice Variation" defaults (ParameterList.h: PortamentoSlop, FilterSlop,
 * EnvelopeSlop, LevelSlop all default to 0.25), scaled as in SynthEngine.h. */
constexpr float SLOP_PORTAMENTO = 0.25f * 0.75f; // processPortamentoSlop: linsc(v, 0, 0.75)
constexpr float SLOP_CUTOFF = 0.25f * 18.f;      // processFilterSlop:     linsc(v, 0, 18)
constexpr float SLOP_ENVELOPE = 0.25f;           // processEnvelopeSlop:   v
constexpr float SLOP_LEVEL = 0.25f * 0.67f;      // processLevelSlop:      linsc(v, 0, 0.67)

/* OB-Xf Portamento parameter at its default (0): processPortamento() gives
 * logsc(1 - 0, 0.14, 250, 150) = 250. Voice.h always runs the note through this
 * one-pole "RC" smoother, so a glide of 0 is still a ~0.6 ms slew. */
constexpr float PORTAMENTO_DEFAULT = 250.f;

// Small deterministic RNG for the per-voice "slop" offsets (no allocation, no locks).
struct Rng
{
    uint32_t state;
    explicit Rng(uint32_t seed = 0x1234567u) : state(seed ? seed : 1u) {}
    uint32_t next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    // uniform in [-0.5, 0.5), like juce::Random::nextFloat() - 0.5f in Voice.h
    float bipolarHalf() { return (next() >> 8) * (1.f / 16777216.f) - 0.5f; }
};

inline float clamp01(float x) { return x < 0.f ? 0.f : (x > 1.f ? 1.f : x); }

} // namespace obxfvcv
