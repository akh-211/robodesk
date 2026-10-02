#pragma once
#include <stddef.h>
#include <stdint.h>
#include <esp_heap_caps.h>
#include <mbedtls/platform.h>

// Install once before networking starts. Never swap the global allocator while
// TLS sessions are alive; heap_caps_free accepts both PSRAM and internal blocks.
inline void* roboTlsCalloc(size_t count, size_t size) {
  if (size && count > SIZE_MAX / size) return nullptr;
  void* memory = heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!memory) memory = heap_caps_calloc(count, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  return memory;
}

inline void roboTlsFree(void* memory) { heap_caps_free(memory); }

inline int roboInstallTlsAllocator() {
  return mbedtls_platform_set_calloc_free(roboTlsCalloc, roboTlsFree);
}
