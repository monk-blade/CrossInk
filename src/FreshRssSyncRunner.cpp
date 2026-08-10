#include "FreshRssSyncRunner.h"

#include <GujaratiIntegration.h>
#include <HtmlRichText.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <vector>

#include "CrossPointSettings.h"
#include "FreshRssAccountStore.h"
#include "FreshRssApiClient.h"
#include "FreshRssCache.h"
#include "FreshRssTypes.h"
#include "RssItemStateStore.h"

namespace {
constexpr size_t MAX_CACHED_ARTICLE_CHARS = 32 * 1024;
}  // namespace

bool runFreshRssSync(FreshRssSyncHost& host, std::string& error) {
  host.progress.received.store(0);
  host.progress.limit.store(SETTINGS.freshRssArticleLimit);
  host.progress.processingArticle.store(false);

  if (host.setStatus) host.setStatus("Preparing FreshRSS sync");
  if (host.paintProgress) host.paintProgress(true);

  FreshRssMetadata metadata;
  std::string auth;
  FreshRssApiClient client(FRESHRSS_ACCOUNT.getAccount());
  if (host.setStatus) host.setStatus("Loading subscriptions and categories");
  if (host.paintProgress) host.paintProgress(true);
  if (!client.fetchMetadata(metadata, auth, error)) {
    if (error.empty()) error = tr(STR_FETCH_FEED_FAILED);
    return false;
  }

  FreshRssSyncCursor previousCursor;
  const FreshRssAccount account = FRESHRSS_ACCOUNT.getAccount();
  const uint32_t accountIdentity = freshRssAccountIdentity(account);
  const bool hasCursor = FreshRssCache::loadSyncCursor(previousCursor);
  const bool canDelta = hasCursor && previousCursor.accountIdentity == accountIdentity &&
                        previousCursor.articleLimit == SETTINGS.freshRssArticleLimit;

  auto performSnapshot = [&](const bool delta, FreshRssSyncCursor& nextCursor, bool& deltaUnsupported,
                             bool& deltaInconsistent) {
    FreshRssCache::WriteSession writer;
    if (!writer.begin(metadata, SETTINGS.freshRssArticleLimit, nextCursor)) return false;
    std::vector<uint32_t> changedKeys;
    changedKeys.reserve(SETTINGS.freshRssArticleLimit);
    const FreshRssSyncCursor* cursor = delta ? &previousCursor : nullptr;
    if (host.setStatus) host.setStatus(delta ? "Incremental FreshRSS sync" : "Downloading and caching articles");
    if (host.paintProgress) host.paintProgress(true);
    const bool fetched = client.fetchArticles(
        auth, SETTINGS.freshRssArticleLimit,
        [&host, &writer, &changedKeys, &nextCursor, delta, &deltaInconsistent](FreshRssArticle&& parsed) {
          if (delta && (parsed.modifiedMsec == 0 || parsed.modifiedMsec <= nextCursor.modifiedMsec)) {
            deltaInconsistent = true;
          }
          host.progress.processingArticle.store(true);
          if (host.paintProgress) host.paintProgress(host.progress.received.load() == 0);
          FreshRssCachedArticle item;
          item.id = parsed.id;
          item.key = RssItemStateStore::itemKey("freshrss:" + parsed.id, parsed.link, parsed.title);
          item.modifiedMsec = parsed.modifiedMsec;
          item.title = std::move(parsed.title);
          item.author = std::move(parsed.author);
          item.origin = std::move(parsed.origin);
          item.originUrl = std::move(parsed.originUrl);
          item.streamId = std::move(parsed.streamId);
          item.subscriptionId = item.streamId;
          item.link = std::move(parsed.link);
          item.date = std::move(parsed.date);
          item.categoryIds = std::move(parsed.categoryIds);
          const std::string& html = !parsed.contentHtml.empty() ? parsed.contentHtml : parsed.summaryHtml;
          item.body = HtmlRichText::convert(html, MAX_CACHED_ARTICLE_CHARS,
                                            [&host](const size_t) {
                                              if (host.paintProgress) host.paintProgress(false);
                                            });
          std::string().swap(parsed.contentHtml);
          std::string().swap(parsed.summaryHtml);
          item.bodyTruncated = parsed.bodyTruncated;
          GujaratiIntegration::shapeLongUiString(item.title);
          GujaratiIntegration::shapeLongUiString(item.author);
          GujaratiIntegration::shapeLongUiString(item.origin);
          size_t shapedWordCount = 0;
          for (auto& paragraph : item.body) {
            for (auto& word : paragraph.words) {
              GujaratiIntegration::shapeSanitizedWord(word.text);
              if ((++shapedWordCount & 31u) == 0 && host.paintProgress) host.paintProgress(false);
            }
          }
          const bool appended = writer.append(item);
          if (appended) {
            changedKeys.push_back(item.key);
            nextCursor.modifiedMsec = std::max(nextCursor.modifiedMsec, item.modifiedMsec);
          }
          host.progress.processingArticle.store(false);
          return appended;
        },
        error,
        [&host](const size_t received, const size_t limit) {
          host.progress.received.store(received);
          host.progress.limit.store(limit);
          if (host.paintProgress) host.paintProgress(received == 0 || received == limit);
        },
        cursor, &deltaUnsupported);
    if (!fetched || deltaInconsistent) {
      writer.abort();
      return false;
    }
    if (delta && !writer.copyUnchangedFromCurrent(changedKeys)) {
      writer.abort();
      return false;
    }
    if (host.setStatus) host.setStatus("Committing article cache");
    if (host.paintProgress) host.paintProgress(true);
    writer.setSyncCursor(nextCursor);
    return writer.commit();
  };

  FreshRssSyncCursor nextCursor;
  nextCursor.accountIdentity = accountIdentity;
  nextCursor.articleLimit = SETTINGS.freshRssArticleLimit;
  nextCursor.generation = hasCursor ? previousCursor.generation + 1 : 1;
  nextCursor.modifiedMsec = canDelta ? previousCursor.modifiedMsec : 0;
  bool deltaUnsupported = false;
  bool deltaInconsistent = false;
  bool committed = false;
  if (canDelta) {
    committed = performSnapshot(true, nextCursor, deltaUnsupported, deltaInconsistent);
    if (!committed && (deltaUnsupported || deltaInconsistent)) {
      if (host.setStatus) host.setStatus("Rebuilding FreshRSS cache");
      if (host.paintProgress) host.paintProgress(true);
      nextCursor.modifiedMsec = 0;
      deltaUnsupported = false;
      deltaInconsistent = false;
      committed = performSnapshot(false, nextCursor, deltaUnsupported, deltaInconsistent);
    }
  } else {
    committed = performSnapshot(false, nextCursor, deltaUnsupported, deltaInconsistent);
  }
  if (!committed) {
    if (error.empty()) error = tr(STR_FETCH_FEED_FAILED);
    return false;
  }
  if (!FreshRssCache::exists()) {
    error = tr(STR_PARSE_FEED_FAILED);
    return false;
  }
  return true;
}
