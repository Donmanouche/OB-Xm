/*
 * OB-Xm — OBXm Oscillator: OB-Xf's two oscillators, mixer and unison.
 *
 * Parameter ranges, defaults and display units follow OB-Xf's
 * src/parameter/ParameterList.h (commit b08ffb6). DSP: src/dsp/OscEngine.hpp.
 *
 * Copyright (C) 2026 OB-Xm contributors.
 * Released under the GNU General Public Licence v3 or later (GPL-3.0-or-later).
 * Based on OB-Xf, https://github.com/surge-synthesizer/OB-Xf
 */
#include "plugin.hpp"
#include "ObxfComponents.hpp"
#include "dsp/OscEngine.hpp"

#include <memory>

using namespace obxfvcv;

namespace
{

// Osc2Detune: logsc(v, 0.001, 0.6) semitones, shown in cents (ParameterList.h)
struct DetuneQuantity : ParamQuantity
{
    float getDisplayValue() override { return obxf::logsc(getValue(), 0.001f, 0.6f) * 100.f; }
    void setDisplayValue(float cents) override
    {
        const float st = clamp(cents / 100.f, 0.001f, 0.6f);
        setValue(std::log((st - 0.001f) / (0.6f - 0.001f) * 19.f + 1.f) / std::log(20.f));
    }
};

// 1 unison voice means unison off
struct UnisonVoicesQuantity : ParamQuantity
{
    std::string getDisplayValueString() override
    {
        const int v = (int)std::round(getValue());
        return v <= 1 ? "1 (off)" : std::to_string(v);
    }
};

} // namespace

struct ObxfOscillator : ObxmModule
{
    enum ParamId
    {
        OSC1_PITCH_PARAM,
        OSC2_PITCH_PARAM,
        DETUNE_PARAM,
        PW_PARAM,
        OSC2_PW_OFFSET_PARAM,
        ENV_PITCH_PARAM,
        ENV_PW_PARAM,
        CROSSMOD_PARAM,
        BRIGHT_PARAM,
        OSC1_VOL_PARAM,
        OSC2_VOL_PARAM,
        RING_VOL_PARAM,
        NOISE_VOL_PARAM,
        NOISE_COLOR_PARAM,
        TRANSPOSE_PARAM,
        TUNE_PARAM,
        UNISON_VOICES_PARAM,
        UNISON_DETUNE_PARAM,
        SAW1_PARAM,
        PULSE1_PARAM,
        SAW2_PARAM,
        PULSE2_PARAM,
        SYNC_PARAM,
        OSC2_KEYTRACK_PARAM,
        ENV_PITCH_BOTH_PARAM,
        ENV_PITCH_INV_PARAM,
        ENV_PW_BOTH_PARAM,
        ENV_PW_INV_PARAM,
        OSC2_PW_OFFSET_ATT_PARAM,
        PARAMS_LEN
    };
    enum InputId
    {
        VOCT_INPUT,
        ENV_PITCH_INPUT,
        ENV_PW_INPUT,
        OSC2_PW_OFFSET_INPUT,
        // appended after 2.0.5's inputs, so existing cables keep their ids
        WAVE1_INPUT,
        WAVE2_INPUT,
        OSC1_VOL_INPUT,
        OSC2_VOL_INPUT,
        RING_VOL_INPUT,
        INPUTS_LEN
    };
    enum OutputId
    {
        OUT_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId
    {
        SAW1_LIGHT,
        PULSE1_LIGHT,
        SAW2_LIGHT,
        PULSE2_LIGHT,
        LIGHTS_LEN
    };

    std::unique_ptr<OscEngine> engine;
    dsp::ClockDivider paramDivider;
    bool hq = false; // OB-Xf "HQ" (2x oversampling); context menu
    // OB-Xf snaps OSC 1/2 pitch to semitones with a modifier drag; here it is the
    // default, so intervals are exact (context menu to turn it off)
    bool snapPitch = true;

