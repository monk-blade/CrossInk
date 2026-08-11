#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

/**
 * Lightweight Gujarati text shaper.
 *
 * Transforms Gujarati codepoint sequences into shaped glyph codepoints
 * (using PUA assignments for conjunct/half-form glyphs). Operates on UTF-8
 * buffers with no heap allocation.
 *
 * Ported from the Malayalam shaper (crosspoint-reader/crosspoint-reader
 * PR #1756) for Gujarati's North-Indic shaping model (shared with
 * Devanagari): conjuncts form via GSUB-derived rules applied right-to-left
 * within a virama-joined cluster. The pre-base matra (U+0ABF) is reordered
 * ahead of its consonant cluster; reph (RA+virama) is moved after its base
 * consonant cluster. GfxRenderer draws the shaped stream sequentially and
 * overlays the reph glyph on the preceding consonant.
 */
class GujaratiShaper {
 public:
  static constexpr uint32_t REPH_GLYPH = 0xE065;
  static constexpr uint32_t SUBJOINED_RA_GLYPH = 0xE07A;

  /**
   * Shape Gujarati text in place when the string contains Gujarati codepoints.
   * No-op for non-Gujarati strings. Used for UI labels (e.g. status-bar titles)
   * that bypass ParsedText word shaping.
   */
  static void shapeInPlace(std::string& text);

  /**
   * Shape a UTF-8 string containing Gujarati text.
   *
   * Scans the input for Gujarati sequences and applies GSUB-derived
   * substitution rules (conjunct formation, half-forms, etc). Non-Gujarati
   * codepoints pass through unchanged. Input longer than one shaping window
   * (MAX_WORD_CPS codepoints, see GujaratiShaper.cpp) is shaped in successive
   * chunks rather than truncated — a conjunct that happens to straddle a
   * chunk boundary won't form, but no codepoints are dropped.
   *
   * @param input      Input UTF-8 string
   * @param inputLen   Length of input in bytes
   * @param output     Output buffer (must be at least inputLen * 2 bytes for safety)
   * @param outputCap  Capacity of output buffer in bytes
   * @return           Number of bytes written to output
   */
  static size_t shape(const char* input, size_t inputLen, char* output, size_t outputCap);

  /**
   * Returns true if the buffer contains any Gujarati codepoints (U+0A80-U+0AFF).
   */
  static bool containsGujarati(const char* text, size_t len);

 private:
  // Decode one UTF-8 codepoint, advance pointer. Returns 0 on end/error.
  static uint32_t nextCodepoint(const char*& p, const char* end);

  // Encode one codepoint as UTF-8, advance pointer. Returns bytes written.
  static size_t encodeCodepoint(uint32_t cp, char*& out, const char* outEnd);

  // Try to match the longest rule starting at the current position.
  // Returns output codepoint if matched, 0 if no match.
  // On match, advances 'cps' index past consumed input codepoints.
  static uint32_t tryMatch(const uint32_t* cps, size_t count, size_t& pos);
};
