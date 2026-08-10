#pragma once

#include <EpdFontFamily.h>

#include <string>
#include <vector>

#include "RichText.h"
#include "RssFeedStore.h"
#include "RssItemCache.h"
#include "activities/Activity.h"

/**
 * Paginated rich-text reader for a single RSS/Atom item. Feed metadata is
 * loaded first and the selected item's body is loaded only from the SD cache.
 * Unlike the feed list, only one full article is resident while this activity
 * is open.
 *
 * Layout is word-level (not line-level): each word carries its own style
 * (regular/bold/italic) and each paragraph its own alignment, so a single
 * wrapped line can mix styles and the reader can do real justification —
 * unlike TxtReaderActivity's plain-text pagination, which has neither.
 */
class RssArticleActivity final : public Activity {
 public:
  explicit RssArticleActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, RssFeed feed, CachedRssItem item)
      : Activity("RssArticle", renderer, mappedInput), feed(std::move(feed)), item(std::move(item)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }

 private:
  struct LineWord {
    const StyledWord* word = nullptr;
    int width = 0;  // cached advance width at cachedFontId/style
  };

  struct WrappedLine {
    std::vector<LineWord> words;  // empty = a blank paragraph-spacer line
    TextAlign align = TextAlign::LEFT;
    bool isParagraphEnd = false;  // last line of its paragraph — justify never stretches it
    int indent = 0;               // left-edge offset (kept for alignment bookkeeping)
    int availableWidth = 0;       // effective column width for this line (viewportWidth - indent)
    int naturalWidth = 0;         // line width at normal (non-justified) spacing
    int gapCount = 0;             // stretchable word-gaps, for justify's per-gap extra
    bool isSeparator = false;     // visual rule between the article heading and body
  };

  RssFeed feed;
  CachedRssItem item;
  RichParagraph titleParagraph;

  std::vector<WrappedLine> allLines;  // wrapped visual lines, built once
  int linesPerPage = 0;
  int currentPage = 0;
  int totalPages = 1;
  int viewportWidth = 0;
  int marginTop = 0;
  int marginLeft = 0;
  int cachedFontId = 0;
  int spaceWidth = 0;
  int lineHeight = 0;
  int pagesUntilFullRefresh = 1;
  bool rssFontSessionActive = false;
  bool longStarHandled = false;
  bool longQueueHandled = false;
  bool cacheError = false;
  uint8_t savedFontFamily = 0;
  uint8_t savedFontPointSize = 0;
  char savedSdFontFamilyName[32] = "";

  void buildWrappedLines();
  void prewarmVisiblePage(int startLine, int endLine);
  void loadBody();
  void wrapParagraph(const RichParagraph& paragraph);
  void finalizeLine(std::vector<LineWord>&& words, TextAlign align, bool isParagraphEnd, int indent);
  int gapAdvance(const StyledWord& left, const StyledWord& right) const;
  TextAlign resolveAlign(TextAlign paragraphAlign) const;
  RichParagraph buildTitleParagraph() const;
  void activateRssFont();
  void restoreReaderFont();
  void toggleStar();
  void toggleQueue();
};
