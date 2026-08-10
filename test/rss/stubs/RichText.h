#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace EpdFontFamily {
enum Style : uint8_t { REGULAR = 0, BOLD = 1, ITALIC = 2, BOLD_ITALIC = 3 };
}

enum class TextAlign : uint8_t { INHERIT = 0, LEFT = 1, CENTER = 2, RIGHT = 3, JUSTIFY = 4 };

struct StyledWord {
  std::string text;
  EpdFontFamily::Style style = EpdFontFamily::REGULAR;
  bool continuesPrevious = false;
};

struct RichParagraph {
  TextAlign align = TextAlign::INHERIT;
  std::vector<StyledWord> words;
};

using RichText = std::vector<RichParagraph>;
