#include "FreshRssCache.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Utf8.h>

#include <algorithm>
#include <array>
#include <ctime>
#include <limits>
#include <unordered_set>

namespace {
constexpr uint32_t MAGIC = 0x46525335;  // FRS5
constexpr uint8_t VERSION = 4;
constexpr uint32_t COMMIT = 0x46524331;  // FRC1
constexpr char ROOT[] = "/.crosspoint/freshrss";
constexpr char SNAPSHOT[] = "/.crosspoint/freshrss/snapshot.bin";
constexpr char TEMP[] = "/.crosspoint/freshrss/snapshot.bin.tmp";
constexpr char BACKUP[] = "/.crosspoint/freshrss/snapshot.bin.bak";
constexpr size_t MAX_ID = 256;
constexpr size_t MAX_LABEL = 256;
// New refreshes admit at most MAX_ARTICLES. Keep the previous larger bound
// readable so changing the setting does not discard an already-valid snapshot
// before the user manually refreshes it.
constexpr size_t MAX_SNAPSHOT_ARTICLES = 3000;
constexpr size_t MAX_CATEGORIES = 32;
constexpr size_t MAX_PARAGRAPHS = 2000;
constexpr size_t MAX_WORDS = 2000;
constexpr size_t MAX_WORD_CHARS = 32768;
constexpr size_t MAX_INDEX_CATEGORIES = 128;
constexpr uint8_t INDEX_FLAG_TRUNCATED = 0x01;
constexpr size_t HEADER_INDEX_OFFSET = sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint32_t) * 4;
constexpr size_t HEADER_INDEX_COUNT = HEADER_INDEX_OFFSET + sizeof(uint32_t);
constexpr size_t HEADER_TIMESTAMP = HEADER_INDEX_COUNT + sizeof(uint32_t);
constexpr size_t HEADER_CURSOR_MSEC = HEADER_TIMESTAMP + sizeof(uint32_t);
constexpr size_t HEADER_GENERATION = HEADER_CURSOR_MSEC + sizeof(uint64_t);
constexpr size_t HEADER_ACCOUNT_IDENTITY = HEADER_GENERATION + sizeof(uint32_t);
constexpr size_t HEADER_SIZE = HEADER_ACCOUNT_IDENTITY + sizeof(uint32_t);

struct SnapshotHeader {
  uint8_t version = 0;
  uint32_t articleCount = 0;
  uint32_t subCount = 0;
  uint32_t tagCount = 0;
  uint32_t limit = 0;
  uint32_t indexOffset = 0;
  uint32_t indexCount = 0;
  uint32_t lastRefreshUnix = 0;
  uint64_t cursorModifiedMsec = 0;
  uint32_t generation = 0;
  uint32_t accountIdentity = 0;
};

template <typename T>
bool writePod(HalFile& file, const T& value) {
  return file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(value)) == sizeof(value);
}
template <typename T>
bool readPod(HalFile& file, T& value) {
  return file.read(reinterpret_cast<uint8_t*>(&value), sizeof(value)) == sizeof(value);
}
bool hasBytes(HalFile& file, const size_t count) {
  const size_t size = file.fileSize();
  const size_t pos = file.position();
  return pos <= size && count <= size - pos;
}
bool writeString(HalFile& file, const std::string& value, const size_t max) {
  if (value.size() > max || value.size() > std::numeric_limits<uint32_t>::max()) return false;
  const uint32_t size = static_cast<uint32_t>(value.size());
  return writePod(file, size) && (size == 0 || file.write(reinterpret_cast<const uint8_t*>(value.data()), size) == size);
}
bool readString(HalFile& file, std::string& value, const size_t max) {
  uint32_t size = 0;
  if (!readPod(file, size) || size > max || !hasBytes(file, size)) return false;
  value.resize(size);
  return size == 0 || static_cast<size_t>(file.read(value.data(), size)) == size;
}
bool skipBytes(HalFile& file, const size_t size) { return hasBytes(file, size) && file.seekCur(static_cast<int64_t>(size)); }

bool writeRichText(HalFile& file, const RichText& body) {
  if (body.size() > MAX_PARAGRAPHS) return false;
  size_t totalBytes = 0;
  if (!writePod(file, static_cast<uint32_t>(body.size()))) return false;
  for (const auto& paragraph : body) {
    if (paragraph.words.size() > MAX_WORDS || static_cast<uint8_t>(paragraph.align) > static_cast<uint8_t>(TextAlign::JUSTIFY) ||
        !writePod(file, static_cast<uint8_t>(paragraph.align)) || !writePod(file, static_cast<uint32_t>(paragraph.words.size())))
      return false;
    for (const auto& word : paragraph.words) {
      if (word.text.size() > MAX_WORD_CHARS || word.text.size() > FreshRssCache::MAX_BODY_BYTES -
                                                     std::min(FreshRssCache::MAX_BODY_BYTES, totalBytes))
        return false;
      const uint8_t style = static_cast<uint8_t>(word.style);
      const uint8_t continues = word.continuesPrevious ? 1 : 0;
      if (!writePod(file, style) || !writePod(file, continues) || !writeString(file, word.text, MAX_WORD_CHARS)) return false;
      totalBytes += word.text.size();
    }
  }
  return true;
}
bool readRichText(HalFile& file, RichText& body, bool loadBody) {
  body.clear();
  uint32_t paragraphs = 0;
  if (!readPod(file, paragraphs) || paragraphs > MAX_PARAGRAPHS) return false;
  if (loadBody) body.reserve(paragraphs);
  size_t totalBytes = 0;
  for (uint32_t p = 0; p < paragraphs; ++p) {
    uint8_t align = 0;
    uint32_t words = 0;
    if (!readPod(file, align) || align > static_cast<uint8_t>(TextAlign::JUSTIFY) || !readPod(file, words) || words > MAX_WORDS)
      return false;
    RichParagraph paragraph;
    if (loadBody) {
      paragraph.align = static_cast<TextAlign>(align);
      paragraph.words.reserve(words);
    }
    for (uint32_t w = 0; w < words; ++w) {
      uint8_t style = 0;
      uint8_t continues = 0;
      uint32_t length = 0;
      if (!readPod(file, style) || style > 3 || !readPod(file, continues) || continues > 1 || !readPod(file, length) ||
          length > MAX_WORD_CHARS || length > FreshRssCache::MAX_BODY_BYTES - std::min(FreshRssCache::MAX_BODY_BYTES, totalBytes) ||
          !hasBytes(file, length))
        return false;
      if (loadBody) {
        StyledWord word;
        word.style = static_cast<EpdFontFamily::Style>(style);
        word.continuesPrevious = continues != 0;
        word.text.resize(length);
        if (length && static_cast<size_t>(file.read(word.text.data(), length)) != length) return false;
        utf8SanitizeInPlace(word.text);
        paragraph.words.push_back(std::move(word));
      } else if (!skipBytes(file, length)) {
        return false;
      }
      totalBytes += length;
    }
    if (loadBody) body.push_back(std::move(paragraph));
  }
  return true;
}

bool writeCategories(HalFile& file, const std::vector<std::string>& categories) {
  if (categories.size() > MAX_CATEGORIES || !writePod(file, static_cast<uint32_t>(categories.size()))) return false;
  for (const auto& category : categories) if (!writeString(file, category, MAX_ID)) return false;
  return true;
}
bool readCategories(HalFile& file, std::vector<std::string>& categories) {
  uint32_t count = 0;
  if (!readPod(file, count) || count > MAX_CATEGORIES) return false;
  categories.clear();
  categories.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    std::string category;
    if (!readString(file, category, MAX_ID)) return false;
    categories.push_back(std::move(category));
  }
  return true;
}

