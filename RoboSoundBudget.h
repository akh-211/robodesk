#pragma once
#include <stdint.h>

// PD-1.8a spontaneous sound budget (PRODUCT_DIRECTION section 8): at most 6 unprompted sounds in any rolling hour
// and none during quiet hours. Sounds the user triggers are not counted (they keep the director's quietGain).
namespace robosound {
class SoundBudget {
 public:
  static constexpr uint8_t MaxPerHour = 6;
  static constexpr uint32_t WindowMs = 3600000;

  bool allow(uint32_t now, bool quietHours) const {
    if (quietHours) return false;
    uint8_t recent = 0;
    for (uint8_t i = 0; i < count_; ++i)
      if (uint32_t(now - at_[i]) < WindowMs) ++recent;
    return recent < MaxPerHour;
  }
  void commit(uint32_t now) {
    at_[next_] = now;
    next_ = uint8_t((next_ + 1) % MaxPerHour);
    if (count_ < MaxPerHour) ++count_;
  }
  uint8_t usedLastHour(uint32_t now) const {
    uint8_t recent = 0;
    for (uint8_t i = 0; i < count_; ++i)
      if (uint32_t(now - at_[i]) < WindowMs) ++recent;
    return recent;
  }

 private:
  uint32_t at_[MaxPerHour] = {};
  uint8_t next_ = 0, count_ = 0;
};
}  // namespace robosound
