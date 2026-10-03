/* Test-only stand-in for OB-Xf src/engine/VoiceMatrix.h: the modulation matrix is
 * empty in these tests, so every adjustment is inactive (as with no matrix rows). */
#pragma once
#include <array>
#include <cstddef>
#include "ParamScales.h"

enum class MatrixSource { Strike, Lift, Press, Slide, Glide, None };
enum class MatrixTarget
{
    None, FilterCutoff, FilterResonance, Osc1Pitch, Osc2Pitch, Osc2Detune, Osc2PWOffset,
    Osc1Vol, Osc2Vol, NoiseVol, RingModVol, OscPitch, UnisonDetune, OscPW, OscCrossmod,
    LFO1ModAmount1, LFO1ModAmount2, LFO2Rate, LFO2ModAmount1, LFO2ModAmount2,
    FilterEnvAttack, FilterEnvRelease, AmpEnvAttack, AmpEnvRelease, Count
};
inline constexpr size_t MatrixTargetCount{static_cast<size_t>(MatrixTarget::Count)};

struct MatrixTargetScaling
{
    float nativeMin{0.f};
    float nativeMax{1.f};
    constexpr float span() const { return nativeMax - nativeMin; }
};
// Only the two entries Voice.h reads, copied from the real matrixTargetScaling()
inline constexpr MatrixTargetScaling matrixTargetScaling(MatrixTarget t)
{
    return t == MatrixTarget::FilterCutoff ? MatrixTargetScaling{0.f, 120.f}
                                           : MatrixTargetScaling{0.f, 48.f};
}

struct VoiceMatrixAdjustments
{
    void clear() {}
    float modFor(MatrixTarget) const { return 0.f; }
    float nativeOr(MatrixTarget, float unmodulated) const { return unmodulated; }
};
struct VoiceMatrixSourceValues
{
    void clear() {}
};
inline void setMatrixSource(VoiceMatrixSourceValues &, MatrixSource, float) {}
struct VoiceMatrix
{
};
