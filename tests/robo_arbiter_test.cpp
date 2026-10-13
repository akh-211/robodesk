#include "../RoboArbiter.h"

#include <cassert>
#include <iostream>

using companion::Priority;
using roboarb::Decision;
using roboarb::Running;
using roboarb::Tier;

namespace {
Running on(Tier t) { Running r; r.active = true; r.tier = t; return r; }
uint8_t bit(Priority p) { return uint8_t(1u << uint8_t(p)); }
}  // namespace

int main() {
  // Section 3 order: safety > owner > emotion > urgent > routine > activity > micro.
  assert(Tier::Safety < Tier::Owner && Tier::Owner < Tier::Emotion && Tier::Emotion < Tier::Urgent &&
         Tier::Urgent < Tier::Routine && Tier::Routine < Tier::Activity && Tier::Activity < Tier::Micro);
  assert(roboarb::tierOf(Priority::Recovery) == Tier::Safety);
  assert(roboarb::tierOf(Priority::User) == Tier::Owner && roboarb::tierOf(Priority::Sensor) == Tier::Owner);
  assert(roboarb::tierOf(Priority::Reminder) == Tier::Routine);
  assert(roboarb::tierOf(Priority::Proactive) == Tier::Activity && roboarb::tierOf(Priority::Ambient) == Tier::Micro);
  // Clip priorities keep the same order inside LivingEyes (higher value preempts).
  for (uint8_t a = 1; a < 7; ++a) assert(roboarb::clipPriority(Tier(a)) > roboarb::clipPriority(Tier(a + 1)));
  assert(roboarb::clipPriority(Tier::Micro) >= 1);

  // Idle screen: anything starts.
  for (uint8_t t = 1; t <= 7; ++t) assert(roboarb::decide(Tier(t), Running()) == Decision::Start);
  // Higher tier interrupts (smoothly) whatever runs below it.
  assert(roboarb::decide(Tier::Safety, on(Tier::Owner)) == Decision::Interrupt);
  assert(roboarb::decide(Tier::Owner, on(Tier::Activity)) == Decision::Interrupt);
  assert(roboarb::decide(Tier::Urgent, on(Tier::Routine)) == Decision::Interrupt);
  // A newer touch replaces the previous touch reaction; a second activity waits.
  assert(roboarb::decide(Tier::Owner, on(Tier::Owner)) == Decision::Interrupt);
  assert(roboarb::decide(Tier::Activity, on(Tier::Activity)) == Decision::Defer);
  // Lower tiers never cut in: routine waits, idle beats are dropped.
  assert(roboarb::decide(Tier::Routine, on(Tier::Owner)) == Decision::Defer);
  assert(roboarb::decide(Tier::Owner, on(Tier::Safety)) == Decision::Defer);
  assert(roboarb::decide(Tier::Activity, on(Tier::Emotion)) == Decision::Drop);
  assert(roboarb::decide(Tier::Micro, on(Tier::Urgent)) == Decision::Drop);
  static_assert(roboarb::InterruptEaseMs <= 150, "interrupt ease budget");

  // Queue admission masks.
  assert(roboarb::admitMask(Running()) == 0x3f);
  const uint8_t duringTouch = roboarb::admitMask(on(Tier::Owner));
  assert(duringTouch == (bit(Priority::Recovery) | bit(Priority::User) | bit(Priority::Sensor)));
  const uint8_t duringActivity = roboarb::admitMask(on(Tier::Activity));
  assert(duringActivity == (bit(Priority::Recovery) | bit(Priority::User) | bit(Priority::Sensor) | bit(Priority::Reminder)));
  assert(roboarb::admitMask(on(Tier::Safety)) == bit(Priority::Recovery));

  // Actions::pop honours the mask: deferred tiers stay queued, expired ones vanish.
  {
    companion::Actions actions;
    companion::Action a;
    assert(actions.push(companion::ActionKind::Expression, Priority::Proactive, "curious", .5f, 0, 5000));
    assert(actions.push(companion::ActionKind::Expression, Priority::Reminder, "happy", .5f, 0, 5000));
    assert(actions.push(companion::ActionKind::Sound, Priority::Sensor, "affection", .5f, 0, 5000));
    assert(actions.pop(10, false, a, duringTouch) && a.priority == Priority::Sensor);
    assert(!actions.pop(10, false, a, duringTouch));
    assert(actions.pop(10, false, a, duringActivity) && a.priority == Priority::Reminder);
    assert(!actions.pop(10, false, a, duringActivity));
    assert(!actions.pop(6000, false, a));  // the deferred activity expired while it waited
  }
  // Tracker expires by itself, millis-wrap safe.
  roboarb::Tracker tracker;
  assert(!tracker.running(0).active);
  tracker.start(Tier::Emotion, 0xFFFFFF00u, 0x200);
  assert(tracker.running(0xFFFFFFF0u).active && tracker.running(0x50).active);
  assert(tracker.running(0x50).tier == Tier::Emotion);
  assert(!tracker.running(0x100).active);
  tracker.start(Tier::Owner, 10, 100);
  tracker.clear();
  assert(!tracker.running(20).active);
  std::cout << "PASS: arbitration tiers, interrupt/defer/drop, queue admission and tracker expiry\n";
  return 0;
}
