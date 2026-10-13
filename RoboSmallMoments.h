#pragma once
#include <stdint.h>

// PD-2.6 small moments (PRODUCT_DIRECTION sections 5 and 9): rare dreams in deep sleep (eyes tremble 1-2 s),
// a morning look not yet used this week, and the owner's birthday once a year.
namespace robomoments {
class Dreams {
 public:
  static constexpr uint32_t MinGapMs = 45u * 60u * 1000u, MaxGapMs = 90u * 60u * 1000u, MinDreamMs = 1000, MaxDreamMs = 2000;
  explicit Dreams(uint32_t seed = 0x2545f491u) : rng_(seed ? seed : 1u) {}
  // Call while asleep; returns a dream length in ms when one should start now, else 0.
  uint32_t update(uint32_t now, bool deepSleep) {
    if (!deepSleep) { armed_ = false; return 0; }
    if (!armed_) { armed_ = true; nextAt_ = now + MinGapMs + next() % (MaxGapMs - MinGapMs + 1u); return 0; }
    if (int32_t(now - nextAt_) < 0) return 0;
    nextAt_ = now + MinGapMs + next() % (MaxGapMs - MinGapMs + 1u);
    return MinDreamMs + next() % (MaxDreamMs - MinDreamMs + 1u);
  }

 private:
  uint32_t next() { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return rng_; }
  uint32_t rng_, nextAt_ = 0;
  bool armed_ = false;
};

// Morning looks (RoboEmotionMap names). Each week uses each one at most once before repeating.
constexpr uint8_t MorningLookCount = 7;
inline const char* morningLook(uint8_t i) {
  static const char* const looks[MorningLookCount] = {"happy", "curious", "excited", "love", "wink", "calm", "laugh"};
  return looks[i % MorningLookCount];
}
class WeeklyLooks {
 public:
  // dayIndex: local day number. Returns the index of a look not used this week.
  uint8_t pick(uint32_t dayIndex, uint32_t random) {
    const uint32_t week = dayIndex / 7u;
    if (week != week_) { week_ = week; used_ = 0; }
    const uint8_t all = uint8_t((1u << MorningLookCount) - 1u);
    if ((used_ & all) == all) used_ = 0;
    uint8_t free[MorningLookCount], n = 0;
    for (uint8_t i = 0; i < MorningLookCount; ++i) if (!(used_ & (1u << i))) free[n++] = i;
    const uint8_t pick = free[random % n];
    used_ = uint8_t(used_ | (1u << pick));
    return pick;
  }

 private:
  uint32_t week_ = 0xffffffffu;
  uint8_t used_ = 0;
};

// Owner birthday (month 1-12, day 1-31; 0 = not set). Feb 29 is celebrated on Feb 28 in non-leap years.
inline bool leap(uint16_t year) { return (year % 4u == 0 && year % 100u != 0) || year % 400u == 0; }
class Birthday {
 public:
  bool due(bool clockSynced, uint16_t year, uint8_t month, uint8_t day, uint8_t birthMonth, uint8_t birthDay) const {
    if (!clockSynced || !birthMonth || !birthDay || birthMonth > 12 || birthDay > 31 || year == lastYear_) return false;
    uint8_t bd = birthDay;
    if (birthMonth == 2 && birthDay == 29 && !leap(year)) bd = 28;
    return month == birthMonth && day == bd;
  }
  void celebrated(uint16_t year) { lastYear_ = year; }
  uint16_t lastYear() const { return lastYear_; }
  void restore(uint16_t year) { lastYear_ = year; }

 private:
  uint16_t lastYear_ = 0;
};
}  // namespace robomoments
