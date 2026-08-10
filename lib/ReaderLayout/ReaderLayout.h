#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

// Small, allocation-bounded layout primitives shared by EPUB and RSS. The
// callers own their words; this module stores only break indexes and the
// temporary dynamic-programming costs.
namespace ReaderLayout {

using GapAdvance = std::function<int(size_t leftWord, size_t rightWord)>;

inline int justificationExtra(const int spareSpace, const size_t gapCount) {
  if (gapCount == 0 || spareSpace <= 0) return 0;
  return spareSpace / static_cast<int>(gapCount);
}

inline int firstLineIndent(const bool requested, const bool containsGujarati, const int normalIndent) {
  return requested && !containsGujarati ? normalIndent : 0;
}

inline std::vector<size_t> minimumRaggednessBreaks(const std::vector<uint16_t>& wordWidths,
                                                   const std::vector<bool>& continues,
                                                   const std::vector<bool>& noSpaceBefore, const int pageWidth,
                                                   const int firstIndent, const GapAdvance& gapAdvance) {
  const size_t count = wordWidths.size();
  if (count == 0) return {};
  constexpr int MAX_COST = std::numeric_limits<int>::max() / 4;
  std::vector<int> costs(count, MAX_COST);
  std::vector<size_t> ends(count, 0);
  costs[count - 1] = 0;
  ends[count - 1] = count - 1;

  for (size_t reverse = count - 1; reverse-- > 0;) {
    int lineWidth = 0;
    const int available = std::max(1, pageWidth - (reverse == 0 ? firstIndent : 0));
    for (size_t end = reverse; end < count; ++end) {
      if (end > reverse) {
        if (noSpaceBefore.size() > end && noSpaceBefore[end]) {
          // A Unicode break opportunity has no inserted Latin space.
        } else {
          lineWidth += gapAdvance(end - 1, end);
        }
      }
      lineWidth += wordWidths[end];
      if (lineWidth > available) break;
      if (continues.size() > end + 1 && continues[end + 1]) continue;

      int cost = 0;
      if (end + 1 < count) {
        const long long remaining = available - lineWidth;
        const long long candidate = remaining * remaining + costs[end + 1];
        cost = candidate >= MAX_COST ? MAX_COST : static_cast<int>(candidate);
      }
      // Match the EPUB breaker: equal costs prefer the longer line.
      if (cost <= costs[reverse]) {
        costs[reverse] = cost;
        ends[reverse] = end;
      }
    }
    if (costs[reverse] == MAX_COST) {
      ends[reverse] = reverse;
      costs[reverse] = reverse + 1 < count ? costs[reverse + 1] : 0;
    }
  }

  std::vector<size_t> breaks;
  for (size_t start = 0; start < count;) {
    size_t next = ends[start] + 1;
    if (next <= start || next > count) next = start + 1;
    breaks.push_back(next);
    start = next;
  }
  return breaks;
}

}  // namespace ReaderLayout