bool writeMetadata(HalFile& file, const FreshRssMetadata& metadata) {
  if (metadata.tags.size() > 128 || metadata.subscriptions.size() > 128 ||
      !writePod(file, static_cast<uint32_t>(metadata.tags.size()))) return false;
  for (const auto& tag : metadata.tags) {
    if (!writeString(file, tag.id, MAX_ID) || !writeString(file, tag.label, MAX_LABEL)) return false;
  }
  if (!writePod(file, static_cast<uint32_t>(metadata.subscriptions.size()))) return false;
  for (const auto& sub : metadata.subscriptions) {
    if (!writeString(file, sub.id, MAX_ID) || !writeString(file, sub.title, MAX_LABEL) ||
        !writeString(file, sub.htmlUrl, MAX_ID) || !writeCategories(file, sub.categoryIds)) return false;
  }
  return true;
}

bool readMetadata(HalFile& file, FreshRssMetadata* metadata) {
  uint32_t count = 0;
  if (!readPod(file, count) || count > 128) return false;
  if (metadata) metadata->tags.clear(), metadata->tags.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    FreshRssTag tag;
    if (!readString(file, tag.id, MAX_ID) || !readString(file, tag.label, MAX_LABEL)) return false;
    if (metadata) metadata->tags.push_back(std::move(tag));
  }
  if (!readPod(file, count) || count > 128) return false;
  if (metadata) metadata->subscriptions.clear(), metadata->subscriptions.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    FreshRssSubscription sub;
    if (!readString(file, sub.id, MAX_ID) || !readString(file, sub.title, MAX_LABEL) || !readString(file, sub.htmlUrl, MAX_ID) ||
        !readCategories(file, sub.categoryIds)) return false;
    if (metadata) metadata->subscriptions.push_back(std::move(sub));
  }
  return true;
}

bool writeArticle(HalFile& file, const FreshRssCachedArticle& article) {
  if (!writePod(file, article.key) || !writeString(file, article.id, MAX_ID) || !writeString(file, article.title, MAX_LABEL) ||
      !writeString(file, article.author, MAX_LABEL) || !writeString(file, article.origin, MAX_LABEL) ||
      !writeString(file, article.originUrl, MAX_ID) || !writeString(file, article.streamId, MAX_ID) ||
      !writeString(file, article.subscriptionId, MAX_ID) ||
      !writeString(file, article.link, MAX_ID) || !writeString(file, article.date, MAX_LABEL) ||
      !writeCategories(file, article.categoryIds) || !writePod(file, static_cast<uint8_t>(article.bodyTruncated ? 1 : 0)) ||
      !writeRichText(file, article.body)) return false;
  return true;
}
bool writeSizedArticle(HalFile& file, const FreshRssCachedArticle& article, uint32_t& recordOffset, uint32_t& recordSize) {
  const size_t sizeOffset = file.position();
  const uint32_t zero = 0;
  if (!writePod(file, zero)) return false;
  const size_t payloadOffset = file.position();
  if (!writeArticle(file, article)) return false;
  const size_t end = file.position();
  if (end < payloadOffset || end - payloadOffset > std::numeric_limits<uint32_t>::max()) return false;
  recordSize = static_cast<uint32_t>(end - payloadOffset);
  if (sizeOffset > std::numeric_limits<uint32_t>::max() || end > std::numeric_limits<uint32_t>::max()) return false;
  recordOffset = static_cast<uint32_t>(sizeOffset);
  return file.seek(sizeOffset) && writePod(file, recordSize) && file.seek(end);
}
bool readArticleFields(HalFile& file, FreshRssCachedArticle& article, uint8_t& truncated) {
  if (!readPod(file, article.key) || !readString(file, article.id, MAX_ID) || !readString(file, article.title, MAX_LABEL) ||
      !readString(file, article.author, MAX_LABEL) || !readString(file, article.origin, MAX_LABEL) ||
      !readString(file, article.originUrl, MAX_ID) || !readString(file, article.streamId, MAX_ID) ||
      !readString(file, article.subscriptionId, MAX_ID) || !readString(file, article.link, MAX_ID) ||
      !readString(file, article.date, MAX_LABEL) || !readCategories(file, article.categoryIds) || !readPod(file, truncated) ||
      truncated > 1)
    return false;
  // Repair snapshots written before URL-as-title normalization. This also
  // keeps article lists useful when the user has not refreshed yet.
  article.title = freshRssDisplayTitle(article.title, article.link);
  utf8SanitizeInPlace(article.title);
  utf8SanitizeInPlace(article.author);
  utf8SanitizeInPlace(article.origin);
  utf8SanitizeInPlace(article.date);
  return true;
}
bool beginSizedRecord(HalFile& file, const bool sized, size_t& recordEnd) {
  if (!sized) return true;
  uint32_t recordSize = 0;
  if (!readPod(file, recordSize) || !hasBytes(file, recordSize)) return false;
  recordEnd = file.position() + recordSize;
  return true;
}
bool finishSizedRecord(HalFile& file, const bool sized, const size_t recordEnd) {
  if (!sized) return true;
  if (file.position() > recordEnd) return false;
  return file.seekSet(recordEnd);
}
bool readArticle(HalFile& file, FreshRssCachedArticle& article, const bool loadBody, const bool sized) {
  size_t recordEnd = 0;
  if (!beginSizedRecord(file, sized, recordEnd)) return false;
  uint8_t truncated = 0;
  if (!readArticleFields(file, article, truncated)) return false;
  if (loadBody) {
    if (!readRichText(file, article.body, true)) return false;
  } else if (!sized && !readRichText(file, article.body, false)) {
    return false;
  }
  if (!finishSizedRecord(file, sized, recordEnd)) return false;
  article.bodyTruncated = truncated != 0;
  return true;
}
bool readArticleBodyForKey(HalFile& file, FreshRssCachedArticle& article, const uint32_t wantedKey, const bool sized) {
  size_t recordEnd = 0;
  if (!beginSizedRecord(file, sized, recordEnd)) return false;
  uint8_t truncated = 0;
  if (!readArticleFields(file, article, truncated)) return false;
  if ((!sized || article.key == wantedKey) && !readRichText(file, article.body, article.key == wantedKey)) return false;
  if (!finishSizedRecord(file, sized, recordEnd)) return false;
  article.bodyTruncated = truncated != 0;
  return true;
}

bool readHeader(HalFile& file, SnapshotHeader& header) {
  uint32_t magic = 0;
  if (!readPod(file, magic) || magic != MAGIC || !readPod(file, header.version) ||
      (header.version != 1 && header.version != 2 && header.version != 3 && header.version != VERSION) ||
      !readPod(file, header.articleCount) || !readPod(file, header.subCount) || !readPod(file, header.tagCount) ||
      !readPod(file, header.limit) || header.articleCount > MAX_SNAPSHOT_ARTICLES || header.subCount > 128 ||
      header.tagCount > 128 || header.limit > MAX_SNAPSHOT_ARTICLES)
    return false;
  if (header.version >= 3 && (!readPod(file, header.indexOffset) || !readPod(file, header.indexCount) ||
                              !readPod(file, header.lastRefreshUnix) || header.indexCount != header.articleCount ||
                              header.indexCount > MAX_SNAPSHOT_ARTICLES ||
                              header.indexOffset < (header.version >= 4 ? HEADER_SIZE : HEADER_INDEX_COUNT)))
    return false;
  if (header.version >= 4 && (!readPod(file, header.cursorModifiedMsec) || !readPod(file, header.generation) ||
                              !readPod(file, header.accountIdentity)))
    return false;
  return true;
}

