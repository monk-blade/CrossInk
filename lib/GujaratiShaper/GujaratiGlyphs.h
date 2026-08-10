#pragma once

#include <cstdint>

// Draw-time helpers for shaped Gujarati output (PUA conjuncts and overlay marks).
// Kept in the GujaratiShaper module so forks can wire GfxRenderer without
// extending lib/Utf8.

inline bool utf8IsGujaratiReph(const uint32_t cp) { return cp == 0xE065; }

inline bool utf8IsGujaratiSubjoinedRa(const uint32_t cp) { return cp == 0xE07A; }

inline bool utf8IsGujaratiAnusvara(const uint32_t cp) { return cp == 0x0A82; }

inline bool utf8IsGujaratiChandrabindu(const uint32_t cp) { return cp == 0x0A81; }

inline bool utf8IsGujaratiSyllableMark(const uint32_t cp) {
  return utf8IsGujaratiAnusvara(cp) || utf8IsGujaratiChandrabindu(cp);
}

inline bool utf8IsGujaratiBelowBaseMatra(const uint32_t cp) { return cp == 0x0AC1 || cp == 0x0AC2; }

inline bool utf8IsGujaratiOverlayMark(const uint32_t cp) {
  return utf8IsGujaratiReph(cp) || utf8IsGujaratiSubjoinedRa(cp) || utf8IsGujaratiSyllableMark(cp);
}
