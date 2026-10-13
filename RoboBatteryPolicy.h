#pragma once
#include <stdint.h>

// PD-1.9 low/critical battery policy with hysteresis. Pure; the sketch feeds RoboBatteryMonitor's smoothed value.
// Samples taken while the speaker drives (or just after) are ignored: a voltage sag must never look like a flat cell.
// Low fires once per discharge cycle; it re-arms only after the cell is clearly recharged.
namespace robobattery {
enum class Level : uint8_t { Ok, Low, Critical };
enum class Event : uint8_t { None, Low, Critical, Recovered };

class Policy {
 public:
  static constexpr uint16_t LowMv = 3500, CriticalMv = 3300, LowClearMv = 3800, CriticalClearMv = 3600;
  static constexpr uint32_t HoldMs = 30000, ClearHoldMs = 60000, SagGuardMs = 2000;

  Event sample(uint32_t now, bool valid, uint16_t mv, bool speakerActive) {
    if (speakerActive) { speakerAt_ = now; speakerSeen_ = true; }
    if (!valid || speakerActive || (speakerSeen_ && uint32_t(now - speakerAt_) < SagGuardMs)) { resetTimers(); return Event::None; }
    Event e = Event::None;
    if (level_ != Level::Critical && mv <= CriticalMv) {
      if (hold(criticalSince_, now) >= HoldMs) { level_ = Level::Critical; e = Event::Critical; lowFired_ = true; }
    } else criticalSince_ = 0;
    if (e == Event::None && level_ == Level::Ok && mv <= LowMv) {
      if (hold(lowSince_, now) >= HoldMs) { level_ = Level::Low; if (!lowFired_) { lowFired_ = true; e = Event::Low; } }
    } else if (level_ == Level::Ok) lowSince_ = 0;
    const uint16_t clearMv = level_ == Level::Critical ? CriticalClearMv : LowClearMv;
    if (e == Event::None && level_ != Level::Ok && mv >= clearMv) {
      if (hold(clearSince_, now) >= ClearHoldMs) {
        const bool recharged = mv >= LowClearMv;
        level_ = recharged ? Level::Ok : Level::Low;
        if (recharged) lowFired_ = false;  // a new discharge cycle may warn again
        clearSince_ = 0;
        e = Event::Recovered;
      }
    } else clearSince_ = 0;
    return e;
  }
  Level level() const { return level_; }

 private:
  static uint32_t hold(uint32_t& since, uint32_t now) { if (!since) since = now ? now : 1u; return uint32_t(now - since); }
  void resetTimers() { lowSince_ = criticalSince_ = clearSince_ = 0; }
  Level level_ = Level::Ok;
  bool lowFired_ = false, speakerSeen_ = false;
  uint32_t lowSince_ = 0, criticalSince_ = 0, clearSince_ = 0, speakerAt_ = 0;
};
}  // namespace robobattery
