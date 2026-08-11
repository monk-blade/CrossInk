#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "FreshRssCache.h"
#include "HalStorage.h"

namespace {
RichText body(const std::string& text) {
  RichParagraph paragraph;
  paragraph.align = TextAlign::CENTER;
  paragraph.words.push_back(StyledWord{text, EpdFontFamily::BOLD, false});
  return {std::move(paragraph)};
}

FreshRssCachedArticle article(uint32_t key, const char* id, const char* category, bool truncated = false) {
  FreshRssCachedArticle result;
  result.key = key;
  result.id = id;
  result.title = id;
  result.link = std::string("https://example.test/") + id;
  result.date = "2026-08-03T10:00:00Z";
  result.categoryIds = {category};
  result.subscriptionId = "stream/one";
  result.body = body("Gujarati " + std::string(id));
  result.bodyTruncated = truncated;
  return result;
}

class FreshRssCacheTest : public ::testing::Test {
 protected:
  void SetUp() override {
    HalStorage::setRoot("/tmp/crosspoint-freshrss-cache-test");
    FreshRssCache::clear();
  }
  void TearDown() override { FreshRssCache::clear(); }
};
}  // namespace

TEST_F(FreshRssCacheTest, RoundTripMetadataBodiesAndFilters) {
  FreshRssMetadata metadata;
  metadata.tags.push_back({"user/-/label/Tech", ""});
  metadata.tags.push_back({"user/-/state/com.google/read", "Read"});
  metadata.subscriptions.push_back({"feed/one", "One", "https://one", {"user/-/label/Tech"}});

  FreshRssCache::WriteSession writer;
  ASSERT_TRUE(writer.begin(metadata, 50));
  auto first = article(101, "first", "user/-/label/Tech");
  first.origin = "Origin";
  first.author = "Author";
  auto second = article(202, "second", "other", true);
  second.subscriptionId = "stream/two";
  ASSERT_TRUE(writer.append(first));
  ASSERT_TRUE(writer.append(second));
  ASSERT_TRUE(writer.commit());

  std::vector<uint32_t> keys;
  ASSERT_TRUE(FreshRssCache::loadKeys(FreshRssFilterKind::All, "", keys));
  ASSERT_EQ(keys.size(), 2u);
  std::vector<CachedRssItem> page;
  ASSERT_TRUE(FreshRssCache::loadItemsByKeys(keys, 1, 1, page));
  ASSERT_EQ(page.size(), 1u);
  EXPECT_EQ(page[0].key, 202u);
  EXPECT_EQ(page[0].subtitle, "");

  ASSERT_TRUE(FreshRssCache::loadItemsByKeys(keys, 0, 1, page));
  ASSERT_EQ(page.size(), 1u);
  EXPECT_EQ(page[0].subtitle, "Origin · Author");

  std::vector<FreshRssIndexEntry> index;
  ASSERT_TRUE(FreshRssCache::loadNavigationIndex(index));
  ASSERT_EQ(index.size(), 2u);
  EXPECT_GT(index[0].recordOffset, 0U);
  RichText offsetBody;
  bool offsetComplete = false;
  ASSERT_TRUE(FreshRssCache::loadItemBodyByOffset(index[1].recordOffset, index[1].key, offsetBody, offsetComplete));
  EXPECT_FALSE(offsetComplete);
  EXPECT_EQ(offsetBody[0].words[0].text, "Gujarati second");

  // A stale offset paired with the wrong key must not silently return
  // whatever record now lives at that offset (regression for the bug where
  // only the offset was checked).
  RichText mismatchedBody;
  bool mismatchedComplete = false;
  EXPECT_FALSE(FreshRssCache::loadItemBodyByOffset(index[1].recordOffset, index[1].key + 1, mismatchedBody,
                                                    mismatchedComplete));
  EXPECT_GT(FreshRssCache::stats().indexBytes, 0U);

  std::vector<CachedRssItem> items;
  ASSERT_TRUE(FreshRssCache::loadIndex(FreshRssFilterKind::All, "", items));
  ASSERT_EQ(items.size(), 2u);
  EXPECT_TRUE(items[1].bodyTruncated);

  items.clear();
  ASSERT_TRUE(FreshRssCache::loadIndex(FreshRssFilterKind::Category, "user/-/label/Tech", items));
  ASSERT_EQ(items.size(), 1u);
  EXPECT_EQ(items[0].key, 101u);

  std::vector<FreshRssNavigationEntry> navigation;
  ASSERT_TRUE(FreshRssCache::loadNavigation(navigation));
  EXPECT_TRUE(std::any_of(navigation.begin(), navigation.end(), [](const FreshRssNavigationEntry& entry) {
    return entry.kind == FreshRssFilterKind::Category && entry.id == "user/-/label/Tech";
  }));
  EXPECT_TRUE(std::any_of(navigation.begin(), navigation.end(), [](const FreshRssNavigationEntry& entry) {
    return entry.kind == FreshRssFilterKind::Category && entry.label == "Tech" && entry.articleCount == 1;
  }));
  EXPECT_FALSE(std::any_of(navigation.begin(), navigation.end(), [](const FreshRssNavigationEntry& entry) {
    return entry.id == "user/-/state/com.google/read";
  }));
  EXPECT_FALSE(std::any_of(navigation.begin(), navigation.end(), [](const FreshRssNavigationEntry& entry) {
    return entry.kind == FreshRssFilterKind::Uncategorized;
  }));

  RichText loaded;
  bool complete = false;
  ASSERT_TRUE(FreshRssCache::loadItemBody(202, loaded, complete));
  EXPECT_FALSE(complete);
  ASSERT_EQ(loaded.size(), 1u);
  EXPECT_EQ(loaded[0].words[0].text, "Gujarati second");
}

