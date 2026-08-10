#pragma once

#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>

#include "FreshRssTypes.h"

/** One read-only FreshRSS account, persisted independently from settings.json. */
class FreshRssAccountStore : public PersistableStore<FreshRssAccountStore> {
 private:
  FreshRssAccount account;

  FreshRssAccountStore() = default;
  friend class PersistableStore<FreshRssAccountStore>;

 public:
  static const char* getFilePath() { return "/.crosspoint/freshrss.json"; }

  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  const FreshRssAccount& getAccount() const { return account; }
  bool hasAccount() const { return !account.apiUrl.empty() && !account.username.empty() && !account.password.empty(); }
  bool update(const FreshRssAccount& value);
  bool clear();

  // Accepts a host, an /api path, or the final greader.php endpoint. The API
  // client always uses the normalized result, so the settings screen can be
  // forgiving without creating duplicate endpoint variants.
  static std::string normalizeApiUrl(std::string url);
};

#define FRESHRSS_ACCOUNT FreshRssAccountStore::getInstance()
