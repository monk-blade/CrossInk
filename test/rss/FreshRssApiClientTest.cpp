#include <gtest/gtest.h>

#include <string>
#include <utility>

#include "FreshRssApiClient.h"
#include "network/HttpDownloader.h"

namespace {
bool emit(const HttpDownloader::DataCallback& callback, const std::string& body) {
  return callback(reinterpret_cast<const uint8_t*>(body.data()), body.size());
}

std::string headerValue(const HttpDownloader::Request& request, const std::string& name) {
  for (const auto& header : request.headers) {
    if (header.first == name) return header.second;
  }
  return {};
}

FreshRssAccount account() { return {"https://reader.example/api/greader.php", "reader", "api-secret"}; }
}  // namespace

class FreshRssApiClientTest : public testing::Test {
 protected:
  void SetUp() override { HttpDownloader::reset(); }
  void TearDown() override { HttpDownloader::reset(); }
};

TEST_F(FreshRssApiClientTest, AuthenticatesPagesAndUsesOnlyReadOnlyEndpoints) {
  HttpDownloader::postHandler = [](const HttpDownloader::Request& request, const HttpDownloader::DataCallback& callback) {
    EXPECT_EQ(request.url, "https://reader.example/api/greader.php/accounts/ClientLogin");
    EXPECT_NE(request.body.find("Email=reader"), std::string::npos);
    EXPECT_NE(request.body.find("Passwd=api-secret"), std::string::npos);
    return emit(callback, "SID=ignored\nAuth=auth-value\n");
  };
  HttpDownloader::getHandler = [](const HttpDownloader::Request& request, const HttpDownloader::DataCallback& callback) {
    EXPECT_EQ(headerValue(request, "Authorization"), "GoogleLogin auth=auth-value");
    EXPECT_EQ(headerValue(request, "Accept"), "application/json");
    if (request.url.find("/reader/api/0/subscription/list?output=json") != std::string::npos)
      return emit(callback, R"({"subscriptions":[{"id":"feed/one","title":"One"}]})");
    if (request.url.find("/reader/api/0/tag/list?output=json") != std::string::npos)
      return emit(callback, R"({"tags":[{"id":"user/-/label/Tech","label":"Tech"}]})");
    if (request.url.find("c=next-page") != std::string::npos)
      return emit(callback, R"({"items":[{"id":"article-2","title":"Two"}]})");
    EXPECT_NE(request.url.find("/reader/api/0/stream/contents/reading-list?output=json&n=25"), std::string::npos);
    return emit(callback, R"({"continuation":"next-page","items":[{"id":"article-1","title":"One"}]})");
  };

  FreshRssApiClient client(account());
  FreshRssMetadata metadata;
  std::vector<FreshRssArticle> articles;
  std::string auth;
  std::string error;
  ASSERT_TRUE(client.fetchMetadata(metadata, auth, error));
  ASSERT_TRUE(client.fetchArticles(auth, 1000,
                                   [&articles](FreshRssArticle&& article) {
                                     articles.push_back(std::move(article));
                                     return true;
                                   },
                                   error));
  ASSERT_EQ(metadata.subscriptions.size(), 1U);
  ASSERT_EQ(metadata.tags.size(), 1U);
  ASSERT_EQ(articles.size(), 2U);
  ASSERT_EQ(HttpDownloader::requests.size(), 5U);
  for (const auto& request : HttpDownloader::requests) {
    if (request.method == "POST") {
      EXPECT_NE(request.url.find("ClientLogin"), std::string::npos);
    } else {
      EXPECT_TRUE(request.url.find("/reader/api/0/subscription/list") != std::string::npos ||
                  request.url.find("/reader/api/0/tag/list") != std::string::npos ||
                  request.url.find("/reader/api/0/stream/contents/reading-list") != std::string::npos);
    }
    EXPECT_EQ(request.url.find("/token"), std::string::npos);
    EXPECT_EQ(request.url.find("edit-tag"), std::string::npos);
    EXPECT_EQ(request.url.find("stream/items/ids"), std::string::npos);
  }
  EXPECT_EQ(auth, "auth-value");
}

