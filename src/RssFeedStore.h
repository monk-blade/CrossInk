#pragma once

#include <string>

#include "FreshRssCache.h"

// One FreshRSS navigation target (dashboard row, category, or subscription).
// Constructed ad hoc by the RSS activities from a FreshRssNavigationEntry —
// FreshRSS is the only RSS backend, so there is no persisted feed list to
// load this from.
struct RssFeed {
  std::string name;
  std::string url;
  FreshRssFilterKind freshFilter = FreshRssFilterKind::All;
  RssLocalFilter freshLocalFilter = RssLocalFilter::None;
  std::string freshId;
  bool freshUnreadOnly = false;
  size_t articleCount = 0;
  size_t unreadCount = 0;
};
