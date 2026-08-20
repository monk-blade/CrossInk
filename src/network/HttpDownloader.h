#pragma once
#include <HalStorage.h>
#include <Stream.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

/**
 * HTTP client utility for fetching content and downloading files.
 * Streams requests through the configured HTTP transport so large downloads
 * do not need to fit in RAM.
 */
class HttpDownloader {
 public:
  using ProgressCallback = std::function<void(size_t downloaded, size_t total)>;
  using CancelCallback = std::function<bool()>;
  // Called with each body chunk as it arrives; return false to abort. Lets a
  // streaming parser consume the response without buffering the whole body.
  using DataCallback = std::function<bool(const uint8_t* data, size_t len)>;
  using Header = std::pair<std::string, std::string>;

  enum DownloadError {
    OK = 0,
    HTTP_ERROR,
    FILE_ERROR,
    ABORTED,
  };

  enum class Transport {
    ESP_HTTP,
    WOLFSSL,
  };

  struct DownloadOptions {
    explicit DownloadOptions(bool preservePartial = false, bool resumePartial = false,
                             CancelCallback shouldCancel = nullptr, size_t bufferSize = 0,
                             Transport transport = Transport::ESP_HTTP)
        : preservePartial(preservePartial),
          resumePartial(resumePartial),
          shouldCancel(std::move(shouldCancel)),
          bufferSize(bufferSize),
          transport(transport) {}

    bool preservePartial;
    bool resumePartial;
    CancelCallback shouldCancel;
    size_t bufferSize;
    Transport transport;
  };

  /**
   * Fetch text content from a URL with optional credentials.
   */
  static bool fetchUrl(const std::string& url, std::string& outContent, const std::string& username = "",
                       const std::string& password = "");

  static bool fetchUrl(const std::string& url, Stream& stream, const std::string& username = "",
                       const std::string& password = "");

  /**
   * Stream the response body to onData as it arrives, without buffering it.
   */
  static bool fetchUrl(const std::string& url, const DataCallback& onData, const std::string& username = "",
                       const std::string& password = "");

  // `shouldCancel`, when provided, is polled during the blocking read loop —
  // callers running on their own task (e.g. a FreshRSS sync) can pass a
  // lambda that re-polls raw input state to let a long-running request be
  // interrupted from the same call stack that started it.
  static bool fetchUrlWithHeaders(const std::string& url, const DataCallback& onData,
                                  const std::vector<Header>& headers, CancelCallback shouldCancel = nullptr);
  static bool postForm(const std::string& url, const std::string& formBody, const DataCallback& onData,
                       const std::vector<Header>& headers = {}, CancelCallback shouldCancel = nullptr);

  // Heap-allocate one SecureHttpClient for a FreshRSS sync burst so TLS
  // keep-alive can skip a handshake per request on ESP32-C3. Call end after
  // the last request and before SD fonts are reloaded.
  static void beginFreshRssSession();
  static void endFreshRssSession();

  /**
   * Stream a URL with cancellation/progress support and a detailed result.
   */
  static DownloadError streamUrl(const std::string& url, const DataCallback& onData,
                                 ProgressCallback progress = nullptr, const std::string& username = "",
                                 const std::string& password = "", DownloadOptions options = DownloadOptions());

  /**
   * Download a file to the SD card with optional credentials.
   */
  static DownloadError downloadToFile(const std::string& url, const std::string& destPath,
                                      ProgressCallback progress = nullptr, bool* cancelFlag = nullptr,
                                      const std::string& username = "", const std::string& password = "",
                                      DownloadOptions options = DownloadOptions());
};
