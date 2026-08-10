#include "RssItemCache.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <utility>

namespace {
constexpr uint8_t RSS_ITEMS_FILE_VERSION = 4;
constexpr uint8_t RSS_ITEMS_FILE_VERSION_LEGACY = 3;
constexpr uint8_t RSS_ITEMS_FILE_VERSION_OLD = 2;
constexpr uint8_t RSS_ITEM_BODY_FILE_VERSION = 1;
constexpr uint32_t RSS_COMMIT_MAGIC = 0x52535334;  // RSS4
constexpr size_t MAX_CACHED_ITEMS = 64;
constexpr size_t MAX_TITLE_CHARS = 200;
constexpr size_t MAX_LINK_CHARS = 768;
constexpr size_t MAX_DATE_CHARS = 64;
constexpr size_t MAX_WORD_CHARS = 32768;
constexpr size_t MAX_BODY_TEXT_BYTES = 32 * 1024;
constexpr uint32_t MAX_PARAGRAPHS = 2000;
constexpr uint32_t MAX_WORDS_PER_PARAGRAPH = 2000;
constexpr char RSS_ROOT[] = "/.crosspoint/rss";
constexpr char RSS_MANIFEST_PATH[] = "/.crosspoint/rss/manifest.bin";
constexpr char RSS_MANIFEST_TMP_PATH[] = "/.crosspoint/rss/manifest.bin.tmp";

std::string itemsBinPath(const std::string& feedUrl) { return RssItemCache::cacheDirFor(feedUrl) + "/items.bin"; }
std::string itemsBinTmpPath(const std::string& feedUrl) {
  return RssItemCache::cacheDirFor(feedUrl) + "/items.bin.tmp";
}
std::string itemsBinBackupPath(const std::string& feedUrl) {
  return RssItemCache::cacheDirFor(feedUrl) + "/items.bin.bak";
}
std::string itemBodyPath(const std::string& feedUrl, const uint32_t key) {
  return RssItemCache::cacheDirFor(feedUrl) + "/item_" + std::to_string(key) + ".bin";
}

template <typename T>
bool writePod(HalFile& file, const T& value) {
  return file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(T)) == sizeof(T);
}

template <typename T>
bool readPod(HalFile& file, T& value) {
  return file.read(reinterpret_cast<uint8_t*>(&value), sizeof(T)) == sizeof(T);
}

bool hasBytes(HalFile& file, const size_t count) {
  const size_t size = file.fileSize();
  const size_t position = file.position();
  return position <= size && count <= size - position;
}

bool writeString(HalFile& file, const std::string& value) {
  if (value.size() > std::numeric_limits<uint32_t>::max()) return false;
  const uint32_t length = static_cast<uint32_t>(value.size());
  if (!writePod(file, length)) return false;
  return length == 0 || file.write(reinterpret_cast<const uint8_t*>(value.data()), length) == length;
}

bool readString(HalFile& file, std::string& value, const size_t maxLength) {
  uint32_t length = 0;
  if (!readPod(file, length) || length > maxLength || !hasBytes(file, length)) return false;
  value.resize(length);
  return length == 0 || static_cast<size_t>(file.read(value.data(), length)) == length;
}

bool skipBytes(HalFile& file, const size_t count) {
  return hasBytes(file, count) && file.seekCur(static_cast<int64_t>(count));
}

bool writeRichText(HalFile& file, const RichText& body) {
  if (body.size() > MAX_PARAGRAPHS) return false;
  if (!writePod(file, static_cast<uint32_t>(body.size()))) return false;
  size_t textBytes = 0;
  for (const auto& paragraph : body) {
    if (paragraph.words.size() > MAX_WORDS_PER_PARAGRAPH ||
        static_cast<uint8_t>(paragraph.align) > static_cast<uint8_t>(TextAlign::JUSTIFY)) {
      return false;
    }
    if (!writePod(file, static_cast<uint8_t>(paragraph.align)) ||
        !writePod(file, static_cast<uint32_t>(paragraph.words.size()))) {
      return false;
    }
    for (const auto& word : paragraph.words) {
      if (word.text.size() > MAX_WORD_CHARS || textBytes > MAX_BODY_TEXT_BYTES -
                                                    std::min(MAX_BODY_TEXT_BYTES, word.text.size())) {
        return false;
      }
      textBytes += word.text.size();
      const auto style = static_cast<uint8_t>(word.style);
      if (style > 3 || !writePod(file, style) ||
          !writePod(file, static_cast<uint8_t>(word.continuesPrevious ? 1 : 0)) ||
          !writeString(file, word.text)) {
        return false;
      }
    }
  }
  return true;
}

