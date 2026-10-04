/*
 * OB-Xm — OBXm Filter: OB-Xf's multimode filter with its filter envelope.
 *
 * Parameter ranges, defaults and display units follow OB-Xf's
 * src/parameter/ParameterList.h (commit b08ffb6). DSP: src/dsp/FilterEngine.hpp.
 *
 * CV conventions:
 *   CUTOFF   1 V/oct (12 semitones per volt) at full attenuverter
 *   RES, ENV AMT, MODE   10 V = the knob's full range, added to the knob
 *   MODE with XPANDER on: 0..10 V spread over the 15 Xpander modes, round(V / 10 * 14),
 *            i.e. one mode every 0.71 V (OB-Xf processFilterXpanderMode)
 *   VEL      0..10 V, read on the gate's rising edge (1 when unpatched)
 *   ENV out  0..10 V (negative when INVERT is on)
 *
 * Copyright (C) 2026 OB-Xm contributors.
 * Released under the GNU General Public Licence v3 or later (GPL-3.0-or-later).
 * Based on OB-Xf, https://github.com/surge-synthesizer/OB-Xf
 */
#include "plugin.hpp"
#include "ObxfComponents.hpp"
#include "dsp/FilterEngine.hpp"

#include <memory>

using namespace obxfvcv;

namespace
{

// OB-Xf's Xpander mode names (ParameterList.h FilterXpanderMode)
const char *const XPANDER_NAMES[15] = {"LP4", "LP3", "LP2", "LP1", "HP3", "HP2", "HP1", "BP4",
                                       "BP2", "N2", "PH3", "HP2+LP1", "HP3+LP1", "N2+LP1",
                                       "PH3+LP1"};
// The same names as OB-Xf's 7-segment MODE display (assets menu-xpander.svg)
const char *const XPANDER_SHORT[15] = {"L4", "L3", "L2", "L1", "H3", "H2", "H1", "B4",
                                       "B2", "N2", "P3", "H2L1", "H3L1", "N2L1", "P3L1"};

// Envelope times: logsc(v, 1, 60000, 900) ms (SynthEngine::processFilterEnvAttack, ...)
struct EnvTimeQuantity : ParamQuantity
{
    float getDisplayValue() override { return obxf::logsc(getValue(), 1.f, 60000.f, 900.f); }
    void setDisplayValue(float ms) override
    {
        ms = clamp(ms, 1.f, 60000.f);
        setValue(std::log((ms - 1.f) / 59999.f * 900.f + 1.f) / std::log(901.f));
    }
    std::string getDisplayValueString() override
    {
        const float ms = getDisplayValue();
        return ms >= 1000.f ? string::f("%.2f s", ms / 1000.f) : string::f("%.1f ms", ms);
    }
    std::string getUnit() override { return ""; }
};

struct ModeQuantity;

} // namespace

struct ObxfFilter : ObxmModule
{
    /* Fixed output gain, applied after the filter (so the filter is driven exactly as in
     * OB-Xf): one saw through an open filter = +-2.5 V. This leaves headroom for
     * resonance, both oscillators and unison before Rack's Audio module clips at +-10 V. */
    static constexpr float OUTPUT_GAIN = 0.5f;

    enum ParamId
    {
        CUTOFF_PARAM,
        RESONANCE_PARAM,
        ENV_AMOUNT_PARAM,
        KEYTRACK_PARAM,
        MODE_PARAM,
        FOUR_POLE_PARAM,
        XPANDER_PARAM,
        PUSH_PARAM,
        BP_BLEND_PARAM,
        INVERT_PARAM,
        ATTACK_PARAM,
        DECAY_PARAM,
        SUSTAIN_PARAM,
        RELEASE_PARAM,
        CURVE_PARAM,
        VELOCITY_PARAM,
        CUTOFF_ATT_PARAM,
        RES_ATT_PARAM,
        ENV_AMT_ATT_PARAM,
        MODE_ATT_PARAM,
        ATTACK_ATT_PARAM, // appended: earlier ids unchanged
        DECAY_ATT_PARAM,
        PARAMS_LEN
    };
    enum InputId
    {
        IN_INPUT,
        GATE_INPUT,
        VEL_INPUT,
        VOCT_INPUT,
        CUTOFF_INPUT,
        RES_INPUT,
        ENV_AMT_INPUT,
        MODE_INPUT,
        ATTACK_INPUT, // appended: earlier cables keep their ids
        DECAY_INPUT,
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

