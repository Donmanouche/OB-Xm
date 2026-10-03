#!/usr/bin/env bash
# Converts the Rack SVGs (res/) to the PNGs the MetaModule needs (metamodule/assets/),
# with the SDK's SvgToPng.py (needs Inkscape 1.2.x). Run tools/gen_panels.py first.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
# SvgToPng.py does not cope with absolute paths containing spaces: work with
# paths relative to this directory.
cd "$HERE"
SDK="${METAMODULE_SDK_DIR:-../../metamodule-plugin-sdk}"
RES="../res"
OUT="assets"

rm -rf "$OUT"
mkdir -p "$OUT/components"
# Faceplates: 240 px high, opaque
for f in ObxfOsc ObxfFilter; do
    python3 "$SDK/scripts/SvgToPng.py" --input "$RES/$f.svg" --output "$OUT" --height=240
done
# Components (knobs, buttons, sliders), transparent. The Rack-only knob layers
# (*_bg, *_fg) are not needed: the MetaModule uses the merged Knob.svg / Trim.svg.
TMP="$(mktemp -d)" # no spaces in /tmp
trap 'rm -rf "$TMP"' EXIT
for f in "$RES"/components/*.svg; do
    case "$(basename "$f")" in *_bg.svg|*_fg.svg) continue ;; esac
    cp "$f" "$TMP/"
done
python3 "$SDK/scripts/SvgToPng.py" --input "$TMP" --output "$OUT/components"
ls -l "$OUT" "$OUT/components"
