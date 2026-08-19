#pragma once

#include <cstdint>

struct ESPMock {
  uint32_t getFreeHeap() const { return 1024U * 1024U; }
  uint32_t getMaxAllocHeap() const { return 1024U * 1024U; }
};

inline ESPMock ESP;
