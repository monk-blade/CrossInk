#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "GujaratiShaper.h"

namespace {

std::string shapeStr(const std::string& in) {
  char buf[512];
  size_t len = GujaratiShaper::shape(in.data(), in.size(), buf, sizeof(buf));
  return std::string(buf, len);
}

void appendCp(std::string& s, uint32_t cp) {
  char buf[4];
  char* out = buf;
  size_t n = 0;
  if (cp < 0x80) {
    buf[0] = static_cast<char>(cp);
    n = 1;
  } else if (cp < 0x800) {
    buf[0] = static_cast<char>(0xC0 | (cp >> 6));
    buf[1] = static_cast<char>(0x80 | (cp & 0x3F));
    n = 2;
  } else {
    buf[0] = static_cast<char>(0xE0 | (cp >> 12));
    buf[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    buf[2] = static_cast<char>(0x80 | (cp & 0x3F));
    n = 3;
  }
  (void)out;
  s.append(buf, n);
}

std::vector<uint32_t> decodeCps(const std::string& shaped) {
  const auto* p = reinterpret_cast<const unsigned char*>(shaped.data());
  const auto* end = p + shaped.size();
  std::vector<uint32_t> cps;
  while (p < end) {
    uint32_t cp;
    if (*p < 0x80) {
      cp = *p;
      p += 1;
    } else if ((*p & 0xE0) == 0xC0) {
      cp = (*p & 0x1F) << 6 | (p[1] & 0x3F);
      p += 2;
    } else {
      cp = (*p & 0x0F) << 12 | (p[1] & 0x3F) << 6 | (p[2] & 0x3F);
      p += 3;
    }
    cps.push_back(cp);
  }
  return cps;
}

}  // namespace

// Plain Gujarati text with no conjuncts/virama should pass through byte-identical.
TEST(GujaratiShaper, PassesThroughSimpleText) {
  std::string word;
  appendCp(word, 0x0A95);  // ka
  appendCp(word, 0x0AAE);  // ma
  appendCp(word, 0x0ABE);  // aa-matra (post-base, no reorder)
  EXPECT_EQ(shapeStr(word), word);
}

// ASCII / non-Gujarati text passes through untouched.
TEST(GujaratiShaper, PassesThroughAscii) { EXPECT_EQ(shapeStr("hello"), "hello"); }

// RSS may reopen a snapshot written before cache-time shaping and therefore
// shape a word that is already in the PUA representation.  The compatibility
// pass must leave current section/cache output unchanged.
TEST(GujaratiShaper, ShapingIsIdempotent) {
  for (const std::string word : {"મેઘ", "અગાઉ", "કર્યું", "સંઘર્ષથી", "ક્ષમતા", "દર્દ"}) {
    const std::string shaped = shapeStr(word);
    ASSERT_FALSE(shaped.empty());
    EXPECT_EQ(shapeStr(shaped), shaped) << word;
  }
}

// containsGujarati correctly detects Gujarati-block and PUA codepoints, and
// rejects plain ASCII.
TEST(GujaratiShaper, ContainsGujaratiDetection) {
  std::string ascii = "hello";
  EXPECT_FALSE(GujaratiShaper::containsGujarati(ascii.c_str(), ascii.size()));

  std::string guj;
  appendCp(guj, 0x0A95);
  EXPECT_TRUE(GujaratiShaper::containsGujarati(guj.c_str(), guj.size()));
}

// Ka + Virama alone (no following consonant) shapes to a single PUA half-form
// codepoint — this documents current behavior for tracking down the "every
// glyph is a box" bug: is this half-form codepoint actually present in the
// rasterized font's PUA interval?
TEST(GujaratiShaper, KaVirama) {
  std::string word;
  appendCp(word, 0x0A95);  // ka
  appendCp(word, 0x0ACD);  // virama
  std::string shaped = shapeStr(word);

  // Decode the shaped output back to codepoints for inspection.
  const auto* p = reinterpret_cast<const unsigned char*>(shaped.data());
  const auto* end = p + shaped.size();
  std::vector<uint32_t> cps;
  while (p < end) {
    uint32_t cp;
    if (*p < 0x80) {
      cp = *p;
      p += 1;
    } else if ((*p & 0xE0) == 0xC0) {
      cp = (*p & 0x1F) << 6 | (p[1] & 0x3F);
      p += 2;
    } else {
      cp = (*p & 0x0F) << 12 | (p[1] & 0x3F) << 6 | (p[2] & 0x3F);
      p += 3;
    }
    cps.push_back(cp);
  }
  ASSERT_EQ(cps.size(), 1u);
  EXPECT_GE(cps[0], 0xE000u);
  EXPECT_LE(cps[0], 0xF8FFu);
}

// Ka + Virama + Tha (a real conjunct: ka+virama+tha) should shape to a single
// output codepoint via the 3-char rule table (or chain through 2-char rules).
TEST(GujaratiShaper, KaViramaTha) {
  std::string word;
  appendCp(word, 0x0A95);  // ka
  appendCp(word, 0x0ACD);  // virama
  appendCp(word, 0x0AA5);  // tha
  std::string shaped = shapeStr(word);
  EXPECT_GT(shaped.size(), 0u);
}

// Pre-base matra (િ) is reordered ahead of its consonant cluster for shaping;
// GfxRenderer draws the shaped stream sequentially (font metrics place the matra).
TEST(GujaratiShaper, ReordersPreBaseMatraBeforeConsonant) {
  std::string word;
  appendCp(word, 0x0AAE);  // ma
  appendCp(word, 0x0ABF);  // i matra
  appendCp(word, 0x0AA4);  // ta
  appendCp(word, 0x0ACD);  // virama
  appendCp(word, 0x0AB0);  // ra
  std::string shaped = shapeStr(word);
  auto cps = decodeCps(shaped);
  ASSERT_GE(cps.size(), 2u);
  EXPECT_EQ(cps[0], 0x0ABFu);
  EXPECT_EQ(cps[1], 0x0AAEu);
}

TEST(GujaratiShaper, RepositionsRephAfterBaseConsonant) {
  std::string word;
  appendCp(word, 0x0AB5);  // va
  appendCp(word, 0x0ABE);  // aa
  appendCp(word, 0x0AB0);  // ra
  appendCp(word, 0x0ACD);  // virama
  appendCp(word, 0x0AA4);  // ta
  appendCp(word, 0x0ABE);  // aa
  std::string shaped = shapeStr(word);
  auto cps = decodeCps(shaped);
  ASSERT_EQ(cps.size(), 5u);
  EXPECT_EQ(cps[0], 0x0AB5u);
  EXPECT_EQ(cps[1], 0x0ABEu);
  EXPECT_EQ(cps[2], 0x0AA4u);
  EXPECT_EQ(cps[3], 0x0ABEu);
  EXPECT_EQ(cps[4], GujaratiShaper::REPH_GLYPH);
}

TEST(GujaratiShaper, SubjoinedRaForTroy) {
  std::string word;
  appendCp(word, 0x0A9F);  // ta
  appendCp(word, 0x0ACD);  // virama
  appendCp(word, 0x0AB0);  // ra
  appendCp(word, 0x0ACB);  // o matra
  appendCp(word, 0x0AAF);  // ya
  std::string shaped = shapeStr(word);
  auto cps = decodeCps(shaped);
  ASSERT_EQ(cps.size(), 4u);
  EXPECT_EQ(cps[0], 0x0A9Fu);
  EXPECT_EQ(cps[1], GujaratiShaper::SUBJOINED_RA_GLYPH);
  EXPECT_EQ(cps[2], 0x0ACBu);
  EXPECT_EQ(cps[3], 0x0AAFu);
}

TEST(GujaratiShaper, RephClusterIncludesViramaConsonantTail) {
  std::string word;
  appendCp(word, 0x0AB5);  // va
  appendCp(word, 0x0AB0);  // ra
  appendCp(word, 0x0ACD);  // virama
  appendCp(word, 0x0AB2);  // la
  appendCp(word, 0x0ACD);  // virama
  appendCp(word, 0x0AA1);  // da
  std::string shaped = shapeStr(word);
  auto cps = decodeCps(shaped);
  ASSERT_EQ(cps.size(), 4u);
  EXPECT_EQ(cps[0], 0x0AB5u);
  EXPECT_EQ(cps[1], 0xE066u);
  EXPECT_EQ(cps[2], 0x0AA1u);
  EXPECT_EQ(cps[3], GujaratiShaper::REPH_GLYPH);
}

TEST(GujaratiShaper, RephDoesNotSwallowPreBaseMatra) {
  std::string word;
  appendCp(word, 0x0AB8);  // sa
  appendCp(word, 0x0A82);  // anusvara
  appendCp(word, 0x0A98);  // gha
  appendCp(word, 0x0AB0);  // ra
  appendCp(word, 0x0ACD);  // virama
  appendCp(word, 0x0AB7);  // ssha
  appendCp(word, 0x0AA5);  // tha
  appendCp(word, 0x0ABF);  // i matra
  std::string shaped = shapeStr(word);
  auto cps = decodeCps(shaped);
  ASSERT_EQ(cps.size(), 7u);
  EXPECT_EQ(cps[0], 0x0AB8u);
  EXPECT_EQ(cps[1], 0x0A82u);
  EXPECT_EQ(cps[2], 0x0A98u);
  EXPECT_EQ(cps[3], 0x0AB7u);
  EXPECT_EQ(cps[4], GujaratiShaper::REPH_GLYPH);
  EXPECT_EQ(cps[5], 0x0ABFu);
  EXPECT_EQ(cps[6], 0x0AA5u);
}

TEST(GujaratiShaper, ThermometerKeepsRephAfterPostBaseOMatra) {
  const auto cps = decodeCps(shapeStr("થર્મોમીટર"));
  EXPECT_EQ(cps, (std::vector<uint32_t>{0x0AA5, 0x0AAE, 0x0ACB, GujaratiShaper::REPH_GLYPH, 0x0AAE, 0x0AC0,
                                       0x0A9F, 0x0AB0}));
}

TEST(GujaratiShaper, RavipurtiKeepsPreBaseIMatraBeforeRephCluster) {
  const auto cps = decodeCps(shapeStr("રવિપૂર્તિ"));
  EXPECT_EQ(cps, (std::vector<uint32_t>{0x0AB0, 0x0ABF, 0x0AB5, 0x0AAA, 0x0AC2, 0x0ABF, 0x0AA4,
                                       GujaratiShaper::REPH_GLYPH}));
}

// Regression corpus from field-tested Gujarati words.
TEST(GujaratiShaper, CorpusMitra) {
  const auto cps = decodeCps(shapeStr("મિત્ર"));
  ASSERT_GE(cps.size(), 2u);
  EXPECT_EQ(cps[0], 0x0ABFu);
}

TEST(GujaratiShaper, CorpusUiTitlesKeepPreBaseIMatrasWithTheirConsonants) {
  EXPECT_EQ(decodeCps(shapeStr("લલિત")),
            (std::vector<uint32_t>{0x0AB2, 0x0ABF, 0x0AB2, 0x0AA4}));
  EXPECT_EQ(decodeCps(shapeStr("વિવિધ")),
            (std::vector<uint32_t>{0x0ABF, 0x0AB5, 0x0ABF, 0x0AB5, 0x0AA7}));
}

TEST(GujaratiShaper, CorpusMaam) {
  const auto cps = decodeCps(shapeStr("માં"));
  ASSERT_EQ(cps.size(), 3u);
  EXPECT_EQ(cps[0], 0x0AAEu);
  EXPECT_EQ(cps[1], 0x0ABEu);
  EXPECT_EQ(cps[2], 0x0A82u);
}

TEST(GujaratiShaper, CorpusBharatMaam) {
  const auto cps = decodeCps(shapeStr("ભારતમાં"));
  ASSERT_GE(cps.size(), 3u);
  EXPECT_EQ(cps.back(), 0x0A82u);
}

TEST(GujaratiShaper, CorpusKaryu) {
  const auto cps = decodeCps(shapeStr("કર્યું"));
  ASSERT_EQ(cps.size(), 5u);
  EXPECT_EQ(cps[0], 0x0A95u);
  EXPECT_EQ(cps[3], GujaratiShaper::REPH_GLYPH);
  EXPECT_EQ(cps[4], 0x0A82u);
}

TEST(GujaratiShaper, CorpusSangharshthi) {
  const auto cps = decodeCps(shapeStr("સંઘર્ષથી"));
  ASSERT_FALSE(cps.empty());
  EXPECT_NE(cps[0], GujaratiShaper::REPH_GLYPH);
}

TEST(GujaratiShaper, CorpusKshamata) {
  const auto cps = decodeCps(shapeStr("ક્ષમતા"));
  ASSERT_GE(cps.size(), 2u);
  EXPECT_GE(cps[0], 0xE000u);
}

TEST(GujaratiShaper, CorpusDard) {
  const auto cps = decodeCps(shapeStr("દર્દ"));
  ASSERT_GE(cps.size(), 2u);
  EXPECT_EQ(cps.back(), GujaratiShaper::REPH_GLYPH);
}

// Regression test: shape() used to decode at most MAX_WORD_CPS (128)
// codepoints and silently drop everything past that instead of continuing.
// A long whole-string caller (e.g. GujaratiIntegration::shapeUiString on an
// unusually long title) must not lose text past the 128th codepoint.
TEST(GujaratiShaper, DoesNotDropCodepointsBeyondShapingWindow) {
  constexpr int kRepeats = 200;  // > MAX_WORD_CPS (128)
  std::string word;
  for (int i = 0; i < kRepeats; ++i) {
    appendCp(word, 0x0A95);  // standalone "ka" — no virama/matra, so no rule
                             // touches it and every copy should pass through
                             // unchanged regardless of chunk boundaries.
  }
  char buf[kRepeats * 3 + 16];
  const size_t len = GujaratiShaper::shape(word.data(), word.size(), buf, sizeof(buf));
  const auto cps = decodeCps(std::string(buf, len));
  ASSERT_EQ(cps.size(), static_cast<size_t>(kRepeats));
  for (const uint32_t cp : cps) EXPECT_EQ(cp, 0x0A95u);
}

// Regression test: U+0AC0 (ii-matra) is post-base, like U+0ABE (aa-matra) —
// only U+0ABF (i-matra) is pre-base. It used to be excluded from the reph
// cluster on the mistaken assumption that it was pre-base too, which stopped
// the reph target one codepoint short whenever a word ended in ii-matra.
TEST(GujaratiShaper, RephClusterIncludesPostBaseIiMatra) {
  std::string word;
  appendCp(word, 0x0AB5);  // va
  appendCp(word, 0x0ABE);  // aa
  appendCp(word, 0x0AB0);  // ra
  appendCp(word, 0x0ACD);  // virama
  appendCp(word, 0x0AA4);  // ta
  appendCp(word, 0x0AC0);  // ii matra (post-base)
  auto cps = decodeCps(shapeStr(word));
  ASSERT_EQ(cps.size(), 5u);
  EXPECT_EQ(cps[0], 0x0AB5u);
  EXPECT_EQ(cps[1], 0x0ABEu);
  EXPECT_EQ(cps[2], 0x0AA4u);
  EXPECT_EQ(cps[3], 0x0AC0u);
  EXPECT_EQ(cps[4], GujaratiShaper::REPH_GLYPH);
}

// Regression test: U+0AF9 GUJARATI LETTER ZHA sits outside the contiguous
// 0x0A95-0x0AB9 consonant block but the shaping tables include rules keyed on
// it. It used to be invisible to isGujaratiConsonant(), so it could never be
// a reph base even though ra+virama+zha is exactly the reph pattern.
TEST(GujaratiShaper, RephFormsBeforeZhaConsonant) {
  std::string word;
  appendCp(word, 0x0AB0);  // ra
  appendCp(word, 0x0ACD);  // virama
  appendCp(word, 0x0AF9);  // zha
  auto cps = decodeCps(shapeStr(word));
  ASSERT_EQ(cps.size(), 2u);
  EXPECT_EQ(cps[0], 0x0AF9u);
  EXPECT_EQ(cps[1], GujaratiShaper::REPH_GLYPH);
}

// A ka+virama pair near the end of a long string (past the first shaping
// window) must still shape to its half-form — regression coverage for the
// chunk boundary landing well clear of the interesting text, not just for
// raw pass-through codepoints. Mirrors the KaVirama test's expectations.
TEST(GujaratiShaper, ShapesHalfFormPastFirstShapingWindow) {
  std::string word;
  for (int i = 0; i < 150; ++i) {
    appendCp(word, 0x0A96);  // padding: 150 standalone "kha" (> MAX_WORD_CPS),
                             // distinct from ka/tha below so the tail is unambiguous
  }
  appendCp(word, 0x0A95);  // ka
  appendCp(word, 0x0ACD);  // virama  -> ka+virama half-form (see KaVirama)
  appendCp(word, 0x0AA5);  // tha, unaffected (no rule chains a half-form + consonant)
  char buf[1024];
  const size_t len = GujaratiShaper::shape(word.data(), word.size(), buf, sizeof(buf));
  const auto cps = decodeCps(std::string(buf, len));
  ASSERT_EQ(cps.size(), 152u);  // 150 padding + [half-ka, tha]
  for (size_t i = 0; i < 150; ++i) EXPECT_EQ(cps[i], 0x0A96u) << "padding index " << i;
  EXPECT_GE(cps[150], 0xE000u);
  EXPECT_LE(cps[150], 0xF8FFu);
  EXPECT_EQ(cps[151], 0x0AA5u);
}
