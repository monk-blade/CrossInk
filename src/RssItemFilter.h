#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace RssItemFilter {

// Return positions into the already-loaded metadata vector. This deliberately
// never copies titles, RichText bodies, or other article data. FreshRSS can
// retain up to 1000 article metadata records, so use a compact 16-bit index.
template <typename IsRead>
std::vector<uint16_t> visibleIndexes(const size_t itemCount, const bool unreadOnly, IsRead&& isRead) {
  std::vector<uint16_t> indexes;
  indexes.reserve(std::min<size_t>(itemCount, 65535));
  for (size_t i = 0; i < itemCount && i <= 65535; ++i) {
    if (unreadOnly && isRead(i)) continue;
    indexes.push_back(static_cast<uint16_t>(i));
  }
  return indexes;
}

}  // namespace RssItemFilter
