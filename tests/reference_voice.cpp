// Test-only. Compiled in the global namespace against the *original* OB-Xf
// Voice.h, Lfo.h and Tuning.h (copied by tests/Makefile from thirdparty-src/).
#include <cstdlib>
#include <cmath>
#include "Voice.h"
#include "Smoother.h"
#include "reference_voice.hpp"

struct RefSynth::Impl
{
    static constexpr int MAXU = 8;
    Tuning tuning;
    Voice voices[MAXU];
    VoiceMatrix vm;
    Smoother co, re, fm;
    Decimator17 dec;
    int unison;
    bool hq;
    RefPatch p;
};

RefSynth::RefSynth(float sr, int unison, bool hq, unsigned seed, const RefPatch &p)
    : impl(new Impl)
{
    impl->unison = unison;
    impl->hq = hq;
    impl->p = p;
    impl->co.setSampleRate(sr);
    impl->re.setSampleRate(sr);
    impl->fm.setSampleRate(sr);
    // SynthEngine::processFilterCutoff/Resonance/Mode
    impl->co.setStep(linsc(p.cutoff, 0.f, 120.f));
    impl->re.setStep(p.resonance);
    impl->fm.setStep(p.mode);

    for (int u = 0; u < unison; u++)
    {
        Voice &v = impl->voices[u];
        v.initTuning(&impl->tuning);
        std::srand(seed + u);
        v.setSampleRate(sr * (hq ? 2.f : 1.f)); // Motherboard::SetHQMode
        v.setHQMode(hq);

        // SynthEngine::process*() at the patch values
        v.oscs.par.pitch.transpose = p.transpose;
        v.oscs.par.pitch.tune = p.tune * 0.01f;
        v.oscs.par.pitch.unisonDetune = logsc(p.unisonDetune, 0.001f, 1.f);
        v.oscs.par.osc.pitch1 = p.osc1Pitch + 24.f;
        v.oscs.par.osc.pitch2 = p.osc2Pitch + 24.f;
        v.oscs.par.osc.detune = logsc(p.detune, 0.001f, 0.6f);
        v.oscs.par.osc.pw = linsc(p.pw, 0.f, 0.95f);
        v.oscs.par.osc.saw1 = p.saw1;
        v.oscs.par.osc.saw2 = p.saw2;
        v.oscs.par.osc.pulse1 = p.pulse1;
        v.oscs.par.osc.pulse2 = p.pulse2;
        v.oscs.par.osc.crossmod = p.crossmod * 48.f;
        v.oscs.par.osc.sync = p.sync;
        v.oscs.par.osc.keytrack2 = p.osc2Keytrack;
        v.oscs.par.mod.envToPitchInvert = p.envPitchInvert;
        v.oscs.par.mod.envToPWInvert = p.envPWInvert;
        v.oscs.par.mix.osc1 = p.osc1Vol;
        v.oscs.par.mix.osc2 = p.osc2Vol;
        v.oscs.par.mix.ringMod = p.ringModVol;
        v.oscs.par.mix.noise = p.noiseVol;
        v.oscs.par.mix.noiseColor = p.noiseColor;
        v.par.osc.pwOsc2Offset = linsc(p.osc2PWOffset, 0.f, 0.95f);
        v.par.osc.envPitchAmt = p.envToPitch * 40.f;
        v.par.osc.envPWAmt = linsc(p.envToPW, 0.f, 1.055555555555555f);
        v.par.osc.envPitchBothOscs = p.envPitchBothOscs;
        v.par.osc.envPWBothOscs = p.envPWBothOscs;
        v.par.osc.portamento = logsc(1.f - 0.f, 0.14f, 250.f, 150.f);
        v.setBrightness(linsc(p.brightness, 7000.f, 26000.f));

        v.par.filter.keytrack = p.keytrack;
        v.par.filter.envAmt = linsc(p.envAmount, 0.f, 140.f);
        v.par.filter.invertEnv = p.invertEnv;
        v.par.filter.invertEnvScale = p.invertEnv ? -1.f : 1.f;
        v.par.filter.fourPole = p.fourPole;
        v.setFilter2PolePush(p.push2Pole);
        v.filter.par.bpBlend2Pole = p.bpBlend2Pole;
        v.filter.par.xpander4Pole = p.xpander;
        v.filter.par.xpanderMode = (uint8_t)std::lround(p.mode * (NUM_XPANDER_MODES - 1));
        v.par.extmod.velToFilter = p.velocity;

        const float a = logsc(p.attack, 1.f, 60000.f, 900.f);
        const float d = logsc(p.decay, 1.f, 60000.f, 900.f);
        const float r = logsc(p.release, 1.f, 60000.f, 900.f);
        v.filterEnv.setAttack(a);
        v.filterEnv.setDecay(d);
        v.filterEnv.setSustain(p.sustain);
        v.filterEnv.setRelease(r);
        v.filterEnv.setAttackCurve(p.attackCurve);
        v.filterEnvAttackBase = a;
        v.filterEnvReleaseBase = r;
    }
}

RefSynth::~RefSynth() { delete impl; }

void RefSynth::noteOn(int note, float velocity)
{
    for (int u = 0; u < impl->unison; u++)
        impl->voices[u].NoteOn(note, velocity, 0);
}

void RefSynth::noteOff()
{
    for (int u = 0; u < impl->unison; u++)
        impl->voices[u].NoteOff(0.f);
}

float RefSynth::process()
{
    // SynthEngine::processSample
    const float co = impl->co.smoothStep();
    const float re = impl->re.smoothStep();
    const float fm = impl->fm.smoothStep();
    const float res = 0.991f - logsc(1.f - re, 0.f, 0.991f, 40.f);

    float x1 = 0.f, x2 = 0.f;
    for (int u = 0; u < impl->unison; u++)
    {
        Voice &v = impl->voices[u];
        v.par.filter.cutoff = co;
        v.filter.setResonance(res);
        v.filter.setMultimode(fm);
        v.pitchBend = 0.f;
        v.lfo1In = 0.f;
        v.vibratoLFOIn = 0.f;
        // Motherboard::processSynthVoice (ECO mode)
        v.updateSoundingState();
        if (!v.isSounding())
            continue;
        x1 += v.ProcessSample(impl->vm);
        if (impl->hq)
            x2 += v.ProcessSample(impl->vm);
    }
    return impl->hq ? impl->dec.decimate(x1, x2) : x1;
}
