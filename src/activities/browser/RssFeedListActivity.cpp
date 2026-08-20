#include "RssFeedListActivity.h"

#include <Arduino.h>
#include <FontCacheManager.h>
#include <GujaratiIntegration.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>
#include <cstring>
#include <optional>

#include "CrossPointSettings.h"
#include "FreshRssCache.h"
#include "RssItemStateStore.h"
#include "SdCardFontSystem.h"
#include "activities/browser/RssItemListActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr unsigned long SYNC_PROGRESS_PAINT_MS = 250;

RssFeed makeFreshFeed(const FreshRssNavigationEntry& entry) {
  RssFeed feed;
  // FreshRssCache's synthetic Uncategorized entry carries an internal,
  // untranslated placeholder label — the UI is the layer that owns i18n, so
  // substitute the translated string here rather than baking English into
  // the cache module (see FreshRssCache::loadNavigation).
  feed.name = entry.kind == FreshRssFilterKind::Uncategorized ? tr(STR_FRESHRSS_UNCATEGORIZED) : entry.label;
  feed.url = "freshrss";
  feed.freshFilter = entry.kind;
  feed.freshId = entry.id;
  feed.freshUnreadOnly = entry.unreadOnly;
  feed.articleCount = entry.articleCount;
  feed.unreadCount = entry.unreadCount;
  GujaratiIntegration::shapeLongUiString(feed.name);
  return feed;
}

void disconnectWifi() {
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    WiFi.mode(WIFI_OFF);
  }
}
}  // namespace

void RssFeedListActivity::onEnter() {
  Activity::onEnter();
  activateListFont();
  view = View::DASHBOARD;
  dashboardAction = DashboardAction::ALL;
  selectorIndex = 0;
  selectedCategoryId.clear();
  selectedCategoryLabel.clear();
  syncState = SyncState::IDLE;
  statusMessage.clear();
  errorMessage.clear();
  refreshMenuLabel = tr(STR_REFRESH);
  syncProgress.received.store(0);
  syncProgress.limit.store(0);
  syncProgress.processingArticle.store(false);
  lastSyncPaintMs = 0;
  loadDashboard();
  requestUpdate();
}

void RssFeedListActivity::loadDashboard() {
  entries.clear();
  std::vector<FreshRssNavigationEntry> navigation;
  const auto isRead = [](const uint32_t key) { return RSS_ITEM_STATE.isRead("freshrss", key); };
  std::vector<uint32_t> keys;
  const bool hasSnapshot = FreshRssCache::loadKeys(FreshRssFilterKind::All, "", keys);
  if (!hasSnapshot) keys.clear();
  FreshRssNavigationEntry all{FreshRssFilterKind::All, "", tr(STR_FRESHRSS_ALL_ARTICLES)};
  all.articleCount = keys.size();
  all.unreadCount = RSS_ITEM_STATE.countUnread("freshrss", keys);
  FreshRssNavigationEntry unread{FreshRssFilterKind::All, "", tr(STR_FRESHRSS_UNREAD), true};
  unread.articleCount = all.unreadCount;
  unread.unreadCount = unread.articleCount;
  size_t starred = 0;
  size_t queued = 0;
  for (const uint32_t key : keys) {
    if (RSS_ITEM_STATE.isStarred("freshrss", key)) ++starred;
    if (RSS_ITEM_STATE.isQueued("freshrss", key)) ++queued;
  }
  RssFeed refreshEntry;
  refreshEntry.name = refreshMenuLabel.empty() ? tr(STR_REFRESH) : refreshMenuLabel;
  refreshEntry.url = "freshrss";
  entries.push_back(std::move(refreshEntry));

  entries.push_back(makeFreshFeed(all));
  RssFeed unreadFeed = makeFreshFeed(unread);
  unreadFeed.freshLocalFilter = RssLocalFilter::Unread;
  entries.push_back(std::move(unreadFeed));
  RssFeed starredFeed;
  starredFeed.name = tr(STR_FRESHRSS_STARRED_ARTICLES);
  starredFeed.url = "freshrss";
  starredFeed.freshLocalFilter = RssLocalFilter::Starred;
  starredFeed.articleCount = starred;
  entries.push_back(std::move(starredFeed));
  RssFeed queuedFeed;
  queuedFeed.name = tr(STR_FRESHRSS_READING_QUEUE);
  queuedFeed.url = "freshrss";
  queuedFeed.freshLocalFilter = RssLocalFilter::Queued;
  queuedFeed.articleCount = queued;
  entries.push_back(std::move(queuedFeed));

  if (hasSnapshot) FreshRssCache::loadNavigation(navigation, isRead);
  size_t categoryCount = 0;
  size_t subscriptionCount = 0;
  for (const auto& item : navigation) {
    if (item.kind == FreshRssFilterKind::Category || item.kind == FreshRssFilterKind::Uncategorized) ++categoryCount;
    if (item.kind == FreshRssFilterKind::Subscription) ++subscriptionCount;
  }
  RssFeed categories;
  categories.name = tr(STR_FRESHRSS_CATEGORIES);
  categories.url = "freshrss";
  categories.articleCount = categoryCount;
  entries.push_back(std::move(categories));
  RssFeed subscriptions;
  subscriptions.name = tr(STR_FRESHRSS_SUBSCRIPTIONS);
  subscriptions.url = "freshrss";
  subscriptions.articleCount = subscriptionCount;
  entries.push_back(std::move(subscriptions));

  for (auto& entry : entries) GujaratiIntegration::shapeLongUiString(entry.name);
  selectorIndex = std::clamp(selectorIndex, 0, std::max(0, static_cast<int>(entries.size()) - 1));
}

