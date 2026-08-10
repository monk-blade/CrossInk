#pragma once

#include <cstdint>

#include "GujaratiGlyphs.h"
#include "GujaratiOverlayMetrics.h"

// Tracks syllable state while GfxRenderer walks a shaped Gujarati string.
struct GujaratiOverlayState {
  int lastBaseX = 0;
  int lastConsX = 0;
  int lastSyllableX = 0;
  int32_t lastConsAdvanceFP = 0;
  int32_t prevAdvanceFP = 0;
};

inline bool gujaratiOverlaySkipsAdvance(const uint32_t cp) { return utf8IsGujaratiOverlayMark(cp); }

inline void gujaratiOverlayUpdateAfterBaseGlyph(GujaratiOverlayState& state, const uint32_t cp, const int lastBaseX,
                                                const int32_t prevAdvanceFP) {
  if ((cp >= 0x0A95 && cp <= 0x0AB9) || (cp >= 0xE000 && cp <= 0xF8FF)) {
    state.lastConsX = lastBaseX;
    state.lastConsAdvanceFP = prevAdvanceFP;
  }
  if (cp != 0x0ABF) {
    state.lastSyllableX = lastBaseX;
  }
}
