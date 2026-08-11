#include "FreshRssApiClient.h"

#include "network/HttpDownloader.h"
#if defined(ARDUINO)
#include <Arduino.h>
#endif
#include <StreamingJsonParser.h>
#include <Logging.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits>
#include <new>

namespace {
constexpr size_t MAX_SUBSCRIPTIONS = 128;
constexpr size_t MAX_TAGS = 128;
// The client admits only MAX_ARTICLES into the SD snapshot, but the parser
// must be able to drain a server response that ignores the requested n= value
// without turning an intentionally ignored tail into a parse failure.
constexpr size_t MAX_PARSED_ARTICLES = 3000;
constexpr size_t MAX_ARTICLES = 1000;
constexpr size_t MAX_CATEGORIES_PER_ITEM = 32;
constexpr size_t MAX_ID_CHARS = 256;
constexpr size_t MAX_LABEL_CHARS = 256;
constexpr size_t MAX_ARTICLE_BODY_BYTES = 32 * 1024;

// Appends up to `max - target.size()` bytes of `value`, truncating the tail
// instead of rejecting the whole append when it would exceed the cap. A field
// that runs past this cosmetic size limit still ends up non-empty, so it
// can't trip a "required field is empty" check purely from being long — only
// a genuinely absent field (the server never sent the key at all) should fail
// record validation. Returns false, with `wasTruncated` set, when some input
// bytes were dropped.
bool appendBounded(std::string& target, const char* value, const size_t len, const size_t max, bool& wasTruncated) {
  if (target.size() >= max) {
    wasTruncated = true;
    return false;
  }
  const size_t available = max - target.size();
  const size_t take = std::min(len, available);
  target.append(value, take);
  if (take < len) {
    wasTruncated = true;
    return false;
  }
  return true;
}

// Convenience overload for the common case where the caller doesn't need to
// react to truncation of this particular field.
bool appendBounded(std::string& target, const char* value, const size_t len, const size_t max) {
  bool wasTruncated = false;
  return appendBounded(target, value, len, max, wasTruncated);
}

bool parseCrawlTimeMsec(const char* value, const size_t len, uint64_t& millisOut, std::string& dateOut) {
  char buffer[32] = {};
  if (len == 0 || len >= sizeof(buffer)) return false;
  memcpy(buffer, value, len);
  char* end = nullptr;
  errno = 0;
  const unsigned long long millis = strtoull(buffer, &end, 10);
  if (errno == ERANGE || end == buffer || *end != '\0' || millis > std::numeric_limits<uint64_t>::max()) return false;

  const time_t seconds = static_cast<time_t>(millis / 1000ULL);
  std::tm time = {};
#if defined(_WIN32)
  gmtime_s(&time, &seconds);
#else
  gmtime_r(&seconds, &time);
#endif
  char date[32] = {};
  if (strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%SZ", &time) == 0) return false;
  millisOut = static_cast<uint64_t>(millis);
  dateOut = date;
  return true;
}

std::string formEncode(const std::string& value) {
  static constexpr char HEX_DIGITS[] = "0123456789ABCDEF";
  std::string encoded;
  for (const unsigned char c : value) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      encoded.push_back(static_cast<char>(c));
    } else if (c == ' ') {
      encoded.push_back('+');
    } else {
      encoded.push_back('%');
      encoded.push_back(HEX_DIGITS[c >> 4]);
      encoded.push_back(HEX_DIGITS[c & 0x0f]);
    }
  }
  return encoded;
}

std::string normalizeApiUrl(std::string url) {
  while (!url.empty() && (url.back() == '/' || url.back() == ' ' || url.back() == '\t')) url.pop_back();
  size_t start = 0;
  while (start < url.size() && (url[start] == ' ' || url[start] == '\t')) ++start;
  if (start > 0) url.erase(0, start);
  if (url.empty() || (url.size() >= 11 && url.compare(url.size() - 11, 11, "greader.php") == 0)) return url;
  if (url.size() >= 4 && url.compare(url.size() - 4, 4, "/api") == 0) return url + "/greader.php";
  return url + "/api/greader.php";
}
}  // namespace

