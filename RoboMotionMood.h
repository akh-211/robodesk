#pragma once
#include <stdint.h>

// PD-1.6 IMU moods on top of LivingEyes' own motion reactions (tilt moves the eye pair, shake = Nervous spiral):
// shaken 3x within 30 s -> annoyed; fall -> shock, then a little sad (shorter when petted); put down -> relief.
namespace robomotion {
enum class Event : uint8_t { None, Shake, Annoyed, Fall, FallSad, PutDown, Recovered };

class MotionMood {
 public:
  static constexpr uint32_t ShakeWindowMs = 30000, ShakeCooldownMs = 1100, FallCooldownMs = 2200;
  static constexpr uint32_t FallSadDelayMs = 1500, SadMs = 20000, PettedSadMs = 3000;
  static constexpr uint8_t AnnoyedShakes = 3;

  // One IMU sample: |g|, |gyro| (rad/s), the adapter's picked-up flag and the adapter thresholds.
  Event sample(uint32_t now, float g, float gyro, bool picked, float fallG, float shakeGyro) {
    Event e = Event::None;
    if (g < fallG && (!fallAt_ || uint32_t(now - fallAt_) >= FallCooldownMs)) {
      fallAt_ = now;
      sadPending_ = true;
      sad_ = false;
      e = Event::Fall;
    } else if (gyro > shakeGyro && (!shakeAt_ || uint32_t(now - shakeAt_) >= ShakeCooldownMs)) {
      shakeAt_ = now;
      if (shakes_ && uint32_t(now - firstShakeAt_) <= ShakeWindowMs) ++shakes_;
      else { shakes_ = 1; firstShakeAt_ = now; }
      if (shakes_ >= AnnoyedShakes) { shakes_ = 0; e = Event::Annoyed; }
      else e = Event::Shake;
    } else if (picked_ && !picked) {
      e = Event::PutDown;
    }
    picked_ = picked;
    return e;
  }
  Event update(uint32_t now) {
    if (sadPending_ && uint32_t(now - fallAt_) >= FallSadDelayMs) {
      sadPending_ = false;
      sad_ = true;
      sadUntil_ = now + SadMs;
      return Event::FallSad;
    }
    if (sad_ && int32_t(now - sadUntil_) >= 0) { sad_ = false; return Event::Recovered; }
    return Event::None;
  }
  // Petting while sad after a fall shortens the sadness.
  void pet(uint32_t now) {
    if (sad_ && int32_t(sadUntil_ - (now + PettedSadMs)) > 0) sadUntil_ = now + PettedSadMs;
  }
  bool sad() const { return sad_; }

 private:
  uint32_t fallAt_ = 0, shakeAt_ = 0, firstShakeAt_ = 0, sadUntil_ = 0;
  uint8_t shakes_ = 0;
  bool picked_ = false, sadPending_ = false, sad_ = false;
};
}  // namespace robomotion
