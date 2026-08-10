#include "RssParser.h"

#include <Logging.h>
#include <XmlParserUtils.h>

#include <cstring>
#include <functional>
#include <utility>

namespace {
constexpr size_t MAX_FEED_TITLE_CHARS = 160;
constexpr size_t MAX_TITLE_CHARS = 200;
constexpr size_t MAX_LINK_CHARS = 768;
constexpr size_t MAX_ID_CHARS = 256;
constexpr size_t MAX_DATE_CHARS = 64;
}  // namespace

RssParser::RssParser(const size_t maxBodyChars) : maxBodyChars(maxBodyChars) {
  parser = XML_ParserCreate(nullptr);
  if (!parser) {
    errorOccured = true;
    LOG_DBG("RSS", "Couldn't allocate memory for parser");
    return;
  }
  XML_SetUserData(parser, this);
  XML_SetElementHandler(parser, startElement, endElement);
  XML_SetCharacterDataHandler(parser, characterData);
}

RssParser::~RssParser() { destroyXmlParser(parser); }

size_t RssParser::write(uint8_t c) { return write(&c, 1); }

size_t RssParser::write(const uint8_t* xmlData, const size_t length) {
  if (itemSinkFailed) return 0;
  if (errorOccured) return length;

  const char* currentPos = reinterpret_cast<const char*>(xmlData);
  size_t remaining = length;
  constexpr size_t chunkSize = 1024;

  while (remaining > 0) {
    const size_t toRead = remaining < chunkSize ? remaining : chunkSize;
    void* const buf = XML_GetBuffer(parser, toRead);
    if (!buf) {
      errorOccured = true;
      LOG_DBG("RSS", "Couldn't allocate memory for buffer");
      destroyXmlParser(parser);
      return length;
    }

    memcpy(buf, currentPos, toRead);

    if (XML_ParseBuffer(parser, static_cast<int>(toRead), 0) == XML_STATUS_ERROR) {
      errorOccured = true;
      LOG_DBG("RSS", "Parse error at line %lu: %s", XML_GetCurrentLineNumber(parser),
              XML_ErrorString(XML_GetErrorCode(parser)));
      destroyXmlParser(parser);
      return length;
    }
    if (itemSinkFailed) return 0;
    currentPos += toRead;
    remaining -= toRead;
  }
  return length;
}

void RssParser::flush() {
  if (errorOccured || !parser) return;
  if (XML_Parse(parser, nullptr, 0, XML_TRUE) != XML_STATUS_OK) {
    errorOccured = true;
    destroyXmlParser(parser);
  }
}

bool RssParser::error() const { return errorOccured; }

void RssParser::setTargetKey(const uint32_t key) {
  targetEnabled = true;
  targetFound = false;
  targetKey = key;
  items.clear();
}

void RssParser::setItemSink(ItemSink sink) {
  itemSink = std::move(sink);
  items.clear();
  std::vector<RssItem>().swap(items);
  acceptedItemCount = 0;
  itemSinkFailed = false;
}

void RssParser::clear() {
  feedTitle.clear();
  items.clear();
  currentItem = RssItem{};
  pendingDescription.clear();
  pendingContent.clear();
  currentText.clear();
  inItem = collectCurrentItem = feedTitleCaptured = false;
  inTitle = inLink = inId = inDate = inDescription = inContent = false;
  feedTruncated = false;
  targetFound = false;
  acceptedItemCount = 0;
  currentFieldTruncated = false;
  descriptionTruncated = false;
  contentTruncated = false;
  itemSinkFailed = false;
}

const char* RssParser::findAttribute(const XML_Char** atts, const char* name) {
  for (int i = 0; atts[i]; i += 2) {
    if (strcmp(atts[i], name) == 0) return atts[i + 1];
  }
  return nullptr;
}

void RssParser::assignBounded(std::string& target, const char* value, const size_t maxLen) {
  if (!value) {
    target.clear();
    return;
  }
  target.assign(value, strnlen(value, maxLen));
}

void RssParser::appendBounded(std::string& target, const char* value, const size_t len, const size_t maxLen) {
  if (target.size() >= maxLen) return;
  const size_t remaining = maxLen - target.size();
  target.append(value, len < remaining ? len : remaining);
}