void RssFeedListActivity::onExit() {
  Activity::onExit();
  restoreReaderFont();
  if (syncState != SyncState::IDLE) disconnectWifi();
}

void RssFeedListActivity::activateListFont() {
  savedFontFamily = SETTINGS.fontFamily;
  savedFontPointSize = SETTINGS.readerFontPointSize;
  strncpy(savedSdFontFamilyName, SETTINGS.sdFontFamilyName, sizeof(savedSdFontFamilyName) - 1);
  savedSdFontFamilyName[sizeof(savedSdFontFamilyName) - 1] = '\0';

  SETTINGS.fontFamily = SETTINGS.rssListFontFamily;
  SETTINGS.readerFontPointSize = SETTINGS.rssListFontPointSize;
  strncpy(SETTINGS.sdFontFamilyName, SETTINGS.rssListSdFontFamilyName, sizeof(SETTINGS.sdFontFamilyName) - 1);
  SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
  {
    RenderLock lock(*this);
    sdFontSystem.ensureLoaded(renderer);
  }
  listFontSessionActive = true;
}

void RssFeedListActivity::restoreReaderFont() {
  if (!listFontSessionActive) return;
  SETTINGS.fontFamily = savedFontFamily;
  SETTINGS.readerFontPointSize = savedFontPointSize;
  strncpy(SETTINGS.sdFontFamilyName, savedSdFontFamilyName, sizeof(SETTINGS.sdFontFamilyName) - 1);
  SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
  {
    RenderLock lock(*this);
    sdFontSystem.ensureLoaded(renderer);
  }
  listFontSessionActive = false;
}

bool RssFeedListActivity::loadCategories() {
  std::vector<FreshRssNavigationEntry> categories;
  const bool loaded = FreshRssCache::loadCategoryNavigation(
      categories, [](const uint32_t key) { return RSS_ITEM_STATE.isRead("freshrss", key); });
  entries.clear();
  if (!loaded) {
    return false;
  }
  entries.reserve(categories.size());
  for (const auto& category : categories) entries.push_back(makeFreshFeed(category));
  selectorIndex = std::clamp(selectorIndex, 0, std::max(0, static_cast<int>(entries.size()) - 1));
  return true;
}

