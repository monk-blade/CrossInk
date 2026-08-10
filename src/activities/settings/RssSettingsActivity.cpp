#include "RssSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <functional>
#include <iterator>
#include <string>

#include "CrossPointSettings.h"
#include "FreshRssCache.h"
#include "MappedInputManager.h"
#include "ReaderFontSizes.h"
#include "RssItemCache.h"
#include "RssItemStateStore.h"
#include "SdCardFontSystem.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr StrId TAB_IDS[] = {StrId::STR_RSS_VIEW, StrId::STR_RSS_FONT_SETTINGS, StrId::STR_RSS_CONTROLS,
                             StrId::STR_RSS_CACHE};
constexpr StrId VIEW_IDS[] = {StrId::STR_RSS_FILTER, StrId::STR_RSS_LIST_DENSITY, StrId::STR_RSS_DATE_DISPLAY,
                              StrId::STR_RSS_ARTICLE_TITLE, StrId::STR_RSS_ARTICLE_SEPARATOR};
constexpr StrId FONT_IDS[] = {StrId::STR_RSS_FONT_FAMILY, StrId::STR_RSS_FONT_SIZE,
                              StrId::STR_RSS_LIST_FONT_FAMILY, StrId::STR_RSS_LIST_FONT_SIZE,
                              StrId::STR_LINE_SPACING, StrId::STR_RSS_PARAGRAPH_SPACING,
                              StrId::STR_RSS_PARAGRAPH_INDENT, StrId::STR_PARA_ALIGNMENT, StrId::STR_SCREEN_MARGIN};
constexpr StrId CONTROL_IDS[] = {StrId::STR_RSS_REFRESH_BUTTON, StrId::STR_RSS_MARK_READ_TIMING,
                                 StrId::STR_RSS_END_ACTION, StrId::STR_RSS_BUTTON_HINTS, StrId::STR_RSS_STAR_ACTION};
constexpr StrId CACHE_IDS[] = {StrId::STR_RSS_CACHE_STATS, StrId::STR_RSS_CLEAR_CACHE};

constexpr StrId FILTER_VALUES[] = {StrId::STR_RSS_ALL_ITEMS, StrId::STR_RSS_UNREAD_ONLY};
constexpr StrId DENSITY_VALUES[] = {StrId::STR_RSS_COMPACT, StrId::STR_RSS_COMFORTABLE};
constexpr StrId DATE_VALUES[] = {StrId::STR_RSS_HUMAN_DATE, StrId::STR_RSS_COMPACT_DATE, StrId::STR_RSS_HIDE_DATE};
constexpr StrId ON_OFF_VALUES[] = {StrId::STR_STATE_OFF, StrId::STR_STATE_ON};
constexpr StrId LINE_VALUES[] = {StrId::STR_TIGHT, StrId::STR_NORMAL, StrId::STR_WIDE};
constexpr StrId ALIGN_VALUES[] = {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER, StrId::STR_ALIGN_RIGHT,
                                  StrId::STR_BOOK_S_STYLE};
constexpr StrId REFRESH_VALUES[] = {StrId::STR_DIR_LEFT, StrId::STR_DIR_RIGHT, StrId::STR_RSS_REFRESH_DISABLED};
constexpr StrId MARK_VALUES[] = {StrId::STR_RSS_ON_OPEN, StrId::STR_RSS_ON_LAST_PAGE};
constexpr StrId END_VALUES[] = {StrId::STR_RSS_RETURN_TO_LIST, StrId::STR_RSS_NEXT_UNREAD};
constexpr StrId STAR_VALUES[] = {StrId::STR_RSS_STAR_DISABLED, StrId::STR_RSS_STAR_RIGHT, StrId::STR_RSS_STAR_LONG_OPEN};
template <size_t N>
std::string optionText(const StrId (&options)[N], const uint8_t value) {
  return value < N ? std::string(I18N.get(options[value])) : std::string();
}

}  // namespace