struct FreshRssJsonParser::ParserStorage {
  StreamingJsonParser parser;
  explicit ParserStorage(const JsonCallbacks& callbacks) : parser(callbacks) {}
};

FreshRssJsonParser::FreshRssJsonParser(const Document document, ArticleSink sink)
    : document(document), sink(std::move(sink)) {
  JsonCallbacks callbacks{};
  callbacks.ctx = this;
  callbacks.onKey = &FreshRssJsonParser::onKey;
  callbacks.onStringChunk = &FreshRssJsonParser::onStringChunk;
  callbacks.onNumber = &FreshRssJsonParser::onNumber;
  callbacks.onObjectStart = &FreshRssJsonParser::onObjectStart;
  callbacks.onObjectEnd = &FreshRssJsonParser::onObjectEnd;
  callbacks.onArrayStart = &FreshRssJsonParser::onArrayStart;
  callbacks.onArrayEnd = &FreshRssJsonParser::onArrayEnd;
  storage = new (std::nothrow) ParserStorage(callbacks);
  if (!storage) valid = false;
}

FreshRssJsonParser::~FreshRssJsonParser() {
  delete storage;
  storage = nullptr;
}

void FreshRssJsonParser::feed(const uint8_t* data, const size_t len) {
  if (!valid || !storage) return;
  storage->parser.feed(reinterpret_cast<const char*>(data), len);
  parserError = storage->parser.hasError();
}

bool FreshRssJsonParser::finish() {
  if (storage) {
    parserError = storage->parser.hasError();
    delete storage;
    storage = nullptr;
  }
  if (nesting != 0 || !arrayKeys.empty() || subscriptionLevel != 0 || tagLevel != 0 || articleLevel != 0) valid = false;
  return ok();
}

void FreshRssJsonParser::onKey(void* ctx, const char* value, const size_t len) {
  static_cast<FreshRssJsonParser*>(ctx)->key(value, len);
}
void FreshRssJsonParser::onStringChunk(void* ctx, const char* value, const size_t len, const bool final) {
  static_cast<FreshRssJsonParser*>(ctx)->stringChunk(value, len, final);
}
void FreshRssJsonParser::onNumber(void* ctx, const char* value, const size_t len) {
  static_cast<FreshRssJsonParser*>(ctx)->number(value, len);
}
void FreshRssJsonParser::onObjectStart(void* ctx) { static_cast<FreshRssJsonParser*>(ctx)->objectStart(); }
void FreshRssJsonParser::onObjectEnd(void* ctx) { static_cast<FreshRssJsonParser*>(ctx)->objectEnd(); }
void FreshRssJsonParser::onArrayStart(void* ctx) { static_cast<FreshRssJsonParser*>(ctx)->arrayStart(); }
void FreshRssJsonParser::onArrayEnd(void* ctx) { static_cast<FreshRssJsonParser*>(ctx)->arrayEnd(); }

void FreshRssJsonParser::key(const char* value, const size_t len) {
  if (len > MAX_LABEL_CHARS) {
    fail();
    return;
  }
  currentKey.assign(value, len);
  contentValue = false;
  summaryValue = false;
  if (document == Document::Articles && articleLevel != 0) {
    if (currentKey == "content") {
      // Google Reader responses commonly wrap these values in an object, but
      // FreshRSS installations may also return a direct string.
      summaryValue = summaryLevel != 0 && contentLevel == 0;
      contentValue = !summaryValue;
    } else if (currentKey == "summary") {
      contentValue = contentLevel != 0 && summaryLevel == 0;
      summaryValue = !contentValue;
    }
  }
}

