# Bundled SD-card fonts

Pre-built `.cpfont` families committed for simulator and device testing.
Regenerate with the repo venv:

```sh
LEX=lib/EpdFont/builtinFonts/source/LexendDeca
RASA=.work/fonts/downloads/Rasa/Rasa[wght].ttf

.venv/bin/python lib/GujaratiShaper/scripts/build_combined_font.py \
  --latin-regular "$LEX/LexendDeca-Regular.ttf" \
  --latin-bold "$LEX/LexendDeca-Bold.ttf" \
  --gujarati-regular "$RASA" \
  --gujarati-regular-weight 500 \
  --gujarati-bold "$RASA" \
  --gujarati-bold-weight 700 \
  --gujarati-sizes 16:19,18:21 \
  --name LexendDecaRasa500 \
  --output-dir ./assets/sd-fonts/LexendDecaRasa500/
```

`make prepare-simulator-fs` copies these into `fs_/.fonts/` for the SDL simulator.

### LexendDecaHindVadodara500

```sh
LEX=lib/EpdFont/builtinFonts/source/LexendDeca
HIND=.work/fonts/downloads/HindVadodara

.venv/bin/python lib/GujaratiShaper/scripts/build_combined_font.py \
  --latin-regular "$LEX/LexendDeca-Regular.ttf" \
  --latin-bold "$LEX/LexendDeca-Bold.ttf" \
  --gujarati-regular "$HIND/HindVadodara-Medium.ttf" \
  --gujarati-bold "$HIND/HindVadodara-Bold.ttf" \
  --gujarati-sizes 16:16,18:18 \
  --name LexendDecaHindVadodara500 \
  --output-dir ./assets/sd-fonts/LexendDecaHindVadodara500/
```
