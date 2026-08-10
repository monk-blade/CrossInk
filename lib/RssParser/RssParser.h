#pragma once
#include <Print.h>
#include <expat.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/**
 * Represents a single entry from an RSS 2.0 <item> or Atom <entry>.
 * `body` is the raw (unstripped) HTML body — prefer <content:encoded>/
 * <content> over <description>/<summary> when both are present. The parser
 * marks a body as truncated when its configured bound was reached.
 */
struct RssItem {
  std::string title;
  std::string link;
  std::string id;    // <guid> (RSS) or <id> (Atom)
  std::string date;  // raw <pubDate>/<updated>/<published> string, undated
  std::string body;  // raw HTML
  bool bodyTruncated = false;
};

/**
 * Parser for RSS 2.0 and Atom feeds. Uses the Expat XML parser and accepts
 * either format transparently — both are encountered in practice under the
 * umbrella of "RSS feed" URLs.
 *
 * Usage:
 *   RssParser parser;
 *   { RssParserStream stream{parser}; HttpDownloader::fetchUrl(url, stream); }
 *   if (parser) {
 *     for (const auto& item : parser.getItems()) { ... }
 *   }
 */
class RssParser final : public Print {
 public:
  // Keep a small explicit feed bound for the ESP32 list view. The sink path
  // admits exactly this many items and reports overflow through truncated().
  static constexpr size_t MAX_ITEMS = 60;
  using ItemSink = std::function<bool(RssItem&&)>;

  explicit RssParser(size_t maxBodyChars = 4000);
  ~RssParser();

  RssParser(const RssParser&) = delete;
  RssParser& operator=(const RssParser&) = delete;

  size_t write(uint8_t) override;
  size_t write(const uint8_t*, size_t) override;

  void flush() override;

  bool error() const;
  bool truncated() const { return feedTruncated; }
  bool sinkFailed() const { return itemSinkFailed; }
  size_t itemCount() const { return acceptedItemCount; }

  // In sink mode completed items are delivered immediately and are not kept in
  // the parser's vector. This is the memory-bounded path used by RSS refresh.
  void setItemSink(ItemSink sink);

  // When set, parse only the item whose stable state key matches targetKey.
  // Non-target items are still streamed through Expat, but their body is
  // released at the end of each item instead of accumulating in the feed.
  void setTargetKey(uint32_t targetKey);

  operator bool() { return !error(); }

  const std::string& getFeedTitle() const { return feedTitle; }

  const std::vector<RssItem>& getItems() const& { return items; }
  std::vector<RssItem> getItems() && { return std::move(items); }

  void clear();

 private:
  // Expat callbacks
  static void XMLCALL startElement(void* userData, const XML_Char* name, const XML_Char** atts);
  static void XMLCALL endElement(void* userData, const XML_Char* name);
  static void XMLCALL characterData(void* userData, const XML_Char* s, int len);

  static const char* findAttribute(const XML_Char** atts, const char* name);
  static void assignBounded(std::string& target, const char* value, size_t maxLen);
  static void appendBounded(std::string& target, const char* value, size_t len, size_t maxLen);

  XML_Parser parser = nullptr;
  std::string feedTitle;
  std::vector<RssItem> items;

  RssItem currentItem;
  std::string pendingDescription;  // <description> / <summary>
  std::string pendingContent;      // <content:encoded> / <content>
  std::string currentText;

  // Parser state
  bool inItem = false;
  bool collectCurrentItem = false;
  bool feedTitleCaptured = false;
  bool inTitle = false;
  bool inLink = false;  // RSS-style <link>text</link> (no href attribute)
  bool inId = false;    // <guid> or <id>
  bool inDate = false;  // <pubDate>, <updated>, or <published>
  bool inDescription = false;
  bool inContent = false;

  bool errorOccured = false;
  bool feedTruncated = false;
  bool targetEnabled = false;
  bool targetFound = false;
  uint32_t targetKey = 0;
  size_t maxBodyChars = 4000;
  size_t acceptedItemCount = 0;
  bool currentFieldTruncated = false;
  bool descriptionTruncated = false;
  bool contentTruncated = false;
  bool itemSinkFailed = false;
  ItemSink itemSink;
};