void FreshRssJsonParser::stringChunk(const char* value, const size_t len, const bool final) {
  if (!valid) return;
  if (currentKey == "continuation" && nesting <= 1) {
    if (nextContinuation.size() + len > MAX_ID_CHARS) {
      fail();
      return;
    }
    nextContinuation.append(value, len);
    return;
  }

  if (document == Document::Subscriptions && subscriptionLevel != 0) {
    if (currentKey == "id" && !categoryArray) appendBounded(subscription.id, value, len, MAX_ID_CHARS);
    else if (currentKey == "title" && !categoryArray) appendBounded(subscription.title, value, len, MAX_LABEL_CHARS);
    else if (currentKey == "htmlUrl" && !categoryArray) appendBounded(subscription.htmlUrl, value, len, MAX_ID_CHARS);
    else if (currentKey == "id" && categoryArray && subscription.categoryIds.size() < MAX_CATEGORIES_PER_ITEM) {
      std::string category(value, len);
      if (category.size() <= MAX_ID_CHARS) subscription.categoryIds.push_back(std::move(category));
    }
    // A missing id (server never sent the key at all) makes the subscription
    // unusable as a cache key; an id merely longer than MAX_ID_CHARS is kept
    // truncated by appendBounded above, so it never reaches this check empty.
    if (final && subscription.id.empty()) fail();
    return;
  }

  if (document == Document::Tags && tagLevel != 0) {
    if (currentKey == "id") appendBounded(tag.id, value, len, MAX_ID_CHARS);
    else if (currentKey == "label") appendBounded(tag.label, value, len, MAX_LABEL_CHARS);
    if (final && tag.id.empty()) fail();
    return;
  }

  if (document != Document::Articles || articleLevel == 0) return;
  if (contentValue || summaryValue) {
    consumeArticleString(value, len, final);
    return;
  }
  if (currentKey == "id" && !categoryArray && originLevel == 0 && canonicalLevel == 0 && alternateLevel == 0)
    appendBounded(article.id, value, len, MAX_ID_CHARS);
  else if (currentKey == "title" && originLevel == 0) appendBounded(article.title, value, len, MAX_LABEL_CHARS);
  else if ((currentKey == "name" || currentKey == "title") && originLevel != 0)
    appendBounded(article.origin, value, len, MAX_LABEL_CHARS);
  else if (currentKey == "htmlUrl" && originLevel != 0) appendBounded(article.originUrl, value, len, MAX_ID_CHARS);
  else if (currentKey == "streamId" && originLevel != 0) appendBounded(article.streamId, value, len, MAX_ID_CHARS);
  else if (currentKey == "href" && canonicalLevel != 0) appendBounded(canonicalLink, value, len, MAX_ID_CHARS);
  else if (currentKey == "href" && alternateLevel != 0) appendBounded(alternateLink, value, len, MAX_ID_CHARS);
  else if (currentKey == "href" && originLevel == 0) appendBounded(alternateLink, value, len, MAX_ID_CHARS);
  else if (currentKey == "id" && categoryArray && article.categoryIds.size() < MAX_CATEGORIES_PER_ITEM) {
    // A single over-long category id is dropped, not treated as a whole-page
    // parse failure — the article itself still has a usable id.
    std::string category(value, std::min(len, MAX_ID_CHARS));
    article.categoryIds.push_back(std::move(category));
  }
  else if (currentKey == "author" && originLevel == 0) appendBounded(article.author, value, len, MAX_LABEL_CHARS);
  else if (currentKey == "name" && originLevel == 0 && article.author.empty())
    appendBounded(article.author, value, len, MAX_LABEL_CHARS);
  else if (currentKey == "crawlTimeMsec" && originLevel == 0 && canonicalLevel == 0 && alternateLevel == 0) {
    if (!appendBounded(crawlTimeText, value, len, 31)) return;
    if (final && !parseCrawlTimeMsec(crawlTimeText.data(), crawlTimeText.size(), article.modifiedMsec, article.date))
      LOG_ERR("FRSS", "article %s has an unparseable crawlTimeMsec", article.id.c_str());
  }
  // A missing id (server never sent the key at all) makes the article
  // unusable as a cache key; an id merely longer than MAX_ID_CHARS is kept
  // truncated by appendBounded above, so it never reaches this check empty.
  if (final && currentKey == "id" && article.id.empty()) fail();
}

