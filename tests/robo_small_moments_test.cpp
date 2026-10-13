#include "../RoboSmallMoments.h"

#include <cassert>
#include <cstring>
#include <iostream>

using namespace robomoments;

int main() {
  // Dreams: rare (45-90 min apart), 1-2 s, only in deep sleep.
  for (uint32_t start : {0u, 0xFFFFFFFFu - 3600000u}) {
    Dreams d(99);
    unsigned dreams = 0;
    uint32_t last = 0;
    for (uint32_t t = 0; t < 8u * 3600000u; t += 1000) {
      const uint32_t len = d.update(start + t, true);
      if (!len) continue;
      assert(len >= Dreams::MinDreamMs && len <= Dreams::MaxDreamMs);
      if (last) assert(t - last >= Dreams::MinGapMs && t - last <= Dreams::MaxGapMs + 1000);
      last = t;
      ++dreams;
    }
    assert(dreams >= 5 && dreams <= 10);
  }
  Dreams awake(5);
  for (uint32_t t = 0; t < 4u * 3600000u; t += 1000) assert(awake.update(t, false) == 0);
  // Weekly looks: 7 different looks in a week, reset next week.
  WeeklyLooks w;
  uint8_t seen = 0;
  for (uint32_t day = 700; day < 707; ++day) { const uint8_t i = w.pick(day, day * 2654435761u); assert(!(seen & (1u << i))); seen = uint8_t(seen | (1u << i)); }
  assert(seen == 0x7f);
  (void)w.pick(707, 1);  // new week starts fresh
  assert(morningLook(0) && strlen(morningLook(6)) > 0);
  // Birthday once a year, synced clock only, Feb 29 handled.
  Birthday b;
  assert(!b.due(true, 2026, 5, 17, 0, 0));
  assert(!b.due(false, 2026, 5, 17, 5, 17));
  assert(b.due(true, 2026, 5, 17, 5, 17));
  b.celebrated(2026);
  assert(!b.due(true, 2026, 5, 17, 5, 17));  // reboot the same day after saving
  assert(b.due(true, 2027, 5, 17, 5, 17));
  assert(b.due(true, 2027, 2, 28, 2, 29) && !b.due(true, 2028, 2, 28, 2, 29) && b.due(true, 2028, 2, 29, 2, 29));
  assert(!b.due(true, 2027, 5, 17, 13, 17));
  std::cout << "PASS: rare 1-2 s dreams in deep sleep, weekly unused morning looks, owner birthday once per year\n";
  return 0;
}