TEST_F(FreshRssCacheTest, PersistsSyncCursorAndCopiesUnchangedRecords) {
  FreshRssMetadata metadata;
  FreshRssSyncCursor firstCursor;
  firstCursor.modifiedMsec = 1000;
  firstCursor.generation = 4;
  firstCursor.accountIdentity = 77;
  firstCursor.articleLimit = 200;
  firstCursor.valid = true;
  FreshRssCache::WriteSession first;
  ASSERT_TRUE(first.begin(metadata, 200, firstCursor));
  auto unchanged = article(2, "unchanged", "cat");
  unchanged.modifiedMsec = 900;
  auto replaced = article(1, "old", "cat");
  replaced.modifiedMsec = 1000;
  ASSERT_TRUE(first.append(replaced));
  ASSERT_TRUE(first.append(unchanged));
  ASSERT_TRUE(first.commit());

  FreshRssSyncCursor loaded;
  ASSERT_TRUE(FreshRssCache::loadSyncCursor(loaded));
  EXPECT_EQ(loaded.modifiedMsec, 1000U);
  EXPECT_EQ(loaded.generation, 4U);
  EXPECT_EQ(loaded.accountIdentity, 77U);

  FreshRssSyncCursor secondCursor = loaded;
  secondCursor.modifiedMsec = 2000;
  secondCursor.generation = 5;
  FreshRssCache::WriteSession second;
  ASSERT_TRUE(second.begin(metadata, 200, secondCursor));
  auto updated = article(1, "new", "cat");
  updated.modifiedMsec = 2000;
  ASSERT_TRUE(second.append(updated));
  ASSERT_TRUE(second.copyUnchangedFromCurrent({1}));
  ASSERT_TRUE(second.commit());

  std::vector<CachedRssItem> items;
  ASSERT_TRUE(FreshRssCache::loadIndex(FreshRssFilterKind::All, "", items));
  ASSERT_EQ(items.size(), 2U);
  EXPECT_EQ(items[0].title, "new");
  EXPECT_EQ(items[1].title, "unchanged");
}

