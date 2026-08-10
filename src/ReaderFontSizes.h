#pragma once

#include <SdCardFontRegistry.h>

#include <cstddef>
#include <cstdint>
#include <vector>

inline constexpr uint8_t BUILTIN_READER_POINT_SIZES[] = {12, 14, 16, 18};

std::vector<uint8_t> readerFontPointSizes(const SdCardFontRegistry* registry, const char* sdFamilyName);
uint8_t snapToNearestPointSize(const uint8_t* sizes, size_t count, uint8_t pt);

inline uint8_t snapToNearestPointSize(const std::vector<uint8_t>& sizes, const uint8_t pt) {
  return sizes.empty() ? pt : snapToNearestPointSize(sizes.data(), sizes.size(), pt);
}
