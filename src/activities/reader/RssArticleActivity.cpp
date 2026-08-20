#include "RssArticleActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
// The cache-missing screen uses localized error and navigation labels.
#include <GujaratiIntegration.h>
#include <Utf8.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <optional>

#include "CrossPointSettings.h"
#include "FreshRssCache.h"
#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "RssItemStateStore.h"
#include "../../../lib/ReaderLayout/ReaderLayout.h"
#include "SdCardFontSystem.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr size_t MAX_PAGE_PREWARM_BYTES = 2048;

void appendPrewarmText(std::string& target, const std::string& word) {
  if (target.size() >= MAX_PAGE_PREWARM_BYTES || word.empty()) return;
  const size_t room = MAX_PAGE_PREWARM_BYTES - target.size();
  const size_t take = std::min(room, word.size());
  target.append(word.data(), take);
  // Never hand the UTF-8 scanner a continuation-byte tail.
  while (!target.empty() && (static_cast<unsigned char>(target.back()) & 0xC0) == 0x80) target.pop_back();
  if (target.size() < MAX_PAGE_PREWARM_BYTES && take == word.size()) target.push_back(' ');
}

uint32_t firstCodepoint(const std::string& word) {
  const auto* ptr = reinterpret_cast<const unsigned char*>(word.c_str());
  while (true) {
    const uint32_t cp = utf8NextCodepoint(&ptr);
    if (cp == 0) return 0;
    if (cp != 0x00AD) return cp;
  }
}

uint32_t lastCodepoint(const std::string& word) {
  if (word.empty()) return 0;
  size_t i = word.size() - 1;
  while (i > 0 && (static_cast<uint8_t>(word[i]) & 0xC0) == 0x80) --i;
  const auto* ptr = reinterpret_cast<const unsigned char*>(word.c_str() + i);
  return utf8NextCodepoint(&ptr);
}

}  // namespace

void RssArticleActivity::onEnter() {
  Activity::onEnter();
  activateRssFont();
  loadBody();
  if (!cacheError) buildWrappedLines();
  currentPage = 0;
  pagesUntilFullRefresh = 1;  // full refresh on first paint of a new screen

  // Opening an article is "engaging" with it — mirrors how the OPDS/EPUB
  // flows treat opening content as the read signal, no separate preview mode.
  if (!cacheError && SETTINGS.rssMarkReadTiming == CrossPointSettings::RSS_MARK_READ_ON_OPEN &&
      !RSS_ITEM_STATE.isRead(feed.url, item.key)) {
    RSS_ITEM_STATE.markRead(feed.url, item.key);
  }

  requestUpdate();
}

void RssArticleActivity::loadBody() {
  bool complete = false;
  const bool loaded = item.cacheOffset != 0
                          ? FreshRssCache::loadItemBodyByOffset(item.cacheOffset, item.key, item.body, complete)
                          : FreshRssCache::loadItemBody(item.key, item.body, complete);
  if (!loaded) {
    cacheError = true;
    item.body.clear();
    return;
  }

  // Older RSS snapshots were written before Gujarati shaping was added to the
  // cache writer. EPUB sections are shaped during parsing, so perform the same
  // bounded, in-memory compatibility pass when an article is opened. New
  // shaped PUA streams are idempotent and remain unchanged. This keeps article
  // opening offline and does not create a second body-sized buffer.
  GujaratiIntegration::shapeLongUiString(item.title);
  for (auto& paragraph : item.body) {
    for (auto& word : paragraph.words) GujaratiIntegration::shapeSanitizedWord(word.text);
  }
  item.bodyTruncated = !complete;
}

void RssArticleActivity::onExit() {
  Activity::onExit();
  RSS_ITEM_STATE.saveIfDirty();  // debounced: only touches SD if markRead() actually changed something
  allLines.clear();
  restoreReaderFont();
}

TextAlign RssArticleActivity::resolveAlign(const TextAlign paragraphAlign) const {
  if (paragraphAlign != TextAlign::INHERIT) return paragraphAlign;
  // No explicit HTML alignment for this paragraph — fall back to the user's
  // own reading preference, the same one TxtReaderActivity/EpubReaderActivity
  // already honor.
  switch (SETTINGS.rssParagraphAlignment) {
    case CrossPointSettings::LEFT_ALIGN:
      return TextAlign::LEFT;
    case CrossPointSettings::CENTER_ALIGN:
      return TextAlign::CENTER;
    case CrossPointSettings::RIGHT_ALIGN:
      return TextAlign::RIGHT;
    case CrossPointSettings::BOOK_STYLE:
      // BOOK_STYLE is the EPUB setting's name for its normal book layout.
      // RSS has no CSS-level default to inherit here, so use the same
      // justified body treatment as the book reader.
      return TextAlign::JUSTIFY;
    case CrossPointSettings::JUSTIFIED:
    default:
      return TextAlign::JUSTIFY;
  }
}

