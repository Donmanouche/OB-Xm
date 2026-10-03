#!/usr/bin/env python3
"""
OB-Xm panel generator.

Single source of truth for the two faceplates. It writes:

  res/ObxfOsc.svg, res/ObxfFilter.svg   faceplates (text converted to paths with
                                        Inkscape, because NanoSVG -- Rack's SVG
                                        renderer -- does not support <text>, and the
                                        MetaModule PNGs need the labels baked in)
  res/components/*.svg                  knobs, buttons, sliders, cut from OB-Xf's
                                        VectorTheme assets (thirdparty/obxf/assets)
  src/PanelLayout.hpp                   control positions (mm) used by the C++

Colours, proportions and component art follow OB-Xf's default VectorTheme:
panel grey #727882, frame #191919, wood cheeks #795336, red LEDs / displays.

Usage:  python3 tools/gen_panels.py        (needs `inkscape` on PATH)

GPL-3.0-or-later.
"""
from __future__ import annotations

import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "thirdparty" / "obxf" / "assets"
RES = ROOT / "res"
COMP = RES / "components"
LAYOUT_HPP = ROOT / "src" / "PanelLayout.hpp"

SVG_NS = "http://www.w3.org/2000/svg"
ET.register_namespace("", SVG_NS)
ET.register_namespace("xlink", "http://www.w3.org/1999/xlink")

# ---------------------------------------------------------------------------
# Style (OB-Xf VectorTheme, assets/binary/VectorTheme/background.svg)
# ---------------------------------------------------------------------------
FRAME = "#191919"      # background around the sections
PANEL = "#727882"      # section grey
PANEL_EDGE = "#5A5F68"
WOOD = "#795336"
WOOD_DARK = "#5E3F28"
LABEL = "#FFFFFF"
LABEL_DIM = "#E4E6EA"
RED = "#FF0000"
JACK_BOX = "#26272A"
FONT = "Liberation Sans"

PANEL_H = 128.5
HP = 5.08

# OB-Xf's UI pixels -> mm. Chosen so that a 40 px OB-Xf knob is 9 mm on the panel.
PX = 9.0 / 40.0

# Component sizes (mm)
KNOB_D = 40 * PX            # 9.0
TRIM_D = 6.2
BUTTON_W, BUTTON_H = 23 * PX, 35 * PX   # 5.175 x 7.875
SLIM_W, SLIM_H = 18 * PX, 13 * PX       # 4.05 x 2.925
SLIDER_W, SLIDER_H = 47 * PX, 12 * PX   # 10.575 x 2.7
HANDLE_W, HANDLE_H = 9 * PX, 9 * PX


# ---------------------------------------------------------------------------
# Components cut from OB-Xf assets
# ---------------------------------------------------------------------------
def _num_list(s: str) -> list[float]:
    return [float(v) for v in re.findall(r"-?\d*\.?\d+(?:e-?\d+)?", s)]


def _path_ys(d: str) -> list[float]:
    """y coordinates of the points of a path (absolute M/L/H/V/C/S/Q/T/Z commands)."""
    tokens = re.findall(r"[MLHVCSQTZmlhvcsqtz]|-?\d*\.?\d+(?:e-?\d+)?", d)
    ys: list[float] = []
    cmd, i, x, y = "M", 0, 0.0, 0.0
    while i < len(tokens):
        t = tokens[i]
        if t.isalpha():
            cmd = t
            i += 1
            continue
        if cmd in "Hh":
            x = float(t)
            i += 1
        elif cmd in "Vv":
            y = float(t)
            i += 1
        else:  # coordinate pairs
            x, y = float(tokens[i]), float(tokens[i + 1])
            i += 2
        ys.append(y)
    return ys


def _elem_y(el: ET.Element) -> float | None:
    """Reference y of a top-level element of a filmstrip SVG."""
    tag = el.tag.split("}")[-1]
    if tag == "rect":
        y = float(el.get("y", "0"))
        tr = el.get("transform", "")
        m = re.match(r"translate\(\s*[-\d.e]+[ ,]+([-\d.e]+)\s*\)", tr)
        if m:
            y += float(m.group(1))
        return y + float(el.get("height", "0")) / 2
    if tag == "circle":
        return float(el.get("cy", "0"))
    if tag == "path":
        ys = _path_ys(el.get("d", ""))
        return (min(ys) + max(ys)) / 2 if ys else None
    return None


