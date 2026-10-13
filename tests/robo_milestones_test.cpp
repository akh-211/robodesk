#include "../RoboMilestones.h"

#include <cassert>
#include <iostream>

using robomilestone::Day;
using robomilestone::Event;
using robomilestone::State;
using robomilestone::Tracker;

namespace {
constexpr uint32_t T0 = 1800000000u;
}

int main() {
  // First life: no event, first epoch recorded, one save that day.
  Tracker t;
  t.begin(State());
  assert(t.onClock(0) == Event::None && t.state().firstEpoch == 0);  // clock not synced yet
  assert(t.onClock(T0) == Event::None && t.state().firstEpoch == T0);
  assert(t.saveDue(T0));
  t.saved(T0);
  assert(!t.saveDue(T0 + 3600));  // at most once per day
  assert(t.onClock(T0 + Day) == Event::None && t.saveDue(T0 + Day));
  // Day 7 once, even across a reboot after it was saved.
  unsigned d7 = 0;
  for (uint32_t h = 0; h < 8 * 24; ++h) { const uint32_t e = T0 + h * 3600u; if (t.onClock(e) == Event::Day7) ++d7; if (t.saveDue(e)) t.saved(e); }
  assert(d7 == 1);
  Tracker r;
  r.begin(t.state());
  assert(r.onClock(T0 + 8 * Day) == Event::None);
  // Clock jumping backwards: no rewind, no negative age, no events.
  assert(r.onClock(T0 - 30 * Day) == Event::None && r.state().lastSeenEpoch >= T0 + 8 * Day);
  assert(r.ageDays(T0 - 30 * Day) == 0);
  // Off for 3 days -> "missed you" once at boot; a short power cut does not.
  State s = r.state();
  Tracker back;
  back.begin(s);
  assert(back.onClock(s.lastSeenEpoch + 3 * Day) == Event::Missed);
  assert(back.onClock(s.lastSeenEpoch + 3 * Day + 60) != Event::Missed);
  Tracker brief;
  brief.begin(s);
  assert(brief.onClock(s.lastSeenEpoch + 30 * 3600u) == Event::None);  // <48 h since the (possibly day-old) stamp
  // Long gap past several milestones: celebrate the biggest reached, retire the smaller ones.
  State old;
  old.firstEpoch = T0;
  old.lastSeenEpoch = T0 + 5 * Day;
  Tracker leap;
  leap.begin(old);
  assert(leap.onClock(T0 + 120 * Day) == Event::Missed);
  assert(leap.onClock(T0 + 120 * Day + 1) == Event::Day100);
  assert(leap.onClock(T0 + 120 * Day + 2) == Event::None);
  assert(leap.onClock(T0 + 366 * Day) == Event::Day365);
  assert(leap.state().celebrated == 0x0f);
  std::cout << "PASS: milestones 7/30/100/365 once, missed-you after >=48 h stamp gap, reboot, clock backwards, unsynced, 1 save/day\n";
  return 0;
}