bool RssFeedListActivity::loadSubscriptionsForSelectedCategory() {
  std::vector<FreshRssNavigationEntry> subscriptions;
  if (!FreshRssCache::loadSubscriptionNavigationForCategory(
          selectedCategoryId, subscriptions,
          [](const uint32_t key) { return RSS_ITEM_STATE.isRead("freshrss", key); })) {
    entries.clear();
    return false;
  }
  entries.clear();
  entries.reserve(subscriptions.size());
  for (const auto& subscription : subscriptions) entries.push_back(makeFreshFeed(subscription));
  selectorIndex = std::clamp(selectorIndex, 0, std::max(0, static_cast<int>(entries.size()) - 1));
  return true;
}

void RssFeedListActivity::refreshNavigation() {
  if (view == View::DASHBOARD) {
    loadDashboard();
  } else if (view == View::CATEGORIES) {
    loadCategories();
  } else if (view == View::SUBSCRIPTIONS) {
    loadSubscriptionsForSelectedCategory();
  }
}

void RssFeedListActivity::paintSyncProgress(const bool force) {
  const unsigned long now = millis();
  if (!force && now - lastSyncPaintMs < SYNC_PROGRESS_PAINT_MS) return;
  lastSyncPaintMs = now;
  requestUpdateAndWait();
}

void RssFeedListActivity::triggerRefresh() {
  syncProgress.received.store(0);
  syncProgress.limit.store(0);
  syncState = SyncState::CHECK_WIFI;
  statusMessage = tr(STR_CHECKING_WIFI);
  requestUpdate();
  checkAndConnectWifi();
}

void RssFeedListActivity::checkAndConnectWifi() {
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    syncState = SyncState::LOADING;
    statusMessage = tr(STR_LOADING);
    requestUpdate();
    performSync();
    return;
  }
  launchWifiSelection();
}

