#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * Tracks per-item read/unread state and per-feed last-fetch time across RSS
 * feeds, persisted as a small binary cache at /.crosspoint/rss/state.bin.
 *
 * Bounded by design (380KB RAM, no PSRAM): at most MAX_FEEDS_TRACKED feeds,
 * each keeping at most MAX_READ_KEYS_PER_FEED read markers (oldest evicted
 * first) — worst case ~20 * 200 * 4 bytes = 16KB while a feed/item list
 * activity is open. Not loaded unless RSS Feeds is actually in use.
 */
class RssItemStateStore {
 public:
  static RssItemStateStore& getInstance();

  void load();
  // Only touches SD if markRead()/setLastFetchedUnix() changed something
  // since the last save — avoid unconditional writes (SD wear).
  bool saveIfDirty();

  // Stable per-item identifier for this run of the firmware: hash of
  // item.id, falling back to item.link, then item.title, whichever is the
  // first non-empty one. Not guaranteed stable across a firmware rebuild
  // (std::hash is implementation-defined) — the same tradeoff the EPUB cache
  // already makes for its `epub_<hash>` cache directories.
  static uint32_t itemKey(const std::string& id, const std::string& link, const std::string& title);

  bool isRead(const std::string& feedUrl, uint32_t key) const;
  void markRead(const std::string& feedUrl, uint32_t key);

  bool isStarred(const std::string& feedUrl, uint32_t key) const;
  void toggleStar(const std::string& feedUrl, uint32_t key);
  bool isQueued(const std::string& feedUrl, uint32_t key) const;
  // Returns false when the bounded queue is full and the item is not already
  // queued. The caller can surface that condition without growing RAM.
  bool toggleQueued(const std::string& feedUrl, uint32_t key);
  std::vector<uint32_t> loadQueuedIds(const std::string& feedUrl) const;
  size_t countQueued(const std::string& feedUrl) const;
  void clearQueue();
  void clearStars();
  void clearAll();

  // Unread items among the given keys still present in the feed's read set.
  size_t countUnread(const std::string& feedUrl, const std::vector<uint32_t>& keys) const;

 private:
  RssItemStateStore() = default;

  struct FeedState {
    std::vector<uint32_t> readKeys;  // insertion order; front = oldest, for FIFO eviction
    std::vector<uint32_t> starredKeys;  // insertion order; star protection markers
    std::vector<uint32_t> queuedKeys;  // local read-later queue; never synced
  };

  static constexpr size_t MAX_READ_KEYS_PER_FEED = 200;
  static constexpr size_t MAX_QUEUE_KEYS_PER_FEED = 256;
  // Matches RssFeedStore::MAX_FEEDS; a small excess is tolerated so state
  // isn't lost immediately after trimming feeds.json down.
  static constexpr size_t MAX_FEEDS_TRACKED = 24;

  std::unordered_map<std::string, FeedState> feedStates;
  bool loaded = false;
  bool dirty = false;

  // Returns nullptr if feedUrl isn't tracked and the tracked-feed cap has
  // already been reached — callers degrade gracefully (no persisted
  // read-state for that feed) rather than growing unbounded.
  FeedState* getOrCreateFeedState(const std::string& feedUrl);
};

#define RSS_ITEM_STATE RssItemStateStore::getInstance()