void FreshRssJsonParser::consumeArticleString(const char* value, const size_t len, const bool /*final*/) {
  const bool useContent = contentValue;
  if (useContent && !contentSelected) {
    // A FreshRSS response can place summary before content. The body ceiling
    // applies to the selected body, not to the discarded summary, so a large
    // summary must not prevent the full content from being cached.
    contentSelected = true;
    article.summaryHtml.clear();
    article.contentHtml.clear();
    bodyBytes = 0;
    article.bodyTruncated = false;
  }
  if (!useContent && contentSelected) return;  // Full content wins over summary.
  std::string& target = useContent ? article.contentHtml : article.summaryHtml;
  if (bodyBytes >= MAX_ARTICLE_BODY_BYTES) {
    article.bodyTruncated = true;
    return;
  }
  const size_t take = std::min(len, MAX_ARTICLE_BODY_BYTES - bodyBytes);
  target.append(value, take);
  bodyBytes += take;
  if (take != len) article.bodyTruncated = true;
}

void FreshRssJsonParser::number(const char* value, const size_t len) {
  if (document == Document::Articles && articleLevel != 0 && currentKey == "crawlTimeMsec") {
    if (!parseCrawlTimeMsec(value, len, article.modifiedMsec, article.date)) fail();
  }
}

void FreshRssJsonParser::objectStart() {
  ++nesting;
  ++objectLevel;
  if (document == Document::Subscriptions && (currentKey == "subscriptions" || (subscriptionsArray && subscriptionLevel == 0))) {
    subscription = {};
    subscriptionLevel = objectLevel;
  } else if (document == Document::Tags && (currentKey == "tags" || (tagsArray && tagLevel == 0))) {
    tag = {};
    tagLevel = objectLevel;
  } else if (document == Document::Articles && (currentKey == "items" || (itemsArray && articleLevel == 0))) {
    article = {};
    canonicalLink.clear();
    alternateLink.clear();
    crawlTimeText.clear();
    bodyBytes = 0;
    contentSelected = false;
    articleLevel = objectLevel;
  } else if (document == Document::Articles && articleLevel != 0) {
    if (currentKey == "content") contentLevel = objectLevel;
    if (currentKey == "summary") summaryLevel = objectLevel;
    if (currentKey == "origin") originLevel = objectLevel;
    if (currentKey == "canonical") canonicalLevel = objectLevel;
    if (currentKey == "alternate") alternateLevel = objectLevel;
  }
}

void FreshRssJsonParser::objectEnd() {
  if (document == Document::Subscriptions && subscriptionLevel == objectLevel) finishSubscription();
  if (document == Document::Tags && tagLevel == objectLevel) finishTag();
  if (document == Document::Articles && articleLevel == objectLevel) finishArticle();
  if (contentLevel == objectLevel) contentLevel = 0;
  if (summaryLevel == objectLevel) summaryLevel = 0;
  if (originLevel == objectLevel) originLevel = 0;
  if (canonicalLevel == objectLevel) canonicalLevel = 0;
  if (alternateLevel == objectLevel) alternateLevel = 0;
  if (articleLevel == objectLevel) articleLevel = 0;
  if (subscriptionLevel == objectLevel) subscriptionLevel = 0;
  if (tagLevel == objectLevel) tagLevel = 0;
  if (objectLevel > 0) --objectLevel;
  if (nesting > 0) --nesting;
}

void FreshRssJsonParser::arrayStart() {
  ++nesting;
  arrayKeys.push_back(currentKey);
  if (currentKey == "categories") categoryArray = true;
  if (currentKey == "subscriptions") subscriptionsArray = true;
  if (currentKey == "tags") tagsArray = true;
  if (currentKey == "items") itemsArray = true;
}

void FreshRssJsonParser::arrayEnd() {
  const std::string closed = arrayKeys.empty() ? std::string() : std::move(arrayKeys.back());
  if (!arrayKeys.empty()) arrayKeys.pop_back();
  if (closed == "categories") categoryArray = false;
  if (closed == "subscriptions") subscriptionsArray = false;
  if (closed == "tags") tagsArray = false;
  if (closed == "items") itemsArray = false;
  if (nesting > 0) --nesting;
}

