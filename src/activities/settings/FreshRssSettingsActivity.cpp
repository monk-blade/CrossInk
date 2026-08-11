#include "FreshRssSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <iterator>

#include "CrossPointSettings.h"
#include "FreshRssAccountStore.h"
#include "FreshRssCache.h"
#include "MappedInputManager.h"
#include "RssItemStateStore.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr uint16_t LIMITS[] = {200, 500, 1000};
}

void FreshRssSettingsActivity::onEnter() {
  Activity::onEnter();
  FRESHRSS_ACCOUNT.loadFromFile();
  editAccount = FRESHRSS_ACCOUNT.getAccount();
  selectedIndex = 0;
  saveError = false;
  requestUpdate();
}

void FreshRssSettingsActivity::onExit() { Activity::onExit(); }

void FreshRssSettingsActivity::save() {
  saveError = !FRESHRSS_ACCOUNT.update(editAccount);
  requestUpdate();
}

std::string FreshRssSettingsActivity::rowValue(const int row) const {
  switch (row) {
    case 0:
      return editAccount.apiUrl.empty() ? tr(STR_NOT_SET) : editAccount.apiUrl;
    case 1:
      return editAccount.username.empty() ? tr(STR_NOT_SET) : editAccount.username;
    case 2:
      return editAccount.password.empty() ? tr(STR_NOT_SET) : "******";
    case 3:
      return std::to_string(SETTINGS.freshRssArticleLimit) + "";
    case 4: {
      const auto stats = FreshRssCache::stats();
      char text[160];
      snprintf(text, sizeof(text), tr(STR_RSS_CACHE_STATS), static_cast<unsigned int>(stats.itemCount),
               static_cast<unsigned int>(stats.completeBodies), static_cast<unsigned int>(stats.truncatedBodies),
               static_cast<unsigned int>(stats.bytes / 1024));
      std::string result = text;
      result += " · index " + std::to_string(stats.indexBytes / 1024) + " KB · queue " +
                std::to_string(RSS_ITEM_STATE.countQueued("freshrss"));
      if (stats.lastRefreshUnix != 0) {
        const time_t now = std::time(nullptr);
        if (now >= static_cast<time_t>(stats.lastRefreshUnix))
          result += " · age " + std::to_string(static_cast<unsigned long>(now - stats.lastRefreshUnix) / 3600) + "h";
      }
      return result;
    }
    case 5:
      return tr(STR_SELECT);
    default:
      return {};
  }
}

void FreshRssSettingsActivity::selectRow() {
  if (selectedIndex == 0 || selectedIndex == 1 || selectedIndex == 2) {
    const StrId title = selectedIndex == 0 ? StrId::STR_FRESHRSS_API_URL
                                           : selectedIndex == 1 ? StrId::STR_USERNAME : StrId::STR_PASSWORD;
    const InputType type = selectedIndex == 0 ? InputType::Url : selectedIndex == 2 ? InputType::Password : InputType::Text;
    const std::string current = selectedIndex == 0 ? editAccount.apiUrl : selectedIndex == 1 ? editAccount.username : editAccount.password;
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, I18N.get(title), current, 255, type),
                           [this](const ActivityResult& result) {
                             if (result.isCancelled) return;
                             const auto& text = std::get<KeyboardResult>(result.data).text;
                             if (selectedIndex == 0) editAccount.apiUrl = text;
                             else if (selectedIndex == 1) editAccount.username = text;
                             else editAccount.password = text;
                             save();
                           });
  } else if (selectedIndex == 3) {
    const auto it = std::find(std::begin(LIMITS), std::end(LIMITS), SETTINGS.freshRssArticleLimit);
    const size_t next = it == std::end(LIMITS) ? 0 : (static_cast<size_t>(it - std::begin(LIMITS)) + 1) % std::size(LIMITS);
    SETTINGS.freshRssArticleLimit = LIMITS[next];
    SETTINGS.saveToFile();
    // Keep the read-marker cap for the FreshRSS pseudo-feed in step with the
    // new limit so raising it doesn't start evicting read markers below the
    // new snapshot size (see RssItemStateStore::setFreshRssArticleLimit).
    RSS_ITEM_STATE.setFreshRssArticleLimit(SETTINGS.freshRssArticleLimit);
    requestUpdate();
  } else if (selectedIndex == 5) {
    if (FreshRssCache::clear()) {
      RSS_ITEM_STATE.clearAll();
      RSS_ITEM_STATE.saveIfDirty();
    } else {
      saveError = true;
    }
    requestUpdate();
  }
}

void FreshRssSettingsActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    selectRow();
    return;
  }
  buttonNavigator.onNext([this] { selectedIndex = (selectedIndex + 1) % ROWS; requestUpdate(); });
  buttonNavigator.onPrevious([this] { selectedIndex = (selectedIndex + ROWS - 1) % ROWS; requestUpdate(); });
}

void FreshRssSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                 tr(STR_FRESHRSS_SETTINGS));
  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int height = renderer.getScreenHeight() - top - metrics.buttonHintsHeight - metrics.verticalSpacing;
  GUI.drawList(renderer, Rect{0, top, renderer.getScreenWidth(), height}, ROWS, static_cast<int>(selectedIndex),
               [](int row) {
                 static const StrId ids[] = {StrId::STR_FRESHRSS_API_URL, StrId::STR_USERNAME, StrId::STR_PASSWORD,
                                             StrId::STR_FRESHRSS_ARTICLE_LIMIT, StrId::STR_RSS_CACHE_STATS,
                                             StrId::STR_FRESHRSS_CLEAR_CACHE};
                 return std::string(I18N.get(ids[row]));
               }, nullptr, nullptr, [this](int row) { return rowValue(row); }, true);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  if (saveError) GUI.drawPopup(renderer, tr(STR_ERROR_GENERAL_FAILURE));
  renderer.displayBuffer();
}
