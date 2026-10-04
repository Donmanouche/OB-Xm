/*
 * OB-Xm — OBXm Amplifier: OB-Xf's amplifier envelope and linear VCA, polyphonic.
 *
 * Parameter ranges, defaults and display units follow OB-Xf's
 * src/parameter/ParameterList.h (commit b08ffb6). DSP: src/dsp/AmpEngine.hpp.
 *
 * CV conventions: ATTACK / DECAY / SUSTAIN / RELEASE CVs are added to their knobs,
 * 10 V = full range, through their attenuverters. VEL 0..10 V, read on the gate's
 * rising edge (1 when unpatched). ENV out 0..10 V.
 *
 * Copyright (C) 2026 OB-Xm contributors.
 * Released under the GNU General Public Licence v3 or later (GPL-3.0-or-later).
 * Based on OB-Xf, https://github.com/surge-synthesizer/OB-Xf
 */
#include "plugin.hpp"
#include "ObxfComponents.hpp"
#include "dsp/AmpEngine.hpp"

#include <memory>

using namespace obxfvcv;

namespace
{
// Envelope times as OB-Xf shows them (logsc curves of SynthEngine::processAmpEnv*)
template <float (*Curve)(float), int MinMs> struct AmpTimeQuantity : ParamQuantity
{
    float getDisplayValue() override { return Curve(getValue()); }
    void setDisplayValue(float ms) override
    {
        ms = clamp(ms, (float)MinMs, 60000.f);
        setValue(std::log((ms - MinMs) / (60000.f - MinMs) * 900.f + 1.f) / std::log(901.f));
    }
    std::string getDisplayValueString() override
    {
        const float ms = getDisplayValue();
        return ms >= 1000.f ? string::f("%.2f s", ms / 1000.f) : string::f("%.1f ms", ms);
    }
    std::string getUnit() override { return ""; }
};
using AttackQuantity = AmpTimeQuantity<AmpEngine::attackMs, 4>;
using DecayQuantity = AmpTimeQuantity<AmpEngine::decayMs, 4>;
using ReleaseQuantity = AmpTimeQuantity<AmpEngine::releaseMs, 8>;
} // namespace

struct ObxfAmp : Module
{
    enum ParamId
    {
        ATTACK_PARAM,
        DECAY_PARAM,
        SUSTAIN_PARAM,
        RELEASE_PARAM,
        CURVE_PARAM,
        VELOCITY_PARAM,
        ATTACK_ATT_PARAM,
        DECAY_ATT_PARAM,
        SUSTAIN_ATT_PARAM,
        RELEASE_ATT_PARAM,
        PARAMS_LEN
    };
    enum InputId
    {
        IN_INPUT,
        GATE_INPUT,
        VEL_INPUT,
        ATTACK_INPUT,
        DECAY_INPUT,
        SUSTAIN_INPUT,
        RELEASE_INPUT,
        INPUTS_LEN
    };
    enum OutputId
    {
        OUT_OUTPUT,
        ENV_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId
    {
        LIGHTS_LEN
    };

    std::unique_ptr<AmpEngine> engine;
    dsp::ClockDivider paramDivider;
    dsp::SchmittTrigger gateTrigger[MAX_CHANNELS];

    ObxfAmp() : engine(new AmpEngine)
    {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configParam<AttackQuantity>(ATTACK_PARAM, 0.f, 1.f, 0.f, "Attack");
        configParam<DecayQuantity>(DECAY_PARAM, 0.f, 1.f, 0.f, "Decay");
        configParam(SUSTAIN_PARAM, 0.f, 1.f, 1.f, "Sustain", "%", 0.f, 100.f);
        configParam<ReleaseQuantity>(RELEASE_PARAM, 0.f, 1.f, 0.f, "Release");
        configParam(CURVE_PARAM, 0.f, 1.f, 0.f, "Attack curve (exponential .. linear)", "%", 0.f, 100.f);
        configParam(VELOCITY_PARAM, 0.f, 1.f, 0.f, "Velocity to amplitude", "%", 0.f, 100.f);
        configParam(ATTACK_ATT_PARAM, -1.f, 1.f, 0.f, "Attack CV attenuverter", "%", 0.f, 100.f);
        configParam(DECAY_ATT_PARAM, -1.f, 1.f, 0.f, "Decay CV attenuverter", "%", 0.f, 100.f);
        configParam(SUSTAIN_ATT_PARAM, -1.f, 1.f, 0.f, "Sustain CV attenuverter", "%", 0.f, 100.f);
        configParam(RELEASE_ATT_PARAM, -1.f, 1.f, 0.f, "Release CV attenuverter", "%", 0.f, 100.f);

        configInput(IN_INPUT, "Audio (polyphonic)");
        configInput(GATE_INPUT, "Gate (polyphonic: one envelope per voice)");
        configInput(VEL_INPUT, "Velocity (0..10 V)");
        configInput(ATTACK_INPUT, "Attack CV (10 V = full range)");
        configInput(DECAY_INPUT, "Decay CV (10 V = full range)");
        configInput(SUSTAIN_INPUT, "Sustain CV (10 V = full range)");
        configInput(RELEASE_INPUT, "Release CV (10 V = full range)");
        configOutput(OUT_OUTPUT, "Audio");
        configOutput(ENV_OUTPUT, "Envelope (0..10 V)");
        configBypass(IN_INPUT, OUT_OUTPUT);

        paramDivider.setDivision(16);
        engine->setSampleRate(48000.f);
        updateSettings();
    }