int RssArticleActivity::gapAdvance(const StyledWord& left, const StyledWord& right) const {
  const uint32_t leftCp = lastCodepoint(left.text);
  const uint32_t rightCp = firstCodepoint(right.text);
  if (right.continuesPrevious) {
    return renderer.getKerning(cachedFontId, leftCp, rightCp, left.style);
  }
  // Use the same combined space-plus-flanking-kern metric as ParsedText.
  // Measuring only the standalone space makes Gujarati lines drift and then
  // forces justification to distribute the wrong amount of spare width.
  return renderer.getSpaceAdvance(cachedFontId, leftCp, rightCp, left.style);
}

void RssArticleActivity::finalizeLine(std::vector<LineWord>&& words, const TextAlign align, const bool isParagraphEnd,
                                      const int indent) {
  int naturalWidth = 0;
  int gapCount = 0;
  for (size_t i = 0; i < words.size(); ++i) {
    naturalWidth += words[i].width;
    if (i > 0) {
      naturalWidth += gapAdvance(*words[i - 1].word, *words[i].word);
      if (!words[i].word->continuesPrevious) gapCount++;
    }
  }
  const int availableWidth = viewportWidth - indent;
  allLines.push_back(
      WrappedLine{std::move(words), align, isParagraphEnd, indent, availableWidth, naturalWidth, gapCount});
}

void RssArticleActivity::wrapParagraph(const RichParagraph& paragraph) {
  const TextAlign align = resolveAlign(paragraph.align);
  // Force a visible, one-em-like indent on normal body paragraphs regardless
  // of script or publisher styling. Centered/right-aligned semantic blocks
  // and the centered article title keep their natural layout.
  const int firstLineIndent = ReaderLayout::forcedParagraphIndent(
      SETTINGS.rssParagraphIndent == CrossPointSettings::RSS_FORCE_PARAGRAPH_INDENT &&
          (align == TextAlign::LEFT || align == TextAlign::JUSTIFY),
      renderer.getFontAscenderSize(cachedFontId), lineHeight);
  if (paragraph.words.empty()) return;

  // EPUB paragraphs use ParsedText's minimum-raggedness line breaker rather
  // than a greedy wrap. Reproduce that decision here while retaining RSS's
  // lightweight line representation. Greedy wrapping is the main reason RSS
  // pages still differed from books after spacing metrics were corrected: an
  // early short line changes every following line and page boundary.
  const size_t wordCount = paragraph.words.size();
  std::vector<uint16_t> wordWidths;
  wordWidths.reserve(wordCount);
  for (const auto& word : paragraph.words) {
    const int width = renderer.getTextAdvanceX(cachedFontId, word.text.c_str(), word.style);
    wordWidths.push_back(
        static_cast<uint16_t>(std::min(width, static_cast<int>(std::numeric_limits<uint16_t>::max()))));
  }

  std::vector<bool> continues(wordCount, false);
  std::vector<bool> noSpaceBefore(wordCount, false);
  for (size_t i = 0; i < wordCount; ++i) continues[i] = paragraph.words[i].continuesPrevious;
  const auto breaks = ReaderLayout::minimumRaggednessBreaks(
      wordWidths, continues, noSpaceBefore, viewportWidth, firstLineIndent,
      [this, &paragraph](const size_t left, const size_t right) {
        return gapAdvance(paragraph.words[left], paragraph.words[right]);
      });
  size_t lineStart = 0;
  size_t lineNumber = 0;
  while (lineStart < wordCount) {
    size_t lineEnd = lineNumber < breaks.size() ? breaks[lineNumber] : lineStart + 1;
    if (lineEnd <= lineStart || lineEnd > wordCount) lineEnd = lineStart + 1;

    std::vector<LineWord> line;
    line.reserve(lineEnd - lineStart);
    for (size_t i = lineStart; i < lineEnd; ++i) line.push_back(LineWord{&paragraph.words[i], wordWidths[i]});
    finalizeLine(std::move(line), align, lineEnd == wordCount, lineStart == 0 ? firstLineIndent : 0);
    lineStart = lineEnd;
    ++lineNumber;
  }
}