def cut_frame(src: Path, y0: float, h: float, width_mm: float, height_mm: float,
              x0: float = 0.0, w: float | None = None) -> str:
    """Keep only the elements of one frame of an OB-Xf filmstrip and give it a viewBox."""
    tree = ET.parse(src)
    root = tree.getroot()
    full_w = float(root.get("width"))
    full_h = float(root.get("height"))
    w = full_w if w is None else w
    out = ET.Element(f"{{{SVG_NS}}}svg", {
        "width": f"{width_mm:.4f}mm",
        "height": f"{height_mm:.4f}mm",
        "viewBox": f"{x0:g} {y0:g} {w:g} {h:g}",
        "fill": "none",
    })
    for el in list(root):
        tag = el.tag.split("}")[-1]
        if tag == "defs":
            out.append(el)
            continue
        # full-size transparent hit areas
        if tag == "rect" and float(el.get("height", "0")) >= full_h - 0.01:
            continue
        y = _elem_y(el)
        if y is not None and y0 <= y < y0 + h:
            out.append(el)
    return ET.tostring(out, encoding="unicode")


def scaled_copy(src: Path, width_mm: float, height_mm: float) -> str:
    tree = ET.parse(src)
    root = tree.getroot()
    if "viewBox" not in root.attrib:
        root.set("viewBox", f"0 0 {root.get('width')} {root.get('height')}")
    root.set("width", f"{width_mm:.4f}mm")
    root.set("height", f"{height_mm:.4f}mm")
    return ET.tostring(root, encoding="unicode")


def _prefixed_ids(src: Path, prefix: str) -> ET.Element:
    """Parse an SVG with every id (and reference to it) prefixed, so two files can merge."""
    txt = src.read_text(encoding="utf-8")
    ids = set(re.findall(r'\bid="([^"]+)"', txt))
    for i in sorted(ids, key=len, reverse=True):
        txt = re.sub(rf'\bid="{re.escape(i)}"', f'id="{prefix}{i}"', txt)
        txt = txt.replace(f"url(#{i})", f"url(#{prefix}{i})")
        txt = txt.replace(f'href="#{i}"', f'href="#{prefix}{i}"')
    return ET.fromstring(txt)


def merged_knob(width_mm: float) -> str:
    """layer1 (body) + layer2 (ridges, pointer) in one SVG, for MetaModule where a
    knob is a single rotating image."""
    l1 = _prefixed_ids(ASSETS / "knob-layer1.svg", "a")
    l2 = _prefixed_ids(ASSETS / "knob-layer2.svg", "b")
    out = ET.Element(f"{{{SVG_NS}}}svg", {
        "width": f"{width_mm:.4f}mm", "height": f"{width_mm:.4f}mm",
        "viewBox": l1.get("viewBox"),
    })
    for i, layer in enumerate((l1, l2)):
        g = ET.SubElement(out, f"{{{SVG_NS}}}g", {"id": f"layer{i + 1}"})
        for el in list(layer):
            tag = el.tag.split("}")[-1]
            if tag in ("namedview", "page"):
                continue
            g.append(el)
    return ET.tostring(out, encoding="unicode")


def write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not text.startswith("<?xml"):
        text = '<?xml version="1.0" encoding="UTF-8"?>\n' + text
    path.write_text(text, encoding="utf-8")


