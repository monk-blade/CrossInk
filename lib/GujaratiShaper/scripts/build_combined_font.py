#!/usr/bin/env python3
"""Build a combined Latin + Gujarati SD .cpfont family.

Produces a single `.cpfont` family whose Latin glyphs come from a Latin font
(e.g. Lexend Deca) and whose Gujarati glyphs (base block + shaped conjuncts,
reph, rakar) come from a Gujarati font (e.g. Rasa, Hind Vadodara). The result
is selectable from the normal font picker and needs no firmware changes: because
one font carries both scripts, Latin words render in the Latin face and Gujarati
words in the Gujarati face.

Pipeline (wraps the two existing scripts):
  1. bake_sd_font.py         -> bake the Gujarati font's component conjuncts into
                                single glyphs at the shaper's PUA codepoints.
  2. fontconvert_sdcard.py   -> merge Latin (primary) + baked Gujarati (fallback,
                                incl. the Gujarati block + PUA range) into .cpfont.

Requires: fontTools, uharfbuzz, freetype-py  (pip install fonttools uharfbuzz freetype-py)

Example (Rasa is a variable font; Hind Vadodara ships static weights):
  python3 lib/GujaratiShaper/scripts/build_combined_font.py \
      --latin-regular LexendDeca-Regular.ttf --latin-bold LexendDeca-Bold.ttf \
      --gujarati-regular "Rasa[wght].ttf" --gujarati-regular-weight 400 \
      --gujarati-bold    "Rasa[wght].ttf" --gujarati-bold-weight 700 \
      --name LexendDecaRasa --output-dir ./fs_/.fonts/LexendDecaRasa/

  python3 lib/GujaratiShaper/scripts/build_combined_font.py \
      --latin-regular LexendDeca-Regular.ttf --latin-bold LexendDeca-Bold.ttf \
      --gujarati-regular HindVadodara-Regular.ttf \
      --gujarati-bold    HindVadodara-Bold.ttf \
      --name LexendDecaHindVadodara --output-dir ./fs_/.fonts/LexendDecaHindVadodara/
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
DEFAULT_PUA = os.path.join(HERE, "pua_mapping.json")
FONTCONVERT = os.path.join(REPO, "lib", "EpdFont", "scripts", "fontconvert_sdcard.py")
# Gujarati block + shaped-conjunct PUA range + Indic danda + Indian rupee sign.
GUJARATI_FALLBACK_RANGES = "0x0A80-0x0AFF;0xE000-0xE07C;0x0964-0x0965;0x20B9-0x20B9"
OUTPUT_INTERVALS = "latin-ext,gujarati,(0x0964-0x0965),(0x20B9-0x20B9)"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--latin-regular", required=True)
    ap.add_argument("--latin-bold", required=True)
    ap.add_argument("--gujarati-regular", required=True)
    ap.add_argument("--gujarati-bold", required=True)
    ap.add_argument("--gujarati-regular-weight", type=float, default=None, help="Weight axis for a variable Gujarati regular.")
    ap.add_argument("--gujarati-bold-weight", type=float, default=None, help="Weight axis for a variable Gujarati bold.")
    ap.add_argument("--name", required=True, help="Combined family name (used for filenames and the font picker).")
    ap.add_argument("--sizes", default="12,14,16,18")
    ap.add_argument("--output-dir", required=True)
    ap.add_argument("--pua-mapping", default=DEFAULT_PUA)
    args = ap.parse_args()

    bake = os.path.join(HERE, "bake_sd_font.py")
    with tempfile.TemporaryDirectory(prefix="combined-font-") as tmp:
        reg_ttf = os.path.join(tmp, "guj-regular.baked.ttf")
        bold_ttf = os.path.join(tmp, "guj-bold.baked.ttf")
        reg_map = os.path.join(tmp, "pua-regular.json")
        bold_map = os.path.join(tmp, "pua-bold.json")

        def run_bake(src, weight, out_ttf, out_map):
            cmd = [sys.executable, bake, "--font", src, "--pua-mapping", args.pua_mapping,
                   "--out", out_ttf, "--out-mapping", out_map]
            if weight is not None:
                cmd += ["--weight", str(weight)]
            subprocess.run(cmd, check=True)

        run_bake(args.gujarati_regular, args.gujarati_regular_weight, reg_ttf, reg_map)
        run_bake(args.gujarati_bold, args.gujarati_bold_weight, bold_ttf, bold_map)

        # The baked PUA name mapping is deterministic across weights of the same
        # family, so the regular mapping applies to both faces.
        cmd = [
            sys.executable, FONTCONVERT,
            "--regular", args.latin_regular, "--bold", args.latin_bold,
            "--fallback-regular", reg_ttf, "--fallback-bold", bold_ttf,
            "--fallback-regular-ranges", GUJARATI_FALLBACK_RANGES,
            "--fallback-bold-ranges", GUJARATI_FALLBACK_RANGES,
            "--intervals", OUTPUT_INTERVALS,
            "--sizes", args.sizes, "--name", args.name,
            "--pua-mapping", reg_map, "--output-dir", args.output_dir,
        ]
        subprocess.run(cmd, check=True)
    print(f"Built combined family '{args.name}' -> {args.output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
