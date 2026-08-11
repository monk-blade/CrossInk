#include <gtest/gtest.h>

#include <string>

#include "HtmlRichText.h"

namespace {
std::string wordText(const RichText& text, const size_t paragraph, const size_t word) {
  return text.at(paragraph).words.at(word).text;
}
}  // namespace

TEST(HtmlRichText, StripsTagsAndKeepsPlainText) {
  const RichText result = HtmlRichText::convert("<p>hello <a href=\"x\">world</a></p>", 1024);
  ASSERT_EQ(result.size(), 1u);
  ASSERT_EQ(result[0].words.size(), 2u);
  EXPECT_EQ(wordText(result, 0, 0), "hello");
  EXPECT_EQ(wordText(result, 0, 1), "world");
}

TEST(HtmlRichText, DecodesNamedAndNumericEntities) {
  const RichText result = HtmlRichText::convert("Tom &amp; Jerry &lt;3 caf&#233; &#x2013; done", 1024);
  ASSERT_EQ(result.size(), 1u);
  const auto& words = result[0].words;
  ASSERT_EQ(words.size(), 7u);
  EXPECT_EQ(words[1].text, "&");
  EXPECT_EQ(words[3].text, "<3");
  EXPECT_EQ(words[4].text, "caf\xC3\xA9");  // é (U+00E9)
  EXPECT_EQ(words[5].text, "\xE2\x80\x93");  // – (U+2013 EN DASH)
}

TEST(HtmlRichText, UnknownNamedEntityIsSilentlyDropped) {
  // "foobar" is short enough to be treated as a real (if unrecognized) entity
  // name — see the MAX_ENTITY_LEN case below for the "too long" fallback.
  const RichText result = HtmlRichText::convert("a&foobar;b", 1024);
  ASSERT_EQ(result.size(), 1u);
  ASSERT_EQ(result[0].words.size(), 1u);
  EXPECT_EQ(wordText(result, 0, 0), "ab");
}

TEST(HtmlRichText, OverlongEntityNameIsTreatedAsLiteralText) {
  // decodeEntity gives up on an entity name longer than its internal cap and
  // falls back to emitting '&' literally, leaving the rest as plain text —
  // it must not consume/drop text past the '&' in that case.
  const RichText result = HtmlRichText::convert("a&muchtoolongtobeanentity;b", 1024);
  ASSERT_EQ(result.size(), 1u);
  ASSERT_EQ(result[0].words.size(), 1u);
  EXPECT_EQ(wordText(result, 0, 0), "a&muchtoolongtobeanentity;b");
}

TEST(HtmlRichText, SkipsScriptAndStyleElementBodies) {
  const RichText result =
      HtmlRichText::convert("before<script>var x = 1 < 2;</script><style>.a{color:red}</style>after", 1024);
  ASSERT_EQ(result.size(), 1u);
  ASSERT_EQ(result[0].words.size(), 2u);
  EXPECT_EQ(wordText(result, 0, 0), "before");
  EXPECT_EQ(wordText(result, 0, 1), "after");
}

TEST(HtmlRichText, TracksBoldAndItalicStyleAcrossNestedTags) {
  const RichText result = HtmlRichText::convert("plain <b>bold <i>bolditalic</i></b> plain", 1024);
  ASSERT_EQ(result.size(), 1u);
  const auto& words = result[0].words;
  ASSERT_EQ(words.size(), 4u);
  EXPECT_EQ(words[0].style, EpdFontFamily::REGULAR);
  EXPECT_EQ(words[1].style, EpdFontFamily::BOLD);
  EXPECT_EQ(words[2].style, EpdFontFamily::BOLD_ITALIC);
  EXPECT_EQ(words[3].style, EpdFontFamily::REGULAR);
}

TEST(HtmlRichText, MidWordStyleChangeSetsContinuesPrevious) {
  // "bo" + "ld" from "<b>bo</b>ld" — a style change happens mid-word, not at
  // a word boundary, so the second half must stay glued to the first.
  const RichText result = HtmlRichText::convert("<b>bo</b>ld", 1024);
  ASSERT_EQ(result.size(), 1u);
  const auto& words = result[0].words;
  ASSERT_EQ(words.size(), 2u);
  EXPECT_EQ(words[0].text, "bo");
  EXPECT_FALSE(words[0].continuesPrevious);
  EXPECT_EQ(words[1].text, "ld");
  EXPECT_TRUE(words[1].continuesPrevious);
}

TEST(HtmlRichText, RealWhitespaceAlwaysStartsANewBoundary) {
  const RichText result = HtmlRichText::convert("<b>bo</b> ld", 1024);
  ASSERT_EQ(result.size(), 1u);
  const auto& words = result[0].words;
  ASSERT_EQ(words.size(), 2u);
  EXPECT_FALSE(words[1].continuesPrevious);
}

