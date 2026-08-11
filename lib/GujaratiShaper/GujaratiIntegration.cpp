#include "GujaratiIntegration.h"

#include "GujaratiShaper.h"

#include <Utf8.h>

namespace GujaratiIntegration {

namespace {

// GujaratiShaper::shape() requires an output buffer sized for the worst case
// (roughly 3 bytes per input codepoint after conjunct/reph expansion). This
// covers any realistic single word — every caller here splits on whitespace
// before shaping a "word" (see shapeWordsInSanitizedText() below and
// ParsedText's own word tokenization) — while staying a fixed stack buffer,
// not a per-call heap allocation, to keep this hot path (called per word
// during EPUB/RSS ingest) allocation-free.
constexpr size_t kShapedWordBufferBytes = 512;

void shapeSanitized(std::string& word) {
  if (!GujaratiShaper::containsGujarati(word.c_str(), word.size())) {
    return;
  }
  if (word.size() > kShapedWordBufferBytes / 3) {
    // Pathologically long "word" (e.g. an unspaced URL/hash run with no
    // whitespace for the caller to split on). Leave it unshaped rather than
    // truncate the output mid-conjunct — ordinary text never hits this.
    return;
  }
  char shaped[kShapedWordBufferBytes];
  const size_t shapedLen = GujaratiShaper::shape(word.c_str(), word.size(), shaped, sizeof(shaped));
  if (shapedLen > 0) {
    word.assign(shaped, shapedLen);
  }
}

// Splits already-sanitized text on whitespace and shapes each run
// individually via shapeSanitized(), so a long string is never handed to
// GujaratiShaper::shape() as a single multi-hundred-codepoint call. Shared by
// shapeUiString() and shapeLongUiString(): both need this to stay correct for
// long input (see shape()'s chunk-boundary caveat), the "long" one just also
// promises to handle unbounded length.
void shapeWordsInSanitizedText(std::string& text) {
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
      shapeSanitized(word);  // no-op if this particular word has no Gujarati in it
      result += word;
    }
  }
  text.swap(result);
}

}  // namespace

void shapeWord(std::string& word) {
  utf8SanitizeInPlace(word);
  shapeSanitized(word);
}

void shapeSanitizedWord(std::string& word) { shapeSanitized(word); }

void shapeUiString(std::string& text) {
  utf8SanitizeInPlace(text);
  shapeWordsInSanitizedText(text);
}

void shapeLongUiString(std::string& text) {
  utf8SanitizeInPlace(text);
  shapeWordsInSanitizedText(text);
}

bool containsGujarati(const std::string& text) { return GujaratiShaper::containsGujarati(text.c_str(), text.size()); }

}  // namespace GujaratiIntegration
