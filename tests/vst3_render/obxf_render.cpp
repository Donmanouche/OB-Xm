// Offline renderer for the installed OB-Xf VST3 (or any VST3 instrument), used to
// compare OB-Xm with the real plugin. Test tool only; builds against the
// Steinberg VST3 SDK (see tests/vst3_render/Makefile).
//
//   obxf_render <plugin.vst3> --list
//   obxf_render <plugin.vst3> <out.wav> <note> <seconds> <gate_seconds> [Param=value ...]
//
// Values are normalized 0..1, parameters are matched by title (case-sensitive).
// Output: 32-bit float WAV, mono (left channel), 48 kHz.
//
// GPL-3.0-or-later.
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "public.sdk/source/vst/utility/stringconvert.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

static const double SR = 48000.0;
static const int BLOCK = 64;

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
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: %s plugin.vst3 --list | out.wav note seconds gate [Param=v ...]\n", argv[0]);
        return 1;
    }
    auto *host = new HostApplication;
    PluginContextFactory::instance().setPluginContext(host);

    std::string err;
    auto module = VST3::Hosting::Module::create(argv[1], err);
    if (!module)
    {
        std::fprintf(stderr, "cannot load %s: %s\n", argv[1], err.c_str());
        return 1;
    }
    auto factory = module->getFactory();
    IPtr<PlugProvider> provider;
    for (auto &ci : factory.classInfos())
        if (ci.category() == kVstAudioEffectClass)
        {
            provider = owned(new PlugProvider(factory, ci, true));
            break;
        }
    if (!provider)
        return 1;
    provider->getComponent(); // instantiates and initializes the plug-in
    auto component = provider->getComponentPtr();
    if (!component)
    {
        std::fprintf(stderr, "cannot instantiate the plug-in\n");
        return 1;
    }
    auto controller = provider->getControllerPtr();
    FUnknownPtr<IAudioProcessor> proc(component);

    std::map<std::string, ParamID> ids;
    for (int32 i = 0; i < controller->getParameterCount(); i++)
    {
        ParameterInfo pi{};
        controller->getParameterInfo(i, pi);
        const std::string title = VST3::StringConvert::convert(pi.title);
        ids[title] = pi.id;
        if (argc == 3 && std::strcmp(argv[2], "--list") == 0)
        {
            String128 disp{};
            controller->getParamStringByValue(pi.id, controller->getParamNormalized(pi.id), disp);
            std::printf("%-28s %.4f  %s\n", title.c_str(), controller->getParamNormalized(pi.id),
                        VST3::StringConvert::convert(disp).c_str());
        }
    }
    if (argc == 3)
        return 0;

    const char *outPath = argv[2];
    const int note = std::atoi(argv[3]);
    const double seconds = std::atof(argv[4]);
    const double gate = std::atof(argv[5]);

    // optional note sequence: SEQ="note:start_s:dur_s,..."
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
            double st, d;
            if (std::sscanf(str.substr(p, e - p).c_str(), "%d:%lf:%lf", &n, &st, &d) == 3)
                seq.push_back({n, (int)(st * SR), (int)(d * SR)});
            p = e + 1;
        }
    }
    ParameterChanges changes;
    for (int a = 6; a < argc; a++)
    {
        std::string kv = argv[a];
        auto eq = kv.find('=');
        auto it = ids.find(kv.substr(0, eq));
        if (eq == std::string::npos || it == ids.end())
        {
            std::fprintf(stderr, "unknown parameter: %s\n", argv[a]);
            return 1;
        }
        const double v = std::atof(kv.c_str() + eq + 1);
        int32 idx = 0;
        changes.addParameterData(it->second, idx)->addPoint(0, v, idx);
        controller->setParamNormalized(it->second, v);
    }

    ProcessSetup setup{kOffline, kSample32, BLOCK, SR};
    proc->setupProcessing(setup);
    for (int32 b = 0; b < component->getBusCount(kAudio, kOutput); b++)
        component->activateBus(kAudio, kOutput, b, b == 0);
    if (component->getBusCount(kEvent, kInput) > 0)
        component->activateBus(kEvent, kInput, 0, true);
    component->setActive(true);
    proc->setProcessing(true);

    HostProcessData data;
    data.prepare(*component, BLOCK, kSample32);
    ProcessContext ctx{};
    ctx.sampleRate = SR;
    ctx.tempo = 120;
    ctx.state = ProcessContext::kPlaying | ProcessContext::kTempoValid;
    data.processContext = &ctx;
    EventList events;
    ParameterChanges none;
    data.inputEvents = &events;

    // Let the parameter changes and smoothers settle for 0.5 s before the note
    const int preroll = (int)(0.5 * SR) / BLOCK * BLOCK;
    const int total = preroll + (int)(seconds * SR);
    const int noteOn = preroll, noteOff = preroll + (int)(gate * SR);
    std::vector<float> out;
    out.reserve(total);
    for (int pos = 0; pos < total; pos += BLOCK)
    {
        events.clear();
        auto addNote = [&](int at, bool on, int pitch) {
            if (at < pos || at >= pos + BLOCK)
                return;
            Event e{};
            e.busIndex = 0;
            e.sampleOffset = at - pos;
            e.type = on ? Event::kNoteOnEvent : Event::kNoteOffEvent;
            if (on)
            {
                e.noteOn.pitch = pitch;
                e.noteOn.velocity = 1.f;
                e.noteOn.noteId = -1;
            }
            else
            {
                e.noteOff.pitch = pitch;
                e.noteOff.velocity = 0.f;
                e.noteOff.noteId = -1;
            }
            events.addEvent(e);
        };
        if (seq.empty())
        {
            addNote(noteOn, true, note);
            addNote(noteOff, false, note);
        }
        for (auto &q : seq)
        {
            addNote(preroll + q.start, true, q.note);
            addNote(preroll + q.start + q.len, false, q.note);
        }
        data.inputParameterChanges = pos == 0 ? &changes : &none;
        ctx.projectTimeSamples = pos;
        data.numSamples = BLOCK;
        proc->process(data);
        if (pos >= preroll)
            for (int i = 0; i < BLOCK; i++)
                out.push_back(data.outputs[0].channelBuffers32[0][i]);
    }
    proc->setProcessing(false);
    component->setActive(false);
    data.unprepare();
    writeWav(outPath, out);
    std::printf("wrote %s (%zu samples)\n", outPath, out.size());
    return 0;
}
