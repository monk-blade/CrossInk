#include "GujaratiShaper.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "GujaratiShapingData.h"

using namespace GujaratiShapingData;

// Maximum codepoints in a single word we'll process
static constexpr size_t MAX_WORD_CPS = 128;

static inline bool isGujarati(uint32_t cp) {
  return (cp >= 0x0A80 && cp <= 0x0AFF) || (cp >= PUA_START && cp <= PUA_END);
}

static inline bool isGujaratiConsonant(uint32_t cp) {
  // U+0AF9 GUJARATI LETTER ZHA sits outside the main 0x0A95-0x0AB9 consonant
  // block (Gujarati Unicode additions), but the shaping tables include rules
  // keyed on it (see GujaratiShapingData.h) — without this it can't be a
  // reph/subjoined-Ra base or take part in pre-base matra reordering.
  return (cp >= 0x0A95 && cp <= 0x0AB9) || cp == 0x0AF9;
}

static inline bool isVirama(uint32_t cp) { return cp == 0x0ACD; }

static inline bool isPreBaseMatra(uint32_t cp) { return cp == 0x0ABF; }

// rphf output (uni0AB00ACD) — GfxRenderer overlays this on the preceding consonant.
static constexpr uint32_t REPH_GLYPH = 0xE065;
// blwf output (uni0ACD0AB0) — GfxRenderer overlays this below the preceding consonant.
static constexpr uint32_t SUBJOINED_RA_GLYPH = 0xE07A;

static inline bool isRephBase(uint32_t cp) {
  return isGujaratiConsonant(cp) || (cp >= PUA_START && cp <= PUA_END);
}

// Post-base vowel matras that stay in the same syllable as the consonant reph attaches to.
// Only U+0ABF (i-matra) is pre-base and must be excluded — it is reordered
// before the consonant cluster and must not extend the reph target (e.g.
// સંઘર્ષથી). U+0AC0 (ii-matra) is post-base, like U+0ABE, despite visually
// resembling U+0ABF; excluding it here (as an earlier version of this
// function did) stopped the reph cluster one codepoint short whenever a word
// ended in ii-matra after a reph-eligible tail.
static inline bool isPostBaseVowelMatra(uint32_t cp) {
  return cp == 0x0ABE || cp == 0x0AC0 || cp == 0x0AC1 || cp == 0x0AC2 || (cp >= 0x0AC3 && cp <= 0x0AC5) ||
         cp == 0x0AC7 || cp == 0x0AC8 || cp == 0x0AC9 || cp == 0x0ACB || cp == 0x0ACC;
}

// RA+virama before a consonant is a reph (rphf): drop the RA+virama and emit the
// consonant cluster first, then the reph glyph (HarfBuzz draws reph after the base).
static void repositionReph(uint32_t* cps, size_t& count) {
  uint32_t out[MAX_WORD_CPS];
  size_t outCount = 0;

  for (size_t i = 0; i < count;) {
    if (i + 2 < count && cps[i] == 0x0AB0 && isVirama(cps[i + 1]) && isRephBase(cps[i + 2])) {
      const size_t clusterStart = i + 2;
      size_t clusterEnd = clusterStart + 1;
      // Include virama+consonant tails (e.g. ર+્+લ+્+ડ in વર્લ્ડ) before the reph glyph.
      while (clusterEnd + 1 < count && isVirama(cps[clusterEnd]) && isRephBase(cps[clusterEnd + 1])) {
        clusterEnd += 2;
      }
      while (clusterEnd < count && isPostBaseVowelMatra(cps[clusterEnd])) {
        clusterEnd++;
      }
      for (size_t j = clusterStart; j < clusterEnd && outCount < MAX_WORD_CPS; j++) {
        out[outCount++] = cps[j];
      }
      if (outCount < MAX_WORD_CPS) {
        out[outCount++] = REPH_GLYPH;
      }
      i = clusterEnd;
      continue;
    }
    if (outCount < MAX_WORD_CPS) {
      out[outCount++] = cps[i++];
    } else {
      i++;
    }
  }

  memcpy(cps, out, outCount * sizeof(uint32_t));
  count = outCount;
}

// blwf: consonant+virama+RA with no rkrf conjunct (e.g. ટ્ર) becomes consonant + subjoined Ra.
static void applySubjoinedRa(uint32_t* cps, size_t& count) {
  uint32_t out[MAX_WORD_CPS];
  size_t outCount = 0;

  for (size_t i = 0; i < count;) {
    if (i + 2 < count && isGujaratiConsonant(cps[i]) && isVirama(cps[i + 1]) && cps[i + 2] == 0x0AB0) {
      if (outCount + 2 > MAX_WORD_CPS) break;
      out[outCount++] = cps[i++];
      out[outCount++] = SUBJOINED_RA_GLYPH;
      i += 2;
      continue;
    }
    if (outCount < MAX_WORD_CPS) {
      out[outCount++] = cps[i++];
    } else {
      i++;
    }
  }

  memcpy(cps, out, outCount * sizeof(uint32_t));
  count = outCount;
}

