#include "../RoboDailyRhythm.h"

#include <cassert>
#include <iostream>

using roborhythm::DailyRhythm;

namespace {
// Quiet 22:00-07:00. Simulates one day in 1-minute steps with PIR rises at the given minutes; returns greetings.
unsigned day(DailyRhythm& r, uint32_t dayIndex, bool synced, const uint16_t* pirs, unsigned n) {
  unsigned greetings = 0;
  for (uint16_t m = 0; m < 1440; ++m) {
    const bool quiet = m >= 22 * 60 || m < 7 * 60;
    bool pir = false;
    for (unsigned i = 0; i < n; ++i) pir = pir || pirs[i] == m;
    if (r.morning(synced, m, dayIndex, quiet, pir)) {
      ++greetings;
      assert(!quiet && m >= 7 * 60 && m < 12 * 60);
    }
  }
  return greetings;
}
}  // namespace

int main() {
  DailyRhythm r;
  const uint16_t busy[] = {3 * 60, 7 * 60 + 10, 7 * 60 + 30, 9 * 60, 20 * 60};
  assert(day(r, 100, true, busy, 5) == 1);  // once per day, first PIR after quiet ends (03:00 PIR ignored)
  assert(r.greetedToday(100));
  assert(day(r, 101, true, busy, 5) == 1);  // next day again
  const uint16_t late[] = {13 * 60};
  assert(day(r, 102, true, late, 1) == 0);  // first PIR after noon: no stale greeting
  assert(day(r, 103, false, busy, 5) == 0);  // clock not synced: never
  // Reboot mid-morning without having seen the quiet window: no greeting that day, normal the next.
  DailyRhythm fresh;
  const uint16_t mid[] = {9 * 60};
  unsigned g = 0;
  for (uint16_t m = 8 * 60; m < 1440; ++m) g += fresh.morning(true, m, 200, m >= 22 * 60, m == mid[0]) ? 1u : 0u;
  assert(g == 0);
  assert(day(fresh, 201, true, busy, 5) == 1);
  // Evening helpers.
  assert(roborhythm::evening(true, 19 * 60, false) && !roborhythm::evening(true, 19 * 60, true));
  assert(!roborhythm::evening(false, 19 * 60, false) && !roborhythm::evening(true, 10 * 60, false));
  assert(roborhythm::EveningEnergyScale < 1.f && roborhythm::EveningSoundGain < 1.f);
  std::cout << "PASS: morning greeting once per day after quiet hours (synced clock, before noon), evening softening\n";
  return 0;
}