    std::unique_ptr<FilterEngine> engine;
    dsp::ClockDivider paramDivider;
    dsp::SchmittTrigger gateTrigger[MAX_CHANNELS];
    bool hq = false;
    float modeCv0 = 0.f; // channel 0 mode CV, for the display

    ObxfFilter();

    void onSampleRateChange(const SampleRateChangeEvent &e) override
    {
        engine->setSampleRate(e.sampleRate);
    }

    void updateSettings()
    {
        FilterSettings s;
        s.cutoff = params[CUTOFF_PARAM].getValue();
        s.resonance = params[RESONANCE_PARAM].getValue();
        s.mode = params[MODE_PARAM].getValue();
        s.envAmount = params[ENV_AMOUNT_PARAM].getValue();
        s.keytrack = params[KEYTRACK_PARAM].getValue();
        s.fourPole = params[FOUR_POLE_PARAM].getValue() > 0.5f;
        s.xpander = params[XPANDER_PARAM].getValue() > 0.5f;
        s.push2Pole = params[PUSH_PARAM].getValue() > 0.5f;
        s.bpBlend2Pole = params[BP_BLEND_PARAM].getValue() > 0.5f;
        s.invertEnv = params[INVERT_PARAM].getValue() > 0.5f;
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
        {
#ifndef METAMODULE
            engine->setHQ(hq);
#endif
            updateSettings();
        }
        engine->beginSample();

        const int channels = std::min(std::max(inputs[IN_INPUT].getChannels(), 1), MAX_CHANNELS);
        const float cutAtt = params[CUTOFF_ATT_PARAM].getValue();
        const float resAtt = params[RES_ATT_PARAM].getValue();
        const float envAtt = params[ENV_AMT_ATT_PARAM].getValue();
        const float modeAtt = params[MODE_ATT_PARAM].getValue();
        const float attackAtt = params[ATTACK_ATT_PARAM].getValue();
        const float decayAtt = params[DECAY_ATT_PARAM].getValue();
        const bool velPatched = inputs[VEL_INPUT].isConnected();

        for (int c = 0; c < channels; c++)
        {
            FilterModulation m;
            m.in = inputs[IN_INPUT].getVoltage(c);
            gateTrigger[c].process(inputs[GATE_INPUT].getPolyVoltage(c), 0.1f, 1.f);
            m.gate = gateTrigger[c].isHigh();
            m.velocity = velPatched ? clamp(inputs[VEL_INPUT].getPolyVoltage(c) * 0.1f, 0.f, 1.f) : 1.f;
            m.voct = inputs[VOCT_INPUT].getPolyVoltage(c);
            m.cutoff = inputs[CUTOFF_INPUT].getPolyVoltage(c) * 12.f * cutAtt;
            m.resonance = inputs[RES_INPUT].getPolyVoltage(c) * 0.1f * resAtt;
            m.envAmount = inputs[ENV_AMT_INPUT].getPolyVoltage(c) * 0.1f * envAtt;
            m.mode = inputs[MODE_INPUT].getPolyVoltage(c) * 0.1f * modeAtt;
            m.attack = inputs[ATTACK_INPUT].getPolyVoltage(c) * 0.1f * attackAtt;
            m.decay = inputs[DECAY_INPUT].getPolyVoltage(c) * 0.1f * decayAtt;
            if (c == 0)
                modeCv0 = m.mode;

            float env = 0.f;
            outputs[OUT_OUTPUT].setVoltage(engine->process(c, m, env) * OUTPUT_GAIN, c);
            outputs[ENV_OUTPUT].setVoltage(env * 10.f, c);
        }
        outputs[OUT_OUTPUT].setChannels(channels);
        outputs[ENV_OUTPUT].setChannels(channels);
    }

