/*
 * OB-Xm — polyphonic OB-Xf filter with its filter envelope.
 *
 * Adapted from OB-Xf (https://github.com/surge-synthesizer/OB-Xf, commit b08ffb6):
 *   - src/engine/Voice.h        Voice::ProcessSample() (cutoff computation, envelope,
 *                               velocity, keytrack), NoteOn()/NoteOff()
 *   - src/engine/SynthEngine.h  processSample() smoothing, process*() parameter scalings
 *   - src/engine/VoiceMatrix.h  resonance curve (matrixTargetScaling)
 *   - src/engine/Motherboard.h  2x oversampling + Decimator17
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

struct FilterSettings
{
    float cutoff{1.f};    // 0..1 (OB-Xf FilterCutoff, normalized)
    float resonance{0.f}; // 0..1
    float mode{0.f};      // 0..1 (FilterMode; also selects the Xpander mode)
    float envAmount{0.f}; // 0..1
    float keytrack{0.f};  // 0..1
    bool fourPole{false};
    bool xpander{false};
    bool push2Pole{false};
    bool bpBlend2Pole{false};
    bool invertEnv{false};

    float attack{0.f}, decay{0.f}, sustain{1.f}, release{0.f}; // 0..1
    float attackCurve{0.f};
    float velocity{0.f}; // VelToFilterEnv
};

struct FilterModulation
{
    float in{0.f};       // audio, volts
    bool gate{false};
    float velocity{1.f}; // 0..1, sampled on the gate's rising edge
    float voct{0.f};     // keytrack pitch, 0 V = C4
    float cutoff{0.f};   // semitones added to the cutoff
    float resonance{0.f}; // added to the normalized resonance
    float envAmount{0.f}; // added to the normalized env amount
    float mode{0.f};      // added to the normalized mode
    float attack{0.f};    // added to the normalized envelope attack
    float decay{0.f};     // added to the normalized envelope decay
};

struct EngineTestAccess; // defined in tests/ only

class FilterEngine
{
    friend struct EngineTestAccess;

  public:
    static int xpanderModeFor(float mode)
    {
        // SynthEngine::processFilterXpanderMode: roundToInt(val * (NUM_XPANDER_MODES - 1))
        return static_cast<int>(std::lround(clamp01(mode) * (obxf::NUM_XPANDER_MODES - 1)));
    }

    // VoiceMatrix.h matrixTargetScaling(FilterResonance)
    static float resonanceCurve(float v) { return 0.991f - obxf::logsc(1.f - v, 0.f, 0.991f, 40.f); }

    FilterEngine()
    {
        for (int c = 0; c < MAX_CHANNELS; c++)
        {
            Channel &ch = chans[c];
            // Voice.h constructor
            ch.slopEnv = rng.bipolarHalf();
            ch.slopCutoff = rng.bipolarHalf();
            ch.slopPortamento = rng.bipolarHalf();
        }
    }

    void setSampleRate(float sr)
    {
        hostRate = sr;
        // SynthEngine::setSampleRate: smoothers run at the host rate
        cutoffSmoother.setSampleRate(sr);
        resSmoother.setSampleRate(sr);
        modeSmoother.setSampleRate(sr);
        updateRate();
    }

    void setHQ(bool hq)
    {
        if (hq == oversample)
            return;
        oversample = hq;
        updateRate();
    }

    bool isHQ() const { return oversample; }

    void setSettings(const FilterSettings &s)
    {
        using namespace obxf;

        // SynthEngine::processFilterCutoff / Resonance / Mode feed the smoothers
        cutoffSmoother.setStep(linsc(s.cutoff, 0.f, 120.f));
        resSmoother.setStep(s.resonance);
        modeSmoother.setStep(s.mode);
        modeRaw = s.mode;

        envAmount = s.envAmount;
        keytrack = s.keytrack;
        fourPole = s.fourPole;
        xpander = s.xpander;
        push2Pole = s.push2Pole;
        bpBlend2Pole = s.bpBlend2Pole;
        invertEnvScale = s.invertEnv ? -1.f : 1.f;
        velToFilter = s.velocity;

        // Attack and decay take per-channel CV, so they are applied in process()
        attackNorm = s.attack;
        decayNorm = s.decay;

        // processFilterEnvRelease: logsc(val, 1, 60000, 900) ms
        const float r = logsc(s.release, 1.f, 60000.f, 900.f);
        if (s.sustain != envS || r != envR || s.attackCurve != envCurve)
        {
            envS = s.sustain;
            envR = r;
            envCurve = s.attackCurve;
            envVersion++;
        }
    }

    // Advance the shared smoothers. Call once per host sample, before process().
    void beginSample()
    {
        cutoffSmoothed = cutoffSmoother.smoothStep();
        resSmoothed = resSmoother.smoothStep();
        modeSmoothed = modeSmoother.smoothStep();
    }

    // Xpander mode index for a channel's mode CV (unsmoothed, like OB-Xf's discrete parameter)
    int xpanderModeIndex(float modeMod) const { return xpanderModeFor(modeRaw + modeMod); }

    // Returns the filtered output in volts; envOut receives the envelope (1.0 = 10 V).
    float process(int c, const FilterModulation &m, float &envOut)
    {
        using namespace obxf;
        Channel &ch = chans[c];

        if (ch.envVersion != envVersion)
        {
            ch.env.setSustain(envS);
            ch.env.setRelease(envR);
            ch.env.setAttackCurve(envCurve);
            ch.envVersion = envVersion;
        }

        /* Attack and decay: knob + CV, per channel. A new time is applied to the running
         * stage, as OB-Xf's modulation matrix does for the filter envelope attack
         * (Voice.h: filterEnv.applyMatrixAttack every sample). Only recomputed when the
         * value changes: SynthEngine's processFilterEnvAttack/Decay curve,
         * logsc(val, 1, 60000, 900) ms. */
        {
            const float aN = clamp01(attackNorm + m.attack);
            const float dN = clamp01(decayNorm + m.decay);
            if (aN != ch.attackNorm)
            {
                ch.env.setAttack(logsc(aN, 1.f, 60000.f, 900.f));
                ch.attackNorm = aN;
            }
            if (dN != ch.decayNorm)
            {
                ch.env.setDecay(logsc(dN, 1.f, 60000.f, 900.f));
                ch.decayNorm = dN;
            }
        }

        // Voice::NoteOn / NoteOff driven by the gate input
        if (m.gate && !ch.gate)
        {
            if (!ch.env.isActive())
            {
                ch.envDelayed.fillZeroes();
                ch.env.ResetEnvelopeState();
            }
            ch.velocity = m.velocity;
            ch.env.triggerAttack();
        }
        else if (!m.gate && ch.gate)
        {
            ch.env.triggerRelease();
        }
        ch.gate = m.gate;

        // SynthEngine::processSample: per-voice resonance and multimode, once per host sample
        ch.filter.setResonance(resonanceCurve(clamp01(resSmoothed + m.resonance)));
        const float mode = clamp01(modeSmoothed + m.mode);
        ch.filter.setMultimode(mode);
        ch.filter.par.xpanderMode = static_cast<uint8_t>(xpanderModeIndex(m.mode));
        ch.filter.par.xpander4Pole = xpander;
        ch.filter.par.bpBlend2Pole = bpBlend2Pole;
        ch.filter.par.push2Pole = push2Pole;

        const float envAmt = linsc(clamp01(envAmount + m.envAmount), 0.f, 140.f); // processFilterEnvAmount
        const float cutoffBase = cutoffSmoothed + m.cutoff;
        const float noteTarget = 60.f + 12.f * m.voct - 93.f; // Voice.h: tunedNote - 93
        const float x = m.in * VOLT_SCALE_INV;

        float modEnv = 0.f;
        auto renderOnce = [&](float input) -> float {

            /* Voice.h calls filterEnv.applyMatrixRelease() before every sample. While the
             * envelope is releasing this re-derives the release coefficient from the current
             * level, which shapes OB-Xf's release curve, so it is reproduced here.
             * (applyMatrixAttack() is not needed: it only rewrites the same attack time.) */
            ch.env.applyMatrixRelease(envR);

            // Voice.h: filter envelope
            modEnv = invertEnvScale * ch.env.processSample() * (1 - (1 - ch.velocity) * velToFilter);

            // Voice.h: portamento-smoothed note, used here for keytracking only
            const float notePlaying = tpt_lp_unwarped(
                ch.portamento, noteTarget,
                PORTAMENTO_DEFAULT * (1 + ch.slopPortamento * SLOP_PORTAMENTO), rateInv);

            // Voice.h: with juce::Random this was swinging ~[-1.75, 1.75] ... factor of 3.365
            const float noisyCutoff = ch.noise.getWhite() * 3.365f;

            // Voice.h: filter cutoff calculation (LFO terms omitted: no LFO here)
            const float cutoffPitch =
                getPitch(cutoffBase + ch.slopCutoff * SLOP_CUTOFF +
                         envAmt * ch.envDelayed.feedReturn(modEnv) - 45 +
                         (keytrack * (notePlaying + 40)));

            // limit max cutoff for numerical stability
            float cutoffcalc = std::min(cutoffPitch + noisyCutoff, (rate * 0.5f - 120.0f));

            // limit our max cutoff on self-oscillation to prevent aliasing
            if (push2Pole)
                cutoffcalc = std::min(cutoffcalc, 19000.f + (5000.f * oversample));

            return fourPole ? ch.filter.apply4Pole(input, cutoffcalc)
                            : ch.filter.apply2Pole(input, cutoffcalc);
        };

        float out;
        if (oversample)
        {
            /* OB-Xf oversamples the whole voice (oscillators included). Here the audio
             * arrives at the host rate, so it is upsampled by linear interpolation. */
            const float y1 = renderOnce(0.5f * (ch.lastInput + x));
            const float y2 = renderOnce(x);
            out = ch.decimator.decimate(y1, y2);
        }
        else
        {
            out = renderOnce(x);
        }
        ch.lastInput = x;

        envOut = modEnv;
        return out * VOLT_SCALE;
    }

    bool isActive(int c) { return chans[c].env.isActive(); }

  private:
    struct Channel
    {
        obxf::Filter filter;
        obxf::ADSREnvelope env;
        obxf::Noise noise;
        // Voice.h: DelayLine<B_SAMPLES * OVERSAMPLE_FACTOR, float> filterEnvDelayed
        obxf::DelayLine<obxf::B_SAMPLES * obxf::OVERSAMPLE_FACTOR, float> envDelayed;
        obxf::Decimator17 decimator;

        float portamento{0.f};
        float slopEnv{0.f}, slopCutoff{0.f}, slopPortamento{0.f};
        float velocity{1.f};
        float lastInput{0.f};
        bool gate{false};
        int envVersion{-1};
        float attackNorm{-1.f}, decayNorm{-1.f}; // last applied (normalized), -1 = force
    };

    Channel chans[MAX_CHANNELS];
    obxf::Smoother cutoffSmoother, resSmoother, modeSmoother;
    float cutoffSmoothed{0.f}, resSmoothed{0.f}, modeSmoothed{0.f};
    float modeRaw{0.f};

    float hostRate{48000.f};
    float rate{48000.f};
    float rateInv{1.f / 48000.f};
    bool oversample{false};

    float envAmount{0.f}, keytrack{0.f}, velToFilter{0.f}, invertEnvScale{1.f};
    bool fourPole{false}, xpander{false}, push2Pole{false}, bpBlend2Pole{false};

    float attackNorm{0.f}, decayNorm{0.f};
    float envS{1.f}, envR{1.f}, envCurve{0.f};
    int envVersion{0};

    // Own RNG rather than std::rand(), which takes a lock in glibc
    Rng rng{0x85EBCA6Bu ^ static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this))};

    void updateRate()
    {
        rate = hostRate * (oversample ? 2.f : 1.f);
        rateInv = 1.f / rate;

        for (int c = 0; c < MAX_CHANNELS; c++)
        {
            Channel &ch = chans[c];
            // Voice::setSampleRate / setHQMode
            ch.filter.setSampleRate(rate);
            ch.filter.reset();
            ch.env.setSampleRate(rate);
            ch.noise.setSampleRate(rate);
            ch.noise.seedWhiteNoise(static_cast<int32_t>(rng.next())); // Voice.h: std::rand()
            // Voice::setEnvTimingOffset(EnvelopeSlop)
            ch.env.setEnvOffsets(1.f + ch.slopEnv * SLOP_ENVELOPE);
            ch.envVersion = -1; // recompute the envelope coefficients for the new rate
            ch.attackNorm = ch.decayNorm = -1.f;
            ch.decimator.resetDecimator();
        }
    }
};

} // namespace obxfvcv