void XMLCALL RssParser::startElement(void* userData, const XML_Char* name, const XML_Char** atts) {
  auto* self = static_cast<RssParser*>(userData);

  // RSS 2.0 <item> and Atom <entry> are treated identically — both formats
  // show up under the umbrella of "RSS feed" URLs in practice.
  const bool isItem = strcmp(name, "item") == 0 || strstr(name, ":item") != nullptr || strcmp(name, "entry") == 0 ||
                      strstr(name, ":entry") != nullptr;
  if (isItem) {
    self->inItem = true;
    self->collectCurrentItem = self->targetEnabled ? !self->targetFound :
                                                               self->acceptedItemCount < RssParser::MAX_ITEMS;
    self->feedTruncated = self->feedTruncated || !self->collectCurrentItem;
    self->currentItem = RssItem{};
    self->pendingDescription.clear();
    self->pendingContent.clear();
    self->currentText.clear();
    self->currentFieldTruncated = false;
    self->descriptionTruncated = false;
    self->contentTruncated = false;
    self->inTitle = self->inLink = self->inId = self->inDate = self->inDescription = self->inContent = false;
    return;
  }

  if (strcmp(name, "title") == 0 || strstr(name, ":title") != nullptr) {
    if (self->inItem) {
      if (self->collectCurrentItem) {
        self->inTitle = true;
        self->currentText.clear();
      }
    } else if (!self->feedTitleCaptured) {
      // Channel/feed title, always precedes items in practice.
      self->inTitle = true;
      self->currentText.clear();
    }
    return;
  }

  if (!self->inItem || !self->collectCurrentItem) return;

  if (strcmp(name, "link") == 0 || strstr(name, ":link") != nullptr) {
    // Atom: <link href="..." rel="alternate"/> (self-closing, no text body).
    // RSS: <link>https://...</link> (text body, no attributes).
    const char* href = findAttribute(atts, "href");
    if (href) {
      const char* rel = findAttribute(atts, "rel");
      const bool acceptable = !rel || strcmp(rel, "alternate") == 0;
      if (acceptable && self->currentItem.link.empty()) {
        assignBounded(self->currentItem.link, href, MAX_LINK_CHARS);
      }
    } else if (self->currentItem.link.empty()) {
      self->inLink = true;
      self->currentText.clear();
    }
  } else if (strcmp(name, "guid") == 0 || strcmp(name, "id") == 0 || strstr(name, ":id") != nullptr) {
    self->inId = true;
    self->currentText.clear();
  } else if (strcmp(name, "pubDate") == 0 || strcmp(name, "updated") == 0 || strstr(name, ":updated") != nullptr ||
             strcmp(name, "published") == 0 || strstr(name, ":published") != nullptr) {
    self->inDate = true;
    self->currentText.clear();
  } else if (strcmp(name, "description") == 0 || strcmp(name, "summary") == 0 || strstr(name, ":summary") != nullptr) {
    self->inDescription = true;
    self->currentFieldTruncated = false;
    self->currentText.clear();
  } else if (strcmp(name, "content:encoded") == 0 || strcmp(name, "content") == 0) {
    // Exact match only (no ":content" substring fallback) — the Media RSS
    // extension's <media:content> would otherwise false-positive here.
    self->inContent = true;
    self->currentFieldTruncated = false;
    self->currentText.clear();
  }
}

