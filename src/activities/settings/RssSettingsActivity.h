#pragma once

#include <SdCardFontRegistry.h>

#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "util/ButtonNavigator.h"

/** Dedicated RSS settings editor. RSS typography is persisted separately from
 * the normal EPUB/TXT reader profile so feed experimentation cannot change
 * book reading preferences. */
class RssSettingsActivity final : public Activity {
 public:
  enum class Tab : uint8_t { View, Font, Controls, Cache, Count };

  RssSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const SdCardFontRegistry* registry)
      : Activity("RssSettings", renderer, mappedInput), registry_(registry) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static constexpr int VIEW_ROWS = 5;
  static constexpr int FONT_ROWS = 9;
  static constexpr int CONTROL_ROWS = 5;
  static constexpr int CACHE_ROWS = 2;

  const SdCardFontRegistry* registry_ = nullptr;
  ButtonNavigator buttonNavigator_;
  OptionPopup optionPopup_;
  OptionPopup clearPopup_;
  Tab tab_ = Tab::View;
  int selectedIndex_[static_cast<int>(Tab::Count)] = {};
  std::vector<std::string> fontNames_;
  std::vector<uint8_t> articleFontSizes_;
  std::vector<uint8_t> listFontSizes_;

  int currentFontIndex(bool listFont) const;
  void rebuildFonts();
  void rebuildFontSizes();
  int rowCount() const;
  int& selectedIndex() { return selectedIndex_[static_cast<int>(tab_)]; }
  int selectedIndex() const { return selectedIndex_[static_cast<int>(tab_)]; }
  void switchTab(int direction = 1);
  void activateRow(int row);
  void saveAndRefresh();
  bool handleTouch();
  std::string rowTitle(int row) const;
  std::string rowValue(int row) const;
  void clearRssCache();
};