    // Name of the current filter mode, as OB-Xf would show it
    std::string modeName(bool shortName)
    {
        const float mode = clamp(params[MODE_PARAM].getValue() + modeCv0, 0.f, 1.f);
        if (params[XPANDER_PARAM].getValue() > 0.5f && params[FOUR_POLE_PARAM].getValue() > 0.5f)
        {
            const int i = FilterEngine::xpanderModeFor(mode);
            return shortName ? XPANDER_SHORT[i] : XPANDER_NAMES[i];
        }
        if (params[FOUR_POLE_PARAM].getValue() > 0.5f)
        {
            // Filter::apply4Pole morphs LP4 -> LP3 -> LP2 -> LP1
            static const char *const poles[4] = {"L4", "L3", "L2", "L1"};
            return poles[(int)std::round(mode * 3.f)];
        }
        // Filter::apply2Pole morphs LP -> HP (or LP -> BP -> HP with BP blend)
        if (params[BP_BLEND_PARAM].getValue() > 0.5f)
        {
            static const char *const m3[3] = {"L2", "B2", "H2"};
            return m3[(int)std::round(mode * 2.f)];
        }
        return mode < 0.5f ? "L2" : "H2";
    }

    json_t *dataToJson() override
    {
        json_t *root = ObxmModule::dataToJson();
        json_object_set_new(root, "hq", json_boolean(hq));
        return root;
    }

