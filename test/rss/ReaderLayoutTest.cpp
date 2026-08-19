#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "../../lib/ReaderLayout/ReaderLayout.h"

TEST(ReaderLayout, UsesMinimumRaggednessAndHonorsContinuationGroups) {
  const std::vector<uint16_t> widths = {20, 20, 20, 20};
  const std::vector<bool> continues = {false, false, true, false};
  const std::vector<bool> noSpace = {false, false, false, false};
  const auto breaks = ReaderLayout::minimumRaggednessBreaks(
      widths, continues, noSpace, 45, 0, [](size_t, size_t) { return 5; });
  ASSERT_FALSE(breaks.empty());
  EXPECT_EQ(breaks.back(), widths.size());
  EXPECT_TRUE(breaks[0] != 2);  // the attached third token cannot start a line
}

TEST(ReaderLayout, ForcedParagraphIndentUsesVisibleFontRelativeWidth) {
  EXPECT_EQ(ReaderLayout::forcedParagraphIndent(true, 12, 16), 12);
  EXPECT_EQ(ReaderLayout::forcedParagraphIndent(true, 0, 16), 16);
  EXPECT_EQ(ReaderLayout::forcedParagraphIndent(false, 12, 16), 0);
}

TEST(ReaderLayout, JustificationExtraIsShared) {
  EXPECT_EQ(ReaderLayout::justificationExtra(11, 3), 3);
  EXPECT_EQ(ReaderLayout::justificationExtra(11, 0), 0);
}
