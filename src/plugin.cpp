/*
 * OB-Xm — plugin entry point.
 *
 * Copyright (C) 2026 OB-Xm contributors.
 * Released under the GNU General Public Licence v3 or later (GPL-3.0-or-later).
 */
#include "plugin.hpp"

Plugin *pluginInstance;

void init(Plugin *p)
{
    pluginInstance = p;
    p->addModel(modelObxfOscillator);
    p->addModel(modelObxfFilter);
}
