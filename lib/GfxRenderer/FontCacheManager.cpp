#include "FontCacheManager.h"

#include <FontDecompressor.h>
#include <Logging.h>
#include <SdCardFont.h>

#include <cstring>

FontCacheManager::FontCacheManager(const std::map<int, EpdFontFamily>& fontMap,
                                   const std::map<int, SdCardFont*>& sdCardFonts)
    : fontMap_(fontMap), sdCardFonts_(sdCardFonts) {}

void FontCacheManager::setFontDecompressor(FontDecompressor* d) { fontDecompressor_ = d; }

void FontCacheManager::clearCache() {
  if (fontDecompressor_) fontDecompressor_->clearCache();
  for (auto& [id, font] : sdCardFonts_) {
    font->clearCache();
  }
}

bool FontCacheManager::prewarmCache(int fontId, const char* utf8Text, uint8_t styleMask,
                                    const PreparationPolicy policy) {
  // SD card font prewarm path: prewarm all requested styles in one call
  auto it = sdCardFonts_.find(fontId);
  if (it != sdCardFonts_.end()) {
    int missed =
        it->second->prewarm(utf8Text, styleMask, /*metadataOnly=*/false, policy != PreparationPolicy::DictionaryLean);
    if (missed > 0) {
      LOG_DBG("FCM", "prewarmCache(SD): %d glyph(s) not found (styleMask=0x%02X)", missed, styleMask);
    }
    return !it->second->lastPrewarmFailed();
  }

  // Standard compressed font prewarm path: loop over all requested styles
  if (!fontDecompressor_ || fontMap_.count(fontId) == 0) return false;

  // Reverse iteration is harmless now; the decompressor keeps one retained page slot per style.
  for (int8_t i = 3; i >= 0; i--) {
    if (!(styleMask & (1 << i))) continue;
    auto style = static_cast<EpdFontFamily::Style>(i);
    const EpdFontData* data = fontMap_.at(fontId).getData(style);
    if (!data || !data->groups) continue;
    int missed = fontDecompressor_->prewarmCache(data, utf8Text);
    if (missed > 0) {
      LOG_DBG("FCM", "prewarmCache: %d glyph(s) not cached for style %d", missed, i);
    }
  }
  return true;
}

void FontCacheManager::logStats(const char* label) {
  if (fontDecompressor_) fontDecompressor_->logStats(label);
  for (auto& [id, font] : sdCardFonts_) {
    font->logStats(label);
  }
}

void FontCacheManager::resetStats() {
  if (fontDecompressor_) fontDecompressor_->resetStats();
  for (auto& [id, font] : sdCardFonts_) {
    font->resetStats();
  }
}

bool FontCacheManager::isScanning() const { return scanMode_ == ScanMode::Scanning; }

void FontCacheManager::recordText(const char* text, int fontId, EpdFontFamily::Style style) {
  const uint8_t styleIndex = static_cast<uint8_t>(style) & 0x03;
  ScanBucket* bucket = nullptr;
  for (uint8_t i = 0; i < scanBucketCount_; ++i) {
    if (scanBuckets_[i].fontId == fontId && scanBuckets_[i].styleIndex == styleIndex) {
      bucket = &scanBuckets_[i];
      break;
    }
  }
  if (!bucket) {
    if (scanBucketCount_ >= scanBuckets_.size()) {
      scanOverflow_ = true;
      return;
    }
    bucket = &scanBuckets_[scanBucketCount_++];
    bucket->fontId = fontId;
    bucket->styleIndex = styleIndex;
    bucket->text.clear();
    if (bucket->text.capacity() < SCAN_BUCKET_RESERVE) bucket->text.reserve(SCAN_BUCKET_RESERVE);
  }

  if ((style & EpdFontFamily::SMALL_CAPS) != 0) {
    for (const char* p = text; *p != '\0'; ++p) {
      bucket->text.push_back((*p >= 'a' && *p <= 'z') ? static_cast<char>(*p - ('a' - 'A')) : *p);
    }
  } else {
    bucket->text += text;
  }
}

// --- PrewarmScope implementation ---

FontCacheManager::PrewarmScope::PrewarmScope(FontCacheManager& manager, const PreparationPolicy policy)
    : manager_(&manager), policy_(policy) {
  manager_->scanMode_ = ScanMode::Scanning;
  manager_->clearCache();
  manager_->resetStats();
  for (auto& bucket : manager_->scanBuckets_) bucket.text.clear();
  manager_->scanBucketCount_ = 0;
  manager_->scanOverflow_ = false;
}

bool FontCacheManager::PrewarmScope::endScanAndPrewarm() {
  manager_->scanMode_ = ScanMode::None;
  bool ok = !manager_->scanOverflow_;
  if (manager_->scanOverflow_) {
    LOG_ERR("FCM", "Prewarm scan exceeded %u font/style buckets", FontCacheManager::MAX_SCAN_BUCKETS);
  }

  for (uint8_t i = 0; i < manager_->scanBucketCount_; ++i) {
    auto& bucket = manager_->scanBuckets_[i];
    if (!bucket.text.empty()) {
      ok = manager_->prewarmCache(bucket.fontId, bucket.text.c_str(), 1u << bucket.styleIndex, policy_) && ok;
    }
    // Keep each bucket's grown capacity so the next page can reuse it without
    // another allocate-grow-shrink cycle.
    bucket.text.clear();
  }
  manager_->scanBucketCount_ = 0;
  return ok;
}

FontCacheManager::PrewarmScope::~PrewarmScope() {
  if (active_) {
    endScanAndPrewarm();  // no-op if already called (scanText_ is empty)
    manager_->clearCache();
  }
}

FontCacheManager::PrewarmScope::PrewarmScope(PrewarmScope&& other) noexcept
    : manager_(other.manager_), policy_(other.policy_), active_(other.active_) {
  other.active_ = false;
}

FontCacheManager::PrewarmScope FontCacheManager::createPrewarmScope(const PreparationPolicy policy) {
  return PrewarmScope(*this, policy);
}
