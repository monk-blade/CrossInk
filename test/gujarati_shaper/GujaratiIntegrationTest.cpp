#include <gtest/gtest.h>

#include <string>

#include "GujaratiIntegration.h"
#include "Utf8.h"

namespace {
void appendCp(std::string& s, const uint32_t cp) {
  if (cp < 0x80) {
    s += static_cast<char>(cp);
  } else if (cp < 0x800) {
    s += static_cast<char>(0xC0 | (cp >> 6));
    s += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    s += static_cast<char>(0xE0 | (cp >> 12));
    s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    s += static_cast<char>(0x80 | (cp & 0x3F));
  }
}
}  // namespace

TEST(GujaratiIntegration, ContainsGujaratiDetectsBlockAndPua) {
  EXPECT_FALSE(GujaratiIntegration::containsGujarati("hello"));
  std::string guj;
  appendCp(guj, 0x0A95);  // ka
  EXPECT_TRUE(GujaratiIntegration::containsGujarati(guj));
}

TEST(GujaratiIntegration, ShapeWordIsNoOpForNonGujaratiText) {
  std::string ascii = "hello world";
  GujaratiIntegration::shapeWord(ascii);
  EXPECT_EQ(ascii, "hello world");
}

TEST(GujaratiIntegration, ShapeWordShapesAGujaratiWord) {
  // ka + virama -> single half-form PUA codepoint (see GujaratiShaperTest.KaVirama)
  std::string word;
  appendCp(word, 0x0A95);
  appendCp(word, 0x0ACD);
  const std::string original = word;
  GujaratiIntegration::shapeWord(word);
  EXPECT_NE(word, original);
  EXPECT_TRUE(GujaratiIntegration::containsGujarati(word));
}

TEST(GujaratiIntegration, ShapeWordSanitizesMalformedUtf8Input) {
  std::string malformed = "ok\x80";  // 0x80 is a bare/invalid UTF-8 continuation byte
  appendCp(malformed, 0x0A95);
  appendCp(malformed, 0x0ACD);
  GujaratiIntegration::shapeWord(malformed);
  // The shaped half-form PUA codepoint legitimately contains 0x80 as a
  // continuation byte in its own 3-byte encoding, so checking for that raw
  // byte isn't meaningful. Assert instead that "ok" survived untouched and
  // that nothing decodes as U+FFFD (the sanitizer's replacement marker).
  EXPECT_EQ(malformed.rfind("ok", 0), 0u);
  auto sanitized = malformed;
  utf8SanitizeInPlace(sanitized);
  EXPECT_EQ(sanitized, malformed) << "shaped output should already be clean UTF-8";
}

TEST(GujaratiIntegration, ShapeSanitizedWordSkipsUtf8Sanitization) {
  // A word that is already known-clean (per the doc comment, RSS rich-text
  // words are pre-sanitized by HtmlRichText) still gets shaped.
  std::string word;
  appendCp(word, 0x0A95);
  appendCp(word, 0x0ACD);
  const std::string original = word;
  GujaratiIntegration::shapeSanitizedWord(word);
  EXPECT_NE(word, original);
}

TEST(GujaratiIntegration, ShapeUiStringPreservesWhitespaceAndShapesEachWord) {
  std::string text = "hello ";
  appendCp(text, 0x0A95);
  appendCp(text, 0x0ACD);
  text += "\nworld";
  const std::string original = text;
  GujaratiIntegration::shapeUiString(text);
  // The ASCII portions and the newline must survive untouched; only the
  // Gujarati word changes.
  EXPECT_NE(text, original);
  EXPECT_NE(text.find("hello "), std::string::npos);
  EXPECT_NE(text.find("\nworld"), std::string::npos);
}

// Regression test: shapeUiString() used to call GujaratiShaper::shape()
// directly on the whole string, which silently dropped everything past its
// internal ~128-codepoint window. It now splits on whitespace first (like
// shapeLongUiString), so an unbounded number of short words is safe.
TEST(GujaratiIntegration, ShapeUiStringDoesNotDropWordsInALongString) {
  std::string text;
  for (int i = 0; i < 200; ++i) {
    if (i != 0) text += ' ';
    appendCp(text, 0x0A95);  // ka, standalone -- passes through unshaped (no virama)
  }
  GujaratiIntegration::shapeUiString(text);
  size_t spaceCount = 0;
  for (const char c : text) {
    if (c == ' ') ++spaceCount;
  }
  // All 199 separating spaces must survive — if any word had been dropped
  // (the bug this guards against), the space count would come up short.
  EXPECT_EQ(spaceCount, 199u);
  EXPECT_EQ(text.size(), 200u * 3 + 199u);  // U+0A95 is a 3-byte UTF-8 sequence
}

TEST(GujaratiIntegration, ShapeLongUiStringMatchesShapeUiStringForShortInput) {
  std::string a;
  appendCp(a, 0x0A95);
  appendCp(a, 0x0ACD);
  appendCp(a, 0x0AA5);
  std::string b = a;
  GujaratiIntegration::shapeUiString(a);
  GujaratiIntegration::shapeLongUiString(b);
  EXPECT_EQ(a, b);
}

// A single "word" too long to fit the shaping buffer (see kShapedWordBufferBytes
// in GujaratiIntegration.cpp) is left unshaped rather than truncated mid-output.
TEST(GujaratiIntegration, PathologicallyLongWordIsLeftUnshapedNotTruncated) {
  std::string word;
  for (int i = 0; i < 200; ++i) {
    appendCp(word, 0x0A95);  // 200 codepoints, no whitespace to split on
  }
  const std::string original = word;
  GujaratiIntegration::shapeWord(word);
  EXPECT_EQ(word, original);
}