TEST_F(FreshRssCacheTest, CategoryFirstNavigationSeparatesDirectAndFeedModes) {
  FreshRssMetadata metadata;
  metadata.subscriptions.push_back({"feed/one", "One", "https://one", {"user/-/label/Tech"}});
  metadata.subscriptions.push_back({"feed/two", "Two", "https://two", {"user/-/label/Other"}});

  FreshRssCache::WriteSession writer;
  ASSERT_TRUE(writer.begin(metadata, 20));
  auto first = article(11, "first", "missing-from-item");
  // The subscription relationship is the fallback category mapping used by
  // real FreshRSS responses that omit item categories.
  first.categoryIds.clear();
  first.subscriptionId = "feed/one";
  ASSERT_TRUE(writer.append(first));
  auto second = article(22, "second", "user/-/label/Other");
  second.subscriptionId = "feed/two";
  ASSERT_TRUE(writer.append(second));
  ASSERT_TRUE(writer.commit());

  std::vector<FreshRssNavigationEntry> categories;
  ASSERT_TRUE(FreshRssCache::loadCategoryNavigation(categories));
  ASSERT_EQ(categories.size(), 2u);
  EXPECT_EQ(categories[0].id, "user/-/label/Tech");
  EXPECT_EQ(categories[0].articleCount, 1u);
  EXPECT_EQ(categories[1].id, "user/-/label/Other");

  std::vector<FreshRssNavigationEntry> subscriptions;
  ASSERT_TRUE(FreshRssCache::loadSubscriptionNavigationForCategory("user/-/label/Tech", subscriptions));
  ASSERT_EQ(subscriptions.size(), 1u);
  EXPECT_EQ(subscriptions[0].id, "feed/one");
  EXPECT_EQ(subscriptions[0].articleCount, 1u);

  std::vector<uint32_t> keys;
  ASSERT_TRUE(FreshRssCache::loadKeys(FreshRssFilterKind::Category, "user/-/label/Tech", keys));
  ASSERT_EQ(keys.size(), 1u);
  EXPECT_EQ(keys[0], 11u);
}

TEST_F(FreshRssCacheTest, RepairsUrlTitlesFromExistingSnapshot) {
  FreshRssMetadata metadata;
  FreshRssCache::WriteSession writer;
  ASSERT_TRUE(writer.begin(metadata, 20));
  auto value = article(303, "url-title", "cat");
  value.link = "https://example.test/news/old-title-from-url-987654.html";
  value.title = value.link;
  ASSERT_TRUE(writer.append(value));
  ASSERT_TRUE(writer.commit());

  std::vector<CachedRssItem> items;
  ASSERT_TRUE(FreshRssCache::loadIndex(FreshRssFilterKind::All, "", items));
  ASSERT_EQ(items.size(), 1u);
  EXPECT_EQ(items[0].title, "Old title from url");
}

TEST_F(FreshRssCacheTest, FailedReplacementPreservesPreviousSnapshot) {
  FreshRssMetadata metadata;
  FreshRssCache::WriteSession good;
  ASSERT_TRUE(good.begin(metadata, 20));
  ASSERT_TRUE(good.append(article(1, "old", "cat")));
  ASSERT_TRUE(good.commit());

  FreshRssCache::WriteSession failed;
  ASSERT_TRUE(failed.begin(metadata, 20));
  auto invalid = article(2, "new", "cat");
  invalid.title.assign(5000, 'x');
  EXPECT_FALSE(failed.append(invalid));
  failed.abort();

  std::vector<CachedRssItem> items;
  ASSERT_TRUE(FreshRssCache::loadIndex(FreshRssFilterKind::All, "", items));
  ASSERT_EQ(items.size(), 1u);
  EXPECT_EQ(items[0].key, 1u);
}

TEST_F(FreshRssCacheTest, SanitizesMalformedCachedBodyOnLoad) {
  FreshRssMetadata metadata;
  FreshRssCache::WriteSession writer;
  ASSERT_TRUE(writer.begin(metadata, 20));
  auto value = article(7, "damaged", "cat");
  value.body = body(std::string("ok") + "\xE0\xA4\xE0\xA4\xB0\xEF\xBF\xBD" + "end");
  ASSERT_TRUE(writer.append(value));
  ASSERT_TRUE(writer.commit());

  RichText loaded;
  bool complete = false;
  ASSERT_TRUE(FreshRssCache::loadItemBody(7, loaded, complete));
  ASSERT_EQ(loaded.size(), 1u);
  ASSERT_EQ(loaded[0].words.size(), 1u);
  EXPECT_EQ(loaded[0].words[0].text, std::string("ok") + "\xE0\xA4\xB0" + "end");
}

TEST_F(FreshRssCacheTest, RejectsCorruptOrIncompleteSnapshot) {
  FreshRssMetadata metadata;
  FreshRssCache::WriteSession writer;
  ASSERT_TRUE(writer.begin(metadata, 20));
  ASSERT_TRUE(writer.append(article(1, "one", "cat")));
  ASSERT_TRUE(writer.commit());

  const auto path = HalStorage::hostPath("/.crosspoint/freshrss/snapshot.bin");
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << "incomplete";
  out.close();
  std::vector<CachedRssItem> items;
  EXPECT_FALSE(FreshRssCache::loadIndex(FreshRssFilterKind::All, "", items));
  EXPECT_FALSE(FreshRssCache::exists());
}
