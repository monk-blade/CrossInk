#include "RssItemListActivity.h"

#include <Arduino.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <GujaratiIntegration.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <functional>
#include <optional>

#include "MappedInputManager.h"
#include "CrossPointSettings.h"
#include "FreshRssCache.h"
#include "FreshRssSyncRunner.h"
#include "RssItemStateStore.h"
#include "activities/reader/ReaderUtils.h"
#include "SdCardFontSystem.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/reader/RssArticleActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr unsigned long SYNC_PROGRESS_PAINT_MS = 250;

struct ArticleRowText {
  std::string primary;
  std::string secondary;
};
}  // namespace

void RssItemListActivity::onEnter() {
  Activity::onEnter();
  activateListFont();
  state = ListState::LOADING;
  items.clear();
  freshVisibleKeys.clear();
  freshUnavailableKeys.clear();
  freshPageStart = 0;
  selectorIndex = 0;
  consumeConfirm = false;
  consumeBack = false;
  longQueueHandled = false;
  queueMessage.clear();
  queueMessageUntil = 0;
  errorMessage.clear();
  cacheState.clear();
  syncProgress.received.store(0);
  syncProgress.limit.store(0);
  syncProgress.processingArticle.store(false);
  lastSyncPaintMs = 0;
  requestUpdate();

  if (loadFromCache()) {
    state = ListState::BROWSING;
    requestUpdate();
    return;
  }

  state = ListState::CHECK_WIFI;
  statusMessage = tr(STR_CHECKING_WIFI);
  requestUpdate();
  checkAndConnectWifi();
}

void RssItemListActivity::paintSyncProgress(const bool force) {
  const unsigned long now = millis();
  if (!force && now - lastSyncPaintMs < SYNC_PROGRESS_PAINT_MS) return;
  lastSyncPaintMs = now;
  // fetchFeed() runs on the activity task. Waiting here is intentional: the
  // old requestUpdate(true) only queued a render and allowed the parser/sink
  // to block the display for tens of seconds on a large article.
  requestUpdateAndWait();
}

