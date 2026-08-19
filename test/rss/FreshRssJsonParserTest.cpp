#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "FreshRssApiClient.h"

TEST(FreshRssJsonParser, ParsesMetadataAndCategories) {
  const std::string json = R"({"subscriptions":[{"id":"feed/one","title":"ગુજરાતી સમાચાર","htmlUrl":"https://one","categories":[{"id":"user/-/label/Tech","label":"Tech"}]}]})";
  FreshRssJsonParser parser(FreshRssJsonParser::Document::Subscriptions);
  for (const char c : json) parser.feed(reinterpret_cast<const uint8_t*>(&c), 1);
  ASSERT_TRUE(parser.finish());
  ASSERT_EQ(parser.metadata().subscriptions.size(), 1u);
  EXPECT_EQ(parser.metadata().subscriptions[0].id, "feed/one");
  EXPECT_EQ(parser.metadata().subscriptions[0].categoryIds[0], "user/-/label/Tech");
}

TEST(FreshRssJsonParser, PrefersContentAndFallsBackToSummary) {
  const std::string json = R"({"items":[{"id":"a","title":"Title","crawlTimeMsec":1785751200000,"origin":{"streamId":"feed/one","title":"Origin"},"canonical":[{"href":"https://canonical"}],"summary":{"content":"summary"},"content":{"content":"<p><b>full</b></p>"},"categories":["user/-/label/Tech"]},{"id":"b","title":"Summary only","summary":{"content":"summary body"}}]})";
  std::vector<FreshRssArticle> articles;
  FreshRssJsonParser parser(FreshRssJsonParser::Document::Articles,
                            [&articles](FreshRssArticle&& article) {
                              articles.push_back(std::move(article));
                              return true;
                            });
  parser.feed(reinterpret_cast<const uint8_t*>(json.data()), json.size());
  ASSERT_TRUE(parser.finish());
  ASSERT_EQ(articles.size(), 2u);
  EXPECT_EQ(articles[0].contentHtml, "<p><b>full</b></p>");
  EXPECT_EQ(articles[0].link, "https://canonical");
  EXPECT_EQ(articles[0].streamId, "feed/one");
  EXPECT_EQ(articles[0].modifiedMsec, 1785751200000ULL);
  EXPECT_EQ(articles[1].contentHtml, "summary body");
}

TEST(FreshRssJsonParser, ParsesFreshRssStringCrawlTime) {
  FreshRssArticle result;
  const std::string json = R"({"items":[{"id":"a","crawlTimeMsec":"1785751200000","summary":{"content":"body"}}]})";
  FreshRssJsonParser parser(FreshRssJsonParser::Document::Articles, [&result](FreshRssArticle&& article) {
    result = std::move(article);
    return true;
  });
  parser.feed(reinterpret_cast<const uint8_t*>(json.data()), json.size());
  ASSERT_TRUE(parser.finish());
  EXPECT_EQ(result.modifiedMsec, 1785751200000ULL);
  EXPECT_EQ(result.date, "2026-08-03T10:00:00Z");
}

TEST(FreshRssJsonParser, AcceptsDirectContentAndSummaryStrings) {
  std::vector<FreshRssArticle> articles;
  const std::string json = R"({"items":[
    {"id":"direct-content","title":"Content","content":"<p>Full</p>"},
    {"id":"direct-summary","title":"Summary","summary":"<p>Fallback</p>"}
  ]})";
  FreshRssJsonParser parser(FreshRssJsonParser::Document::Articles,
                           [&articles](FreshRssArticle&& item) {
                             articles.push_back(std::move(item));
                             return true;
                           });
  parser.feed(reinterpret_cast<const uint8_t*>(json.data()), json.size());
  ASSERT_TRUE(parser.finish());
  ASSERT_EQ(articles.size(), 2U);
  EXPECT_EQ(articles[0].contentHtml, "<p>Full</p>");
  EXPECT_EQ(articles[1].contentHtml, "<p>Fallback</p>");
}

TEST(FreshRssJsonParser, AcceptsDirectAuthorString) {
  FreshRssArticle result;
  const std::string json = R"({"items":[{"id":"a","author":"Gujarati Desk"}]})";
  FreshRssJsonParser parser(FreshRssJsonParser::Document::Articles, [&result](FreshRssArticle&& article) {
    result = std::move(article);
    return true;
  });
  parser.feed(reinterpret_cast<const uint8_t*>(json.data()), json.size());
  ASSERT_TRUE(parser.finish());
  EXPECT_EQ(result.author, "Gujarati Desk");
}

TEST(FreshRssJsonParser, ReplacesUrlAsTitleWithReadableSlug) {
  FreshRssArticle result;
  const std::string json = R"({"items":[{"id":"url-title","title":"https://example.test/news/first-story-about-rss-123456.html","canonical":[{"href":"https://example.test/news/first-story-about-rss-123456.html"}]}]})";
  FreshRssJsonParser parser(FreshRssJsonParser::Document::Articles, [&result](FreshRssArticle&& article) {
    result = std::move(article);
    return true;
  });
  parser.feed(reinterpret_cast<const uint8_t*>(json.data()), json.size());
  ASSERT_TRUE(parser.finish());
  EXPECT_EQ(result.title, "First story about rss");
}

