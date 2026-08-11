#include "XtcReaderChapterSelectionActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <GujaratiIntegration.h>
#include <I18n.h>

#include <optional>
#include <string>

#include "MappedInputManager.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/UIScale.h"
#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_ROW = 1;
}

XtcReaderChapterSelectionActivity::XtcReaderChapterSelectionActivity(GfxRenderer& renderer,
                                                                     MappedInputManager& mappedInput,
                                                                     const std::shared_ptr<Xtc>& xtc,
                                                                     const uint32_t currentPage)
    : Activity("XtcReaderChapterSelection", renderer, mappedInput),
      xtc(xtc),
      currentPage(currentPage),
      uiTarget(makeUiTarget(renderer)),
      app(uiTarget, uiTarget.deviceContext()) {}

int XtcReaderChapterSelectionActivity::findChapterIndexForPage(const uint32_t page) const {
  if (!xtc) return 0;
  const auto chapters = xtc->getChapters();
  for (size_t i = 0; i < chapters.size(); i++) {
    if (page >= chapters[i].startPage && page <= chapters[i].endPage) return static_cast<int>(i);
  }
  return 0;
}

void XtcReaderChapterSelectionActivity::onEnter() {
  Activity::onEnter();
  mappedInput.setReaderTouchscreenOverride(true);
  if (!xtc) return;

  selectorIndex = findChapterIndexForPage(currentPage);
  topIndex = 0;
  visibleRows = 1;
  initialViewportPending = true;
  uiReady = false;
  app.setTheme(uiThemeTokens(uiTarget));
  app.on(ACTION_ROW, &XtcReaderChapterSelectionActivity::onRowEvent, this);
  app.setScreen(&XtcReaderChapterSelectionActivity::chapterScreen, this);
  requestUpdate();
}

void XtcReaderChapterSelectionActivity::onExit() {
  mappedInput.setReaderTouchscreenOverride(false);
  Activity::onExit();
}

void XtcReaderChapterSelectionActivity::selectChapter() {
  const auto& chapters = xtc->getChapters();
  if (selectorIndex >= 0 && selectorIndex < static_cast<int>(chapters.size())) {
    setResult(PageResult{chapters[selectorIndex].startPage});
    finish();
  }
}

void XtcReaderChapterSelectionActivity::onRowEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<XtcReaderChapterSelectionActivity*>(user);
  const int totalItems = static_cast<int>(self->xtc->getChapters().size());
  if (event.value < 0 || event.value >= totalItems) return;
  self->selectorIndex = event.value;
  self->app.clearTapFlash();
  self->selectChapter();
}

void XtcReaderChapterSelectionActivity::loop() {
  const int totalItems = static_cast<int>(xtc->getChapters().size());
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const Rect header{safe.x, safe.y + metrics.topPadding, safe.width,
                    TouchHeaderBackButton::height(metrics, mappedInput)};
  if (TouchHeaderBackButton::wasTapped(mappedInput, header) ||
      mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }
  if (uiReady) {
    const fui::InputSnapshot snap = touchSnapshotFrom(mappedInput);
    if (snap.touchPressed || snap.touchReleased) {
      const auto event = app.route(snap);
      if (app.invalidated()) requestUpdate();
      if (event) return;
    }
  }
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    const int delta = swipe == MappedInputManager::SwipeDir::Up ? visibleRows : -visibleRows;
    const int next = scrollListBy(topIndex, delta, visibleRows, totalItems);
    if (next != topIndex) {
      topIndex = next;
      requestUpdate();
    }
    return;
  }
  const auto moveSelection = [this, totalItems](const int index) {
    selectorIndex = index;
    topIndex = followListSelection(selectorIndex, topIndex, visibleRows, totalItems);
    requestUpdate();
  };
  buttonNavigator.onNextRelease(
      [this, totalItems, &moveSelection] { moveSelection(ButtonNavigator::nextIndex(selectorIndex, totalItems)); });
  buttonNavigator.onPreviousRelease(
      [this, totalItems, &moveSelection] { moveSelection(ButtonNavigator::previousIndex(selectorIndex, totalItems)); });
  buttonNavigator.onNextContinuous([this, totalItems, &moveSelection] {
    moveSelection(ButtonNavigator::nextPageIndex(selectorIndex, totalItems, visibleRows));
  });
  buttonNavigator.onPreviousContinuous([this, totalItems, &moveSelection] {
    moveSelection(ButtonNavigator::previousPageIndex(selectorIndex, totalItems, visibleRows));
  });
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) selectChapter();
}

