#pragma once
#include <stdint.h>
#include "RoboSleepCycle.h"

// PD-1.3 OLED burn-in protection. Pure plan: SSD1306 contrast (0x81), panel on/off (0xAF/0xAE) and a slow
// 1-2 px eye shift (FaceLayoutProfile::eyeYOffset). The sketch applies only changes and resends a full frame
// after the panel comes back on, because the delta presenter only transfers differences.
namespace robooled {
constexpr uint8_t ContrastDay = 0xCF, ContrastNight = 0x40, ContrastDrowsy = 0x30, ContrastSleep = 0x08, ContrastOverlay = 0x8F;
constexpr uint32_t ShiftStepMinutes = 3;

struct Plan {
  uint8_t contrast = ContrastDay;
  bool on = true;
  int8_t eyeYOffset = 0;
  bool operator==(const Plan& o) const { return contrast == o.contrast && on == o.on && eyeYOffset == o.eyeYOffset; }
  bool operator!=(const Plan& o) const { return !(*this == o); }
};

// minutes: any monotonic minute counter (uptime is fine). Pattern 0,1,2,1,0,-1,-2,-1 keeps the mean at 0.
inline int8_t eyeShift(uint32_t minutes) {
  static const int8_t pattern[8] = {0, 1, 2, 1, 0, -1, -2, -1};
  return pattern[(minutes / ShiftStepMinutes) % 8u];
}

// night: quiet hours with a synced clock. overlay: pairing passkey, notification, alert or any banner that must be read.
inline Plan plan(robosleep::State state, bool night, uint32_t minutes, bool overlay) {
  Plan p;
  p.eyeYOffset = eyeShift(minutes);
  switch (state) {
    case robosleep::State::Awake:
    case robosleep::State::Waking: p.contrast = night ? ContrastNight : ContrastDay; break;
    case robosleep::State::Drowsy: p.contrast = ContrastDrowsy; break;
    case robosleep::State::LightSleep: p.contrast = ContrastSleep; break;
    case robosleep::State::DeepSleep: p.contrast = ContrastSleep; p.on = false; break;
  }
  if (overlay) {
    p.on = true;
    if (p.contrast < ContrastOverlay) p.contrast = ContrastOverlay;
  }
  return p;
}

// Remembers what the panel has, so commands go out only on change.
class Applier {
 public:
  struct Step {
    bool sendContrast = false, sendPower = false, fullFrame = false, layout = false;
  };
  Step next(const Plan& want) {
    Step s;
    if (!known_ || want.contrast != have_.contrast) s.sendContrast = true;
    if (!known_ || want.on != have_.on) {
      s.sendPower = true;
      s.fullFrame = want.on;  // GDDRAM may be stale versus the delta presenter's shadow; resend everything
    }
    if (!known_ || want.eyeYOffset != have_.eyeYOffset) s.layout = true;
    have_ = want;
    known_ = true;
    return s;
  }
  void forget() { known_ = false; }

 private:
  Plan have_;
  bool known_ = false;
};
}  // namespace robooled
