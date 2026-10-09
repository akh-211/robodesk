#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#if defined(ARDUINO_ARCH_ESP32)
#include <esp_heap_caps.h>
#endif

template <size_t Capacity>
class RoboPsramByteRing {
  static_assert(Capacity > 1, "RoboPsramByteRing capacity must exceed 1 byte");
  uint8_t* data_ = nullptr;
  size_t head_ = 0;
  size_t size_ = 0;
  uint32_t dropped_ = 0;
  bool inPsram_ = false;

public:
  bool begin() {
    if (data_) return true;
#if defined(ARDUINO_ARCH_ESP32)
    data_ = static_cast<uint8_t*>(heap_caps_malloc(Capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    inPsram_ = data_ != nullptr;
    if (!data_) data_ = static_cast<uint8_t*>(heap_caps_malloc(Capacity, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
#else
    data_ = static_cast<uint8_t*>(malloc(Capacity));
#endif
    return data_ != nullptr;
  }

  ~RoboPsramByteRing() {
#if defined(ARDUINO_ARCH_ESP32)
    if (data_) heap_caps_free(data_);
#else
    ::free(data_);
#endif
  }

  bool ready() const { return data_ != nullptr; }
  bool inPsram() const { return inPsram_; }
  void clear() { head_ = 0; size_ = 0; }
  size_t size() const { return size_; }
  size_t free() const { return Capacity - size_; }
  constexpr size_t capacity() const { return Capacity; }
  uint32_t dropped() const { return dropped_; }

  size_t write(const uint8_t* src, size_t n) {
    if (!data_ || !src || !n) return 0;
    const size_t accepted = n < free() ? n : free();
    for (size_t i = 0; i < accepted; ++i) data_[(head_ + size_ + i) % Capacity] = src[i];
    size_ += accepted;
    if (accepted < n) dropped_ += uint32_t(n - accepted);
    return accepted;
  }

  size_t read(uint8_t* dst, size_t n) {
    if (!data_ || !dst || !n || !size_) return 0;
    const size_t count = n < size_ ? n : size_;
    for (size_t i = 0; i < count; ++i) dst[i] = data_[(head_ + i) % Capacity];
    head_ = (head_ + count) % Capacity;
    size_ -= count;
    return count;
  }

  size_t discard(size_t n) {
    const size_t count = n < size_ ? n : size_;
    head_ = (head_ + count) % Capacity;
    size_ -= count;
    return count;
  }
};