void RssSettingsActivity::onEnter() {
  Activity::onEnter();
  tab_ = Tab::View;
  std::fill(std::begin(selectedIndex_), std::end(selectedIndex_), 0);
  rebuildFonts();
  rebuildFontSizes();
  requestUpdate();
}

void RssSettingsActivity::onExit() { Activity::onExit(); }

void RssSettingsActivity::rebuildFonts() {
  fontNames_.clear();
  fontNames_.push_back(I18N.get(StrId::STR_NOTO_SERIF));
  fontNames_.push_back(I18N.get(StrId::STR_NOTO_SANS));
  if (registry_) {
    for (const auto& family : registry_->getFamilies()) fontNames_.push_back(family.name);
  }
}

int RssSettingsActivity::currentFontIndex(const bool listFont) const {
  const char* selectedSdFamily = listFont ? SETTINGS.rssListSdFontFamilyName : SETTINGS.rssSdFontFamilyName;
  const uint8_t selectedBuiltIn = listFont ? SETTINGS.rssListFontFamily : SETTINGS.rssFontFamily;
  if (selectedSdFamily[0] != '\0' && registry_) {
    const auto& families = registry_->getFamilies();
    for (int i = 0; i < static_cast<int>(families.size()); ++i) {
      if (families[i].name == selectedSdFamily) return CrossPointSettings::BUILTIN_FONT_COUNT + i;
    }
  }
  return selectedBuiltIn < CrossPointSettings::BUILTIN_FONT_COUNT ? selectedBuiltIn : 0;
}

void RssSettingsActivity::rebuildFontSizes() {
  articleFontSizes_ = readerFontPointSizes(registry_, SETTINGS.rssSdFontFamilyName);
  if (articleFontSizes_.empty()) articleFontSizes_ = {12, 14, 16, 18};
  listFontSizes_ = readerFontPointSizes(registry_, SETTINGS.rssListSdFontFamilyName);
  if (listFontSizes_.empty()) listFontSizes_ = {12, 14, 16, 18};
}

int RssSettingsActivity::rowCount() const {
  switch (tab_) {
    case Tab::View:
      return VIEW_ROWS;
    case Tab::Font:
      return FONT_ROWS;
    case Tab::Controls:
      return CONTROL_ROWS;
    case Tab::Cache:
      return CACHE_ROWS;
    default:
      return 0;
  }
}

void RssSettingsActivity::switchTab(const int direction) {
  const int count = static_cast<int>(Tab::Count);
  int next = static_cast<int>(tab_) + direction;
  if (next < 0) next = count - 1;
  if (next >= count) next = 0;
  tab_ = static_cast<Tab>(next);
  selectedIndex() = 0;
  requestUpdate();
}

void RssSettingsActivity::saveAndRefresh() {
  SETTINGS.saveToFile();
  rebuildFontSizes();
  requestUpdate();
}

std::string RssSettingsActivity::rowTitle(const int row) const {
  const StrId* ids = nullptr;
  switch (tab_) {
    case Tab::View:
      ids = VIEW_IDS;
      break;
    case Tab::Font:
      ids = FONT_IDS;
      break;
    case Tab::Controls:
      ids = CONTROL_IDS;
      break;
    case Tab::Cache:
      ids = CACHE_IDS;
      break;
    default:
      return {};
  }
  return row >= 0 && row < rowCount() ? std::string(I18N.get(ids[row])) : std::string();
}

