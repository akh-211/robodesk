#pragma once
#include <stdint.h>

// PD-2.2 milestones and "missed you" (PRODUCT_DIRECTION section 9). Pure; the sketch persists State in NVS at most
// once per UTC day (saveDue). Day 7/30/100/365 celebrate once each. "Missed you" needs >=48 h since the saved
// last-seen stamp: that stamp may be up to a day old, so 48 h guarantees the robot really was off for >=24 h.
namespace robomilestone {
constexpr uint32_t MinEpoch = 1700000000u, Day = 86400u, MissedGap = 2u * Day;
enum class Event : uint8_t { None, Missed, Day7, Day30, Day100, Day365 };

struct State {
  uint32_t firstEpoch = 0, lastSeenEpoch = 0;
  uint8_t celebrated = 0;  // bit0 7, bit1 30, bit2 100, bit3 365
};

class Tracker {
 public:
  void begin(const State& saved) { s_ = saved; bootChecked_ = false; dirty_ = false; savedDay_ = saved.lastSeenEpoch / Day; }
  // epoch = 0 while the clock is not synced. Returns at most one event per call.
  Event onClock(uint32_t epoch) {
    if (epoch < MinEpoch) return Event::None;
    if (!s_.firstEpoch) { s_.firstEpoch = epoch; dirty_ = true; }
    Event e = Event::None;
    if (!bootChecked_) {
      bootChecked_ = true;
      if (s_.lastSeenEpoch && epoch > s_.lastSeenEpoch && epoch - s_.lastSeenEpoch >= MissedGap) e = Event::Missed;
    }
    if (epoch > s_.lastSeenEpoch) { s_.lastSeenEpoch = epoch; dirty_ = true; }  // a clock that jumps back never rewinds it
    if (e != Event::None) return e;
    const uint32_t age = epoch > s_.firstEpoch ? (epoch - s_.firstEpoch) / Day : 0;
    static const uint16_t days[4] = {7, 30, 100, 365};
    for (int i = 3; i >= 0; --i) {
      if (age >= days[i] && !(s_.celebrated & (1u << i))) {
        s_.celebrated = uint8_t(s_.celebrated | ((1u << (i + 1)) - 1u));  // also retire missed smaller ones
        dirty_ = true;
        return Event(uint8_t(Event::Day7) + uint8_t(i));
      }
    }
    return Event::None;
  }
  // True at most once per UTC day while there is something new to store.
  bool saveDue(uint32_t epoch) const { return dirty_ && epoch >= MinEpoch && epoch / Day != savedDay_; }
  void saved(uint32_t epoch) { dirty_ = false; savedDay_ = epoch / Day; }
  const State& state() const { return s_; }
  uint32_t ageDays(uint32_t epoch) const { return s_.firstEpoch && epoch > s_.firstEpoch ? (epoch - s_.firstEpoch) / Day : 0; }

 private:
  State s_;
  bool bootChecked_ = false, dirty_ = false;
  uint32_t savedDay_ = 0;
};
}  // namespace robomilestone
