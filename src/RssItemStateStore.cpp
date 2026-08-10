#include "RssItemStateStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Serialization.h>

#include <algorithm>
#include <functional>
#include <utility>

namespace {
constexpr uint8_t RSS_STATE_FILE_VERSION = 3;
constexpr uint8_t RSS_STATE_FILE_VERSION_LEGACY = 1;
constexpr uint8_t RSS_STATE_FILE_VERSION_WITH_STARS = 2;
constexpr const char* STATE_DIR = "/.crosspoint/rss";
constexpr const char* STATE_PATH = "/.crosspoint/rss/state.bin";
constexpr const char* STATE_TMP_PATH = "/.crosspoint/rss/state.bin.tmp";
constexpr uint32_t MAX_PERSISTED_KEYS_PER_FEED = 200;
}  // namespace

RssItemStateStore& RssItemStateStore::getInstance() {
  static RssItemStateStore instance;
  return instance;
}

uint32_t RssItemStateStore::itemKey(const std::string& id, const std::string& link, const std::string& title) {
  const std::string& basis = !id.empty() ? id : (!link.empty() ? link : title);
  return static_cast<uint32_t>(std::hash<std::string>{}(basis));
}

void RssItemStateStore::load() {
  feedStates.clear();
  loaded = true;
  dirty = false;

  HalFile f;
  if (!Storage.openFileForRead("RSS", STATE_PATH, f)) {
    return;  // No saved state yet — expected on first use.
  }

  uint8_t version = 0;
  serialization::readPod(f, version);
  if (version != RSS_STATE_FILE_VERSION && version != RSS_STATE_FILE_VERSION_WITH_STARS &&
      version != RSS_STATE_FILE_VERSION_LEGACY) {
    LOG_DBG("RSS", "state.bin version mismatch (%u != %u), discarding", version, RSS_STATE_FILE_VERSION);
    return;
  }

  uint32_t feedCount = 0;
  serialization::readPod(f, feedCount);
  for (uint32_t i = 0; i < feedCount && i < MAX_FEEDS_TRACKED; ++i) {
    std::string feedUrl;
    serialization::readString(f, feedUrl);
    FeedState state;
    uint32_t readKeyCount = 0;
    serialization::readPod(f, readKeyCount);
    if (readKeyCount > MAX_PERSISTED_KEYS_PER_FEED) {
      LOG_DBG("RSS", "state.bin read-key count too large (%lu), discarding", static_cast<unsigned long>(readKeyCount));
      return;
    }
    state.readKeys.reserve(readKeyCount);
    for (uint32_t k = 0; k < readKeyCount; ++k) {
      uint32_t key = 0;
      serialization::readPod(f, key);
      if (state.readKeys.size() < MAX_READ_KEYS_PER_FEED) state.readKeys.push_back(key);
    }
    if (version >= RSS_STATE_FILE_VERSION_WITH_STARS) {
      uint32_t starredKeyCount = 0;
      serialization::readPod(f, starredKeyCount);
      if (starredKeyCount > MAX_PERSISTED_KEYS_PER_FEED) {
        LOG_DBG("RSS", "state.bin star-key count too large (%lu), discarding", static_cast<unsigned long>(starredKeyCount));
        return;
      }
      state.starredKeys.reserve(starredKeyCount);
      for (uint32_t k = 0; k < starredKeyCount; ++k) {
        uint32_t key = 0;
        serialization::readPod(f, key);
        if (state.starredKeys.size() < MAX_READ_KEYS_PER_FEED) state.starredKeys.push_back(key);
      }
    }
    if (version >= RSS_STATE_FILE_VERSION) {
      uint32_t queuedKeyCount = 0;
      serialization::readPod(f, queuedKeyCount);
      if (queuedKeyCount > RssItemStateStore::MAX_QUEUE_KEYS_PER_FEED) {
        LOG_DBG("RSS", "state.bin queue count too large (%lu), discarding", static_cast<unsigned long>(queuedKeyCount));
        return;
      }
      state.queuedKeys.reserve(queuedKeyCount);
      for (uint32_t k = 0; k < queuedKeyCount; ++k) {
        uint32_t key = 0;
        serialization::readPod(f, key);
        state.queuedKeys.push_back(key);
      }
    }
    feedStates.emplace(std::move(feedUrl), std::move(state));
  }
  LOG_DBG("RSS", "Loaded read-state for %zu feeds", feedStates.size());
}

