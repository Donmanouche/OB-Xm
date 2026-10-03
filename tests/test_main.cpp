// OB-Xm — offline DSP tests.
//
// 1. A/B against OB-Xf's own Voice class (tests/reference_voice.cpp): the
//    oscillator engine feeding the filter engine must reproduce Voice::ProcessSample.
// 2. Aliasing / level checks of the oscillator.
// 3. WAV renders in tests/out/ for listening.
//
// GPL-3.0-or-later.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <complex>
#include <string>
#include <vector>
#include <memory>
#include <chrono>

#include "dsp/OscEngine.hpp"
#include "dsp/FilterEngine.hpp"
#include "reference_voice.hpp"

namespace obxfvcv
{
// Gives the tests the same seeds / zero slop as the reference voice.
struct EngineTestAccess
{
    static void alignOsc(OscEngine &e, int c, int unison, unsigned seed)
    {
        for (int u = 0; u < unison; u++)
        {
            std::srand(seed + u);
            auto &v = e.voices[c][u];
            v.oscs.setSampleRate(e.rate); // first std::rand() of Voice::setSampleRate
            if (e.oversample)
                v.oscs.setDecimation();
            v.slopPortamento = 0.f;
            v.slopLevel = 0.f;
        }
    }
    static void noPitchJitter(OscEngine &e)
    {
        for (auto &row : e.voices)
            for (auto &v : row)
                v.oscs.par.mod.oscPitchNoise = 0.f;
    }
    static void alignFilter(FilterEngine &e, int c, unsigned seed)
    {
        std::srand(seed);
        std::rand();
        const int second = std::rand(); // Voice::setSampleRate: noiseGen.seedWhiteNoise(std::rand())
        auto &ch = e.chans[c];
        ch.noise.seedWhiteNoise(second);
        ch.slopCutoff = ch.slopEnv = ch.slopPortamento = 0.f;
        ch.env.setEnvOffsets(1.f);
        ch.envVersion = -1;
    }
};
} // namespace obxfvcv

using namespace obxfvcv;

static constexpr float SR = 48000.f;
static int failures = 0;
static float g_perturb = 0.f; // explore only: relative error injected into the osc signal

static void writeWav(const std::string &path, const std::vector<float> &x, float gain)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f)
        return;
    const uint32_t n = x.size(), sr = (uint32_t)SR, bytes = n * 4;
    auto w32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto w16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f);
    w32(36 + bytes);
    std::fwrite("WAVEfmt ", 1, 8, f);
    w32(16);
    w16(3); // IEEE float
    w16(1);
    w32(sr);
    w32(sr * 4);
    w16(4);
    w16(32);
    std::fwrite("data", 1, 4, f);
    w32(bytes);
    for (float v : x)
    {
        float s = v * gain;
        std::fwrite(&s, 4, 1, f);
    }
    std::fclose(f);
}

static OscSettings oscFrom(const RefPatch &p, int unison)
{
    OscSettings s;
    s.osc1Pitch = p.osc1Pitch;
    s.osc2Pitch = p.osc2Pitch;
    s.detune = p.detune;
    s.pw = p.pw;
    s.osc2PWOffset = p.osc2PWOffset;
    s.envToPitch = p.envToPitch;
    s.envToPW = p.envToPW;
    s.crossmod = p.crossmod;
    s.brightness = p.brightness;
    s.osc1Vol = p.osc1Vol;
    s.osc2Vol = p.osc2Vol;
    s.ringModVol = p.ringModVol;
    s.noiseVol = p.noiseVol;
    s.noiseColor = p.noiseColor;
    s.transpose = p.transpose;
    s.tune = p.tune;
    s.unisonVoices = unison;
    s.unisonDetune = p.unisonDetune;
    s.saw1 = p.saw1;
    s.pulse1 = p.pulse1;
    s.saw2 = p.saw2;
    s.pulse2 = p.pulse2;
    s.sync = p.sync;
    s.osc2Keytrack = p.osc2Keytrack;
    s.envPitchBothOscs = p.envPitchBothOscs;
    s.envPitchInvert = p.envPitchInvert;
    s.envPWBothOscs = p.envPWBothOscs;
    s.envPWInvert = p.envPWInvert;
    return s;
}

