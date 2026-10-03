// Test-only: the handful of JUCE symbols OB-Xf's Voice.h / Lfo.h use.
#pragma once
#include <cstdint>
#include <cmath>
#include "obxf_shim.h" // provides juce::jlimit

namespace juce
{
template <typename T> struct MathConstants
{
    static constexpr T pi = static_cast<T>(3.141592653589793238L);
    static constexpr T twoPi = static_cast<T>(2 * 3.141592653589793238L);
    static constexpr T halfPi = static_cast<T>(3.141592653589793238L / 2);
};

template <typename T> constexpr T jmax(T a, T b) { return a < b ? b : a; }

/* Deterministic: nextFloat() is always 0.5, so every "slop" offset in Voice.h
 * (nextFloat() - 0.5f) is zero. The reference comparison zeroes ours the same way. */
class Random
{
  public:
    Random() = default;
    static Random &getSystemRandom()
    {
        static Random r;
        return r;
    }
    float nextFloat() { return 0.5f; }
};

namespace dsp
{
struct FastMathApproximations
{
    // Same Pade approximant as JUCE's FastMathApproximations::sin
    template <typename F> static F sin(F x) noexcept
    {
        auto x2 = x * x;
        auto num = -x * (-(F)11511339840 + x2 * ((F)1640635920 + x2 * (-(F)52785432 + x2 * (F)479249)));
        auto den = (F)11511339840 + x2 * ((F)277920720 + x2 * ((F)3177720 + x2 * (F)18361));
        return num / den;
    }
};
} // namespace dsp
} // namespace juce