void XtcReaderChapterSelectionActivity::chapterScreen(UiApp::ScreenType& screen, void* user) {
  static_cast<XtcReaderChapterSelectionActivity*>(user)->buildChapterScreen(screen);
}

void XtcReaderChapterSelectionActivity::buildChapterScreen(UiApp::ScreenType& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMargin(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + TouchHeaderBackButton::height(metrics, mappedInput)),
      static_cast<int16_t>(renderer.getScreenWidth() - safe.x - safe.width),
      static_cast<int16_t>(renderer.getScreenHeight() - safe.y - safe.height), static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  const auto& chapters = xtc->getChapters();
  if (chapters.empty()) {
    screen.centeredText(tr(STR_NO_CHAPTERS), screen.theme().bodyText);
    return;
  }
  std::vector<std::string> labels(chapters.size());
  std::vector<fui::ListItem> items;
  items.reserve(chapters.size());
  for (size_t i = 0; i < chapters.size(); ++i) {
    labels[i] = chapters[i].name[0] == '\0' ? tr(STR_UNNAMED) : chapters[i].name;
    GujaratiIntegration::shapeLongUiString(labels[i]);
    fui::ListItem row;
    row.label = labels[i].c_str();
    row.actionValue = static_cast<int16_t>(i);
    items.push_back(row);
  }
  fui::ListProps props;
  props.items = items.data();
  props.count = static_cast<uint16_t>(items.size());
  props.selectedIndex = static_cast<int16_t>(selectorIndex);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.labelText = screen.theme().bodyText;
  const auto rows = configureUiList(props, screen.theme(), screen.body());
  visibleRows = rows > 0 ? rows : 1;
  const int chapterCount = static_cast<int>(chapters.size());
  topIndex = initialViewportPending ? followListSelection(selectorIndex, 0, visibleRows, chapterCount)
                                    : scrollListBy(topIndex, 0, visibleRows, chapterCount);
  initialViewportPending = false;
  props.topIndex = static_cast<uint16_t>(topIndex);
  screen.list(props);
}

void XtcReaderChapterSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const Rect header{safe.x, safe.y + metrics.topPadding, safe.width,
                    TouchHeaderBackButton::height(metrics, mappedInput)};
  if (mappedInput.hasTouchHardware()) {
    TouchHeaderBackButton::draw(renderer, uiTarget, header, tr(STR_SELECT_CHAPTER), true);
  } else {
    GUI.drawHeader(renderer, header, tr(STR_SELECT_CHAPTER), nullptr, true);
  }
  uiReady = false;
  // The prewarm scope must stay alive until after app.render() below draws the
  // real, positioned rows: its destructor clears the SD font's prewarmed glyph
  // cache (see FontCacheManager::PrewarmScope), so ending the scope here would
  // discard the batch-loaded glyphs before app.render() ever draws them,
  // falling back to the small on-demand overflow cache and producing tofu for
  // shaped Gujarati chapter titles.
  std::optional<FontCacheManager::PrewarmScope> prewarmScope;
  if (xtc) {
    const int fontId = uiScaleSpec().bodyFontId;
    std::string prewarmText;
    for (const auto& chapter : xtc->getChapters()) {
      std::string label = chapter.name[0] == '\0' ? tr(STR_UNNAMED) : chapter.name;
      GujaratiIntegration::shapeLongUiString(label);
      prewarmText += label;
      prewarmText += '\n';
    }
    if (auto* fcm = renderer.getFontCacheManager(); fcm && !prewarmText.empty()) {
      prewarmScope.emplace(fcm->createPrewarmScope());
      renderer.drawText(fontId, 0, 0, prewarmText.c_str(), true);
      prewarmScope->endScanAndPrewarm();
    }
  }
  app.render();
  uiReady = true;
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
  renderer.displayBuffer();
}
