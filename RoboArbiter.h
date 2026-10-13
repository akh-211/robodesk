#pragma once
#include <stdint.h>
#include "CompanionCore.h"

// PD-1.11 behavior arbitration in PRODUCT_DIRECTION section 3 order (lower tier wins):
// 1 IMU safety, 2 owner touch/voice, 3 strong emotion, 4 urgent need (very sleepy, battery critical),
// 5 routine (alarm, reminder, morning greeting), 6 own activity, 7 micro beats.
namespace roboarb {
enum class Tier : uint8_t { Safety = 1, Owner, Emotion, Urgent, Routine, Activity, Micro };
// A preempted clip is never hard-cut: the next clip starts from the current pose, easing within this budget.
constexpr uint16_t InterruptEaseMs = 150;

inline Tier tierOf(companion::Priority p) {
  switch (p) {
    case companion::Priority::Recovery: return Tier::Safety;
    case companion::Priority::User: return Tier::Owner;
    case companion::Priority::Sensor: return Tier::Owner;  // touch/motion feedback answers the owner's own action
    case companion::Priority::Reminder: return Tier::Routine;
    case companion::Priority::Proactive: return Tier::Activity;
    case companion::Priority::Ambient: return Tier::Micro;
  }
  return Tier::Micro;
}
// LivingEyes clip priority: a playing clip only yields to an equal or higher value.
inline uint8_t clipPriority(Tier t) { return uint8_t(8u - uint8_t(t)); }

enum class Decision : uint8_t { Start, Interrupt, Defer, Drop };
struct Running {
  bool active = false;
  Tier tier = Tier::Micro;
};
inline Decision decide(Tier incoming, const Running& running) {
  if (!running.active) return Decision::Start;
  if (incoming < running.tier) return Decision::Interrupt;
  if (incoming == running.tier) return incoming <= Tier::Owner ? Decision::Interrupt : Decision::Defer;  // newest touch wins
  return incoming <= Tier::Routine ? Decision::Defer : Decision::Drop;  // alarms wait; idle beats are not queued up
}
inline bool admits(Decision d) { return d == Decision::Start || d == Decision::Interrupt; }
// Bit i set = companion::Priority(i) may start now (Actions::pop mask). Deferred actions stay queued until they expire.
inline uint8_t admitMask(const Running& running) {
  uint8_t mask = 0;
  for (uint8_t i = 0; i <= uint8_t(companion::Priority::Ambient); ++i)
    if (admits(decide(tierOf(companion::Priority(i)), running))) mask = uint8_t(mask | (1u << i));
  return mask;
}

// What is on screen now, as started by the firmware. Expires on its own so a lost "end" never blocks forever.
class Tracker {
 public:
  void start(Tier tier, uint32_t now, uint32_t holdMs) { tier_ = tier; until_ = now + holdMs; active_ = holdMs != 0; }
  void clear() { active_ = false; }
  Running running(uint32_t now) {
    if (active_ && int32_t(now - until_) >= 0) active_ = false;
    Running r;
    r.active = active_;
    r.tier = tier_;
    return r;
  }

 private:
  Tier tier_ = Tier::Micro;
  uint32_t until_ = 0;
  bool active_ = false;
};
}  // namespace roboarb