bool readRichText(HalFile& file, RichText& body, size_t* textBytes = nullptr) {
  body.clear();
  if (textBytes) *textBytes = 0;

  uint32_t paragraphCount = 0;
  if (!readPod(file, paragraphCount) || paragraphCount > MAX_PARAGRAPHS) return false;
  body.reserve(paragraphCount);
  size_t totalTextBytes = 0;
  for (uint32_t p = 0; p < paragraphCount; ++p) {
    RichParagraph paragraph;
    uint8_t align = 0;
    uint32_t wordCount = 0;
    if (!readPod(file, align) || align > static_cast<uint8_t>(TextAlign::JUSTIFY) || !readPod(file, wordCount) ||
        wordCount > MAX_WORDS_PER_PARAGRAPH) {
      body.clear();
      return false;
    }
    paragraph.align = static_cast<TextAlign>(align);
    paragraph.words.reserve(wordCount);
    for (uint32_t w = 0; w < wordCount; ++w) {
      StyledWord word;
      uint8_t style = 0;
      uint8_t continues = 0;
      if (!readPod(file, style) || style > 3 || !readPod(file, continues) ||
          continues > 1 ||
          !readString(file, word.text, MAX_WORD_CHARS) ||
          word.text.size() > MAX_BODY_TEXT_BYTES - std::min(MAX_BODY_TEXT_BYTES, totalTextBytes)) {
        body.clear();
        return false;
      }
      totalTextBytes += word.text.size();
      word.style = static_cast<EpdFontFamily::Style>(style);
      word.continuesPrevious = continues != 0;
      paragraph.words.push_back(std::move(word));
    }
    body.push_back(std::move(paragraph));
  }
  if (textBytes) *textBytes = totalTextBytes;
  return true;
}

bool skipRichText(HalFile& file, size_t* textBytes = nullptr) {
  if (textBytes) *textBytes = 0;
  uint32_t paragraphCount = 0;
  if (!readPod(file, paragraphCount) || paragraphCount > MAX_PARAGRAPHS) return false;
  size_t totalTextBytes = 0;
  for (uint32_t p = 0; p < paragraphCount; ++p) {
    uint8_t align = 0;
    uint32_t wordCount = 0;
    if (!readPod(file, align) || align > static_cast<uint8_t>(TextAlign::JUSTIFY) || !readPod(file, wordCount) ||
        wordCount > MAX_WORDS_PER_PARAGRAPH) {
      return false;
    }
    for (uint32_t w = 0; w < wordCount; ++w) {
      uint8_t style = 0;
      uint8_t continues = 0;
      uint32_t length = 0;
      if (!readPod(file, style) || style > 3 || !readPod(file, continues) || continues > 1 ||
          !readPod(file, length) ||
          length > MAX_WORD_CHARS || length > MAX_BODY_TEXT_BYTES - std::min(MAX_BODY_TEXT_BYTES, totalTextBytes) ||
          !skipBytes(file, length)) {
        return false;
      }
      totalTextBytes += length;
    }
  }
  if (textBytes) *textBytes = totalTextBytes;
  return true;
}

struct ReadHeader {
  uint8_t version = 0;
  uint32_t itemCount = 0;
  std::vector<uint32_t> keys;
};

bool readHeader(HalFile& file, ReadHeader& header) {
  if (!readPod(file, header.version) ||
      (header.version != RSS_ITEMS_FILE_VERSION && header.version != RSS_ITEMS_FILE_VERSION_LEGACY &&
       header.version != RSS_ITEMS_FILE_VERSION_OLD) ||
      !readPod(file, header.itemCount) || header.itemCount > MAX_CACHED_ITEMS) {
    return false;
  }

  const size_t keyCount = header.version == RSS_ITEMS_FILE_VERSION ? MAX_CACHED_ITEMS : header.itemCount;
  header.keys.resize(keyCount);
  for (size_t i = 0; i < keyCount; ++i) {
    if (!readPod(file, header.keys[i])) return false;
  }
  if (header.version == RSS_ITEMS_FILE_VERSION) header.keys.resize(header.itemCount);
  return true;
}

