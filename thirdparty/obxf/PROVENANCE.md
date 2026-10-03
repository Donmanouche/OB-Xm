# OB-Xf DSP — provenance

- Upstream: https://github.com/surge-synthesizer/OB-Xf
- Commit: `b08ffb6ab6cfa0f66cb057e855149ab640de0f78` (2026-09-30)
- License: GPL-3.0-or-later (see `LICENSE` in this directory, copied from upstream)

## `engine/` — verbatim copies, **unmodified**

Copied byte-for-byte from `src/engine/` of the commit above:
`AdsrEnvelope.h`, `AudioUtils.h`, `BlepData.h`, `Decimator.h`, `DelayLine.h`,
`Filter.h`, `Noise.h`, `OscillatorBlock.h`, `ParamScales.h`, `PulseOsc.h`,
`SawOsc.h`, `Smoother.h`, `TriangleOsc.h`.

They are never included directly: `src/dsp/ObxfDsp.hpp` pre-includes the
standard headers and then includes them inside `namespace obxf { ... }` so
their global names (`pi`, `dc`, `blep`, ...) cannot clash with Rack.

## `shim/` — replacements for JUCE-dependent upstream headers

The engine files include `"SynthEngine.h"`, `"Voice.h"` and `<Utils.h>`,
which pull in JUCE and the whole synth. `shim/` provides stand-ins with those
names that only define the few symbols the DSP actually uses (`pi`, `dc`,
`mult`, `getPitch()`, `NUM_XPANDER_MODES`, `juce::jlimit`), copied from
`src/core/Constants.h`, `src/configuration.h` and `src/Utils.h`.

## What is *re-implemented* (not copied) in `src/dsp/`

The per-voice glue of `src/engine/Voice.h` (`Voice::ProcessSample`), the
parameter scalings of `src/engine/SynthEngine.h` (`process*()` functions),
the 2x oversampling of `src/engine/Motherboard.h` and the default
"voice variation" amounts of `src/parameter/ParameterList.h` are re-written
in `src/dsp/OscEngine.hpp` and `src/dsp/FilterEngine.hpp`, line by line,
with comments pointing at the upstream source.

## `assets/`

`knob-layer1.svg`, `knob-layer2.svg`, `button.svg`, `menu-xpander.svg`,
`background.svg` from `assets/binary/VectorTheme/` (GPL-3.0-or-later, part of
the OB-Xf repository). `tools/gen_panels.py` derives the Rack components in
`res/components/` from them.