    void onSampleRateChange(const SampleRateChangeEvent &e) override
    {
        engine->setSampleRate(e.sampleRate);
    }

    void updateSettings()
    {
        AmpSettings s;
        s.attack = params[ATTACK_PARAM].getValue();
        s.decay = params[DECAY_PARAM].getValue();
        s.sustain = params[SUSTAIN_PARAM].getValue();
        s.release = params[RELEASE_PARAM].getValue();
        s.attackCurve = params[CURVE_PARAM].getValue();
        s.velocity = params[VELOCITY_PARAM].getValue();
        engine->setSettings(s);
    }

    void process(const ProcessArgs &args) override
    {
        if (paramDivider.process())
            updateSettings();

        // One voice per channel of IN or GATE (a mono input can be shaped per voice)
        const int channels = std::min(
            std::max({inputs[IN_INPUT].getChannels(), inputs[GATE_INPUT].getChannels(), 1}),
            MAX_CHANNELS);
        const bool velPatched = inputs[VEL_INPUT].isConnected();
        const float attAtt = params[ATTACK_ATT_PARAM].getValue();
        const float decAtt = params[DECAY_ATT_PARAM].getValue();
        const float susAtt = params[SUSTAIN_ATT_PARAM].getValue();
        const float relAtt = params[RELEASE_ATT_PARAM].getValue();

        for (int c = 0; c < channels; c++)
        {
            AmpModulation m;
            m.in = inputs[IN_INPUT].getPolyVoltage(c);
            gateTrigger[c].process(inputs[GATE_INPUT].getPolyVoltage(c), 0.1f, 1.f);
            m.gate = gateTrigger[c].isHigh();
            m.velocity = velPatched ? clamp(inputs[VEL_INPUT].getPolyVoltage(c) * 0.1f, 0.f, 1.f) : 1.f;
            m.attack = inputs[ATTACK_INPUT].getPolyVoltage(c) * 0.1f * attAtt;
            m.decay = inputs[DECAY_INPUT].getPolyVoltage(c) * 0.1f * decAtt;
            m.sustain = inputs[SUSTAIN_INPUT].getPolyVoltage(c) * 0.1f * susAtt;
            m.release = inputs[RELEASE_INPUT].getPolyVoltage(c) * 0.1f * relAtt;

            float env = 0.f;
            outputs[OUT_OUTPUT].setVoltage(engine->process(c, m, env), c);
            outputs[ENV_OUTPUT].setVoltage(env * 10.f, c);
        }
        outputs[OUT_OUTPUT].setChannels(channels);
        outputs[ENV_OUTPUT].setChannels(channels);
    }
};

struct ObxfAmpWidget : ModuleWidget
{
    ObxfAmpWidget(ObxfAmp *module)
    {
        using namespace obxfui;
        namespace L = layout::amp;
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/ObxfAmp.svg")));

        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewBlack>(
            Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        using M = ObxfAmp;
        addParam(createParamCentered<ObxfKnob>(mm(L::ATTACK), module, M::ATTACK_PARAM));
        addParam(createParamCentered<ObxfKnob>(mm(L::DECAY), module, M::DECAY_PARAM));
        addParam(createParamCentered<ObxfKnob>(mm(L::SUSTAIN), module, M::SUSTAIN_PARAM));
        addParam(createParamCentered<ObxfKnob>(mm(L::RELEASE), module, M::RELEASE_PARAM));
        addParam(createParamCentered<ObxfSlider>(mm(L::CURVE), module, M::CURVE_PARAM));
        addParam(createParamCentered<ObxfSlider>(mm(L::VELOCITY), module, M::VELOCITY_PARAM));

        addParam(createParamCentered<ObxfTrimpot>(mm(L::ATTACK_ATT), module, M::ATTACK_ATT_PARAM));
        addParam(createParamCentered<ObxfTrimpot>(mm(L::DECAY_ATT), module, M::DECAY_ATT_PARAM));
        addParam(createParamCentered<ObxfTrimpot>(mm(L::SUSTAIN_ATT), module, M::SUSTAIN_ATT_PARAM));
        addParam(createParamCentered<ObxfTrimpot>(mm(L::RELEASE_ATT), module, M::RELEASE_ATT_PARAM));
        addInput(createInputCentered<PJ301MPort>(mm(L::ATTACK_INPUT), module, M::ATTACK_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm(L::DECAY_INPUT), module, M::DECAY_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm(L::SUSTAIN_INPUT), module, M::SUSTAIN_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm(L::RELEASE_INPUT), module, M::RELEASE_INPUT));

        addInput(createInputCentered<PJ301MPort>(mm(L::IN_INPUT), module, M::IN_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm(L::GATE_INPUT), module, M::GATE_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm(L::VEL_INPUT), module, M::VEL_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm(L::OUT_OUTPUT), module, M::OUT_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm(L::ENV_OUTPUT), module, M::ENV_OUTPUT));
    }
};

Model *modelObxfAmp = createModel<ObxfAmp, ObxfAmpWidget>("OBXmAmplifier");
