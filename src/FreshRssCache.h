#pragma once

#include <cstdint>

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "FreshRssTypes.h"
#include "RssItemCache.h"

enum class FreshRssFilterKind : uint8_t { All, Category, Subscription, Uncategorized };

// Device-local filters are deliberately kept out of the FreshRSS snapshot:
// FreshRSS remains read-only and read/star/queue state never gets uploaded.
enum class RssLocalFilter : uint8_t { None, Unread, Starred, Queued };

struct RssListQuery {
  FreshRssFilterKind source = FreshRssFilterKind::All;
  std::string id;
  RssLocalFilter local = RssLocalFilter::None;
};

struct FreshRssIndexEntry {
  uint32_t key = 0;
  uint64_t modifiedMsec = 0;
  uint32_t recordOffset = 0;
  uint32_t recordSize = 0;
  uint16_t subscriptionIndex = 0xffff;
  uint8_t flags = 0;  // bit 0: body truncated
  uint32_t categoryMask[4] = {};
};

struct FreshRssNavigationEntry {
  FreshRssFilterKind kind = FreshRssFilterKind::All;
  std::string id;
  std::string label;
  bool unreadOnly = false;
  size_t articleCount = 0;
  size_t unreadCount = 0;
};

struct FreshRssCachedArticle {
  uint32_t key = 0;
  // Google Reader crawl timestamp used by the v4 index for delta-sync
  // comparisons. It remains index metadata and is not duplicated in the
  // variable-sized article record.
  uint64_t modifiedMsec = 0;
  std::string id;
  std::string title;
  std::string author;
  std::string origin;
  std::string originUrl;
  std::string streamId;
  std::string subscriptionId;
  std::string link;
  std::string date;
  std::vector<std::string> categoryIds;
  RichText body;
  bool bodyTruncated = false;
};

namespace FreshRssCache {

struct CacheStats {
  size_t itemCount = 0;
  size_t completeBodies = 0;
  size_t truncatedBodies = 0;
  uint64_t bytes = 0;
  uint64_t indexBytes = 0;
  uint32_t lastRefreshUnix = 0;
  // Supplied by RssItemStateStore at the UI boundary. The cache module stays
  // independent so host cache tests do not need to link the state store.
  size_t queuedCount = 0;
};

struct SnapshotInfo {
  FreshRssSyncCursor cursor;
  uint8_t version = 0;
  uint32_t generation = 0;
  uint32_t lastRefreshUnix = 0;
  size_t itemCount = 0;
  uint64_t bytes = 0;
  uint64_t indexBytes = 0;
};

class WriteSession final {
 public:
  WriteSession() = default;
  ~WriteSession();
  WriteSession(const WriteSession&) = delete;
  WriteSession& operator=(const WriteSession&) = delete;

  bool begin(const FreshRssMetadata& metadata, size_t articleLimit);
  bool begin(const FreshRssMetadata& metadata, size_t articleLimit, const FreshRssSyncCursor& cursor);
  bool append(const FreshRssCachedArticle& article);
  // Append records from the currently committed snapshot without loading
  // their RichText bodies. The old snapshot remains open/read-only until the
  // new temporary file commits.
  bool copyUnchangedFromCurrent(const std::vector<uint32_t>& changedKeys);
  void setSyncCursor(const FreshRssSyncCursor& cursor) { syncCursor = cursor; }
  bool commit();
  void abort();
  bool failed() const { return failedState; }
  size_t itemCount() const { return articleCount; }

 private:
  std::string tempPath;
  class HalFileHolder;
  HalFileHolder* holder = nullptr;
  FreshRssMetadata metadata;
  uint32_t articleCount = 0;
  uint32_t articleLimit = 0;
  uint32_t lastRefreshUnix = 0;
  FreshRssSyncCursor syncCursor;
  std::vector<FreshRssIndexEntry> indexEntries;
  std::vector<std::string> indexCategories;
  bool open = false;
  bool failedState = false;
};

constexpr size_t MAX_ARTICLES = 1000;
constexpr size_t MAX_BODY_BYTES = 32 * 1024;

bool exists();
bool loadSnapshotInfo(SnapshotInfo& outInfo);
bool loadSyncCursor(FreshRssSyncCursor& outCursor);
bool loadNavigationIndex(std::vector<FreshRssIndexEntry>& outEntries);
// The source portion is resolved by the SD index. Device-local state filters
// remain in the activity layer so the cache module does not retain or link
// against the read/star/queue store.
bool loadKeys(const RssListQuery& query, std::vector<uint32_t>& outKeys);
bool loadKeys(FreshRssFilterKind kind, const std::string& id, std::vector<uint32_t>& outKeys);
bool loadIndex(FreshRssFilterKind kind, const std::string& id, std::vector<CachedRssItem>& outItems);
bool loadItemsByKeys(const std::vector<uint32_t>& keys, size_t offset, size_t count,
                     std::vector<CachedRssItem>& outItems);
// `key` must match the record found at `recordOffset` — the offset alone is
// not a safe cache key: a re-commit (delta sync, eviction) can reuse the same
// byte offset for a different article, and without this check a stale
// `cacheOffset` held by the UI would silently open the wrong article.
bool loadItemBodyByOffset(uint32_t recordOffset, uint32_t key, RichText& outBody, bool& complete);
bool loadItemBody(uint32_t key, RichText& outBody, bool& complete);
// Builds the home navigation and counts every matching entry in one snapshot
// scan. The optional read predicate lets the caller populate local unread
// badges without rescanning the snapshot once per category/subscription.
bool loadNavigation(std::vector<FreshRssNavigationEntry>& outEntries,
                    const std::function<bool(uint32_t)>& isRead = {});
// Returns only categories that contain at least one cached article. This is
// intentionally separate from the legacy flat navigation so the RSS home can
// offer category-first browsing without exposing empty subscription rows.
bool loadCategoryNavigation(std::vector<FreshRssNavigationEntry>& outEntries,
                            const std::function<bool(uint32_t)>& isRead = {});
// Returns subscriptions assigned to one category, with counts for all cached
// articles in each matching subscription. Selecting one of these entries then
// opens the subscription-wide article list.
bool loadSubscriptionNavigationForCategory(const std::string& categoryId,
                                           std::vector<FreshRssNavigationEntry>& outEntries,
                                           const std::function<bool(uint32_t)>& isRead = {});
CacheStats stats();
bool clear();

}  // namespace FreshRssCache
