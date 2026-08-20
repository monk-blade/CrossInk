#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

class HttpDownloader {
 public:
  using Header = std::pair<std::string, std::string>;
  using DataCallback = std::function<bool(const uint8_t*, size_t)>;
  struct Request {
    std::string method;
    std::string url;
    std::string body;
    std::vector<Header> headers;
  };
  using Handler = std::function<bool(const Request&, const DataCallback&)>;
  using CancelCallback = std::function<bool()>;
  using ProgressCallback = std::function<void(size_t, size_t)>;

  enum DownloadError {
    OK = 0,
    HTTP_ERROR,
    FILE_ERROR,
    ABORTED,
  };

  static Handler postHandler;
  static Handler getHandler;
  static std::vector<Request> requests;

  static bool postForm(const std::string&, const std::string&, const DataCallback&, const std::vector<Header>& = {},
                       CancelCallback = nullptr);
  static bool fetchUrlWithHeaders(const std::string&, const DataCallback&, const std::vector<Header>&,
                                  CancelCallback = nullptr, bool keepConnection = false);
  static void beginFreshRssSession() {}
  static void endFreshRssSession() {}
  static void releaseFreshRssConnection() {}
  static DownloadError downloadToFileWithHeaders(const std::string&, const std::string&, const std::vector<Header>&,
                                                 ProgressCallback = nullptr, CancelCallback = nullptr,
                                                 bool keepConnection = false) {
    (void)keepConnection;
    return HTTP_ERROR;
  }
  static void reset();
};
