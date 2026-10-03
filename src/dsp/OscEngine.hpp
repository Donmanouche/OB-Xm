/*
 * OB-Xm — polyphonic OB-Xf oscillator section with unison.
 *
 * Adapted from OB-Xf (https://github.com/surge-synthesizer/OB-Xf, commit b08ffb6):
 *   - src/engine/Voice.h        Voice::ProcessSample() (pitch/PW modulation, brightness)
 *   - src/engine/SynthEngine.h  process*() parameter scalings
 *   - src/engine/Motherboard.h  unison voices, 2x oversampling + Decimator17
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

// Control values, in the units of the OB-Xf parameters (mostly normalized 0..1).
struct OscSettings
{
    float osc1Pitch{0.f}; // semitones, -24..24 (OB-Xf Osc1Pitch)
    float osc2Pitch{0.f}; // semitones, -24..24 (OB-Xf Osc2Pitch)
    float detune{0.f};    // 0..1 (Osc2Detune)
    float pw{0.f};        // 0..1 (OscPW)
    float osc2PWOffset{0.f};
    float envToPitch{0.f}; // 0..1 (EnvToPitchAmount)
    float envToPW{0.f};    // 0..1 (EnvToPWAmount)
    float crossmod{0.f};
    float brightness{1.f};

    float osc1Vol{1.f};
    float osc2Vol{0.f};
    float ringModVol{0.f};
    float noiseVol{0.f};
    int noiseColor{0}; // 0 white, 1 pink, 2 red

    int transpose{0};   // semitones, -24..24
    float tune{0.f};    // cents, -100..100
    int unisonVoices{1};
    float unisonDetune{0.25f};

    bool saw1{true}, pulse1{false}, saw2{true}, pulse2{false};
    bool sync{false};
    bool osc2Keytrack{true};
    bool envPitchBothOscs{true}, envPitchInvert{false};
    bool envPWBothOscs{true}, envPWInvert{false};
};

// Per-channel, per-sample inputs.
struct OscModulation
{
    float voct{0.f};        // V/Oct, 0 V = C4
    float envPitch{0.f};    // external envelope, 1.0 = full scale (10 V)
    float envPW{0.f};       // external envelope, 1.0 = full scale (10 V)
    float osc2PWOffset{0.f}; // added to the normalized Osc2 PW Offset

    /* Waveform selection by CV (not in OB-Xf, which only has the buttons): -1 keeps the
     * buttons, otherwise bit 0 = saw, bit 1 = pulse (0 = triangle, as with both off). */
    int wave1{-1};
    int wave2{-1};

    // Added to the normalized mixer levels (OB-Xf Osc1Vol / Osc2Vol / RingModVol)
    float osc1Vol{0.f};
    float osc2Vol{0.f};
    float ringModVol{0.f};
};

struct EngineTestAccess; // defined in tests/ only

class OscEngine
{
    friend struct EngineTestAccess;