bool readMetadata(HalFile& file, CachedRssItem& item, const uint8_t version) {
  if (!readString(file, item.title, MAX_TITLE_CHARS) || !readString(file, item.link, MAX_LINK_CHARS) ||
      !readString(file, item.date, MAX_DATE_CHARS)) {
    return false;
  }
  if (version == RSS_ITEMS_FILE_VERSION_OLD) {
    item.bodyTruncated = true;
    return true;
  }
  uint8_t truncated = 0;
  if (!readPod(file, truncated) || truncated > 1) return false;
  item.bodyTruncated = truncated != 0;
  return true;
}

bool readCommitMarker(HalFile& file, const uint8_t version) {
  if (version != RSS_ITEMS_FILE_VERSION) return file.available() == 0;
  uint32_t marker = 0;
  return readPod(file, marker) && marker == RSS_COMMIT_MAGIC && file.available() == 0;
}

void recoverSnapshot(const std::string& feedUrl) {
  const std::string finalPath = itemsBinPath(feedUrl);
  const std::string backupPath = itemsBinBackupPath(feedUrl);
  const std::string tempPath = itemsBinTmpPath(feedUrl);
  if (!Storage.exists(finalPath.c_str()) && Storage.exists(backupPath.c_str())) {
    Storage.rename(backupPath.c_str(), finalPath.c_str());
  } else if (Storage.exists(finalPath.c_str()) && Storage.exists(backupPath.c_str())) {
    Storage.remove(backupPath.c_str());
  }
  if (Storage.exists(finalPath.c_str()) && Storage.exists(tempPath.c_str())) {
    Storage.remove(tempPath.c_str());
  } else if (!Storage.exists(finalPath.c_str()) && !Storage.exists(backupPath.c_str()) && Storage.exists(tempPath.c_str())) {
    Storage.remove(tempPath.c_str());
  }
}

bool loadLegacyOverlay(const std::string& feedUrl, const uint32_t key, RichText& body, bool& complete) {
  HalFile file;
  if (!Storage.openFileForRead("RSS", itemBodyPath(feedUrl, key), file)) return false;
  uint8_t version = 0;
  uint8_t truncated = 0;
  if (!readPod(file, version) || version != RSS_ITEM_BODY_FILE_VERSION || !readPod(file, truncated) || truncated > 1 ||
      !readRichText(file, body) || file.available() != 0) {
    body.clear();
    return false;
  }
  complete = truncated == 0;
  return true;
}

void cleanLegacyFeedFiles(const std::string& feedUrl) {
  const std::string directory = RssItemCache::cacheDirFor(feedUrl);
  auto dir = Storage.open(directory.c_str());
  if (!dir || !dir.isDirectory()) return;

  char name[128];
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    entry.getName(name, sizeof(name));
    const std::string child(name);
    entry.close();
    if (child.rfind("item_", 0) == 0 && child.size() > 9 && child.compare(child.size() - 4, 4, ".bin") == 0) {
      Storage.remove((directory + "/" + child).c_str());
    }
  }
  dir.close();
}

bool loadBodyRecords(const std::string& feedUrl, std::vector<CachedRssItem>& outItems, const bool bodies) {
  recoverSnapshot(feedUrl);
  HalFile file;
  if (!Storage.openFileForRead("RSS", itemsBinPath(feedUrl), file)) return false;

  ReadHeader header;
  if (!readHeader(file, header)) return false;
  std::vector<CachedRssItem> loaded;
  loaded.reserve(header.itemCount);
  for (uint32_t i = 0; i < header.itemCount; ++i) {
    CachedRssItem item;
    item.key = header.keys[i];
    if (!readMetadata(file, item, header.version)) return false;
    if (bodies) {
      if (!readRichText(file, item.body)) return false;
    } else if (!skipRichText(file)) {
      return false;
    }
    loaded.push_back(std::move(item));
  }
  if (!readCommitMarker(file, header.version)) return false;
  outItems = std::move(loaded);
  return true;
}

bool collectStatsFromPath(const std::string& path, RssItemCache::CacheStats& stats) {
  HalFile file;
  if (!Storage.openFileForRead("RSS", path, file)) return false;
  const uint64_t fileBytes = file.fileSize64();
  ReadHeader header;
  if (!readHeader(file, header)) return false;
  for (uint32_t i = 0; i < header.itemCount; ++i) {
    CachedRssItem item;
    item.key = header.keys[i];
    if (!readMetadata(file, item, header.version) || !skipRichText(file)) return false;
    ++stats.itemCount;
    if (item.bodyTruncated)
      ++stats.truncatedBodies;
    else
      ++stats.completeBodies;
  }
  if (!readCommitMarker(file, header.version)) return false;
  stats.bytes += fileBytes;
  return true;
}
}  // namespace