void XMLCALL RssParser::endElement(void* userData, const XML_Char* name) {
  auto* self = static_cast<RssParser*>(userData);

  const bool isItem = strcmp(name, "item") == 0 || strstr(name, ":item") != nullptr || strcmp(name, "entry") == 0 ||
                      strstr(name, ":entry") != nullptr;
  if (isItem) {
    if (self->collectCurrentItem && !self->currentItem.title.empty()) {
      // Prefer the fuller <content:encoded>/<content> body over the
      // <description>/<summary> teaser when both are present.
      if (!self->pendingContent.empty()) {
        self->currentItem.body = std::move(self->pendingContent);
      } else {
        self->currentItem.body = std::move(self->pendingDescription);
      }
      self->currentItem.bodyTruncated = !self->pendingContent.empty() ? self->contentTruncated : self->descriptionTruncated;

      const std::string& identity = !self->currentItem.id.empty()
                                        ? self->currentItem.id
                                        : (!self->currentItem.link.empty() ? self->currentItem.link
                                                                            : self->currentItem.title);
      const uint32_t itemKey = static_cast<uint32_t>(std::hash<std::string>{}(identity));
      if (!self->targetEnabled || itemKey == self->targetKey) {
        ++self->acceptedItemCount;
        if (self->itemSink) {
          if (!self->itemSink(std::move(self->currentItem))) {
            self->itemSinkFailed = true;
            self->errorOccured = true;
          }
        } else {
          self->items.push_back(std::move(self->currentItem));
        }
        self->targetFound = self->targetEnabled;
      }
    }
    self->inItem = false;
    self->collectCurrentItem = false;
    self->pendingDescription.clear();
    self->pendingContent.clear();
    return;
  }

  if (strcmp(name, "title") == 0 || strstr(name, ":title") != nullptr) {
    if (self->inItem) {
      if (self->inTitle) self->currentItem.title = self->currentText;
    } else if (self->inTitle && !self->feedTitleCaptured) {
      self->feedTitle = self->currentText;
      self->feedTitleCaptured = true;
    }
    self->inTitle = false;
    return;
  }

  if (!self->inItem) return;

  if (strcmp(name, "link") == 0 || strstr(name, ":link") != nullptr) {
    if (self->inLink) self->currentItem.link = self->currentText;
    self->inLink = false;
  } else if (strcmp(name, "guid") == 0 || strcmp(name, "id") == 0 || strstr(name, ":id") != nullptr) {
    if (self->inId) self->currentItem.id = self->currentText;
    self->inId = false;
  } else if (strcmp(name, "pubDate") == 0 || strcmp(name, "updated") == 0 || strstr(name, ":updated") != nullptr ||
             strcmp(name, "published") == 0 || strstr(name, ":published") != nullptr) {
    if (self->inDate) self->currentItem.date = self->currentText;
    self->inDate = false;
  } else if (strcmp(name, "description") == 0 || strcmp(name, "summary") == 0 || strstr(name, ":summary") != nullptr) {
    if (self->inDescription) {
      self->pendingDescription = std::move(self->currentText);
      self->descriptionTruncated = self->currentFieldTruncated;
    }
    self->inDescription = false;
  } else if (strcmp(name, "content:encoded") == 0 || strcmp(name, "content") == 0) {
    if (self->inContent) {
      self->pendingContent = std::move(self->currentText);
      self->contentTruncated = self->currentFieldTruncated;
    }
    self->inContent = false;
  }
}

void XMLCALL RssParser::characterData(void* userData, const XML_Char* s, const int len) {
  auto* self = static_cast<RssParser*>(userData);
  if (self->inItem) {
    if (!self->collectCurrentItem) return;
    if (self->inTitle) {
      appendBounded(self->currentText, s, len, MAX_TITLE_CHARS);
    } else if (self->inLink) {
      appendBounded(self->currentText, s, len, MAX_LINK_CHARS);
    } else if (self->inId) {
      appendBounded(self->currentText, s, len, MAX_ID_CHARS);
    } else if (self->inDate) {
      appendBounded(self->currentText, s, len, MAX_DATE_CHARS);
    } else if (self->inDescription || self->inContent) {
      // A description is only a fallback. If content arrives later, release
      // the teaser before retaining the content so one item never keeps two
      // 32 KB body buffers at once.
      if (self->inContent && !self->pendingDescription.empty()) self->pendingDescription.clear();
      if (self->inDescription && !self->pendingContent.empty()) return;
      if (self->currentText.size() >= self->maxBodyChars ||
          static_cast<size_t>(len) > self->maxBodyChars - self->currentText.size()) {
        self->currentFieldTruncated = true;
      }
      appendBounded(self->currentText, s, len, self->maxBodyChars);
    }
  } else if (self->inTitle && !self->feedTitleCaptured) {
    appendBounded(self->currentText, s, len, MAX_FEED_TITLE_CHARS);
  }
}
