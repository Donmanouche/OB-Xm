/*
 * OB-Xm — panel components in OB-Xf's VectorTheme style.
 *
 * The SVGs in res/components/ are cut from OB-Xf's assets/binary/VectorTheme
 * (GPL-3.0-or-later) by tools/gen_panels.py.
 *
 * Copyright (C) 2026 OB-Xm contributors.
 * Released under the GNU General Public Licence v3 or later (GPL-3.0-or-later).
 */
#pragma once

#include "plugin.hpp"
#include "PanelLayout.hpp"

#include <string>

namespace obxfui
{

inline std::shared_ptr<window::Svg> svg(const char *name)
{
    return window::Svg::load(asset::plugin(pluginInstance, std::string("res/components/") + name));
}

inline math::Vec mm(layout::Mm p) { return mm2px(math::Vec(p.x, p.y)); }

/* OB-Xf knob: static body (layer 1) under a rotating cap with pointer (layer 2).
 * The MetaModule draws a knob as one rotating image, so it gets both layers merged. */
template <bool Small> struct ObxfKnobBase : app::SvgKnob
{
    ObxfKnobBase()
    {
        // JUCE rotary default used by OB-Xf: 1.2 pi .. 2.8 pi
        minAngle = -0.8f * M_PI;
        maxAngle = 0.8f * M_PI;
#ifdef METAMODULE
        setSvg(svg(Small ? "Trim.svg" : "Knob.svg"));
#else
        auto *bg = new widget::SvgWidget;
        fb->addChildBelow(bg, tw);
        bg->setSvg(svg(Small ? "Trim_bg.svg" : "Knob_bg.svg"));
        setSvg(svg(Small ? "Trim_fg.svg" : "Knob_fg.svg"));
#endif
        shadow->opacity = 0.f; // the OB-Xf art has its own shading
    }
};
using ObxfKnob = ObxfKnobBase<false>;
using ObxfTrimpot = ObxfKnobBase<true>;

struct ObxfSnapKnob : ObxfKnob
{
    ObxfSnapKnob() { snap = true; }
};

// Large latching button with LED (oscillator waves, SYNC, XPANDER)
struct ObxfButton : app::SvgSwitch
{
    ObxfButton()
    {
        addFrame(svg("Button_0.svg"));
        addFrame(svg("Button_1.svg"));
        shadow->opacity = 0.f;
    }
};

/* Large button whose LED is a separate light (ObxfButtonLed) driven by the module, so it
 * can show a state the param alone does not know about (waveform chosen by CV). Both
 * frames are the unlit OB-Xf button. A light, unlike a switch frame, also follows the
 * module state on the MetaModule. */
struct ObxfLedButton : app::SvgSwitch
{
    ObxfLedButton()
    {
        addFrame(svg("Button_0.svg"));
        addFrame(svg("Button_0.svg"));
        shadow->opacity = 0.f;
    }
};

// The LED of ObxfButton's art (button.svg: r = 3.75 px, #FF0000 / unlit #301010)
struct ObxfButtonLed : app::ModuleLightWidget
{
    ObxfButtonLed()
    {
        bgColor = nvgRGB(0x30, 0x10, 0x10);
        borderColor = nvgRGB(0x19, 0x19, 0x19);
        addBaseColor(nvgRGB(0xff, 0x00, 0x00));
        box.size = mm2px(math::Vec(1.7f, 1.7f));
    }

    // Without a module (module browser, library screenshots) show the param's default
    // state instead of Rack's all-lit preview.
    bool previewOn = false;

    void step() override
    {
        if (!module)
        {
            color = previewOn ? baseColors[0] : nvgRGBA(0, 0, 0, 0);
            return;
        }
        app::ModuleLightWidget::step();
    }
};

// Offset (mm) of the LED from the centre of an ObxfButton (button.svg: cy = 6.5 of 35 px)
constexpr float BUTTON_LED_DY = (6.5f - 17.5f) * 9.f / 40.f;

// Slim latching LED button (4-POLE, INVERT, OSC1+2, INV, ...)
struct ObxfSlimButton : app::SvgSwitch
{
    ObxfSlimButton()
    {
        addFrame(svg("Slim_0.svg"));
        addFrame(svg("Slim_1.svg"));
        shadow->opacity = 0.f;
    }
};

// Noise colour: white / pink / red, cycles on click
struct ObxfNoiseButton : app::SvgSwitch
{
    ObxfNoiseButton()
    {
        addFrame(svg("Noise_0.svg"));
        addFrame(svg("Noise_1.svg"));
        addFrame(svg("Noise_2.svg"));
        shadow->opacity = 0.f;
    }
};

// Horizontal slider (CURVE, VELOCITY)
struct ObxfSlider : app::SvgSlider
{
    ObxfSlider()
    {
        horizontal = true;
        setBackgroundSvg(svg("SliderTrack.svg"));
        setHandleSvg(svg("SliderHandle.svg"));
        const float pad = 0.35f;
        const float y = (box.size.y - handle->box.size.y) / 2.f;
        setHandlePos(math::Vec(mm2px(pad), y),
                     math::Vec(box.size.x - handle->box.size.x - mm2px(pad), y));
    }
};

/* Red seven-segment display, like OB-Xf's mode / voices / patch number displays.
 * Pure nanovg polygons: no font, and it renders on the MetaModule as a graphics display. */
struct SevenSegmentDisplay : widget::TransparentWidget
{
    int digits = 2;