RichParagraph RssArticleActivity::buildTitleParagraph() const {
  RichParagraph titleParagraph;
  titleParagraph.align = TextAlign::CENTER;

  std::string word;
  auto flush = [&] {
    if (!word.empty()) {
      titleParagraph.words.push_back(StyledWord{word, EpdFontFamily::BOLD, false});
      word.clear();
    }
  };
  for (const char c : item.title) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      flush();
    } else {
      word += c;
    }
  }
  flush();
  return titleParagraph;
}

void RssArticleActivity::buildWrappedLines() {
  cachedFontId = SETTINGS.getReaderFontId();

  int viewTop = 0;
  int viewRight = 0;
  int viewBottom = 0;
  int viewLeft = 0;
  renderer.getOrientedViewableTRBL(&viewTop, &viewRight, &viewBottom, &viewLeft);
  viewBottom += UITheme::getInstance().getStatusBarHeight();
  const int rssMargin = SETTINGS.rssScreenMargin;
  marginTop = viewTop + rssMargin;
  marginLeft = viewLeft + rssMargin;

  viewportWidth = renderer.getScreenWidth() - viewLeft - viewRight - rssMargin * 2;
  const int viewportHeight = renderer.getScreenHeight() - viewTop - viewBottom - rssMargin;
  // Use the renderer's same rounded compression path as EPUB pages. A raw
  // multiply truncates fractional compression and can make RSS pagination
  // drift by one row per screen.
  lineHeight = std::max(1, renderer.getLineHeight(cachedFontId));
  linesPerPage = std::max(1, viewportHeight / lineHeight);

  titleParagraph = buildTitleParagraph();

  // Prime the SD card font's advance table for every style actually used in
  // this article (title included — it's always bold) before the wrap loop
  // below measures word by word. Without this, each getTextAdvanceX() call
  // triggers an on-demand glyph load through the 8-slot overflow ring
  // buffer — thrashes badly on anything with more unique glyphs than that,
  // same reasoning as TxtReaderActivity::loadPageAtOffset().
  if (renderer.isSdCardFont(cachedFontId)) {
    uint8_t styleMask = 0;
    std::string textChunk;
    textChunk.reserve(2048);
    auto collect = [&](const RichParagraph& paragraph) {
      for (const auto& word : paragraph.words) {
        if (textChunk.size() + word.text.size() + 1 > 2048 && !textChunk.empty()) {
          renderer.ensureSdCardFontReady(cachedFontId, textChunk.c_str(), styleMask);
          textChunk.clear();
        }
        textChunk += word.text;
        textChunk += ' ';
        styleMask |= static_cast<uint8_t>(1u << (static_cast<uint8_t>(word.style) & 0x03));
      }
    };
    collect(titleParagraph);
    for (const auto& paragraph : item.body) collect(paragraph);
    if (styleMask == 0) styleMask = 0x01;
    if (!textChunk.empty()) renderer.ensureSdCardFontReady(cachedFontId, textChunk.c_str(), styleMask);
  }

  allLines.clear();
  if (SETTINGS.rssShowArticleTitle && !titleParagraph.words.empty()) {
    wrapParagraph(titleParagraph);
    if (SETTINGS.rssShowArticleSeparator) {
      WrappedLine separator;
      separator.isSeparator = true;
      allLines.push_back(std::move(separator));
    }
    allLines.push_back(WrappedLine{});  // spacer between the title heading and the body
  }
  for (const auto& paragraph : item.body) {
    wrapParagraph(paragraph);
    // RSS HTML commonly already supplies an empty paragraph between blocks.
    // Keep Normal spacing compact and add only one controlled blank line for
    // the explicit Wide setting, without duplicating an existing blank line.
    if (SETTINGS.rssParagraphSpacing == CrossPointSettings::WIDE && !allLines.empty() &&
        !allLines.back().words.empty()) {
      allLines.push_back(WrappedLine{});
    }
  }
  if (!allLines.empty() && allLines.back().words.empty() && !allLines.back().isSeparator)
    allLines.pop_back();  // no trailing paragraph spacer after the last paragraph
  if (allLines.empty()) allLines.push_back(WrappedLine{});

  totalPages = std::max(1, static_cast<int>((allLines.size() + linesPerPage - 1) / linesPerPage));
}

void RssArticleActivity::prewarmVisiblePage(const int startLine, const int endLine) {
  auto* fcm = renderer.getFontCacheManager();
  if (!fcm || !renderer.isSdCardFont(cachedFontId)) return;

  std::string chunk;
  uint8_t styleMask = 0;
  for (int i = startLine; i < endLine; ++i) {
    const auto& line = allLines[i];
    if (line.isSeparator || line.words.empty()) continue;
    for (const auto& lw : line.words) {
      const auto& word = *lw.word;
      appendPrewarmText(chunk, word.text);
      styleMask |= static_cast<uint8_t>(1u << (static_cast<uint8_t>(word.style) & 0x03));
    }
  }
  if (styleMask == 0) styleMask = 0x01;
  if (!chunk.empty()) {
    fcm->prewarmCache(cachedFontId, chunk.c_str(), styleMask);
  }
}

