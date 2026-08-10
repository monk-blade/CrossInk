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
