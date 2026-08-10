#include "HtmlRichText.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <utility>

#include <Utf8.h>

namespace {

constexpr size_t MAX_TAG_NAME = 16;
constexpr size_t MAX_ATTR_CAPTURE = 80;
constexpr size_t MAX_ENTITY_LEN = 12;  // longest handled entity, incl. digits

bool isBlockTag(const char* tag) {
  static const char* const kBlockTags[] = {
      "p", "div", "br", "li", "ul", "ol", "blockquote", "pre", "h1", "h2", "h3", "h4", "h5", "h6", "tr", "table",
  };
  for (const char* block : kBlockTags) {
    if (strcmp(tag, block) == 0) return true;
  }
  return false;
}

void appendUtf8(std::string& out, const long codepoint) {
  if (codepoint <= 0 || codepoint >= 0x110000) return;
  if (codepoint < 0x80) {
    out += static_cast<char>(codepoint);
  } else if (codepoint < 0x800) {
    out += static_cast<char>(0xC0 | (codepoint >> 6));
    out += static_cast<char>(0x80 | (codepoint & 0x3F));
  } else if (codepoint < 0x10000) {
    out += static_cast<char>(0xE0 | (codepoint >> 12));
    out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (codepoint & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (codepoint >> 18));
    out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (codepoint & 0x3F));
  }
}

// Decodes one entity starting at html[pos] (which must be '&'). Appends the
// decoded character(s) to out and returns the index just past the '&' (on
// failure) or the terminating ';' (on success) — i.e. where scanning should
// resume from.
size_t decodeEntity(const std::string& html, const size_t pos, std::string& out) {
  const size_t semi = html.find(';', pos + 1);
  if (semi == std::string::npos || semi - pos > MAX_ENTITY_LEN) {
    out += '&';
    return pos + 1;
  }
  const std::string name = html.substr(pos + 1, semi - pos - 1);
  if (name == "amp") {
    out += '&';
  } else if (name == "lt") {
    out += '<';
  } else if (name == "gt") {
    out += '>';
  } else if (name == "quot") {
    out += '"';
  } else if (name == "apos") {
    out += '\'';
  } else if (name == "nbsp") {
    out += ' ';
  } else if (!name.empty() && name[0] == '#') {
    const bool isHex = name.size() > 1 && (name[1] == 'x' || name[1] == 'X');
    const long codepoint = strtol(name.c_str() + (isHex ? 2 : 1), nullptr, isHex ? 16 : 10);
    appendUtf8(out, codepoint);
  }
  // Unknown named entities are silently dropped rather than guessed at.
  return semi + 1;
}

// Scans HTML text into a RichText, tracking bold/italic nesting depth and
// paragraph-level centering as it goes. One-shot, non-reentrant: construct,
// call build() once.
class Builder {
 public:
  explicit Builder(const size_t maxChars, const HtmlRichText::ProgressCallback& progress)
      : maxChars_(maxChars), progress_(progress) {
    // RSS bodies are often a sequence of short words. Avoid repeatedly
    // growing the temporary word buffer while keeping the working set small.
    currentWord_.reserve(64);
    result_.reserve(16);
  }

  RichText build(const std::string& html) {
    bool inTag = false;
    bool tagNameComplete = false;
    bool closingTag = false;
    char tagNameBuf[MAX_TAG_NAME] = {0};
    size_t tagNameLen = 0;
    char attrBuf[MAX_ATTR_CAPTURE] = {0};
    size_t attrLen = 0;

    bool inSkippedElement = false;  // inside <script> or <style>
    char skippedTagName[8] = {0};

    for (size_t i = 0; i < html.size() && totalChars_ < maxChars_; ++i) {
      if (progress_ && i - lastProgress_ >= 1024) {
        lastProgress_ = i;
        progress_(i);
      }
      const char c = html[i];

      if (inSkippedElement) {
        if (c == '<' && i + 1 < html.size() && html[i + 1] == '/') {
          size_t j = i + 2;
          size_t k = 0;
          char buf[8] = {0};
          while (j < html.size() && html[j] != '>' && k < sizeof(buf) - 1) {
            buf[k++] = static_cast<char>(tolower(static_cast<unsigned char>(html[j])));
            ++j;
          }
          if (strcmp(buf, skippedTagName) == 0) {
            inSkippedElement = false;
            i = j;  // land on '>'; loop's ++i skips past it
          }
        }
        continue;
      }

      if (c == '<') {
        // Any tag boundary — inline (<a>, <span>) or block — introduces no
        // whitespace of its own, so a word interrupted here glues to
        // whatever follows once we resume (see flushForTagBoundary).
        flushForTagBoundary();
        inTag = true;
        tagNameComplete = false;
        closingTag = false;
        tagNameLen = 0;
        attrLen = 0;
        continue;
      }

      if (inTag) {
        if (c == '>') {
          inTag = false;
          tagNameBuf[tagNameLen] = '\0';
          attrBuf[attrLen] = '\0';
          if (!closingTag && (strcmp(tagNameBuf, "script") == 0 || strcmp(tagNameBuf, "style") == 0)) {
            inSkippedElement = true;
            strncpy(skippedTagName, tagNameBuf, sizeof(skippedTagName) - 1);
          } else {
            handleTag(tagNameBuf, attrBuf, closingTag);
          }
          continue;
        }
        if (!tagNameComplete) {
          if (c == '/' && tagNameLen == 0) {
            closingTag = true;
            continue;
          }
          if (isalnum(static_cast<unsigned char>(c)) || c == ':') {
            if (tagNameLen < MAX_TAG_NAME - 1) tagNameBuf[tagNameLen++] = static_cast<char>(tolower(c));
            continue;
          }
          tagNameComplete = true;  // whitespace (or anything else) ends the tag name
        }
        // Attributes: only ever consulted for a dumb "does this contain the
        // substring 'center'" check (catches align="center" and any
        // text-align:center style), not real parsing.
        if (attrLen < MAX_ATTR_CAPTURE - 1) attrBuf[attrLen++] = static_cast<char>(tolower(c));
        continue;
      }

      if (c == '&') {
        std::string decoded;
        const size_t next = decodeEntity(html, i, decoded);
        i = next - 1;  // loop's ++i lands exactly at `next`
        for (const char ch : decoded) appendChar(ch);
        continue;
      }

      appendChar(c);
    }

    flushWord();
    finalizeParagraph();
    if (progress_ && lastProgress_ != html.size()) progress_(html.size());
    return std::move(result_);
  }