void FreshRssJsonParser::finishSubscription() {
  if (subscription.id.empty() || parsedMetadata.subscriptions.size() >= MAX_SUBSCRIPTIONS) {
    fail();
    return;
  }
  parsedMetadata.subscriptions.push_back(std::move(subscription));
}
void FreshRssJsonParser::finishTag() {
  if (tag.id.empty() || parsedMetadata.tags.size() >= MAX_TAGS) {
    fail();
    return;
  }
  parsedMetadata.tags.push_back(std::move(tag));
}
void FreshRssJsonParser::finishArticle() {
  if (article.id.empty() || parsedArticleCount == MAX_PARSED_ARTICLES || !sink) {
    fail();
    return;
  }
  article.link = !canonicalLink.empty() ? canonicalLink : alternateLink;
  if (article.contentHtml.empty()) article.contentHtml = std::move(article.summaryHtml);
  article.title = freshRssDisplayTitle(article.title, article.link);
  if (!sink(std::move(article))) {
    sinkError = true;
    fail();
    return;
  }
  ++parsedArticleCount;
}

std::string FreshRssApiClient::endpoint(const std::string& path) const {
  return normalizeApiUrl(account.apiUrl) + "/" + path;
}

bool FreshRssApiClient::login(std::string& auth, std::string& error, const CancelCallback& shouldCancel) {
  std::string response;
  const std::string form = "Email=" + formEncode(account.username) + "&Passwd=" + formEncode(account.password);
  if (!HttpDownloader::postForm(endpoint("accounts/ClientLogin"), form,
                                 [&response](const uint8_t* data, size_t len) {
                                   if (response.size() + len > 2048) return false;
                                   response.append(reinterpret_cast<const char*>(data), len);
                                   return true;
                                 },
                                 {}, shouldCancel)) {
    error = "ClientLogin failed";
    LOG_ERR("FRSS", "login: ClientLogin request failed");
    return false;
  }
  const std::string marker = "Auth=";
  const size_t start = response.find(marker);
  if (start == std::string::npos) {
    error = "FreshRSS returned no Auth token";
    LOG_ERR("FRSS", "login: ClientLogin response had no Auth= token (likely bad credentials)");
    return false;
  }
  const size_t valueStart = start + marker.size();
  const size_t end = response.find_first_of("\r\n", valueStart);
  auth = response.substr(valueStart, end == std::string::npos ? std::string::npos : end - valueStart);
  if (auth.empty() || auth.size() > 1024) {
    error = "FreshRSS Auth token is invalid";
    LOG_ERR("FRSS", "login: Auth token failed validation (len=%zu)", auth.size());
    return false;
  }
  return true;
}

bool FreshRssApiClient::getJson(const std::string& path, const FreshRssJsonParser::Document /*document*/,
                                FreshRssJsonParser& parser, const std::string& auth, std::string& error,
                                bool* requestCompleted, const CancelCallback& shouldCancel) {
  const std::vector<HttpDownloader::Header> headers = {{"Authorization", "GoogleLogin auth=" + auth},
                                                        {"Accept", "application/json"}};
  const bool fetched = HttpDownloader::fetchUrlWithHeaders(
      endpoint(path), [&parser](const uint8_t* data, size_t len) {
        parser.feed(data, len);
        return parser.ok();
      }, headers, shouldCancel);
  if (requestCompleted) *requestCompleted = fetched;
  if (!fetched || !parser.finish()) {
    error = fetched ? "FreshRSS response could not be parsed" : "FreshRSS request failed";
    LOG_ERR("FRSS", "getJson failed for %s: %s", path.c_str(), error.c_str());
    return false;
  }
  return true;
}

bool FreshRssApiClient::refresh(const size_t articleLimit, FreshRssMetadata& metadata, const ArticleSink& sink,
                                std::string& error) {
  if (account.apiUrl.empty() || account.username.empty() || account.password.empty()) {
    error = "FreshRSS account is not configured";
    return false;
  }
  std::string auth;
  if (!fetchMetadata(metadata, auth, error)) return false;
  return fetchArticles(auth, articleLimit, sink, error);
}

