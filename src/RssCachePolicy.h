#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace RssCachePolicy {

struct Entry {
  uint32_t accessSequence = 0;
  bool protectedByStar = false;
};

// Return manifest positions to remove, oldest first. Protected entries never
// appear in the result, even if their count exceeds the normal unprotected
// limit. The caller owns storage deletion; this helper stays independent of
// SD APIs and therefore remains easy to test on a desktop.
inline std::vector<size_t> selectEvictions(const std::vector<Entry>& entries, const size_t maxUnprotected) {
  size_t unprotected = 0;
  for (const auto& entry : entries) {
    if (!entry.protectedByStar) ++unprotected;
  }

  std::vector<size_t> order(entries.size());
  for (size_t i = 0; i < entries.size(); ++i) order[i] = i;
  std::sort(order.begin(), order.end(), [&](const size_t a, const size_t b) {
    return entries[a].accessSequence < entries[b].accessSequence;
  });

  std::vector<size_t> evictions;
  for (const size_t index : order) {
    if (unprotected <= maxUnprotected) break;
    if (entries[index].protectedByStar) continue;
    evictions.push_back(index);
    --unprotected;
  }
  return evictions;
}

}  // namespace RssCachePolicy