  public:
    OscEngine()
    {
        Rng rng(0x9E3779B9u ^ static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this)));
        for (int c = 0; c < MAX_CHANNELS; c++)
        {
            for (int u = 0; u < MAX_UNISON; u++)
            {
                // Voice.h constructor: slop.* = juce::Random::nextFloat() - 0.5f
                voices[c][u].slopPortamento = rng.bipolarHalf();
                voices[c][u].slopLevel = rng.bipolarHalf();
            }
        }
    }

    void setSampleRate(float sr)
    {
        hostRate = sr;
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

    // Called at control rate. Mirrors SynthEngine::process*() scalings.
    void setSettings(const OscSettings &s)
    {
        using namespace obxf;

        OscillatorBlock::Parameters p;
        p.pitch.transpose = s.transpose;
        p.pitch.tune = s.tune * 0.01f;                     // processTune: +-1 semitone
        p.pitch.unisonDetune = logsc(s.unisonDetune, 0.001f, 1.f); // processUnisonDetune
        p.osc.pitch1 = s.osc1Pitch + 24.f;                 // processOsc1Pitch: val * 48
        p.osc.pitch2 = s.osc2Pitch + 24.f;                 // processOsc2Pitch: val * 48
        p.osc.detune = logsc(s.detune, 0.001f, 0.6f);      // processOsc2Detune
        p.osc.pw = linsc(s.pw, 0.f, 0.95f);                // processOscPW
        p.osc.saw1 = s.saw1;
        p.osc.saw2 = s.saw2;
        p.osc.pulse1 = s.pulse1;
        p.osc.pulse2 = s.pulse2;
        p.osc.crossmod = s.crossmod * 48.f;                // processCrossmod
        p.osc.sync = s.sync;
        p.osc.keytrack2 = s.osc2Keytrack;
        p.mod.envToPitchInvert = s.envPitchInvert;
        p.mod.envToPWInvert = s.envPWInvert;
        p.mix.osc1 = s.osc1Vol;
        p.mix.osc2 = s.osc2Vol;
        p.mix.ringMod = s.ringModVol;
        p.mix.noise = s.noiseVol;
        p.mix.noiseColor = s.noiseColor;
        sharedPar = p;

        envPitchAmt = s.envToPitch * 40.f;                  // processEnvToPitchAmount
        envPWAmt = linsc(s.envToPW, 0.f, 1.055555555555555f); // processEnvToPWAmount
        envPitchBoth = s.envPitchBothOscs;
        envPWBoth = s.envPWBothOscs;
        osc2PWOffset = s.osc2PWOffset;

        if (s.brightness != brightness)
        {
            brightness = s.brightness;
            updateBrightness();
        }

        unison = s.unisonVoices < 1 ? 1 : (s.unisonVoices > MAX_UNISON ? MAX_UNISON : s.unisonVoices);
    }

    // Number of unison voices actually run for a given channel count (CPU budget).
    int unisonFor(int channels) const
    {
        int budget = MAX_OSC_BLOCKS / (channels < 1 ? 1 : channels);
        if (budget < 1)
            budget = 1;
        return unison < budget ? unison : budget;
    }

    // Returns the oscillator output in volts for channel c.
    float process(int c, int channels, const OscModulation &m)
    {
        Channel &ch = chans[c];
        const int n = unisonFor(channels);

        // Voice.h: tunedNote - 93 (equal temperament, MIDI note from V/Oct)
        const float noteTarget = 60.f + 12.f * m.voct - 93.f;

        // Voice.h: pitch and pulse-width modulation from the (here external) envelope
        const float pitchEnv = m.envPitch * (sharedPar.mod.envToPitchInvert ? -1.f : 1.f);
        const float pwEnv = m.envPW * (sharedPar.mod.envToPWInvert ? -1.f : 1.f);
        const float osc2Offset = obxf::linsc(clamp01(osc2PWOffset + m.osc2PWOffset), 0.f, 0.95f);

        const float osc1PitchMod = envPitchBoth ? envPitchAmt * pitchEnv : 0.f;
        const float osc2PitchMod = envPitchAmt * pitchEnv;
        const float osc1PWMod = envPWBoth ? envPWAmt * pwEnv : 0.f;
        const float osc2PWMod = envPWAmt * pwEnv + osc2Offset;

        auto renderOnce = [&]() -> float {
            float sum = 0.f;
            for (int u = 0; u < n; u++)
            {
                UnisonVoice &v = voices[c][u];
                obxf::OscillatorBlock &o = v.oscs;

                o.par.pitch.transpose = sharedPar.pitch.transpose;
                o.par.pitch.tune = sharedPar.pitch.tune;
                o.par.pitch.unisonDetune = sharedPar.pitch.unisonDetune;
                o.par.osc = sharedPar.osc;
                o.par.mix = sharedPar.mix;
                if (m.wave1 >= 0)
                {
                    o.par.osc.saw1 = m.wave1 & 1;
                    o.par.osc.pulse1 = m.wave1 & 2;
                }
                if (m.wave2 >= 0)
                {
                    o.par.osc.saw2 = m.wave2 & 1;
                    o.par.osc.pulse2 = m.wave2 & 2;
                }
                o.par.mix.osc1 = clamp01(sharedPar.mix.osc1 + m.osc1Vol);
                o.par.mix.osc2 = clamp01(sharedPar.mix.osc2 + m.osc2Vol);
                o.par.mix.ringMod = clamp01(sharedPar.mix.ringMod + m.ringModVol);

                // Voice.h: portamento processing (implements RC circuit)
                o.par.pitch.notePlaying = obxf::tpt_lp_unwarped(
                    v.portamento, noteTarget,
                    PORTAMENTO_DEFAULT * (1 + v.slopPortamento * SLOP_PORTAMENTO), rateInv);

                o.par.mod.osc1PitchMod = osc1PitchMod;
                o.par.mod.osc2PitchMod = osc2PitchMod;
                o.par.mod.osc1PWMod = osc1PWMod;
                o.par.mod.osc2PWMod = osc2PWMod;

                // Voice.h: oscs.ProcessSample() * (1 - par.slop.level * slop.level)
                sum += o.ProcessSample() * (1 - SLOP_LEVEL * v.slopLevel);
            }

            /* Voice.h: "process oscillator brightness". Both stages are linear, so
             * running them once on the unison sum equals running them per voice. */
            sum = sum - obxf::tpt_lp_unwarped(ch.dcBlock, sum, 12, rateInv);
            sum = obxf::tpt_process(ch.brightness, sum, brightnessCoef);
            return sum;
        };

        float out;
        if (oversample)
        {
            // Motherboard.h: two voice samples at 2x, then Decimator17
            const float x1 = renderOnce();
            const float x2 = renderOnce();
            out = ch.decimator.decimate(x1, x2);
        }
        else
        {
            out = renderOnce();
        }

        return out * VOLT_SCALE;
    }

  private:
    struct UnisonVoice
    {
        obxf::OscillatorBlock oscs;
        float portamento{0.f};
        float slopPortamento{0.f};
        float slopLevel{0.f};
    };

    struct Channel
    {
        float dcBlock{0.f};
        float brightness{0.f};
        obxf::Decimator17 decimator;
    };

    UnisonVoice voices[MAX_CHANNELS][MAX_UNISON];
    Channel chans[MAX_CHANNELS];
    obxf::OscillatorBlock::Parameters sharedPar;

    float hostRate{48000.f};
    float rate{48000.f};
    float rateInv{1.f / 48000.f};
    bool oversample{false};

    float envPitchAmt{0.f}, envPWAmt{0.f}, osc2PWOffset{0.f};
    bool envPitchBoth{true}, envPWBoth{true};
    float brightness{1.f};
    float brightnessCoef{0.f};
    int unison{1};

    void updateRate()
    {
        // Motherboard::SetHQMode(): voices run at sampleRate * (1 + over)
        rate = hostRate * (oversample ? 2.f : 1.f);
        rateInv = 1.f / rate;

        for (int c = 0; c < MAX_CHANNELS; c++)
        {
            for (int u = 0; u < MAX_UNISON; u++)
            {
                obxf::OscillatorBlock &o = voices[c][u].oscs;
                /* Also re-rolls the tuning slop and start phases, as in OB-Xf. OB-Xf's
                 * OscillatorBlock seeds them with std::rand(); this only happens on a
                 * sample-rate or HQ change, never per sample. */
                o.setSampleRate(rate);
                if (oversample)
                    o.setDecimation();
                else
                    o.removeDecimation();
            }
            chans[c].decimator.resetDecimator();
        }
        updateBrightness();
    }

    void updateBrightness()
    {
        // SynthEngine::processOscBrightness + Voice::setBrightness
        const float hz = obxf::linsc(brightness, 7000.f, 26000.f);
        // Voice::setBrightness uses the double-precision tan()
        brightnessCoef = static_cast<float>(::tan(std::min(hz, (rate * 0.5f) - 10) * obxf::pi * rateInv));
    }
};

} // namespace obxfvcv
