#include "FileBrowserActionActivity.h"

#include <FontCacheManager.h>
#include <GujaratiIntegration.h>
#include <I18n.h>

#include <algorithm>
#include <optional>

#include "components/UiAppHelpers.h"

FileBrowserActionActivity::FileBrowserActionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                     std::string title, std::vector<MenuItem> items,
                                                     const bool ignoreInitialConfirmRelease)
    : Activity("FileBrowserAction", renderer, mappedInput),
      title(std::move(title)),
      items(std::move(items)),
      ignoreConfirmRelease(ignoreInitialConfirmRelease) {}

void FileBrowserActionActivity::onExit() {
  releaseUiSdFontCachesForLowMemory(renderer);
  Activity::onExit();
}

void FileBrowserActionActivity::onEnter() {
  Activity::onEnter();
  // The parent browser remains underneath this popup. Acquire the render lock
  // before reclaiming its transient SD-font caches.
  {
    RenderLock lock(*this);
    releaseUiSdFontCachesForLowMemory(renderer);
  }
  // A touch long-press opens this activity while the finger is still down.
  // Wait for that contact to end so its release cannot activate a menu row.
  int touchX = 0;
  int touchY = 0;
  ignoreTouchRelease = mappedInput.isScreenTouchHeld(touchX, touchY);
  optionLabels.resize(items.size());
  std::transform(items.begin(), items.end(), optionLabels.begin(),
                 [](const MenuItem& item) { return std::string(I18N.get(item.labelId)); });
  GujaratiIntegration::shapeLongUiString(title);
  optionPopup.show(title.c_str(), optionLabels, 0, [this](const int index) {
    if (index < 0 || index >= static_cast<int>(items.size())) return;
    selectionMade = true;
    setResult(FileBrowserActionResult{static_cast<int>(items[index].action)});
    finish();
  });
  requestUpdate();
}

void FileBrowserActionActivity::finishCancelled() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void FileBrowserActionActivity::loop() {
  if (ignoreTouchRelease) {
    if (mappedInput.wasScreenTouchReleased()) {
      ignoreTouchRelease = false;
    }
    return;
  }

  if (ignoreConfirmRelease) {
    const bool confirmReleased = mappedInput.wasReleased(MappedInputManager::Button::Confirm);
    if (confirmReleased || !mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      ignoreConfirmRelease = false;
      return;
    }
  }

  optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
  if (!optionPopup.isActive() && !selectionMade) finishCancelled();
}

void FileBrowserActionActivity::render(RenderLock&&) {
  // Book/file titles can contain scripts supplied by the active SD font. Shape
  // and batch-load the popup's bold title before the real draw; the small
  // on-demand overflow cache cannot hold a full Gujarati title reliably.
  std::optional<FontCacheManager::PrewarmScope> titlePrewarm;
  if (GujaratiIntegration::containsGujarati(title)) {
    if (auto* fcm = renderer.getFontCacheManager(); fcm != nullptr) {
      titlePrewarm.emplace(fcm->createPrewarmScope());
      renderer.drawText(UI_12_FONT_ID, 0, 0, title.c_str(), true, EpdFontFamily::BOLD);
      titlePrewarm->endScanAndPrewarm();
    }
  }
  optionPopup.processRender(renderer, mappedInput);
}