std::string RssSettingsActivity::rowValue(const int row) const {
  switch (tab_) {
    case Tab::View:
      switch (row) {
        case 0:
          return optionText(FILTER_VALUES, SETTINGS.rssListFilter);
        case 1:
          return optionText(DENSITY_VALUES, SETTINGS.rssListDensity);
        case 2:
          return optionText(DATE_VALUES, SETTINGS.rssDateDisplay);
        case 3:
          return optionText(ON_OFF_VALUES, SETTINGS.rssShowArticleTitle);
        case 4:
          return optionText(ON_OFF_VALUES, SETTINGS.rssShowArticleSeparator);
        default:
          return {};
      }
    case Tab::Font:
      switch (row) {
        case 0: {
          const int index = currentFontIndex(false);
          return index >= 0 && index < static_cast<int>(fontNames_.size()) ? fontNames_[index] : fontNames_[0];
        }
        case 1: {
          const uint8_t selected = snapToNearestPointSize(articleFontSizes_, SETTINGS.rssFontPointSize);
          return std::to_string(selected) + " pt";
        }
        case 2:
          {
            const int index = currentFontIndex(true);
            return index >= 0 && index < static_cast<int>(fontNames_.size()) ? fontNames_[index] : fontNames_[0];
          }
        case 3:
          return std::to_string(snapToNearestPointSize(listFontSizes_, SETTINGS.rssListFontPointSize)) + " pt";
        case 4:
          return optionText(LINE_VALUES, SETTINGS.rssLineSpacing);
        case 5:
          return optionText(LINE_VALUES, SETTINGS.rssParagraphSpacing);
        case 6:
          return optionText(ON_OFF_VALUES, SETTINGS.rssParagraphIndent);
        case 7:
          return optionText(ALIGN_VALUES, SETTINGS.rssParagraphAlignment);
        case 8:
          return std::to_string(SETTINGS.rssScreenMargin) + " px";
        default:
          return {};
      }
    case Tab::Controls:
      switch (row) {
        case 0:
          return optionText(REFRESH_VALUES, SETTINGS.rssRefreshButton);
        case 1:
          return optionText(MARK_VALUES, SETTINGS.rssMarkReadTiming);
        case 2:
          return optionText(END_VALUES, SETTINGS.rssArticleEndAction);
        case 3:
          return optionText(ON_OFF_VALUES, SETTINGS.rssShowButtonHints);
        case 4:
          return optionText(STAR_VALUES, SETTINGS.rssStarAction);
        default:
          return {};
      }
    case Tab::Cache:
      switch (row) {
        case 0: {
          const auto cache = FreshRssCache::stats();
          char text[160];
          snprintf(text, sizeof(text), tr(STR_RSS_CACHE_STATS), static_cast<unsigned int>(cache.itemCount),
                   static_cast<unsigned int>(cache.completeBodies), static_cast<unsigned int>(cache.truncatedBodies),
                   static_cast<unsigned int>(cache.bytes / 1024));
          std::string result = text;
          result += " · index " + std::to_string(cache.indexBytes / 1024) + " KB · queue " +
                    std::to_string(RSS_ITEM_STATE.countQueued("freshrss"));
          if (cache.lastRefreshUnix != 0) {
            const time_t now = std::time(nullptr);
            if (now >= static_cast<time_t>(cache.lastRefreshUnix))
              result += " · age " + std::to_string(static_cast<unsigned long>(now - cache.lastRefreshUnix) / 3600) + "h";
          }
          return result;
        }
        case 1:
          return tr(STR_SELECT);
        default:
          return {};
      }
    default:
      return {};
  }
}

