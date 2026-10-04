/*
 * OB-Xm — panel components in OB-Xf's VectorTheme style.
 *
 * The SVGs in res/components/ are cut from OB-Xf's assets/binary/VectorTheme
 * (GPL-3.0-or-later) by tools/gen_panels.py; the "_Noir" ones (panel style B) are
 * drawn by it.
 *
 * Copyright (C) 2026 OB-Xm contributors.
 * Released under the GNU General Public Licence v3 or later (GPL-3.0-or-later).
 */
#pragma once

#include "plugin.hpp"
#include "PanelLayout.hpp"

#include <string>
#include <vector>

namespace obxfui
{

inline std::shared_ptr<window::Svg> svg(const char *name)
{
    return window::Svg::load(asset::plugin(pluginInstance, std::string("res/components/") + name));
}

inline math::Vec mm(layout::Mm p) { return mm2px(math::Vec(p.x, p.y)); }

// File suffix of a panel style's art
inline const char *themeSuffix(int theme) { return theme == THEME_NOIR ? "_Noir" : ""; }

// A component whose art follows the panel style (set by ObxmModuleWidget)
struct Themed
{
    virtual ~Themed() = default;
    virtual void setTheme(int theme) = 0;
};

/* OB-Xf knob: static body (layer 1) under a rotating cap with pointer (layer 2).
 * The MetaModule draws a knob as one rotating image, so it gets both layers merged. */
template <bool Small> struct ObxfKnobBase : app::SvgKnob, Themed
{
    widget::SvgWidget *bg = nullptr;

    ObxfKnobBase()
    {
        // JUCE rotary default used by OB-Xf: 1.2 pi .. 2.8 pi
        minAngle = -0.8f * M_PI;
        maxAngle = 0.8f * M_PI;
#ifdef METAMODULE
        setSvg(svg(Small ? "Trim.svg" : "Knob.svg"));
#else
        bg = new widget::SvgWidget;
        fb->addChildBelow(bg, tw);
        bg->setSvg(svg(Small ? "Trim_bg.svg" : "Knob_bg.svg"));
        setSvg(svg(Small ? "Trim_fg.svg" : "Knob_fg.svg"));
#endif
        shadow->opacity = 0.f; // the OB-Xf art has its own shading
    }

    void setTheme(int theme) override
    {
        if (!bg)
            return;
        const std::string base = std::string(Small ? "Trim" : "Knob") + themeSuffix(theme);
        bg->setSvg(svg((base + "_bg.svg").c_str()));
        setSvg(svg((base + "_fg.svg").c_str()));
        fb->setDirty();
    }
};
using ObxfKnob = ObxfKnobBase<false>;
using ObxfTrimpot = ObxfKnobBase<true>;

struct ObxfSnapKnob : ObxfKnob
{
    ObxfSnapKnob() { snap = true; }
};

/* Switch whose frames are res/components/<name><style suffix>_<i>.svg; `art` lists the
 * file index of each frame. */
struct ObxfThemedSwitch : app::SvgSwitch, Themed
{
    std::string name;
    std::vector<int> art;

    void initFrames(const char *n, std::initializer_list<int> files)
    {
        name = n;
        art = files;
        for (int i : art)
            addFrame(svg((name + "_" + std::to_string(i) + ".svg").c_str()));
        shadow->opacity = 0.f;
    }

