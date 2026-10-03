/*
 * OB-Xm — wraps the vendored OB-Xf engine headers in `namespace obxf`.
 *
 * Copyright (C) 2026 OB-Xm contributors. GPL-3.0-or-later.
 * The included files are OB-Xf code (https://github.com/surge-synthesizer/OB-Xf),
 * see thirdparty/obxf/PROVENANCE.md.
 */
#pragma once

// Everything the OB-Xf headers include must be pulled in *before* the
// namespace block, so their own #includes become no-ops inside it.
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <math.h>

namespace obxf
{
#include "obxf_shim.h"
#include "ParamScales.h"
#include "AudioUtils.h"
#include "OscillatorBlock.h"
#include "Filter.h"
#include "AdsrEnvelope.h"
#include "Decimator.h"
#include "Smoother.h"
} // namespace obxf