void RssSettingsActivity::activateRow(const int row) {
  auto show = [this](const StrId title, const StrId* values, const int count, const uint8_t current,
                     const std::function<void(int)>& callback) {
    optionPopup_.show(title, values, count, current, [this, callback](const int value) {
      callback(value);
      saveAndRefresh();
    });
    requestUpdate();
  };

  switch (tab_) {
    case Tab::View:
      if (row == 0) show(StrId::STR_RSS_FILTER, FILTER_VALUES, 2, SETTINGS.rssListFilter,
                         [](const int value) { SETTINGS.rssListFilter = value; });
      else if (row == 1) show(StrId::STR_RSS_LIST_DENSITY, DENSITY_VALUES, 2, SETTINGS.rssListDensity,
                              [](const int value) { SETTINGS.rssListDensity = value; });
      else if (row == 2) show(StrId::STR_RSS_DATE_DISPLAY, DATE_VALUES, 3, SETTINGS.rssDateDisplay,
                              [](const int value) { SETTINGS.rssDateDisplay = value; });
      else if (row == 3) show(StrId::STR_RSS_ARTICLE_TITLE, ON_OFF_VALUES, 2, SETTINGS.rssShowArticleTitle,
                              [](const int value) { SETTINGS.rssShowArticleTitle = value; });
      else if (row == 4) show(StrId::STR_RSS_ARTICLE_SEPARATOR, ON_OFF_VALUES, 2, SETTINGS.rssShowArticleSeparator,
                              [](const int value) { SETTINGS.rssShowArticleSeparator = value; });
      return;
    case Tab::Font:
      if (row == 0 || row == 2) {
        const bool listFont = row == 2;
        optionPopup_.show(listFont ? StrId::STR_RSS_LIST_FONT_FAMILY : StrId::STR_RSS_FONT_FAMILY, fontNames_,
                          currentFontIndex(listFont), [this, listFont](const int value) {
          if (value < CrossPointSettings::BUILTIN_FONT_COUNT) {
            if (listFont) {
              SETTINGS.rssListFontFamily = value;
              SETTINGS.rssListSdFontFamilyName[0] = '\0';
            } else {
              SETTINGS.rssFontFamily = value;
              SETTINGS.rssSdFontFamilyName[0] = '\0';
            }
          } else if (registry_) {
            const int sdIndex = value - CrossPointSettings::BUILTIN_FONT_COUNT;
            const auto& families = registry_->getFamilies();
            if (sdIndex >= 0 && sdIndex < static_cast<int>(families.size())) {
              char* selectedName = listFont ? SETTINGS.rssListSdFontFamilyName : SETTINGS.rssSdFontFamilyName;
              const size_t selectedNameSize = listFont ? sizeof(SETTINGS.rssListSdFontFamilyName)
                                                       : sizeof(SETTINGS.rssSdFontFamilyName);
              strncpy(selectedName, families[sdIndex].name.c_str(), selectedNameSize - 1);
              selectedName[selectedNameSize - 1] = '\0';
              rebuildFontSizes();
              if (listFont) {
                SETTINGS.rssListFontPointSize = snapToNearestPointSize(listFontSizes_, SETTINGS.rssListFontPointSize);
              } else {
                SETTINGS.rssFontPointSize = snapToNearestPointSize(articleFontSizes_, SETTINGS.rssFontPointSize);
              }
            }
          }
          saveAndRefresh();
        });
        requestUpdate();
      } else if (row == 1 || row == 3) {
        const bool listFont = row == 3;
        const auto& availableSizes = listFont ? listFontSizes_ : articleFontSizes_;
        const uint8_t currentSize = listFont ? SETTINGS.rssListFontPointSize : SETTINGS.rssFontPointSize;
        const StrId title = listFont ? StrId::STR_RSS_LIST_FONT_SIZE : StrId::STR_RSS_FONT_SIZE;
        std::vector<std::string> sizes;
        sizes.reserve(availableSizes.size());
        const uint8_t selected = snapToNearestPointSize(availableSizes, currentSize);
        int selectedIndex = 0;
        for (int i = 0; i < static_cast<int>(availableSizes.size()); ++i) {
          sizes.push_back(std::to_string(availableSizes[i]) + " pt");
          if (availableSizes[i] == selected) selectedIndex = i;
        }
        optionPopup_.show(title, sizes, selectedIndex, [this, listFont](const int value) {
          const auto& selectedSizes = listFont ? listFontSizes_ : articleFontSizes_;
          if (value >= 0 && value < static_cast<int>(selectedSizes.size())) {
            if (listFont) {
              SETTINGS.rssListFontPointSize = selectedSizes[value];
            } else {
              SETTINGS.rssFontPointSize = selectedSizes[value];
            }
          }
          saveAndRefresh();
        });
        requestUpdate();
      } else if (row == 4) {
        show(StrId::STR_LINE_SPACING, LINE_VALUES, 3, SETTINGS.rssLineSpacing,
             [](const int value) { SETTINGS.rssLineSpacing = value; });
      } else if (row == 5) {
        show(StrId::STR_RSS_PARAGRAPH_SPACING, LINE_VALUES, 3, SETTINGS.rssParagraphSpacing,
             [](const int value) { SETTINGS.rssParagraphSpacing = value; });
      } else if (row == 6) {
        show(StrId::STR_RSS_PARAGRAPH_INDENT, ON_OFF_VALUES, 2, SETTINGS.rssParagraphIndent,
             [](const int value) { SETTINGS.rssParagraphIndent = value; });
      } else if (row == 7) {
        show(StrId::STR_PARA_ALIGNMENT, ALIGN_VALUES, 5, SETTINGS.rssParagraphAlignment,
             [](const int value) { SETTINGS.rssParagraphAlignment = value; });
      } else if (row == 8) {
        const std::vector<std::string> margins = {"5 px", "10 px", "15 px", "20 px", "25 px", "30 px", "35 px", "40 px"};
        const int selected = std::clamp<int>((SETTINGS.rssScreenMargin - CrossPointSettings::SCREEN_MARGIN_MIN) /
                                                 CrossPointSettings::SCREEN_MARGIN_STEP,
                                             0, static_cast<int>(margins.size()) - 1);
        optionPopup_.show(StrId::STR_SCREEN_MARGIN, margins, selected, [this](const int value) {
          SETTINGS.rssScreenMargin = CrossPointSettings::SCREEN_MARGIN_MIN +
                                     static_cast<uint8_t>(value * CrossPointSettings::SCREEN_MARGIN_STEP);
          saveAndRefresh();
        });
        requestUpdate();
      }
      return;
    case Tab::Controls:
      if (row == 0) show(StrId::STR_RSS_REFRESH_BUTTON, REFRESH_VALUES, 3, SETTINGS.rssRefreshButton,
                         [](const int value) { SETTINGS.rssRefreshButton = value; });
      else if (row == 1) show(StrId::STR_RSS_MARK_READ_TIMING, MARK_VALUES, 2, SETTINGS.rssMarkReadTiming,
                              [](const int value) { SETTINGS.rssMarkReadTiming = value; });
      else if (row == 2) show(StrId::STR_RSS_END_ACTION, END_VALUES, 2, SETTINGS.rssArticleEndAction,
                              [](const int value) { SETTINGS.rssArticleEndAction = value; });
      else if (row == 3) show(StrId::STR_RSS_BUTTON_HINTS, ON_OFF_VALUES, 2, SETTINGS.rssShowButtonHints,
                              [](const int value) { SETTINGS.rssShowButtonHints = value; });
      else if (row == 4) show(StrId::STR_RSS_STAR_ACTION, STAR_VALUES, 3, SETTINGS.rssStarAction,
                              [](const int value) { SETTINGS.rssStarAction = value; });
      return;
    case Tab::Cache:
      if (row == 1) {
        const char* options[] = {tr(STR_CANCEL), tr(STR_CLEAR_BUTTON)};
        clearPopup_.show(tr(STR_RSS_CLEAR_CACHE), options, 2, 0, [this](const int value) {
          if (value == 1) clearRssCache();
          requestUpdate();
        });
        requestUpdate();
      }
      return;
    default:
      return;
  }
}

