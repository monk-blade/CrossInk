#pragma once

#include <cstdint>

#include <string>
#include <vector>

// A few feeds exposed through FreshRSS publish the canonical URL as the
// Google Reader `title`. Keep those records readable without fetching the
// linked page: turn the final URL slug into a compact display title. Real
// titles always win and are returned unchanged.
inline std::string freshRssDisplayTitle(const std::string& title, const std::string& link) {
  const bool looksLikeUrl = title.empty() || title.rfind("http://", 0) == 0 || title.rfind("https://", 0) == 0;
  if (!looksLikeUrl) return title;

  std::string source = title.empty() ? link : title;
  const size_t query = source.find_first_of("?#");
  if (query != std::string::npos) source.resize(query);
  while (!source.empty() && source.back() == '/') source.pop_back();
  const size_t slash = source.find_last_of('/');
  if (slash != std::string::npos) source.erase(0, slash + 1);
  const size_t extension = source.rfind('.');
  if (extension != std::string::npos && extension > 0) source.resize(extension);

  // News sites commonly append an internal numeric entry ID to the slug.
  const size_t idSeparator = source.find_last_of('-');
  if (idSeparator != std::string::npos && source.size() - idSeparator > 4) {
    bool numericId = true;
    for (size_t i = idSeparator + 1; i < source.size(); ++i) {
      if (source[i] < '0' || source[i] > '9') {
        numericId = false;
        break;
      }
    }
    if (numericId) source.resize(idSeparator);
  }

  for (char& character : source) {
    if (character == '-' || character == '_' || character == '+') character = ' ';
  }
  std::string result;
  bool previousSpace = true;
  for (const char character : source) {
    const bool space = character == ' ' || character == '\t';
    if (space) {
      if (!previousSpace) result.push_back(' ');
    } else {
      result.push_back(character);
    }
    previousSpace = space;
  }
  while (!result.empty() && result.back() == ' ') result.pop_back();
  if (!result.empty() && result[0] >= 'a' && result[0] <= 'z') result[0] = static_cast<char>(result[0] - 'a' + 'A');
  return result.empty() ? (title.empty() ? link : title) : result;
}

struct FreshRssAccount {
  std::string apiUrl;
  std::string username;
  std::string password;
};

// The cursor is committed with the cache, never before a replacement
// snapshot is durable.  modifiedMsec is the Google Reader crawl timestamp;
// the other fields make a cursor unusable after an account or limit change.
struct FreshRssSyncCursor {
  uint64_t modifiedMsec = 0;
  uint32_t generation = 0;
  uint32_t accountIdentity = 0;
  uint16_t articleLimit = 0;
  bool valid = false;
};

enum class FreshRssSyncMode : uint8_t { Full, Delta, FallbackFull };

inline uint32_t freshRssAccountIdentity(const FreshRssAccount& account) {
  // FNV-1a is intentionally small and deterministic; this is an identity
  // guard, not a credential hash, and the password is never included.
  uint32_t hash = 2166136261u;
  const std::string identity = account.apiUrl + "\n" + account.username;
  for (const unsigned char value : identity) {
    hash ^= value;
    hash *= 16777619u;
  }
  return hash;
}

struct FreshRssSubscription {
  std::string id;
  std::string title;
  std::string htmlUrl;
  std::vector<std::string> categoryIds;
};

struct FreshRssTag {
  std::string id;
  std::string label;
};

struct FreshRssMetadata {
  std::vector<FreshRssSubscription> subscriptions;
  std::vector<FreshRssTag> tags;
};

struct FreshRssArticle {
  std::string id;
  std::string title;
  std::string author;
  std::string origin;
  std::string originUrl;
  std::string streamId;
  std::string link;
  std::string date;
  uint64_t modifiedMsec = 0;
  std::vector<std::string> categoryIds;
  std::string contentHtml;
  std::string summaryHtml;
  bool bodyTruncated = false;
};