TEST(FreshRssJsonParser, StreamsLongBodyWithBoundedPrefixAndReportsContinuation) {
  const std::string body(40 * 1024, 'x');
  const std::string json = std::string(R"({"continuation":"next","items":[{"id":"long","summary":{"content":"<p>)") + body +
                             R"(</p>"}}]})";
  FreshRssArticle result;
  FreshRssJsonParser parser(FreshRssJsonParser::Document::Articles, [&result](FreshRssArticle&& article) {
    result = std::move(article);
    return true;
  });
  parser.feed(reinterpret_cast<const uint8_t*>(json.data()), json.size());
  ASSERT_TRUE(parser.finish());
  EXPECT_EQ(result.contentHtml.size(), 32u * 1024u);
  EXPECT_TRUE(result.bodyTruncated);
  EXPECT_EQ(parser.continuation(), "next");
}

TEST(FreshRssJsonParser, FullContentGetsFreshBudgetAfterAnEarlierSummary) {
  const std::string summary(31 * 1024, 's');
  const std::string content(2 * 1024, 'c');
  const std::string json = std::string(R"({"items":[{"id":"a","summary":{"content":")") + summary +
                             R"("},"content":{"content":")" + content + R"("}}]})";
  FreshRssArticle result;
  FreshRssJsonParser parser(FreshRssJsonParser::Document::Articles, [&result](FreshRssArticle&& article) {
    result = std::move(article);
    return true;
  });
  parser.feed(reinterpret_cast<const uint8_t*>(json.data()), json.size());
  ASSERT_TRUE(parser.finish());
  EXPECT_GT(result.contentHtml.size(), 1024u);
  EXPECT_EQ(result.contentHtml.find_first_not_of('c'), std::string::npos);
  EXPECT_FALSE(result.bodyTruncated);
}

TEST(FreshRssJsonParser, SinkFailureIsReported) {
  const std::string json = R"({"items":[{"id":"a"}]})";
  FreshRssJsonParser parser(FreshRssJsonParser::Document::Articles, [](FreshRssArticle&&) { return false; });
  parser.feed(reinterpret_cast<const uint8_t*>(json.data()), json.size());
  EXPECT_FALSE(parser.finish());
}

// Regression test: an id field longer than the internal MAX_ID_CHARS bound
// (256 bytes) used to be dropped entirely rather than truncated, which then
// tripped the "id is empty" check and failed the whole document — one
// oversized field aborting every article in the page, not just itself.
TEST(FreshRssJsonParser, OverlongIdIsTruncatedNotDropped) {
  const std::string longId(300, 'x');
  const std::string json = R"({"items":[{"id":")" + longId + R"(","title":"Kept"},{"id":"normal","title":"Also kept"}]})";
  std::vector<FreshRssArticle> articles;
  FreshRssJsonParser parser(FreshRssJsonParser::Document::Articles,
                            [&articles](FreshRssArticle&& article) {
                              articles.push_back(std::move(article));
                              return true;
                            });
  parser.feed(reinterpret_cast<const uint8_t*>(json.data()), json.size());
  ASSERT_TRUE(parser.finish());
  ASSERT_EQ(articles.size(), 2u);
  EXPECT_EQ(articles[0].id.size(), 256u);
  EXPECT_EQ(articles[0].id, longId.substr(0, 256));
  EXPECT_EQ(articles[1].id, "normal");
}

TEST(FreshRssJsonParser, TruncatesNavigationMetadataAtDeviceCapacity) {
  std::string subscriptions = R"({"subscriptions":[)";
  std::string tags = R"({"tags":[)";
  for (int i = 0; i < 130; ++i) {
    if (i != 0) {
      subscriptions += ',';
      tags += ',';
    }
    subscriptions += "{\"id\":\"feed/" + std::to_string(i) + "\",\"title\":\"Feed\"}";
    tags += "{\"id\":\"user/-/label/" + std::to_string(i) + "\",\"label\":\"Tag\"}";
  }
  subscriptions += "]}";
  tags += "]}";

  FreshRssJsonParser subscriptionParser(FreshRssJsonParser::Document::Subscriptions);
  subscriptionParser.feed(reinterpret_cast<const uint8_t*>(subscriptions.data()), subscriptions.size());
  ASSERT_TRUE(subscriptionParser.finish());
  EXPECT_EQ(subscriptionParser.metadata().subscriptions.size(), 128U);

  FreshRssJsonParser tagParser(FreshRssJsonParser::Document::Tags);
  tagParser.feed(reinterpret_cast<const uint8_t*>(tags.data()), tags.size());
  ASSERT_TRUE(tagParser.finish());
  EXPECT_EQ(tagParser.metadata().tags.size(), 128U);
}