def gen_components() -> None:
    COMP.mkdir(parents=True, exist_ok=True)
    # Knobs: background (static) + foreground (rotating) for Rack, merged for MetaModule
    for name, d in (("Knob", KNOB_D), ("Trim", TRIM_D)):
        write(COMP / f"{name}_bg.svg", scaled_copy(ASSETS / "knob-layer1.svg", d, d))
        write(COMP / f"{name}_fg.svg", scaled_copy(ASSETS / "knob-layer2.svg", d, d))
        write(COMP / f"{name}.svg", merged_knob(d))
    # Big push button with LED: frames are off, off-pressed, on, on-pressed (35 px each)
    write(COMP / "Button_0.svg", cut_frame(ASSETS / "button.svg", 0, 35, BUTTON_W, BUTTON_H))
    write(COMP / "Button_1.svg", cut_frame(ASSETS / "button.svg", 70, 35, BUTTON_W, BUTTON_H))
    # Slim LED button: off, off-pressed, on, on-pressed (13 px each)
    write(COMP / "Slim_0.svg", cut_frame(ASSETS / "button-slim.svg", 0, 13, SLIM_W, SLIM_H))
    write(COMP / "Slim_1.svg", cut_frame(ASSETS / "button-slim.svg", 26, 13, SLIM_W, SLIM_H))
    # Noise colour button: white, pink, red (each followed by its pressed frame)
    for i, y in enumerate((0, 26, 52)):
        write(COMP / f"Noise_{i}.svg",
              cut_frame(ASSETS / "button-slim-noise.svg", y, 13, SLIM_W, SLIM_H))
    # Horizontal slider (CURVE / VELOCITY)
    write(COMP / "SliderTrack.svg", scaled_copy(ASSETS / "slider-h-layer1.svg", SLIDER_W, SLIDER_H))
    write(COMP / "SliderHandle.svg",
          cut_frame(ASSETS / "slider-h-layer2.svg", 1.5, 9, HANDLE_W, HANDLE_H, x0=0, w=9))


# ---------------------------------------------------------------------------
# Faceplate model
# ---------------------------------------------------------------------------
@dataclass
class Control:
    cid: str          # C++ identifier in PanelLayout.hpp
    kind: str         # knob, trim, button, slim, noise, slider, in, out, display
    x: float
    y: float
    label: str = ""
    label_dy: float | None = None  # label offset below the control centre
    w: float = 0.0    # display size
    h: float = 0.0


@dataclass
class Section:
    title: str
    x0: float
    y0: float
    x1: float
    y1: float
    title_x: float | None = None


@dataclass
class Panel:
    name: str
    hp: int
    title: str
    sections: list[Section] = field(default_factory=list)
    controls: list[Control] = field(default_factory=list)
    extra: list[str] = field(default_factory=list)  # raw SVG snippets

    @property
    def width(self) -> float:
        return self.hp * HP


def label_offset(c: Control) -> float:
    if c.label_dy is not None:
        return c.label_dy
    return {
        "knob": KNOB_D / 2 + 2.3,
        "trim": TRIM_D / 2 + 2.0,
        "button": BUTTON_H / 2 + 2.3,
        "slim": SLIM_H / 2 + 2.2,
        "noise": SLIM_H / 2 + 2.2,
        "slider": SLIDER_H / 2 + 2.3,
    }.get(c.kind, 0.0)


def text(x: float, y: float, s: str, size: float, color: str = LABEL, anchor: str = "middle",
         weight: str = "bold", style: str = "normal") -> str:
    s = s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    return (f'<text x="{x:.3f}" y="{y:.3f}" font-family="{FONT}" font-size="{size:.2f}" '
            f'font-weight="{weight}" font-style="{style}" fill="{color}" '
            f'text-anchor="{anchor}">{s}</text>')


def rrect(x0: float, y0: float, x1: float, y1: float, fill: str, r: float = 0.8,
          stroke: str | None = None, sw: float = 0.3) -> str:
    st = f' stroke="{stroke}" stroke-width="{sw}"' if stroke else ""
    return (f'<rect x="{x0:.3f}" y="{y0:.3f}" width="{x1 - x0:.3f}" height="{y1 - y0:.3f}" '
            f'rx="{r}" fill="{fill}"{st}/>')


