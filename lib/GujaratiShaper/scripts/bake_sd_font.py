#!/usr/bin/env python3
"""Bake a component-based Gujarati font into single-glyph PUA conjuncts.

The firmware shaper (lib/GujaratiShaper) replaces Gujarati conjunct/reph/rakar
sequences with single Private-Use-Area codepoints (see pua_mapping.json, which is
derived from Noto Serif Gujarati). At draw time the renderer blits ONE glyph per
PUA codepoint. Noto Serif Gujarati works directly with
`lib/EpdFont/scripts/fontconvert_sdcard.py --pua-mapping pua_mapping.json`
because it already contains one pre-composed ligature glyph per conjunct.

Other Google Gujarati fonts (Rasa, Hind Vadodara, Mukta Vaani, …) instead build
conjuncts from *component* glyphs (half-forms + `.post`/`gjRakar` pieces) placed
by GPOS, so they have no single glyph to point a PUA codepoint at. This tool
shapes each PUA's Unicode sequence in the target font with HarfBuzz and merges
the resulting component outlines (at their shaped offsets) into one composite
glyph, then emits a font-specific PUA mapping usable by fontconvert_sdcard.py.

This is an in-tree stand-in for the maintainer's external `build-gujarati-fonts.py`
(referenced by the Makefile as FONT_BUILDER), letting the desktop simulator test
these families without the out-of-tree tooling.

Requires: fontTools, uharfbuzz  (`pip install fonttools uharfbuzz`)

Usage (variable font -> one static weight per run):
    python3 lib/GujaratiShaper/scripts/bake_sd_font.py \
        --font Rasa[wght].ttf --weight 400 \
        --pua-mapping lib/GujaratiShaper/scripts/pua_mapping.json \
        --out Rasa-Regular.baked.ttf --out-mapping rasa_pua.json

Then feed the baked TTFs to the SD-font converter (one shared mapping):
    python3 lib/EpdFont/scripts/fontconvert_sdcard.py \
        --regular Rasa-Regular.baked.ttf --bold Rasa-Bold.baked.ttf \
        --intervals "gujarati,latin-ext" --sizes 12,14,16,18 --name Rasa \
        --pua-mapping rasa_pua.json --output-dir ./Rasa/
"""
from __future__ import annotations

import argparse
import json
import re
import sys

KA = 0x0A95
RA = 0x0AB0
VIRAMA = 0x0ACD
ARGS_ARE_XY_VALUES = 0x0002


def decode_sequence(glyph_name: str):
    """Map a Noto PUA glyph name to (kind, unicode-sequence)."""
    n = glyph_name.split(".")[0]
    if n == "rgujarati":
        return "reph", [RA, VIRAMA]
    if n in ("zhgujarati", "zh_ragujarati"):
        return None, None  # ZHA has no encoded base letter; skip.
    if n.startswith("uni"):
        cps = [int(h, 16) for h in re.findall(r"[0-9A-Fa-f]{4}", n[3:])]
        cps = [RA if c == 0x0072 else c for c in cps]  # Noto's 'r' suffix = rakar RA
        if cps == [RA, VIRAMA]:
            return "reph", cps
        if cps == [VIRAMA, RA]:
            return "rakar", cps
        if cps and cps[-1] == VIRAMA:
            return "half", cps
        return "full", cps
    return None, None


