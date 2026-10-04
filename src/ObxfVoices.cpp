/*
 * OB-Xm — OBXm Voice Variation: the PAN and LEVELS controls of OB-Xf's
 * "Voice Variation" section, as a polyphonic-in / stereo-out module.
 *
 * Adapted from OB-Xf (https://github.com/surge-synthesizer/OB-Xf, commit b08ffb6):
 *   - src/engine/Motherboard.h  processSample(): vl += x * (1 - pannings[i % MAX_PANNINGS]),
 *                               vr += x * pannings[i % MAX_PANNINGS]
 *   - src/engine/Voice.h        level slop: sample * (1 - par.slop.level * slop.level),
 *                               slop.level = nextFloat() - 0.5, fixed per voice
 *   - src/engine/SynthEngine.h  processLevelSlop(): linsc(val, 0, 0.67)
 *
 * The other Voice Variation controls are not here: FILTERS and ENVELOPES act inside
 * OBXm Filter (fixed at OB-Xf's default, 25 %), OBXm Oscillator already applies the
 * default 25 % level slop (LEVELS here adds to it, so it defaults to 0), and there is
 * no glide in these modules.
 *
 * VCV Rack: 8 pans (voices 9-16 reuse pans 1-8, as OB-Xf's i % 8).
 * MetaModule: 4 pans, as cables carry at most 4 channels there.
 *
 * Copyright (C) 2026 OB-Xm contributors.
 * Released under the GNU General Public Licence v3 or later (GPL-3.0-or-later).
 */
#include "plugin.hpp"
#include "ObxfComponents.hpp"
#include "dsp/Common.hpp"

using namespace obxfvcv;

namespace
{
#ifdef METAMODULE
constexpr int NUM_PANS = 4;
#else
constexpr int NUM_PANS = 8; // OB-Xf configuration.h: MAX_PANNINGS
#endif

// Pan 0..1 shown as OB-Xf does: left / center / right
struct PanQuantity : ParamQuantity
{
    std::string getDisplayValueString() override
    {
        const float v = getValue();
        if (std::fabs(v - 0.5f) < 0.005f)
            return "Center";
        return string::f("%s %.0f%%", v < 0.5f ? "Left" : "Right", std::fabs(v - 0.5f) * 200.f);
    }
};
} // namespace

struct ObxfVoices : Module
{
    // Ids are shared by the 8-pan (Rack) and 4-pan (MetaModule) builds: the first ones match.
    enum ParamId
    {
        LEVELS_PARAM,
        ENUMS(PAN_PARAMS, NUM_PANS),
        PARAMS_LEN
    };
    enum InputId
    {
        IN_INPUT,
        ENUMS(PAN_INPUTS, NUM_PANS),
        INPUTS_LEN
    };
    enum OutputId
    {
        L_OUTPUT,
        R_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId
    {
        LIGHTS_LEN
    };

    // Voice.h: slop.level = juce::Random::nextFloat() - 0.5f, drawn once per voice
    float slopLevel[MAX_CHANNELS];

    ObxfVoices()
    {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        // Per-voice level variation, added to the one OBXm Oscillator already applies
        configParam(LEVELS_PARAM, 0.f, 1.f, 0.f, "Levels", "%", 0.f, 100.f);
        for (int i = 0; i < NUM_PANS; i++)
        {
            configParam<PanQuantity>(PAN_PARAMS + i, 0.f, 1.f, 0.5f, string::f("Voice %d pan", i + 1));
            configInput(PAN_INPUTS + i, string::f("Voice %d pan CV (added to the knob, 10 V = full range)", i + 1));
        }
        configInput(IN_INPUT, "Audio (polyphonic)");
        configOutput(L_OUTPUT, "Left");
        configOutput(R_OUTPUT, "Right");

        Rng rng(0xC2B2AE35u ^ static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this)));
        for (float &s : slopLevel)
            s = rng.bipolarHalf();
    }

    void process(const ProcessArgs &args) override
    {
        const int channels = std::min(inputs[IN_INPUT].getChannels(), MAX_CHANNELS);
        // SynthEngine::processLevelSlop: linsc(val, 0, 0.67)
        const float levelSlop = params[LEVELS_PARAM].getValue() * 0.67f;

        float pans[NUM_PANS];
        for (int i = 0; i < NUM_PANS; i++)
            pans[i] = clamp01(params[PAN_PARAMS + i].getValue() +
                              inputs[PAN_INPUTS + i].getVoltage() * 0.1f);

        float l = 0.f, r = 0.f;
        for (int c = 0; c < channels; c++)
        {
            const float x = inputs[IN_INPUT].getVoltage(c) * (1.f - levelSlop * slopLevel[c]);
            const float pan = pans[c % NUM_PANS]; // Motherboard: pannings[i % MAX_PANNINGS]
            l += x * (1.f - pan);
            r += x * pan;
        }
        outputs[L_OUTPUT].setVoltage(l);
        outputs[R_OUTPUT].setVoltage(r);
    }
};

struct ObxfVoicesWidget : ModuleWidget
{
    ObxfVoicesWidget(ObxfVoices *module)
    {
        using namespace obxfui;
#ifdef METAMODULE
        namespace L = layout::voices4;
        setPanel(createPanel(asset::plugin(pluginInstance, "res/ObxfVoices4.svg")));
        const layout::Mm pans[NUM_PANS] = {L::PAN_1, L::PAN_2, L::PAN_3, L::PAN_4};
        const layout::Mm cvs[NUM_PANS] = {L::PAN_CV_1, L::PAN_CV_2, L::PAN_CV_3, L::PAN_CV_4};
#else
        namespace L = layout::voices;
        setPanel(createPanel(asset::plugin(pluginInstance, "res/ObxfVoices.svg")));
        const layout::Mm pans[NUM_PANS] = {L::PAN_1, L::PAN_2, L::PAN_3, L::PAN_4,
                                           L::PAN_5, L::PAN_6, L::PAN_7, L::PAN_8};
        const layout::Mm cvs[NUM_PANS] = {L::PAN_CV_1, L::PAN_CV_2, L::PAN_CV_3, L::PAN_CV_4,
                                          L::PAN_CV_5, L::PAN_CV_6, L::PAN_CV_7, L::PAN_CV_8};
#endif
        setModule(module);

        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewBlack>(
            Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        using M = ObxfVoices;
        addParam(createParamCentered<ObxfKnob>(mm(L::LEVELS), module, M::LEVELS_PARAM));
        for (int i = 0; i < NUM_PANS; i++)
        {
            addParam(createParamCentered<ObxfKnob>(mm(pans[i]), module, M::PAN_PARAMS + i));
            addInput(createInputCentered<PJ301MPort>(mm(cvs[i]), module, M::PAN_INPUTS + i));
        }
        addInput(createInputCentered<PJ301MPort>(mm(L::IN_INPUT), module, M::IN_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm(L::L_OUTPUT), module, M::L_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm(L::R_OUTPUT), module, M::R_OUTPUT));
    }
};

Model *modelObxfVoices = createModel<ObxfVoices, ObxfVoicesWidget>("OBXmVoices");