void RssArticleActivity::loop() {
  if (cacheError) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) finish();
    return;
  }

  if (SETTINGS.rssStarAction == CrossPointSettings::RSS_STAR_LONG_OPEN) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      longStarHandled = false;
    } else if (!longStarHandled && mappedInput.getHeldTime() >= ReaderUtils::BOOKMARK_HOLD_MS) {
      toggleStar();
      longStarHandled = true;
      return;
    }
  }

  // The default RSS shortcut is a long Confirm: it toggles the local
  // read-later queue and never touches FreshRSS. The legacy long-open star
  // shortcut retains precedence when explicitly selected in RSS settings.
  if (SETTINGS.rssStarAction != CrossPointSettings::RSS_STAR_LONG_OPEN) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      longQueueHandled = false;
    } else if (!longQueueHandled && mappedInput.getHeldTime() >= ReaderUtils::BOOKMARK_HOLD_MS) {
      toggleQueue();
      longQueueHandled = true;
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  const auto touch = ReaderUtils::detectTouchPageTurn(renderer, mappedInput);
  auto [prevTriggered, nextTriggered, fromSideButton, fromTilt] = ReaderUtils::detectPageTurn(mappedInput);
  (void)fromSideButton;
  (void)fromTilt;
  prevTriggered = prevTriggered || touch.prev;
  nextTriggered = nextTriggered || touch.next;
  if (!prevTriggered && !nextTriggered) return;

  if (prevTriggered && currentPage > 0) {
    currentPage--;
    requestUpdate();
  } else if (nextTriggered) {
    if (currentPage < totalPages - 1) {
      currentPage++;
      requestUpdate();
    } else {
      if (SETTINGS.rssMarkReadTiming == CrossPointSettings::RSS_MARK_READ_ON_LAST_PAGE &&
          !RSS_ITEM_STATE.isRead(feed.url, item.key)) {
        RSS_ITEM_STATE.markRead(feed.url, item.key);
      }
      if (SETTINGS.rssArticleEndAction == CrossPointSettings::RSS_OPEN_NEXT_UNREAD) {
        setResult(RssArticleResult{true});
      }
      finish();  // last page — next turn exits back to the item list
    }
  }
}