// Reorder the pre-base matra to appear before its base consonant cluster.
// Shaped output order is matra + consonant [+ virama + consonant]*; GfxRenderer
// draws the stream sequentially and the font metrics position the matra.
static void reorderPreBaseMatras(uint32_t* cps, size_t& count) {
  for (size_t i = 1; i < count; i++) {
    if (!isPreBaseMatra(cps[i])) continue;

    size_t lastCons = i - 1;
    if (!isGujaratiConsonant(cps[lastCons])) continue;

    size_t clusterStart = lastCons;
    while (clusterStart >= 2 && isVirama(cps[clusterStart - 1]) && isGujaratiConsonant(cps[clusterStart - 2])) {
      clusterStart -= 2;
    }

    uint32_t matra = cps[i];
    memmove(&cps[clusterStart + 1], &cps[clusterStart], (i - clusterStart) * sizeof(uint32_t));
    cps[clusterStart] = matra;
  }
}
uint32_t GujaratiShaper::nextCodepoint(const char*& p, const char* end) {
  if (p >= end) return 0;
  uint8_t b = static_cast<uint8_t>(*p);
  uint32_t cp;
  int len;

  if (b < 0x80) {
    cp = b;
    len = 1;
  } else if ((b & 0xE0) == 0xC0) {
    cp = b & 0x1F;
    len = 2;
  } else if ((b & 0xF0) == 0xE0) {
    cp = b & 0x0F;
    len = 3;
  } else if ((b & 0xF8) == 0xF0) {
    cp = b & 0x07;
    len = 4;
  } else {
    p++;
    return 0xFFFD;
  }

  if (p + len > end) {
    p = end;
    return 0xFFFD;
  }

  for (int i = 1; i < len; i++) {
    uint8_t cb = static_cast<uint8_t>(p[i]);
    if ((cb & 0xC0) != 0x80) {
      p++;
      return 0xFFFD;
    }
    cp = (cp << 6) | (cb & 0x3F);
  }
  p += len;
  return cp;
}

size_t GujaratiShaper::encodeCodepoint(uint32_t cp, char*& out, const char* outEnd) {
  if (cp < 0x80) {
    if (out >= outEnd) return 0;
    *out++ = static_cast<char>(cp);
    return 1;
  } else if (cp < 0x800) {
    if (out + 2 > outEnd) return 0;
    *out++ = static_cast<char>(0xC0 | (cp >> 6));
    *out++ = static_cast<char>(0x80 | (cp & 0x3F));
    return 2;
  } else if (cp < 0x10000) {
    if (out + 3 > outEnd) return 0;
    *out++ = static_cast<char>(0xE0 | (cp >> 12));
    *out++ = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    *out++ = static_cast<char>(0x80 | (cp & 0x3F));
    return 3;
  } else {
    if (out + 4 > outEnd) return 0;
    *out++ = static_cast<char>(0xF0 | (cp >> 18));
    *out++ = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    *out++ = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    *out++ = static_cast<char>(0x80 | (cp & 0x3F));
    return 4;
  }
}

// Binary search for Rule2 by (in0, in1)
static uint32_t matchRule2(uint32_t in0, uint32_t in1) {
  int lo = 0, hi = static_cast<int>(rules2Count) - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    const auto& r = rules2[mid];
    if (r.in0 < in0 || (r.in0 == in0 && r.in1 < in1)) {
      lo = mid + 1;
    } else if (r.in0 > in0 || (r.in0 == in0 && r.in1 > in1)) {
      hi = mid - 1;
    } else {
      return r.out;
    }
  }
  return 0;
}

// Binary search for Rule3 by (in0, in1, in2)
static uint32_t matchRule3(uint32_t in0, uint32_t in1, uint32_t in2) {
  int lo = 0, hi = static_cast<int>(rules3Count) - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    const auto& r = rules3[mid];
    if (r.in0 < in0 || (r.in0 == in0 && r.in1 < in1) || (r.in0 == in0 && r.in1 == in1 && r.in2 < in2)) {
      lo = mid + 1;
    } else if (r.in0 > in0 || (r.in0 == in0 && r.in1 > in1) || (r.in0 == in0 && r.in1 == in1 && r.in2 > in2)) {
      hi = mid - 1;
    } else {
      return r.out;
    }
  }
  return 0;
}

uint32_t GujaratiShaper::tryMatch(const uint32_t* cps, size_t count, size_t& pos) {
  size_t remaining = count - pos;

  // Try longest match first (3-char) — NotoSerifGujarati's GSUB rules top
  // out at 3 input codepoints (see GujaratiShapingData.h); regenerate via
  // extract_shaping_rules.py and extend here if a future font needs longer
  // chains (the generator groups rules by whatever length actually occurs).
  if (remaining >= 3) {
    uint32_t out = matchRule3(cps[pos], cps[pos + 1], cps[pos + 2]);
    if (out) {
      pos += 3;
      return out;
    }
  }

  // Try 2-char match
  if (remaining >= 2) {
    uint32_t out = matchRule2(cps[pos], cps[pos + 1]);
    if (out) {
      pos += 2;
      return out;
    }
  }

  return 0;
}