namespace RssItemCache {

std::string cacheDirFor(const std::string& feedUrl) {
  return std::string(RSS_ROOT) + "/feed_" + std::to_string(std::hash<std::string>{}(feedUrl));
}

FeedWriteSession::~FeedWriteSession() { abort(); }

bool FeedWriteSession::begin() {
  if (open) return false;
  recoverSnapshot(feedUrl);
  const std::string directory = cacheDirFor(feedUrl);
  tempPath = itemsBinTmpPath(feedUrl);
  Storage.mkdir(RSS_ROOT);
  Storage.mkdir(directory.c_str());
  Storage.remove(tempPath.c_str());
  if (!Storage.openFileForWrite("RSS", tempPath, file)) {
    failedState = true;
    return false;
  }
  open = true;

  keys.clear();
  keys.reserve(MAX_CACHED_ITEMS);
  const uint8_t version = RSS_ITEMS_FILE_VERSION;
  const uint32_t itemCount = 0;
  if (!writePod(file, version) || !writePod(file, itemCount)) {
    abort();
    return false;
  }
  for (size_t i = 0; i < MAX_CACHED_ITEMS; ++i) {
    const uint32_t key = 0;
    if (!writePod(file, key)) {
      abort();
      return false;
    }
  }
  failedState = false;
  return true;
}

bool FeedWriteSession::append(const CachedRssItem& item) {
  if (!open || failedState || keys.size() >= MAX_CACHED_ITEMS) {
    failedState = true;
    return false;
  }
  if (item.title.size() > MAX_TITLE_CHARS || item.link.size() > MAX_LINK_CHARS || item.date.size() > MAX_DATE_CHARS) {
    failedState = true;
    return false;
  }
  const uint8_t truncated = item.bodyTruncated ? 1 : 0;
  if (!writeString(file, item.title) || !writeString(file, item.link) ||
      !writeString(file, item.date) || !writePod(file, truncated) || !writeRichText(file, item.body)) {
    failedState = true;
    return false;
  }
  keys.push_back(item.key);
  return true;
}

bool FeedWriteSession::commit() {
  if (!open || failedState) {
    abort();
    return false;
  }

  const uint32_t marker = RSS_COMMIT_MAGIC;
  if (!writePod(file, marker)) {
    abort();
    return false;
  }
  file.flush();
  if (!file.seek(sizeof(uint8_t))) {
    abort();
    return false;
  }
  const uint32_t itemCount = static_cast<uint32_t>(keys.size());
  if (!writePod(file, itemCount)) {
    abort();
    return false;
  }
  for (size_t i = 0; i < MAX_CACHED_ITEMS; ++i) {
    const uint32_t key = i < keys.size() ? keys[i] : 0;
    if (!writePod(file, key)) {
      abort();
      return false;
    }
  }
  file.flush();
  if (!file.close()) {
    failedState = true;
    open = false;
    Storage.remove(tempPath.c_str());
    return false;
  }
  open = false;

  const std::string finalPath = itemsBinPath(feedUrl);
  const std::string backupPath = itemsBinBackupPath(feedUrl);
  Storage.remove(backupPath.c_str());
  if (Storage.exists(finalPath.c_str()) && !Storage.rename(finalPath.c_str(), backupPath.c_str())) {
    Storage.remove(tempPath.c_str());
    failedState = true;
    return false;
  }
  if (!Storage.rename(tempPath.c_str(), finalPath.c_str())) {
    if (Storage.exists(backupPath.c_str())) Storage.rename(backupPath.c_str(), finalPath.c_str());
    Storage.remove(tempPath.c_str());
    failedState = true;
    return false;
  }
  Storage.remove(backupPath.c_str());
  // v4 contains every body in the committed snapshot. The old lazy-fetch
  // overlays and LRU manifest are no longer consulted by new caches.
  cleanLegacyFeedFiles(feedUrl);
  Storage.remove(RSS_MANIFEST_PATH);
  Storage.remove(RSS_MANIFEST_TMP_PATH);
  return true;
}

void FeedWriteSession::abort() {
  if (open) file.close();
  open = false;
  if (!tempPath.empty()) Storage.remove(tempPath.c_str());
}

bool exists(const std::string& feedUrl) {
  recoverSnapshot(feedUrl);
  std::vector<CachedRssItem> metadata;
  return loadBodyRecords(feedUrl, metadata, false);
}

bool loadIndex(const std::string& feedUrl, std::vector<CachedRssItem>& outItems) {
  return loadBodyRecords(feedUrl, outItems, false);
}

bool load(const std::string& feedUrl, std::vector<CachedRssItem>& outItems) {
  return loadBodyRecords(feedUrl, outItems, true);
}

bool loadItemBody(const std::string& feedUrl, const uint32_t key, RichText& outBody, bool& complete) {
  outBody.clear();
  complete = false;
  recoverSnapshot(feedUrl);

  HalFile file;
  if (!Storage.openFileForRead("RSS", itemsBinPath(feedUrl), file)) return false;
  uint8_t version = 0;
  if (!readPod(file, version)) return false;
  if (!file.seek(0)) return false;

  // v2/v3 caches may contain a full article overlay produced by the previous
  // lazy-fetch implementation. v4 snapshots are self-contained and must not
  // be shadowed by legacy files.
  if (version == RSS_ITEMS_FILE_VERSION_LEGACY || version == RSS_ITEMS_FILE_VERSION_OLD) {
    if (loadLegacyOverlay(feedUrl, key, outBody, complete)) return true;
  }

  ReadHeader header;
  if (!readHeader(file, header)) return false;
  bool found = false;
  bool truncated = false;
  RichText foundBody;
  for (uint32_t i = 0; i < header.itemCount; ++i) {
    CachedRssItem metadata;
    metadata.key = header.keys[i];
    if (!readMetadata(file, metadata, header.version)) return false;
    if (metadata.key == key) {
      if (!readRichText(file, foundBody)) return false;
      found = true;
      truncated = metadata.bodyTruncated;
    } else if (!skipRichText(file)) {
      return false;
    }
  }
  if (!readCommitMarker(file, header.version) || !found) return false;
  outBody = std::move(foundBody);
  complete = !truncated;
  return true;
}

std::vector<uint32_t> peekKeys(const std::string& feedUrl) {
  std::vector<CachedRssItem> metadata;
  if (!loadBodyRecords(feedUrl, metadata, false)) return {};
  std::vector<uint32_t> keys;
  keys.reserve(metadata.size());
  for (const auto& item : metadata) keys.push_back(item.key);
  return keys;
}

CacheStats stats() {
  CacheStats result;
  auto root = Storage.open(RSS_ROOT);
  if (!root || !root.isDirectory()) return result;

  char name[128];
  for (auto entry = root.openNextFile(); entry; entry = root.openNextFile()) {
    entry.getName(name, sizeof(name));
    const std::string child(name);
    if (!entry.isDirectory() || child.rfind("feed_", 0) != 0) {
      entry.close();
      continue;
    }
    entry.close();
    collectStatsFromPath(std::string(RSS_ROOT) + "/" + child + "/items.bin", result);
  }
  root.close();
  return result;
}

bool clearAll() {
  bool ok = true;
  auto root = Storage.open(RSS_ROOT);
  if (root && root.isDirectory()) {
    char name[128];
    for (auto entry = root.openNextFile(); entry; entry = root.openNextFile()) {
      entry.getName(name, sizeof(name));
      const std::string child(name);
      if (entry.isDirectory() && child.rfind("feed_", 0) == 0) {
        entry.close();
        ok = Storage.removeDir((std::string(RSS_ROOT) + "/" + child).c_str()) && ok;
      } else {
        entry.close();
      }
    }
    root.close();
  }
  if (Storage.exists(RSS_MANIFEST_PATH)) ok = Storage.remove(RSS_MANIFEST_PATH) && ok;
  Storage.remove(RSS_MANIFEST_TMP_PATH);
  return ok;
}

bool save(const std::string& feedUrl, const std::vector<CachedRssItem>& items) {
  FeedWriteSession writer(feedUrl);
  if (!writer.begin()) return false;
  for (const auto& item : items) {
    if (!writer.append(item)) {
      writer.abort();
      return false;
    }
  }
  return writer.commit();
}

}  // namespace RssItemCache
