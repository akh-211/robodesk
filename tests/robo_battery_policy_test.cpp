#include "../RoboBatteryPolicy.h"

#include <cassert>
#include <iostream>

using robobattery::Event;
using robobattery::Level;
using robobattery::Policy;

namespace {
// Feeds a constant voltage for `ms` (1 s samples); returns the first non-None event.
Event hold(Policy& p, uint32_t& now, uint32_t ms, uint16_t mv, bool speaker = false) {
  Event first = Event::None;
  for (uint32_t t = 0; t < ms; t += 1000) { now += 1000; const Event e = p.sample(now, true, mv, speaker); if (first == Event::None) first = e; }
  return first;
}
}  // namespace

int main() {
  Policy p;
  uint32_t now = 0;
  assert(hold(p, now, 120000, 3900) == Event::None && p.level() == Level::Ok);
  // Speaker sag: deep dips while the speaker plays never count.
  assert(hold(p, now, 120000, 3100, true) == Event::None && p.level() == Level::Ok);
  assert(hold(p, now, 1500, 3100) == Event::None);  // the sag guard right after playback
  assert(hold(p, now, 20000, 3900) == Event::None);
  // A short dip (<30 s) is ignored.
  assert(hold(p, now, 20000, 3450) == Event::None && p.level() == Level::Ok);
  assert(hold(p, now, 5000, 3700) == Event::None);
  // Sustained low: warns once.
  assert(hold(p, now, 31000, 3450) == Event::Low && p.level() == Level::Low);
  assert(hold(p, now, 600000, 3450) == Event::None);
  // Hovering around the threshold does not re-warn (hysteresis): only a real recharge re-arms.
  assert(hold(p, now, 30000, 3600) == Event::None);
  assert(hold(p, now, 31000, 3450) == Event::None && p.level() == Level::Low);
  // Critical.
  assert(hold(p, now, 31000, 3250) == Event::Critical && p.level() == Level::Critical);
  assert(hold(p, now, 120000, 3250) == Event::None);
  // Recharge to 3.65 V: out of critical but still low; no new low warning.
  assert(hold(p, now, 61000, 3650) == Event::Recovered && p.level() == Level::Low);
  // Full recharge re-arms the warning for the next discharge cycle.
  assert(hold(p, now, 61000, 3950) == Event::Recovered && p.level() == Level::Ok);
  assert(hold(p, now, 31000, 3450) == Event::Low);
  // Invalid readings never trigger.
  Policy q;
  uint32_t t = 0;
  for (int i = 0; i < 100; ++i) { t += 1000; assert(q.sample(t, false, 0, false) == Event::None); }
  assert(q.level() == Level::Ok);
  std::cout << "PASS: battery low once per discharge cycle, critical, hysteresis, speaker-sag guard, invalid readings ignored\n";
  return 0;
}