static FilterSettings filterFrom(const RefPatch &p)
{
    FilterSettings s;
    s.cutoff = p.cutoff;
    s.resonance = p.resonance;
    s.mode = p.mode;
    s.envAmount = p.envAmount;
    s.keytrack = p.keytrack;
    s.fourPole = p.fourPole;
    s.xpander = p.xpander;
    s.push2Pole = p.push2Pole;
    s.bpBlend2Pole = p.bpBlend2Pole;
    s.invertEnv = p.invertEnv;
    s.attack = p.attack;
    s.decay = p.decay;
    s.sustain = p.sustain;
    s.release = p.release;
    s.attackCurve = p.attackCurve;
    s.velocity = p.velocity;
    return s;
}

/* Renders the same note through OB-Xf's Voice and through OscEngine -> FilterEngine.
 * The reference voice also has OB-Xf's amp envelope (Voice.h defaults: 4 ms attack,
 * sustain capped at 0.9, 8 ms release, delayed by 32 samples), so the VCV chain is
 * followed here by the same envelope -- in a patch that is the job of a VCA. */
static double abCompare(const char *name, const RefPatch &p, int unison, bool hq, int note,
                        float vel, double maxErrDb)
{
    const int N = (int)(SR * 1.0f);
    const int noteOffAt = (int)(SR * 0.7f);
    const unsigned seed = 1234;

    RefSynth ref(SR, unison, hq, seed, p);

    auto osc = std::make_unique<OscEngine>();
    auto flt = std::make_unique<FilterEngine>();
    auto envSrc = std::make_unique<FilterEngine>(); // gives the osc the same-sample envelope
    osc->setSampleRate(SR);
    flt->setSampleRate(SR);
    envSrc->setSampleRate(SR);
    osc->setHQ(hq);
    flt->setHQ(hq);
    envSrc->setHQ(hq);
    osc->setSettings(oscFrom(p, unison));
    flt->setSettings(filterFrom(p));
    envSrc->setSettings(filterFrom(p));
    EngineTestAccess::alignOsc(*osc, 0, unison, seed);
    EngineTestAccess::alignFilter(*flt, 0, seed);
    EngineTestAccess::alignFilter(*envSrc, 0, seed);

    obxf::ADSREnvelope amp; // test-only stand-in for OB-Xf's amp envelope
    obxf::DelayLine<obxf::B_SAMPLES * obxf::OVERSAMPLE_FACTOR, float> ampDelayed;
    amp.setSampleRate(SR);
    amp.setAttack(4.f);
    amp.setRelease(8.f);
    amp.triggerAttack();

    std::vector<float> a(N), b(N);
    ref.noteOn(note, vel);
    for (int i = 0; i < N; i++)
    {
        if (i == noteOffAt)
        {
            ref.noteOff();
            amp.triggerRelease();
        }
        a[i] = ref.process();

        const bool gate = i < noteOffAt;
        float env = 0.f;
        FilterModulation em;
        em.gate = gate;
        em.velocity = vel;
        em.voct = (note - 60) / 12.f;
        envSrc->beginSample();
        envSrc->process(0, em, env);

        OscModulation om;
        om.voct = (note - 60) / 12.f;
        om.envPitch = env;
        om.envPW = env;
        const float o = osc->process(0, 1, om);

        FilterModulation fm = em;
        fm.in = o * (1.f + g_perturb);
        float envOut;
        flt->beginSample();
        amp.applyMatrixRelease(8.f);
        b[i] = flt->process(0, fm, envOut) * VOLT_SCALE_INV * ampDelayed.feedReturn(amp.processSample());
    }

    // b may lag a by a few samples (HQ: two decimators instead of one): best lag 0..24
    const int from = 0;
    double errDb = 1e9;
    int bestLag = 0;
    for (int lag = 0; lag <= 24; lag++)
    {
        double num = 0, den = 0;
        for (int i = from; i < N - lag; i++)
        {
            const double d = a[i] - b[i + lag];
            num += d * d;
            den += (double)a[i] * a[i];
        }
        const double e = 10 * std::log10((num + 1e-30) / (den + 1e-30));
        if (e < errDb)
        {
            errDb = e;
            bestLag = lag;
        }
    }
    const bool ok = errDb <= maxErrDb;
    std::printf("  %-26s uni=%d hq=%d  error %7.1f dB, lag %2d (limit %6.1f)  %s\n", name, unison,
                hq, errDb, bestLag, maxErrDb, ok ? "OK" : "FAIL");
    if (!ok)
        failures++;

    std::string base = std::string("out/") + name + (hq ? "_hq" : "") + "_u" + std::to_string(unison);
    writeWav(base + "_obxf.wav", a, 1.f);
    writeWav(base + "_vcv.wav", b, 1.f);
    return errDb;
}

