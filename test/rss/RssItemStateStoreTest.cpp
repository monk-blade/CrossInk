#include <gtest/gtest.h>

#include "HalStorage.h"
#include "RssItemStateStore.h"

class RssItemStateStoreTest : public testing::Test {
 protected:
  void SetUp() override {
    HalStorage::setRoot("/tmp/crosspoint-rss-state-test");
    RSS_ITEM_STATE.load();
    RSS_ITEM_STATE.clearAll();
    ASSERT_TRUE(RSS_ITEM_STATE.saveIfDirty());
  }
};

TEST_F(RssItemStateStoreTest, QueuePersistsAndIsIndependentOfReadAndStarState) {
  ASSERT_TRUE(RSS_ITEM_STATE.toggleQueued("freshrss", 42));
  RSS_ITEM_STATE.markRead("freshrss", 42);
  RSS_ITEM_STATE.toggleStar("freshrss", 42);
  ASSERT_TRUE(RSS_ITEM_STATE.saveIfDirty());

  RSS_ITEM_STATE.load();
  EXPECT_TRUE(RSS_ITEM_STATE.isQueued("freshrss", 42));
  EXPECT_TRUE(RSS_ITEM_STATE.isRead("freshrss", 42));
  EXPECT_TRUE(RSS_ITEM_STATE.isStarred("freshrss", 42));
  EXPECT_EQ(RSS_ITEM_STATE.countQueued("freshrss"), 1U);
  ASSERT_TRUE(RSS_ITEM_STATE.toggleQueued("freshrss", 42));
  EXPECT_FALSE(RSS_ITEM_STATE.isQueued("freshrss", 42));
}

TEST_F(RssItemStateStoreTest, QueueHasA256ItemBound) {
  for (uint32_t key = 0; key < 256; ++key) ASSERT_TRUE(RSS_ITEM_STATE.toggleQueued("freshrss", key));
  EXPECT_FALSE(RSS_ITEM_STATE.toggleQueued("freshrss", 256));
  EXPECT_EQ(RSS_ITEM_STATE.countQueued("freshrss"), 256U);
}

// Regression test: the FreshRSS pseudo-feed used to share the 200-entry
// per-feed read-marker cap with ordinary feeds, so reading article 201 of a
// 1000-article snapshot evicted article 1's read marker and it resurfaced as
// unread. The cap must track the injected article limit instead.
TEST_F(RssItemStateStoreTest, ReadMarkerCapScalesWithFreshRssArticleLimit) {
  RSS_ITEM_STATE.setFreshRssArticleLimit(1000);
  for (uint32_t key = 0; key < 300; ++key) RSS_ITEM_STATE.markRead("freshrss", key);
  for (uint32_t key = 0; key < 300; ++key) EXPECT_TRUE(RSS_ITEM_STATE.isRead("freshrss", key)) << key;

  ASSERT_TRUE(RSS_ITEM_STATE.saveIfDirty());
  RSS_ITEM_STATE.setFreshRssArticleLimit(1000);  // persists across the process; reapply after reload
  RSS_ITEM_STATE.load();
  RSS_ITEM_STATE.setFreshRssArticleLimit(1000);
  for (uint32_t key = 0; key < 300; ++key) EXPECT_TRUE(RSS_ITEM_STATE.isRead("freshrss", key)) << key;
}

// An ordinary (non-FreshRSS) feed keeps the original fixed 200-entry cap —
// only the "freshrss" pseudo-feed key scales with the injected limit.
TEST_F(RssItemStateStoreTest, OrdinaryFeedKeepsFixedReadMarkerCap) {
  RSS_ITEM_STATE.setFreshRssArticleLimit(1000);
  for (uint32_t key = 0; key < 210; ++key) RSS_ITEM_STATE.markRead("https://example.com/feed", key);
  EXPECT_FALSE(RSS_ITEM_STATE.isRead("https://example.com/feed", 0)) << "oldest marker should have been evicted";
  EXPECT_TRUE(RSS_ITEM_STATE.isRead("https://example.com/feed", 209));
}
