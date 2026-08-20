#pragma once

#include <EpdFontFamily.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

class FontDecompressor;
class SdCardFont;

class FontCacheManager {
 public:
  enum class PreparationPolicy : uint8_t { Normal, DictionaryLean };

  FontCacheManager(const std::map<int, EpdFontFamily>& fontMap, const std::map<int, SdCardFont*>& sdCardFonts);

  void setFontDecompressor(FontDecompressor* d);

  void clearCache();
  bool prewarmCache(int fontId, const char* utf8Text, uint8_t styleMask = 0x0F,
                    PreparationPolicy policy = PreparationPolicy::Normal);
  void logStats(const char* label = "render");
  void resetStats();

  // Scan-mode API: called by GfxRenderer::drawText() during scan pass
  bool isScanning() const;
  void recordText(const char* text, int fontId, EpdFontFamily::Style style);

  // The FontDecompressor pointer, needed by GfxRenderer::getGlyphBitmap()
  FontDecompressor* getDecompressor() const { return fontDecompressor_; }

  // RAII scope for two-pass prewarm pattern
  class PrewarmScope {
   public:
    explicit PrewarmScope(FontCacheManager& manager, PreparationPolicy policy);
    ~PrewarmScope();
    bool endScanAndPrewarm();
    PrewarmScope(PrewarmScope&& other) noexcept;
    PrewarmScope& operator=(PrewarmScope&&) = delete;
    PrewarmScope(const PrewarmScope&) = delete;
    PrewarmScope& operator=(const PrewarmScope&) = delete;

   private:
    FontCacheManager* manager_;
    PreparationPolicy policy_;
    bool active_ = true;
  };
  PrewarmScope createPrewarmScope(PreparationPolicy policy = PreparationPolicy::Normal);

 private:
  static constexpr uint8_t MAX_SCAN_BUCKETS = 8;
  static constexpr size_t SCAN_BUCKET_RESERVE = 512;

  struct ScanBucket {
    int fontId = 0;
    uint8_t styleIndex = 0;
    std::string text;
  };

  const std::map<int, EpdFontFamily>& fontMap_;
  const std::map<int, SdCardFont*>& sdCardFonts_;
  FontDecompressor* fontDecompressor_ = nullptr;

  enum class ScanMode : uint8_t { None, Scanning };
  ScanMode scanMode_ = ScanMode::None;
  // Render helpers can prewarm UI text while a page-level scope is still
  // alive (for example the EPUB status bar). Only the outermost scope owns
  // global cache cleanup; otherwise the nested UI scope would evict the page
  // glyphs before image/grayscale redraw passes consume them.
  uint8_t activePrewarmScopes_ = 0;
  // A page normally uses one reader font with up to four styles. UI cards can
  // instead use separate size-matched SD fallback fonts for title, author, and
  // stats. Keep those font/style inputs distinct so each file is prewarmed with
  // only the glyphs it will actually draw. The fixed bucket table adds no
  // render-time container allocation; each string retains its high-water
  // capacity just like the former single scan string did.
  std::array<ScanBucket, MAX_SCAN_BUCKETS> scanBuckets_{};
  uint8_t scanBucketCount_ = 0;
  bool scanOverflow_ = false;
};
