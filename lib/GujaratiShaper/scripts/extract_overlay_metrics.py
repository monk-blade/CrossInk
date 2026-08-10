#!/usr/bin/env python3
"""Extract Gujarati overlay placement metrics from a source TTF.

Computes the subjoined-Ra x-offset as a ratio of the base consonant advance
for the canonical ટ+્+ર probe sequence used by GujaratiShaper (PUA E07A).
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import freetype

# Ta + virama + Ra in Unicode.
TA = 0x0A9F
VIRAMA = 0x0ACD
RA = 0x0AB0


def encode_utf8(cp: int) -> bytes:
    if cp < 0x80:
        return bytes([cp])
    if cp < 0x800:
        return bytes([0xC0 | (cp >> 6), 0x80 | (cp & 0x3F)])
    return bytes([0xE0 | (cp >> 12), 0x80 | ((cp >> 6) & 0x3F), 0x80 | (cp & 0x3F)])


def measure_subjoined_ra_ratio(font_path: Path, size: int = 26) -> dict[str, int]:
    face = freetype.Face(str(font_path))
    face.set_char_size(size * 64)

    ta_index = face.get_char_index(TA)
    if not ta_index:
        return {"subjoinedRaOffsetNum": 201, "subjoinedRaOffsetDen": 482}

    face.load_glyph(ta_index, freetype.FT_LOAD_DEFAULT)
    ta_advance = face.glyph.advance.x
    if ta_advance <= 0:
        return {"subjoinedRaOffsetNum": 201, "subjoinedRaOffsetDen": 482}

    subjoin_index = face.get_char_index(0xE07A)
    if not subjoin_index:
        # Source fonts may not expose the PUA glyph until fontconvert embeds it.
        return {"subjoinedRaOffsetNum": 201, "subjoinedRaOffsetDen": 482}

    face.load_glyph(subjoin_index, freetype.FT_LOAD_DEFAULT)
    subjoin_left = face.glyph.bitmap_left
    offset = ta_advance + subjoin_left * 64
    numerator = max(0, ta_advance - offset)
    denominator = max(1, ta_advance)
    from math import gcd

    g = gcd(numerator // 64, denominator // 64)
    num = (numerator // 64) // max(1, g)
    den = (denominator // 64) // max(1, g)
    return {"subjoinedRaOffsetNum": int(num), "subjoinedRaOffsetDen": int(den)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("font", type=Path, help="Gujarati regular TTF/OTF")
    parser.add_argument("--family", required=True, help="Font family id")
    parser.add_argument("--output", type=Path, required=True, help="overlay_metrics.json path")
    args = parser.parse_args()

    metrics = measure_subjoined_ra_ratio(args.font)
    payload: dict[str, object] = {"families": {}}
    if args.output.is_file():
        payload = json.loads(args.output.read_text())
    payload.setdefault("families", {})
    payload["families"][args.family] = metrics
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
    print(json.dumps({args.family: metrics}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