 private:
  RichText result_;
  RichParagraph currentParagraph_;
  std::string currentWord_;
  bool currentWordContinuesPrevious_ = false;
  bool paragraphForceCenter_ = false;
  size_t totalChars_ = 0;
  size_t lastProgress_ = 0;
  int boldDepth_ = 0;
  int italicDepth_ = 0;
  int centerDepth_ = 0;
  const size_t maxChars_;
  const HtmlRichText::ProgressCallback& progress_;

  EpdFontFamily::Style currentStyle() const {
    uint8_t s = 0;
    if (boldDepth_ > 0) s |= EpdFontFamily::BOLD;
    if (italicDepth_ > 0) s |= EpdFontFamily::ITALIC;
    return static_cast<EpdFontFamily::Style>(s);
  }

  void appendChar(const char c) {
    const bool isSpace = c == ' ' || c == '\n' || c == '\t' || c == '\r';
    if (isSpace) {
      flushWord();
      currentWordContinuesPrevious_ = false;  // real whitespace always makes a normal boundary
      return;
    }
    currentWord_ += c;
  }

  // Called right as a tag opens. If a word was mid-accumulation, flush it
  // and mark whatever comes next as glued to it — HTML tags don't imply
  // whitespace, only the text (or entities) between/around them do.
  void flushForTagBoundary() {
    if (currentWord_.empty()) return;
    flushWord();
    currentWordContinuesPrevious_ = true;
  }

  void flushWord() {
    if (currentWord_.empty()) return;
    if (totalChars_ >= maxChars_) {
      currentWord_.clear();
      return;
    }
    utf8SanitizeInPlace(currentWord_);
    if (currentWord_.empty()) return;
    const size_t room = maxChars_ - totalChars_;
    if (currentWord_.size() > room) {
      currentWord_.resize(room);
      // Stay on a validated UTF-8 boundary after the hard cut.
      currentWord_.resize(static_cast<size_t>(utf8SafeTruncateBuffer(currentWord_.data(), static_cast<int>(room))));
    }
    totalChars_ += currentWord_.size();
    currentParagraph_.words.push_back({std::move(currentWord_), currentStyle(), currentWordContinuesPrevious_});
    currentWord_.clear();
    currentWordContinuesPrevious_ = false;
  }

  void finalizeParagraph() {
    flushWord();
    if (!currentParagraph_.words.empty()) {
      currentParagraph_.align = (centerDepth_ > 0 || paragraphForceCenter_) ? TextAlign::CENTER : TextAlign::INHERIT;
      result_.push_back(std::move(currentParagraph_));
    }
    currentParagraph_ = RichParagraph{};
    paragraphForceCenter_ = false;
    // A new paragraph never starts glued to the previous one's last word.
    currentWordContinuesPrevious_ = false;
  }

  void handleTag(const char* tagName, const char* attrs, const bool closing) {
    const bool isBold = strcmp(tagName, "b") == 0 || strcmp(tagName, "strong") == 0;
    const bool isItalic = strcmp(tagName, "i") == 0 || strcmp(tagName, "em") == 0;
    const bool isCenterTag = strcmp(tagName, "center") == 0;

    if (isBold) {
      if (closing) {
        if (boldDepth_ > 0) boldDepth_--;
      } else {
        boldDepth_++;
      }
      return;
    }
    if (isItalic) {
      if (closing) {
        if (italicDepth_ > 0) italicDepth_--;
      } else {
        italicDepth_++;
      }
      return;
    }
    if (isCenterTag) {
      if (closing) {
        if (centerDepth_ > 0) centerDepth_--;
      } else {
        centerDepth_++;
      }
      return;
    }
    if (isBlockTag(tagName)) {
      finalizeParagraph();
      if (!closing && strstr(attrs, "center") != nullptr) paragraphForceCenter_ = true;
    }
  }
};

}  // namespace

RichText HtmlRichText::convert(const std::string& html, const size_t maxChars,
                               const HtmlRichText::ProgressCallback& progress) {
  Builder builder(maxChars, progress);
  return builder.build(html);
}