void RssItemListActivity::onExit() {
  Activity::onExit();
  items.clear();
  restoreReaderFont();

  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void RssItemListActivity::activateListFont() {
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

void RssItemListActivity::restoreReaderFont() {
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

bool RssItemListActivity::loadFromCache() {
  // loadKeys validates the header, bounded records, and commit marker while
  // producing the compact key list. Do not run the expensive stats scan and
  // then scan the same snapshot again just to open the cached list.
  if (!rebuildVisibleItems()) return false;
  cacheState = tr(STR_FRESHRSS_CACHE_STATE_CACHED);
  FreshRssCache::SnapshotInfo snapshot;
  if (FreshRssCache::loadSnapshotInfo(snapshot) && snapshot.lastRefreshUnix != 0) {
    const time_t now = std::time(nullptr);
    if (now >= static_cast<time_t>(snapshot.lastRefreshUnix) &&
        static_cast<unsigned long>(now - static_cast<time_t>(snapshot.lastRefreshUnix)) >= 24UL * 60UL * 60UL)
      cacheState = tr(STR_FRESHRSS_CACHE_STATE_STALE);
  }
  return visibleCount() == 0 || ensureFreshPageForIndex(0);
}

bool RssItemListActivity::rebuildVisibleItems() {
  std::vector<uint32_t> keys;
  RssListQuery query;
  query.source = feed.freshFilter;
  query.id = feed.freshId;
  query.local = feed.freshLocalFilter;
  if (!FreshRssCache::loadKeys(query, keys)) {
    freshVisibleKeys.clear();
    items.clear();
    return false;
  }
  const bool unreadOnly = feed.freshUnreadOnly || feed.freshLocalFilter == RssLocalFilter::Unread ||
                          (feed.freshLocalFilter == RssLocalFilter::None &&
                           SETTINGS.rssListFilter == CrossPointSettings::RSS_UNREAD_ONLY);
  freshVisibleKeys.clear();
  freshVisibleKeys.reserve(keys.size());
  freshUnavailableKeys.clear();
  for (const uint32_t key : keys) {
    if (unreadOnly && RSS_ITEM_STATE.isRead(feed.url, key)) continue;
    if (feed.freshLocalFilter == RssLocalFilter::Starred && !RSS_ITEM_STATE.isStarred(feed.url, key)) continue;
    if (feed.freshLocalFilter == RssLocalFilter::Queued && !RSS_ITEM_STATE.isQueued(feed.url, key)) continue;
    freshVisibleKeys.push_back(key);
  }
  if (feed.freshLocalFilter == RssLocalFilter::Queued) {
    // Preserve queued IDs that FreshRSS no longer returns. They remain
    // removable locally and never trigger a network lookup.
    for (const uint32_t key : RSS_ITEM_STATE.loadQueuedIds(feed.url)) {
      if (std::find(freshVisibleKeys.begin(), freshVisibleKeys.end(), key) != freshVisibleKeys.end()) continue;
      freshVisibleKeys.push_back(key);
      freshUnavailableKeys.push_back(key);
    }
  }
  items.clear();
  freshPageStart = 0;
  selectorIndex = std::min(selectorIndex, std::max(0, static_cast<int>(freshVisibleKeys.size()) - 1));
  return true;
}

size_t RssItemListActivity::visibleCount() const { return freshVisibleKeys.size(); }

bool RssItemListActivity::listHasSubtitle() const {
  const bool comfortable = SETTINGS.rssListDensity == CrossPointSettings::RSS_COMFORTABLE_LIST;
  return comfortable;
}

bool RssItemListActivity::ensureFreshPageForIndex(const size_t index) {
  if (index >= freshVisibleKeys.size()) {
    items.clear();
    freshPageStart = index;
    return true;
  }
  const int pageItems = UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, listHasSubtitle());
  const size_t pageSize = static_cast<size_t>(std::max(1, pageItems));
  const size_t pageStart = (index / pageSize) * pageSize;
  if (pageStart == freshPageStart && index < freshPageStart + items.size()) return true;
  const size_t pageEnd = std::min(freshVisibleKeys.size(), pageStart + pageSize);
  std::vector<uint32_t> availableKeys;
  availableKeys.reserve(pageEnd - pageStart);
  for (size_t i = pageStart; i < pageEnd; ++i) {
    if (std::find(freshUnavailableKeys.begin(), freshUnavailableKeys.end(), freshVisibleKeys[i]) ==
        freshUnavailableKeys.end())
      availableKeys.push_back(freshVisibleKeys[i]);
  }
  std::vector<CachedRssItem> availableItems;
  if (!availableKeys.empty() && !FreshRssCache::loadItemsByKeys(availableKeys, 0, availableKeys.size(), availableItems))
    return false;
  std::vector<CachedRssItem> page;
  page.reserve(pageEnd - pageStart);
  for (size_t i = pageStart, availableIndex = 0; i < pageEnd; ++i) {
    if (std::find(freshUnavailableKeys.begin(), freshUnavailableKeys.end(), freshVisibleKeys[i]) !=
        freshUnavailableKeys.end()) {
      CachedRssItem unavailable;
      unavailable.key = freshVisibleKeys[i];
      unavailable.title = tr(STR_FRESHRSS_UNAVAILABLE_TITLE);
      unavailable.subtitle = tr(STR_FRESHRSS_HOLD_CONFIRM_REMOVE);
      unavailable.unavailable = true;
      page.push_back(std::move(unavailable));
    } else if (availableIndex < availableItems.size()) {
      page.push_back(std::move(availableItems[availableIndex++]));
    } else {
      return false;
    }
  }
  items = std::move(page);
  freshPageStart = pageStart;
  return true;
}

const CachedRssItem* RssItemListActivity::itemAtVisibleIndex(const size_t index) const {
  if (index < freshPageStart || index >= freshPageStart + items.size()) return nullptr;
  return &items[index - freshPageStart];
}

int RssItemListActivity::selectedItemIndex() {
  if (!ensureFreshPageForIndex(static_cast<size_t>(std::max(0, selectorIndex)))) return -1;
  if (selectorIndex < 0 || selectorIndex >= static_cast<int>(freshVisibleKeys.size())) return -1;
  if (selectorIndex < static_cast<int>(freshPageStart) ||
      selectorIndex >= static_cast<int>(freshPageStart + items.size()))
    return -1;
  return selectorIndex - static_cast<int>(freshPageStart);
}

void RssItemListActivity::toggleSelectedStar() {
  const int itemIndex = selectedItemIndex();
  if (itemIndex < 0) return;
  RSS_ITEM_STATE.toggleStar(feed.url, items[itemIndex].key);
  RSS_ITEM_STATE.saveIfDirty();
  requestUpdate();
}

void RssItemListActivity::toggleSelectedQueue() {
  const int itemIndex = selectedItemIndex();
  if (itemIndex < 0) return;
  if (!RSS_ITEM_STATE.toggleQueued(feed.url, items[itemIndex].key)) {
    queueMessage = tr(STR_FRESHRSS_QUEUE_FULL);
    queueMessageUntil = millis() + 2200;
    requestUpdate();
    return;
  }
  RSS_ITEM_STATE.saveIfDirty();
  queueMessage = RSS_ITEM_STATE.isQueued(feed.url, items[itemIndex].key) ? tr(STR_FRESHRSS_ADDED_TO_QUEUE)
                                                                          : tr(STR_FRESHRSS_REMOVED_FROM_QUEUE);
  queueMessageUntil = millis() + 1600;
  if (feed.freshLocalFilter == RssLocalFilter::Queued) rebuildVisibleItems();
  requestUpdate();
}

void RssItemListActivity::checkAndConnectWifi() {
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    state = ListState::LOADING;
    statusMessage = tr(STR_LOADING);
    requestUpdate();
    fetchFeed();
    return;
  }
  launchWifiSelection();
}

void RssItemListActivity::launchWifiSelection() {
  state = ListState::WIFI_SELECTION;
  requestUpdate();

  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void RssItemListActivity::onWifiSelectionComplete(const bool connected) {
  if (connected) {
    state = ListState::LOADING;
    statusMessage = tr(STR_LOADING);
    requestUpdate(true);
    fetchFeed();
  } else if (FreshRssCache::exists()) {
    // A manual refresh was cancelled — keep showing what we already had.
    state = ListState::BROWSING;
    requestUpdate();
  } else {
    state = ListState::ERROR;
    errorMessage = tr(STR_WIFI_CONN_FAILED);
    requestUpdate();
  }
}

void RssItemListActivity::fetchFeed() {
  const auto disconnectWifi = [] {
    if (WiFi.getMode() != WIFI_MODE_NULL) {
      WiFi.disconnect(false);
      WiFi.mode(WIFI_OFF);
    }
  };

  // host.shouldCancel is intentionally left unset here — see the identical
  // comment in RssFeedListActivity::performSync() for why live cancellation
  // isn't wired up yet even though the network layer now supports it.
  FreshRssSyncHost host{syncProgress,
                        [this](const char* message) { statusMessage = message; },
                        [this](const bool force) { paintSyncProgress(force); }};
  std::string error;
  if (!runFreshRssSync(host, error)) {
    disconnectWifi();
    const bool hasCache = FreshRssCache::exists();
    state = hasCache ? ListState::BROWSING : ListState::ERROR;
    if (hasCache) cacheState = tr(STR_FRESHRSS_CACHE_STATE_REFRESH_FAILED);
    if (!hasCache) errorMessage = error.empty() ? tr(STR_FETCH_FEED_FAILED) : error;
    requestUpdate();
    return;
  }
  statusMessage = tr(STR_FRESHRSS_RELOADING_CACHE);
  paintSyncProgress(true);
  rebuildVisibleItems();
  if (visibleCount() > 0 && !ensureFreshPageForIndex(0)) {
    disconnectWifi();
    state = ListState::ERROR;
    errorMessage = tr(STR_PARSE_FEED_FAILED);
    requestUpdate();
    return;
  }
  selectorIndex = 0;
  disconnectWifi();
  state = ListState::BROWSING;
  cacheState = tr(STR_FRESHRSS_CACHE_STATE_CURRENT);
  if (visibleCount() == 0) errorMessage.clear();
  requestUpdate();
}

void RssItemListActivity::openSelectedItem() {
  const int itemIndex = selectedItemIndex();
  if (itemIndex < 0) return;
  if (items[itemIndex].unavailable) {
    queueMessage = tr(STR_FRESHRSS_ARTICLE_NOT_CACHED);
    queueMessageUntil = millis() + 2200;
    requestUpdate();
    return;
  }
  startActivityForResult(std::make_unique<RssArticleActivity>(renderer, mappedInput, feed, items[itemIndex]),
                         [this](const ActivityResult& result) {
                           rebuildVisibleItems();
                           if (const auto* rssResult = std::get_if<RssArticleResult>(&result.data);
                               rssResult && rssResult->openNextUnread) {
                             openNextUnread();
                             return;
                           }
                           requestUpdate();
                         });  // repaint the now-read row on return
}

void RssItemListActivity::openNextUnread() {
  for (int i = selectorIndex; i < static_cast<int>(visibleCount()); ++i) {
    const uint32_t key = freshVisibleKeys[i];
    if (!RSS_ITEM_STATE.isRead(feed.url, key)) {
      selectorIndex = i;
      openSelectedItem();
      return;
    }
  }
  requestUpdate();
}

void RssItemListActivity::loop() {
  if (state == ListState::WIFI_SELECTION) return;

  if (consumeConfirm && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    consumeConfirm = false;
    return;
  }
  if (consumeBack && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    consumeBack = false;
    return;
  }

  if (state == ListState::ERROR) {
    int tx = 0;
    int ty = 0;
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(tx, ty)) {
      state = ListState::CHECK_WIFI;
      statusMessage = tr(STR_CHECKING_WIFI);
      requestUpdate();
      checkAndConnectWifi();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      finish();
    }
    return;
  }

  if (state == ListState::CHECK_WIFI || state == ListState::LOADING) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) finish();
    return;
  }

  // BROWSING
  if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
    longQueueHandled = false;
  } else if (!longQueueHandled && mappedInput.getHeldTime() >= ReaderUtils::BOOKMARK_HOLD_MS) {
    toggleSelectedQueue();
    longQueueHandled = true;
    consumeConfirm = true;
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right) &&
      SETTINGS.rssStarAction == CrossPointSettings::RSS_STAR_RIGHT_BUTTON) {
    toggleSelectedStar();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openSelectedItem();
    return;
  }

  const int totalItems = static_cast<int>(visibleCount());
  if (totalItems == 0) return;

  const bool showSubtitle = listHasSubtitle();
  const int pageItems = UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, showSubtitle);

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

void RssItemListActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto pageHeight = renderer.getScreenHeight();

  const auto metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  std::string articleCount;
  std::string headerSubtitleText;
  const char* headerSubtitle = nullptr;
  if (state == ListState::BROWSING) {
    articleCount = std::to_string(visibleCount());
    headerSubtitleText = articleCount;
    const bool cacheNeedsAttention =
        cacheState == tr(STR_FRESHRSS_CACHE_STATE_STALE) || cacheState == tr(STR_FRESHRSS_CACHE_STATE_REFRESH_FAILED);
    if (cacheNeedsAttention) headerSubtitleText += " · " + cacheState;
    headerSubtitle = headerSubtitleText.c_str();
  }
  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 feed.name.c_str(), headerSubtitle);

  if (state == ListState::CHECK_WIFI || state == ListState::LOADING) {
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

  if (state == ListState::ERROR) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, tr(STR_ERROR_MSG));
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, errorMessage.c_str());
    if (mappedInput.hasTouch()) {
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 40, tr(STR_TAP_TO_RETRY));
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  const int contentTop = screen.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = screen.height - contentTop - metrics.verticalSpacing;

  if (visibleCount() == 0) {
    const char* emptyMessage = nullptr;
    if (feed.freshLocalFilter == RssLocalFilter::Starred) emptyMessage = tr(STR_FRESHRSS_NO_STARRED);
    else if (feed.freshLocalFilter == RssLocalFilter::Queued) emptyMessage = tr(STR_FRESHRSS_QUEUE_EMPTY);
    else if (feed.freshLocalFilter == RssLocalFilter::Unread || feed.freshUnreadOnly ||
             SETTINGS.rssListFilter == CrossPointSettings::RSS_UNREAD_ONLY)
      emptyMessage = tr(STR_RSS_NO_UNREAD);
    else emptyMessage = tr(STR_NO_ENTRIES);
    renderer.drawCenteredText(UI_10_FONT_ID, contentTop + contentHeight / 2, emptyMessage);
  } else {
    // Prewarm only the visible page's titles before drawList measures/draws
    // them row by row — same reasoning (and font/style combo) as
    // EpubReaderChapterSelectionActivity::render(): doing on-demand glyph
    // loads in that hot path is noticeably sluggish for anything with more
    // unique glyphs than the 8-slot overflow cache holds.
    const bool showSubtitle = listHasSubtitle();
    const int pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, showSubtitle);
    const int pageStartIndex = pageItems > 0 ? (selectorIndex / pageItems) * pageItems : 0;
    if (!ensureFreshPageForIndex(static_cast<size_t>(std::max(0, pageStartIndex)))) {
      state = ListState::ERROR;
      errorMessage = tr(STR_PARSE_FEED_FAILED);
      requestUpdate();
      renderer.displayBuffer();
      return;
    }
    const int pageEndIndex = std::min(static_cast<int>(visibleCount()), pageStartIndex + std::max(1, pageItems));

    // Article titles own the row. Comfortable mode uses both lines for the
    // headline; compact mode keeps one full-width line. Leave only the active
    // theme's selection padding and scroll gutter outside the wrap width.
    const bool roundedRaff = SETTINGS.uiTheme == CrossPointSettings::ROUNDEDRAFF;
    const int titleSideBudget =
        roundedRaff ? 36 : std::max(16, metrics.scrollBarWidth + metrics.scrollBarRightOffset + 16);
    const int titleWidth = std::max(40, screen.width - metrics.contentSidePadding * 2 - titleSideBudget);
    const auto titleStyle = roundedRaff ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    std::vector<ArticleRowText> visibleRows;
    visibleRows.reserve(static_cast<size_t>(std::max(0, pageEndIndex - pageStartIndex)));
    std::string visibleTitles;
    visibleTitles.reserve(visibleRows.capacity() * 96);
    for (int i = pageStartIndex; i < pageEndIndex; ++i) {
      const auto* item = itemAtVisibleIndex(static_cast<size_t>(i));
      ArticleRowText row;
      if (!item) {
        visibleRows.push_back(std::move(row));
        continue;
      }

      auto titleLines =
          renderer.wrappedText(UI_10_FONT_ID, item->title.c_str(), titleWidth, showSubtitle ? 2 : 1, titleStyle);
      if (!titleLines.empty()) row.primary = std::move(titleLines[0]);
      if (showSubtitle) {
        if (titleLines.size() > 1) {
          row.secondary = std::move(titleLines[1]);
        } else if (item->unavailable) {
          row.secondary = item->subtitle;
        }
      }
      visibleRows.push_back(std::move(row));
      visibleTitles += item->title;
      visibleTitles += '\n';
    }
    // The prewarm scope must stay alive until after GUI.drawList() below draws
    // the real, positioned rows: its destructor clears the SD font's prewarmed
    // glyph cache (see FontCacheManager::PrewarmScope), so ending the scope
    // here would discard the batch-loaded glyphs before drawList ever draws
    // them, falling back to the small on-demand overflow cache and producing
    // tofu for shaped Gujarati titles.
    std::optional<FontCacheManager::PrewarmScope> prewarmScope;
    if (!visibleTitles.empty()) {
      auto* fcm = renderer.getFontCacheManager();
      if (fcm) {
        prewarmScope.emplace(fcm->createPrewarmScope());
        renderer.drawText(UI_10_FONT_ID, 0, 0, visibleTitles.c_str(), true, titleStyle);
        renderer.drawText(UI_10_FONT_ID, 0, 0, "\xe2\x80\xa6", true, titleStyle);
        prewarmScope->endScanAndPrewarm();
      }
    }

    GUI.drawList(
        renderer, Rect{screen.x, contentTop, screen.width, contentHeight}, static_cast<int>(visibleCount()),
        selectorIndex,
        [&visibleRows, pageStartIndex](const int index) {
          const int row = index - pageStartIndex;
          return row >= 0 && row < static_cast<int>(visibleRows.size()) ? visibleRows[static_cast<size_t>(row)].primary
                                                                        : std::string();
        },
        showSubtitle ? std::function<std::string(int)>([&visibleRows, pageStartIndex](const int index) {
          const int row = index - pageStartIndex;
          return row >= 0 && row < static_cast<int>(visibleRows.size())
                     ? visibleRows[static_cast<size_t>(row)].secondary
                     : std::string();
        })
                     : nullptr,
        nullptr, nullptr, false,
        [this](const int index) {
          const auto* item = itemAtVisibleIndex(static_cast<size_t>(index));
          return item && RSS_ITEM_STATE.isRead(feed.url, item->key);
        },
        nullptr, 1, true, showSubtitle);
  }

  if (!queueMessage.empty() && millis() < queueMessageUntil) {
    renderer.drawCenteredText(SMALL_FONT_ID, screen.y + screen.height - metrics.buttonHintsHeight - 8, queueMessage.c_str());
  }

  if (SETTINGS.rssShowButtonHints) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), "",
                                              SETTINGS.rssStarAction == CrossPointSettings::RSS_STAR_RIGHT_BUTTON
                                                  ? tr(STR_RSS_STAR)
                                                  : "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  renderer.displayBuffer();
}
