#include "GujaratiIntegration.h"

#include "GujaratiShaper.h"

#include <Utf8.h>

namespace GujaratiIntegration {

namespace {
void shapeSanitized(std::string& word) {
  if (!GujaratiShaper::containsGujarati(word.c_str(), word.size())) {
    return;
  }
  char shaped[256];
  const size_t shapedLen = GujaratiShaper::shape(word.c_str(), word.size(), shaped, sizeof(shaped));
  if (shapedLen > 0) {
    word.assign(shaped, shapedLen);
  }
}
}  // namespace

void shapeWord(std::string& word) {
  utf8SanitizeInPlace(word);
  shapeSanitized(word);
}

void shapeSanitizedWord(std::string& word) { shapeSanitized(word); }

void shapeUiString(std::string& text) {
  utf8SanitizeInPlace(text);
  GujaratiShaper::shapeInPlace(text);
}

void shapeLongUiString(std::string& text) {
  utf8SanitizeInPlace(text);
  if (!containsGujarati(text)) return;

  std::string result;
  result.reserve(text.size());
  size_t i = 0;
  const auto isBreak = [](const char c) { return c == ' ' || c == '\n' || c == '\t' || c == '\r'; };

  while (i < text.size()) {
    const size_t wsStart = i;
    while (i < text.size() && isBreak(text[i])) ++i;
    result.append(text, wsStart, i - wsStart);  // copy whitespace/paragraph breaks verbatim

    const size_t wordStart = i;
    while (i < text.size() && !isBreak(text[i])) ++i;
    if (i > wordStart) {
      std::string word = text.substr(wordStart, i - wordStart);
      shapeWord(word);  // no-op if this particular word has no Gujarati in it
      result += word;
    }
  }
  text.swap(result);
}

bool containsGujarati(const std::string& text) { return GujaratiShaper::containsGujarati(text.c_str(), text.size()); }

}  // namespace GujaratiIntegration
