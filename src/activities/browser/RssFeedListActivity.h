#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "FreshRssSyncRunner.h"
#include "RssFeedStore.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * FreshRSS dashboard navigation. Every dashboard target is local and cache
 * only except Refresh, which owns the manual FreshRSS sync path.
 */
class RssFeedListActivity final : public Activity {
  enum class View : uint8_t { DASHBOARD, CATEGORIES, SUBSCRIPTIONS };
  enum class DashboardAction : uint8_t { REFRESH, ALL, UNREAD, STARRED, QUEUED, CATEGORIES, SUBSCRIPTIONS };
  enum class SyncState : uint8_t { IDLE, CHECK_WIFI, WIFI_SELECTION, LOADING, ERROR };

  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  View view = View::DASHBOARD;
  DashboardAction dashboardAction = DashboardAction::ALL;
  SyncState syncState = SyncState::IDLE;
  std::vector<RssFeed> entries;
  std::string selectedCategoryId;
  std::string selectedCategoryLabel;
  std::string statusMessage;
  std::string errorMessage;
  std::string refreshMenuLabel;
  bool listFontSessionActive = false;
  uint8_t savedFontFamily = 0;
  uint8_t savedFontPointSize = 0;
  char savedSdFontFamilyName[32] = "";
  FreshRssSyncProgress syncProgress;
  unsigned long lastSyncPaintMs = 0;

  bool loadCategories();
  void loadDashboard();
  bool loadSubscriptionsForSelectedCategory();
  void refreshNavigation();
  void openSelectedEntry();
  void triggerRefresh();
  void checkAndConnectWifi();
  void launchWifiSelection();
  void onWifiSelectionComplete(bool connected);
  void performSync();
  void paintSyncProgress(bool force = false);
  size_t itemCount() const;
  std::string headerText() const;
  void activateListFont();
  void restoreReaderFont();
  bool preventAutoSleep() override { return syncState != SyncState::IDLE; }

 public:
  explicit RssFeedListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("RssFeedList", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