void RssArticleActivity::render(RenderLock&&) {
  renderer.clearScreen();

  if (cacheError) {
    renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2 - 15, tr(STR_ERROR_MSG));
    renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2 + 15, tr(STR_FETCH_FEED_FAILED));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  const int startLine = currentPage * linesPerPage;
  const int endLine = std::min(static_cast<int>(allLines.size()), startLine + linesPerPage);

  const auto drawArticlePage = [&](const int yStart) {
    int y = yStart;
    for (int i = startLine; i < endLine; ++i) {
      const auto& line = allLines[i];
      if (line.isSeparator) {
        const int separatorY = y + lineHeight / 2;
        renderer.drawLine(marginLeft, separatorY, marginLeft + viewportWidth - 1, separatorY);
        y += lineHeight;
        continue;
      }
      if (!line.words.empty()) {
        int x = marginLeft + line.indent;
        int extraPerGap = 0;
        if (line.align == TextAlign::CENTER) {
          x = marginLeft + line.indent + std::max(0, (line.availableWidth - line.naturalWidth) / 2);
        } else if (line.align == TextAlign::RIGHT) {
          x = marginLeft + line.indent + std::max(0, line.availableWidth - line.naturalWidth);
        } else if (line.align == TextAlign::JUSTIFY && !line.isParagraphEnd && line.gapCount > 0) {
          const int spare = line.availableWidth - line.naturalWidth;
          extraPerGap = ReaderLayout::justificationExtra(spare, line.gapCount);
        }

        for (size_t w = 0; w < line.words.size(); ++w) {
          const auto& word = *line.words[w].word;
          if (w > 0) {
            x += gapAdvance(*line.words[w - 1].word, word);
            if (!word.continuesPrevious) x += extraPerGap;
          }
          renderer.drawText(cachedFontId, x, y, word.text.c_str(), true, word.style);
          x += line.words[w].width;
        }
      }
      y += lineHeight;
    }
  };

  auto* fcm = renderer.getFontCacheManager();
  const bool prewarmSdGlyphs = fcm && renderer.isSdCardFont(cachedFontId);
  if (prewarmSdGlyphs) {
    // SD fonts keep glyph metadata on disk until prewarm() loads the visible subset
    // into the mini cache. drawText() resolves missing glyphs via findGlyph(), not
    // the on-demand miss handler, so skipping this pass leaves shaped Gujarati PUA
    // text as U+FFFD diamonds while the advance table still measures correctly.
    auto scope = fcm->createPrewarmScope();
    drawArticlePage(marginTop);
    scope.endScanAndPrewarm();
    drawArticlePage(marginTop);
  } else {
    drawArticlePage(marginTop);
  }

  std::string statusTitle = item.title;
  if (RSS_ITEM_STATE.isStarred(feed.url, item.key)) statusTitle += " *";
  if (RSS_ITEM_STATE.isQueued(feed.url, item.key)) statusTitle += " Q";
  // The prewarm scope must stay alive until after GUI.drawStatusBar() below
  // draws the real, positioned title: its destructor clears the SD font's
  // prewarmed glyph cache (see FontCacheManager::PrewarmScope), so ending the
  // scope here would discard the batch-loaded glyphs before drawStatusBar
  // ever draws them, falling back to the small on-demand overflow cache and
  // producing tofu for the shaped Gujarati title.
  std::optional<FontCacheManager::PrewarmScope> statusScope;
  if (fcm && GujaratiIntegration::containsGujarati(statusTitle)) {
    std::string shapedStatus = statusTitle;
    GujaratiIntegration::shapeUiString(shapedStatus);
    // Status chrome uses SMALL_FONT_ID; Gujarati routes to the size-matched SD
    // fallback. A dedicated scan pass records the resolved SD font id for prewarm.
    statusScope.emplace(fcm->createPrewarmScope());
    renderer.drawText(SMALL_FONT_ID, 0, 0, shapedStatus.c_str(), true, EpdFontFamily::REGULAR);
    statusScope->endScanAndPrewarm();
  }
  GUI.drawStatusBar(renderer, totalPages > 0 ? static_cast<float>(currentPage + 1) / totalPages : 1.0f, currentPage + 1,
                    totalPages, statusTitle);

  ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);

  if (SETTINGS.textAntiAliasing && ReaderUtils::readerForegroundBlack()) {
    const int articleY = marginTop;
    ReaderUtils::renderAntiAliased(renderer, [&drawArticlePage, articleY]() { drawArticlePage(articleY); });
  }

  if (currentPage + 1 < totalPages) {
    prewarmVisiblePage((currentPage + 1) * linesPerPage,
                       std::min(static_cast<int>(allLines.size()), (currentPage + 2) * linesPerPage));
  }
}

void RssArticleActivity::activateRssFont() {
  savedFontFamily = SETTINGS.fontFamily;
  savedFontPointSize = SETTINGS.readerFontPointSize;
  strncpy(savedSdFontFamilyName, SETTINGS.sdFontFamilyName, sizeof(savedSdFontFamilyName) - 1);
  savedSdFontFamilyName[sizeof(savedSdFontFamilyName) - 1] = '\0';

  SETTINGS.fontFamily = SETTINGS.rssFontFamily;
  SETTINGS.readerFontPointSize = SETTINGS.rssFontPointSize;
  strncpy(SETTINGS.sdFontFamilyName, SETTINGS.rssSdFontFamilyName, sizeof(SETTINGS.sdFontFamilyName) - 1);
  SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
  {
    RenderLock lock(*this);
    sdFontSystem.ensureLoaded(renderer);
  }
  rssFontSessionActive = true;
}

void RssArticleActivity::restoreReaderFont() {
  if (!rssFontSessionActive) return;
  SETTINGS.fontFamily = savedFontFamily;
  SETTINGS.readerFontPointSize = savedFontPointSize;
  strncpy(SETTINGS.sdFontFamilyName, savedSdFontFamilyName, sizeof(SETTINGS.sdFontFamilyName) - 1);
  SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
  // ActivityManager holds RenderLock while calling onExit(). Taking it again
  // would deadlock when returning to the article list or entering sleep.
  sdFontSystem.ensureLoaded(renderer);
  rssFontSessionActive = false;
}

void RssArticleActivity::toggleStar() {
  RSS_ITEM_STATE.toggleStar(feed.url, item.key);
  RSS_ITEM_STATE.saveIfDirty();
  requestUpdate();
}

void RssArticleActivity::toggleQueue() {
  if (!RSS_ITEM_STATE.toggleQueued(feed.url, item.key)) return;
  RSS_ITEM_STATE.saveIfDirty();
  requestUpdate();
}
