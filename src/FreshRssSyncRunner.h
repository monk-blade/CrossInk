#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <string>

struct FreshRssSyncProgress {
  std::atomic<size_t> received{0};
  std::atomic<size_t> limit{0};
  std::atomic<bool> processingArticle{false};
};

struct FreshRssSyncHost {
  FreshRssSyncProgress& progress;
  std::function<void(const char* message)> setStatus;
  std::function<void(bool force)> paintProgress;
};

// Downloads and commits a FreshRSS article snapshot. Returns false on failure
// and sets error when provided.
bool runFreshRssSync(FreshRssSyncHost& host, std::string& error);