void RssSettingsActivity::clearRssCache() {
  RssItemCache::clearAll();
  FreshRssCache::clear();
  RSS_ITEM_STATE.clearAll();
  RSS_ITEM_STATE.saveIfDirty();
  requestUpdate();
}

bool RssSettingsActivity::handleTouch() {
  int tx = 0;
  int ty = 0;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int tabTop = metrics.topPadding + metrics.headerHeight;
  const int listTop = tabTop + metrics.tabBarHeight + metrics.verticalSpacing;
  const int listHeight = renderer.getScreenHeight() -
                         (tabTop + metrics.tabBarHeight + metrics.buttonHintsHeight + metrics.verticalSpacing * 2);

  if (mappedInput.wasScreenTouchDown(tx, ty) || mappedInput.wasScreenTapped(tx, ty)) {
    std::vector<TabInfo> tabs;
    for (int i = 0; i < static_cast<int>(Tab::Count); ++i) tabs.push_back({I18N.get(TAB_IDS[i]), i == static_cast<int>(tab_)});
    int tabIndex = -1;
    if (GUI.tabIndexFromPoint(renderer, Rect{0, tabTop, renderer.getScreenWidth(), metrics.tabBarHeight}, tabs, tx, ty,
                              tabIndex)) {
      tab_ = static_cast<Tab>(tabIndex);
      selectedIndex() = 0;
      requestUpdate();
      return true;
    }
  }

  if (mappedInput.wasScreenTapped(tx, ty) && ty >= listTop && ty < listTop + listHeight && rowCount() > 0) {
    const int rowHeight = std::max(1, listHeight / rowCount());
    const int row = std::clamp((ty - listTop) / rowHeight, 0, rowCount() - 1);
    selectedIndex() = row + 1;
    activateRow(row);
    return true;
  }
  return false;
}