TEST_F(FreshRssApiClientTest, EnforcesArticleLimitWhenAResponseIsLargerThanRequested) {
  HttpDownloader::postHandler = [](const HttpDownloader::Request&, const HttpDownloader::DataCallback& callback) {
    return emit(callback, "Auth=auth\n");
  };
  HttpDownloader::getHandler = [](const HttpDownloader::Request& request, const HttpDownloader::DataCallback& callback) {
    if (request.url.find("/reader/api/0/subscription/list") != std::string::npos)
      return emit(callback, R"({"subscriptions":[]})");
    if (request.url.find("/reader/api/0/tag/list") != std::string::npos) return emit(callback, R"({"tags":[]})");
    std::string response = R"({"items":[)";
    for (int i = 0; i < 1005; ++i) {
      if (i != 0) response += ',';
      response += "{\"id\":\"article-" + std::to_string(i) + "\"}";
    }
    response += "]}";
    return emit(callback, response);
  };

  FreshRssApiClient client(account());
  FreshRssMetadata metadata;
  size_t admitted = 0;
  std::string auth;
  std::string error;
  ASSERT_TRUE(client.fetchMetadata(metadata, auth, error));
  ASSERT_TRUE(client.fetchArticles(auth, 1000, [&admitted](FreshRssArticle&&) {
    ++admitted;
    return true;
  }, error));
  EXPECT_EQ(admitted, 1000U);
}

TEST_F(FreshRssApiClientTest, RejectsFailedLoginAndMalformedAuth) {
  HttpDownloader::postHandler = [](const HttpDownloader::Request&, const HttpDownloader::DataCallback& callback) {
    return emit(callback, "Error=Invalid credentials\n");
  };
  FreshRssApiClient client(account());
  FreshRssMetadata metadata;
  std::string auth;
  std::string error;
  EXPECT_FALSE(client.fetchMetadata(metadata, auth, error));
  EXPECT_NE(error.find("no Auth"), std::string::npos);
  EXPECT_EQ(HttpDownloader::requests.size(), 1U);
}

TEST_F(FreshRssApiClientTest, RejectsMalformedJson) {
  HttpDownloader::postHandler = [](const HttpDownloader::Request&, const HttpDownloader::DataCallback& callback) {
    return emit(callback, "Auth=auth\n");
  };
  HttpDownloader::getHandler = [](const HttpDownloader::Request&, const HttpDownloader::DataCallback& callback) {
    return emit(callback, "{not-json");
  };
  FreshRssApiClient client(account());
  FreshRssMetadata metadata;
  std::string auth;
  std::string error;
  EXPECT_FALSE(client.fetchMetadata(metadata, auth, error));
  EXPECT_NE(error.find("could not be parsed"), std::string::npos);
}

TEST_F(FreshRssApiClientTest, RequestsModifiedSinceCursorWithoutMutationEndpoints) {
  HttpDownloader::getHandler = [](const HttpDownloader::Request& request,
                                  const HttpDownloader::DataCallback& callback) {
    EXPECT_NE(request.url.find("reading-list?output=json&n=25&ot=1785751200"), std::string::npos);
    EXPECT_EQ(request.url.find("/token"), std::string::npos);
    return emit(callback, R"({"items":[{"id":"delta","crawlTimeMsec":1785751300000}]})");
  };
  FreshRssApiClient client(account());
  FreshRssSyncCursor cursor;
  cursor.modifiedMsec = 1785751200000ULL;
  cursor.valid = true;
  std::vector<FreshRssArticle> articles;
  std::string error;
  bool unsupported = false;
  ASSERT_TRUE(client.fetchArticles("auth", 200,
                                   [&articles](FreshRssArticle&& item) {
                                     articles.push_back(std::move(item));
                                     return true;
                                   },
                                   error, {}, &cursor, &unsupported));
  ASSERT_EQ(articles.size(), 1U);
  EXPECT_EQ(articles[0].modifiedMsec, 1785751300000ULL);
  EXPECT_FALSE(unsupported);
}
