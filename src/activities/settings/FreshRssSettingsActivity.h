#pragma once

#include "FreshRssAccountStore.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class FreshRssSettingsActivity final : public Activity {
 public:
  explicit FreshRssSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("FreshRssSettings", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  size_t selectedIndex = 0;
  FreshRssAccount editAccount;
  bool saveError = false;

  static constexpr int ROWS = 6;
  void selectRow();
  void save();
  std::string rowValue(int row) const;
};
