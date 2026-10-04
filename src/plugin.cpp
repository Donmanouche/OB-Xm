/*
 * OB-Xm — plugin entry point.
 *
 * Copyright (C) 2026 OB-Xm contributors.
 * Released under the GNU General Public Licence v3 or later (GPL-3.0-or-later).
 */
#include "plugin.hpp"

Plugin *pluginInstance;
int defaultPanelTheme = THEME_LIGHT;

void init(Plugin *p)
{
    pluginInstance = p;
    p->addModel(modelObxfOscillator);
    p->addModel(modelObxfFilter);
    p->addModel(modelObxfAmp);
    p->addModel(modelObxfVoices);
}

#ifndef METAMODULE
// Plugin-wide settings, stored by Rack in settings["pluginSettings"]["OB-Xm"]
json_t *settingsToJson()
{
    json_t *root = json_object();
    json_object_set_new(root, "defaultPanelTheme", json_integer(defaultPanelTheme));
    return root;
}

void settingsFromJson(json_t *root)
{
    if (json_t *j = json_object_get(root, "defaultPanelTheme"))
        defaultPanelTheme = json_integer_value(j) == THEME_NOIR ? THEME_NOIR : THEME_LIGHT;
}
#endif
