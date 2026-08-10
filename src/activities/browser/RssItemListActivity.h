#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <string>
#include <vector>

#include "RssFeedStore.h"
#include "RssItemCache.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * Fetches and lists one feed's items. WiFi is connected on demand (only when
 * this activity is active) and disconnected in onExit() — this activity
 * never leaves WiFi running in the background. If a fetch fails but a
 * previously-cached copy of the feed exists, the cached items are shown
 * instead of an error (offline reading is a first-class case, not just a
 * failure fallback). Selecting an item pushes RssArticleActivity.
 */
class RssItemListActivity final : public Activity {
 public:
  enum class ListState { CHECK_WIFI, WIFI_SELECTION, LOADING, BROWSING, ERROR };

  explicit RssItemListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, RssFeed feed)
      : Activity("RssItemList", renderer, mappedInput), feed(std::move(feed)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  ListState state = ListState::LOADING;
  RssFeed feed;
  // Direct RSS keeps its small legacy metadata vector. FreshRSS retains only
  // filtered keys and the current display page; article bodies stay on SD.
  std::vector<CachedRssItem> items;
  std::vector<uint32_t> freshVisibleKeys;
  std::vector<uint32_t> freshUnavailableKeys;
  size_t freshPageStart = 0;
  // Indices into items; filtering never copies titles or bodies.
  std::vector<uint16_t> visibleItems;
  int selectorIndex = 0;
  bool consumeConfirm = false;
  bool consumeBack = false;
  bool longQueueHandled = false;
  std::string queueMessage;
  unsigned long queueMessageUntil = 0;
  std::string errorMessage;
  std::string statusMessage;
  // Kept short because it shares the header subtitle with the local count.
  // This makes a stale/offline-preserved snapshot visible without adding a
  // second full-screen pass or delaying the cache-first first paint.
  std::string cacheState;
  bool backgroundRefresh = false;
  bool listFontSessionActive = false;
  uint8_t savedFontFamily = 0;
  uint8_t savedFontPointSize = 0;
  char savedSdFontFamilyName[32] = "";

  void checkAndConnectWifi();
  void launchWifiSelection();
  void onWifiSelectionComplete(bool connected);
  void fetchFeed();
  bool loadFromCache();
  void openSelectedItem();
  void triggerRefresh();
  bool rebuildVisibleItems();
  void toggleSelectedStar();
  void toggleSelectedQueue();
  int selectedItemIndex();
  void openNextUnread();
  void activateListFont();
  void restoreReaderFont();
  size_t visibleCount() const;
  bool ensureFreshPageForIndex(size_t index);
  const CachedRssItem* itemAtVisibleIndex(size_t index) const;
  bool listHasSubtitle() const;
  bool preventAutoSleep() override { return true; }

  std::atomic<size_t> syncReceived{0};
  std::atomic<size_t> syncLimit{0};
  std::atomic<bool> syncProcessingArticle{false};
  unsigned long lastSyncPaintMs = 0;

  // Refresh runs on the activity task so cache writes remain atomic and
  // bounded. Throttled waits let the render task repaint the progress screen
  // while a large article is being converted and written.
  void paintSyncProgress(bool force = false);
};
