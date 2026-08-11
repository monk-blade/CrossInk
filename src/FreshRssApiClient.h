#pragma once

#include <cstddef>

#include <functional>
#include <string>
#include <vector>

#include "FreshRssTypes.h"

/** Endpoint-specific SAX parser. It is also host-testable without WiFi. */
class FreshRssJsonParser {
 public:
  enum class Document { Subscriptions, Tags, Articles };

  using ArticleSink = std::function<bool(FreshRssArticle&&)>;

  explicit FreshRssJsonParser(Document document, ArticleSink sink = nullptr);
  ~FreshRssJsonParser();

  void feed(const uint8_t* data, size_t len);
  bool finish();
  bool ok() const { return valid && !parserError; }
  bool sinkFailed() const { return sinkError; }
  const std::string& continuation() const { return nextContinuation; }
  const FreshRssMetadata& metadata() const { return parsedMetadata; }
  size_t articleCount() const { return parsedArticleCount; }

 private:
  static void onKey(void* ctx, const char* value, size_t len);
  static void onStringChunk(void* ctx, const char* value, size_t len, bool final);
  static void onNumber(void* ctx, const char* value, size_t len);
  static void onObjectStart(void* ctx);
  static void onObjectEnd(void* ctx);
  static void onArrayStart(void* ctx);
  static void onArrayEnd(void* ctx);

  void key(const char* value, size_t len);
  void stringChunk(const char* value, size_t len, bool final);
  void number(const char* value, size_t len);
  void objectStart();
  void objectEnd();
  void arrayStart();
  void arrayEnd();
  void consumeArticleString(const char* value, size_t len, bool final);
  void finishSubscription();
  void finishTag();
  void finishArticle();
  void fail() { valid = false; }

  Document document;
  ArticleSink sink;
  FreshRssMetadata parsedMetadata;
  std::string currentKey;
  std::string nextContinuation;
  std::string canonicalLink;
  std::string alternateLink;
  std::string crawlTimeText;
  FreshRssSubscription subscription;
  FreshRssTag tag;
  FreshRssArticle article;
  size_t nesting = 0;
  size_t objectLevel = 0;
  size_t subscriptionLevel = 0;
  size_t tagLevel = 0;
  size_t articleLevel = 0;
  size_t contentLevel = 0;
  size_t summaryLevel = 0;
  size_t originLevel = 0;
  size_t canonicalLevel = 0;
  size_t alternateLevel = 0;
  bool categoryArray = false;
  bool subscriptionsArray = false;
  bool tagsArray = false;
  bool itemsArray = false;
  std::vector<std::string> arrayKeys;
  bool contentValue = false;
  bool summaryValue = false;
  bool contentSelected = false;
  size_t bodyBytes = 0;
  size_t parsedArticleCount = 0;
  bool valid = true;
  bool parserError = false;
  bool sinkError = false;

  // Opaque to the header so the parser remains independent of the JSON
  // implementation details used by firmware and host tests.
  struct ParserStorage;
  ParserStorage* storage = nullptr;
};

class FreshRssApiClient {
 public:
  using ArticleSink = FreshRssJsonParser::ArticleSink;
  using ProgressCallback = std::function<void(size_t received, size_t limit)>;
  // Polled between HTTP reads during a request. Sync runs on the caller's own
  // task (see FreshRssSyncRunner), so this is how a long-running fetch can be
  // interrupted from the same blocking call stack that started it — e.g. a
  // lambda that re-polls raw Back-button state.
  using CancelCallback = std::function<bool()>;
  explicit FreshRssApiClient(FreshRssAccount account) : account(std::move(account)) {}

  bool fetchMetadata(FreshRssMetadata& metadata, std::string& auth, std::string& error,
                     const CancelCallback& shouldCancel = nullptr);
  bool fetchArticles(const std::string& auth, size_t articleLimit, const ArticleSink& sink, std::string& error,
                     const ProgressCallback& progress = {}, const FreshRssSyncCursor* cursor = nullptr,
                     bool* deltaUnsupported = nullptr, const CancelCallback& shouldCancel = nullptr);
  bool fetchArticles(const std::string& auth, size_t articleLimit, const ArticleSink& sink, std::string& error,
                     FreshRssSyncMode mode, const FreshRssSyncCursor* cursor, bool& deltaUnsupported,
                     const ProgressCallback& progress = {}) {
    return fetchArticles(auth, articleLimit, sink, error, progress, mode == FreshRssSyncMode::Delta ? cursor : nullptr,
                         &deltaUnsupported);
  }
  bool refresh(size_t articleLimit, FreshRssMetadata& metadata, const ArticleSink& sink, std::string& error);

 private:
  bool login(std::string& auth, std::string& error, const CancelCallback& shouldCancel);
  // `requestCompleted`, when provided, reports whether the HTTP request
  // itself completed (regardless of whether the response then parsed) — the
  // caller needs this to tell a transport failure (DNS/timeout/TLS/reset)
  // apart from a response that came back but failed to parse or validate.
  bool getJson(const std::string& path, FreshRssJsonParser::Document document, FreshRssJsonParser& parser,
              const std::string& auth, std::string& error, bool* requestCompleted = nullptr,
              const CancelCallback& shouldCancel = nullptr);
  std::string endpoint(const std::string& path) const;

  FreshRssAccount account;
};
