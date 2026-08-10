#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "RssParser.h"

namespace {

void parse(RssParser& parser, const std::string& xml) {
  ASSERT_EQ(parser.write(reinterpret_cast<const uint8_t*>(xml.data()), xml.size()), xml.size());
  parser.flush();
  ASSERT_FALSE(parser.error());
}

constexpr char FEED[] = R"xml(
<rss><channel><title>Feed</title>
<item><guid>one</guid><title>One</title><link>https://one</link><description>Teaser one</description><content:encoded><![CDATA[<p>Full <b>one</b></p>]]></content:encoded></item>
<item><guid>two</guid><title>Two</title><link>https://two</link><description>Second body</description></item>
</channel></rss>
)xml";

}  // namespace

TEST(RssParser, SinkReceivesOneItemAtATimeAndDoesNotRetainFeed) {
  RssParser parser(1024);
  std::vector<RssItem> received;
  parser.setItemSink([&received](RssItem&& item) {
    received.push_back(std::move(item));
    return true;
  });

  parse(parser, FEED);

  ASSERT_EQ(received.size(), 2u);
  EXPECT_TRUE(parser.getItems().empty());
  EXPECT_EQ(received[0].body, "<p>Full <b>one</b></p>");
  EXPECT_EQ(received[1].body, "Second body");
  EXPECT_FALSE(received[0].bodyTruncated);
}

TEST(RssParser, ReportsBodyTruncationAtTheBound) {
  RssParser parser(8);
  std::vector<RssItem> received;
  parser.setItemSink([&received](RssItem&& item) {
    received.push_back(std::move(item));
    return true;
  });

  parse(parser, "<rss><channel><item><title>x</title><description>123456789</description></item></channel></rss>");

  ASSERT_EQ(received.size(), 1u);
  EXPECT_EQ(received[0].body, "12345678");
  EXPECT_TRUE(received[0].bodyTruncated);
}

TEST(RssParser, ReleasesDescriptionBeforeRetainingBoundedContent) {
  RssParser parser(8);
  std::vector<RssItem> received;
  parser.setItemSink([&received](RssItem&& item) {
    received.push_back(std::move(item));
    return true;
  });

  parse(parser,
        "<rss><channel><item><title>x</title><description>teaser12345678</description>"
        "<content:encoded><![CDATA[fullbody123456]]></content:encoded></item></channel></rss>");

  ASSERT_EQ(received.size(), 1u);
  EXPECT_EQ(received[0].body, "fullbody");
  EXPECT_TRUE(received[0].bodyTruncated);
}

TEST(RssParser, SinkFailureAbortsTheCurrentStream) {
  RssParser parser(1024);
  parser.setItemSink([](RssItem&&) { return false; });

  const std::string xml = "<rss><channel><item><title>x</title><description>body</description></item></channel></rss>";
  EXPECT_EQ(parser.write(reinterpret_cast<const uint8_t*>(xml.data()), xml.size()), 0u);
  EXPECT_TRUE(parser.error());
  EXPECT_TRUE(parser.sinkFailed());
}

TEST(RssParser, ReportsFeedOverflowAtTheExplicitSafetyBound) {
  std::string xml = "<rss><channel>";
  for (size_t i = 0; i < RssParser::MAX_ITEMS + 1; ++i) {
    xml += "<item><title>" + std::to_string(i) + "</title><description>x</description></item>";
  }
  xml += "</channel></rss>";

  RssParser parser(8);
  size_t received = 0;
  parser.setItemSink([&received](RssItem&&) {
    ++received;
    return true;
  });
  parse(parser, xml);

  EXPECT_EQ(received, RssParser::MAX_ITEMS);
  EXPECT_EQ(parser.itemCount(), RssParser::MAX_ITEMS);
  EXPECT_TRUE(parser.truncated());
}