size_t GujaratiShaper::shape(const char* input, size_t inputLen, char* output, size_t outputCap) {
  const char* p = input;
  const char* end = input + inputLen;
  char* out = output;
  const char* outEnd = output + outputCap;

  // Each shaping pass below looks within a single MAX_WORD_CPS-codepoint
  // window (conjunct/reph/matra-reorder rules never need more context than
  // that — MAX_WORD_CPS already covers far more than a real Gujarati word).
  // Callers are expected to split on whitespace before reaching this deep
  // (see GujaratiIntegration::shapeWord / shapeLongUiString), so in practice
  // this loop runs once. But shapeUiString() and other whole-string callers
  // can still hand us more than MAX_WORD_CPS codepoints; looping here shapes
  // the input in successive chunks instead of silently dropping everything
  // past the first MAX_WORD_CPS codepoints. The only correctness cost is that
  // a conjunct straddling a chunk boundary won't form — never observed with
  // real text, and strictly better than the data loss it replaces.
  while (p < end && out < outEnd) {
    // Decode one chunk of codepoints into a flat array for lookahead
    uint32_t cps[MAX_WORD_CPS];
    size_t cpCount = 0;

    while (p < end && cpCount < MAX_WORD_CPS) {
      cps[cpCount++] = nextCodepoint(p, end);
    }

    reorderPreBaseMatras(cps, cpCount);

    // Phase 1: Form conjuncts RIGHT-TO-LEFT within consonant clusters.
    //
    // In Indic shaping the "base" consonant is the rightmost in a cluster, so
    // conjuncts must form from the right. Example: ક્ષ્ય (ka+virama+ssa+virama+ya)
    //   Right-to-left: ssa+virama+ya -> ssya first, then ka+virama+ssya -> full form.
    //   Left-to-right would wrongly grab ka+virama+ssa first, orphaning virama+ya.
    //
    // We repeatedly scan right-to-left for the rightmost 3-char C+virama+C match
    // (or PUA+virama+C for triple+ stacking) and replace it, until no more form.
    {
      bool changed = true;
      while (changed) {
        changed = false;
        for (int i = static_cast<int>(cpCount) - 3; i >= 0; i--) {
          if (!isVirama(cps[i + 1])) continue;
          if (!isGujarati(cps[i]) || !isGujarati(cps[i + 2])) continue;
          uint32_t matched = matchRule3(cps[i], cps[i + 1], cps[i + 2]);
          if (matched) {
            cps[i] = matched;
            memmove(&cps[i + 1], &cps[i + 3], (cpCount - i - 3) * sizeof(uint32_t));
            cpCount -= 2;
            changed = true;
            break;  // restart scan from right after each replacement
          }
        }
      }
    }

    repositionReph(cps, cpCount);

    applySubjoinedRa(cps, cpCount);

    // Phase 2: Left-to-right rule application for remaining substitutions
    // (half-forms, PUA+consonant chaining, etc.).
    // Multi-pass needed because some rules chain (e.g., half-form + conjunct).
    {
      bool changed = true;
      while (changed) {
        changed = false;
        uint32_t newCps[MAX_WORD_CPS];
        size_t newCount = 0;
        size_t pos = 0;

        while (pos < cpCount) {
          if (isGujarati(cps[pos])) {
            uint32_t out_cp = tryMatch(cps, cpCount, pos);
            if (out_cp) {
              newCps[newCount++] = out_cp;
              changed = true;
              continue;
            }
          }
          newCps[newCount++] = cps[pos++];
        }

        if (changed) {
          memcpy(cps, newCps, newCount * sizeof(uint32_t));
          cpCount = newCount;
        }
      }
    }

    // Encode this chunk's shaped codepoints back to UTF-8. Stop entirely if
    // the output buffer runs out — same behavior as before chunking, just
    // reachable from any chunk instead of only the first.
    for (size_t i = 0; i < cpCount; i++) {
      if (encodeCodepoint(cps[i], out, outEnd) == 0) return static_cast<size_t>(out - output);
    }
  }

  return static_cast<size_t>(out - output);
}

bool GujaratiShaper::containsGujarati(const char* text, size_t len) {
  const char* p = text;
  const char* end = text + len;
  while (p < end) {
    uint32_t cp = nextCodepoint(p, end);
    if ((cp >= 0x0A80 && cp <= 0x0AFF) || (cp >= PUA_START && cp <= PUA_END)) return true;
  }
  return false;
}

void GujaratiShaper::shapeInPlace(std::string& text) {
  if (!containsGujarati(text.c_str(), text.size())) return;
  std::string shaped(text.size() * 2, '\0');
  const size_t shapedLen = shape(text.c_str(), text.size(), shaped.data(), shaped.size());
  if (shapedLen > 0) {
    text.assign(shaped.data(), shapedLen);
  }
}
