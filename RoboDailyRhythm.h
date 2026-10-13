#pragma once
#include <stdint.h>

// PD-2.1 daily rhythm (PRODUCT_DIRECTION sections 5 and 9): one morning greeting a day, on the first PIR after the
// quiet window ends (synced clock only, until noon); evenings drain energy faster and soften sounds.
namespace roborhythm {
constexpr uint16_t EveningStartMin = 18 * 60, MorningEndMin = 12 * 60;
constexpr float EveningEnergyScale = .8f, EveningSoundGain = .6f;

inline bool evening(bool clockSynced, uint16_t minuteOfDay, bool quietHours) {
  return clockSynced && !quietHours && minuteOfDay >= EveningStartMin;
}

class DailyRhythm {
 public:
  // Call every loop. Returns true exactly when the morning greeting should start.
  bool morning(bool clockSynced, uint16_t minuteOfDay, uint32_t dayIndex, bool quietHours, bool pirRising) {
    if (!clockSynced) return false;  // never guess the time of day
    if (quietHours) { armed_ = true; return false; }
    if (!armed_) return false;
    if (minuteOfDay >= MorningEndMin) { armed_ = false; return false; }  // nobody came by noon: no stale "good morning"
    if (!pirRising || (greeted_ && greetedDay_ == dayIndex)) return false;
    armed_ = false;
    greeted_ = true;
    greetedDay_ = dayIndex;
    return true;
  }
  bool greetedToday(uint32_t dayIndex) const { return greeted_ && greetedDay_ == dayIndex; }

 private:
  bool armed_ = false, greeted_ = false;
  uint32_t greetedDay_ = 0;
};
}  // namespace roborhythm
