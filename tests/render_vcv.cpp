// Renders one note through OBXm Oscillator -> OBXm Filter (+ an OB-Xf-style amp
// envelope and output gain), taking the same parameter names and normalized values
// as the OB-Xf plug-in, so the output can be compared with tests/vst3_render.
//
//   render_vcv <out.wav> <note> <seconds> <gate_seconds> [Param=value ...]
//
// GPL-3.0-or-later.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "dsp/FilterEngine.hpp"
#include "dsp/OscEngine.hpp"

using namespace obxfvcv;

static const float SR = 48000.f;

static void writeWav(const char *path, const std::vector<float> &x)
{
    FILE *f = std::fopen(path, "wb");
    if (!f)
        return;
    const uint32_t n = x.size(), sr = (uint32_t)SR, bytes = n * 4;
    auto w32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto w16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f);
    w32(36 + bytes);
    std::fwrite("WAVEfmt ", 1, 8, f);
    w32(16);
    w16(3);
    w16(1);
    w32(sr);
    w32(sr * 4);
    w16(4);
    w16(32);
    std::fwrite("data", 1, 4, f);
    w32(bytes);
    std::fwrite(x.data(), 4, n, f);
    std::fclose(f);
}

int main(int argc, char **argv)
{
    if (argc < 5)
    {
        std::fprintf(stderr, "usage: %s out.wav note seconds gate [Param=v ...]\n", argv[0]);
        return 1;
    }
    OscSettings o;
    FilterSettings f;
    float volume = 0.5f;
    int unisonVoices = 8;
    bool unison = false;
    bool hq = false;

    auto semis = [](float v) { return v * 48.f - 24.f; };
    std::map<std::string, std::function<void(float)>> set = {
        {"Volume", [&](float v) { volume = v; }},
        {"Transpose", [&](float v) { o.transpose = (int)std::lround((v * 2 - 1) * 24); }},
        {"Tune", [&](float v) { o.tune = (v * 2 - 1) * 100; }},
        {"High Quality Mode", [&](float v) { hq = v > 0.5f; }},
        {"Unison Voices", [&](float v) { unisonVoices = 1 + (int)(v * 32); }},
        {"Unison", [&](float v) { unison = v > 0.5f; }},
        {"Unison Detune", [&](float v) { o.unisonDetune = v; }},
        {"Osc 1 Pitch", [&](float v) { o.osc1Pitch = semis(v); }},
        {"Osc 2 Pitch", [&](float v) { o.osc2Pitch = semis(v); }},
        {"Osc 2 Detune", [&](float v) { o.detune = v; }},
        {"Osc 1 Saw Wave", [&](float v) { o.saw1 = v > 0.5f; }},
        {"Osc 1 Pulse Wave", [&](float v) { o.pulse1 = v > 0.5f; }},
        {"Osc 2 Saw Wave", [&](float v) { o.saw2 = v > 0.5f; }},
        {"Osc 2 Pulse Wave", [&](float v) { o.pulse2 = v > 0.5f; }},
        {"Osc Pulsewidth", [&](float v) { o.pw = v; }},
        {"Osc 2 PW Offset", [&](float v) { o.osc2PWOffset = v; }},
        {"Cross Modulation", [&](float v) { o.crossmod = v; }},
        {"Osc Sync", [&](float v) { o.sync = v > 0.5f; }},
        {"Osc Brightness", [&](float v) { o.brightness = v; }},
        {"Osc 1 Volume", [&](float v) { o.osc1Vol = v; }},
        {"Osc 2 Volume", [&](float v) { o.osc2Vol = v; }},
        {"Ring Mod Volume", [&](float v) { o.ringModVol = v; }},
        {"Noise Volume", [&](float v) { o.noiseVol = v; }},
        {"Noise Color", [&](float v) { o.noiseColor = (int)std::lround(v * 2); }},
        {"Osc 2 Keytrack", [&](float v) { o.osc2Keytrack = v > 0.5f; }},
        {"Filter Cutoff", [&](float v) { f.cutoff = v; }},
        {"Filter Resonance", [&](float v) { f.resonance = v; }},
        {"Filter Env Amount", [&](float v) { f.envAmount = v; }},
        {"Filter Keytrack", [&](float v) { f.keytrack = v; }},
        {"Filter Mode", [&](float v) { f.mode = v; }},
        {"Filter 4-Pole Mode", [&](float v) { f.fourPole = v > 0.5f; }},
        {"Filter Env Attack", [&](float v) { f.attack = v; }},
        {"Filter Env Decay", [&](float v) { f.decay = v; }},
        {"Filter Env Sustain", [&](float v) { f.sustain = v; }},
        {"Filter Env Release", [&](float v) { f.release = v; }},
    };
    for (int a = 5; a < argc; a++)
    {
        std::string kv = argv[a];
        auto eq = kv.find('=');
        auto it = set.find(kv.substr(0, eq));
        if (eq == std::string::npos || it == set.end())
        {
            std::fprintf(stderr, "unsupported parameter: %s\n", argv[a]);
            return 1;
        }
        it->second(std::atof(kv.c_str() + eq + 1));
    }
    o.unisonVoices = unison ? std::min(unisonVoices, MAX_UNISON) : 1;

    const int note = std::atoi(argv[2]);
    const int N = (int)(std::atof(argv[3]) * SR);
    const int gateN = (int)(std::atof(argv[4]) * SR);

    auto osc = std::make_unique<OscEngine>();
    auto flt = std::make_unique<FilterEngine>();
    osc->setSampleRate(SR);
    flt->setSampleRate(SR);
    osc->setHQ(hq);
    flt->setHQ(hq);
    osc->setSettings(o);
    flt->setSettings(f);
    // let the cutoff/resonance smoothers settle, like the 0.5 s preroll of vst3_render
    for (int i = 0; i < (int)(0.5f * SR); i++)
    {
        flt->beginSample();
        float e;
        flt->process(0, FilterModulation{}, e);
    }

    // OB-Xf amp envelope at its defaults (4 ms attack, 8 ms release, sustain capped at 0.9),
    // delayed by 32 samples as in Voice.h
    obxf::ADSREnvelope amp;
    obxf::DelayLine<obxf::B_SAMPLES * obxf::OVERSAMPLE_FACTOR, float> ampDelayed;
    amp.setSampleRate(SR);
    amp.setAttack(4.f);
    amp.setDecay(4.f);
    amp.setSustain(1.f);
    amp.setRelease(8.f);
    amp.triggerAttack();

    // Motherboard: voice * pan(0.5) * volume, volume = linsc(Volume, 0, 0.3)
    const float outGain = 0.5f * volume * 0.3f;

    // optional note sequence: SEQ="note:start_s:dur_s,..." (one voice, like a Rack channel)
    struct Q
    {
        int note, start, len;
    };
    std::vector<Q> seq;
    if (const char *sq = getenv("SEQ"))
    {
        std::string str = sq;
        size_t p = 0;
        while (p < str.size())
        {
            size_t e = str.find(',', p);
            if (e == std::string::npos)
                e = str.size();
            int n;
            float st, d;
            if (std::sscanf(str.substr(p, e - p).c_str(), "%d:%f:%f", &n, &st, &d) == 3)
                seq.push_back({n, (int)(st * SR), (int)(d * SR)});
            p = e + 1;
        }
    }
    const bool useAmp = !getenv("NOAMP");
    int curNote = note;
    bool prevGate = false;
    if (!seq.empty())
        amp.ResetEnvelopeState();

    std::vector<float> out(N);
    for (int i = 0; i < N; i++)
    {
        bool gate = i < gateN;
        if (!seq.empty())
        {
            gate = false;
            for (auto &q : seq)
                if (i >= q.start && i < q.start + q.len)
                {
                    gate = true;
                    curNote = q.note;
                }
            if (gate && !prevGate)
                amp.triggerAttack();
            if (!gate && prevGate)
                amp.triggerRelease();
        }
        else if (i == gateN)
            amp.triggerRelease();
        prevGate = gate;
        OscModulation om;
        om.voct = (curNote - 60) / 12.f;
        FilterModulation fm;
        fm.gate = gate;
        fm.voct = om.voct;
        fm.in = osc->process(0, 1, om);
        float e;
        flt->beginSample();
        const float y = flt->process(0, fm, e) * VOLT_SCALE_INV;
        amp.applyMatrixRelease(8.f); // Voice.h: ampEnv.applyMatrixRelease(base) every sample
        const float a = ampDelayed.feedReturn(amp.processSample());
        out[i] = y * (useAmp ? a : 1.f) * outGain;
    }
    writeWav(argv[1], out);
    std::printf("wrote %s\n", argv[1]);
    return 0;
}