void RssFeedListActivity::launchWifiSelection() {
  syncState = SyncState::WIFI_SELECTION;
  requestUpdate();
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void RssFeedListActivity::onWifiSelectionComplete(const bool connected) {
  if (connected) {
    syncState = SyncState::LOADING;
    statusMessage = tr(STR_LOADING);
    requestUpdate(true);
    performSync();
  } else {
    syncState = SyncState::IDLE;
    requestUpdate();
  }
}

void RssFeedListActivity::performSync() {
  // host.shouldCancel is intentionally left unset: the network layer
  // (HttpDownloader::fetchUrlWithHeaders/postForm) now honors it end to end,
  // but wiring a live Back-button poll here needs mappedInput.update() called
  // from inside this blocking call — off the normal per-frame cadence — which
  // needs hardware verification of its interaction with wasPressed/
  // wasReleased edge-tracking before it's safe to enable. Until then, Back
  // during LOADING/CHECK_WIFI (see loop()) can't interrupt an in-flight
  // request; it only resets local state and disconnects once the call
  // returns.
  FreshRssSyncHost host{
      syncProgress,
      [this](const char* message) { statusMessage = message; },
      [this](const bool force) { paintSyncProgress(force); }};

  const uint32_t freeBeforeFontRelease = ESP.getFreeHeap();
  const uint32_t maxAllocBeforeFontRelease = ESP.getMaxAllocHeap();
  {
    RenderLock lock(*this);
    // The Gujarati/list SD family includes multiple UI fallback sizes and
    // glyph caches. They are not needed by the HTTP parser and can leave too
    // little contiguous internal RAM for the larger response records that
    // follow FreshRSS's tiny ClientLogin response on C3 devices.
    sdFontSystem.releaseForNetwork(renderer);
  }
  LOG_INF("FRSS", "Sync memory prepared: free %u->%u maxAlloc %u->%u", freeBeforeFontRelease,
          ESP.getFreeHeap(), maxAllocBeforeFontRelease, ESP.getMaxAllocHeap());

  std::string error;
  const bool committed = ::runFreshRssSync(host, error);
  disconnectWifi();
  {
    // runFreshRssSync has destroyed its HTTP/TLS clients, so it is safe to
    // restore the selected list family before painting success or failure.
    RenderLock lock(*this);
    sdFontSystem.ensureLoaded(renderer);
  }
  if (!committed) {
    syncState = FreshRssCache::exists() ? SyncState::IDLE : SyncState::ERROR;
    if (syncState == SyncState::ERROR) {
      errorMessage = error.empty() ? tr(STR_FETCH_FEED_FAILED) : error;
    }
    requestUpdate();
    return;
  }
  syncState = SyncState::IDLE;
  refreshNavigation();
  requestUpdate();
}

void RssFeedListActivity::openSelectedEntry() {
  if (view == View::DASHBOARD) {
    dashboardAction = static_cast<DashboardAction>(selectorIndex);
    if (dashboardAction == DashboardAction::REFRESH) {
      triggerRefresh();
      return;
    }
    if (dashboardAction == DashboardAction::CATEGORIES || dashboardAction == DashboardAction::SUBSCRIPTIONS) {
      selectorIndex = 0;
      const bool hasSnapshot = FreshRssCache::exists();
      if (!loadCategories() && !hasSnapshot) {
        triggerRefresh();
        return;
      }
      view = View::CATEGORIES;
      requestUpdate();
      return;
    }
    if (selectorIndex < 0 || selectorIndex >= static_cast<int>(entries.size())) return;
    const RssFeed feed = entries[selectorIndex];
    startActivityForResult(std::make_unique<RssItemListActivity>(renderer, mappedInput, feed),
                           [this](const ActivityResult&) {
                             refreshNavigation();
                             requestUpdate();
                           });
    return;
  }

  if (selectorIndex < 0 || selectorIndex >= static_cast<int>(entries.size())) return;
  if (view == View::CATEGORIES && dashboardAction == DashboardAction::SUBSCRIPTIONS) {
    selectedCategoryId = entries[selectorIndex].freshId;
    selectedCategoryLabel = entries[selectorIndex].name;
    selectorIndex = 0;
    if (!loadSubscriptionsForSelectedCategory()) return;
    view = View::SUBSCRIPTIONS;
    requestUpdate();
    return;
  }

  const RssFeed feed = entries[selectorIndex];
  startActivityForResult(std::make_unique<RssItemListActivity>(renderer, mappedInput, feed),
                         [this](const ActivityResult&) {
                           refreshNavigation();
                           requestUpdate();
                         });
}

size_t RssFeedListActivity::itemCount() const { return entries.size(); }

std::string RssFeedListActivity::headerText() const {
  if (view == View::DASHBOARD) return tr(STR_RSS_FEEDS);
  if (view == View::CATEGORIES)
    return dashboardAction == DashboardAction::SUBSCRIPTIONS ? tr(STR_FRESHRSS_SELECT_CATEGORY)
                                                              : tr(STR_FRESHRSS_CATEGORIES);
  return selectedCategoryLabel;
}

void RssFeedListActivity::loop() {
  if (syncState == SyncState::WIFI_SELECTION) return;

  if (syncState == SyncState::ERROR) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      triggerRefresh();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      syncState = SyncState::IDLE;
      requestUpdate();
    }
    return;
  }

  if (syncState == SyncState::CHECK_WIFI || syncState == SyncState::LOADING) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      syncState = SyncState::IDLE;
      disconnectWifi();
      requestUpdate();
    }
    return;
  }

  const int totalItems = static_cast<int>(itemCount());

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (view == View::DASHBOARD) {
      onGoHome();
    } else if (view == View::SUBSCRIPTIONS) {
      view = View::CATEGORIES;
      selectorIndex = 0;
      loadCategories();
      requestUpdate();
    } else {
      view = View::DASHBOARD;
      selectorIndex = 0;
      loadDashboard();
      requestUpdate();
    }
    return;
  }

  if (totalItems == 0) return;

  const int pageItems = UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, false);
  const auto metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int contentTop = screen.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = screen.height - contentTop - metrics.verticalSpacing;

  (void)contentTop;
  (void)contentHeight;

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openSelectedEntry();
    return;
  }

  buttonNavigator.onNextRelease([this, totalItems] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, totalItems);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this, totalItems] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, totalItems);
    requestUpdate();
  });
  buttonNavigator.onNextContinuous([this, totalItems, pageItems] {
    selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, totalItems, pageItems);
    requestUpdate();
  });
  buttonNavigator.onPreviousContinuous([this, totalItems, pageItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, totalItems, pageItems);
    requestUpdate();
  });
}

void RssFeedListActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto pageHeight = renderer.getScreenHeight();

  const auto metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const std::string title = headerText();
  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 title.c_str());

  if (syncState == SyncState::CHECK_WIFI || syncState == SyncState::LOADING) {
    const size_t progressTotal = syncProgress.limit.load();
    const size_t progressCurrent = std::min(syncProgress.received.load(), progressTotal);
    const int progressY = pageHeight / 2 + 18;
    const char* visibleStatus =
        syncProgress.processingArticle.load() ? tr(STR_FRESHRSS_PROCESSING_ARTICLE) : statusMessage.c_str();
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - (progressTotal > 0 ? 18 : 0), visibleStatus);
    if (progressTotal > 0) {
      GUI.drawProgressBar(renderer, Rect{50, progressY, renderer.getScreenWidth() - 100, 20}, progressCurrent,
                          progressTotal);
      const std::string progressText = std::to_string(progressCurrent) + " / " + std::to_string(progressTotal);
      renderer.drawCenteredText(SMALL_FONT_ID, progressY + 28, progressText.c_str());
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (syncState == SyncState::ERROR) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, tr(STR_ERROR_MSG));
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, errorMessage.c_str());
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  const int contentTop = screen.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = screen.height - contentTop - metrics.verticalSpacing;
  const int totalItems = static_cast<int>(itemCount());
  if (totalItems == 0) {
    const char* message = view == View::SUBSCRIPTIONS ? tr(STR_FRESHRSS_NO_CACHED_SUBSCRIPTIONS)
                                                        : tr(STR_FRESHRSS_NO_CACHED_ARTICLES);
    renderer.drawCenteredText(UI_10_FONT_ID, contentTop + contentHeight / 2, message);
  } else {
    // The prewarm scope must stay alive until after GUI.drawList() below draws
    // the real, positioned text: its destructor clears the SD font's prewarmed
    // glyph cache (see FontCacheManager::PrewarmScope), so ending the scope
    // here would discard the batch-loaded glyphs before drawList ever draws
    // them, falling back to the small on-demand overflow cache and producing
    // tofu for Gujarati/CJK entry names.
    std::optional<FontCacheManager::PrewarmScope> prewarmScope;
    if (view == View::DASHBOARD && !entries.empty()) {
      auto* fcm = renderer.getFontCacheManager();
      if (fcm) {
        std::string names;
        for (const auto& entry : entries) {
          names += entry.name;
          names += '\n';
        }
        prewarmScope.emplace(fcm->createPrewarmScope());
        renderer.drawText(UI_10_FONT_ID, 0, 0, names.c_str(), true, EpdFontFamily::REGULAR);
        prewarmScope->endScanAndPrewarm();
      }
    }
    GUI.drawList(
        renderer, Rect{screen.x, contentTop, screen.width, contentHeight}, totalItems, selectorIndex,
        [this](const int index) {
          return entries[index].name;
        }, nullptr,
        [this](const int index) {
          if (view != View::DASHBOARD) return UIIcon::Folder;
          switch (index) {
            case 0:
              return UIIcon::Transfer;
            case 1:
              return UIIcon::List;
            case 2:
              return UIIcon::Mail;
            case 3:
              return UIIcon::Star;
            case 4:
              return UIIcon::BookmarkMenu;
            case 5:
              return UIIcon::Folder;
            case 6:
              return UIIcon::Rss;
            default:
              return UIIcon::None;
          }
        },
        [this](const int index) {
          if (view != View::DASHBOARD || index >= static_cast<int>(entries.size())) return std::string();
          if (index == static_cast<int>(DashboardAction::REFRESH)) return std::string();
          if (entries[index].articleCount == 0) return std::string();
          const std::string count = std::to_string(entries[index].articleCount);
          return entries[index].unreadCount == entries[index].articleCount
                     ? count
                     : count + " / " + std::to_string(entries[index].unreadCount) + " unread";
        },
        /*highlightValue=*/true, nullptr, nullptr, 1, true);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
