#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(ARDUINO_ARCH_ESP32)
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

struct RoboMemorySnapshot {
  uint32_t internalFree = 0;
  uint32_t internalMinimumFree = 0;
  uint32_t internalLargestBlock = 0;
  uint32_t psramFree = 0;
  uint32_t psramLargestBlock = 0;
};

inline RoboMemorySnapshot roboMemorySnapshot() {
  RoboMemorySnapshot snapshot{};
#if defined(ARDUINO_ARCH_ESP32)
  constexpr uint32_t internalCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  constexpr uint32_t psramCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  snapshot.internalFree = uint32_t(heap_caps_get_free_size(internalCaps));
  snapshot.internalMinimumFree = uint32_t(heap_caps_get_minimum_free_size(internalCaps));
  snapshot.internalLargestBlock = uint32_t(heap_caps_get_largest_free_block(internalCaps));
  snapshot.psramFree = uint32_t(heap_caps_get_free_size(psramCaps));
  snapshot.psramLargestBlock = uint32_t(heap_caps_get_largest_free_block(psramCaps));
#endif
  return snapshot;
}

inline uint32_t roboTaskStackHighWaterBytes(
#if defined(ARDUINO_ARCH_ESP32)
    TaskHandle_t task
#else
    void* task
#endif
) {
#if defined(ARDUINO_ARCH_ESP32)
  return task ? uint32_t(uxTaskGetStackHighWaterMark(task)) : 0u;
#else
  (void)task;
  return 0u;
#endif
}