// Compatibility wrapper for the legacy record-scan helpers below. New indexed
// readers use SnapshotHeader directly, while v1/v2 tests and migration retain
// their original bounded record format.
bool readHeader(HalFile& file, uint32_t& articleCount, uint32_t& subCount, uint32_t& tagCount, uint32_t& limit,
                uint8_t& version) {
  SnapshotHeader header;
  if (!readHeader(file, header)) return false;
  articleCount = header.articleCount;
  subCount = header.subCount;
  tagCount = header.tagCount;
  limit = header.limit;
  version = header.version;
  return true;
}

bool readCommit(HalFile& file);

bool writeIndexEntry(HalFile& file, const FreshRssIndexEntry& entry, const uint8_t version) {
  return writePod(file, entry.key) && (version < 4 || writePod(file, entry.modifiedMsec)) &&
         writePod(file, entry.recordOffset) && writePod(file, entry.recordSize) &&
         writePod(file, entry.subscriptionIndex) && writePod(file, entry.flags) &&
         writePod(file, entry.categoryMask[0]) && writePod(file, entry.categoryMask[1]) &&
         writePod(file, entry.categoryMask[2]) && writePod(file, entry.categoryMask[3]);
}

bool readIndexEntry(HalFile& file, FreshRssIndexEntry& entry, const uint8_t version) {
  return readPod(file, entry.key) && (version < 4 || readPod(file, entry.modifiedMsec)) &&
         readPod(file, entry.recordOffset) && readPod(file, entry.recordSize) &&
         readPod(file, entry.subscriptionIndex) && readPod(file, entry.flags) && readPod(file, entry.categoryMask[0]) &&
         readPod(file, entry.categoryMask[1]) && readPod(file, entry.categoryMask[2]) &&
         readPod(file, entry.categoryMask[3]);
}

bool readIndexTable(HalFile& file, const SnapshotHeader& header, std::vector<std::string>* categories,
                    std::vector<FreshRssIndexEntry>& entries) {
  if (header.version < 3 || header.indexOffset >= file.fileSize() || !file.seekSet(header.indexOffset)) {
    LOG_ERR("FRSS", "readIndexTable: bad header (version=%u indexOffset=%u fileSize=%llu)", header.version,
            header.indexOffset, static_cast<unsigned long long>(file.fileSize()));
    return false;
  }
  uint32_t categoryCount = 0;
  if (!readPod(file, categoryCount) || categoryCount > MAX_INDEX_CATEGORIES) {
    LOG_ERR("FRSS", "readIndexTable: bad category count %u", categoryCount);
    return false;
  }
  std::vector<std::string> loadedCategories;
  loadedCategories.reserve(categoryCount);
  for (uint32_t i = 0; i < categoryCount; ++i) {
    std::string category;
    if (!readString(file, category, MAX_ID)) {
      LOG_ERR("FRSS", "readIndexTable: failed reading category %u/%u", i, categoryCount);
      return false;
    }
    loadedCategories.push_back(std::move(category));
  }
  entries.clear();
  entries.reserve(header.indexCount);
  // A linear std::find here made duplicate-key detection O(n^2) over the
  // index (~500k comparisons for a 1000-article snapshot) on every single
  // cache read (loadKeys, loadItemsByKeys, loadItemBody, loadNavigation,
  // stats, ...). A hash set makes each check O(1) amortized.
  std::unordered_set<uint32_t> seenKeys;
  seenKeys.reserve(header.indexCount);
  for (uint32_t i = 0; i < header.indexCount; ++i) {
    FreshRssIndexEntry entry;
    const size_t minimumRecordOffset = header.version >= 4 ? HEADER_SIZE : HEADER_TIMESTAMP + sizeof(uint32_t);
    if (!readIndexEntry(file, entry, header.version) || entry.recordOffset < minimumRecordOffset ||
        entry.recordOffset >= header.indexOffset || entry.recordSize == 0 ||
        entry.recordSize > header.indexOffset - entry.recordOffset - sizeof(uint32_t) ||
        seenKeys.count(entry.key) != 0 ||
        (entry.subscriptionIndex >= 128 && entry.subscriptionIndex != 0xffff)) {
      LOG_ERR("FRSS", "readIndexTable: rejected malformed/duplicate index entry %u/%u", i, header.indexCount);
      return false;
    }
    seenKeys.insert(entry.key);
    entries.push_back(entry);
  }
  if (!readCommit(file)) {
    LOG_ERR("FRSS", "readIndexTable: missing/invalid commit marker after index table");
    return false;
  }
  if (categories) *categories = std::move(loadedCategories);
  return true;
}

bool writeIndexTable(HalFile& file, const std::vector<std::string>& categories,
                     const std::vector<FreshRssIndexEntry>& entries, const uint8_t version) {
  if (categories.size() > MAX_INDEX_CATEGORIES || entries.size() > FreshRssCache::MAX_ARTICLES ||
      !writePod(file, static_cast<uint32_t>(categories.size())))
    return false;
  for (const auto& category : categories) {
    if (!writeString(file, category, MAX_ID)) return false;
  }
  for (const auto& entry : entries) {
    if (!writeIndexEntry(file, entry, version)) return false;
  }
  return true;
}
bool readCommit(HalFile& file) {
  uint32_t marker = 0;
  return readPod(file, marker) && marker == COMMIT && file.available() == 0;
}
bool matches(const FreshRssCachedArticle& article, FreshRssFilterKind kind, const std::string& id,
             const FreshRssMetadata* metadata = nullptr) {
  if (kind == FreshRssFilterKind::All) return true;
  if (kind == FreshRssFilterKind::Uncategorized) {
    if (!article.categoryIds.empty()) return false;
    if (metadata) {
      const auto sub = std::find_if(metadata->subscriptions.begin(), metadata->subscriptions.end(),
                                    [&article](const FreshRssSubscription& value) {
                                      return value.id == article.subscriptionId;
                                    });
      if (sub != metadata->subscriptions.end() && !sub->categoryIds.empty()) return false;
    }
    return true;
  }
  if (kind == FreshRssFilterKind::Subscription) return article.subscriptionId == id;
  if (std::find(article.categoryIds.begin(), article.categoryIds.end(), id) != article.categoryIds.end()) return true;
  // Older FreshRSS responses sometimes omit item categories while still
  // providing the subscription-to-category relationship. Treat that
  // relationship as authoritative so category navigation does not appear
  // empty merely because one response shape was used.
  if (metadata) {
    const auto sub = std::find_if(metadata->subscriptions.begin(), metadata->subscriptions.end(),
                                  [&article](const FreshRssSubscription& value) {
                                    return value.id == article.subscriptionId;
                                  });
    if (sub != metadata->subscriptions.end()) {
      return std::find(sub->categoryIds.begin(), sub->categoryIds.end(), id) != sub->categoryIds.end();
    }
  }
  return false;
}

bool isCategoryTag(const FreshRssTag& tag) {
  // FreshRSS exposes local categories and Google Reader state tags through the
  // same tag endpoint. Only user labels are category navigation entries;
  // unread/starred state remains device-local in this client.
  return tag.id.rfind("user/-/label/", 0) == 0;
}

std::string categoryLabel(const FreshRssTag& tag) {
  if (!tag.label.empty()) return tag.label;
  constexpr char PREFIX[] = "user/-/label/";
  if (tag.id.rfind(PREFIX, 0) != 0 || tag.id.size() <= sizeof(PREFIX) - 1) return {};
  return tag.id.substr(sizeof(PREFIX) - 1);
}

