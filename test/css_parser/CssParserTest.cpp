#include <gtest/gtest.h>

#include "Epub/css/CssParser.h"

TEST(CssParser, StripsImportantBeforeResolvingLengthUnits) {
  const CssStyle style = CssParser::parseInlineStyle("text-indent: 1.60em !important;");

  ASSERT_TRUE(style.hasTextIndent());
  EXPECT_EQ(style.textIndent.unit, CssUnit::Em);
  EXPECT_FLOAT_EQ(style.textIndent.value, 1.60f);
  EXPECT_EQ(style.textIndent.toPixelsInt16(30.0f), 48);
}

TEST(CssParser, StripsImportantBeforeSplittingLengthShorthand) {
  const CssStyle style = CssParser::parseInlineStyle("margin: 1em 2em !important;");

  EXPECT_FLOAT_EQ(style.marginTop.value, 1.0f);
  EXPECT_EQ(style.marginTop.unit, CssUnit::Em);
  EXPECT_FLOAT_EQ(style.marginRight.value, 2.0f);
  EXPECT_EQ(style.marginRight.unit, CssUnit::Em);
  EXPECT_FLOAT_EQ(style.marginBottom.value, 1.0f);
  EXPECT_EQ(style.marginBottom.unit, CssUnit::Em);
  EXPECT_FLOAT_EQ(style.marginLeft.value, 2.0f);
  EXPECT_EQ(style.marginLeft.unit, CssUnit::Em);
}
