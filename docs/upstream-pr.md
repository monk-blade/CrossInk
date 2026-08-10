# Upstream CrossPoint PR — Gujarati rendering

This branch (`crossink`) carries a merge-ready Gujarati stack for
[CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) `develop`.

## PR 1 (core)

Submit these paths as one PR. Reference Malayalam PR #1756 as prior art.

| Path | Purpose |
|------|---------|
| `lib/GujaratiShaper/` | Self-contained shaper, overlay helpers, scripts |
| `lib/Epub/Epub/ParsedText.cpp` | `GujaratiIntegration::shapeWord()` hook |
| `lib/Epub/Epub/Section.cpp` | `SECTION_FILE_VERSION` bump + changelog |
| `lib/GfxRenderer/GfxRenderer.cpp` | Labelled `// Gujarati` overlay calls only |
| `lib/GfxRenderer/GfxRenderer.h` | `prewarmTextCache()` if not already upstream |
| `lib/EpdFont/scripts/sd-fonts.yaml` | `NotoSerifGujarati` family |
| `lib/EpdFont/scripts/fontconvert_sdcard.py` | PUA interval sorting + `--pua-mapping` |
| `lib/EpdFont/scripts/build-sd-fonts.py` | Pass `pua_mapping` through |
| `src/SdCardFontSystem.cpp` | Gujarati UI fallback probe (`0x0A95`) |
| `src/components/themes/BaseTheme.cpp` | `shapeUiString()` for status bar |
| `src/activities/reader/EpubReaderChapterSelectionActivity.*` | Shaped chapter titles |
| `src/activities/reader/XtcReaderChapterSelectionActivity.cpp` | Shaped XTC chapters |
| `src/activities/reader/EpubReaderBookmarksActivity.*` | Shaped bookmark rows |
| `src/activities/home/RecentBooksActivity.*` | Shaped library titles |
| `src/activities/home/FileBrowserActivity.*` | Shaped file names |
| `src/activities/reader/KOReaderSyncActivity.cpp` | Shaped chapter names |
| `test/gujarati_shaper/` | Host unit tests + corpus |
| `test/CMakeLists.txt` | `add_subdirectory(gujarati_shaper)` |
| `docs/gujarati-rendering.md` | Integration guide |
| `docs/file-formats.md` | Section version history |

## PR 2 (follow-up, optional)

- Overlay metrics per font family (`overlay_metrics.json`, `extract_overlay_metrics.py`)
- Rotated `drawTextRotated90CW` Gujarati parity (already in this branch)
- Additional font families via crosspoint-fonts (Hind, Anek, etc.)

## Rebase workflow

```bash
cd crosspoint-reader
git fetch origin develop
git rebase origin/develop
# resolve conflicts; keep Gujarati blocks labelled
cd ..
git -C crosspoint-reader diff origin/develop > patches/gujarati-rendering.patch
make verify
```

## Opening the PR

```bash
cd crosspoint-reader
git push -u origin crossink
gh pr create --repo crosspoint-reader/crosspoint-reader \
  --base develop --head crossink \
  --title "Add Gujarati EPUB rendering (shaper + NotoSerifGujarati)" \
  --body "$(cat <<'EOF'
## Summary
- Adds `lib/GujaratiShaper/` with on-device conjunct/matra/reph shaping (PUA glyphs)
- Wires `GujaratiIntegration::shapeWord()` at parse time and UI `shapeUiString()` hooks
- Ships NotoSerifGujarati SD-card fonts with matching PUA conjuncts
- GfxRenderer draws zero-advance reph/anusvara/subjoined-Ra overlays

Follows the Malayalam approach in #1756.

## Test plan
- [ ] `cmake --build test --target GujaratiShaperTest && ./test/build/gujarati_shaper/GujaratiShaperTest`
- [ ] Install NotoSerifGujarati on SD card; clear `.crosspoint/`
- [ ] Open Gujarati EPUB; verify conjuncts, `માં`, `ટ્રોય`, chapter list titles
EOF
)"
```

## Fonts

Publish `NotoSerifGujarati_*.cpfont` to
[crosspoint-fonts](https://github.com/crosspoint-reader/crosspoint-fonts) using
`tools/publish-gujarati-release.sh` from the wrapper repo.