bool RssItemStateStore::saveIfDirty() {
  if (!dirty) return true;

  Storage.mkdir(STATE_DIR);
  {
    HalFile f;
    if (!Storage.openFileForWrite("RSS", STATE_TMP_PATH, f)) {
      LOG_ERR("RSS", "Could not open temp state file for write: %s", STATE_TMP_PATH);
      return false;
    }
    serialization::writePod(f, RSS_STATE_FILE_VERSION);
    serialization::writePod(f, static_cast<uint32_t>(feedStates.size()));
    for (const auto& [feedUrl, state] : feedStates) {
      serialization::writeString(f, feedUrl);
      serialization::writePod(f, static_cast<uint32_t>(state.readKeys.size()));
      for (const uint32_t key : state.readKeys) serialization::writePod(f, key);
      serialization::writePod(f, static_cast<uint32_t>(state.starredKeys.size()));
      for (const uint32_t key : state.starredKeys) serialization::writePod(f, key);
      serialization::writePod(f, static_cast<uint32_t>(state.queuedKeys.size()));
      for (const uint32_t key : state.queuedKeys) serialization::writePod(f, key);
    }
    f.flush();
    // f closes at scope exit (DESTRUCTOR_CLOSES_FILE=1) before the rename
    // below — SdFat must not rename a path with an open FsFile.
  }

  // Remove-then-rename (not overwrite-in-place): an interrupted write only
  // ever damages the throwaway .tmp file, matching ProgressFile::writeAtomic.
  Storage.remove(STATE_PATH);
  if (!Storage.rename(STATE_TMP_PATH, STATE_PATH)) {
    LOG_ERR("RSS", "Failed to rename temp state file into place");
    return false;
  }
  dirty = false;
  return true;
}

RssItemStateStore::FeedState* RssItemStateStore::getOrCreateFeedState(const std::string& feedUrl) {
  const auto it = feedStates.find(feedUrl);
  if (it != feedStates.end()) return &it->second;
  if (feedStates.size() >= MAX_FEEDS_TRACKED) return nullptr;
  return &feedStates.emplace(feedUrl, FeedState{}).first->second;
}

bool RssItemStateStore::isRead(const std::string& feedUrl, const uint32_t key) const {
  const auto it = feedStates.find(feedUrl);
  if (it == feedStates.end()) return false;
  const auto& keys = it->second.readKeys;
  return std::find(keys.begin(), keys.end(), key) != keys.end();
}

void RssItemStateStore::markRead(const std::string& feedUrl, const uint32_t key) {
  FeedState* state = getOrCreateFeedState(feedUrl);
  if (!state) return;
  if (std::find(state->readKeys.begin(), state->readKeys.end(), key) != state->readKeys.end()) return;
  state->readKeys.push_back(key);
  if (state->readKeys.size() > MAX_READ_KEYS_PER_FEED) {
    state->readKeys.erase(state->readKeys.begin());  // evict oldest
  }
  dirty = true;
}

bool RssItemStateStore::isStarred(const std::string& feedUrl, const uint32_t key) const {
  const auto it = feedStates.find(feedUrl);
  if (it == feedStates.end()) return false;
  const auto& keys = it->second.starredKeys;
  return std::find(keys.begin(), keys.end(), key) != keys.end();
}

void RssItemStateStore::toggleStar(const std::string& feedUrl, const uint32_t key) {
  FeedState* state = getOrCreateFeedState(feedUrl);
  if (!state) return;
  const auto it = std::find(state->starredKeys.begin(), state->starredKeys.end(), key);
  if (it != state->starredKeys.end()) {
    state->starredKeys.erase(it);
  } else {
    state->starredKeys.push_back(key);
    if (state->starredKeys.size() > MAX_READ_KEYS_PER_FEED) state->starredKeys.erase(state->starredKeys.begin());
  }
  dirty = true;
}

bool RssItemStateStore::isQueued(const std::string& feedUrl, const uint32_t key) const {
  const auto it = feedStates.find(feedUrl);
  if (it == feedStates.end()) return false;
  const auto& keys = it->second.queuedKeys;
  return std::find(keys.begin(), keys.end(), key) != keys.end();
}

bool RssItemStateStore::toggleQueued(const std::string& feedUrl, const uint32_t key) {
  FeedState* state = getOrCreateFeedState(feedUrl);
  if (!state) return false;
  const auto it = std::find(state->queuedKeys.begin(), state->queuedKeys.end(), key);
  if (it != state->queuedKeys.end()) {
    state->queuedKeys.erase(it);
    dirty = true;
    return true;
  }
  if (state->queuedKeys.size() >= MAX_QUEUE_KEYS_PER_FEED) return false;
  state->queuedKeys.push_back(key);
  dirty = true;
  return true;
}

std::vector<uint32_t> RssItemStateStore::loadQueuedIds(const std::string& feedUrl) const {
  const auto it = feedStates.find(feedUrl);
  return it == feedStates.end() ? std::vector<uint32_t>{} : it->second.queuedKeys;
}

size_t RssItemStateStore::countQueued(const std::string& feedUrl) const {
  const auto it = feedStates.find(feedUrl);
  return it == feedStates.end() ? 0 : it->second.queuedKeys.size();
}

void RssItemStateStore::clearQueue() {
  for (auto& [feedUrl, state] : feedStates) state.queuedKeys.clear();
  dirty = true;
}

void RssItemStateStore::clearStars() {
  for (auto& [feedUrl, state] : feedStates) state.starredKeys.clear();
  dirty = true;
}

void RssItemStateStore::clearAll() {
  feedStates.clear();
  dirty = true;
}

size_t RssItemStateStore::countUnread(const std::string& feedUrl, const std::vector<uint32_t>& keys) const {
  const auto it = feedStates.find(feedUrl);
  if (it == feedStates.end()) return keys.size();
  const auto& readKeys = it->second.readKeys;
  size_t unread = 0;
  for (const uint32_t key : keys) {
    if (std::find(readKeys.begin(), readKeys.end(), key) == readKeys.end()) ++unread;
  }
  return unread;
}
