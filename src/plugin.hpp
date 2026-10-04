/*
 * OB-Xm — VCV Rack / MetaModule port of OB-Xf's oscillators and filter.
 *
 * Copyright (C) 2026 OB-Xm contributors.
 * Released under the GNU General Public Licence v3 or later (GPL-3.0-or-later).
 * Based on OB-Xf, https://github.com/surge-synthesizer/OB-Xf
 */
#pragma once
#include <rack.hpp>

using namespace rack;

extern Plugin *pluginInstance;

extern Model *modelObxfOscillator;
extern Model *modelObxfFilter;
extern Model *modelObxfVoices;
extern Model *modelObxfAmp;

/* Panel styles: 0 = "OB-Xf Light" (default; the MetaModule's), 1 = "OB-8 Noir".
 * Chosen per module in its context menu (saved in the patch); the last choice is the
 * default for new modules (saved in Rack's settings). */
enum PanelTheme
{
    THEME_LIGHT = 0,
    THEME_NOIR = 1
};
extern int defaultPanelTheme;

struct ObxmModule : Module
{
    int panelTheme = defaultPanelTheme;

    json_t *dataToJson() override
    {
        json_t *root = json_object();
        json_object_set_new(root, "panelTheme", json_integer(panelTheme));
        return root;
    }

    void dataFromJson(json_t *root) override
    {
        if (json_t *j = json_object_get(root, "panelTheme"))
            panelTheme = json_integer_value(j) == THEME_NOIR ? THEME_NOIR : THEME_LIGHT;
    }
};
