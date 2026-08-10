#include <gtest/gtest.h>

#include <array>
#include <vector>

#include "src/RssCachePolicy.h"
#include "src/RssItemFilter.h"

TEST(RssItemFilter, KeepsOnlyUnreadPositionsWithoutCopyingItems) {
  const std::array<bool, 5> read = {true, false, true, false, false};
  const auto indexes = RssItemFilter::visibleIndexes(read.size(), true, [&](const size_t index) { return read[index]; });
  EXPECT_EQ(indexes, (std::vector<uint16_t>{1, 3, 4}));
}

TEST(RssItemFilter, EmptyUnreadFeedHasNoVisiblePositions) {
  const std::array<bool, 3> read = {true, true, true};
  const auto indexes = RssItemFilter::visibleIndexes(read.size(), true, [&](const size_t index) { return read[index]; });
  EXPECT_TRUE(indexes.empty());
}

TEST(RssItemFilter, SupportsOneThousandArticlePositions) {
  std::vector<bool> read(1000, false);
  const auto indexes = RssItemFilter::visibleIndexes(read.size(), true, [&](const size_t index) { return read[index]; });
  ASSERT_EQ(indexes.size(), 1000U);
  EXPECT_EQ(indexes.front(), 0U);
  EXPECT_EQ(indexes.back(), 999U);
}

TEST(RssCachePolicy, EvictsOldestUnprotectedEntriesOnly) {
  const std::vector<RssCachePolicy::Entry> entries = {
      {10, false}, {20, true}, {30, false}, {40, false}, {50, true}};
  const auto evictions = RssCachePolicy::selectEvictions(entries, 1);
  EXPECT_EQ(evictions, (std::vector<size_t>{0, 2}));
}

TEST(RssCachePolicy, ProtectedEntriesCanExceedNormalLimit) {
  const std::vector<RssCachePolicy::Entry> entries = {{10, true}, {20, true}};
  EXPECT_TRUE(RssCachePolicy::selectEvictions(entries, 0).empty());
}
