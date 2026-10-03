/*
 * OB-Xm — compatibility shim replacing the JUCE / OB-Xf headers that the
 * vendored OB-Xf engine files include ("SynthEngine.h", "Voice.h", <Utils.h>).
 *
 * The values below are copied verbatim from OB-Xf
 * (https://github.com/surge-synthesizer/OB-Xf, commit b08ffb6):
 *   - dc, ln2, mult, pi       : src/core/Constants.h
 *   - NUM_XPANDER_MODES,
 *     OVERSAMPLE_FACTOR       : src/configuration.h
 *   - getPitch()              : src/Utils.h
 *   - juce::jlimit()          : minimal stand-in for JUCE's jlimit (same semantics)
 *
 * This file is GPL-3.0-or-later, like the rest of OB-Xf.
 */
#ifndef OBXF_VCV_SHIM_H
#define OBXF_VCV_SHIM_H

#include <cmath>
#include <cstdint>

#ifdef OBXF_VCV_REFERENCE_TEST
// tests/ compiles the real OB-Xf Voice.h, which brings the real configuration.h
#include "configuration.h"
#else
constexpr uint8_t NUM_XPANDER_MODES{15};
constexpr uint8_t OVERSAMPLE_FACTOR{2};
#endif

constexpr float dc = 1e-18f;
constexpr float ln2 = 0.69314718056f;
constexpr float mult = ln2 / 12.f;
constexpr float pi = 3.14159265358979323846f;

namespace juce
{
template <typename T> constexpr T jlimit(T lower, T upper, T value)
{
    return value < lower ? lower : (upper < value ? upper : value);
}
} // namespace juce

inline static float getPitch(float index) { return 440.f * std::exp(mult * index); }

#endif // OBXF_VCV_SHIM_H
