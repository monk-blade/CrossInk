#pragma once

#include <EpdFontFamily.h>

#include <string>
#include <vector>

// How a paragraph should be laid out. INHERIT means "no explicit HTML
// alignment was found for this paragraph" — callers resolve it against the
// user's own reading preference (CrossPointSettings::paragraphAlignment),
// the same setting the EPUB and TXT readers already honor. An explicit
// <center> (or align="center" / a "center" text-align style) always wins
// over that default, since it's the content's own semantic markup.
// Keep the explicit values stable because paragraph alignment is serialized in
// the RSS caches. Arduino defines DEFAULT as a macro, so it cannot be used as
// an enum member in firmware translation units.
enum class TextAlign : uint8_t { INHERIT = 0, LEFT = 1, CENTER = 2, RIGHT = 3, JUSTIFY = 4 };

// One word (or word-fragment — see continuesPrevious) with the style it
// should render in. Word-level, not run-level: this is what the line-wrap
// layout in RssArticleActivity iterates directly, one measure+place per
// word, so bold/italic can change mid-line without any further splitting.
struct StyledWord {
  std::string text;
  EpdFontFamily::Style style = EpdFontFamily::REGULAR;
  // True when this word has no space before it and must stay glued to the
  // previous one — e.g. "bo" + "ld" from "<b>bo</b>ld", where a style change
  // happened mid-word rather than at a word boundary. Rare in real feed
  // content (styling tags almost always wrap whole words), but without this
  // flag the layout would insert a phantom space in the middle of the word.
  bool continuesPrevious = false;
};

struct RichParagraph {
  std::vector<StyledWord> words;
  TextAlign align = TextAlign::INHERIT;
};

using RichText = std::vector<RichParagraph>;
