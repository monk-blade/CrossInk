#include "RssFeedListActivity.h"

#include <GujaratiIntegration.h>
#include <I18n.h>

#include <algorithm>
#include <cstring>

#include "CrossPointSettings.h"
#include "FreshRssCache.h"
#include "RssItemStateStore.h"
#include "SdCardFontSystem.h"
#include "activities/browser/RssItemListActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr const char* DASHBOARD_LABELS[] = {
    "All Articles", "Unread Articles", "Starred Articles", "Reading Queue", "Categories", "Subscriptions"};

RssFeed makeFreshFeed(const FreshRssNavigationEntry& entry) {
  RssFeed feed;
  feed.name = entry.label;
  feed.url = "freshrss";
  feed.isFreshRss = true;
  feed.freshFilter = entry.kind;
  feed.freshId = entry.id;
  feed.freshUnreadOnly = entry.unreadOnly;
  feed.articleCount = entry.articleCount;
  feed.unreadCount = entry.unreadCount;
  GujaratiIntegration::shapeLongUiString(feed.name);
  return feed;
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
  FreshRssNavigationEntry all{FreshRssFilterKind::All, "", "All Articles"};
  all.articleCount = keys.size();
  all.unreadCount = RSS_ITEM_STATE.countUnread("freshrss", keys);
  FreshRssNavigationEntry unread{FreshRssFilterKind::All, "", "Unread Articles", true};
  unread.articleCount = all.unreadCount;
  unread.unreadCount = unread.articleCount;
  size_t starred = 0;
  size_t queued = 0;
  for (const uint32_t key : keys) {
    if (RSS_ITEM_STATE.isStarred("freshrss", key)) ++starred;
    if (RSS_ITEM_STATE.isQueued("freshrss", key)) ++queued;
  }
  entries.push_back(makeFreshFeed(all));
  RssFeed unreadFeed = makeFreshFeed(unread);
  unreadFeed.freshLocalFilter = RssLocalFilter::Unread;
  entries.push_back(std::move(unreadFeed));
  RssFeed starredFeed;
  starredFeed.name = DASHBOARD_LABELS[2];
  starredFeed.url = "freshrss";
  starredFeed.isFreshRss = true;
  starredFeed.freshLocalFilter = RssLocalFilter::Starred;
  starredFeed.articleCount = starred;
  entries.push_back(std::move(starredFeed));
  RssFeed queuedFeed;
  queuedFeed.name = DASHBOARD_LABELS[3];
  queuedFeed.url = "freshrss";
  queuedFeed.isFreshRss = true;
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
  categories.name = DASHBOARD_LABELS[4];
  categories.url = "freshrss";
  categories.isFreshRss = true;
  categories.articleCount = categoryCount;
  entries.push_back(std::move(categories));
  RssFeed subscriptions;
  subscriptions.name = DASHBOARD_LABELS[5];
  subscriptions.url = "freshrss";
  subscriptions.isFreshRss = true;
  subscriptions.articleCount = subscriptionCount;
  entries.push_back(std::move(subscriptions));
  for (auto& entry : entries) GujaratiIntegration::shapeLongUiString(entry.name);
  selectorIndex = std::clamp(selectorIndex, 0, std::max(0, static_cast<int>(entries.size()) - 1));
}

void RssFeedListActivity::onExit() {
  Activity::onExit();
  restoreReaderFont();
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

void RssFeedListActivity::openAllArticlesForInitialRefresh() {
  RssFeed feed;
  feed.name = "All Articles";
  feed.url = "freshrss";
  feed.isFreshRss = true;
  feed.freshFilter = FreshRssFilterKind::All;
  startActivityForResult(std::make_unique<RssItemListActivity>(renderer, mappedInput, std::move(feed)),
                         [this](const ActivityResult&) {
                           refreshNavigation();
                           requestUpdate();
                         });
}

void RssFeedListActivity::openSelectedEntry() {
  if (view == View::DASHBOARD) {
    dashboardAction = static_cast<DashboardAction>(selectorIndex);
    if (dashboardAction == DashboardAction::CATEGORIES || dashboardAction == DashboardAction::SUBSCRIPTIONS) {
      selectorIndex = 0;
      const bool hasSnapshot = FreshRssCache::exists();
      if (!loadCategories() && !hasSnapshot) {
        openAllArticlesForInitialRefresh();
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
    return dashboardAction == DashboardAction::SUBSCRIPTIONS ? "Select Category" : "Categories";
  return selectedCategoryLabel;
}

void RssFeedListActivity::loop() {
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

  const auto metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const std::string title = headerText();
  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 title.c_str());

  const int contentTop = screen.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = screen.height - contentTop - metrics.verticalSpacing;
  const int totalItems = static_cast<int>(itemCount());
  if (totalItems == 0) {
    const char* message = view == View::SUBSCRIPTIONS ? "No feeds with cached articles in this category"
                                                       : "No cached articles. Open All Articles to refresh.";
    renderer.drawCenteredText(UI_10_FONT_ID, contentTop + contentHeight / 2, message);
  } else {
    std::string allNames;
    for (const auto& entry : entries) {
      allNames += entry.name;
      allNames += '\n';
    }
    const int listFontId = SETTINGS.getReaderFontId();

    GUI.drawList(
        renderer, Rect{screen.x, contentTop, screen.width, contentHeight}, totalItems, selectorIndex,
        [this](const int index) {
          return entries[index].name;
        }, nullptr,
        [this](const int index) {
          if (view == View::CATEGORIES) return UIIcon::Folder;
          switch (index) {
            case 0:
              return UIIcon::List;
            case 1:
              return UIIcon::Mail;
            case 2:
              return UIIcon::Star;
            case 3:
              return UIIcon::BookmarkMenu;
            case 4:
              return UIIcon::Folder;
            case 5:
              return UIIcon::Rss;
            default:
              return UIIcon::None;
          }
        },
        [this](const int index) {
          if (index >= static_cast<int>(entries.size())) return std::string();
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