    void dataFromJson(json_t *root) override
    {
        ObxmModule::dataFromJson(root);
        if (json_t *j = json_object_get(root, "hq"))
            hq = json_boolean_value(j);
    }
};

namespace
{
struct ModeQuantity : ParamQuantity
{
    std::string getDisplayValueString() override
    {
        auto *m = dynamic_cast<ObxfFilter *>(module);
        const std::string pct = string::f("%.1f%%", getValue() * 100.f);
        return m ? pct + " (" + m->modeName(false) + ")" : pct;
    }
};
} // namespace

ObxfFilter::ObxfFilter() : engine(new FilterEngine)
{
    config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

    // FilterCutoff: -45..75 semitones around A4, displayed in Hz (440 * 2^(st/12))
    configParam(CUTOFF_PARAM, 0.f, 1.f, 1.f, "Cutoff", " Hz", 1024.f, 440.f * std::pow(2.f, -45.f / 12.f));
    configParam(RESONANCE_PARAM, 0.f, 1.f, 0.f, "Resonance", "%", 0.f, 100.f);
    configParam(ENV_AMOUNT_PARAM, 0.f, 1.f, 0.f, "Envelope amount", "%", 0.f, 100.f);
    configParam(KEYTRACK_PARAM, 0.f, 1.f, 0.f, "Keytrack", "%", 0.f, 100.f);
    configParam<ModeQuantity>(MODE_PARAM, 0.f, 1.f, 0.f, "Mode");
    configSwitch(FOUR_POLE_PARAM, 0.f, 1.f, 0.f, "4-pole", {"Off (2-pole)", "On"});
    configSwitch(XPANDER_PARAM, 0.f, 1.f, 0.f, "Xpander modes (4-pole)", {"Off", "On"});
    configSwitch(PUSH_PARAM, 0.f, 1.f, 0.f, "2-pole: push (self-oscillation)", {"Off", "On"});
    configSwitch(BP_BLEND_PARAM, 0.f, 1.f, 0.f, "2-pole: band-pass blend", {"Off", "On"});
    configSwitch(INVERT_PARAM, 0.f, 1.f, 0.f, "Invert envelope", {"Off", "On"});

    configParam<EnvTimeQuantity>(ATTACK_PARAM, 0.f, 1.f, 0.f, "Attack");
    configParam<EnvTimeQuantity>(DECAY_PARAM, 0.f, 1.f, 0.f, "Decay");
    configParam(SUSTAIN_PARAM, 0.f, 1.f, 1.f, "Sustain", "%", 0.f, 100.f);
    configParam<EnvTimeQuantity>(RELEASE_PARAM, 0.f, 1.f, 0.f, "Release");
    configParam(CURVE_PARAM, 0.f, 1.f, 0.f, "Attack curve (exponential .. linear)", "%", 0.f, 100.f);
    configParam(VELOCITY_PARAM, 0.f, 1.f, 0.f, "Velocity to envelope", "%", 0.f, 100.f);

    configParam(CUTOFF_ATT_PARAM, -1.f, 1.f, 0.f, "Cutoff CV attenuverter", "%", 0.f, 100.f);
    configParam(RES_ATT_PARAM, -1.f, 1.f, 0.f, "Resonance CV attenuverter", "%", 0.f, 100.f);
    configParam(ENV_AMT_ATT_PARAM, -1.f, 1.f, 0.f, "Envelope amount CV attenuverter", "%", 0.f, 100.f);
    configParam(MODE_ATT_PARAM, -1.f, 1.f, 0.f, "Mode CV attenuverter", "%", 0.f, 100.f);
    configParam(ATTACK_ATT_PARAM, -1.f, 1.f, 0.f, "Attack CV attenuverter", "%", 0.f, 100.f);
    configParam(DECAY_ATT_PARAM, -1.f, 1.f, 0.f, "Decay CV attenuverter", "%", 0.f, 100.f);

    configInput(IN_INPUT, "Audio (polyphonic: sets the voice count)");
    configInput(GATE_INPUT, "Gate");
    configInput(VEL_INPUT, "Velocity (0..10 V)");
    configInput(VOCT_INPUT, "Keytrack V/Oct");
    configInput(CUTOFF_INPUT, "Cutoff CV (1 V/oct)");
    configInput(RES_INPUT, "Resonance CV (10 V = full range)");
    configInput(ENV_AMT_INPUT, "Envelope amount CV (10 V = full range)");
    configInput(MODE_INPUT, "Mode CV (10 V = full range / 15 Xpander modes)");
    configInput(ATTACK_INPUT, "Envelope attack CV (added to the knob, 10 V = full range)");
    configInput(DECAY_INPUT, "Envelope decay CV (added to the knob, 10 V = full range)");
    configOutput(OUT_OUTPUT, "Audio");
    configOutput(ENV_OUTPUT, "Envelope (0..10 V)");
    configBypass(IN_INPUT, OUT_OUTPUT);

    paramDivider.setDivision(16);
    engine->setSampleRate(48000.f);
    updateSettings();
}

struct ModeDisplay : obxfui::SevenSegmentDisplay
{
    ObxfFilter *module = nullptr;
    std::string getText() override { return module ? module->modeName(true) : "L2"; }
};

struct ObxfFilterWidget : ObxmModuleWidget
{
    ObxfFilterWidget(ObxfFilter *module)
    {
        using namespace obxfui;
        namespace L = layout::filter;
        setModule(module);
        setThemedPanel("ObxfFilter");

        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewBlack>(
            Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        using M = ObxfFilter;
        addParam(createParamCentered<ObxfSlimButton>(mm(L::FOUR_POLE), module, M::FOUR_POLE_PARAM));
        addParam(createParamCentered<ObxfKnob>(mm(L::CUTOFF), module, M::CUTOFF_PARAM));
        addParam(createParamCentered<ObxfKnob>(mm(L::RESONANCE), module, M::RESONANCE_PARAM));
        addParam(createParamCentered<ObxfKnob>(mm(L::ENV_AMOUNT), module, M::ENV_AMOUNT_PARAM));
        addParam(createParamCentered<ObxfKnob>(mm(L::KEYTRACK), module, M::KEYTRACK_PARAM));
        addParam(createParamCentered<ObxfKnob>(mm(L::MODE), module, M::MODE_PARAM));
        addParam(createParamCentered<ObxfSlimButton>(mm(L::PUSH), module, M::PUSH_PARAM));
        addParam(createParamCentered<ObxfSlimButton>(mm(L::BP_BLEND), module, M::BP_BLEND_PARAM));
        addParam(createParamCentered<ObxfButton>(mm(L::XPANDER), module, M::XPANDER_PARAM));

        auto *display = new ModeDisplay;
        display->module = module;
        display->digits = 4;
        placeDisplay(display, L::MODE_DISPLAY);
        addChild(display);

        addParam(createParamCentered<ObxfSlimButton>(mm(L::INVERT), module, M::INVERT_PARAM));
        addParam(createParamCentered<ObxfKnob>(mm(L::ATTACK), module, M::ATTACK_PARAM));
        addParam(createParamCentered<ObxfKnob>(mm(L::DECAY), module, M::DECAY_PARAM));
        addParam(createParamCentered<ObxfKnob>(mm(L::SUSTAIN), module, M::SUSTAIN_PARAM));
        addParam(createParamCentered<ObxfKnob>(mm(L::RELEASE), module, M::RELEASE_PARAM));
        addParam(createParamCentered<ObxfSlider>(mm(L::CURVE), module, M::CURVE_PARAM));
        addParam(createParamCentered<ObxfSlider>(mm(L::VELOCITY), module, M::VELOCITY_PARAM));

        addParam(createParamCentered<ObxfTrimpot>(mm(L::CUTOFF_ATT), module, M::CUTOFF_ATT_PARAM));
        addParam(createParamCentered<ObxfTrimpot>(mm(L::RES_ATT), module, M::RES_ATT_PARAM));
        addParam(createParamCentered<ObxfTrimpot>(mm(L::ENV_AMT_ATT), module, M::ENV_AMT_ATT_PARAM));
        addParam(createParamCentered<ObxfTrimpot>(mm(L::MODE_ATT), module, M::MODE_ATT_PARAM));
        addParam(createParamCentered<ObxfTrimpot>(mm(L::ATTACK_ATT), module, M::ATTACK_ATT_PARAM));
        addParam(createParamCentered<ObxfTrimpot>(mm(L::DECAY_ATT), module, M::DECAY_ATT_PARAM));
        addInput(createInputCentered<PJ301MPort>(mm(L::CUTOFF_INPUT), module, M::CUTOFF_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm(L::RES_INPUT), module, M::RES_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm(L::ENV_AMT_INPUT), module, M::ENV_AMT_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm(L::MODE_INPUT), module, M::MODE_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm(L::ATTACK_INPUT), module, M::ATTACK_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm(L::DECAY_INPUT), module, M::DECAY_INPUT));

        addInput(createInputCentered<PJ301MPort>(mm(L::IN_INPUT), module, M::IN_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm(L::GATE_INPUT), module, M::GATE_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm(L::VEL_INPUT), module, M::VEL_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm(L::VOCT_INPUT), module, M::VOCT_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm(L::OUT_OUTPUT), module, M::OUT_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm(L::ENV_OUTPUT), module, M::ENV_OUTPUT));
    }

    void appendContextMenu(Menu *menu) override
    {
#ifndef METAMODULE
        auto *module = getModule<ObxfFilter>();
        menu->addChild(new MenuSeparator);
        menu->addChild(createBoolPtrMenuItem("HQ (2x oversampling)", "", &module->hq));
#endif
        appendThemeMenu(menu);
    }
};

Model *modelObxfFilter = createModel<ObxfFilter, ObxfFilterWidget>("OBXmFilter");