def bake(font_path: str, weight, pua_json: str, out_ttf: str, out_map: str) -> None:
    import uharfbuzz as hb
    from fontTools.ttLib import TTFont
    from fontTools.ttLib.tables._g_l_y_f import Glyph, GlyphComponent

    tt = TTFont(font_path)
    if "fvar" in tt:
        if weight is None:
            sys.exit("error: variable font requires --weight")
        from fontTools.varLib.instancer import instantiateVariableFont
        instantiateVariableFont(tt, {"wght": float(weight)}, inplace=True)
    tt.save(out_ttf)  # materialise instance for HarfBuzz + glyf editing

    blob = hb.Blob.from_file_path(out_ttf)
    face = hb.Face(blob)
    font = hb.Font(face)
    ft = TTFont(out_ttf)
    glyf, hmtx, order = ft["glyf"], ft["hmtx"], ft.getGlyphOrder()

    def shape(cps):
        buf = hb.Buffer()
        buf.add_codepoints(cps)
        buf.script, buf.direction, buf.language = "Gujr", "ltr", "gu"
        hb.shape(font, buf)
        return [
            (order[i.codepoint], i.cluster, p.x_offset, p.y_offset, p.x_advance)
            for i, p in zip(buf.glyph_infos, buf.glyph_positions)
        ]

    pua = json.load(open(pua_json))
    mapping, baked, direct, skipped = {}, 0, 0, 0
    for code, name in pua.items():
        kind, cps = decode_sequence(name)
        if kind is None:
            skipped += 1
            continue
        if kind == "reph":
            g = shape([RA, VIRAMA, KA])
            target = [x for x in g if "reph" in x[0].lower()] or g[-1:]
        elif kind == "rakar":
            # The subjoined-ra component is named differently per font (Rasa:
            # gjRakar / gjR.post, Hind Vadodara: gjRAc2, …). Prefer an obvious
            # rakar name, else take the non-placeholder glyph (fonts insert a
            # dotted-circle base when RA+VIRAMA is shaped without a consonant).
            g = shape([VIRAMA, RA])
            target = [x for x in g if "rakar" in x[0].lower() or "r.post" in x[0].lower()]
            target = target or [x for x in g if "dottedcircle" not in x[0].lower() and x[0] != ".notdef"]
        elif kind == "half":
            g = shape(cps + [KA])
            target = [x for x in g if x[1] < len(cps)]
        else:  # full conjunct
            target = shape(cps)
        target = [x for x in target if x[0] != ".notdef"]
        if not target:
            skipped += 1
            continue
        overlay = kind in ("reph", "rakar")  # zero-advance marks
        if len(target) == 1 and target[0][2] == 0 and target[0][3] == 0 and not overlay:
            mapping[code] = target[0][0]  # single glyph already correct; map by name
            direct += 1
            continue
        comp = Glyph()
        comp.numberOfContours = -1
        comp.components = []
        pen = 0
        for gname, _cl, xo, yo, xadv in target:
            c = GlyphComponent()
            c.glyphName = gname
            c.x, c.y = (0, 0) if overlay else (int(round(pen + xo)), int(round(yo)))
            c.flags = ARGS_ARE_XY_VALUES
            comp.components.append(c)
            pen += xadv
        new_name = f"cpbake_{code[2:]}"
        glyf.glyphs[new_name] = comp
        order.append(new_name)
        hmtx.metrics[new_name] = (0 if overlay else int(round(pen)), 0)
        mapping[code] = new_name
        baked += 1
    ft.setGlyphOrder(order)
    ft["maxp"].numGlyphs = len(order)
    ft.save(out_ttf)
    json.dump(mapping, open(out_map, "w"), indent=1)
    print(f"{out_ttf}: baked={baked} direct={direct} skipped={skipped} mapped={len(mapping)}/{len(pua)}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--font", required=True, help="Target Gujarati TTF/OTF (variable or static).")
    ap.add_argument("--weight", type=float, default=None, help="Weight axis value for a variable font (e.g. 400, 700).")
    ap.add_argument("--pua-mapping", required=True, help="Noto-derived pua_mapping.json (sequence source of truth).")
    ap.add_argument("--out", required=True, help="Output baked TTF path.")
    ap.add_argument("--out-mapping", required=True, help="Output font-specific PUA mapping JSON for fontconvert_sdcard.py.")
    args = ap.parse_args()
    bake(args.font, args.weight, args.pua_mapping, args.out, args.out_mapping)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