bool FreshRssApiClient::fetchMetadata(FreshRssMetadata& metadata, std::string& auth, std::string& error,
                                      const CancelCallback& shouldCancel) {
  if (account.apiUrl.empty() || account.username.empty() || account.password.empty()) {
    error = "FreshRSS account is not configured";
    return false;
  }
  if (!login(auth, error, shouldCancel)) return false;
  FreshRssJsonParser subscriptions(FreshRssJsonParser::Document::Subscriptions);
  if (!getJson("reader/api/0/subscription/list?output=json", FreshRssJsonParser::Document::Subscriptions, subscriptions,
               auth, error, nullptr, shouldCancel))
    return false;
  FreshRssJsonParser tags(FreshRssJsonParser::Document::Tags);
  if (!getJson("reader/api/0/tag/list?output=json", FreshRssJsonParser::Document::Tags, tags, auth, error, nullptr,
               shouldCancel))
    return false;
  metadata = subscriptions.metadata();
  metadata.tags = tags.metadata().tags;
  return true;
}

bool FreshRssApiClient::fetchArticles(const std::string& auth, const size_t articleLimit, const ArticleSink& sink,
                                     std::string& error, const ProgressCallback& progress,
                                     const FreshRssSyncCursor* cursor, bool* deltaUnsupported,
                                     const CancelCallback& shouldCancel) {
  if (auth.empty() || !sink) {
    error = "FreshRSS authentication is not available";
    return false;
  }
  if (deltaUnsupported) *deltaUnsupported = false;
  const size_t boundedLimit = articleLimit == 200 || articleLimit == 500 || articleLimit == MAX_ARTICLES ? articleLimit : 200;
  size_t received = 0;
  if (progress) progress(0, boundedLimit);
  std::string continuation;
  // FreshRSS installations can spend a long time building a large reading
  // list response.  The configured server reliably returns n=25 quickly,
  // while n=100 can exceed the device HTTP timeout before the first article
  // reaches the streaming parser.  Smaller pages also keep the response and
  // parser working set bounded; total transfer size is unchanged.
  const size_t requestPageSize = 25;
  do {
    const size_t remaining = boundedLimit - received;
    std::string path = "reader/api/0/stream/contents/reading-list?output=json&n=" +
                       std::to_string(std::min(remaining, requestPageSize));
    if (cursor && cursor->valid && cursor->modifiedMsec != 0) {
      // FreshRSS follows the Google Reader modified-since convention in
      // seconds even though the article field is named crawlTimeMsec.
      path += "&ot=" + std::to_string(cursor->modifiedMsec / 1000ULL);
    }
    if (!continuation.empty()) path += "&c=" + formEncode(continuation);
    FreshRssJsonParser articles(FreshRssJsonParser::Document::Articles,
                                [&sink, &received, boundedLimit, &progress](FreshRssArticle&& item) {
      // A server may ignore n= and return a larger page. Stop admitting
      // records at the device limit while still draining/parsing that HTTP
      // response cleanly so it is not mistaken for a sink/SD failure.
      if (received >= boundedLimit) return true;
      if (!sink(std::move(item))) return false;
      ++received;
      if (progress) progress(received, boundedLimit);
      return true;
    });
    bool requestCompleted = false;
    if (!getJson(path, FreshRssJsonParser::Document::Articles, articles, auth, error, &requestCompleted,
                shouldCancel)) {
      // Only treat this as "the server ignored ot=" when the HTTP request
      // itself completed but the response then failed to parse or validate.
      // A transport-level failure (DNS, timeout, TLS, connection reset) says
      // nothing about delta support, and must not trigger a full re-download
      // stacked on top of a connection that just dropped.
      if (deltaUnsupported && cursor && cursor->valid && requestCompleted && !articles.sinkFailed())
        *deltaUnsupported = true;
      return false;
    }
    continuation = articles.continuation();
  } while (received < boundedLimit && !continuation.empty());
  return true;
}
