// Runs the real OBXm Oscillator / OBXm Filter Module classes (compiled from src/)
// against libRack, the way Rack's engine does, and writes the output to WAV.
//   module_render <out.wav> <note> [osc|chain] [paramIndex=value ...]
// GPL-3.0-or-later.
#include "plugin.hpp"
#include <cstdio>
#include <vector>

Plugin *pluginInstance = nullptr;

static void writeWav(const char *path, const std::vector<float> &x, float sr)
{
    FILE *f = std::fopen(path, "wb");
    const uint32_t n = x.size(), r = (uint32_t)sr, bytes = n * 4;
    auto w32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto w16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); w32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f); w32(16);
    w16(3); w16(1); w32(r); w32(r * 4); w16(4); w16(32); std::fwrite("data", 1, 4, f); w32(bytes);
    std::fwrite(x.data(), 4, n, f);
    std::fclose(f);
}

// Module::paramsFromJson needs APP->engine; set the values directly instead
static void loadModule(Module *mod, json_t *m)
{
    size_t i;
    json_t *p;
    json_array_foreach(json_object_get(m, "params"), i, p)
    {
        const size_t id = json_integer_value(json_object_get(p, "id"));
        if (id >= mod->params.size())
            continue; // like Module::paramsFromJson
        mod->params[id].setValue(json_number_value(json_object_get(p, "value")));
    }
    if (json_t *d = json_object_get(m, "data"))
        mod->dataFromJson(d);
}

int main(int argc, char **argv)
{
    const float sr = getenv("SR") ? std::atof(getenv("SR")) : 48000.f;
    const int note = std::atoi(argv[2]);
    Module *osc = modelObxfOscillator->createModule();
    Module *flt = modelObxfFilter->createModule();
    Module::SampleRateChangeEvent e;
    e.sampleRate = sr;
    e.sampleTime = 1 / sr;
    osc->onSampleRateChange(e);
    flt->onSampleRateChange(e);
    if (getenv("PATCHJSON"))
    {
        // load both modules exactly as Rack does from a patch file
        json_error_t err;
        json_t *patch = json_load_file(getenv("PATCHJSON"), 0, &err);
        json_t *mods = json_object_get(patch, "modules");
        size_t i;
        json_t *m;
        json_array_foreach(mods, i, m)
        {
            const char *model = json_string_value(json_object_get(m, "model"));
            if (!std::strcmp(model, "OBXmOscillator"))
                loadModule(osc, m);
            if (!std::strcmp(model, "OBXmFilter"))
                loadModule(flt, m);
        }
        for (int k = 0; k < 20; k++)
            std::printf("flt param %d = %.3f\n", k, flt->params[k].getValue());
    }
    for (int a = 3; a < argc; a++)
    {
        int idx;
        float v;
        char which;
        if (std::sscanf(argv[a], "%c%d=%f", &which, &idx, &v) == 3)
        {
            if (which == 'i') // patch an oscillator input with a constant voltage
            {
                osc->inputs[idx].channels = 1;
                osc->inputs[idx].setVoltage(v);
            }
            else
                (which == 'f' ? flt : osc)->params[idx].setValue(v);
        }
    }
    // V/OCT patched, mono
    osc->inputs[0].channels = 1;
    osc->inputs[0].setVoltage((note - 60) / 12.f);
    osc->outputs[0].channels = 1;
    flt->inputs[0].channels = 1; // IN
    flt->inputs[1].channels = 1; // GATE
    flt->inputs[1].setVoltage(10.f);
    flt->inputs[3].channels = 1;
    flt->inputs[3].setVoltage((note - 60) / 12.f);
    flt->outputs[0].channels = 1;

    Module::ProcessArgs args;
    args.sampleRate = sr;
    args.sampleTime = 1 / sr;
    std::vector<float> a, b;
    for (int i = 0; i < (int)sr * 2; i++)
    {
        args.frame = i;
        if (getenv("GATEPERIOD"))
        {
            // toggling gate: high for the first half of each period
            const int per = std::atoi(getenv("GATEPERIOD"));
            flt->inputs[1].setVoltage((i % per) < per / 2 ? 10.f : 0.f);
        }
        osc->process(args);
        flt->inputs[0].setVoltage(osc->outputs[0].getVoltage());
        flt->process(args);
        a.push_back(osc->outputs[0].getVoltage());
        b.push_back(flt->outputs[0].getVoltage());
    }
    std::string base = argv[1];
    writeWav((base + "_osc.wav").c_str(), a, sr);
    writeWav((base + "_flt.wav").c_str(), b, sr);
    std::printf("osc channels out=%d, sample %f\n", osc->outputs[0].getChannels(), a[30000]);
    std::printf("osc lights:");
    for (size_t l = 0; l < osc->lights.size(); l++)
        std::printf(" %zu=%.0f", l, osc->lights[l].getBrightness());
    std::printf("\n");
    return 0;
}