def wave_glyph(kind: str, cx: float, cy: float, color: str = LABEL) -> str:
    """Small waveform icons drawn under the oscillator wave buttons (as in OB-Xf)."""
    w, h = 3.2, 1.6
    x0, y0 = cx - w / 2, cy - h / 2
    if kind == "saw":
        d = f"M{x0:.3f},{y0 + h:.3f} L{x0 + w:.3f},{y0:.3f} L{x0 + w:.3f},{y0 + h:.3f}"
    elif kind == "pulse":
        d = (f"M{x0:.3f},{y0 + h:.3f} L{x0 + w * 0.25:.3f},{y0 + h:.3f} L{x0 + w * 0.25:.3f},{y0:.3f} "
             f"L{x0 + w * 0.75:.3f},{y0:.3f} L{x0 + w * 0.75:.3f},{y0 + h:.3f} L{x0 + w:.3f},{y0 + h:.3f}")
    else:  # triangle
        d = f"M{x0:.3f},{y0 + h:.3f} L{x0 + w / 2:.3f},{y0:.3f} L{x0 + w:.3f},{y0 + h:.3f}"
    return (f'<path d="{d}" fill="none" stroke="{color}" stroke-width="0.35" '
            f'stroke-linejoin="round" stroke-linecap="round"/>')


def jack_ring(x: float, y: float) -> str:
    return f'<circle cx="{x:.3f}" cy="{y:.3f}" r="4.3" fill="{JACK_BOX}"/>'


def build_svg(p: Panel) -> str:
    W, H = p.width, PANEL_H
    out: list[str] = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{W:.3f}mm" height="{H}mm" '
        f'viewBox="0 0 {W:.3f} {H}">',
        f'<rect x="0" y="0" width="{W:.3f}" height="{H}" fill="{FRAME}"/>',
        # wooden cheeks, like OB-Xf's side panels
        f'<rect x="0" y="0" width="1.4" height="{H}" fill="{WOOD}"/>',
        f'<rect x="{W - 1.4:.3f}" y="0" width="1.4" height="{H}" fill="{WOOD}"/>',
        f'<rect x="1.1" y="0" width="0.3" height="{H}" fill="{WOOD_DARK}"/>',
        f'<rect x="{W - 1.4:.3f}" y="0" width="0.3" height="{H}" fill="{WOOD_DARK}"/>',
    ]
    for s in p.sections:
        out.append(rrect(s.x0, s.y0, s.x1, s.y1, PANEL, r=0.6, stroke=PANEL_EDGE, sw=0.25))
        if s.title:
            tx = s.title_x if s.title_x is not None else (s.x0 + s.x1) / 2
            out.append(text(tx, s.y0 + 4.6, s.title, 3.0))
    out.extend(p.extra)

    for c in p.controls:
        dy = label_offset(c)
        if c.kind in ("in", "out"):
            if c.kind == "in":
                out.append(jack_ring(c.x, c.y))
            if c.label:
                out.append(text(c.x, c.y - 5.6, c.label, 2.0, RED if c.kind == "out" else LABEL))
            continue
        if c.kind == "display":
            out.append(rrect(c.x - c.w / 2 - 0.5, c.y - c.h / 2 - 0.5, c.x + c.w / 2 + 0.5,
                             c.y + c.h / 2 + 0.5, "#0E0E0E", r=0.5, stroke="#3A3B3E", sw=0.35))
            if c.label:
                out.append(text(c.x, c.y + c.h / 2 + 3.3, c.label, 2.0))
            continue
        if c.label:
            for i, line in enumerate(c.label.split("\n")):
                out.append(text(c.x, c.y + dy + i * 2.3, line, 2.0))
    out.append("</svg>")
    return "\n".join(out)


def text_to_path(svg: str, dest: Path) -> None:
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td) / "in.svg"
        tmp.write_text(svg, encoding="utf-8")
        subprocess.run(["inkscape", str(tmp), "--export-text-to-path", "--export-plain-svg",
                        "-o", str(dest)], check=True, capture_output=True)


