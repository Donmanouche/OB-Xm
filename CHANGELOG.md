# Changelog

## 0.1 (plugin version 2.0.1) — 2026-10-03

First public release: OBXm Oscillator and OBXm Filter for VCV Rack 2 (Linux x64
binary) and the 4ms MetaModule. Built from OB-Xf's DSP code (commit b08ffb6).

- Waveform LEDs show the default state (saw on) in the module browser.
- Repository: e-mail removed from the manifests, links to the GitHub project,
  MetaModule SDK looked up in `../metamodule-plugin-sdk` by default.

The entries below are the development history before the first release.

## 2.1.0 — 2026-10-03

- **Renamed to OB-Xm.** Plugin slug `OBXf-VCV` -> `OB-Xm`, modules
  `OBXfOscillator` / `OBXfFilter` -> `OBXmOscillator` / `OBXmFilter`
  ("OBXm Oscillator", "OBXm Filter"), brand and panel logo "OB-Xm" (same lettering).
  Patches made with earlier versions do not find the modules any more: replace them.
- The project is still built from OB-Xf's DSP code and keeps its credits and
  GPL-3.0-or-later licence. Entries below use the former name.

## 2.0.7 — 2026-10-03

- OBXf Oscillator: the waveform buttons' LEDs now show the waveform actually played
  (channel 0), including when it is chosen by the WAVE 1 / WAVE 2 CVs. They are
  module lights, so this also works on the MetaModule.
- OBXf Oscillator: the OSC 2 OFFSET attenuverter is labelled "ATT OFFSET", below it.
- OBXf Filter: **ATTACK** and **DECAY** CV inputs with attenuverters (added to the
  knobs, 10 V = full range, polyphonic). A new time applies to the running stage, as
  OB-Xf's modulation matrix does. The CV section is now two rows of three
  (attenuverter + jack): CUTOFF, RES, ENV AMT / MODE, ATTACK, DECAY.

## 2.0.6 — 2026-10-03

- OBXf Oscillator: new polyphonic CV inputs (not in OB-Xf, which has no CV):
  - **WAVE 1 / WAVE 2**: 0–2.5 V triangle, 2.5–5 V saw, 5–7.5 V pulse, 7.5–10 V
    saw+pulse (0.1 V hysteresis); overrides the waveform buttons while patched.
  - **MIX 1 / MIX 2 / RING**: added to the mixer knobs, 10 V = full range.
- The OSC 2 OFFSET CV attenuverter moves next to the OSC 2 OFFSET knob; the jack
  section is now two rows (pitch / PW CVs, then waveform / mixer CVs).
- Existing cables keep their ids (new inputs are appended). The oscillator DSP is
  unchanged.

## 2.0.5 — 2026-10-03

- OBXf Filter: the DRIVE input saturation of 2.0.4 and its level make-up are
  removed; the filter is OB-Xf's again, with no addition.
- Narrower panels, with OB-Xf's spacing (about 1.7 knob diameters between knobs):
  OBXf Oscillator 22 -> 18 HP, OBXf Filter 24 -> 20 HP. Same controls and layout.

## 2.0.4 — 2026-10-02

- OBXf Filter: **DRIVE** knob (SATURATION section). Not part of OB-Xf: a tanh soft
  clipper before the filter, 0 to +30 dB of input gain, with an output make-up so
  the level stays within a few dB. At 0 % it is an exact bypass: the filter is
  OB-Xf's, unchanged (the A/B tests against OB-Xf still pass). The saturation runs
  at the oversampled rate when HQ is on, which reduces its aliasing.

## 2.0.3 — 2026-10-02

- OBXf Filter: the VOLUME knob is removed. The output keeps a fixed gain after the
  filter (one saw through an open filter = ±2.5 V, the former 50 % setting), so the
  filter is driven exactly as in OB-Xf and there is headroom before Rack clips.
- Verified on a note sequence against the OB-Xf VST3 (one voice): OBXf Oscillator ->
  OBXf Filter -> OB-Xf-style amp envelope matches within ±1–2 dB per band. Without an
  amp envelope / VCA after the filter the oscillator keeps sounding between notes
  (OB-Xf silences it with its amp envelope): patch a VCA + ADSR after the filter.
- Tests: `SEQ=` note sequences in `tests/vst3_render` and `tests/render_vcv`.

## 2.0.2 — 2026-10-02

- **VOLUME moved to OBXf Filter**, at the end of the voice like OB-Xf's master
  volume, and defaults to 50 % (one saw through an open filter = ±2.5 V). In 2.0.0
  and 2.0.1 the filter output was at ±5 V per saw and went well past ±10 V with
  resonance, both oscillators or unison: Rack's Audio module hard-clips there, which
  sounded grainy, noisy and out of tune. Measured on a user patch in Rack: 21 % of
  the samples clipped before, none after.
- OBXf Oscillator's output is back to a fixed level (the reference level the filter
  expects), so the filter is always driven as in OB-Xf. The 2.0.1 Oscillator VOLUME
  is ignored when loading older patches.

## 2.0.1 — 2026-10-02

- **OBXf Oscillator: VOLUME knob** (OB-Xf's master volume, in the MASTER section).
  50 % is the previous, reference level (one saw = ±5 V). Two oscillators at full
  level, unison or chords used to exceed ±10 V and clip in Rack's Audio module
  (which also sums the polyphonic channels); turn VOLUME down for those.
- **OSC 1 / OSC 2 pitch knobs snap to semitones** by default (OB-Xf does it with a
  modifier drag), so intervals are exact; context menu to turn it off.
- Tests: `tests/vst3_render` renders the installed OB-Xf VST3 offline and
  `tests/render_vcv` renders the VCV engines with the same parameter names, to
  compare against the real plug-in; `tests/rack_module` runs the actual Rack
  module classes against libRack.

## 2.0.0 — 2026-10-02

First version.

- **OBXf Oscillator**: OB-Xf's two oscillators (saw / pulse / triangle, PW, Osc 2
  PW offset, detune, cross-modulation, hard sync, Osc 2 keytrack, brightness),
  mixer (osc 1, osc 2, ring mod, white/pink/red noise), transpose, tune, unison
  (up to 8 voices per channel; 4 on MetaModule) and unison detune. External
  envelope inputs for Env to Pitch and Env to PW. Optional HQ (2x oversampling).
- **OBXf Filter**: OB-Xf's 2-pole (push, BP blend) and 4-pole filter with the 15
  Xpander modes, keytrack, and its filter envelope (attack curve, velocity,
  invert). CV with attenuverters for cutoff, resonance, env amount and mode;
  envelope output. Optional HQ.
- Polyphonic: 16 channels on VCV Rack, 4 on MetaModule.
- DSP code taken from OB-Xf commit b08ffb6; verified against OB-Xf's own Voice
  class in `tests/`.
