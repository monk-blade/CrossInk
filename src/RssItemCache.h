#pragma once

#include <HalStorage.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "RichText.h"

// One feed's cached item metadata and, when loaded, one already
// HtmlRichText-converted body. Feed indexes normally keep bodies empty until
// the selected item is loaded from the SD card.
struct CachedRssItem {
  uint32_t key = 0;
  // FreshRSS v3 supplies this SD record offset so article opening can seek
  // directly. Legacy/direct RSS records leave it zero and use key lookup.
  uint32_t cacheOffset = 0;
  std::string title;
  std::string link;
  std::string date;
  // Optional FreshRSS list metadata. Legacy/direct RSS cache records leave it
  // empty, preserving their v2/v3/v4 read compatibility.
  std::string subtitle;
  RichText body;
  bool bodyTruncated = false;
  // Local-only placeholder used for queued FreshRSS IDs that disappeared from
  // the latest committed snapshot. It is never serialized as an article.
  bool unavailable = false;
};

namespace RssItemCache {

struct CacheStats {
  size_t itemCount = 0;
  size_t completeBodies = 0;
  size_t truncatedBodies = 0;
  uint64_t bytes = 0;
};

// Writes one complete feed snapshot without retaining the feed's article
// bodies in RAM. The previous items.bin remains readable until commit().
class FeedWriteSession final {
 public:
  explicit FeedWriteSession(std::string feedUrl) : feedUrl(std::move(feedUrl)) {}
  ~FeedWriteSession();

  FeedWriteSession(const FeedWriteSession&) = delete;
  FeedWriteSession& operator=(const FeedWriteSession&) = delete;

  bool begin();
  bool append(const CachedRssItem& item);
  bool commit();
  void abort();

  bool failed() const { return failedState; }
  size_t itemCount() const { return keys.size(); }

 private:
  std::string feedUrl;
  std::string tempPath;
  HalFile file;
  std::vector<uint32_t> keys;
  bool open = false;
  bool failedState = false;
};

std::string cacheDirFor(const std::string& feedUrl);
bool exists(const std::string& feedUrl);

bool loadIndex(const std::string& feedUrl, std::vector<CachedRssItem>& outItems);
bool load(const std::string& feedUrl, std::vector<CachedRssItem>& outItems);
bool loadItemBody(const std::string& feedUrl, uint32_t key, RichText& outBody, bool& complete);

CacheStats stats();
bool clearAll();
std::vector<uint32_t> peekKeys(const std::string& feedUrl);

// Convenience path for tests and any non-streaming callers. Production feed
// refresh uses FeedWriteSession directly.
bool save(const std::string& feedUrl, const std::vector<CachedRssItem>& items);

}  // namespace RssItemCache
