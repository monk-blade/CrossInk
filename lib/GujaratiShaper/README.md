# GujaratiShaper

Lightweight Gujarati text shaper for CrossPoint Reader and forks. Replaces
OpenType shaping at parse time: conjuncts become PUA codepoints, pre-base matras
are reordered, and reph / subjoined Ra are emitted for GfxRenderer overlays.

Based on the Malayalam shaper pattern ([crosspoint-reader PR #1756](https://github.com/crosspoint-reader/crosspoint-reader/pull/1756)).

## Module layout

```
lib/GujaratiShaper/
├── GujaratiShaper.h / .cpp      # Core shaper API
├── GujaratiShapingData.h        # Generated GSUB lookup tables (do not hand-edit)
├── GujaratiGlyphs.h             # PUA / overlay codepoint predicates (for GfxRenderer)
├── GujaratiIntegration.h / .cpp # Fork-friendly hooks (ParsedText, UI)
├── README.md                    # This file
└── scripts/
    ├── extract_shaping_rules.py # Regenerate ShapingData.h + pua_mapping.json from a TTF
    └── pua_mapping.json         # PUA glyph list consumed by fontconvert_sdcard.py
```

## Public API

| Symbol | Use |
|--------|-----|
| `GujaratiShaper::shape()` | Shape a UTF-8 buffer into an output buffer |
| `GujaratiShaper::shapeInPlace()` | Shape a `std::string` in place |
| `GujaratiShaper::containsGujarati()` | Fast scan for Gujarati block / PUA |
| `GujaratiIntegration::shapeWord()` | **Preferred** hook for `ParsedText::addWord` |
| `GujaratiIntegration::shapeUiString()` | **Preferred** hook for status bar / chapter lists |

PUA constants: `REPH_GLYPH` (`0xE065`), `SUBJOINED_RA_GLYPH` (`0xE07A`).

## Regenerating shaping data

When the source font (Noto Serif Gujarati) changes:

```bash
cd lib/GujaratiShaper/scripts
pip install fonttools
python extract_shaping_rules.py NotoSerifGujarati-Regular.ttf ..
```

Then rebuild SD fonts so `.cpfont` files include the updated PUA glyphs:

```bash
python3 lib/EpdFont/scripts/build-sd-fonts.py --only NotoSerifGujarati
```

All three artifacts **must stay in sync**: `GujaratiShapingData.h`, `pua_mapping.json`, and `NotoSerifGujarati_*.cpfont`.

## Tests

```bash
cd test && cmake -B build && cmake --build build --target GujaratiShaperTest
./build/gujarati_shaper/GujaratiShaperTest
```

## Full integration guide

See [docs/gujarati-rendering.md](../../docs/gujarati-rendering.md) for porting to CrossPoint, CrossInk, inx, and other forks.
