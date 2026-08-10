#include "RssFeedStore.h"

#include <GujaratiIntegration.h>
#include <Logging.h>

#include <algorithm>

#include "RssItemStateStore.h"

// toJson exists only to satisfy the PersistableStore<T> interface — this
// store never calls saveToFile(), since feeds.json is user-managed on the SD
// card, not device-managed.
void RssFeedStore::toJson(JsonDocument& doc) const {
  JsonArray arr = doc["feeds"].to<JsonArray>();
  for (const auto& feed : feeds) {
    JsonObject obj = arr.add<JsonObject>();
    obj["name"] = feed.name;
    obj["url"] = feed.url;
  }
}

bool RssFeedStore::fromJson(JsonVariantConst doc) {
  // Tolerate a missing/invalid 'feeds' key (treat as empty list); only a
  // JSON parse error (caught upstream in readDocFromFile) is fatal.
  feeds.clear();
  JsonArrayConst arr = doc["feeds"].as<JsonArrayConst>();
  feeds.reserve(std::min(arr.size(), MAX_FEEDS));

  for (JsonObjectConst obj : arr) {
    if (feeds.size() >= MAX_FEEDS) break;
    const char* url = obj["url"] | "";
    if (url[0] == '\0') continue;  // a feed without a URL is not usable
    RssFeed feed;
    feed.url = url;
    feed.name = obj["name"] | "";
    if (feed.name.empty()) feed.name = feed.url;
    // feeds.json is free-text from the user, so it bypasses ParsedText's
    // parse-time shaping entirely — shape it here, once, like any other UI
    // string (see GujaratiIntegration.h and AGENTS.md's modularity rules).
    // shapeLongUiString() rather than shapeUiString(): a feed name isn't
    // guaranteed to stay under the latter's single-line length assumption.
    GujaratiIntegration::shapeLongUiString(feed.name);
    feeds.push_back(std::move(feed));
  }

  LOG_DBG("RSS", "Loaded %zu RSS feeds from file", feeds.size());
  return true;
}

const RssFeed* RssFeedStore::getFeed(size_t index) const {
  if (index >= feeds.size()) {
    return nullptr;
  }
  return &feeds[index];
}

bool RssFeedStore::loadFreshRssNavigation() {
  std::vector<FreshRssNavigationEntry> entries;
  feeds.clear();
  if (!FreshRssCache::loadNavigation(entries, [](const uint32_t key) { return RSS_ITEM_STATE.isRead("freshrss", key); })) {
    // Keep a single virtual entry when the account is configured but has not
    // completed its first refresh yet. Opening it gives the user the normal
    // setup/authentication error and retry path.
    feeds.push_back(RssFeed{"All Articles", "freshrss", true, FreshRssFilterKind::All, RssLocalFilter::None, {}, false});
    return false;
  }
  feeds.reserve(entries.size());
  for (auto& entry : entries) {
    RssFeed feed;
    feed.name = std::move(entry.label);
    feed.url = "freshrss";
    feed.isFreshRss = true;
    feed.freshFilter = entry.kind;
    feed.freshId = std::move(entry.id);
    feed.freshUnreadOnly = entry.unreadOnly;
    feed.articleCount = entry.articleCount;
    feed.unreadCount = entry.unreadCount;
    GujaratiIntegration::shapeLongUiString(feed.name);
    feeds.push_back(std::move(feed));
  }
  return true;
}