std::string articleSubtitle(const FreshRssCachedArticle& article) {
  std::string result = article.origin;
  if (!article.author.empty()) {
    if (!result.empty()) result += " · ";
    result += article.author;
  }
  return result;
}

CachedRssItem indexItem(const FreshRssCachedArticle& article, const uint32_t cacheOffset = 0) {
  CachedRssItem item;
  item.key = article.key;
  item.cacheOffset = cacheOffset;
  item.title = article.title;
  item.link = article.link;
  item.date = article.date;
  item.subtitle = articleSubtitle(article);
  item.bodyTruncated = article.bodyTruncated;
  return item;
}

bool openSnapshot(HalFile& file) {
  if (!Storage.openFileForRead("FRSS", SNAPSHOT, file)) return false;
  SnapshotHeader header;
  return readHeader(file, header);
}

void cleanLegacyFreshRssFiles() {
  auto root = Storage.open(ROOT);
  if (!root || !root.isDirectory()) return;
  char name[128] = {};
  for (auto entry = root.openNextFile(); entry; entry = root.openNextFile()) {
    entry.getName(name, sizeof(name));
    const std::string child(name);
    entry.close();
    if (child == "manifest.bin" || child.rfind("item_", 0) == 0 || child == "snapshot.bin.tmp" ||
        child == "snapshot.bin.bak")
      Storage.remove((std::string(ROOT) + "/" + child).c_str());
  }
  root.close();
}
}  // namespace