    ObxfOscillator() : engine(new OscEngine)
    {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        configParam(OSC1_PITCH_PARAM, -24.f, 24.f, 0.f, "Osc 1 pitch", " semitones");
        configParam(OSC2_PITCH_PARAM, -24.f, 24.f, 0.f, "Osc 2 pitch", " semitones");
        configParam<DetuneQuantity>(DETUNE_PARAM, 0.f, 1.f, 0.f, "Osc 2 detune", " cents");
        configParam(PW_PARAM, 0.f, 1.f, 0.f, "Pulse width", "%", 0.f, 47.5f, 50.f);
        configParam(OSC2_PW_OFFSET_PARAM, 0.f, 1.f, 0.f, "Osc 2 PW offset", "%", 0.f, 47.5f);
        configParam(ENV_PITCH_PARAM, 0.f, 1.f, 0.f, "Envelope to pitch", " semitones", 0.f, 36.f);
        configParam(ENV_PW_PARAM, 0.f, 1.f, 0.f, "Envelope to pulse width", "%", 0.f, 100.f);
        configParam(CROSSMOD_PARAM, 0.f, 1.f, 0.f, "Cross modulation", "%", 0.f, 100.f);
        configParam(BRIGHT_PARAM, 0.f, 1.f, 1.f, "Brightness", "%", 0.f, 100.f);

        configParam(OSC1_VOL_PARAM, 0.f, 1.f, 1.f, "Osc 1 level", "%", 0.f, 100.f);
        configParam(OSC2_VOL_PARAM, 0.f, 1.f, 0.f, "Osc 2 level", "%", 0.f, 100.f);
        configParam(RING_VOL_PARAM, 0.f, 1.f, 0.f, "Ring modulator level", "%", 0.f, 100.f);
        configParam(NOISE_VOL_PARAM, 0.f, 1.f, 0.f, "Noise level", "%", 0.f, 100.f);
        configSwitch(NOISE_COLOR_PARAM, 0.f, 2.f, 0.f, "Noise color", {"White", "Pink", "Red"});

        configParam(TRANSPOSE_PARAM, -24.f, 24.f, 0.f, "Transpose", " semitones");
        getParamQuantity(TRANSPOSE_PARAM)->snapEnabled = true;
        configParam(TUNE_PARAM, -100.f, 100.f, 0.f, "Tune", " cents");
        configParam<UnisonVoicesQuantity>(UNISON_VOICES_PARAM, 1.f, (float)MAX_UNISON, 1.f,
                                          "Unison voices");
        getParamQuantity(UNISON_VOICES_PARAM)->snapEnabled = true;
        configParam(UNISON_DETUNE_PARAM, 0.f, 1.f, 0.25f, "Unison detune", "%", 0.f, 100.f);

        configSwitch(SAW1_PARAM, 0.f, 1.f, 1.f, "Osc 1 sawtooth", {"Off", "On"});
        configSwitch(PULSE1_PARAM, 0.f, 1.f, 0.f, "Osc 1 pulse", {"Off", "On"});
        configSwitch(SAW2_PARAM, 0.f, 1.f, 1.f, "Osc 2 sawtooth", {"Off", "On"});
        configSwitch(PULSE2_PARAM, 0.f, 1.f, 0.f, "Osc 2 pulse", {"Off", "On"});
        configSwitch(SYNC_PARAM, 0.f, 1.f, 0.f, "Osc 2 hard sync", {"Off", "On"});
        configSwitch(OSC2_KEYTRACK_PARAM, 0.f, 1.f, 1.f, "Osc 2 keytrack", {"Off", "On"});
        configSwitch(ENV_PITCH_BOTH_PARAM, 0.f, 1.f, 1.f, "Envelope to pitch: target",
                     {"Osc 2", "Osc 1+2"});
        configSwitch(ENV_PITCH_INV_PARAM, 0.f, 1.f, 0.f, "Envelope to pitch: invert", {"Off", "On"});
        configSwitch(ENV_PW_BOTH_PARAM, 0.f, 1.f, 1.f, "Envelope to PW: target",
                     {"Osc 2", "Osc 1+2"});
        configSwitch(ENV_PW_INV_PARAM, 0.f, 1.f, 0.f, "Envelope to PW: invert", {"Off", "On"});
        configParam(OSC2_PW_OFFSET_ATT_PARAM, -1.f, 1.f, 0.f, "Osc 2 PW offset CV attenuverter",
                    "%", 0.f, 100.f);

        configInput(VOCT_INPUT, "V/Oct (polyphonic: sets the voice count)");
        configInput(ENV_PITCH_INPUT, "Envelope to pitch (0..10 V)");
        configInput(ENV_PW_INPUT, "Envelope to pulse width (0..10 V)");
        configInput(OSC2_PW_OFFSET_INPUT, "Osc 2 PW offset CV (10 V = full range)");
        configInput(WAVE1_INPUT, "Osc 1 waveform (0-2.5 V triangle, 2.5-5 saw, 5-7.5 pulse, 7.5-10 saw+pulse; overrides the buttons)");
        configInput(WAVE2_INPUT, "Osc 2 waveform (0-2.5 V triangle, 2.5-5 saw, 5-7.5 pulse, 7.5-10 saw+pulse; overrides the buttons)");
        configInput(OSC1_VOL_INPUT, "Osc 1 level CV (added to the knob, 10 V = full range)");
        configInput(OSC2_VOL_INPUT, "Osc 2 level CV (added to the knob, 10 V = full range)");
        configInput(RING_VOL_INPUT, "Ring modulator level CV (added to the knob, 10 V = full range)");
        configOutput(OUT_OUTPUT, "Audio");

        paramDivider.setDivision(16);
        for (auto &row : waveSel)
            for (int &w : row)
                w = -1;
        engine->setSampleRate(48000.f);
        applyPitchSnap();
        updateSettings();
    }

