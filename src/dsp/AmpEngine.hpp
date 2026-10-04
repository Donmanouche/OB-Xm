/*
 * OB-Xm — polyphonic OB-Xf amplifier envelope and VCA.
 *
 * Adapted from OB-Xf (https://github.com/surge-synthesizer/OB-Xf, commit b08ffb6):
 *   - src/engine/Voice.h        ProcessSample(): amp envelope (velocity, 32-sample delay,
 *                               applyMatrixAttack/Release every sample), NoteOn()/NoteOff(),
 *                               setEnvTimingOffset()
 *   - src/engine/SynthEngine.h  processAmpEnv*(): logsc(val, 4, 60000, 900) ms for attack and
 *                               decay, logsc(val, 8, 60000, 900) ms for release
 *   - src/engine/AdsrEnvelope.h (vendored unmodified)
 *
 * OB-Xd was originally written by Vadim Filatov, and then a version
 * was released under the GPL3 at https://github.com/reales/OB-Xd.
 * Subsequently, the product was continued by DiscoDSP and the copyright
 * holders as an excellent closed source product.
 *
 * OB-Xf is a successor to OB-Xd version 2.11.
 * Copyright 2013-2025 by the authors as indicated in the original release,
 * and subsequent authors as per GitHub transaction log.
 * Adaptation copyright (C) 2026 OB-Xm contributors.
 *
 * Released under the GNU General Public Licence v3 or later (GPL-3.0-or-later).
 */
#pragma once

#include "Common.hpp"

namespace obxfvcv
{

struct AmpSettings
{
    float attack{0.f}, decay{0.f}, sustain{1.f}, release{0.f}; // 0..1 (OB-Xf AmpEnv*)
    float attackCurve{0.f};                                    // AmpEnvAttackCurve
    float velocity{0.f};                                       // VelToAmpEnv
};

struct AmpModulation
{
    float in{0.f};       // audio, volts
    bool gate{false};
    float velocity{1.f}; // 0..1, sampled on the gate's rising edge
    float attack{0.f}, decay{0.f}, sustain{0.f}, release{0.f}; // added to the normalized knobs
};

struct EngineTestAccess; // defined in tests/ only

class AmpEngine
{
    friend struct EngineTestAccess;

  public:
    static float attackMs(float v) { return obxf::logsc(v, 4.f, 60000.f, 900.f); }
    static float decayMs(float v) { return obxf::logsc(v, 4.f, 60000.f, 900.f); }
    static float releaseMs(float v) { return obxf::logsc(v, 8.f, 60000.f, 900.f); }

    AmpEngine()
    {
        Rng rng(0x27D4EB2Fu ^ static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this)));
        for (auto &ch : chans)
            ch.slopEnv = rng.bipolarHalf(); // Voice.h: slop.ampEnv = nextFloat() - 0.5f
    }

    void setSampleRate(float sr)
    {
        for (auto &ch : chans)
        {
            ch.env.setSampleRate(sr);
            // Voice::setEnvTimingOffset(EnvelopeSlop = 25 %)
            ch.env.setEnvOffsets(1.f + ch.slopEnv * SLOP_ENVELOPE);
            ch.attackN = ch.decayN = ch.sustainN = ch.releaseN = -1.f; // recompute
            ch.curve = -1.f;
        }
    }

    void setSettings(const AmpSettings &s) { set = s; }

    // Returns the amplified input in volts; envOut receives the applied envelope (0..1).
    float process(int c, const AmpModulation &m, float &envOut)
    {
        Channel &ch = chans[c];
        obxf::ADSREnvelope &env = ch.env;

        // Knob + CV, applied only when a value changes (OB-Xf's curves)
        const float aN = clamp01(set.attack + m.attack);
        const float dN = clamp01(set.decay + m.decay);
        const float sN = clamp01(set.sustain + m.sustain);
        const float rN = clamp01(set.release + m.release);
        if (aN != ch.attackN)
        {
            env.setAttack(attackMs(aN));
            ch.attackN = aN;
            ch.attackMs = attackMs(aN);
        }
        if (dN != ch.decayN)
        {
            env.setDecay(decayMs(dN));
            ch.decayN = dN;
        }
        if (sN != ch.sustainN)
        {
            env.setSustain(sN);
            ch.sustainN = sN;
        }
        if (rN != ch.releaseN)
        {
            env.setRelease(releaseMs(rN));
            ch.releaseN = rN;
            ch.releaseMs = releaseMs(rN);
        }
        if (set.attackCurve != ch.curve)
        {
            env.setAttackCurve(set.attackCurve);
            ch.curve = set.attackCurve;
        }

        // Voice::NoteOn / NoteOff driven by the gate input
        if (m.gate && !ch.gate)
        {
            if (!env.isActive())
            {
                // "When your processing is paused we need to clear delay lines and envelopes"
                ch.delayed.fillZeroes();
                env.ResetEnvelopeState();
            }
            ch.velocity = m.velocity;
            env.triggerAttack();
        }
        else if (!m.gate && ch.gate)
        {
            env.triggerRelease();
        }
        ch.gate = m.gate;

        // Voice.h: applyMatrixAttack / applyMatrixRelease before every sample (the latter
        // re-derives the release coefficient from the current level while releasing)
        env.applyMatrixAttack(ch.attackMs);
        env.applyMatrixRelease(ch.releaseMs);

        // Voice.h: ampEnvDelayed.feedReturn(ampEnv.processSample() * (1 - (1 - velocity) * velToAmp))
        const float a = ch.delayed.feedReturn(env.processSample() *
                                              (1 - (1 - ch.velocity) * set.velocity));
        envOut = a;
        return m.in * a;
    }

  private:
    struct Channel
    {
        obxf::ADSREnvelope env;
        // Voice.h: DelayLine<B_SAMPLES * OVERSAMPLE_FACTOR, float> ampEnvDelayed
        obxf::DelayLine<obxf::B_SAMPLES * obxf::OVERSAMPLE_FACTOR, float> delayed;
        float slopEnv{0.f};
        float velocity{1.f};
        bool gate{false};
        float attackN{-1.f}, decayN{-1.f}, sustainN{-1.f}, releaseN{-1.f}, curve{-1.f};
        float attackMs{4.f}, releaseMs{8.f};
    };

    Channel chans[MAX_CHANNELS];
    AmpSettings set;
};

} // namespace obxfvcv
