#include "FreshRssSyncRunner.h"

#include <GujaratiIntegration.h>
#include <HtmlRichText.h>
#include <I18n.h>
#include <Logging.h>
#if defined(ARDUINO)
#include <MemoryBudget.h>
#include <Arduino.h>
#endif

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

  if (host.setStatus) host.setStatus(tr(STR_FRESHRSS_SYNC_PREPARING));
  if (host.paintProgress) host.paintProgress(true);

  FreshRssMetadata metadata;
  std::string auth;
  FreshRssApiClient client(FRESHRSS_ACCOUNT.getAccount());
  if (host.setStatus) host.setStatus(tr(STR_FRESHRSS_SYNC_LOADING_SUBSCRIPTIONS));
  if (host.paintProgress) host.paintProgress(true);
  if (!client.fetchMetadata(metadata, auth, error, host.shouldCancel)) {
    if (error.empty()) error = tr(STR_FETCH_FEED_FAILED);
    return false;
  }
  if (host.shouldCancel && host.shouldCancel()) {
    error = tr(STR_FETCH_FEED_FAILED);
    return false;
  }

#if defined(ARDUINO)
  delay(200);
#endif

  FreshRssSyncCursor previousCursor;
  const FreshRssAccount account = FRESHRSS_ACCOUNT.getAccount();
  const uint32_t accountIdentity = freshRssAccountIdentity(account);
  const bool hasCursor = FreshRssCache::loadSyncCursor(previousCursor);
  const bool canDelta = hasCursor && previousCursor.accountIdentity == accountIdentity &&
                        previousCursor.articleLimit == SETTINGS.freshRssArticleLimit
#if defined(ARDUINO)
                        && MemoryBudget::hasHeapForFreshRssTls(MemoryBudget::snapshot())
#endif
      ;

  auto performSnapshot = [&](const bool delta, FreshRssSyncCursor& nextCursor, bool& deltaUnsupported,
                             bool& deltaInconsistent) {
    FreshRssCache::WriteSession writer;
    if (!writer.begin(metadata, SETTINGS.freshRssArticleLimit, nextCursor)) return false;
#if defined(ARDUINO)
    // Writer keeps its own metadata copy for subscription/category indexing.
    // Drop the duplicate in the sync runner before article HTTPS pages.
    metadata.subscriptions.clear();
    metadata.subscriptions.shrink_to_fit();
    metadata.tags.clear();
    metadata.tags.shrink_to_fit();
    delay(100);
    LOG_INF("FRSS", "snapshot opened for articles (free=%u maxAlloc=%u)", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
    std::vector<uint32_t> changedKeys;
    changedKeys.reserve(SETTINGS.freshRssArticleLimit);
    const FreshRssSyncCursor* cursor = delta ? &previousCursor : nullptr;
    if (host.setStatus)
      host.setStatus(delta ? tr(STR_FRESHRSS_SYNC_INCREMENTAL) : tr(STR_FRESHRSS_SYNC_DOWNLOADING));
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
          // Body Gujarati shaping runs when the article is opened; doing it here
          // blocked the TLS read loop long enough to time out on ESP32-C3.
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
        cursor, &deltaUnsupported, host.shouldCancel);
    if (!fetched || deltaInconsistent) {
      LOG_ERR("FRSS", "sync aborted: fetch=%d deltaInconsistent=%d error=%s", fetched, deltaInconsistent,
              error.c_str());
      writer.abort();
      return false;
    }
    if (delta && !writer.copyUnchangedFromCurrent(changedKeys)) {
      LOG_ERR("FRSS", "sync aborted: failed to copy unchanged records forward");
      writer.abort();
      return false;
    }
    if (host.setStatus) host.setStatus(tr(STR_FRESHRSS_SYNC_COMMITTING));
    if (host.paintProgress) host.paintProgress(true);
    writer.setSyncCursor(nextCursor);
    if (!writer.commit()) {
      LOG_ERR("FRSS", "sync aborted: snapshot commit failed");
      return false;
    }
    return true;
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
      LOG_DBG("FRSS", "delta sync failed (unsupported=%d inconsistent=%d), falling back to full sync",
              deltaUnsupported, deltaInconsistent);
      if (host.setStatus) host.setStatus(tr(STR_FRESHRSS_SYNC_REBUILDING));
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
