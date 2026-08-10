#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "RssFeedStore.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * FreshRSS dashboard navigation. Every dashboard target is local and cache
 * only; the article list owns the sole manual refresh path.
 */
class RssFeedListActivity final : public Activity {
  enum class View : uint8_t { DASHBOARD, CATEGORIES, SUBSCRIPTIONS };
  enum class DashboardAction : uint8_t { ALL, UNREAD, STARRED, QUEUED, CATEGORIES, SUBSCRIPTIONS };

  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  View view = View::DASHBOARD;
  DashboardAction dashboardAction = DashboardAction::ALL;
  std::vector<RssFeed> entries;
  std::string selectedCategoryId;
  std::string selectedCategoryLabel;
  bool listFontSessionActive = false;
  uint8_t savedFontFamily = 0;
  uint8_t savedFontPointSize = 0;
  char savedSdFontFamilyName[32] = "";

  bool loadCategories();
  void loadDashboard();
  bool loadSubscriptionsForSelectedCategory();
  void refreshNavigation();
  void openSelectedEntry();
  void openAllArticlesForInitialRefresh();
  size_t itemCount() const;
  std::string headerText() const;
  void activateListFont();
  void restoreReaderFont();

 public:
  explicit RssFeedListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("RssFeedList", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