// ---------------------------------------------------------------------------

static void fft(std::vector<std::complex<double>> &x)
{
    const size_t n = x.size();
    for (size_t i = 1, j = 0; i < n; i++)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(x[i], x[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2 * M_PI / len;
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w(1);
            for (size_t k = 0; k < len / 2; k++)
            {
                auto u = x[i + k], v = x[i + k + len / 2] * w;
                x[i + k] = u + v;
                x[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// Ratio (dB) of the energy away from the harmonics of f0, below 20 kHz, to the harmonic energy.
static double inharmonicDb(const std::vector<float> &x, int start, float f0)
{
    const int n = 1 << 15;
    std::vector<std::complex<double>> X(n);
    for (int i = 0; i < n; i++)
    {
        const double t = 2 * M_PI * i / (n - 1); // 4-term Blackman-Harris, -92 dB sidelobes
        const double w = 0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) -
                         0.01168 * std::cos(3 * t);
        X[i] = x[start + i] * w;
    }
    fft(X);
    double harm = 0, other = 0;
    const double binHz = SR / n;
    for (int k = 1; k < n / 2; k++)
    {
        const double f = k * binHz;
        if (f > 20000)
            break;
        const double h = f / f0;
        const double dist = std::fabs(h - std::round(h)) * f0;
        const double e = std::norm(X[k]);
        if (dist < 8 * binHz && std::round(h) >= 1)
            harm += e;
        else
            other += e;
    }
    return 10 * std::log10(other / harm);
}

static std::vector<float> renderOsc(const OscSettings &s, bool hq, int note, int n, int channels = 1,
                                    bool jitter = true)
{
    auto osc = std::make_unique<OscEngine>();
    osc->setSampleRate(SR);
    osc->setHQ(hq);
    osc->setSettings(s);
    if (!jitter)
        EngineTestAccess::noPitchJitter(*osc);
    std::vector<float> out(n);
    for (int i = 0; i < n; i++)
    {
        OscModulation m;
        m.voct = (note - 60) / 12.f;
        out[i] = osc->process(0, channels, m);
        for (int c = 1; c < channels; c++)
            osc->process(c, channels, m);
    }
    return out;
}

static void oscChecks()
{
    std::printf("\nOscillator checks\n");
    OscSettings s;
    s.unisonDetune = 0.f; // no slop detune, for a clean spectrum

    // Level: one saw at full mix should swing about +-5 V
    {
        auto x = renderOsc(s, false, 48, (int)SR);
        float pk = 0;
        for (int i = 4800; i < (int)SR; i++)
            pk = std::max(pk, std::fabs(x[i]));
        // +-5 V plus the BLEP/brightness overshoot, which varies with OB-Xf's random start
        // phase and pitch jitter (6.0-6.6 V seen): this checks the level, not the overshoot
        const bool ok = pk > 4.f && pk < 7.5f;
        std::printf("  saw C3 peak level                 %.2f V   %s\n", pk, ok ? "OK" : "FAIL");
        failures += !ok;
        writeWav("out/osc_saw_C3.wav", x, 0.1f);
    }

    // Aliasing: BLEP keeps foldover well below the harmonics, HQ lowers it further
    struct W
    {
        const char *name;
        bool saw, pulse;
    } waves[] = {{"saw", true, false}, {"pulse", false, true}, {"triangle", false, false}};
    for (auto &w : waves)
    {
        OscSettings t = s;
        t.saw1 = w.saw;
        t.pulse1 = w.pulse;
        const int note = 100; // ~2637 Hz
        const float f0 = 440.f * std::pow(2.f, (note - 69) / 12.f);
        // OB-Xf's per-sample pitch jitter (oscPitchNoise) is disabled to see the aliasing
        auto lo = renderOsc(t, false, note, 48000, 1, false);
        auto hi = renderOsc(t, true, note, 48000, 1, false);
        const double a = inharmonicDb(lo, 8000, f0), b = inharmonicDb(hi, 8000, f0);
        const bool ok = a < -40; // the oscillator is OB-Xf's own code: this is informative
        std::printf("  %-8s @%.0f Hz inharmonic: normal %6.1f dB, HQ %6.1f dB   %s\n", w.name, f0,
                    a, b, ok ? "OK" : "FAIL");
        failures += !ok;
        writeWav(std::string("out/osc_") + w.name + "_2637Hz.wav", lo, 0.1f);
        writeWav(std::string("out/osc_") + w.name + "_2637Hz_hq.wav", hi, 0.1f);
    }

    // Unison: more voices -> louder (OB-Xf sums unison voices without normalization).
    // Start phases are random, as in OB-Xf, so the energy is averaged over 8 instances:
    // N voices of random phase add up to about N times the energy of one.
    {
        OscSettings t = s;
        t.unisonDetune = 0.25f;
        double e1 = 0;
        bool ok = true;
        for (int u : {1, 2, 4, 8})
        {
            if (u > MAX_UNISON)
                break;
            t.unisonVoices = u;
            double e = 0;
            for (int inst = 0; inst < 8; inst++)
            {
                auto x = renderOsc(t, false, 48, 24000);
                for (int i = 4800; i < 24000; i++)
                    e += x[i] * x[i];
            }
            e /= 8 * (24000 - 4800);
            if (u == 1)
                e1 = e;
            std::printf("  unison %d voices: mean energy %.1f x one voice\n", u, e / e1);
            if (u > 1)
                ok = ok && e > 0.5 * u * e1;
        }
        failures += !ok;
    }

    // Waveform CV overrides (bit 0 = saw, bit 1 = pulse) and mixer level CVs
    {
        auto renderWith = [&](int wave1, float osc1VolMod) {
            auto osc = std::make_unique<OscEngine>();
            osc->setSampleRate(SR);
            osc->setSettings(s); // buttons: saw
            EngineTestAccess::noPitchJitter(*osc);
            std::vector<float> out(48000);
            for (int i = 0; i < 48000; i++)
            {
                OscModulation m;
                m.voct = (48 - 60) / 12.f;
                m.wave1 = wave1;
                m.osc1Vol = osc1VolMod;
                out[i] = osc->process(0, 1, m);
            }
            return out;
        };
        auto h2 = [&](const std::vector<float> &x) {
            const int n = 1 << 15;
            std::vector<std::complex<double>> X(n);
            for (int i = 0; i < n; i++)
                X[i] = x[8000 + i] * (0.5 - 0.5 * std::cos(2 * M_PI * i / (n - 1)));
            fft(X);
            const double f0 = 440.0 * std::pow(2.0, (48 - 69) / 12.0);
            auto e = [&](double f) {
                const int b = (int)std::lround(f * n / SR);
                double v = 0;
                for (int k = b - 3; k <= b + 3; k++)
                    v += std::norm(X[k]);
                return v;
            };
            return 10 * std::log10(e(2 * f0) / e(f0));
        };
        const double saw = h2(renderWith(-1, 0.f)), tri = h2(renderWith(0, 0.f)),
                     pulse = h2(renderWith(2, 0.f));
        double silent = 0;
        for (float v : renderWith(-1, -1.f))
            silent = std::max(silent, (double)std::fabs(v));
        const bool ok = saw > -8 && tri < -60 && pulse < -60 && silent < 0.05;
        std::printf("  wave CV: H2 saw %.1f / tri %.1f / pulse %.1f dB, osc1 level CV -1: peak %.3f V   %s\n",
                    saw, tri, pulse, silent, ok ? "OK" : "FAIL");
        failures += !ok;
    }

    // MetaModule CPU budget: unison is capped when channels * unison exceeds MAX_OSC_BLOCKS
    {
        OscEngine e;
        OscSettings t;
        t.unisonVoices = MAX_UNISON;
        e.setSettings(t);
        std::printf("  budget: unison %d -> 1ch:%d 4ch:%d (MAX_OSC_BLOCKS=%d)\n", MAX_UNISON,
                    e.unisonFor(1), e.unisonFor(4), MAX_OSC_BLOCKS);
    }
}

static void filterChecks()
{
    std::printf("\nFilter checks\n");
    // Xpander mode mapping 0..10 V across the 15 modes
    int last = -1;
    bool ok = true;
    for (int i = 0; i <= 100; i++)
    {
        const int m = FilterEngine::xpanderModeFor(i / 100.f);
        ok = ok && (m == last || m == last + 1);
        last = m;
    }
    ok = ok && last == 14;
    std::printf("  xpander mode mapping monotonic, 0..14   %s\n", ok ? "OK" : "FAIL");
    failures += !ok;

    // ATTACK / DECAY CV: time for the envelope to reach 0.9 of its peak
    {
        auto riseTime = [](float attackMod) {
            auto flt = std::make_unique<FilterEngine>();
            flt->setSampleRate(SR);
            FilterSettings fs;
            fs.attack = 0.3f;
            fs.sustain = 1.f;
            flt->setSettings(fs);
            for (int i = 0; i < (int)SR * 10; i++)
            {
                FilterModulation m;
                m.gate = true;
                m.attack = attackMod;
                float e;
                flt->beginSample();
                flt->process(0, m, e);
                if (e > 0.85f)
                    return i / SR;
            }
            return 10.f;
        };
        const float t0 = riseTime(0.f), tUp = riseTime(0.3f), tDown = riseTime(-0.3f);
        const bool okA = tUp > 2 * t0 && tDown < 0.5f * t0;
        std::printf("  attack CV: rise %.0f ms, +3 V %.0f ms, -3 V %.0f ms   %s\n", t0 * 1000,
                    tUp * 1000, tDown * 1000, okA ? "OK" : "FAIL");
        failures += !okA;
    }

}

static void explore()
{
    RefPatch p;
    p.cutoff = 0.55f; p.resonance = 0.3f; p.envAmount = 0.4f; p.attack = 0.1f; p.decay = 0.4f;
    p.sustain = 0.3f; p.release = 0.3f; p.keytrack = 0.5f;
    abCompare("base", p, 1, false, 45, 1.f, -100);
    RefPatch push = p; push.push2Pole = true; push.bpBlend2Pole = true; push.mode = 0.5f;
    push.resonance = 0.95f; push.brightness = 0.3f;
    std::printf("  sensitivity to a 1e-7 relative input error:\n");
    for (float g : {0.f, 1e-7f, -1e-7f})
    {
        g_perturb = g;
        abCompare("base_perturbed", p, 1, false, 45, 1.f, 0);
        abCompare("push_res095_perturbed", push, 1, false, 40, 1.f, 0);
    }
    g_perturb = 0.f;
    RefPatch v = p; v.brightness = 0.3f; abCompare("bright03", v, 1, false, 45, 1.f, -100);
    v = p; v.mode = 0.5f; abCompare("mode05", v, 1, false, 45, 1.f, -100);
    v = p; v.mode = 0.5f; v.bpBlend2Pole = true; abCompare("mode05bp", v, 1, false, 45, 1.f, -100);
    v = p; v.resonance = 0.95f; abCompare("res095", v, 1, false, 45, 1.f, -100);
    v = p; abCompare("note40", v, 1, false, 40, 1.f, -100);
    v = p; v.mode = 0.f; v.bpBlend2Pole = true; abCompare("mode0bp", v, 1, false, 45, 1.f, -100);
    v = p; v.mode = 1.f; abCompare("mode1", v, 1, false, 45, 1.f, -100);
}

// Host CPU cost of one second of audio at 48 kHz, in % of real time on one core.
static void bench()
{
    using clk = std::chrono::steady_clock;
    const int N = 48000 * 4;
    auto pct = [&](double sec) { return 100.0 * sec / (N / SR); };
    volatile float sink = 0;

    for (bool hq : {false, true})
        for (int u : {1, 4, 8})
        {
            if (u > MAX_UNISON)
                continue;
            auto osc = std::make_unique<OscEngine>();
            osc->setSampleRate(SR);
            osc->setHQ(hq);
            OscSettings s;
            s.pulse1 = true;
            s.osc2Vol = 1.f;
            s.unisonVoices = u;
            osc->setSettings(s);
            OscModulation m;
            const auto t0 = clk::now();
            for (int i = 0; i < N; i++)
                sink = sink + osc->process(0, 1, m);
            const double t = std::chrono::duration<double>(clk::now() - t0).count();
            std::printf("  oscillator  1 ch, unison %d, hq %d: %6.3f %% (%.0f ns/sample)\n", u, hq,
                        pct(t), 1e9 * t / N);
        }
    for (bool hq : {false, true})
        for (bool four : {false, true})
        {
            auto flt = std::make_unique<FilterEngine>();
            flt->setSampleRate(SR);
            flt->setHQ(hq);
            FilterSettings s;
            s.fourPole = four;
            s.cutoff = 0.6f;
            s.resonance = 0.5f;
            s.envAmount = 0.5f;
            flt->setSettings(s);
            FilterModulation m;
            m.gate = true;
            const auto t0 = clk::now();
            float e;
            for (int i = 0; i < N; i++)
            {
                m.in = (i % 100) * 0.1f - 5.f;
                flt->beginSample();
                sink = sink + flt->process(0, m, e);
            }
            const double t = std::chrono::duration<double>(clk::now() - t0).count();
            std::printf("  filter      1 ch, %d-pole,   hq %d: %6.3f %% (%.0f ns/sample)\n",
                        four ? 4 : 2, hq, pct(t), 1e9 * t / N);
        }
}

int main(int argc, char **argv)
{
    if (argc > 1 && std::strcmp(argv[1], "bench") == 0)
    {
        bench();
        return 0;
    }
    if (argc > 1 && std::strcmp(argv[1], "explore") == 0)
    {
        explore();
        return 0;
    }
    std::printf("A/B against OB-Xf Voice (commit b08ffb6)\n");

    RefPatch p; // OB-Xf init-like: saw, LP 2-pole
    p.cutoff = 0.55f;
    p.resonance = 0.3f;
    p.envAmount = 0.4f;
    p.attack = 0.1f;
    p.decay = 0.4f;
    p.sustain = 0.3f;
    p.release = 0.3f;
    p.keytrack = 0.5f;
    abCompare("saw_lp2", p, 1, false, 45, 1.f, -100);

    RefPatch q = p; // pulse + sync + env to pitch/PW, 4-pole, high resonance
    q.saw1 = false;
    q.pulse1 = true;
    q.saw2 = false;
    q.pulse2 = true;
    q.osc2Vol = 0.8f;
    q.osc2Pitch = 7.f;
    q.sync = true;
    q.envToPitch = 0.3f;
    q.envToPW = 0.5f;
    q.pw = 0.3f;
    q.osc2PWOffset = 0.2f;
    q.fourPole = true;
    q.resonance = 0.85f;
    q.mode = 0.2f;
    q.velocity = 0.7f;
    q.attackCurve = 0.6f;
    abCompare("pulse_sync_lp4", q, 1, false, 52, 0.6f, -90);

    RefPatch r = p; // triangle + crossmod + ring + noise, Xpander BP4, inverted env
    r.saw1 = false;
    r.saw2 = true;
    r.osc2Vol = 0.6f;
    r.ringModVol = 0.4f;
    r.noiseVol = 0.2f;
    r.noiseColor = 1;
    r.crossmod = 0.2f;
    r.detune = 0.4f;
    r.fourPole = true;
    r.xpander = true;
    r.mode = 7.f / 14.f;
    r.invertEnv = true;
    r.cutoff = 0.75f;
    r.envAmount = 0.6f;
    abCompare("tri_xmod_xpander_bp4", r, 1, false, 57, 1.f, -100);

    RefPatch s = p; // 2-pole push + BP blend, mode morph
    s.push2Pole = true;
    s.bpBlend2Pole = true;
    s.mode = 0.5f;
    s.resonance = 0.95f;
    s.brightness = 0.3f;
    /* Near self-oscillation the filter amplifies float rounding: perturbing the input by
     * 1e-7 (relative) moves OB-Xf's own output by the same -57 dB (see "explore"). The
     * band-pass / high-pass outputs are small next to the input, so their relative error
     * is larger for the same absolute (~5e-8) rounding. Hence the looser limits. */
    abCompare("saw_2pole_push_bp_res095", s, 1, false, 40, 1.f, -50);
    RefPatch s2 = s;
    s2.resonance = 0.5f;
    abCompare("saw_2pole_push_bp_res05", s2, 1, false, 40, 1.f, -80);
    RefPatch s3 = s;
    s3.push2Pole = false;
    abCompare("saw_2pole_bp_res095", s3, 1, false, 40, 1.f, -80);
    RefPatch s4 = p;
    s4.mode = 1.f;
    abCompare("saw_2pole_hp", s4, 1, false, 45, 1.f, -80);
    RefPatch s5 = q;
    s5.attack = 0.f;
    s5.decay = 0.2f;
    s5.release = 0.6f;
    abCompare("pulse_lp4_long_release", s5, 1, false, 33, 0.3f, -80);

    std::printf("\nKnown, expected differences (reported, not exact):\n");
    // Unison: OB-Xf filters each unison voice; here the unison sum is filtered once.
    abCompare("saw_lp2", p, 4, false, 45, 1.f, -20);
    // HQ: OB-Xf runs the whole voice at 2x; here osc and filter oversample separately.
    abCompare("saw_lp2", p, 1, true, 45, 1.f, -20);

    oscChecks();
    filterChecks();

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
