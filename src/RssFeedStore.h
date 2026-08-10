#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>
#include <vector>

#include "FreshRssCache.h"

struct RssFeed {
  std::string name;
  std::string url;
  bool isFreshRss = false;
  FreshRssFilterKind freshFilter = FreshRssFilterKind::All;
  RssLocalFilter freshLocalFilter = RssLocalFilter::None;
  std::string freshId;
  bool freshUnreadOnly = false;
  size_t articleCount = 0;
  size_t unreadCount = 0;
};

/**
 * Singleton, read-only loader for RSS feed configuration on the SD card.
 * Unlike OpdsServerStore, feeds are managed by dropping a JSON file onto the
 * SD card (via the file-transfer web UI or a PC) — there is no on-device
 * add/edit UI, so this store never writes /.crosspoint/rss/feeds.json.
 *
 * Expected format:
 *   { "feeds": [ { "name": "Example", "url": "https://example.com/feed.xml" } ] }
 */
class RssFeedStore : public PersistableStore<RssFeedStore> {
 private:
  std::vector<RssFeed> feeds;

  static constexpr size_t MAX_FEEDS = 20;

  RssFeedStore() = default;

  friend class PersistableStore<RssFeedStore>;

 public:
  static const char* getFilePath() { return "/.crosspoint/rss/feeds.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  const std::vector<RssFeed>& getFeeds() const { return feeds; }
  const RssFeed* getFeed(size_t index) const;
  // Builds the RSS home screen from the committed FreshRSS metadata snapshot.
  // Direct feeds remain readable only as deprecated migration input; they are
  // not exposed once FreshRSS mode is active.
  bool loadFreshRssNavigation();
  size_t getCount() const { return feeds.size(); }
  bool hasFeeds() const { return !feeds.empty(); }
};

#define RSS_FEED_STORE RssFeedStore::getInstance()