    void applyPitchSnap()
    {
        getParamQuantity(OSC1_PITCH_PARAM)->snapEnabled = snapPitch;
        getParamQuantity(OSC2_PITCH_PARAM)->snapEnabled = snapPitch;
    }

    void onSampleRateChange(const SampleRateChangeEvent &e) override
    {
        engine->setSampleRate(e.sampleRate);
    }

    void updateSettings()
    {
        OscSettings s;
        s.osc1Pitch = params[OSC1_PITCH_PARAM].getValue();
        s.osc2Pitch = params[OSC2_PITCH_PARAM].getValue();
        s.detune = params[DETUNE_PARAM].getValue();
        s.pw = params[PW_PARAM].getValue();
        s.osc2PWOffset = params[OSC2_PW_OFFSET_PARAM].getValue();
        s.envToPitch = params[ENV_PITCH_PARAM].getValue();
        s.envToPW = params[ENV_PW_PARAM].getValue();
        s.crossmod = params[CROSSMOD_PARAM].getValue();
        s.brightness = params[BRIGHT_PARAM].getValue();
        s.osc1Vol = params[OSC1_VOL_PARAM].getValue();
        s.osc2Vol = params[OSC2_VOL_PARAM].getValue();
        s.ringModVol = params[RING_VOL_PARAM].getValue();
        s.noiseVol = params[NOISE_VOL_PARAM].getValue();
        s.noiseColor = (int)std::round(params[NOISE_COLOR_PARAM].getValue());
        s.transpose = (int)std::round(params[TRANSPOSE_PARAM].getValue());
        s.tune = params[TUNE_PARAM].getValue();
        s.unisonVoices = (int)std::round(params[UNISON_VOICES_PARAM].getValue());
        s.unisonDetune = params[UNISON_DETUNE_PARAM].getValue();
        s.saw1 = params[SAW1_PARAM].getValue() > 0.5f;
        s.pulse1 = params[PULSE1_PARAM].getValue() > 0.5f;
        s.saw2 = params[SAW2_PARAM].getValue() > 0.5f;
        s.pulse2 = params[PULSE2_PARAM].getValue() > 0.5f;
        s.sync = params[SYNC_PARAM].getValue() > 0.5f;
        s.osc2Keytrack = params[OSC2_KEYTRACK_PARAM].getValue() > 0.5f;
        s.envPitchBothOscs = params[ENV_PITCH_BOTH_PARAM].getValue() > 0.5f;
        s.envPitchInvert = params[ENV_PITCH_INV_PARAM].getValue() > 0.5f;
        s.envPWBothOscs = params[ENV_PW_BOTH_PARAM].getValue() > 0.5f;
        s.envPWInvert = params[ENV_PW_INV_PARAM].getValue() > 0.5f;
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

        const int channels = std::min(std::max(inputs[VOCT_INPUT].getChannels(), 1), MAX_CHANNELS);
        const float offAtt = params[OSC2_PW_OFFSET_ATT_PARAM].getValue();

        for (int c = 0; c < channels; c++)
        {
            OscModulation m;
            m.voct = inputs[VOCT_INPUT].getVoltage(c);
            m.envPitch = inputs[ENV_PITCH_INPUT].getPolyVoltage(c) * 0.1f;
            m.envPW = inputs[ENV_PW_INPUT].getPolyVoltage(c) * 0.1f;
            m.osc2PWOffset = inputs[OSC2_PW_OFFSET_INPUT].getPolyVoltage(c) * 0.1f * offAtt;
            m.wave1 = waveFromCv(WAVE1_INPUT, 0, c);
            m.wave2 = waveFromCv(WAVE2_INPUT, 1, c);
            m.osc1Vol = inputs[OSC1_VOL_INPUT].getPolyVoltage(c) * 0.1f;
            m.osc2Vol = inputs[OSC2_VOL_INPUT].getPolyVoltage(c) * 0.1f;
            m.ringModVol = inputs[RING_VOL_INPUT].getPolyVoltage(c) * 0.1f;
            outputs[OUT_OUTPUT].setVoltage(engine->process(c, channels, m), c);
        }
        outputs[OUT_OUTPUT].setChannels(channels);

        /* Waveform LEDs show what channel 0 actually plays: the WAVE CV when patched,
         * the buttons otherwise. */
        const int w1 = waveSel[0][0] >= 0 ? waveSel[0][0]
                                           : (params[SAW1_PARAM].getValue() > 0.5f) |
                                                 ((params[PULSE1_PARAM].getValue() > 0.5f) << 1);
        const int w2 = waveSel[1][0] >= 0 ? waveSel[1][0]
                                           : (params[SAW2_PARAM].getValue() > 0.5f) |
                                                 ((params[PULSE2_PARAM].getValue() > 0.5f) << 1);
        lights[SAW1_LIGHT].setBrightness(w1 & 1);
        lights[PULSE1_LIGHT].setBrightness((w1 >> 1) & 1);
        lights[SAW2_LIGHT].setBrightness(w2 & 1);
        lights[PULSE2_LIGHT].setBrightness((w2 >> 1) & 1);
    }