namespace FreshRssCache {
class WriteSession::HalFileHolder {
 public:
  HalFile file;
};

WriteSession::~WriteSession() { abort(); }

bool WriteSession::begin(const FreshRssMetadata& value, const size_t articleLimit) {
  return begin(value, articleLimit, FreshRssSyncCursor{});
}

bool WriteSession::begin(const FreshRssMetadata& value, const size_t articleLimit,
                         const FreshRssSyncCursor& cursor) {
  if (open || value.tags.size() > 128 || value.subscriptions.size() > 128 || articleLimit > MAX_ARTICLES) return false;
  tempPath = TEMP;
  Storage.mkdir("/.crosspoint");
  Storage.mkdir(ROOT);
  cleanLegacyFreshRssFiles();
  Storage.remove(TEMP);
  holder = new (std::nothrow) HalFileHolder();
  if (!holder || !Storage.openFileForWrite("FRSS", TEMP, holder->file)) {
    LOG_ERR("FRSS", "WriteSession::begin failed to open %s for write (holder=%d)", TEMP, holder != nullptr);
    delete holder;
    holder = nullptr;
    failedState = true;
    return false;
  }
  metadata = value;
  articleCount = 0;
  this->articleLimit = static_cast<uint32_t>(articleLimit);
  lastRefreshUnix = static_cast<uint32_t>(std::time(nullptr));
  syncCursor = cursor;
  indexEntries.clear();
  indexCategories.clear();
  for (const auto& tag : metadata.tags) {
    if (!tag.id.empty() && std::find(indexCategories.begin(), indexCategories.end(), tag.id) == indexCategories.end())
      indexCategories.push_back(tag.id);
  }
  for (const auto& sub : metadata.subscriptions) {
    for (const auto& category : sub.categoryIds) {
      if (!category.empty() && std::find(indexCategories.begin(), indexCategories.end(), category) == indexCategories.end())
        indexCategories.push_back(category);
    }
  }
  if (indexCategories.size() > MAX_INDEX_CATEGORIES) {
    abort();
    failedState = true;
    return false;
  }
  open = true;
  failedState = false;
  const uint8_t version = VERSION;
  const uint32_t zero = 0;
  const uint64_t zeroMsec = 0;
  const uint32_t limit = articleLimit;
  if (!writePod(holder->file, MAGIC) || !writePod(holder->file, version) || !writePod(holder->file, zero) ||
      !writePod(holder->file, static_cast<uint32_t>(metadata.subscriptions.size())) ||
      !writePod(holder->file, static_cast<uint32_t>(metadata.tags.size())) || !writePod(holder->file, limit) ||
      !writePod(holder->file, zero) || !writePod(holder->file, zero) || !writePod(holder->file, zero) ||
      !writePod(holder->file, zeroMsec) || !writePod(holder->file, zero) || !writePod(holder->file, zero) ||
      !writeMetadata(holder->file, metadata)) {
    LOG_ERR("FRSS", "WriteSession::begin failed to write snapshot header/metadata to %s", TEMP);
    abort();
    return false;
  }
  return true;
}

bool WriteSession::append(const FreshRssCachedArticle& article) {
  if (!open || failedState || articleCount >= articleLimit || articleCount >= MAX_ARTICLES) {
    failedState = true;
    return false;
  }
  if (std::find_if(indexEntries.begin(), indexEntries.end(), [&article](const FreshRssIndexEntry& entry) {
        return entry.key == article.key;
      }) != indexEntries.end())
    return true;
  uint32_t recordOffset = 0;
  uint32_t recordSize = 0;
  if (!writeSizedArticle(holder->file, article, recordOffset, recordSize)) {
    failedState = true;
    return false;
  }
  FreshRssIndexEntry entry;
  entry.key = article.key;
  entry.modifiedMsec = article.modifiedMsec;
  entry.recordOffset = recordOffset;
  entry.recordSize = recordSize;
  entry.flags = article.bodyTruncated ? INDEX_FLAG_TRUNCATED : 0;
  const auto subscription = std::find_if(metadata.subscriptions.begin(), metadata.subscriptions.end(),
                                         [&article](const FreshRssSubscription& value) {
                                           return value.id == article.subscriptionId;
                                         });
  if (subscription != metadata.subscriptions.end())
    entry.subscriptionIndex = static_cast<uint16_t>(subscription - metadata.subscriptions.begin());
  for (const auto& category : article.categoryIds) {
    auto categoryIt = std::find(indexCategories.begin(), indexCategories.end(), category);
    if (categoryIt == indexCategories.end()) {
      if (indexCategories.size() >= MAX_INDEX_CATEGORIES) {
        failedState = true;
        return false;
      }
      indexCategories.push_back(category);
      categoryIt = std::prev(indexCategories.end());
    }
    const size_t categoryIndex = static_cast<size_t>(categoryIt - indexCategories.begin());
    if (categoryIndex < MAX_INDEX_CATEGORIES) entry.categoryMask[categoryIndex / 32] |= 1u << (categoryIndex % 32);
  }
  indexEntries.push_back(entry);
  ++articleCount;
  return true;
}

bool WriteSession::copyUnchangedFromCurrent(const std::vector<uint32_t>& changedKeys) {
  if (!open || failedState) {
    failedState = true;
    return false;
  }

  HalFile source;
  SnapshotHeader header;
  if (!Storage.openFileForRead("FRSS", SNAPSHOT, source) || !readHeader(source, header) || header.version < 3 ||
      !readMetadata(source, nullptr)) {
    failedState = true;
    return false;
  }
  std::vector<std::string> oldCategories;
  std::vector<FreshRssIndexEntry> oldEntries;
  if (!readIndexTable(source, header, &oldCategories, oldEntries)) {
    failedState = true;
    return false;
  }

  const auto isChanged = [&changedKeys](const uint32_t key) {
    return std::find(changedKeys.begin(), changedKeys.end(), key) != changedKeys.end();
  };
  const auto addIndexMetadata = [this](const FreshRssCachedArticle& article, const uint32_t recordOffset,
                                       const uint32_t recordSize, const uint64_t modifiedMsec,
                                       const uint8_t flags) {
    FreshRssIndexEntry entry;
    entry.key = article.key;
    entry.modifiedMsec = modifiedMsec;
    entry.recordOffset = recordOffset;
    entry.recordSize = recordSize;
    entry.flags = flags;
    const auto subscription = std::find_if(metadata.subscriptions.begin(), metadata.subscriptions.end(),
                                           [&article](const FreshRssSubscription& value) {
                                             return value.id == article.subscriptionId;
                                           });
    if (subscription != metadata.subscriptions.end())
      entry.subscriptionIndex = static_cast<uint16_t>(subscription - metadata.subscriptions.begin());
    for (const auto& category : article.categoryIds) {
      auto categoryIt = std::find(indexCategories.begin(), indexCategories.end(), category);
      if (categoryIt == indexCategories.end()) {
        if (indexCategories.size() >= MAX_INDEX_CATEGORIES) return false;
        indexCategories.push_back(category);
        categoryIt = std::prev(indexCategories.end());
      }
      const size_t categoryIndex = static_cast<size_t>(categoryIt - indexCategories.begin());
      entry.categoryMask[categoryIndex / 32] |= 1u << (categoryIndex % 32);
    }
    indexEntries.push_back(entry);
    ++articleCount;
    return true;
  };

  std::array<uint8_t, 512> buffer{};
  for (const auto& oldEntry : oldEntries) {
    if (articleCount >= articleLimit || articleCount >= MAX_ARTICLES) break;
    if (isChanged(oldEntry.key) ||
        std::find_if(indexEntries.begin(), indexEntries.end(), [&oldEntry](const FreshRssIndexEntry& entry) {
          return entry.key == oldEntry.key;
        }) != indexEntries.end())
      continue;
    if (!source.seekSet(oldEntry.recordOffset)) {
      failedState = true;
      return false;
    }
    FreshRssCachedArticle article;
    if (!readArticle(source, article, false, true)) {
      failedState = true;
      return false;
    }
    if (!source.seekSet(oldEntry.recordOffset)) {
      failedState = true;
      return false;
    }
    const uint64_t bytesToCopy = static_cast<uint64_t>(oldEntry.recordSize) + sizeof(uint32_t);
    if (bytesToCopy > std::numeric_limits<uint32_t>::max() || holder->file.position() > std::numeric_limits<uint32_t>::max()) {
      failedState = true;
      return false;
    }
    const uint32_t newOffset = static_cast<uint32_t>(holder->file.position());
    uint64_t remaining = bytesToCopy;
    while (remaining != 0) {
      const size_t take = static_cast<size_t>(std::min<uint64_t>(remaining, buffer.size()));
      if (source.read(buffer.data(), take) != static_cast<int>(take) || holder->file.write(buffer.data(), take) != take) {
        failedState = true;
        return false;
      }
      remaining -= take;
    }
    if (!addIndexMetadata(article, newOffset, oldEntry.recordSize, oldEntry.modifiedMsec, oldEntry.flags)) {
      failedState = true;
      return false;
    }
  }
  source.close();
  return true;
}

bool WriteSession::commit() {
  if (!open || failedState) {
    abort();
    return false;
  }
  if (holder->file.position() > std::numeric_limits<uint32_t>::max()) {
    LOG_ERR("FRSS", "WriteSession::commit: %s exceeds the 32-bit offset range", TEMP);
    abort();
    return false;
  }
  const uint32_t indexOffset = static_cast<uint32_t>(holder->file.position());
  if (!writeIndexTable(holder->file, indexCategories, indexEntries, VERSION) || !writePod(holder->file, COMMIT)) {
    LOG_ERR("FRSS", "WriteSession::commit: failed writing index table/commit marker to %s", TEMP);
    abort();
    return false;
  }
  holder->file.flush();
  if (!holder->file.seek(sizeof(uint32_t) + sizeof(uint8_t)) || !writePod(holder->file, articleCount)) {
    LOG_ERR("FRSS", "WriteSession::commit: failed to backpatch article count");
    abort();
    return false;
  }
  if (!holder->file.seek(HEADER_INDEX_OFFSET) || !writePod(holder->file, indexOffset) ||
      !writePod(holder->file, static_cast<uint32_t>(indexEntries.size()))) {
    LOG_ERR("FRSS", "WriteSession::commit: failed to backpatch index offset/count");
    abort();
    return false;
  }
  // The timestamp is a v3 header extension stored immediately after the
  // index count. It is advisory because boards without a valid RTC report 0.
  if (!holder->file.seek(HEADER_TIMESTAMP) || !writePod(holder->file, lastRefreshUnix)) {
    LOG_ERR("FRSS", "WriteSession::commit: failed to backpatch last-refresh timestamp");
    abort();
    return false;
  }
  if (!holder->file.seek(HEADER_CURSOR_MSEC) || !writePod(holder->file, syncCursor.modifiedMsec) ||
      !writePod(holder->file, syncCursor.generation) || !writePod(holder->file, syncCursor.accountIdentity)) {
    LOG_ERR("FRSS", "WriteSession::commit: failed to backpatch sync cursor");
    abort();
    return false;
  }
  holder->file.flush();
  if (!holder->file.close()) {
    LOG_ERR("FRSS", "WriteSession::commit: failed to close %s", TEMP);
    abort();
    return false;
  }
  delete holder;
  holder = nullptr;
  open = false;
  Storage.remove(BACKUP);
  if (Storage.exists(SNAPSHOT) && !Storage.rename(SNAPSHOT, BACKUP)) {
    LOG_ERR("FRSS", "WriteSession::commit: failed to back up %s before replacing it", SNAPSHOT);
    Storage.remove(TEMP);
    failedState = true;
    return false;
  }
  if (!Storage.rename(TEMP, SNAPSHOT)) {
    LOG_ERR("FRSS", "WriteSession::commit: failed to install %s as %s%s", TEMP, SNAPSHOT,
            Storage.exists(BACKUP) ? "; restoring previous snapshot from backup" : "; no backup to restore from");
    if (Storage.exists(BACKUP) && !Storage.rename(BACKUP, SNAPSHOT))
      LOG_ERR("FRSS", "WriteSession::commit: backup restore also failed — snapshot is now missing");
    Storage.remove(TEMP);
    failedState = true;
    return false;
  }
  Storage.remove(BACKUP);
  return true;
}

void WriteSession::abort() {
  if (holder) {
    if (open) holder->file.close();
    delete holder;
    holder = nullptr;
  }
  open = false;
  if (!tempPath.empty()) Storage.remove(tempPath.c_str());
  Storage.remove(TEMP);
}

bool ensureCurrentSnapshot() {
  HalFile input;
  // Opening the file failing here is the ordinary "no snapshot cached yet"
  // state on first RSS use — not logged as an error.
  if (!Storage.openFileForRead("FRSS", SNAPSHOT, input)) return false;
  uint32_t articleCount = 0, subs = 0, tags = 0, limit = 0;
  uint8_t version = 0;
  if (!readHeader(input, articleCount, subs, tags, limit, version)) {
    input.close();
    return false;
  }
  if (version >= 2) return true;

  // v1 -> current migration: everything past here is an actual failure worth
  // logging, since it means a previously-working snapshot is being discarded.
  FreshRssMetadata metadata;
  if (!readMetadata(input, &metadata) || metadata.subscriptions.size() != subs || metadata.tags.size() != tags) {
    LOG_ERR("FRSS", "ensureCurrentSnapshot: v1 metadata failed to read/validate during migration");
    input.close();
    return false;
  }
  WriteSession writer;
  if (!writer.begin(metadata, limit)) {
    LOG_ERR("FRSS", "ensureCurrentSnapshot: failed to open a write session for v1 migration");
    input.close();
    return false;
  }
  for (uint32_t i = 0; i < articleCount; ++i) {
    FreshRssCachedArticle article;
    if (!readArticle(input, article, true, false) || !writer.append(article)) {
      LOG_ERR("FRSS", "ensureCurrentSnapshot: v1 migration failed at article %u/%u", i, articleCount);
      writer.abort();
      input.close();
      return false;
    }
  }
  if (!readCommit(input)) {
    LOG_ERR("FRSS", "ensureCurrentSnapshot: v1 snapshot missing/invalid commit marker");
    writer.abort();
    input.close();
    return false;
  }
  input.close();
  if (!writer.commit()) {
    LOG_ERR("FRSS", "ensureCurrentSnapshot: failed to commit migrated snapshot");
    return false;
  }
  return true;
}

bool exists() {
  return stats().bytes != 0;
}

bool loadSnapshotInfo(SnapshotInfo& outInfo) {
  outInfo = {};
  if (!ensureCurrentSnapshot()) return false;
  HalFile file;
  SnapshotHeader header;
  if (!openSnapshot(file) || !file.seek(0) || !readHeader(file, header) ||
      !readMetadata(file, nullptr))
    return false;
  outInfo.version = header.version;
  outInfo.generation = header.generation;
  outInfo.lastRefreshUnix = header.lastRefreshUnix;
  outInfo.bytes = file.fileSize64();
  if (header.version >= 3) {
    std::vector<std::string> categories;
    std::vector<FreshRssIndexEntry> entries;
    if (!readIndexTable(file, header, &categories, entries)) return false;
    outInfo.itemCount = entries.size();
    outInfo.indexBytes = outInfo.bytes > header.indexOffset ? outInfo.bytes - header.indexOffset : 0;
  } else {
    for (uint32_t i = 0; i < header.articleCount; ++i) {
      FreshRssCachedArticle article;
      if (!readArticle(file, article, false, false)) return false;
    }
    if (!readCommit(file)) return false;
    outInfo.itemCount = header.articleCount;
  }
  if (header.version >= 4 && header.cursorModifiedMsec != 0 && header.accountIdentity != 0 && header.limit != 0) {
    outInfo.cursor.modifiedMsec = header.cursorModifiedMsec;
    outInfo.cursor.generation = header.generation;
    outInfo.cursor.accountIdentity = header.accountIdentity;
    outInfo.cursor.articleLimit = static_cast<uint16_t>(std::min<uint32_t>(header.limit, 0xffff));
    outInfo.cursor.valid = true;
  }
  return true;
}

bool loadSyncCursor(FreshRssSyncCursor& outCursor) {
  SnapshotInfo info;
  outCursor = {};
  if (!loadSnapshotInfo(info)) return false;
  outCursor = info.cursor;
  return outCursor.valid;
}

bool loadKeys(const RssListQuery& query, std::vector<uint32_t>& outKeys) {
  return loadKeys(query.source, query.id, outKeys);
}

bool loadNavigationIndex(std::vector<FreshRssIndexEntry>& outEntries) {
  outEntries.clear();
  if (!ensureCurrentSnapshot()) return false;
  HalFile file;
  SnapshotHeader header;
  if (!Storage.openFileForRead("FRSS", SNAPSHOT, file) || !readHeader(file, header)) return false;
  if (header.version >= 3) {
    std::vector<std::string> categories;
    return readIndexTable(file, header, &categories, outEntries);
  }

  FreshRssMetadata metadata;
  if (!readMetadata(file, &metadata)) return false;
  outEntries.reserve(header.articleCount);
  for (uint32_t i = 0; i < header.articleCount; ++i) {
    FreshRssCachedArticle article;
    if (!readArticle(file, article, false, header.version >= 2)) return false;
    FreshRssIndexEntry entry;
    entry.key = article.key;
    entry.flags = article.bodyTruncated ? INDEX_FLAG_TRUNCATED : 0;
    outEntries.push_back(entry);
  }
  return readCommit(file);
}

bool matchesIndex(const FreshRssIndexEntry& entry, FreshRssFilterKind kind, const std::string& id,
                  const std::vector<std::string>& categories, const FreshRssMetadata& metadata) {
  if (kind == FreshRssFilterKind::All) return true;
  if (kind == FreshRssFilterKind::Subscription) {
    return entry.subscriptionIndex < metadata.subscriptions.size() && metadata.subscriptions[entry.subscriptionIndex].id == id;
  }
  if (kind == FreshRssFilterKind::Uncategorized) {
    if (entry.categoryMask[0] || entry.categoryMask[1] || entry.categoryMask[2] || entry.categoryMask[3]) return false;
    return entry.subscriptionIndex >= metadata.subscriptions.size() || metadata.subscriptions[entry.subscriptionIndex].categoryIds.empty();
  }
  const auto category = std::find(categories.begin(), categories.end(), id);
  if (category != categories.end()) {
    const size_t index = static_cast<size_t>(category - categories.begin());
    if (index < MAX_INDEX_CATEGORIES && (entry.categoryMask[index / 32] & (1u << (index % 32)))) return true;
  }
  if (entry.subscriptionIndex < metadata.subscriptions.size()) {
    const auto& subscription = metadata.subscriptions[entry.subscriptionIndex];
    return std::find(subscription.categoryIds.begin(), subscription.categoryIds.end(), id) != subscription.categoryIds.end();
  }
  return false;
}

bool loadKeys(const FreshRssFilterKind kind, const std::string& id, std::vector<uint32_t>& outKeys) {
  outKeys.clear();
  if (!ensureCurrentSnapshot()) return false;
  HalFile file;
  if (!openSnapshot(file)) return false;
  SnapshotHeader header;
  FreshRssMetadata metadata;
  if (!file.seek(0) || !readHeader(file, header) || !readMetadata(file, &metadata) ||
      metadata.subscriptions.size() != header.subCount || metadata.tags.size() != header.tagCount)
    return false;
  if (header.version >= 3) {
    std::vector<std::string> categories;
    std::vector<FreshRssIndexEntry> entries;
    if (!readIndexTable(file, header, &categories, entries)) return false;
    outKeys.reserve(entries.size());
    for (const auto& entry : entries) {
      if (matchesIndex(entry, kind, id, categories, metadata)) outKeys.push_back(entry.key);
    }
    return true;
  }
  outKeys.reserve(header.articleCount);
  for (uint32_t i = 0; i < header.articleCount; ++i) {
    FreshRssCachedArticle article;
    if (!readArticle(file, article, false, header.version >= 2)) return false;
    if (matches(article, kind, id, &metadata)) outKeys.push_back(article.key);
  }
  if (!readCommit(file)) {
    outKeys.clear();
    return false;
  }
  return true;
}

bool loadIndex(const FreshRssFilterKind kind, const std::string& id, std::vector<CachedRssItem>& outItems) {
  std::vector<uint32_t> keys;
  if (!loadKeys(kind, id, keys)) return false;
  return loadItemsByKeys(keys, 0, keys.size(), outItems);
}

bool loadItemsByKeys(const std::vector<uint32_t>& keys, const size_t offset, const size_t count,
                     std::vector<CachedRssItem>& outItems) {
  outItems.clear();
  if (offset > keys.size()) return false;
  if (!ensureCurrentSnapshot()) return false;
  const size_t remaining = keys.size() - offset;
  const size_t end = offset + std::min(remaining, count);
  const auto firstKey = keys.begin() + static_cast<std::ptrdiff_t>(offset);
  const auto lastKey = keys.begin() + static_cast<std::ptrdiff_t>(end);
  HalFile file;
  if (!openSnapshot(file)) return false;
  SnapshotHeader header;
  FreshRssMetadata metadata;
  if (!file.seek(0) || !readHeader(file, header) || !readMetadata(file, &metadata) ||
      metadata.subscriptions.size() != header.subCount || metadata.tags.size() != header.tagCount)
    return false;
  outItems.reserve(end - offset);
  if (header.version >= 3) {
    std::vector<std::string> categories;
    std::vector<FreshRssIndexEntry> entries;
    if (!readIndexTable(file, header, &categories, entries)) return false;
    for (auto keyIt = firstKey; keyIt != lastKey; ++keyIt) {
      const auto entry = std::find_if(entries.begin(), entries.end(), [keyIt](const FreshRssIndexEntry& value) {
        return value.key == *keyIt;
      });
      if (entry == entries.end() || !file.seekSet(entry->recordOffset)) {
        outItems.clear();
        return false;
      }
      FreshRssCachedArticle article;
      if (!readArticle(file, article, false, true)) {
        outItems.clear();
        return false;
      }
      outItems.push_back(indexItem(article, entry->recordOffset));
    }
    return outItems.size() == end - offset;
  }
  for (uint32_t i = 0; i < header.articleCount; ++i) {
    FreshRssCachedArticle article;
    if (!readArticle(file, article, false, header.version >= 2)) return false;
    if (std::find(firstKey, lastKey, article.key) != lastKey) outItems.push_back(indexItem(article));
  }
  if (!readCommit(file) || outItems.size() != end - offset) {
    outItems.clear();
    return false;
  }
  return true;
}

bool loadItemBodyByOffset(const uint32_t recordOffset, const uint32_t key, RichText& outBody, bool& complete) {
  outBody.clear();
  complete = false;
  if (!ensureCurrentSnapshot()) return false;
  HalFile file;
  SnapshotHeader header;
  FreshRssMetadata metadata;
  if (!Storage.openFileForRead("FRSS", SNAPSHOT, file) || !file.seek(0) || !readHeader(file, header) ||
      !readMetadata(file, &metadata) || header.version < 3)
    return false;
  std::vector<std::string> categories;
  std::vector<FreshRssIndexEntry> entries;
  if (!readIndexTable(file, header, &categories, entries)) return false;
  // Match on both offset and key: a re-commit (delta sync, eviction) can
  // reuse the same byte offset for a different article, so the offset alone
  // does not identify the record the caller expects.
  const auto entry =
      std::find_if(entries.begin(), entries.end(), [recordOffset, key](const FreshRssIndexEntry& value) {
        return value.recordOffset == recordOffset && value.key == key;
      });
  if (entry == entries.end() || !file.seekSet(recordOffset)) return false;
  FreshRssCachedArticle article;
  if (!readArticle(file, article, true, true) || article.key != key) return false;
  outBody = std::move(article.body);
  complete = !article.bodyTruncated;
  return true;
}

bool loadItemBody(const uint32_t key, RichText& outBody, bool& complete) {
  outBody.clear();
  complete = false;
  if (!ensureCurrentSnapshot()) return false;
  HalFile file;
  if (!openSnapshot(file)) return false;
  SnapshotHeader header;
  FreshRssMetadata metadata;
  if (!file.seek(0) || !readHeader(file, header) || !readMetadata(file, &metadata) ||
      metadata.subscriptions.size() != header.subCount || metadata.tags.size() != header.tagCount)
    return false;
  if (header.version >= 3) {
    std::vector<std::string> categories;
    std::vector<FreshRssIndexEntry> entries;
    if (!readIndexTable(file, header, &categories, entries)) return false;
    const auto entry = std::find_if(entries.begin(), entries.end(), [key](const FreshRssIndexEntry& value) {
      return value.key == key;
    });
    if (entry == entries.end() || !file.seekSet(entry->recordOffset)) return false;
    FreshRssCachedArticle article;
    if (!readArticle(file, article, true, true) || article.key != key) return false;
    outBody = std::move(article.body);
    complete = !article.bodyTruncated;
    return true;
  }
  bool found = false;
  for (uint32_t i = 0; i < header.articleCount; ++i) {
    FreshRssCachedArticle article;
    // Skip every non-selected body directly from the record stream. Only the
    // requested article's bounded RichText is allocated, so opening an item
    // does not repeatedly allocate and discard other article bodies.
    if (!readArticleBodyForKey(file, article, key, header.version >= 2)) return false;
    if (article.key == key) {
      outBody = std::move(article.body);
      complete = !article.bodyTruncated;
      found = true;
    }
  }
  if (!readCommit(file) || !found) return false;
  return true;
}

bool loadNavigation(std::vector<FreshRssNavigationEntry>& outEntries,
                    const std::function<bool(uint32_t)>& isRead) {
  outEntries.clear();
  if (!ensureCurrentSnapshot()) return false;
  HalFile file;
  if (!openSnapshot(file)) return false;
  uint32_t articleCount = 0, subs = 0, tags = 0, limit = 0;
  uint8_t version = 0;
  if (!file.seek(0) || !readHeader(file, articleCount, subs, tags, limit, version)) return false;
  FreshRssMetadata metadata;
  if (!readMetadata(file, &metadata) || metadata.subscriptions.size() != subs || metadata.tags.size() != tags) return false;
  outEntries.push_back({FreshRssFilterKind::All, "", "All Articles"});
  outEntries.push_back({FreshRssFilterKind::All, "", "Unread Articles", true});
  const auto addCategory = [&outEntries](const std::string& id, const std::string& label) {
    if (id.empty() || label.empty()) return;
    const auto existing = std::find_if(outEntries.begin(), outEntries.end(), [&id](const FreshRssNavigationEntry& entry) {
      return entry.kind == FreshRssFilterKind::Category && entry.id == id;
    });
    if (existing == outEntries.end()) outEntries.push_back({FreshRssFilterKind::Category, id, label});
  };
  for (const auto& tag : metadata.tags) {
    if (!isCategoryTag(tag)) continue;
    const std::string label = categoryLabel(tag);
    addCategory(tag.id, label);
  }
  // Some FreshRSS versions expose category IDs on subscriptions but omit
  // those labels from tag/list. Synthesize the display name from the ID so
  // categories remain navigable in either response shape.
  for (const auto& sub : metadata.subscriptions) {
    for (const auto& id : sub.categoryIds) {
      FreshRssTag category{id, {}};
      if (isCategoryTag(category)) addCategory(id, categoryLabel(category));
    }
  }

  if (version >= 3) {
    std::vector<std::string> categories;
    std::vector<FreshRssIndexEntry> entries;
    SnapshotHeader header;
    header.version = version;
    header.articleCount = articleCount;
    header.subCount = subs;
    header.tagCount = tags;
    // Re-read the complete v3 header to obtain the index offset and timestamp.
    if (!file.seek(0) || !readHeader(file, header) || !file.seekSet(header.indexOffset) ||
        !readIndexTable(file, header, &categories, entries))
      return false;
    for (const auto& category : categories) {
      FreshRssTag tag{category, {}};
      if (isCategoryTag(tag)) addCategory(category, categoryLabel(tag));
    }
    for (const auto& sub : metadata.subscriptions) {
      const std::string label = sub.title.empty() ? sub.id : sub.title;
      if (!label.empty()) outEntries.push_back({FreshRssFilterKind::Subscription, sub.id, label});
    }
    outEntries.push_back({FreshRssFilterKind::Uncategorized, "", "Uncategorized"});
    for (const auto& article : entries) {
      const bool read = isRead && isRead(article.key);
      for (auto& entry : outEntries) {
        const bool matched = entry.kind == FreshRssFilterKind::All
                                 ? (!entry.unreadOnly || !read)
                                 : matchesIndex(article, entry.kind, entry.id, categories, metadata);
        if (!matched) continue;
        ++entry.articleCount;
        if (!read) ++entry.unreadCount;
      }
    }
    outEntries.erase(std::remove_if(outEntries.begin(), outEntries.end(), [](const FreshRssNavigationEntry& entry) {
                      return entry.kind != FreshRssFilterKind::All && entry.articleCount == 0;
                    }),
                    outEntries.end());
    return true;
  }

  for (const auto& sub : metadata.subscriptions) {
    const std::string label = sub.title.empty() ? sub.id : sub.title;
    if (!label.empty()) outEntries.push_back({FreshRssFilterKind::Subscription, sub.id, label});
  }
  outEntries.push_back({FreshRssFilterKind::Uncategorized, "", "Uncategorized"});

  for (uint32_t i = 0; i < articleCount; ++i) {
    FreshRssCachedArticle article;
    if (!readArticle(file, article, false, version >= 2)) return false;
    const bool read = isRead && isRead(article.key);
    for (auto& entry : outEntries) {
      bool matched = false;
      if (entry.kind == FreshRssFilterKind::All) {
        matched = !entry.unreadOnly || !read;
      } else {
        matched = matches(article, entry.kind, entry.id, &metadata);
      }
      if (!matched) continue;
      ++entry.articleCount;
      if (!read) ++entry.unreadCount;
    }
  }
  if (!readCommit(file)) return false;

  // Keep the two primary entries as stable navigation targets, but remove
  // empty categories, subscriptions, and the empty uncategorized bucket.
  outEntries.erase(std::remove_if(outEntries.begin(), outEntries.end(), [](const FreshRssNavigationEntry& entry) {
                    return entry.kind != FreshRssFilterKind::All && entry.articleCount == 0;
                  }),
                  outEntries.end());
  return true;
}

bool loadCategoryNavigation(std::vector<FreshRssNavigationEntry>& outEntries,
                            const std::function<bool(uint32_t)>& isRead) {
  std::vector<FreshRssNavigationEntry> all;
  if (!loadNavigation(all, isRead)) {
    outEntries.clear();
    return false;
  }
  outEntries.clear();
  for (auto& entry : all) {
    if (entry.kind == FreshRssFilterKind::Category || entry.kind == FreshRssFilterKind::Uncategorized) {
      outEntries.push_back(std::move(entry));
    }
  }
  return true;
}

bool loadSubscriptionNavigationForCategory(const std::string& categoryId,
                                           std::vector<FreshRssNavigationEntry>& outEntries,
                                           const std::function<bool(uint32_t)>& isRead) {
  outEntries.clear();
  if (!ensureCurrentSnapshot()) return false;

  HalFile file;
  if (!openSnapshot(file)) return false;
  uint32_t articleCount = 0, subs = 0, tags = 0, limit = 0;
  uint8_t version = 0;
  FreshRssMetadata metadata;
  if (!file.seek(0) || !readHeader(file, articleCount, subs, tags, limit, version) ||
      !readMetadata(file, &metadata) || metadata.subscriptions.size() != subs || metadata.tags.size() != tags)
    return false;

  for (const auto& subscription : metadata.subscriptions) {
    const bool belongsToCategory = categoryId.empty() ? subscription.categoryIds.empty()
                                                       : std::find(subscription.categoryIds.begin(),
                                                                   subscription.categoryIds.end(), categoryId) !=
                                                             subscription.categoryIds.end();
    if (!belongsToCategory)
      continue;
    const std::string label = subscription.title.empty() ? subscription.id : subscription.title;
    if (!label.empty()) {
      outEntries.push_back({FreshRssFilterKind::Subscription, subscription.id, label});
    }
  }

  if (version >= 3) {
    SnapshotHeader header;
    std::vector<std::string> categories;
    std::vector<FreshRssIndexEntry> entries;
    if (!file.seek(0) || !readHeader(file, header) || !readMetadata(file, nullptr) ||
        !readIndexTable(file, header, &categories, entries)) {
      outEntries.clear();
      return false;
    }
    for (const auto& article : entries) {
      const bool read = isRead && isRead(article.key);
      for (auto& entry : outEntries) {
        if (!matchesIndex(article, entry.kind, entry.id, categories, metadata)) continue;
        ++entry.articleCount;
        if (!read) ++entry.unreadCount;
      }
    }
    outEntries.erase(std::remove_if(outEntries.begin(), outEntries.end(), [](const FreshRssNavigationEntry& entry) {
                      return entry.articleCount == 0;
                    }),
                    outEntries.end());
    return true;
  }

  for (uint32_t i = 0; i < articleCount; ++i) {
    FreshRssCachedArticle article;
    if (!readArticle(file, article, false, version >= 2)) return false;
    const bool read = isRead && isRead(article.key);
    for (auto& entry : outEntries) {
      if (!matches(article, entry.kind, entry.id, &metadata)) continue;
      ++entry.articleCount;
      if (!read) ++entry.unreadCount;
    }
  }
  if (!readCommit(file)) {
    outEntries.clear();
    return false;
  }
  outEntries.erase(std::remove_if(outEntries.begin(), outEntries.end(), [](const FreshRssNavigationEntry& entry) {
                    return entry.articleCount == 0;
                  }),
                  outEntries.end());
  return true;
}

CacheStats stats() {
  CacheStats result;
  if (!ensureCurrentSnapshot()) return result;
  HalFile file;
  if (!openSnapshot(file)) return result;
  result.bytes = file.fileSize64();
  SnapshotHeader header;
  FreshRssMetadata metadata;
  if (!file.seek(0) || !readHeader(file, header) || !readMetadata(file, &metadata) ||
      metadata.subscriptions.size() != header.subCount || metadata.tags.size() != header.tagCount)
    return {};
  result.lastRefreshUnix = header.lastRefreshUnix;
  if (header.version >= 3) {
    std::vector<std::string> categories;
    std::vector<FreshRssIndexEntry> entries;
    if (!readIndexTable(file, header, &categories, entries)) return {};
    result.itemCount = entries.size();
    result.indexBytes = result.bytes > header.indexOffset ? result.bytes - header.indexOffset : 0;
    for (const auto& entry : entries) {
      if (entry.flags & INDEX_FLAG_TRUNCATED) ++result.truncatedBodies;
      else ++result.completeBodies;
    }
    return result;
  }
  for (uint32_t i = 0; i < header.articleCount; ++i) {
    FreshRssCachedArticle article;
    if (!readArticle(file, article, false, header.version >= 2)) return {};
    ++result.itemCount;
    if (article.bodyTruncated) ++result.truncatedBodies;
    else ++result.completeBodies;
  }
  if (!readCommit(file)) return {};
  return result;
}

bool clear() {
  bool ok = true;
  if (Storage.exists(SNAPSHOT)) ok = Storage.remove(SNAPSHOT) && ok;
  if (Storage.exists(TEMP)) ok = Storage.remove(TEMP) && ok;
  if (Storage.exists(BACKUP)) ok = Storage.remove(BACKUP) && ok;
  cleanLegacyFreshRssFiles();
  return ok;
}
}  // namespace FreshRssCache
