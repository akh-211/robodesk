#pragma once
#include <cstddef>
#include <cstdlib>

extern bool mockHeapAllocationOK;
extern unsigned mockHeapAllocationCalls;
extern size_t mockHeapAllocationBytes;
extern unsigned mockHeapAllocationCaps;

constexpr unsigned MALLOC_CAP_SPIRAM = 1u;
constexpr unsigned MALLOC_CAP_8BIT = 2u;
constexpr unsigned MALLOC_CAP_INTERNAL = 4u;

inline void* heap_caps_malloc(size_t size, unsigned caps) {
  ++mockHeapAllocationCalls;
  mockHeapAllocationBytes = size;
  mockHeapAllocationCaps = caps;
  return mockHeapAllocationOK ? std::malloc(size) : nullptr;
}
