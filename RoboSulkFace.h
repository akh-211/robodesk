#pragma once
#include <stdint.h>

// PD-1.7 readable sulk (PRODUCT_DIRECTION sections 2 and 7): low Suspicious face (emotion "annoyed"), eyes turned to
// one side with an occasional glance back, then a gradual recovery shy -> happy. Ethics fence: a sulk is only a face.
// It never shows text, never plays a guilt sound, and never holds back mute, privacy, alarms, reminders or alerts.
namespace robosulk {
struct Gaze {
  bool update = false;
  float x = 0, y = 0;
  uint32_t holdMs = 0;
};

class SulkFace {
 public:
  static constexpr float AwayX = .8f, AwayY = .15f;
  static constexpr uint32_t GlanceEveryMinMs = 6000, GlanceEveryMaxMs = 10000, GlanceMs = 700, RecoverHappyMs = 1500;

  void start(uint32_t now, uint32_t seed) {
    rng_ = seed ? seed : 1u;
    side_ = (next() & 1u) ? 1.f : -1.f;
    active_ = true;
    glancing_ = false;
    recoveryAt_ = 0;
    recoveryStep_ = 0;
    changeAt_ = now;  // turn away right now
  }
  void stop(uint32_t now) {
    if (!active_) return;
    active_ = false;
    recoveryAt_ = now;
    recoveryStep_ = 1;
  }
  bool active() const { return active_; }

  // Gaze to apply while sulking; update=false means keep the current one.
  Gaze gaze(uint32_t now) {
    Gaze g;
    if (!active_ || int32_t(now - changeAt_) < 0) return g;
    g.update = true;
    if (glancing_) {  // back to looking away
      glancing_ = false;
      g.x = side_ * AwayX;
      g.y = AwayY;
      const uint32_t wait = GlanceEveryMinMs + next() % (GlanceEveryMaxMs - GlanceEveryMinMs + 1u);
      g.holdMs = wait;
      changeAt_ = now + wait;
    } else if (changeAt_ == lastAwayAt_) {  // time for a quick peek back at the owner
      glancing_ = true;
      g.x = 0;
      g.y = 0;
      g.holdMs = GlanceMs;
      changeAt_ = now + GlanceMs;
    } else {  // first turn away
      g.x = side_ * AwayX;
      g.y = AwayY;
      const uint32_t wait = GlanceEveryMinMs + next() % (GlanceEveryMaxMs - GlanceEveryMinMs + 1u);
      g.holdMs = wait;
      changeAt_ = now + wait;
    }
    if (!glancing_) lastAwayAt_ = changeAt_;
    return g;
  }
  // Recovery steps after stop(): "shy" at once, "happy" a moment later, then nullptr.
  const char* recovery(uint32_t now) {
    if (recoveryStep_ == 1) { recoveryStep_ = 2; return "shy"; }
    if (recoveryStep_ == 2 && uint32_t(now - recoveryAt_) >= RecoverHappyMs) { recoveryStep_ = 0; return "happy"; }
    return nullptr;
  }

 private:
  uint32_t next() { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return rng_; }
  uint32_t rng_ = 1, changeAt_ = 0, lastAwayAt_ = 0, recoveryAt_ = 0;
  float side_ = 1.f;
  bool active_ = false, glancing_ = false;
  uint8_t recoveryStep_ = 0;
};

// Ethics fence: what a sulk may hold back. Nothing the owner relies on.
enum class Duty : uint8_t { Mute, Privacy, Alarm, Reminder, Notification, WakeWord, Count };
constexpr bool sulkBlocks(Duty) { return false; }
}  // namespace robosulk
