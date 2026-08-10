#include "FreshRssAccountStore.h"

#include <Logging.h>
#include <ObfuscationUtils.h>

#include <algorithm>

namespace {
void trim(std::string& value) {
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '\n' || value.front() == '\r'))
    value.erase(value.begin());
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\n' || value.back() == '\r'))
    value.pop_back();
}
}  // namespace

std::string FreshRssAccountStore::normalizeApiUrl(std::string url) {
  trim(url);
  while (url.size() > 1 && url.back() == '/') url.pop_back();
  if (url.empty()) return url;
  if (url.size() >= 11 && url.compare(url.size() - 11, 11, "greader.php") == 0) return url;
  if (url.size() >= 4 && url.compare(url.size() - 4, 4, "/api") == 0) return url + "/greader.php";
  return url + "/api/greader.php";
}

void FreshRssAccountStore::toJson(JsonDocument& doc) const {
  doc["api_url"] = account.apiUrl;
  doc["username"] = account.username;
  doc["password_obf"] = obfuscation::obfuscateToBase64(account.password);
}

bool FreshRssAccountStore::fromJson(JsonVariantConst doc) {
  FreshRssAccount loaded;
  loaded.apiUrl = normalizeApiUrl(doc["api_url"] | (doc["url"] | ""));
  loaded.username = doc["username"] | (doc["user"] | "");
  bool needsResave = false;
  loaded.password = extractPassword(doc, needsResave);
  account = std::move(loaded);
  if (needsResave) requestResave();
  LOG_DBG("FRSS", "Loaded FreshRSS account endpoint=%s user=%s", account.apiUrl.c_str(), account.username.c_str());
  return true;
}

bool FreshRssAccountStore::update(const FreshRssAccount& value) {
  account = value;
  account.apiUrl = normalizeApiUrl(account.apiUrl);
  return saveToFile();
}

bool FreshRssAccountStore::clear() {
  account = {};
  return saveToFile();
}