    void setTheme(int theme) override
    {
        frames.clear();
        for (int i : art)
            frames.push_back(
                svg((name + themeSuffix(theme) + "_" + std::to_string(i) + ".svg").c_str()));
        int index = 0;
        if (engine::ParamQuantity *pq = getParamQuantity())
            index = math::clamp((int)std::round(pq->getValue() - pq->getMinValue()), 0,
                                (int)frames.size() - 1);
        sw->setSvg(frames[index]);
        fb->setDirty();
    }
};

// Large latching button with LED (oscillator waves, SYNC, XPANDER)
struct ObxfButton : ObxfThemedSwitch
{
    ObxfButton() { initFrames("Button", {0, 1}); }
};

/* Large button whose LED is a separate light (ObxfButtonLed) driven by the module, so it
 * can show a state the param alone does not know about (waveform chosen by CV). Both
 * frames are the unlit OB-Xf button. A light, unlike a switch frame, also follows the
 * module state on the MetaModule. */
struct ObxfLedButton : ObxfThemedSwitch
{
    ObxfLedButton() { initFrames("Button", {0, 0}); }
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
struct ObxfSlimButton : ObxfThemedSwitch
{
    ObxfSlimButton() { initFrames("Slim", {0, 1}); }
};

// Noise colour: white / pink / red, cycles on click
struct ObxfNoiseButton : ObxfThemedSwitch
{
    ObxfNoiseButton() { initFrames("Noise", {0, 1, 2}); }
};

// Horizontal slider (CURVE, VELOCITY)
struct ObxfSlider : app::SvgSlider, Themed
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

    void setTheme(int theme) override
    {
        const std::string s = themeSuffix(theme);
        setBackgroundSvg(svg(("SliderTrack" + s + ".svg").c_str()));
        setHandleSvg(svg(("SliderHandle" + s + ".svg").c_str()));
        fb->setDirty();
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

/* Module widget with the two panel styles: res/<panel>.svg and res/<panel>_Noir.svg.
 * The MetaModule has no context menu and always shows style A. */
struct ObxmModuleWidget : app::ModuleWidget
{
    std::string panelName;
    int shownTheme = THEME_LIGHT;

    void setThemedPanel(const std::string &name)
    {
        panelName = name;
        setPanel(createPanel(asset::plugin(pluginInstance, "res/" + name + ".svg")));
    }

#ifndef METAMODULE
    int wantedTheme()
    {
        auto *m = dynamic_cast<ObxmModule *>(module);
        return m ? m->panelTheme : defaultPanelTheme;
    }

    void applyTheme(int theme)
    {
        shownTheme = theme;
        if (auto *p = dynamic_cast<app::SvgPanel *>(getPanel()))
            p->setBackground(window::Svg::load(
                asset::plugin(pluginInstance, "res/" + panelName + themeSuffix(theme) + ".svg")));
        for (widget::Widget *w : children)
            if (auto *t = dynamic_cast<Themed *>(w))
                t->setTheme(theme);
    }

    void step() override
    {
        if (wantedTheme() != shownTheme)
            applyTheme(wantedTheme());
        app::ModuleWidget::step();
    }

#endif

    // "Panel style" submenu (VCV Rack only)
    void appendThemeMenu(ui::Menu *menu)
    {
#ifndef METAMODULE
        auto *m = dynamic_cast<ObxmModule *>(module);
        if (!m)
            return;
        menu->addChild(new ui::MenuSeparator);
        menu->addChild(createSubmenuItem("Panel style", "", [=](ui::Menu *sub) {
            static const char *names[] = {"OB-Xf Light", "OB-8 Noir"};
            for (int i = 0; i < 2; i++)
                sub->addChild(createCheckMenuItem(
                    names[i], "", [=]() { return m->panelTheme == i; },
                    [=]() {
                        m->panelTheme = i;
                        defaultPanelTheme = i; // also for the next modules
                    }));
            sub->addChild(new ui::MenuSeparator);
            sub->addChild(createMenuItem("Apply to all OB-Xm modules", "", [=]() {
                for (app::ModuleWidget *mw : APP->scene->rack->getModules())
                    if (auto *om = dynamic_cast<ObxmModule *>(mw->module))
                        om->panelTheme = m->panelTheme;
            }));
        }));
#endif
    }

    void appendContextMenu(ui::Menu *menu) override { appendThemeMenu(menu); }
};

inline void placeDisplay(widget::Widget *d, layout::MmBox b)
{
    d->box.size = mm2px(math::Vec(b.w, b.h));
    d->box.pos = mm2px(math::Vec(b.x - b.w / 2, b.y - b.h / 2));
}

} // namespace obxfui

using obxfui::ObxmModuleWidget;