# ---------------------------------------------------------------------------
# OBXm Oscillator — 18 HP
# ---------------------------------------------------------------------------
def osc_panel() -> Panel:
    # Spacing follows OB-Xf's UI: about 1.7 knob diameters between knob centres
    p = Panel("ObxfOsc", 18, "OSCILLATOR")
    W = p.width
    X1, X2, X3 = 10.0, 25.0, 40.0
    MX = 55.75
    RL, RR = 70.2, 82.7
    TOP, BOT = 2.5, 88.5

    p.sections += [
        Section("OSCILLATORS", 2.5, TOP, 47.5, BOT),
        Section("MIXER", 49.0, TOP, 62.5, BOT),
        Section("MASTER", 64.0, TOP, W - 2.5, 31.0),
        Section("UNISON", 64.0, 33.0, W - 2.5, 66.5),
        Section("", 2.5, 90.5, W - 2.5, 126.0),
    ]
    C = p.controls
    # OSCILLATORS — rows as in OB-Xf's oscillator section
    C += [
        Control("OSC1_PITCH", "knob", X1, 17.5, "OSC 1"),
        Control("DETUNE", "knob", X2, 17.5, "DETUNE"),
        Control("OSC2_PITCH", "knob", X3, 17.5, "OSC 2"),
        Control("SAW1", "button", X1 - 3.1, 32.5),
        Control("PULSE1", "button", X1 + 3.1, 32.5),
        Control("PW", "knob", X2, 32.5, "PW"),
        Control("SAW2", "button", X3 - 3.1, 32.5),
        Control("PULSE2", "button", X3 + 3.1, 32.5),
        Control("ENV_PITCH_BOTH", "slim", X1 - 3.6, 44.5, "1+2"),
        Control("ENV_PITCH_INV", "slim", X1 + 3.6, 44.5, "INV"),
        Control("ENV_PW_INV", "slim", X3 - 3.6, 44.5, "INV"),
        Control("ENV_PW_BOTH", "slim", X3 + 3.6, 44.5, "1+2"),
        Control("ENV_PITCH", "knob", X1, 58.0, "ENV TO\nPITCH"),
        Control("OSC2_PW_OFFSET", "knob", X2, 58.0, "OSC 2\nOFFSET"),
        # attenuverter of the OSC2 OFS CV input, right above the knob it modulates
        Control("OSC2_PW_OFFSET_ATT", "trim", X2, 46.0, ""),
        Control("ENV_PW", "knob", X3, 58.0, "ENV TO\nPW"),
        Control("CROSSMOD", "knob", X1, 75.0, "CROSSMOD"),
        Control("SYNC", "button", X2 - 3.4, 75.0),
        Control("OSC2_KEYTRACK", "button", X2 + 3.4, 75.0),
        Control("BRIGHT", "knob", X3, 75.0, "BRIGHT"),
    ]
    p.extra += [
        wave_glyph("saw", X1 - 3.1, 38.4), wave_glyph("pulse", X1 + 3.1, 38.4),
        wave_glyph("saw", X3 - 3.1, 38.4), wave_glyph("pulse", X3 + 3.1, 38.4),
        # "both off = triangle" hint between the two buttons
        wave_glyph("tri", X1, 27.3, LABEL_DIM), wave_glyph("tri", X3, 27.3, LABEL_DIM),
        text(X2 - 3.4, 75.0 + BUTTON_H / 2 + 2.3, "SYNC", 2.0),
        text(X2 + 3.4, 75.0 + BUTTON_H / 2 + 2.3, "KEY", 2.0),
        text(X2, 51.6, "ATT OFFSET", 1.6, LABEL),
    ]

    # MIXER
    C += [
        Control("OSC1_VOL", "knob", MX, 17.5, "OSC 1"),
        Control("OSC2_VOL", "knob", MX, 35.0, "OSC 2"),
        Control("RING_VOL", "knob", MX, 52.5, "RING"),
        Control("NOISE_VOL", "knob", MX, 70.0, "NOISE"),
        Control("NOISE_COLOR", "noise", MX, 82.5),
    ]
    # MASTER / UNISON (OB-Xf's master volume is not part of these modules)
    C += [
        Control("TRANSPOSE", "knob", RL, 17.5, "TRANSP."),
        Control("TUNE", "knob", RR, 17.5, "TUNE"),
        Control("UNISON_VOICES", "knob", RL, 47.5, "VOICES"),
        Control("UNISON_DETUNE", "knob", RR, 47.5, "DETUNE"),
        Control("VOICES_DISPLAY", "display", (RL + RR) / 2, 60.6, "", w=8.0, h=5.0),
    ]
    # Branding
    bx = (64.0 + W - 2.5) / 2
    p.extra += [
        text(bx, 78.6, "OB-Xm", 7.2, FRAME, weight="bold", style="italic").replace(
            f'fill="{FRAME}"', f'fill="none" stroke="{LABEL}" stroke-width="0.32"'),
        text(bx, 84.0, "OSCILLATOR", 2.6, LABEL),
    ]

    # Jacks: pitch / PW CVs on the first row, waveform and mixer CVs on the second
    J1, J2 = 101.0, 117.5
    JX = [9.5, 21.5, 33.5, 45.5, 57.5]
    C += [
        Control("VOCT_INPUT", "in", JX[0], J1, "V/OCT"),
        Control("ENV_PITCH_INPUT", "in", JX[1], J1, "ENV>PITCH"),
        Control("ENV_PW_INPUT", "in", JX[2], J1, "ENV>PW"),
        Control("OSC2_PW_OFFSET_INPUT", "in", JX[3], J1, "OSC2 OFS"),
        Control("WAVE1_INPUT", "in", JX[0], J2, "WAVE 1"),
        Control("WAVE2_INPUT", "in", JX[1], J2, "WAVE 2"),
        Control("OSC1_VOL_INPUT", "in", JX[2], J2, "MIX 1"),
        Control("OSC2_VOL_INPUT", "in", JX[3], J2, "MIX 2"),
        Control("RING_VOL_INPUT", "in", JX[4], J2, "RING"),
        Control("OUT_OUTPUT", "out", 77.0, 109.0, "OUT"),
    ]
    p.extra += [
        # separator between the waveform and the mixer inputs
        f'<line x1="{(JX[1] + JX[2]) / 2:.2f}" y1="110.5" x2="{(JX[1] + JX[2]) / 2:.2f}" y2="123.0" '
        f'stroke="{LABEL_DIM}" stroke-width="0.25"/>',
        rrect(68.5, 98.0, 85.5, 120.0, JACK_BOX, r=1.0),
    ]
    return p