TEST(HtmlRichText, BlockTagsStartNewParagraphs) {
  const RichText result = HtmlRichText::convert("<p>first</p><p>second</p>", 1024);
  ASSERT_EQ(result.size(), 2u);
  EXPECT_EQ(wordText(result, 0, 0), "first");
  EXPECT_EQ(wordText(result, 1, 0), "second");
}

TEST(HtmlRichText, CenterTagSetsParagraphAlignment) {
  // Alignment is only captured when a paragraph actually finalizes (a block
  // tag, or end of input) while still inside <center> — <center> itself
  // isn't a block tag and doesn't finalize anything on its own, so an inner
  // block tag is needed for the centered paragraph to close within scope.
  const RichText centered = HtmlRichText::convert("<center><p>middle</p></center>", 1024);
  ASSERT_EQ(centered.size(), 1u);
  EXPECT_EQ(centered[0].align, TextAlign::CENTER);

  const RichText plain = HtmlRichText::convert("<p>left</p>", 1024);
  ASSERT_EQ(plain.size(), 1u);
  EXPECT_EQ(plain[0].align, TextAlign::INHERIT);
}

// A bare <center>...</center> with no other paragraph break inside it closes
// (decrementing centerDepth_) before the paragraph is ever finalized, so the
// alignment capture in finalizeParagraph() never observes it. Documents
// current behavior rather than asserting it's ideal.
TEST(HtmlRichText, BareCenterTagWithoutInnerBlockTagDoesNotCenter) {
  const RichText result = HtmlRichText::convert("<center>middle</center>", 1024);
  ASSERT_EQ(result.size(), 1u);
  EXPECT_EQ(result[0].align, TextAlign::INHERIT);
}

TEST(HtmlRichText, AlignCenterAttributeSetsParagraphAlignment) {
  // Deliberately dumb attribute handling: any occurrence of the substring
  // "center" in the tag's attributes, not real CSS/attribute parsing.
  const RichText result = HtmlRichText::convert("<p align=\"center\">middle</p>", 1024);
  ASSERT_EQ(result.size(), 1u);
  EXPECT_EQ(result[0].align, TextAlign::CENTER);
}

TEST(HtmlRichText, MaxCharsBoundsTotalAccumulatedTextNotRawHtmlLength) {
  const RichText result = HtmlRichText::convert("<p>aaaaaaaaaa bbbbbbbbbb cccccccccc</p>", 12);
  ASSERT_FALSE(result.empty());
  size_t total = 0;
  for (const auto& paragraph : result)
    for (const auto& word : paragraph.words) total += word.text.size();
  EXPECT_LE(total, 12u);
}

TEST(HtmlRichText, HardCutAtMaxCharsStaysOnAUtf8Boundary) {
  // café repeated so the maxChars cut is very likely to land mid-codepoint
  // (é is 2 bytes) unless utf8SafeTruncateBuffer backs it up.
  std::string html;
  for (int i = 0; i < 20; ++i) html += "caf\xC3\xA9 ";
  const RichText result = HtmlRichText::convert(html, 11);
  ASSERT_FALSE(result.empty());
  const std::string& lastWord = result.back().words.back().text;
  // A truncated multi-byte sequence would leave a lone lead byte (0xC3) at
  // the end with no continuation byte following it.
  if (!lastWord.empty()) {
    const auto lastByte = static_cast<unsigned char>(lastWord.back());
    EXPECT_TRUE(lastByte < 0x80 || (lastByte & 0xC0) != 0xC0)
        << "word ends with an incomplete multi-byte UTF-8 lead byte";
  }
}

TEST(HtmlRichText, EmptyInputProducesNoParagraphs) { EXPECT_TRUE(HtmlRichText::convert("", 1024).empty()); }

TEST(HtmlRichText, WhitespaceOnlyInputProducesNoParagraphs) {
  EXPECT_TRUE(HtmlRichText::convert("   \n\t  ", 1024).empty());
}

TEST(HtmlRichText, ReportsProgressAsInputIsScanned) {
  std::string html(3000, 'a');
  html[500] = ' ';
  html[1500] = ' ';
  html[2500] = ' ';
  std::vector<size_t> reported;
  HtmlRichText::convert(html, 1024 * 1024, [&reported](const size_t bytes) { reported.push_back(bytes); });
  ASSERT_FALSE(reported.empty());
  EXPECT_EQ(reported.back(), html.size());
  for (size_t i = 1; i < reported.size(); ++i) EXPECT_GE(reported[i], reported[i - 1]);
}