    /* WAVE inputs: 0-10 V in four 2.5 V bands -> triangle, saw, pulse, saw+pulse
     * (bit 0 = saw, bit 1 = pulse). A 0.1 V hysteresis keeps a voltage sitting on a
     * boundary from flipping between two waveforms. Unpatched: the buttons decide. */
    int waveSel[2][MAX_CHANNELS];

    int waveFromCv(int input, int osc, int c)
    {
        if (!inputs[input].isConnected())
        {
            waveSel[osc][c] = -1;
            return -1;
        }
        const float v = clamp(inputs[input].getPolyVoltage(c), 0.f, 10.f);
        int &sel = waveSel[osc][c];
        const float hyst = 0.1f;
        if (sel < 0 || v < sel * 2.5f - hyst || v > (sel + 1) * 2.5f + hyst)
            sel = std::min(3, (int)(v / 2.5f));
        return sel;
    }

    json_t *dataToJson() override
    {
        json_t *root = ObxmModule::dataToJson();
        json_object_set_new(root, "hq", json_boolean(hq));
        json_object_set_new(root, "snapPitch", json_boolean(snapPitch));
        return root;
    }

    void dataFromJson(json_t *root) override
    {
        ObxmModule::dataFromJson(root);
        if (json_t *j = json_object_get(root, "hq"))
            hq = json_boolean_value(j);
        if (json_t *j = json_object_get(root, "snapPitch"))
            snapPitch = json_boolean_value(j);
        applyPitchSnap();
    }
};

struct VoicesDisplay : obxfui::SevenSegmentDisplay
{
    ObxfOscillator *module = nullptr;
    std::string getText() override
    {
        const float v = module ? module->params[ObxfOscillator::UNISON_VOICES_PARAM].getValue() : 1.f;
        return std::to_string((int)std::round(v));
    }
};

struct ObxfOscillatorWidget : ObxmModuleWidget
{
    ObxfOscillatorWidget(ObxfOscillator *module)
    {
        using namespace obxfui;
        namespace L = layout::osc;
        setModule(module);
        setThemedPanel("ObxfOsc");

        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewBlack>(
            Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        using M = ObxfOscillator;
        addParam(createParamCentered<ObxfKnob>(mm(L::OSC1_PITCH), module, M::OSC1_PITCH_PARAM));
        addParam(createParamCentered<ObxfKnob>(mm(L::DETUNE), module, M::DETUNE_PARAM));
        addParam(createParamCentered<ObxfKnob>(mm(L::OSC2_PITCH), module, M::OSC2_PITCH_PARAM));
        auto waveButton = [&](layout::Mm pos, int param, int light, bool defaultOn) {
            addParam(createParamCentered<ObxfLedButton>(mm(pos), module, param));
            auto *led = createLightCentered<ObxfButtonLed>(
                mm2px(Vec(pos.x, pos.y + BUTTON_LED_DY)), module, light);
            led->previewOn = defaultOn;
            addChild(led);
        };
        waveButton(L::SAW1, M::SAW1_PARAM, M::SAW1_LIGHT, true);
        waveButton(L::PULSE1, M::PULSE1_PARAM, M::PULSE1_LIGHT, false);
        addParam(createParamCentered<ObxfKnob>(mm(L::PW), module, M::PW_PARAM));
        waveButton(L::SAW2, M::SAW2_PARAM, M::SAW2_LIGHT, true);
        waveButton(L::PULSE2, M::PULSE2_PARAM, M::PULSE2_LIGHT, false);
        addParam(createParamCentered<ObxfSlimButton>(mm(L::ENV_PITCH_BOTH), module, M::ENV_PITCH_BOTH_PARAM));
        addParam(createParamCentered<ObxfSlimButton>(mm(L::ENV_PITCH_INV), module, M::ENV_PITCH_INV_PARAM));
        addParam(createParamCentered<ObxfSlimButton>(mm(L::ENV_PW_INV), module, M::ENV_PW_INV_PARAM));
        addParam(createParamCentered<ObxfSlimButton>(mm(L::ENV_PW_BOTH), module, M::ENV_PW_BOTH_PARAM));
        addParam(createParamCentered<ObxfKnob>(mm(L::ENV_PITCH), module, M::ENV_PITCH_PARAM));
        addParam(createParamCentered<ObxfKnob>(mm(L::OSC2_PW_OFFSET), module, M::OSC2_PW_OFFSET_PARAM));
        addParam(createParamCentered<ObxfKnob>(mm(L::ENV_PW), module, M::ENV_PW_PARAM));
        addParam(createParamCentered<ObxfKnob>(mm(L::CROSSMOD), module, M::CROSSMOD_PARAM));
        addParam(createParamCentered<ObxfButton>(mm(L::SYNC), module, M::SYNC_PARAM));
        addParam(createParamCentered<ObxfButton>(mm(L::OSC2_KEYTRACK), module, M::OSC2_KEYTRACK_PARAM));
        addParam(createParamCentered<ObxfKnob>(mm(L::BRIGHT), module, M::BRIGHT_PARAM));

        addParam(createParamCentered<ObxfKnob>(mm(L::OSC1_VOL), module, M::OSC1_VOL_PARAM));
        addParam(createParamCentered<ObxfKnob>(mm(L::OSC2_VOL), module, M::OSC2_VOL_PARAM));
        addParam(createParamCentered<ObxfKnob>(mm(L::RING_VOL), module, M::RING_VOL_PARAM));
        addParam(createParamCentered<ObxfKnob>(mm(L::NOISE_VOL), module, M::NOISE_VOL_PARAM));
        addParam(createParamCentered<ObxfNoiseButton>(mm(L::NOISE_COLOR), module, M::NOISE_COLOR_PARAM));

        addParam(createParamCentered<ObxfSnapKnob>(mm(L::TRANSPOSE), module, M::TRANSPOSE_PARAM));
        addParam(createParamCentered<ObxfKnob>(mm(L::TUNE), module, M::TUNE_PARAM));
        addParam(createParamCentered<ObxfSnapKnob>(mm(L::UNISON_VOICES), module, M::UNISON_VOICES_PARAM));
        addParam(createParamCentered<ObxfKnob>(mm(L::UNISON_DETUNE), module, M::UNISON_DETUNE_PARAM));

        auto *voices = new VoicesDisplay;
        voices->module = module;
        voices->digits = 2;
        placeDisplay(voices, L::VOICES_DISPLAY);
        addChild(voices);

        addInput(createInputCentered<PJ301MPort>(mm(L::VOCT_INPUT), module, M::VOCT_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm(L::ENV_PITCH_INPUT), module, M::ENV_PITCH_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm(L::ENV_PW_INPUT), module, M::ENV_PW_INPUT));
        addParam(createParamCentered<ObxfTrimpot>(mm(L::OSC2_PW_OFFSET_ATT), module, M::OSC2_PW_OFFSET_ATT_PARAM));
        addInput(createInputCentered<PJ301MPort>(mm(L::OSC2_PW_OFFSET_INPUT), module, M::OSC2_PW_OFFSET_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm(L::WAVE1_INPUT), module, M::WAVE1_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm(L::WAVE2_INPUT), module, M::WAVE2_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm(L::OSC1_VOL_INPUT), module, M::OSC1_VOL_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm(L::OSC2_VOL_INPUT), module, M::OSC2_VOL_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm(L::RING_VOL_INPUT), module, M::RING_VOL_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm(L::OUT_OUTPUT), module, M::OUT_OUTPUT));
    }

    void appendContextMenu(Menu *menu) override
    {
        auto *module = getModule<ObxfOscillator>();
        menu->addChild(new MenuSeparator);
        menu->addChild(createBoolMenuItem(
            "Snap OSC 1 / OSC 2 pitch to semitones", "", [=]() { return module->snapPitch; },
            [=](bool on) {
                module->snapPitch = on;
                module->applyPitchSnap();
            }));
#ifndef METAMODULE
        menu->addChild(createBoolPtrMenuItem("HQ (2x oversampling)", "", &module->hq));
#endif
        menu->addChild(createMenuLabel(string::f("Unison: up to %d voices per channel", MAX_UNISON)));
#ifdef METAMODULE
        menu->addChild(createMenuLabel(string::f("MetaModule: channels x unison <= %d", MAX_OSC_BLOCKS)));
#endif
        appendThemeMenu(menu);
    }
};

Model *modelObxfOscillator = createModel<ObxfOscillator, ObxfOscillatorWidget>("OBXmOscillator");
