#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "HalStorage.h"
#include "RssItemCache.h"

namespace {

template <typename T>
void writePod(std::ofstream& out, const T& value) {
  out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

void writeString(std::ofstream& out, const std::string& value) {
  const uint32_t length = static_cast<uint32_t>(value.size());
  writePod(out, length);
  out.write(value.data(), value.size());
}

RichText body(const std::string& first, const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  RichParagraph paragraph;
  paragraph.align = TextAlign::CENTER;
  paragraph.words.push_back(StyledWord{first, style, false});
  return {std::move(paragraph)};
}

CachedRssItem item(const uint32_t key, const std::string& title, const bool truncated = false) {
  CachedRssItem value;
  value.key = key;
  value.title = title;
  value.link = "https://example.test/" + title;
  value.date = "2026-08-02";
  value.body = body("Gujarati " + title, truncated ? EpdFontFamily::ITALIC : EpdFontFamily::BOLD);
  value.bodyTruncated = truncated;
  return value;
}

void writeLegacySnapshot(const std::string& feedUrl, const uint8_t version, const CachedRssItem& cached) {
  const std::string directory = RssItemCache::cacheDirFor(feedUrl);
  Storage.mkdir("/.crosspoint");
  Storage.mkdir("/.crosspoint/rss");
  Storage.mkdir(directory.c_str());
  std::ofstream out(HalStorage::hostPath(directory + "/items.bin"), std::ios::binary | std::ios::trunc);
  const uint32_t count = 1;
  writePod(out, version);
  writePod(out, count);
  writePod(out, cached.key);
  writeString(out, cached.title);
  writeString(out, cached.link);
  writeString(out, cached.date);
  if (version == 3) {
    const uint8_t truncated = cached.bodyTruncated ? 1 : 0;
    writePod(out, truncated);
  }
  const uint32_t paragraphs = static_cast<uint32_t>(cached.body.size());
  writePod(out, paragraphs);
  for (const auto& paragraph : cached.body) {
    const auto align = static_cast<uint8_t>(paragraph.align);
    const uint32_t words = static_cast<uint32_t>(paragraph.words.size());
    writePod(out, align);
    writePod(out, words);
    for (const auto& word : paragraph.words) {
      const auto style = static_cast<uint8_t>(word.style);
      const uint8_t continues = word.continuesPrevious ? 1 : 0;
      writePod(out, style);
      writePod(out, continues);
      writeString(out, word.text);
    }
  }
}

class RssItemCacheTest : public ::testing::Test {
 protected:
  void SetUp() override { HalStorage::setRoot("/tmp/crosspoint-rss-cache-test"); }
};

}  // namespace

TEST_F(RssItemCacheTest, FullFeedRoundTripMetadataAndSingleBodyLoading) {
  const std::string feed = "https://example.test/feed";
  const auto first = item(11, "one");
  const auto second = item(22, "two", true);

  ASSERT_TRUE(RssItemCache::save(feed, {first, second}));

  std::vector<CachedRssItem> metadata;
  ASSERT_TRUE(RssItemCache::loadIndex(feed, metadata));
  ASSERT_EQ(metadata.size(), 2u);
  EXPECT_EQ(metadata[0].key, first.key);
  EXPECT_TRUE(metadata[0].body.empty());
  EXPECT_TRUE(metadata[1].bodyTruncated);

  RichText loaded;
  bool complete = false;
  ASSERT_TRUE(RssItemCache::loadItemBody(feed, first.key, loaded, complete));
  EXPECT_TRUE(complete);
  ASSERT_EQ(loaded.size(), 1u);
  EXPECT_EQ(loaded[0].words[0].text, "Gujarati one");
  EXPECT_EQ(loaded[0].words[0].style, EpdFontFamily::BOLD);

  ASSERT_TRUE(RssItemCache::loadItemBody(feed, second.key, loaded, complete));
  EXPECT_FALSE(complete);
  EXPECT_EQ(loaded[0].words[0].style, EpdFontFamily::ITALIC);

  const auto stats = RssItemCache::stats();
  EXPECT_EQ(stats.itemCount, 2u);
  EXPECT_EQ(stats.completeBodies, 1u);
  EXPECT_EQ(stats.truncatedBodies, 1u);
  EXPECT_GT(stats.bytes, 0u);
}

TEST_F(RssItemCacheTest, FailedRefreshPreservesPreviousSnapshot) {
  const std::string feed = "https://example.test/feed";
  const auto oldItem = item(1, "old");
  ASSERT_TRUE(RssItemCache::save(feed, {oldItem}));

  RssItemCache::FeedWriteSession writer(feed);
  ASSERT_TRUE(writer.begin());
  auto invalid = item(2, std::string(201, 'x'));
  EXPECT_FALSE(writer.append(invalid));
  writer.abort();

  std::vector<CachedRssItem> metadata;
  ASSERT_TRUE(RssItemCache::loadIndex(feed, metadata));
  ASSERT_EQ(metadata.size(), 1u);
  EXPECT_EQ(metadata[0].key, oldItem.key);
  EXPECT_FALSE(Storage.exists((RssItemCache::cacheDirFor(feed) + "/items.bin.tmp").c_str()));
}

TEST_F(RssItemCacheTest, RejectsIncompleteSnapshot) {
  const std::string feed = "https://example.test/feed";
  const std::string path = RssItemCache::cacheDirFor(feed) + "/items.bin";
  Storage.mkdir("/.crosspoint");
  Storage.mkdir("/.crosspoint/rss");
  Storage.mkdir(RssItemCache::cacheDirFor(feed).c_str());
  std::ofstream out(HalStorage::hostPath(path), std::ios::binary | std::ios::trunc);
  const uint8_t version = 4;
  const uint32_t count = 0;
  writePod(out, version);
  writePod(out, count);
  for (size_t i = 0; i < 64; ++i) {
    const uint32_t key = 0;
    writePod(out, key);
  }
  out.close();

  std::vector<CachedRssItem> metadata;
  EXPECT_FALSE(RssItemCache::loadIndex(feed, metadata));
}

TEST_F(RssItemCacheTest, ReadsLegacyV2AndV3Bodies) {
  for (const uint8_t version : {static_cast<uint8_t>(2), static_cast<uint8_t>(3)}) {
    const std::string feed = "https://example.test/feed-" + std::to_string(version);
    const auto oldItem = item(version, "legacy", version == 2);
    writeLegacySnapshot(feed, version, oldItem);

    std::vector<CachedRssItem> metadata;
    ASSERT_TRUE(RssItemCache::loadIndex(feed, metadata));
    ASSERT_EQ(metadata.size(), 1u);
    RichText loaded;
    bool complete = false;
    ASSERT_TRUE(RssItemCache::loadItemBody(feed, oldItem.key, loaded, complete));
    EXPECT_EQ(complete, version == 3 && !oldItem.bodyTruncated);
    EXPECT_EQ(loaded[0].words[0].text, "Gujarati legacy");
  }
}

TEST_F(RssItemCacheTest, ClearCacheRemovesSnapshotsTemporaryAndLegacyFiles) {
  const std::string feed = "https://example.test/feed";
  ASSERT_TRUE(RssItemCache::save(feed, {item(1, "one")}));
  const std::string directory = RssItemCache::cacheDirFor(feed);
  std::ofstream(HalStorage::hostPath(directory + "/item_1.bin")).put('x');
  std::ofstream(HalStorage::hostPath("/.crosspoint/rss/manifest.bin")).put('x');
  std::ofstream(HalStorage::hostPath(directory + "/items.bin.tmp")).put('x');

  ASSERT_TRUE(RssItemCache::clearAll());
  EXPECT_FALSE(Storage.exists(directory.c_str()));
  EXPECT_FALSE(Storage.exists("/.crosspoint/rss/manifest.bin"));
}