# ---------------------------------------------------------------------------
# OBXm Filter — 20 HP
# ---------------------------------------------------------------------------
def filter_panel() -> Panel:
    p = Panel("ObxfFilter", 20, "FILTER")
    W = p.width
    X1, X2, X3 = 10.0, 25.0, 40.0
    E0, E1 = 49.0, W - 2.5
    E = [E0 + (E1 - E0) * (i + 0.5) / 4 for i in range(4)]
    TOP = 2.5

    p.sections += [
        Section("FILTER", 2.5, TOP, 47.5, 64.0, title_x=33.0),
        Section("FILTER ENVELOPE", E0, TOP, E1, 40.0, title_x=82.5),
        Section("CV", E0, 42.0, E1, 88.5),
        Section("", 2.5, 90.5, W - 2.5, 126.0),
    ]
    C = p.controls
    C += [
        Control("FOUR_POLE", "slim", 7.6, TOP + 3.6),
        Control("CUTOFF", "knob", X1, 18.5, "CUTOFF"),
        Control("RESONANCE", "knob", X2, 18.5, "RESO"),
        Control("ENV_AMOUNT", "knob", X3, 18.5, "ENV AMT"),
        Control("KEYTRACK", "knob", X1, 37.5, "KEYTRACK"),
        Control("MODE", "knob", X2, 37.5, "MODE"),
        Control("MODE_DISPLAY", "display", X3, 36.0, "", w=12.0, h=7.0),
        Control("PUSH", "slim", X1, 55.0, "PUSH"),
        Control("BP_BLEND", "slim", X2, 55.0, "BP BLEND"),
        Control("XPANDER", "button", X3, 52.8, "XPANDER", label_dy=BUTTON_H / 2 + 2.3),
    ]
    p.extra += [text(10.5, TOP + 4.3, "4-POLE", 2.2, LABEL, anchor="start")]

    # FILTER ENVELOPE
    C += [
        Control("INVERT", "slim", E0 + 4.6, TOP + 3.6),
        Control("ATTACK", "knob", E[0], 18.5, "ATTACK"),
        Control("DECAY", "knob", E[1], 18.5, "DECAY"),
        Control("SUSTAIN", "knob", E[2], 18.5, "SUSTAIN"),
        Control("RELEASE", "knob", E[3], 18.5, "RELEASE"),
        Control("CURVE", "slider", (E[0] + E[1]) / 2, 32.0, "CURVE"),
        Control("VELOCITY", "slider", (E[2] + E[3]) / 2, 32.0, "VELOCITY"),
    ]
    p.extra += [text(E0 + 7.5, TOP + 4.3, "INVERT", 2.2, LABEL, anchor="start")]

    # CV: two rows of three, each an attenuverter (left) and its jack (right)
    names = [("CUTOFF", "CUTOFF"), ("RES", "RES"), ("ENV_AMT", "ENV AMT"),
             ("MODE", "MODE"), ("ATTACK", "ATTACK"), ("DECAY", "DECAY")]
    cw = (E1 - E0) / 3
    for i, (cid, lab) in enumerate(names):
        cx = E0 + cw * (i % 3 + 0.5)
        y = 57.0 if i < 3 else 76.5
        C += [
            Control(f"{cid}_ATT", "trim", cx - 3.9, y, ""),
            Control(f"{cid}_INPUT", "in", cx + 3.6, y, ""),
        ]
        p.extra.append(text(cx, y - 5.4, lab, 2.0))
    p.extra += [
        text((E0 + E1) / 2, 86.4, "ATTENUVERTER + CV IN", 1.7, LABEL_DIM, weight="normal"),
    ]

    # Branding under the filter section
    p.extra += [
        text(25.0, 77.6, "OB-Xm", 7.2, FRAME, weight="bold", style="italic").replace(
            f'fill="{FRAME}"', f'fill="none" stroke="{LABEL}" stroke-width="0.32"'),
        text(25.0, 83.0, "FILTER", 2.6, LABEL),
    ]

    # Jacks
    JY = 112.0
    C += [
        Control("IN_INPUT", "in", 10.0, JY, "IN"),
        Control("GATE_INPUT", "in", 23.0, JY, "GATE"),
        Control("VEL_INPUT", "in", 36.0, JY, "VEL"),
        Control("VOCT_INPUT", "in", 49.0, JY, "V/OCT"),
        Control("OUT_OUTPUT", "out", 75.5, JY, "OUT"),
        Control("ENV_OUTPUT", "out", 89.5, JY, "ENV"),
    ]
    p.extra += [
        rrect(67.5, 101.0, 97.5, 123.0, JACK_BOX, r=1.0),
        text(29.5, 124.3, "AUDIO / GATE / KEYTRACK", 1.9, LABEL_DIM, weight="normal"),
    ]
    return p


# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------
def write_layout(panels: list[Panel]) -> None:
    lines = [
        "// Generated by tools/gen_panels.py -- do not edit by hand.",
        "// Control centres in millimetres (see res/*.svg).",
        "#pragma once",
        "",
        "namespace layout",
        "{",
        "struct Mm",
        "{",
        "    float x, y;",
        "};",
        "struct MmBox",
        "{",
        "    float x, y, w, h; // centre and size",
        "};",
        "",
    ]
    for p in panels:
        lines.append(f"namespace {p.name[4:].lower()}")
        lines.append("{")
        lines.append(f"constexpr int HP = {p.hp};")
        for c in p.controls:
            if c.kind == "display":
                lines.append(f"constexpr MmBox {c.cid} = {{{c.x:.3f}f, {c.y:.3f}f, {c.w:.3f}f, {c.h:.3f}f}};")
            else:
                lines.append(f"constexpr Mm {c.cid} = {{{c.x:.3f}f, {c.y:.3f}f}};")
        lines.append(f"}} // namespace {p.name[4:].lower()}")
        lines.append("")
    lines += [
        "// Component geometry (mm)",
        f"constexpr float SLIDER_W = {SLIDER_W:.4f}f;",
        f"constexpr float HANDLE_W = {HANDLE_W:.4f}f;",
        "} // namespace layout",
        "",
    ]
    LAYOUT_HPP.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    gen_components()
    panels = [osc_panel(), filter_panel()]
    for p in panels:
        text_to_path(build_svg(p), RES / f"{p.name}.svg")
        print(f"wrote res/{p.name}.svg ({p.hp} HP)")
    write_layout(panels)
    print(f"wrote {LAYOUT_HPP.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