void RssSettingsActivity::loop() {
  if (clearPopup_.handleInput(mappedInput, [this] { requestUpdate(); })) return;
  if (optionPopup_.handleInput(mappedInput, [this] { requestUpdate(); })) return;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (selectedIndex() == 0) {
      switchTab();
    } else {
      activateRow(selectedIndex() - 1);
    }
    return;
  }
  if (handleTouch()) return;

  const int ringSize = rowCount() + 1;
  buttonNavigator_.onNextRelease([this, ringSize] {
    selectedIndex() = ButtonNavigator::nextIndex(selectedIndex(), ringSize);
    requestUpdate();
  });
  buttonNavigator_.onPreviousRelease([this, ringSize] {
    selectedIndex() = ButtonNavigator::previousIndex(selectedIndex(), ringSize);
    requestUpdate();
  });
  buttonNavigator_.onNextContinuous([this] { switchTab(); });
  buttonNavigator_.onPreviousContinuous([this] { switchTab(-1); });
}

void RssSettingsActivity::render(RenderLock&&) {
  if (clearPopup_.processRender(renderer, mappedInput)) return;
  if (optionPopup_.processRender(renderer, mappedInput)) return;

  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_RSS_SETTINGS));

  std::vector<TabInfo> tabs;
  for (int i = 0; i < static_cast<int>(Tab::Count); ++i) tabs.push_back({I18N.get(TAB_IDS[i]), i == static_cast<int>(tab_)});
  const int tabTop = metrics.topPadding + metrics.headerHeight;
  GUI.drawTabBar(renderer, Rect{0, tabTop, pageWidth, metrics.tabBarHeight}, tabs, selectedIndex() == 0);
  const int listTop = tabTop + metrics.tabBarHeight + metrics.verticalSpacing;
  const int listHeight = pageHeight - (listTop + metrics.buttonHintsHeight + metrics.verticalSpacing);
  GUI.drawList(renderer, Rect{0, listTop, pageWidth, listHeight}, rowCount(), selectedIndex() - 1,
               [this](const int index) { return rowTitle(index); }, nullptr, nullptr,
               [this](const int index) { return rowValue(index); }, true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), selectedIndex() == 0 ? I18N.get(TAB_IDS[(static_cast<int>(tab_) + 1) % 4])
                                                                                : tr(STR_SELECT),
                                            tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