    // Returns the text to show; must cope with a null module (module browser).
    virtual std::string getText() = 0;

    static uint8_t segmentsFor(char ch)
    {
        // bits: a b c d e f g
        switch (ch)
        {
        case '0': return 0b1111110;
        case '1': return 0b0110000;
        case '2': return 0b1101101;
        case '3': return 0b1111001;
        case '4': return 0b0110011;
        case '5': return 0b1011011;
        case '6': return 0b1011111;
        case '7': return 0b1110000;
        case '8': return 0b1111111;
        case '9': return 0b1111011;
        case 'L': return 0b0001110;
        case 'H': return 0b0110111;
        case 'B': return 0b0011111; // b
        case 'N': return 0b0010101; // n
        case 'P': return 0b1100111;
        case '-': return 0b0000001;
        default: return 0;
        }
    }

    void drawDigit(NVGcontext *vg, float x, float y, float w, float h, uint8_t seg)
    {
        const float t = w * 0.2f; // segment thickness
        const float hh = h / 2.f;
        // segment rectangles a..g as (x0, y0, x1, y1), with bevelled ends
        struct S
        {
            float x0, y0, x1, y1;
            bool horizontal;
        };
        const S s[7] = {
            {x + t * 0.5f, y, x + w - t * 0.5f, y + t, true},                        // a
            {x + w - t, y + t * 0.5f, x + w, y + hh - t * 0.1f, false},              // b
            {x + w - t, y + hh + t * 0.1f, x + w, y + h - t * 0.5f, false},          // c
            {x + t * 0.5f, y + h - t, x + w - t * 0.5f, y + h, true},                // d
            {x, y + hh + t * 0.1f, x + t, y + h - t * 0.5f, false},                  // e
            {x, y + t * 0.5f, x + t, y + hh - t * 0.1f, false},                      // f
            {x + t * 0.5f, y + hh - t * 0.5f, x + w - t * 0.5f, y + hh + t * 0.5f, true}, // g
        };
        for (int i = 0; i < 7; i++)
        {
            if (!(seg & (1 << (6 - i))))
                continue;
            const S &r = s[i];
            const float b = t * 0.5f;
            nvgBeginPath(vg);
            if (r.horizontal)
            {
                nvgMoveTo(vg, r.x0, (r.y0 + r.y1) / 2);
                nvgLineTo(vg, r.x0 + b, r.y0);
                nvgLineTo(vg, r.x1 - b, r.y0);
                nvgLineTo(vg, r.x1, (r.y0 + r.y1) / 2);
                nvgLineTo(vg, r.x1 - b, r.y1);
                nvgLineTo(vg, r.x0 + b, r.y1);
            }
            else
            {
                nvgMoveTo(vg, (r.x0 + r.x1) / 2, r.y0);
                nvgLineTo(vg, r.x1, r.y0 + b);
                nvgLineTo(vg, r.x1, r.y1 - b);
                nvgLineTo(vg, (r.x0 + r.x1) / 2, r.y1);
                nvgLineTo(vg, r.x0, r.y1 - b);
                nvgLineTo(vg, r.x0, r.y0 + b);
            }
            nvgClosePath(vg);
            nvgFill(vg);
        }
    }

    void drawLayer(const DrawArgs &args, int layer) override
    {
        if (layer != 1)
            return;
        const std::string txt = getText();
        const float pad = box.size.y * 0.16f;
        const float h = box.size.y - 2 * pad;
        const float w = h * 0.5f;
        const float gap = w * 0.38f;
        const float total = digits * w + (digits - 1) * gap;
        const float x0 = (box.size.x - total) / 2.f;

        // right-align shorter text, like OB-Xf's displays (which show no unlit segments)
        const int offset = digits - (int)txt.size();
        nvgFillColor(args.vg, nvgRGB(0xFF, 0x10, 0x10));
        for (int i = 0; i < digits; i++)
        {
            const int k = i - offset;
            const char ch = (k >= 0 && k < (int)txt.size()) ? txt[k] : ' ';
            drawDigit(args.vg, x0 + i * (w + gap), pad, w, h, segmentsFor(ch));
        }
        Widget::drawLayer(args, layer);
    }
};

inline void placeDisplay(widget::Widget *d, layout::MmBox b)
{
    d->box.size = mm2px(math::Vec(b.w, b.h));
    d->box.pos = mm2px(math::Vec(b.x - b.w / 2, b.y - b.h / 2));
}

} // namespace obxfui
